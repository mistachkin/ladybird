/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>
#include <LibWeb/TH8/HandleTable.h>

// [B4 / M10] Adversarial coverage for the strict ^obj([0-9]+)$ handle
// parse.  The previous "skip to first digit" parse let script code
// fabricate handles by prefixing junk before the "obj" literal; the
// current parse must reject every one of these forms.
//
// This test drives HandleTable::parse_handle_id, the pure static
// helper extracted from resolve().  It intentionally does NOT need a
// GC::Heap: the strict contract lives entirely in the string parser,
// so exhaustive input coverage here is enough to lock down M10 without
// pulling in the DOM fixture required for a full DOM-bridge fuzzer.

namespace Web {

TEST_CASE(handle_parse_valid_forms)
{
    EXPECT_EQ(TH8::HandleTable::parse_handle_id("obj0"sv).value(), 0ULL);
    EXPECT_EQ(TH8::HandleTable::parse_handle_id("obj1"sv).value(), 1ULL);
    EXPECT_EQ(TH8::HandleTable::parse_handle_id("obj42"sv).value(), 42ULL);
    EXPECT_EQ(TH8::HandleTable::parse_handle_id("obj12345"sv).value(), 12345ULL);
    // Leading zeros are accepted (still an unambiguous decimal id).
    EXPECT_EQ(TH8::HandleTable::parse_handle_id("obj007"sv).value(), 7ULL);
    EXPECT_EQ(TH8::HandleTable::parse_handle_id("obj00000000000000000000000"sv).value(), 0ULL);
    // Maximum u64 boundary.
    EXPECT_EQ(TH8::HandleTable::parse_handle_id("obj18446744073709551615"sv).value(), 0xFFFFFFFFFFFFFFFFULL);
}

TEST_CASE(handle_parse_missing_or_wrong_prefix)
{
    EXPECT(!TH8::HandleTable::parse_handle_id(""sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("42"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("elem42"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("node42"sv).has_value());
    // Case matters -- Ladybird handles are lowercase-only.
    EXPECT(!TH8::HandleTable::parse_handle_id("Obj42"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("OBJ42"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("oBj42"sv).has_value());
    // Junk before the prefix -- the historical vulnerability.
    EXPECT(!TH8::HandleTable::parse_handle_id("myobj42"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("_obj42"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id(" obj42"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("\tobj42"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id(".obj42"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("obj42.obj42"sv).has_value());
}

TEST_CASE(handle_parse_prefix_only_or_bad_digits)
{
    // Prefix with no digits.
    EXPECT(!TH8::HandleTable::parse_handle_id("obj"sv).has_value());
    // Sign characters -- id is u64, no sign allowed.
    EXPECT(!TH8::HandleTable::parse_handle_id("obj-1"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("obj+1"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("obj-"sv).has_value());
    // Hex / non-ASCII digit shape.
    EXPECT(!TH8::HandleTable::parse_handle_id("obj0x42"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("objff"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("obj4a"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("obja"sv).has_value());
    // Whitespace in id.
    EXPECT(!TH8::HandleTable::parse_handle_id("obj 42"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("obj42 "sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("obj4 2"sv).has_value());
    // Decimal point / other punctuation.
    EXPECT(!TH8::HandleTable::parse_handle_id("obj4.2"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("obj42,"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("obj42/"sv).has_value());
    // Trailing junk right after digits.
    EXPECT(!TH8::HandleTable::parse_handle_id("obj42extra"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("obj42_"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("obj42obj42"sv).has_value());
    // High-byte / control characters must be rejected too.
    EXPECT(!TH8::HandleTable::parse_handle_id("obj\xff"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("obj4\xff"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("obj\x00"sv).has_value());
    EXPECT(!TH8::HandleTable::parse_handle_id("obj4\x00"sv).has_value());
}

TEST_CASE(handle_parse_u64_overflow)
{
    // One past u64 max.
    EXPECT(!TH8::HandleTable::parse_handle_id("obj18446744073709551616"sv).has_value());
    // Far beyond.
    EXPECT(!TH8::HandleTable::parse_handle_id("obj999999999999999999999999999"sv).has_value());
    // Extremely long (still all digits) -- overflow, not parser confusion.
    EXPECT(!TH8::HandleTable::parse_handle_id("obj1111111111111111111111111111111111111111"sv).has_value());
}

}
