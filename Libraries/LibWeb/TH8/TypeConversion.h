/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <LibGC/Ptr.h>
#include <LibTH8/CAPI.h>
#include <LibWeb/Forward.h>

namespace Web::TH8 {

class HandleTable;

// Set the TH8 interpreter result to a string value. Returns TH8_OK.
int set_result_string(Th8_Interp* interp, StringView value);

// Set the TH8 interpreter result to a boolean ("1" or "0"). Returns TH8_OK.
int set_result_bool(Th8_Interp* interp, bool value);

// Set the TH8 interpreter result to an integer. Returns TH8_OK.
int set_result_int(Th8_Interp* interp, int value);

// Set the TH8 interpreter result to a DOM node handle. Returns TH8_OK, or TH8_ERROR if
// the node is null or the handle table is full.
int set_result_node(Th8_Interp* interp, HandleTable& handles, GC::Ptr<DOM::Node> node);

// Set the TH8 interpreter result to an error message. Returns TH8_ERROR.
int set_error(Th8_Interp* interp, StringView message);

// Convert a TH8 string argument to an AK String.
String string_from_th8(char const* str, size_t len);

// Convert a TH8 string argument to a FlyString.
FlyString flystring_from_th8(char const* str, size_t len);

}
