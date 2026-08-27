/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Utf16String.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/Object.h>
#include <LibJS/Runtime/Realm.h>
#include <LibJS/Runtime/VM.h>
#include <LibJS/Runtime/Value.h>
#include <LibTH8/Interpreter.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/Scripting/TH8Context.h>
#include <LibWeb/TH8/WindowTH8Namespace.h>

namespace Web::TH8 {

// window.th8.eval(scriptText) -> string
//
// Synchronous JS->TH8 evaluation, the mirror of TH8's `dom::eval_js`
// command (see DOMBridge.cpp).  Both directions gate on the document's
// `cross-eval` policy; both directions ALSO honor the document's
// is_javascript_execution_disabled() shutoff (the JS path runs JS, so
// if JS itself is disabled the caller has bigger problems, but we
// check defensively).  The result string is the TH8 interpreter's
// result_string() at the end of evaluate().  A non-OK evaluate()
// throws a JS Error containing the TH8 error message.
//
// Reentrancy: TH8Context::evaluate has an m_evaluate_depth guard that
// rejects nested top-level evaluations.  A JS->TH8->JS->TH8 chain
// will fail at the second TH8 frame with a clean error -- the same
// failure mode as a JS->TH8->JS->TH8 chain via dom::eval_js.
GC::Ref<JS::Object> create_window_th8_namespace(JS::Realm& realm, GC::Ref<DOM::Document> document)
{
    auto namespace_object = JS::Object::create(realm, nullptr);

    // The document reference is captured by GC::Ref so the lambda keeps
    // it alive; namespace_object holds the lambda so the lifetime is
    // bounded by the window/document anyway.  We deliberately do NOT
    // capture a TH8Context* directly: it is lazily constructed and
    // looked up at call time, matching dom::eval_js's "look up the
    // live document at dispatch" pattern (H2 audit fix).
    namespace_object->define_native_function(
        realm,
        JS::PropertyKey { "eval"_utf16_fly_string },
        [document](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            if (vm.argument_count() < 1) {
                return vm.throw_completion<JS::TypeError>(
                    "window.th8.eval: missing argument"_utf16);
            }

            // [M15-followup] Per-document runtime kill switch.  Surface
            // this BEFORE the cross-eval gate so embedders that toggled
            // it get a clean "TH8 disabled" error instead of "policy
            // not opted in".
            if (document->th8_disabled()) {
                return vm.throw_completion<JS::TypeError>(
                    "window.th8.eval: TH8 is disabled on this document"_utf16);
            }

            // Gate on cross-eval policy.  Throw SecurityError-style
            // TypeError (no DOMException machinery here) so JS code can
            // try/catch it cleanly.
            if (!document->th8_cross_eval_policy()) {
                return vm.throw_completion<JS::TypeError>(
                    "window.th8.eval: cross-eval not permitted; "
                    "add <meta http-equiv=\"TH8-Script-Policy\" content=\"cross-eval\"> "
                    "to enable"_utf16);
            }

            if (document->is_javascript_execution_disabled()) {
                // Pedantic: we are running JS, so JS is enabled; but
                // keep this consistent with dom::eval_js which checks
                // the symmetric flag.
                return vm.throw_completion<JS::TypeError>(
                    "window.th8.eval: JavaScript execution is disabled "
                    "on this document"_utf16);
            }

            auto source_value = vm.argument(0);
            auto source_utf16 = TRY(source_value.to_utf16_string(vm));
            auto source_string = source_utf16.utf16_view().to_utf8_but_should_be_ported_to_utf16();

            auto& th8_context = document->ensure_th8_context();
            int rc = th8_context.evaluate(source_string.bytes_as_string_view(),
                "window.th8.eval"sv);

            if (rc != TH8_OK) {
                auto error_message = th8_context.result_string();
                return vm.throw_completion<JS::Error>(
                    Utf16String::from_utf8_with_replacement_character(error_message));
            }

            // Return the interpreter's result string.  TH8 stores the
            // post-evaluation result on the interp; we hand it back to
            // JS as a string -- structured value marshalling would
            // require an IDL-shaped surface.
            auto result = th8_context.result_string();
            return JS::PrimitiveString::create(vm,
                Utf16String::from_utf8_with_replacement_character(result));
        },
        /*length=*/1,
        JS::default_attributes);

    return namespace_object;
}

}
