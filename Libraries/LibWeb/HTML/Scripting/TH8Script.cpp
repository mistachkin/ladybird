/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Debug.h>
#include <LibJS/Runtime/VM.h>
#include <LibTH8/Interpreter.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/EventLoop/EventLoop.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Scripting/ExceptionReporter.h>
#include <LibWeb/HTML/Scripting/TH8Context.h>
#include <LibWeb/HTML/Scripting/TH8Script.h>
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
    auto* responsible_document = settings.responsible_document();
    if (!responsible_document)
        return JS::normal_completion(JS::js_undefined());

    auto& th8_context = responsible_document->ensure_th8_context();

    // Register source for DevTools visibility.
    th8_context.register_source(filename(), m_source);

    // Evaluate the TH8 source in the shared per-document interpreter.
    int rc = th8_context.evaluate(m_source, filename());

    if (rc != TH8_OK) {
        auto error_message = th8_context.result_string();
        dbgln_if(HTML_SCRIPT_DEBUG, "TH8Script: Error in {}: {}", filename(), error_message);

        if (rethrow_errors == RethrowErrors::Yes) {
            auto& realm = settings.realm();
            return JS::throw_completion(JS::Error::create(realm, MUST(String::from_utf8(error_message))));
        }

        // Report the error through the global object's error reporting mechanism.
        auto& window_or_worker = as<WindowOrWorkerGlobalScopeMixin>(settings.global_object());
        auto& realm = settings.realm();
        auto error = JS::Error::create(realm, MUST(String::from_utf8(error_message)));
        window_or_worker.report_an_exception(error);
    }

    // Drain microtasks that may have been created by DOM operations during TH8 execution.
    // TH8 does not push/pop JS execution contexts, so microtasks are not automatically
    // drained via clean_up_after_running_script().
    main_thread_event_loop().perform_a_microtask_checkpoint();

    return JS::normal_completion(JS::js_undefined());
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
