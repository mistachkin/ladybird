/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <AK/StringView.h>
#include <AK/Types.h>
#include <LibGC/RootHashMap.h>
#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/Export.h>

namespace Web::TH8 {

// Maps opaque string handles (e.g., "elem42") to GC-managed DOM objects.
// The RootHashMap ensures all referenced objects are reported as GC roots,
// preventing premature collection while TH8 code holds a handle.
class WEB_API HandleTable {
public:
    explicit HandleTable(GC::Heap&);
    ~HandleTable() = default;

    // Register a DOM object and return its handle string.
    // Deduplicates: same object always returns the same handle.
    ByteString register_handle(Bindings::PlatformObject& object);

    // Resolve a handle string to the underlying DOM object.
    // Returns nullptr if the handle is invalid or has been released.
    Bindings::PlatformObject* resolve(StringView handle) const;

    // Release a specific handle, allowing the object to be collected.
    void release(StringView handle);

    // Release all handles (called when the interpreter is destroyed).
    void release_all();

    // Check if a handle is valid.
    bool is_valid(StringView handle) const;

    // Current number of active handles.
    size_t size() const { return m_id_to_object.size(); }

    static constexpr size_t max_handles = 10'000;

    // [M10] All issued handles take the form `<handle_prefix><decimal-id>`.
    // resolve() requires the prefix exactly, then digits only.
    static constexpr StringView handle_prefix = "obj"sv;

    // Parse-only helper: extracts the id portion of a handle string
    // without touching the table.  Returns the id on success or an
    // empty Optional on any of: missing prefix, empty id part,
    // non-digit character in id, or u64 overflow.  Exposed so the
    // strict [M10] contract can be exercised by unit tests without
    // constructing a GC::Heap.
    static Optional<u64> parse_handle_id(StringView handle);

private:
    ByteString make_handle_string(Bindings::PlatformObject& object, u64 id);

    u64 m_next_id { 0 };
    GC::RootHashMap<u64, Bindings::PlatformObject*> m_id_to_object;
    HashMap<Bindings::PlatformObject*, u64> m_object_to_id;
    HashMap<u64, ByteString> m_id_to_handle_string;
};

}
