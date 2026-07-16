/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/HashMap.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/OwnPtr.h>
#include <AK/StringView.h>
#include <AK/Vector.h>
#include <LibGC/Cell.h>
#include <LibGC/CellAllocator.h>
#include <LibGC/Ptr.h>
#include <LibTH8/Forward.h>
#include <LibTH8/WebPlatform.h>
#include <LibWeb/Export.h>
#include <LibWeb/Forward.h>

namespace Web::TH8 {
class HandleTable;
}

namespace Web::HTML {

// Non-standard: Per-document TH8 interpreter context.
// Lazily initialized on first <script type="text/th8"> in a document.
// All TH8 script blocks in the same document share this interpreter,
// analogous to how all JS scripts share one realm per document.
class WEB_API TH8Context final : public GC::Cell {
    GC_CELL(TH8Context, GC::Cell);
    GC_DECLARE_ALLOCATOR(TH8Context);

public:
    virtual ~TH8Context() override;

    static GC::Ref<TH8Context> create(GC::Heap&, DOM::Document&);

    int evaluate(StringView script, StringView name = {});
    StringView result_string() const;

    ::TH8::Interpreter& interpreter() { return *m_interpreter; }
    ::TH8::Interpreter const& interpreter() const { return *m_interpreter; }

    // Accessor for the per-context DOM-object handle table.  Exposed
    // so the DOMBridge event-listener thunk can resolve handle ids
    // lazily at dispatch time (avoiding a raw HandleTable* capture in
    // the listener lambda; see H2 audit fix in DOMBridge.cpp).
    Web::TH8::HandleTable& handle_table() { return *m_handle_table; }

    // Source tracking for DevTools: maps script name -> source text.
    // [M11] Capped + FIFO-evicted via m_source_insertion_order in
    // register_source() so a page that re-registers sources in a loop
    // cannot grow the map without bound.
    void register_source(ByteString name, ByteString source);
    HashMap<ByteString, ByteString> const& sources() const { return m_sources; }
    static constexpr size_t max_source_entries = 1024;

    // Register signature sidecar bytes that TH8's signed-only policy
    // chain will consume when verifying `script_name` (typically the
    // script URL).  `script_name` is the same value passed as the
    // origin name to Th8_Eval; the WebPlatform's xGetData callback
    // serves the bytes when TH8 asks for "<script_name>.b64sig".
    void register_signature_sidecar(ByteString script_name, ByteBuffer signature_bytes);

    // [H3] True while the interpreter and its members can still safely
    // service DOM-bridge callbacks.  Returns false once destruction has
    // started OR before the interpreter has been initialized.  The H2
    // event-listener thunk (DOMBridge.cpp) checks this before touching
    // the interpreter / handle_table.
    bool is_alive_for_dispatch() const { return !m_destroying && m_interpreter; }

private:
    explicit TH8Context(DOM::Document&);

    virtual void visit_edges(Cell::Visitor&) override;

    void initialize_interpreter();

    // [H3] Member declaration order matters here for destruction safety,
    // BUT the destructor body still explicitly tears down m_interpreter
    // before m_handle_table / m_platform_context regardless of order --
    // see the destructor in TH8Context.cpp.  Default reverse-declaration
    // destruction would destroy m_handle_table before m_interpreter,
    // and Th8_DeleteInterp can synchronously invoke command callbacks
    // (BridgeContext destructors) that still reach for the handle table.
    OwnPtr<::TH8::WebPlatformContext> m_platform_context;
    OwnPtr<Web::TH8::HandleTable> m_handle_table;
    OwnPtr<::TH8::Interpreter> m_interpreter;
    GC::Ref<DOM::Document> m_document;
    HashMap<ByteString, ByteString> m_sources;
    // [M11] Insertion order, drives FIFO eviction in register_source().
    Vector<ByteString> m_source_insertion_order;

    // [H3] Re-entrancy guard.  m_evaluate_depth is incremented across
    // each Th8_Eval call from this->evaluate(); a re-entrant evaluate()
    // (e.g., dom::eval_js -> JS -> TH8 cycle) is rejected up front so
    // command callbacks never run against partially-mutated state.
    // m_destroying is set at the top of ~TH8Context() before any
    // member teardown; is_alive_for_dispatch() consults it so DOM-bridge
    // callbacks fired DURING destructor unwinding bail out cleanly.
    int m_evaluate_depth { 0 };
    bool m_destroying { false };
};

}
