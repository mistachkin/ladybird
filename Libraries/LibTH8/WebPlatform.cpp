/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTH8/WebPlatform.h>

#include <AK/Memory.h>

// [L19] WebPlatform.h reaches the vendored TH8 C API through the
// curated CAPI.h header (Th8_Platform is forward-declared there).
// The implementation file needs the full upstream surface to manipulate
// Th8_Platform field slots and call Th8_GetDefaultPlatform / etc.
#include <th8.h>

namespace TH8 {

// PIMPL impl: holds the actual Th8_Platform value.  Defined here so
// the full vendored Th8_Platform layout stays inside LibTH8.
struct PlatformDescriptor::Impl {
    Th8_Platform value {};
};

PlatformDescriptor::PlatformDescriptor()
    : m_impl(make<Impl>())
{
}

PlatformDescriptor::~PlatformDescriptor() = default;

Th8_Platform const& PlatformDescriptor::raw() const { return m_impl->value; }
Th8_Platform& PlatformDescriptor::mutable_raw() { return m_impl->value; }


void WebPlatformContext::register_sidecar(ByteString resource_name, ByteBuffer bytes)
{
    m_sidecars.set(move(resource_name), move(bytes));
}

Optional<ReadonlyBytes> WebPlatformContext::get_sidecar(StringView resource_name) const
{
    auto it = m_sidecars.find(resource_name);
    if (it == m_sidecars.end())
        return {};
    return it->value.bytes();
}

namespace {

// Forward declaration so the definition below has a visible prototype
// (-Wmissing-prototypes).  The extern "C" linkage is required: this
// thunk is installed directly into the C Th8_Platform.xGetData slot.
extern "C" int th8_web_platform_x_get_data(
    Th8_Interp*, void*, char const*, size_t, char**, size_t*);

// C-callable thunk for the Th8_Platform.xGetData slot.  Consults the
// WebPlatformContext stashed in pCtx and returns sidecar bytes when
// the requested name matches a registered entry; otherwise returns
// TH8_ERROR so the TH8 policy chain treats the signature as missing.
//
// TH8 owns the buffer ownership contract here: pzOut must be allocated
// with Th8_Malloc (the caller is responsible for freeing via
// Th8_Free).  We copy from the WebPlatformContext's stable storage
// so the caller's free does not race with later sidecar registrations.
extern "C" int th8_web_platform_x_get_data(
    Th8_Interp* interp,
    void* pCtx,
    char const* zName,
    size_t nName,
    char** pzOut,
    size_t* pnOut)
{
    if (!pCtx || !zName || !pzOut || !pnOut)
        return TH8_ERROR;

    auto* context = static_cast<WebPlatformContext*>(pCtx);
    StringView name { zName, nName };

    auto stored = context->get_sidecar(name);
    if (!stored.has_value())
        return TH8_ERROR;

    auto bytes = stored.value();
    if (bytes.is_empty()) {
        // Empty sidecar is treated as "not present" rather than as a
        // valid zero-byte signature; verification would fail anyway.
        return TH8_ERROR;
    }

    auto* buffer = static_cast<char*>(Th8_Malloc(interp, bytes.size()));
    if (!buffer)
        return TH8_ERROR;

    memcpy(buffer, bytes.data(), bytes.size());
    *pzOut = buffer;
    *pnOut = bytes.size();
    return TH8_OK;
}

// [H1, audit B2 follow-up] Defense-in-depth: after all platform layers
// are merged, explicitly NULL out every callback slot that exposes the
// host filesystem, process information, environment, network, or
// stdin/stdout/stderr -- regardless of whether the Null-I/O layer happened
// to cover that slot.
//
// `Th8_MergePlatform` only fills NULL slots (first-layer-wins), so the
// audit's recommended "reorder Posix-first, NullIO-last" alternative
// would actually be a no-op: NullIO last finds every slot already filled
// by Posix and skips them.  The robust alternative is to enumerate every
// sandbox-exposed slot here and set it to NULL after merging, so a future
// TH8 release that adds a new platform-layer slot can be slotted into this
// list without touching merge order.
//
// xGetData is the one exception: TH8Context installs its own context-aware
// shim (sidecar serving for signed-only verification) at the call site, so
// the slot is intentionally non-NULL.  Verify that the shim has actually
// been installed before letting an interpreter spin up.
static void deny_sandbox_unsafe_slots(Th8_Platform& platform, bool has_sidecar_callback)
{
    // Filesystem / loading / process info / network / stdio.
    platform.xDataExists = nullptr;
    platform.xLoad = nullptr;
    platform.xUnload = nullptr;
    platform.xSameFile = nullptr;
    platform.xGetRealPath = nullptr;
    platform.xGetRootPath = nullptr;
    platform.xGetCwd = nullptr;
    platform.xSetCwd = nullptr;
    platform.xGetExePath = nullptr;
    platform.xNormalizePath = nullptr;
    platform.xGetTemporaryData = nullptr;
    platform.xSetTemporaryData = nullptr;
    platform.xDeleteTemporaryData = nullptr;
    platform.xCloseTemporaryData = nullptr;

    platform.xGetPid = nullptr;
    platform.xGetParentPid = nullptr;
    platform.xGetUserName = nullptr;
    platform.xGetHostName = nullptr;
    platform.xGetEnv = nullptr;
    platform.xKeyValue = nullptr;

    platform.xDnsResolve = nullptr;
    platform.xDnsResolveFree = nullptr;

    platform.xInput = nullptr;
    platform.xOutput = nullptr;
    platform.xOutputError = nullptr;
    platform.xGetInput = nullptr;
    platform.xSetInput = nullptr;
    platform.xGetOutput = nullptr;
    platform.xSetOutput = nullptr;
    platform.xGetErrorOutput = nullptr;
    platform.xSetErrorOutput = nullptr;
    platform.xChannelControl = nullptr;

    // xGetData stays only when an embedder shim has been installed
    // (the context-aware sidecar callback in TH8Context's WebPlatform);
    // otherwise it is denied like the rest of the I/O surface.
    if (!has_sidecar_callback)
        platform.xGetData = nullptr;
}

Th8_Platform build_web_content_platform_layers()
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
    // Layer 4 (POSIX): Mutex, time, random-bytes, and similar non-I/O
    // callbacks from the POSIX layer.  POSIX *also* defines callbacks
    // for filesystem / loading / process info that we explicitly do NOT
    // want; those are NULLed out by deny_sandbox_unsafe_slots below
    // regardless of what POSIX filled in here.
    Th8_MergePlatform(&platform, Th8_GetPosixPlatform());
#elif defined(TH8_PLATFORM_WIN32)
    // Layer 4 (Windows): Mutex, time, high-resolution timer, and
    // random-bytes callbacks from the Win32 layer (CryptGenRandom
    // + QueryPerformanceCounter + CRITICAL_SECTION).  Like the POSIX
    // arm, Win32 also fills filesystem / loading / process-info /
    // stdio slots which we intentionally deny; deny_sandbox_unsafe_slots
    // below strips them regardless of what Win32 filled in here.
    //
    // TH8_PLATFORM_POSIX and TH8_PLATFORM_WIN32 are mutually exclusive
    // auto-defines from `Vendor/th8.h:156-162` (POSIX for everything
    // except _WIN32, WIN32 otherwise).  Th8_GetWin32Platform is
    // declared only under `_WIN32 || WIN32` in the same header, so this
    // arm compiles exactly when the symbol exists.
    Th8_MergePlatform(&platform, Th8_GetWin32Platform());
#endif

    // Explicitly NULL out the sandbox-unsafe slots regardless of merge
    // history.  This is the robust replacement for the audit's suggested
    // "reorder Posix-first, NullIO-last" (which the first-layer-wins
    // merge semantic makes impossible).  No sidecar callback is
    // installed at this point -- the WebPlatformContext overload below
    // sets xGetData explicitly after this returns.
    deny_sandbox_unsafe_slots(platform, /*has_sidecar_callback=*/false);

    return platform;
}

}

NonnullOwnPtr<PlatformDescriptor> create_web_content_platform()
{
    auto desc = make<PlatformDescriptor>();
    desc->mutable_raw() = build_web_content_platform_layers();
    return desc;
}

NonnullOwnPtr<PlatformDescriptor> create_web_content_platform(WebPlatformContext& context)
{
    auto desc = make<PlatformDescriptor>();
    auto& platform = desc->mutable_raw();
    platform = build_web_content_platform_layers();

    // Override the Null-I/O xGetData stub with our context-aware shim.
    // pCtx is the WebPlatformContext pointer; the thunk casts it back
    // and dispatches by resource name.
    platform.xGetData = th8_web_platform_x_get_data;
    platform.pCtx = &context;

    return desc;
}

}
