/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTH8/Interpreter.h>

namespace TH8 {

ErrorOr<NonnullOwnPtr<Interpreter>> Interpreter::create(Th8_Platform const& platform)
{
    auto* mutable_platform = const_cast<Th8_Platform*>(&platform);

    int rc = Th8_Initialize(mutable_platform);
    if (rc != TH8_OK)
        return Error::from_string_literal("Failed to initialize TH8 platform");

    auto* interp = Th8_CreateInterp(mutable_platform);
    if (!interp) {
        Th8_Finalize(mutable_platform);
        return Error::from_string_literal("Failed to create TH8 interpreter");
    }

    return adopt_own(*new Interpreter(interp));
}

Interpreter::Interpreter(Th8_Interp* interp)
    : m_interp(interp)
{
}

Interpreter::~Interpreter()
{
    if (m_interp)
        Th8_DeleteInterp(m_interp);
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
    void* (*copy)(Th8_Interp*, void*))
{
    // Th8_CreateCommand expects a null-terminated string for the command name.
    // StringView may not be null-terminated, so we need a ByteString copy.
    auto name_string = ByteString(name);
    return Th8_CreateCommand(m_interp, name_string.characters(), proc, context, destructor, copy);
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
