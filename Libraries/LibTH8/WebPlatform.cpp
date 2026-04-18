/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTH8/WebPlatform.h>

namespace TH8 {

Th8_Platform create_web_content_platform()
{
    // Start with an empty platform.
    Th8_Platform platform = {};

    // Layer 1: Null I/O -- blocks all file I/O, stdin/stdout, dynamic loading.
    // This is the security foundation: scripts cannot access the host filesystem,
    // load native code, or perform any I/O unless explicitly enabled.
    Th8_MergePlatform(&platform, Th8_GetNullIoPlatform());

    // Layer 2: Libc -- provides memory allocation (malloc/free/realloc),
    // memcpy, memset, string comparison, formatting, and sorting.
    // TH8's internal memory limit enforcement (nAllocLimit) works on top
    // of these standard allocators.
    Th8_MergePlatform(&platform, Th8_GetLibcPlatform());

#if defined(__APPLE__)
    // Layer 3 (macOS only): Zone allocator for reduced fragmentation
    // and accurate malloc_zone_size tracking.
    Th8_MergePlatform(&platform, Th8_GetMacOSPlatform());
#endif

#if defined(TH8_PLATFORM_POSIX)
    // Layer 4 (POSIX): Selectively merge mutex and time callbacks
    // from the POSIX platform for thread safety and clock access.
    // The full POSIX platform includes file I/O which we do NOT want,
    // but MergePlatform only fills NULL slots, and the Null I/O platform
    // already filled those with blocking stubs. So we safely merge the
    // full POSIX platform -- the I/O callbacks from NullIO take precedence
    // since they were set first.
    Th8_MergePlatform(&platform, Th8_GetPosixPlatform());
#endif

    return platform;
}

}
