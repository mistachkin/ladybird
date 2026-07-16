/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/HashMap.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/Span.h>
#include <AK/StringView.h>
#include <AK/Types.h>
#include <LibTH8/CAPI.h>

namespace TH8 {

// Per-TH8Context state consulted by the WebPlatform's xGetData callback.
//
// Currently this carries the signature-sidecar table: a map from
// resource name (e.g., "https://example.org/foo.th8.b64sig") to the
// raw signature bytes that the TH8 signed-only policy chain will
// consume when verifying a signed script.
//
// The Ladybird fetch path populates the table BEFORE Th8_Eval is
// invoked.  When TH8's policy chain calls xGetData looking for
// "<scriptname>.b64sig", the callback returns the stored bytes; if
// the entry is missing, xGetData returns TH8_ERROR and the script
// fails verification (fail-closed).
//
// The context is owned by TH8Context and must outlive the
// Th8_Platform / Th8_Interp constructed from it.
class TH8_API WebPlatformContext {
public:
    WebPlatformContext() = default;
    ~WebPlatformContext() = default;

    WebPlatformContext(WebPlatformContext const&) = delete;
    WebPlatformContext& operator=(WebPlatformContext const&) = delete;

    // Register signature bytes under `resource_name` (typically the
    // script URL with a ".b64sig" suffix appended).  Overwrites any
    // prior entry for the same name.  Moves the buffer in.
    void register_sidecar(ByteString resource_name, ByteBuffer bytes);

    // Lookup; returns Nothing if no entry exists.
    Optional<ReadonlyBytes> get_sidecar(StringView resource_name) const;

private:
    HashMap<ByteString, ByteBuffer> m_sidecars;
};

// [L19] Opaque RAII wrapper around the vendored Th8_Platform value
// built by create_web_content_platform.  The Th8_Platform struct
// definition is intentionally not exposed -- consumers can only pass
// `desc->raw()` straight to Interpreter::create, which takes a
// const Th8_Platform& (forward-decl suffices in headers; the full
// type is resolved inside LibTH8.cpp via the PRIVATE Vendor/ path).
class TH8_API PlatformDescriptor {
public:
    PlatformDescriptor();
    ~PlatformDescriptor();
    PlatformDescriptor(PlatformDescriptor const&) = delete;
    PlatformDescriptor& operator=(PlatformDescriptor const&) = delete;

    Th8_Platform const& raw() const;

    // Implementation hook used by the two factory functions below
    // to fill the underlying Th8_Platform.  Not for general use.
    Th8_Platform& mutable_raw();

private:
    struct Impl;
    NonnullOwnPtr<Impl> m_impl;
};

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
//
// The zero-argument form leaves the xGetData callback as the deny
// stub from Null I/O.  The overload that takes a WebPlatformContext&
// installs a custom xGetData that serves signature sidecars from the
// context.  The caller must keep `context` alive for at least as
// long as any Th8_Interp constructed from the returned platform.
TH8_API NonnullOwnPtr<PlatformDescriptor> create_web_content_platform();
TH8_API NonnullOwnPtr<PlatformDescriptor> create_web_content_platform(WebPlatformContext& context);

// Default resource limits for web content interpreters.
static constexpr i64 default_step_limit = 10'000'000;
static constexpr size_t default_memory_limit = 64 * 1024 * 1024; // 64 MB
// [M2] Wall-clock cap.  The step-limit alone can still occupy the
// renderer for seconds on a tight loop of cheap instructions; a
// background watchdog calls Th8_CancelEval after this elapses to
// guarantee bounded main-thread blockage regardless of the script's
// per-step cost.  Tuned for "well under one frame budget" on a fast
// machine while still allowing real workloads.
static constexpr int default_wall_clock_limit_ms = 250;

}
