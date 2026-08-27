/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibTH8/CAPI.h>
#include <LibWeb/Forward.h>

namespace Web::Bindings {
class PlatformObject;
}

namespace Web::TH8 {

class HandleTable;

// Register all TH8 DOM bridge commands with the interpreter.
// This sets up:
//   - dom::document   -- access the current document
//   - dom::console    -- console.log/warn/error
//   - dom::setTimeout / dom::setInterval / dom::clearTimeout / dom::clearInterval
//   - dom::alert
//   - dom::event      -- bind/unbind event listeners
//   - dom::release    -- release a handle
//   - Object ensemble dispatch for all registered handles
void register_dom_commands(Th8_Interp* interp, DOM::Document& document, HandleTable& handles);

// Register a single DOM object as a TH8 command (ensemble pattern).
// Returns the handle string, or empty on failure.
ByteString register_object_command(Th8_Interp* interp, HandleTable& handles, Bindings::PlatformObject& object);

}
