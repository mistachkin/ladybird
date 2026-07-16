/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGC/Heap.h>
#include <LibWeb/TH8/HandleTable.h>

namespace Web::TH8 {

HandleTable::HandleTable(GC::Heap&)
{
    // m_id_to_object is a GC::RootHashMap: it self-registers with the
    // current heap (GC::Heap::the()) on default construction, so the
    // Heap& argument is no longer consumed here.  The parameter is kept
    // for API stability with existing callers (e.g. TH8Context).
}

ByteString HandleTable::make_handle_string([[maybe_unused]] Bindings::PlatformObject& object, u64 id)
{
    // Simple numeric handle — the prefix is determined by the object type
    // but for simplicity we use a uniform "obj" prefix.
    return ByteString::formatted("{}{}", handle_prefix, id);
}

ByteString HandleTable::register_handle(Bindings::PlatformObject& object)
{
    // Deduplication: if this object already has a handle, return it.
    auto existing = m_object_to_id.get(&object);
    if (existing.has_value()) {
        auto handle_it = m_id_to_handle_string.get(existing.value());
        if (handle_it.has_value())
            return handle_it.value();
    }

    // Enforce handle cap.
    if (m_id_to_object.size() >= max_handles)
        return {};

    auto id = m_next_id++;
    auto handle_string = make_handle_string(object, id);

    m_id_to_object.set(id, &object);
    m_object_to_id.set(&object, id);
    m_id_to_handle_string.set(id, handle_string);

    return handle_string;
}

Optional<u64> HandleTable::parse_handle_id(StringView handle)
{
    // [M10] Strict ^obj([0-9]+)$ parse.  The previous "skip to first
    // digit" parse let `myobj42` resolve the same as `obj42`, which
    // gave script code a way to fabricate handles by prefixing junk
    // before the prefix.  Now: handle MUST start with the exact
    // `handle_prefix` literal, followed by one-or-more ASCII digits
    // and nothing else.  Rejects: missing prefix, wrong-case prefix,
    // prefix without digits, non-digit characters, trailing junk,
    // u64 overflow.
    if (!handle.starts_with(handle_prefix))
        return {};
    auto id_part = handle.substring_view(handle_prefix.length());
    if (id_part.is_empty())
        return {};
    for (size_t i = 0; i < id_part.length(); ++i) {
        if (id_part[i] < '0' || id_part[i] > '9')
            return {};
    }
    return id_part.to_number<u64>();
}

Bindings::PlatformObject* HandleTable::resolve(StringView handle) const
{
    auto maybe_id = parse_handle_id(handle);
    if (!maybe_id.has_value())
        return nullptr;

    auto it = m_id_to_object.get(maybe_id.value());
    if (!it.has_value())
        return nullptr;

    return *it;
}

void HandleTable::release(StringView handle)
{
    auto* object = resolve(handle);
    if (!object)
        return;

    auto id_it = m_object_to_id.get(object);
    if (!id_it.has_value())
        return;

    auto id = id_it.value();
    m_id_to_object.remove(id);
    m_object_to_id.remove(object);
    m_id_to_handle_string.remove(id);
}

void HandleTable::release_all()
{
    m_id_to_object.clear();
    m_object_to_id.clear();
    m_id_to_handle_string.clear();
}

bool HandleTable::is_valid(StringView handle) const
{
    return resolve(handle) != nullptr;
}

}
