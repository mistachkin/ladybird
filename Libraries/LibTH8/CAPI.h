/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Libraries/LibTH8/CAPI.h
 *
 * [L19] Curated subset of the vendored TH8 C API surface that LibTH8
 * consumers (LibWeb, LibDevTools, Tests/LibTH8) are permitted to use
 * directly.  The vendored Vendor/th8.h is the authoritative source;
 * this header MIRRORS only the symbols Ladybird actually calls so
 * the vendor directory can stay PRIVATE to LibTH8.
 *
 *   * Adding a new consumer-side `Th8_*` / `TH8_*` use requires
 *     mirroring it here first; the LibTH8 build will fail otherwise
 *     (because Vendor/ is no longer on the public include path).
 *   * The C ABI is unchanged: all declarations match Vendor/th8.h
 *     byte-for-byte.  Keep them in sync with the same `extern "C"`
 *     linkage and TH8_API export decoration.
 *   * LibTH8's own implementation files (Interpreter.cpp,
 *     WebPlatform.cpp) still include Vendor/th8.h directly; this
 *     curated header is only consumed by code OUTSIDE LibTH8.
 */

#pragma once

#include <stddef.h>

/*
 * TH8_API export decoration -- mirrors Vendor/th8.h: __declspec on
 * Windows DLL boundaries, default visibility on GCC/Clang, empty
 * elsewhere.  ENABLE_TH8 is forced OFF on Windows today so the
 * Windows branch is dead in practice but kept for symmetry with the
 * vendored header.
 */
#if !defined(TH8_API)
#  if defined(_WIN32) || defined(__CYGWIN__)
#    if defined(TH8_BUILD_STATIC)
#      define TH8_API
#    elif defined(TH8_BUILD_DLL)
#      define TH8_API __declspec(dllexport)
#    else
#      define TH8_API __declspec(dllimport)
#    endif
#  elif defined(__GNUC__) && __GNUC__ >= 4
#    define TH8_API __attribute__((visibility("default")))
#  else
#    define TH8_API
#  endif
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Opaque struct forward declarations.  Consumers manipulate these only
 * through pointers passed to the C API or to the C++ Interpreter
 * wrapper.  Field layout is intentionally NOT exposed.
 */
typedef struct Th8_Interp Th8_Interp;
typedef struct Th8_Platform Th8_Platform;
typedef struct Th8_KeyringEntry Th8_KeyringEntry;

/*
 * Integer typedefs used by Th8_* signatures consumers touch.
 */
typedef long long th8_int64_t;
typedef unsigned long long th8_uint64_t;

/*
 * Command and debug callback typedefs.  Bridge code registers procs of
 * these shapes via Th8_CreateCommand / Th8_SetDebugCallback.
 */
typedef int (*Th8_CommandProc)(Th8_Interp* interp, void* pCtx, int argc,
    const char** argv, size_t* argl);
typedef int (*Th8_DebugProc)(Th8_Interp* interp, int event, const char* zScript,
    size_t nScript, int nLine, int nLevel, void* pCtx);

/*
 * Status / return codes.
 */
#define TH8_OK     (0)
#define TH8_ERROR  (1)
#define TH8_BREAK  (3)

/*
 * "use the NUL-terminated length" sentinel for the size_t length
 * arguments accepted throughout the TH8 API.
 */
#define TH8_NOLEN  ((size_t)-1)

/*
 * Th8_CancelEval flag bits.  TH8_CANCEL_UNWIND forces the cancellation
 * past intervening [catch] frames.
 */
#define TH8_CANCEL_UNWIND (0x01)

/*
 * Debug event codes (subset of TH8_DEBUG_*).
 */
#define TH8_DEBUG_BREAKPOINT (2)

/*
 * Step modes (subset of TH8_STEP_*).
 */
#define TH8_STEP_NONE (0)

/*
 * Curated function prototypes.  KEEP IN SYNC with Vendor/th8.h.
 *
 * Result management -- used by TypeConversion.cpp helpers and the
 * DOM-bridge command implementations.
 */
TH8_API int Th8_SetResult(Th8_Interp* interp, const char* z, size_t n);
TH8_API int Th8_SetResultInt(Th8_Interp* interp, int iVal);
TH8_API void Th8_ClearResult(Th8_Interp* interp);
TH8_API const char* Th8_GetResult(Th8_Interp* interp, size_t* pN);

/*
 * Script evaluation -- the event-dispatch path in
 * Libraries/LibWeb/TH8/DOMBridge.cpp's add_th8_event_listener thunk
 * calls Th8_Eval directly so it can route the event dict in without
 * routing through the C++ Interpreter wrapper.
 */
TH8_API int Th8_Eval(Th8_Interp* interp, int iFrame, const char* zProg,
    size_t nProg, const char* zName, size_t nName);

/*
 * Command registration -- the DOM bridge registers every dom::*
 * command directly so each can carry its own per-command BridgeContext.
 */
TH8_API int Th8_CreateCommand(Th8_Interp* interp, const char* zName,
    Th8_CommandProc xProc, void* pContext,
    void (*xDel)(Th8_Interp*, void*), th8_uint64_t* pToken);

/*
 * Embedded trust-anchor keyring.  Resolved at link time -- the canonical
 * stub returns nEntries=0; embedders generate a real keyring via
 * `tclsh tools/mkkey.tcl --keyring` and link that file instead of the
 * stub.  See Libraries/LibTH8/Interpreter.cpp::
 * install_signed_only_policy_with_embedded_keyring.
 */
TH8_API const Th8_KeyringEntry* Th8_GetEmbeddedKeyring(size_t* pnEntries);

#if defined(__cplusplus)
}
#endif
