# TH8 Integration Architecture

This document describes the internal architecture of TH8 scripting language integration in Ladybird.

## Component overview

```
Libraries/LibTH8/                    TH8 library wrapper
    Vendor/th8.h, th8.c             Unmodified amalgamation
    Interpreter.h/cpp               RAII C++ wrapper
    WebPlatform.h/cpp               Secure platform layer

Libraries/LibWeb/HTML/Scripting/     Script pipeline integration
    TH8Script.h/cpp                 Script class (analogous to ClassicScript)
    TH8Context.h/cpp                Per-document interpreter context

Libraries/LibWeb/TH8/               DOM bridge
    HandleTable.h/cpp               GC-integrated object handle table
    TypeConversion.h/cpp            DOM <-> TH8 type conversion
    DOMBridge.h/cpp                 Command registration and dispatch

Libraries/LibDevTools/Actors/        DevTools integration
    ThreadActor.h/cpp               Debugger protocol (attach/resume/interrupt)
    ConsoleActor.cpp                TH8 console evaluation
```

## Script execution flow

1. HTML parser encounters `<script type="text/th8">`
2. `HTMLScriptElement::prepare_script()` detects `ScriptType::TH8`
3. For inline scripts: `TH8Script::create()` stores the source text
4. For external scripts: `fetch_th8_script()` fetches via network, creates `TH8Script`
5. `HTMLScriptElement::execute_script()` calls `TH8Script::run()`
6. `TH8Script::run()` calls `Document::ensure_th8_context()` to get/create the interpreter
7. `TH8Context::evaluate()` calls `Th8_Eval()` on the shared interpreter
8. DOM bridge commands dispatch to Ladybird DOM APIs
9. Microtask checkpoint is performed after evaluation

## Handle table and GC

The `HandleTable` uses `GC::RootHashMap<u64, PlatformObject*>` to map handle IDs to DOM objects. This ensures:

- Objects referenced by TH8 handles are not prematurely collected
- Same object always gets the same handle (deduplication)
- Destroying the handle table releases all GC roots
- Maximum 10,000 handles prevents resource exhaustion

When a TH8 command like `$elem setAttribute "class" "foo"` executes:
1. The ensemble dispatch resolves `$elem` (e.g., "elem42") via `HandleTable::resolve()`
2. The returned `PlatformObject*` is cast to `DOM::Element&`
3. `Element::set_attribute()` is called
4. Return value is converted back to a TH8 string result

## Platform security layer

The web content platform is composed from TH8's built-in layers:

```
NullIO (blocks all I/O) + Libc (memory) + macOS (zone alloc) + POSIX (mutex, time)
```

`Th8_MergePlatform()` fills NULL slots only, so NullIO stubs take precedence over POSIX I/O callbacks. This means:
- File read/write: blocked (NullIO returns empty/error)
- Dynamic loading: blocked
- stdin/stdout: blocked
- Memory allocation: allowed (via libc malloc with TH8 limit enforcement)
- Mutexes: allowed (for thread safety)
- Time: allowed (for clock commands)

## Adding new DOM commands

To expose a new DOM API to TH8:

1. Add a new subcommand case to `object_ensemble_command()` in `DOMBridge.cpp`
2. Use `TypeConversion.h` helpers for argument/return value conversion
3. Use `set_result_node()` for DOM objects (registers handle automatically)
4. Use `set_error()` for error cases (returns `TH8_ERROR`)
5. Add corresponding test in `Tests/LibWeb/Text/input/TH8/`

## Script debugging API

TH8 provides a complete source-level debugging API integrated into
`Th8_Ready()`, the work-unit boundary check.  When no debug callback
is installed, the overhead is a single pointer test (zero cost).

### C API

```c
// Debug callback — fires at every command boundary.
// Return TH8_OK to continue, TH8_BREAK to suspend.
typedef int (*Th8_DebugProc)(Th8_Interp*, int event,
    const char *script, size_t nScript,
    int line, int depth, void *ctx);

Th8_SetDebugCallback(interp, callback, clientData)
Th8_SetBreakpoint(interp, script, nScript, line, &breakpointId)
Th8_ClearBreakpoint(interp, breakpointId)
Th8_ClearAllBreakpoints(interp)
Th8_SetStepMode(interp, TH8_STEP_INTO|OVER|OUT|NONE)
Th8_GetStepMode(interp)
Th8_GetFrameCount(interp)
Th8_GetFrameInfo(interp, index, &proc, &nProc, &script, &nScript, &line)
Th8_EvalAtFrame(interp, index, script, nScript)
```

### C++ wrapper (Interpreter class)

```cpp
interpreter->set_debug_callback(callback, context);
interpreter->set_breakpoint(script, line);  // returns ID
interpreter->clear_breakpoint(breakpoint_id);
interpreter->clear_all_breakpoints();
interpreter->set_step_mode(TH8_STEP_INTO);
interpreter->frame_count();
interpreter->frame_info(index, &proc, &len, &script, &len, &line);
interpreter->eval_at_frame(index, script);
interpreter->is_suspended();
```

### DevTools integration

`ThreadActor` is wired to the debug API:
- `attach` → installs debug callback
- `detach` → removes callback, clears breakpoints
- `interrupt` → `freeze()`
- `resume` → `set_step_mode(NONE)` + `thaw()`
- `frames` → iterates `frame_info()` for all frames
- `setBreakpoint` → `set_breakpoint(source, line)`
- `removeBreakpoint` → `clear_breakpoint(id)`

The NRE trampoline preserves the evaluation state on the heap,
so freeze/thaw is a true continuation: `thaw()` + re-invoke
`evaluate()` resumes from exactly where it paused.
