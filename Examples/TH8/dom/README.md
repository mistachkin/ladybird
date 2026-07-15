# TH8 DOM Scripting Examples

Small, self-contained example pages showing how to script the DOM with
TH8 through the LadyBird bridge (`register_dom_commands` in
`Libraries/LibWeb/TH8/DOMBridge.cpp`).  Each page declares a
`TH8-Script-Policy` and drives the page with a `<script type="text/th8">`
block instead of JavaScript.  They are copy-pasteable starting points
and double as living documentation for the command surface described in
`Documentation/TH8Integration.md`.

Open any file in a `LADYBIRD_ENABLE_TH8` build; when TH8 is disabled the
`text/th8` scripts are simply ignored and the pages render inert.

## Pages

| File | Demonstrates | Bridge surface exercised |
|------|--------------|--------------------------|
| `element-lookup.html` | Resolve elements by id and CSS selector; read text and attributes | `dom::document`, `getElementById`, `querySelector`, `textContent`, `getAttribute` |
| `create-insert.html` | Build and insert new nodes, then read the tree back | `createElement`, `createTextNode`, `setAttribute`, `appendChild`, `firstChild`, `nextSibling` |
| `traverse.html` | Walk the node tree in both directions | `firstChild`, `lastChild`, `nextSibling`, `parentNode`, `tagName` |
| `events.html` | Subscribe a TH8 proc to DOM events and read the event dictionary | `addEventListener`, event dict (`type`/`target`/…), `dom::console` |
| `cross-eval.html` | Two-way TH8 ⟷ JavaScript evaluation | `dom::eval_js`, `window.th8.eval` |

## Command surface at a glance

Document-scoped subcommands are available both directly on
`dom::document` and on a captured document handle:

```tcl
set doc [dom::document]
set el  [$doc getElementById "my-id"]     ;# also: dom::document getElementById "my-id"
set el  [$doc querySelector ".item"]
set new [$doc createElement "li"]
set txt [$doc createTextNode "hello"]
set b   [$doc body]                        ;# head, title likewise
```

Node/element handles support:

```tcl
$el getAttribute name ; $el setAttribute name value
$el removeAttribute name ; $el hasAttribute name ; $el tagName
$el textContent ?value? ; $el innerHTML ?value? ; $el querySelector sel
$parent appendChild $child ; $parent removeChild $child
$parent insertBefore $new ?$ref?
$node parentNode ; $node firstChild ; $node lastChild
$node nextSibling ; $node previousSibling
$el addEventListener type procName
dom::console log|warn|error message
dom::release $handle
dom::eval_js script                        ;# requires the cross-eval policy
```

An event handler is called with one argument, a TH8 dictionary with the
keys `type`, `bubbles`, `cancelable`, `eventPhase`, `target`,
`currentTarget`, and `timeStamp`.  Event-subtype properties
(`MouseEvent.clientX`, `KeyboardEvent.key`, …) are not surfaced; see
`Documentation/TH8Integration.md`.

## Note on the document handle

`set doc [dom::document]; $doc getElementById "x"` works because
`object_ensemble_command` in `DOMBridge.cpp` delegates the
document-scoped subcommands (`getElementById`, `createElement`,
`createTextNode`, `body`, `head`, `title`) to the shared document
dispatch when the handle is the `Document`.  Without that delegation
only `dom::document getElementById "x"` (calling the fixed command
directly) would resolve.

## Testing the TH8 logic without a browser

The TH8 in these pages is verified independently of a browser build by a
smoke harness in the TH8 repository at `examples/dom/`
(`dom_bridge_mock.tcl` mocks this exact command surface;
`dom_smoke.tcl` drives the same example procedures and asserts the
results). Run it there with `./bin/th8sh examples/dom/dom_smoke.tcl`.
