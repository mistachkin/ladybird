# TH8 Scripting Language Integration

Ladybird includes support for the TH8 scripting language as a non-standard alternative to JavaScript. TH8 is a minimal, security-first, Tcl-compatible scripting language with a platform abstraction layer that eliminates all direct C runtime dependencies.

## Usage

### Inline scripts

```html
<script type="text/th8">
set doc [dom::document]
set div [$doc createElement "div"]
$div textContent "Hello from TH8"
[$doc querySelector "body"] appendChild $div
</script>
```

### External scripts

```html
<script type="text/th8" src="script.th8"></script>
```

### Recognized MIME types

- `text/th8`
- `text/tcl`
- `th8` (bare type attribute)

## DOM Command Reference

### Document commands

```tcl
# Get the document handle
set doc [dom::document]

# Query elements
set elem [$doc querySelector ".my-class"]
set elem [$doc getElementById "my-id"]

# Create elements
set div [$doc createElement "div"]
set text [$doc createTextNode "Hello"]

# Access document properties
set body [$doc body]
set head [$doc head]
$doc title "New Title"        ;# setter
set t [$doc title]            ;# getter
```

### Element commands

```tcl
# Attributes
$elem getAttribute "class"
$elem setAttribute "class" "highlight"
$elem removeAttribute "data-old"
$elem hasAttribute "id"                  ;# returns "1" or "0"
$elem tagName                            ;# returns "DIV", "P", etc.

# Content
$elem textContent                        ;# getter
$elem textContent "New text"             ;# setter
$elem innerHTML                          ;# getter
$elem innerHTML "<b>Bold</b>"            ;# setter

# Query descendants
$elem querySelector ".child-class"
```

### Node tree manipulation

```tcl
$parent appendChild $child
$parent removeChild $child
$parent insertBefore $new $reference
$node parentNode
$node firstChild
$node lastChild
$node nextSibling
$node previousSibling
```

### Console

```tcl
dom::console log "message"
dom::console warn "warning message"
dom::console error "error message"
```

### Handle management

```tcl
# Release a handle when no longer needed
dom::release $handle
```

### Event handlers

```tcl
# Subscribe a TH8 procedure to a DOM event.
$elem addEventListener click on_click
```

The named procedure is invoked once per dispatch with one argument: a
TH8 dictionary describing the event.  The dictionary is built by
`serialize_event_to_dict` in `Libraries/LibWeb/TH8/DOMBridge.cpp` and
currently exposes the following keys:

| Key             | Source                          | TH8 type                              |
|-----------------|---------------------------------|---------------------------------------|
| `type`          | `Event::type`                   | string (e.g. `"click"`)               |
| `bubbles`       | `Event::bubbles`                | `0` or `1`                            |
| `cancelable`    | `Event::cancelable`             | `0` or `1`                            |
| `eventPhase`    | `Event::event_phase`            | integer (0=NONE / 1=CAPTURE / 2=AT_TARGET / 3=BUBBLE) |
| `target`        | `Event::target` (PlatformObject)| DOM-handle string (e.g. `obj42`)      |
| `currentTarget` | `Event::current_target`         | DOM-handle string                     |
| `timeStamp`     | `Event::time_stamp`             | integer (milliseconds since origin)   |

```tcl
proc on_click {evt} {
    set t [dict get $evt target]      ; # handle string
    dom::console log "click on $t"
}
[$doc getElementById btn] addEventListener click on_click
```

Event-subtype properties not in the table above (e.g.
`MouseEvent.clientX`, `KeyboardEvent.key`, `InputEvent.data`) are
**not** currently surfaced.  Adding a subtype requires extending
`serialize_event_to_dict` in `DOMBridge.cpp`; the table above is the
exhaustive surface that scripts can rely on today.

Event-handler runtime errors are routed to
`WindowOrWorkerGlobalScope::report_an_exception` so they appear in
DevTools and via `window.onerror` alongside JS exceptions.

## Per-document interpreter

All `<script type="text/th8">` blocks in the same document share a single TH8 interpreter instance. This means variables and procedures defined in one script block are visible to later script blocks and event handlers.

```html
<script type="text/th8">
set ::counter 0
proc increment {} {
    incr ::counter
}
</script>
<script type="text/th8">
increment
# ::counter is now 1
</script>
```

## Security model

TH8's security is enforced at the platform abstraction layer level. The web content platform:

- Blocks all file I/O (no file reading, writing, or listing)
- Blocks dynamic native code loading
- Blocks process information access
- Blocks environment variable access
- Blocks stdin/stdout/stderr access
- Enforces computation step limits (10 million operations)
- Enforces memory limits (64 MB per interpreter)
- Enforces stack overflow protection via NRE + stack guard

TH8 scripts can only interact with the page through the registered DOM bridge commands. The bridge respects the Same-Origin Policy for cross-frame access.

## TH8 and JavaScript interoperability

TH8 and JavaScript scripts on the same page are DOM-isolated by default: they cannot call each other's functions. However, both share the same DOM, so modifications made by one language are visible to the other.

```html
<script>
document.getElementById('target').textContent = 'Set by JS';
</script>
<script type="text/th8">
set doc [dom::document]
set elem [$doc getElementById "target"]
set text [$elem textContent]
# text == "Set by JS"
</script>
```

### Opt-in cross-eval

Documents that set:

```html
<meta http-equiv="TH8-Script-Policy" content="cross-eval">
```

enable direct cross-language evaluation in both directions:

| Direction | Surface                                       | Source                                          |
|-----------|-----------------------------------------------|-------------------------------------------------|
| TH8 → JS  | `dom::eval_js <script>` command               | `Libraries/LibWeb/TH8/DOMBridge.cpp`            |
| JS → TH8  | `window.th8.eval(script)` global method       | `Libraries/LibWeb/TH8/WindowTH8Namespace.cpp`   |

Both calls return the evaluated source's result as a string;
exceptions / TH8 errors propagate as JS `Error` objects.  The
per-document `TH8Context::evaluate` reentrancy guard rejects nested
top-level evaluations, so a JS→TH8→JS→TH8 chain fails cleanly at the
second TH8 frame rather than recursing.

`cross-eval` is mutually exclusive with `no-javascript`; if both are
declared, `cross-eval` is dropped.

## Limitations

- No inline event handlers (`onclick="th8:..."` syntax is not supported);
  use `$elem addEventListener type procName` instead (see above).
- Only the event-property surface listed in the
  [Event handlers](#event-handlers) section is exposed.  Subtype
  properties (e.g. `MouseEvent.clientX`) require extending
  `serialize_event_to_dict` in `DOMBridge.cpp`.
- No source maps
- DevTools debugging is limited to pause/resume, console evaluation, and variable inspection
- Source-level breakpoints require future TH8 debug API additions

## Embedder API surface

This section is for **C++ embedders** of LibTH8 -- downstream
projects linking LibTH8 directly (Ladybird is itself such an
embedder).  Scripts on a page do not have access to any of this.

### Gating LibTH8 at build time

The top-level CMake option `ENABLE_TH8` (default `ON`, forced `OFF`
on Windows; see `Meta/CMake/cmake_options.cmake`) controls whether
LibTH8 builds and links into LibWeb / LibDevTools / LibTest at all.
When `OFF` the `LADYBIRD_ENABLE_TH8` compile definition is `0` and
all TH8 entry points in LibWeb are stubbed out -- there is no
runtime cost.  Configure with `-DENABLE_TH8=OFF` to produce a
JS-only build.

### Registering a new built-in command from C++

Built-in commands live alongside the existing DOM bridge in
`Libraries/LibWeb/TH8/DOMBridge.cpp`.  The minimum pattern:

```cpp
static int my_command(Th8_Interp* interp, void* ctx, int argc,
    char const** argv, size_t* argl)
{
    // argc/argv/argl follow the standard TH8 command convention;
    // argv[0] is the command name.  Return TH8_OK / TH8_ERROR.
    if (argc != 2)
        return set_error(interp, "wrong # args"sv);
    return set_result_string(interp, "ok"sv);
}

// Register inside register_dom_commands() in DOMBridge.cpp:
interpreter.create_command("my_command"sv,
    reinterpret_cast<Th8_CommandProc>(my_command),
    /*context=*/nullptr,
    /*destructor=*/nullptr,
    /*flags=*/nullptr);
```

For commands that need per-interpreter state, allocate a
`BridgeContext`-style struct and supply a destructor callback so
TH8 frees it on `Th8_DeleteInterp`.

### Custom platform layer

`Libraries/LibTH8/WebPlatform.cpp` builds a 79-slot `Th8_Platform`
struct.  Embedders that need different sandbox semantics can
construct their own.  The high-level recipe:

1. Start from `Th8_GetDefaultPlatform()` (full set of safe defaults).
2. Override only the slots that differ -- for example, a stricter
   `xRandomBytes`, a different `xCurrentTime`, or a custom
   `xGetData` to back the signed-only policy with a different
   signature source.
3. Pass the result to `::TH8::Interpreter::create(platform)`.

Slots that the web sandbox **must** NULL (filesystem, process, env,
network) are listed in `deny_sandbox_unsafe_slots` in
`WebPlatform.cpp`; mirror that function in any custom platform.

### Plugging in a custom signing key

Build-time trust anchors live in a single C source file shaped like
`Libraries/LibTH8/Vendor/th8_keyring_stub.c`.  The canonical empty
stub returns `nEntries=0`, so signed-only documents reject every
script.  Generate a real keyring:

```sh
tclsh tools/mkkey.tcl --keyring -o keyring.c key1.snk key2.snk
```

Then link `keyring.c` instead of the stub by listing it before the
stub in the embedder's CMake target sources, or by removing the
stub from the LibTH8 SOURCES list and substituting your file.  The
public `Th8_GetEmbeddedKeyring()` API yields the entries to
`Th8_PolicyPreloadKey` at `Interpreter::install_signed_only_policy_with_embedded_keyring()`
time.

### Gating TH8 at runtime

Per-document toggles are exposed through `<meta http-equiv="TH8-Script-Policy">`
and the matching HTTP header (see [Security model](#security-model)).
Embedders that want to disable TH8 on a specific document without
touching the build flag can call:

```cpp
document.set_th8_disabled(true);
```

After this call:
- every `<script type="text/th8">` (and `text/th8+signed`, `text/tcl`,
  `th8`) on `document` is rejected at `execute_script` time with an
  `error` event,
- `window.th8.eval` throws a `TypeError` with message
  `"TH8 is disabled on this document"`.

The flag is independent of `ENABLE_TH8` (build-time) and of the
`TH8-Script-Policy` directives (signed-only / no-javascript /
cross-eval) -- it overrides them all.  Resetting via
`set_th8_disabled(false)` restores normal behavior.
