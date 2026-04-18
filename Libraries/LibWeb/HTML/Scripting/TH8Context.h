/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullOwnPtr.h>
#include <LibGC/Cell.h>
#include <LibGC/Ptr.h>
#include <LibTH8/Forward.h>
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

    // Source tracking for DevTools: maps script name -> source text.
    void register_source(ByteString name, ByteString source);
    HashMap<ByteString, ByteString> const& sources() const { return m_sources; }

private:
    TH8Context(DOM::Document&);

    virtual void visit_edges(Cell::Visitor&) override;

    void initialize_interpreter();

    OwnPtr<::TH8::Interpreter> m_interpreter;
    OwnPtr<Web::TH8::HandleTable> m_handle_table;
    GC::Ref<DOM::Document> m_document;
    HashMap<ByteString, ByteString> m_sources;
};

}
