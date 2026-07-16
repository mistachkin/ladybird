/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// Fuzz target for the TH8 <-> LibWeb DOM bridge byte-handling surface.
//
// Exercises the parts of the DOM bridge (Libraries/LibWeb/TH8/) that
// process UNTRUSTED BYTES and therefore carry the memory-safety risk,
// without needing a live DOM tree:
//
//   * Web::TH8::string_from_th8 / flystring_from_th8 -- convert an
//     arbitrary TH8 byte string (possibly invalid UTF-8, embedded
//     NULs, lone surrogates, high bytes) into an AK String / FlyString.
//   * Web::TH8::HandleTable::parse_handle_id -- the strict [M10]
//     handle-id parser (prefix + digits only, u64 overflow), driven as
//     a pure function against arbitrary input.  Complements the ~40
//     hand-written cases in Tests/LibWeb/TestTH8HandleTable.cpp with
//     continuous coverage.
//   * Web::TH8::set_result_string / set_error -- push an arbitrary byte
//     string through the TH8 interpreter's result buffer.
//
// These are exactly the HandleTable + TypeConversion components named in
// the PR audit's B4 residual, driven against fuzzed input.
//
// NOT covered here: the command ensemble dispatch
// (object_ensemble_command / document_subcommand /
// register_dom_commands).  Those are file-static in DOMBridge.cpp and
// operate on real DOM::Node / DOM::Document objects, so fuzzing them
// needs a GC-heap + live-Document fixture (an event-loop-driven Page /
// navigable).  That fixture is still outstanding; see
// Tests/LibTH8/CMakeLists.txt.
//
// Build with libFuzzer:
//   cmake -DENABLE_TH8=ON -DENABLE_FUZZERS=ON ...
//   ninja fuzz_th8_dom_bridge

#include <AK/FlyString.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <LibWeb/TH8/HandleTable.h>
#include <LibWeb/TH8/TypeConversion.h>
#include <stddef.h>
#include <stdint.h>
#include <th8.h>

static Th8_Interp* s_interp = nullptr;

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    // Bare libc platform: no file I/O, no network.  Only needed so the
    // set_result_string / set_error paths have a real result buffer to
    // write into; the conversion and parse helpers are pure.
    auto* platform = Th8_GetLibcPlatform();

    if (Th8_Initialize(platform) != TH8_OK)
        return 1;

    s_interp = Th8_CreateInterp(platform);
    if (!s_interp)
        return 1;

    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    if (size > 100000)
        return 0;

    auto const* chars = reinterpret_cast<char const*>(data);
    StringView const view { chars, size };

    // 1. TypeConversion: arbitrary bytes -> AK String / FlyString.
    //    Exercises UTF-8 validation / replacement on hostile input.
    (void)Web::TH8::string_from_th8(chars, size);
    (void)Web::TH8::flystring_from_th8(chars, size);

    // 2. HandleTable: strict handle-id parse against arbitrary bytes.
    (void)Web::TH8::HandleTable::parse_handle_id(view);

    // 3. TH8 result buffer: push arbitrary bytes through the result and
    //    error setters, then clear for the next input.
    if (s_interp) {
        Web::TH8::set_result_string(s_interp, view);
        Web::TH8::set_error(s_interp, view);
        Th8_ClearResult(s_interp);
    }

    return 0;
}
