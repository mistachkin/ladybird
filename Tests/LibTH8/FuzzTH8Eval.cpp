/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// Fuzz target for the TH8 script evaluator.
//
// Exercises the TH8 parser and evaluation engine with arbitrary
// input, under resource limits (step limit and memory limit) to
// prevent hangs and OOM.  The interpreter is created once and
// reused across inputs for performance.
//
// Build with libFuzzer:
//   clang++ -fsanitize=fuzzer,address -o fuzz_th8_eval FuzzTH8Eval.cpp \
//     -I../../Libraries/LibTH8/Vendor -llagom-th8
//
// Or integrate with oss-fuzz via the project's build system.

#include <stddef.h>
#include <stdint.h>
#include <th8.h>

static Th8_Interp* s_interp = nullptr;

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    // Use the default libc platform (no file I/O, no network).
    auto* platform = Th8_GetLibcPlatform();

    int rc = Th8_Initialize(platform);
    if (rc != TH8_OK)
        return 1;

    s_interp = Th8_CreateInterp(platform);
    if (!s_interp)
        return 1;

    // Tight resource limits to prevent hangs.
    Th8_SetStepLimit(s_interp, 50000);
    Th8_SetAllocLimit(s_interp, 4 * 1024 * 1024); // 4 MB

    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    if (!s_interp || size == 0 || size > 100000)
        return 0;

    // Evaluate the fuzzed input as a TH8 script.
    Th8_Eval(s_interp, 0,
        reinterpret_cast<char const*>(data), size,
        nullptr, 0);

    // Reset interpreter state for the next input.
    Th8_ClearResult(s_interp);
    Th8_ResetCancel(s_interp);
    Th8_SetStepLimit(s_interp, 50000);

    return 0;
}
