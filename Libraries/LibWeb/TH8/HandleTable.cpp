/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/TH8/HandleTable.h>

namespace Web::TH8 {

HandleTable::HandleTable(GC::Heap& heap)
    : m_id_to_object(heap)
{
}

ByteString HandleTable::make_handle_string(Bindings::PlatformObject& object, u64 id)
{
    (void)object;
    // Simple numeric handle — the prefix is determined by the object type
    // but for simplicity we use a uniform "obj" prefix.
    return ByteString::formatted("obj{}", id);
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

Bindings::PlatformObject* HandleTable::resolve(StringView handle) const
{
    // Parse the numeric ID from the handle string.
    // Handle format: <prefix><id>
    size_t digit_start = 0;
    for (size_t i = 0; i < handle.length(); ++i) {
        if (handle[i] >= '0' && handle[i] <= '9') {
            digit_start = i;
            break;
        }
    }
    if (digit_start == 0 && (handle.is_empty() || handle[0] < '0' || handle[0] > '9'))
        return nullptr;

    auto id_part = handle.substring_view(digit_start);
    auto maybe_id = id_part.to_number<u64>();
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
