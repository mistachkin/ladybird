/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTH8/Interpreter.h>

#include <AK/ByteString.h>

// [L19] Interpreter.h now reaches the vendored TH8 C API through the
// curated CAPI.h header.  The implementation file still needs the full
// upstream surface, so pull <th8.h> in here (resolved via LibTH8's
// PRIVATE include of Vendor/).
#include <th8.h>

namespace TH8 {

ErrorOr<NonnullOwnPtr<Interpreter>> Interpreter::create(Th8_Platform const& platform)
{
    auto* mutable_platform = const_cast<Th8_Platform*>(&platform);

    // [H9] Th8_Initialize sets up PROCESS-GLOBAL library state (main-thread
    // allocator registration, the global mutex, and a copy of the platform)
    // and deliberately returns TH8_ERROR when called more than once.  A single
    // WebContent process hosts many documents -- each Document builds its own
    // TH8Context and Interpreter, and processes are reused across navigations
    // -- so initializing per interpreter succeeds for the first document and
    // fails ("already initialized") for every one after it, leaving all later
    // documents without a working TH8 interpreter.  Initialize the library
    // exactly once per process via a magic static (C++ guarantees thread-safe,
    // run-once initialization) and treat that single result as authoritative
    // for every interpreter.  Each interpreter still receives its own
    // per-document platform through Th8_CreateInterp below; the copy
    // Th8_Initialize takes of this first platform only seeds global
    // mutex/allocator state, which is identical across every web platform.
    // Th8_Finalize is intentionally never called: the global state lives for
    // the whole process and is reclaimed by the OS at exit.
    static int const s_library_init_rc = Th8_Initialize(mutable_platform);
    if (s_library_init_rc != TH8_OK)
        return Error::from_string_literal("Failed to initialize TH8 platform");

    auto* interp = Th8_CreateInterp(mutable_platform);
    if (!interp)
        return Error::from_string_literal("Failed to create TH8 interpreter");

    // [H10] A bare Th8_CreateInterp interpreter has NO commands registered --
    // not even `set`, `if`, `expr`, or `proc`.  Th8_RegisterLanguage installs
    // the built-in language commands and must be called exactly once per
    // interpreter to make it usable as a scripting engine.  These are pure
    // computation commands (control flow, variables, string/list ops); all
    // I/O, loading, and process access remain denied by the sandboxed
    // platform, so registering the full language is safe for web content.
    if (Th8_RegisterLanguage(interp) != TH8_OK) {
        Th8_DeleteInterp(interp);
        return Error::from_string_literal("Failed to register TH8 language commands");
    }

    return adopt_own(*new Interpreter(interp));
}

Interpreter::Interpreter(Th8_Interp* interp)
    : m_interp(interp)
{
}

Interpreter::~Interpreter()
{
    if (m_interp) {
        remove_signed_only_policy();
        Th8_DeleteInterp(m_interp);
    }
}

int Interpreter::evaluate(StringView script, StringView name)
{
    return Th8_Eval(m_interp, 0,
        script.characters_without_null_termination(), script.length(),
        name.is_empty() ? nullptr : name.characters_without_null_termination(),
        name.length());
}

StringView Interpreter::result_string() const
{
    size_t length = 0;
    char const* result = Th8_GetResult(m_interp, &length);
    if (!result || length == 0)
        return {};
    return { result, length };
}

void Interpreter::set_step_limit(i64 limit)
{
    Th8_SetStepLimit(m_interp, static_cast<th8_int64_t>(limit));
}

i64 Interpreter::step_limit() const
{
    return static_cast<i64>(Th8_GetStepLimit(m_interp));
}

void Interpreter::set_memory_limit(size_t limit)
{
    Th8_SetAllocLimit(m_interp, limit);
}

size_t Interpreter::memory_limit() const
{
    return Th8_GetAllocLimit(m_interp);
}

void Interpreter::freeze()
{
    Th8_Freeze(m_interp);
}

void Interpreter::thaw()
{
    Th8_Thaw(m_interp);
}

bool Interpreter::is_canceled() const
{
    return Th8_IsCanceled(const_cast<Th8_Interp*>(m_interp), 0) != 0;
}

void Interpreter::cancel(StringView message)
{
    if (message.is_empty()) {
        Th8_CancelEval(m_interp, nullptr, 0, 0);
    } else {
        Th8_CancelEval(m_interp,
            message.characters_without_null_termination(),
            message.length(), 0);
    }
}

void Interpreter::reset_cancel()
{
    Th8_ResetCancel(m_interp);
}

int Interpreter::create_command(StringView name, Th8_CommandProc proc, void* context,
    void (*destructor)(Th8_Interp*, void*),
    th8_uint64_t* token)
{
    // Th8_CreateCommand expects a null-terminated string for the command name.
    // StringView may not be null-terminated, so we need a ByteString copy.
    auto name_string = ByteString(name);
    return Th8_CreateCommand(m_interp, name_string.characters(), proc, context, destructor, token);
}

int Interpreter::set_variable(StringView name, StringView value)
{
    return Th8_SetVar(m_interp,
        name.characters_without_null_termination(), name.length(),
        value.characters_without_null_termination(), value.length());
}

int Interpreter::get_variable(StringView name)
{
    return Th8_GetVar(m_interp,
        name.characters_without_null_termination(), name.length());
}

bool Interpreter::variable_exists(StringView name)
{
    return Th8_ExistsVar(m_interp,
        name.characters_without_null_termination(), name.length()) != 0;
}

int Interpreter::install_signed_only_policy()
{
    void* ctx = nullptr;
    int rc = Th8_InstallSignedPolicy(m_interp, &ctx);
    if (rc != TH8_OK)
        return rc;
    m_policy_ctx = ctx;
    return TH8_OK;
}

void Interpreter::remove_signed_only_policy()
{
    if (m_policy_ctx) {
        Th8_RemoveSignedPolicy(m_interp, m_policy_ctx);
        m_policy_ctx = nullptr;
    }
}

int Interpreter::preload_signing_key(StringView snk_key_blob)
{
    if (!m_policy_ctx)
        return TH8_ERROR;

    // Load the .snk key blob into an Th8_RsaKey, then preload it.
    Th8_RsaKey* key = nullptr;
    int rc = Th8_RsaKeyLoad(m_interp,
        reinterpret_cast<unsigned char const*>(snk_key_blob.characters_without_null_termination()),
        snk_key_blob.length(), &key);
    if (rc != TH8_OK || !key)
        return TH8_ERROR;

    return Th8_PolicyPreloadKey(m_interp, m_policy_ctx, key);
}

int Interpreter::install_signed_only_policy_with_embedded_keyring()
{
    if (install_signed_only_policy() != TH8_OK)
        return -1;

    size_t entry_count = 0;
    auto const* entries = Th8_GetEmbeddedKeyring(&entry_count);

    int preloaded = 0;
    if (entries && entry_count > 0) {
        for (size_t i = 0; i < entry_count; ++i) {
            if (!entries[i].zData || entries[i].nData == 0)
                continue;
            Th8_RsaKey* key = nullptr;
            int rc = Th8_RsaKeyLoad(m_interp, entries[i].zData, entries[i].nData, &key);
            if (rc != TH8_OK || !key)
                continue;
            if (Th8_PolicyPreloadKey(m_interp, m_policy_ctx, key) == TH8_OK)
                ++preloaded;
        }
    }

    // Turn on signed-only enforcement so every Th8_Eval after this point
    // routes through the policy chain (signature check + verified-script
    // re-entrancy bookkeeping).  Even with zero preloaded keys, enabling
    // this is the right thing: an attacker-controlled unsigned script
    // cannot run, which is the conservative default for the empty-stub
    // keyring case.
    Th8_EnableSignedOnly(m_interp, 1);

    return preloaded;
}

bool Interpreter::is_signed_only() const
{
    return m_policy_ctx != nullptr;
}

int Interpreter::set_debug_callback(Th8_DebugProc callback, void* context)
{
    return Th8_SetDebugCallback(m_interp, callback, context);
}

int Interpreter::set_breakpoint(StringView script, int line)
{
    int breakpoint_id = 0;
    int rc = Th8_SetBreakpoint(m_interp,
        script.characters_without_null_termination(), script.length(),
        line, &breakpoint_id);
    if (rc != TH8_OK)
        return -1;
    return breakpoint_id;
}

int Interpreter::clear_breakpoint(int breakpoint_id)
{
    return Th8_ClearBreakpoint(m_interp, breakpoint_id);
}

int Interpreter::clear_all_breakpoints()
{
    return Th8_ClearAllBreakpoints(m_interp);
}

int Interpreter::set_step_mode(int mode)
{
    return Th8_SetStepMode(m_interp, mode);
}

int Interpreter::step_mode() const
{
    return Th8_GetStepMode(const_cast<Th8_Interp*>(m_interp));
}

int Interpreter::frame_count() const
{
    return Th8_GetFrameCount(const_cast<Th8_Interp*>(m_interp));
}

int Interpreter::frame_info(int frame_index,
    char const** proc_name, size_t* proc_length,
    char const** script_name, size_t* script_length,
    int* line) const
{
    return Th8_GetFrameInfo(const_cast<Th8_Interp*>(m_interp),
        frame_index, proc_name, proc_length,
        script_name, script_length, line);
}

int Interpreter::eval_at_frame(int frame_index, StringView script)
{
    return Th8_EvalAtFrame(m_interp, frame_index,
        script.characters_without_null_termination(), script.length());
}

bool Interpreter::is_suspended() const
{
    return Th8_IsSuspended(const_cast<Th8_Interp*>(m_interp)) != 0;
}

}
