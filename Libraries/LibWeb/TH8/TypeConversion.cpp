/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/TH8/TypeConversion.h>

#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/DOM/Node.h>
#include <LibWeb/TH8/DOMBridge.h>
#include <LibWeb/TH8/HandleTable.h>

namespace Web::TH8 {

int set_result_string(Th8_Interp* interp, StringView value)
{
    return Th8_SetResult(interp, value.characters_without_null_termination(), value.length());
}

int set_result_bool(Th8_Interp* interp, bool value)
{
    return Th8_SetResult(interp, value ? "1" : "0", 1);
}

int set_result_int(Th8_Interp* interp, int value)
{
    return Th8_SetResultInt(interp, value);
}

int set_result_node(Th8_Interp* interp, HandleTable& handles, GC::Ptr<DOM::Node> node)
{
    if (!node)
        return Th8_SetResult(interp, "", 0);

    auto& platform_object = static_cast<Bindings::PlatformObject&>(*node);
    // [H11] Register the node as an ensemble COMMAND, not merely a handle
    // string.  register_handle() alone yields an opaque id ("objN") with no
    // associated TH8 command, so a subsequent `$node <method>` fails with
    // "no such command: objN" -- every node returned to a script was a dead
    // handle.  register_object_command() both interns the handle (dedup) and
    // installs the object-ensemble dispatch command under that name.
    auto handle = register_object_command(interp, handles, platform_object);
    if (handle.is_empty())
        return set_error(interp, "handle table full"sv);

    return Th8_SetResult(interp, handle.characters(), handle.length());
}

int set_error(Th8_Interp* interp, StringView message)
{
    Th8_SetResult(interp, message.characters_without_null_termination(), message.length());
    return TH8_ERROR;
}

String string_from_th8(char const* str, size_t len)
{
    return MUST(String::from_utf8(StringView { str, len }));
}

FlyString flystring_from_th8(char const* str, size_t len)
{
    return MUST(FlyString::from_utf8(StringView { str, len }));
}

}
