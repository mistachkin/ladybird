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

TH8 and JavaScript scripts on the same page are isolated: they cannot call each other's functions. However, both share the same DOM, so modifications made by one language are visible to the other.

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

## Limitations

- No inline event handlers (`onclick="th8:..."` syntax is not supported)
- Event handling is programmatic only (via future `dom::event bind` command)
- No source maps
- DevTools debugging is limited to pause/resume, console evaluation, and variable inspection
- Source-level breakpoints require future TH8 debug API additions
