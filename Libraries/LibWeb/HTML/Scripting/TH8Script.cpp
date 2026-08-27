/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/HTML/Scripting/TH8Script.h>

#include <AK/Debug.h>
#include <AK/Utf16String.h>
#include <LibJS/Runtime/VM.h>
#include <LibTH8/Interpreter.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Scripting/ExceptionReporter.h>
#include <LibWeb/HTML/Scripting/TH8Context.h>
#include <LibWeb/HTML/WindowOrWorkerGlobalScope.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(TH8Script);

GC::Ref<TH8Script> TH8Script::create(ByteString filename, StringView source, EnvironmentSettingsObject& settings, URL::URL base_url)
{
    auto& vm = settings.vm();

    auto script = vm.heap().allocate<TH8Script>(move(base_url), move(filename), settings);

    // TH8 does not have a separate parse phase; store source for evaluation at run-time.
    script->m_source = ByteString(source);

    // No parse errors are possible at creation time.
    script->set_parse_error(JS::js_null());
    script->set_error_to_rethrow(JS::js_null());

    return script;
}

JS::Completion TH8Script::run(RethrowErrors rethrow_errors)
{
    auto& settings = this->settings_object();

    // Check if we can run script with settings.
    if (can_run_script(settings) == RunScriptDecision::DoNotRun)
        return JS::normal_completion(JS::js_undefined());

    // Get or create the document's TH8 interpreter context.
    auto responsible_document = settings.responsible_document();
    if (!responsible_document)
        return JS::normal_completion(JS::js_undefined());

    auto& th8_context = responsible_document->ensure_th8_context();

    // Register source for DevTools visibility.
    th8_context.register_source(filename(), m_source);

    // If a signature sidecar was attached during fetch (text/th8+signed),
    // hand it to the TH8Context's WebPlatform so the signed-only policy
    // chain can fetch it when verifying.  This must happen BEFORE
    // evaluate() so the policy's xGetData lookup succeeds.
    if (m_signature_sidecar.has_value()) {
        // Copy the bytes; the buffer may be consumed by the policy
        // chain and we want the script to remain re-runnable.
        auto bytes = MUST(ByteBuffer::copy(m_signature_sidecar->bytes()));
        th8_context.register_signature_sidecar(filename(), move(bytes));
    }

    // [H4] Push the canonical script execution context BEFORE invoking
    // TH8.  DOM operations triggered from TH8 (set_inner_html, fetch,
    // TrustedTypes lookups, ...) consult `vm.running_execution_context()`
    // to resolve incumbent/current realm; without prepare_to_run_script
    // they would see whatever frame happened to be on top, which is
    // undefined for a cold TH8 entry and is what makes the cross-eval
    // JS->TH8->DOM path work only by accident.  clean_up_after_running_script
    // additionally drains microtasks when the JS stack returns to empty,
    // replacing the prior hand-rolled perform_a_microtask_checkpoint() call.
    prepare_to_run_script(settings);

    // Evaluate the TH8 source in the shared per-document interpreter.
    int rc = th8_context.evaluate(m_source, filename());

    JS::Completion result = JS::normal_completion(JS::js_undefined());

    if (rc != TH8_OK) {
        auto error_message = th8_context.result_string();
        dbgln_if(HTML_SCRIPT_DEBUG, "TH8Script: Error in {}: {}", filename(), error_message);

        // [M5] TH8 strings are 8-bit-clean; binary bytes can reach
        // here via e.g. `[format "%c" 0xFF]`.  MUST(String::from_utf8(...))
        // aborts the process on invalid UTF-8 -- replace decode errors
        // with U+FFFD instead.
        auto& realm = settings.realm();
        auto error = JS::Error::create(realm, Utf16String::from_utf8_with_replacement_character(error_message));

        if (rethrow_errors == RethrowErrors::Yes) {
            result = JS::throw_completion(error);
        } else {
            // Report the error through the global object's error reporting mechanism.
            auto& window_or_worker = as<WindowOrWorkerGlobalScopeMixin>(settings.global_object());
            window_or_worker.report_an_exception(error);
        }
    }

    // [H4] Symmetric clean-up.  Pops the execution context we pushed
    // above and runs the microtask checkpoint when the JS stack is
    // empty.  Must happen on every return path (success and failure).
    clean_up_after_running_script(settings);

    return result;
}

TH8Script::TH8Script(URL::URL base_url, ByteString filename, EnvironmentSettingsObject& settings)
    : Script(move(base_url), move(filename), settings)
{
}

TH8Script::~TH8Script() = default;

void TH8Script::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
}

}
