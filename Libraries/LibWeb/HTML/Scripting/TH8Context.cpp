/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTH8/Interpreter.h>
#include <LibTH8/WebPlatform.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/Scripting/TH8Context.h>
#include <LibWeb/TH8/DOMBridge.h>
#include <LibWeb/TH8/HandleTable.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(TH8Context);

GC::Ref<TH8Context> TH8Context::create(GC::Heap& heap, DOM::Document& document)
{
    return heap.allocate<TH8Context>(document);
}

TH8Context::TH8Context(DOM::Document& document)
    : m_document(document)
{
    initialize_interpreter();
}

TH8Context::~TH8Context() = default;

void TH8Context::initialize_interpreter()
{
    auto platform = ::TH8::create_web_content_platform();
    auto interpreter_or_error = ::TH8::Interpreter::create(platform);
    if (interpreter_or_error.is_error()) {
        dbgln("TH8Context: Failed to create interpreter: {}", interpreter_or_error.error());
        return;
    }

    m_interpreter = interpreter_or_error.release_value();

    // Apply default resource limits for web content.
    m_interpreter->set_step_limit(::TH8::default_step_limit);
    m_interpreter->set_memory_limit(::TH8::default_memory_limit);

    // Create the handle table for DOM object references.
    m_handle_table = make<Web::TH8::HandleTable>(m_document->heap());

    // Register DOM bridge commands with the interpreter.
    Web::TH8::register_dom_commands(m_interpreter->raw(), *m_document, *m_handle_table);
}

int TH8Context::evaluate(StringView script, StringView name)
{
    if (!m_interpreter) {
        dbgln("TH8Context: Cannot evaluate, interpreter not initialized");
        return TH8_ERROR;
    }

    return m_interpreter->evaluate(script, name);
}

StringView TH8Context::result_string() const
{
    if (!m_interpreter)
        return {};

    return m_interpreter->result_string();
}

void TH8Context::register_source(ByteString name, ByteString source)
{
    m_sources.set(move(name), move(source));
}

void TH8Context::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_document);
}

}
