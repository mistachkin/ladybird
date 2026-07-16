/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/Noncopyable.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/StringView.h>
#include <AK/Types.h>
#include <LibTH8/CAPI.h>

namespace TH8 {

class TH8_API Interpreter {
    AK_MAKE_NONCOPYABLE(Interpreter);
    AK_MAKE_NONMOVABLE(Interpreter);

public:
    static ErrorOr<NonnullOwnPtr<Interpreter>> create(Th8_Platform const&);
    ~Interpreter();

    Th8_Interp* raw() { return m_interp; }
    Th8_Interp const* raw() const { return m_interp; }

    int evaluate(StringView script, StringView name = {});
    StringView result_string() const;

    void set_step_limit(i64 limit);
    i64 step_limit() const;

    void set_memory_limit(size_t limit);
    size_t memory_limit() const;

    void freeze();
    void thaw();

    bool is_canceled() const;
    void cancel(StringView message = {});
    void reset_cancel();

    int create_command(StringView name, Th8_CommandProc proc, void* context,
        void (*destructor)(Th8_Interp*, void*) = nullptr,
        th8_uint64_t* token = nullptr);

    int set_variable(StringView name, StringView value);
    int get_variable(StringView name);
    bool variable_exists(StringView name);

    // Script signing and verification.
    // When enabled, only scripts with valid RSA-SHA512 signatures
    // are evaluated.  This maps to Content Security Policy concepts
    // in the browser -- the signing key acts as a trust anchor.
    //
    // Usage:
    //   interp->install_signed_only_policy();
    //   interp->preload_signing_key(snk_data);  // .snk key blob
    //   interp->evaluate(script);               // only signed scripts pass
    int install_signed_only_policy();
    void remove_signed_only_policy();
    int preload_signing_key(StringView snk_key_blob);

    // Install the signed-only policy and preload every key in the
    // build-time embedded keyring (see Th8_GetEmbeddedKeyring in
    // th8.h).  Returns the number of keys actually preloaded; zero
    // is a valid result if the canonical empty stub keyring is
    // linked.  Returns -1 if the policy could not be installed.
    int install_signed_only_policy_with_embedded_keyring();

    bool is_signed_only() const;

    // Script debugging.
    int set_debug_callback(Th8_DebugProc callback, void* context);
    int set_breakpoint(StringView script, int line);
    int clear_breakpoint(int breakpoint_id);
    int clear_all_breakpoints();
    int set_step_mode(int mode);
    int step_mode() const;
    int frame_count() const;
    int frame_info(int frame_index, char const** proc_name, size_t* proc_length,
        char const** script_name, size_t* script_length, int* line) const;
    int eval_at_frame(int frame_index, StringView script);
    bool is_suspended() const;

private:
    explicit Interpreter(Th8_Interp*);

    Th8_Interp* m_interp;
    void* m_policy_ctx { nullptr };
};

}
