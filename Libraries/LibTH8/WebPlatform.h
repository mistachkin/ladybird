/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Types.h>
#include <th8.h>

namespace TH8 {

// Creates a Th8_Platform configured for secure web content execution.
//
// The platform is composed from TH8's built-in layers:
//   1. Null I/O platform (blocks all file I/O, stdin/stdout, loading)
//   2. Libc platform (memory allocation, memcpy, string ops)
//   3. macOS platform (zone allocator, on Apple platforms)
//   4. POSIX platform (mutex, time callbacks only)
//
// All file I/O, dynamic loading, process info, environment access,
// and output callbacks are explicitly left NULL for security.
Th8_Platform create_web_content_platform();

// Default resource limits for web content interpreters.
static constexpr i64 default_step_limit = 10'000'000;
static constexpr size_t default_memory_limit = 64 * 1024 * 1024; // 64 MB

}
