/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/TH8/DOMBridge.h>

#include <AK/ByteString.h>
#include <AK/Utf16String.h>
#include <LibJS/Runtime/NativeFunction.h>
#include <LibJS/Runtime/PrimitiveString.h>
#include <LibJS/Runtime/ValueInlines.h>
#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/DOM/DOMEventListener.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Element.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/DOM/EventTarget.h>
#include <LibWeb/DOM/IDLEventListener.h>
#include <LibWeb/DOM/Node.h>
#include <LibWeb/DOM/ParentNode.h>
#include <LibWeb/DOM/Text.h>
#include <LibWeb/HTML/HTMLHeadElement.h>
#include <LibWeb/HTML/Scripting/ClassicScript.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Scripting/TH8Context.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/HTML/WindowOrWorkerGlobalScope.h>
#include <LibWeb/TH8/HandleTable.h>
#include <LibWeb/TH8/TypeConversion.h>
#include <LibWeb/TrustedTypes/TrustedHTML.h>
#include <LibWeb/WebIDL/CallbackType.h>

namespace Web::TH8 {

// Helper: clear result to NULL (no result) and return TH8_OK.
static int result_clear(Th8_Interp* interp)
{
    Th8_ClearResult(interp);
    return TH8_OK;
}

// Context struct passed to TH8 command callbacks via pCtx.
struct BridgeContext {
    DOM::Document* document;
    HandleTable* handles;
    Th8_Interp* interp;
};

// Forward declarations of command implementations.
static int document_command(Th8_Interp* interp, void* ctx, int argc, char const** argv, size_t* argl);
static int console_command(Th8_Interp* interp, void* ctx, int argc, char const** argv, size_t* argl);
static int object_ensemble_command(Th8_Interp* interp, void* ctx, int argc, char const** argv, size_t* argl);
static int release_command(Th8_Interp* interp, void* ctx, int argc, char const** argv, size_t* argl);
static int eval_js_command(Th8_Interp* interp, void* ctx, int argc, char const** argv, size_t* argl);

// Helper to resolve a handle from argv and get the PlatformObject.
static Bindings::PlatformObject* resolve_handle(HandleTable& handles, char const* str, size_t len)
{
    return handles.resolve(StringView { str, len });
}

// ---- Event Listener Support ----

// Serialize a DOM Event's key properties into a TH8 dict string.
// The dict is a Tcl-formatted key-value list suitable for [dict get].
static ByteString serialize_event_to_dict(DOM::Event const& event, HandleTable& handles)
{
    StringBuilder dict;

    // type
    dict.append("type {"sv);
    dict.append(event.type().bytes_as_string_view());
    dict.append("} "sv);

    // bubbles, cancelable
    dict.append("bubbles {"sv);
    dict.append(event.bubbles() ? "1"sv : "0"sv);
    dict.append("} cancelable {"sv);
    dict.append(event.cancelable() ? "1"sv : "0"sv);
    dict.append("} "sv);

    // eventPhase
    dict.append("eventPhase {"sv);
    dict.append(ByteString::number(event.event_phase()));
    dict.append("} "sv);

    // target handle (if resolvable)
    if (auto target = event.target()) {
        if (is<Bindings::PlatformObject>(*target)) {
            auto& platform_obj = static_cast<Bindings::PlatformObject&>(*target);
            auto handle = handles.register_handle(platform_obj);
            if (!handle.is_empty()) {
                dict.append("target {"sv);
                dict.append(handle);
                dict.append("} "sv);
            }
        }
    }

    // currentTarget handle (if resolvable)
    if (auto current = event.current_target()) {
        if (is<Bindings::PlatformObject>(*current)) {
            auto& platform_obj = static_cast<Bindings::PlatformObject&>(*current);
            auto handle = handles.register_handle(platform_obj);
            if (!handle.is_empty()) {
                dict.append("currentTarget {"sv);
                dict.append(handle);
                dict.append("} "sv);
            }
        }
    }

    // timeStamp
    dict.append("timeStamp {"sv);
    dict.append(ByteString::number(event.time_stamp()));
    dict.append('}');

    return dict.to_byte_string();
}

// Register a TH8 proc as a DOM event listener on an EventTarget.
// Creates a JS NativeFunction wrapper that, when invoked by the DOM
// event system, evaluates the TH8 proc with the event dict as its argument.
//
// [H2, audit follow-up] The lambda does NOT capture the Th8_Interp or
// HandleTable directly.  Both live inside the per-document TH8Context,
// which may be GC'd while the listener's EventTarget is still alive
// (shadow-tree moves, adoption, navigation).  Instead we capture only
// the proc name (a copyable ByteString) and look the interpreter +
// handles up FRESH from the dispatched event's current_target -> node
// -> document -> th8_context chain.  Every link in the chain is
// null-checked so a stale invocation against a destroyed TH8Context
// returns cleanly without dereferencing freed memory.
//
// vm.argument(0) is also guarded with is<DOM::Event> rather than the
// previous unchecked as<>: if a TH8 listener is invoked via a non-
// EventListener code path (e.g., as a plain JS callable), the cast
// would crash; the guard returns undefined instead.
static int add_th8_event_listener(
    DOM::EventTarget& target,
    FlyString const& event_type,
    ByteString proc_name,
    Th8_Interp* /*interp_unused*/,
    HandleTable& /*handles_unused*/,
    DOM::Document& document)
{
    auto& realm = document.realm();

    auto callback_function = JS::NativeFunction::create(
        realm,
        [proc_name](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            if (vm.argument_count() < 1)
                return JS::js_undefined();

            auto argument = vm.argument(0);
            if (!argument.is_object() || !is<DOM::Event>(argument.as_object()))
                return JS::js_undefined();
            auto& event = static_cast<DOM::Event&>(argument.as_object());

            // Look up the current TH8 interpreter via the dispatched
            // event's current target -> document -> th8_context.  Any
            // missing link short-circuits to undefined (fail-closed).
            auto current_target = event.current_target();
            if (!current_target)
                return JS::js_undefined();
            auto target_as_node = as_if<DOM::Node>(current_target.ptr());
            if (!target_as_node)
                return JS::js_undefined();
            auto& live_document = target_as_node->document();
            auto live_context = live_document.th8_context();
            if (!live_context || !live_context->is_alive_for_dispatch())
                return JS::js_undefined();
            auto* live_interp = live_context->interpreter().raw();
            auto& live_handles = live_context->handle_table();

            // Serialize event to TH8 dict using the live handle table.
            auto event_dict = serialize_event_to_dict(event, live_handles);

            // Build the TH8 command: procName {event_dict}
            auto script = ByteString::formatted("{} {{{}}}", proc_name, event_dict);

            // Evaluate in the TH8 interpreter.
            int rc = Th8_Eval(live_interp, 0,
                script.characters(), script.length(),
                "event", 5);

            if (rc != TH8_OK) {
                size_t err_len = 0;
                auto const* err = Th8_GetResult(live_interp, &err_len);
                StringView err_view { err ? err : "", err_len };
                dbgln("[TH8 event error] {}: {}", proc_name, err_view);
                // [M9] Surface event-handler failures through the
                // canonical exception-reporting path so they appear
                // in DevTools / the page's `window.onerror` /
                // `window.report_an_exception` flow alongside JS
                // errors -- a silent dbgln was easy to miss.
                auto& realm = vm.current_realm() ? *vm.current_realm() : live_document.realm();
                auto error = JS::Error::create(realm,
                    String::from_utf8_with_replacement_character(err_view));
                auto& window_or_worker = as<HTML::WindowOrWorkerGlobalScopeMixin>(realm.global_object());
                window_or_worker.report_an_exception(error);
            }

            return JS::js_undefined();
        },
        0, Utf16FlyString {}, &realm);

    auto callback = realm.heap().allocate<WebIDL::CallbackType>(*callback_function, realm);
    auto listener = realm.heap().allocate<DOM::DOMEventListener>();
    listener->type = event_type;
    listener->callback = DOM::IDLEventListener::create(realm, *callback);

    target.add_an_event_listener(*listener);
    return TH8_OK;
}

// Destructor callback for the bridge context.  Called by TH8
// when the command owning this context is deleted (interpreter
// cleanup).  Frees the heap-allocated BridgeContext.
static void bridge_context_destructor(Th8_Interp*, void* ctx)
{
    delete static_cast<BridgeContext*>(ctx);
}

void register_dom_commands(Th8_Interp* interp, DOM::Document& document, HandleTable& handles)
{
    // Allocate bridge context on the heap; freed by bridge_context_destructor
    // when the dom::document command is deleted during interpreter cleanup.
    auto* ctx = new BridgeContext { &document, &handles, interp };

    // The first command owns the context and registers the destructor.
    Th8_CreateCommand(interp, "dom::document",
        reinterpret_cast<Th8_CommandProc>(document_command), ctx,
        bridge_context_destructor, nullptr);

    // Remaining commands share the same context (no destructor).
    Th8_CreateCommand(interp, "dom::console",
        reinterpret_cast<Th8_CommandProc>(console_command), ctx, nullptr, nullptr);

    Th8_CreateCommand(interp, "dom::release",
        reinterpret_cast<Th8_CommandProc>(release_command), ctx, nullptr, nullptr);

    Th8_CreateCommand(interp, "dom::eval_js",
        reinterpret_cast<Th8_CommandProc>(eval_js_command), ctx, nullptr, nullptr);
}

ByteString register_object_command(Th8_Interp* interp, HandleTable& handles, Bindings::PlatformObject& object)
{
    auto handle = handles.register_handle(object);
    if (handle.is_empty())
        return {};

    Th8_CreateCommand(interp, handle.characters(),
        reinterpret_cast<Th8_CommandProc>(object_ensemble_command),
        &handles, nullptr, nullptr);

    return handle;
}

// ---- Command Implementations ----

// dom::document ?subcommand? ?args...?
// Dispatch a document-scoped subcommand (querySelector, getElementById,
// createElement, createTextNode, body, head, title).  Shared by
// document_command (the `dom::document` ensemble) and
// object_ensemble_command, so a captured document handle
// (`set doc [dom::document]`) resolves the same document-only methods
// that `dom::document <sub>` does -- these are not node methods and
// otherwise fall through to the node ensemble's "unknown subcommand".
// Sets out_handled to true when the subcommand matched; leaves it false
// (and returns TH8_OK) so the caller can continue its own dispatch.
static int document_subcommand(Th8_Interp* interp, DOM::Document& document,
    HandleTable& handles, int argc, char const** argv, size_t* argl, bool& out_handled)
{
    out_handled = true;
    StringView subcommand { argv[1], argl[1] };

    if (subcommand == "querySelector"sv) {
        if (argc < 3)
            return set_error(interp, "wrong # args: should be \"dom::document querySelector selector\""sv);
        auto selector = string_from_th8(argv[2], argl[2]);
        auto result = document.query_selector(selector);
        if (result.is_error())
            return set_error(interp, "querySelector failed"sv);
        return set_result_node(interp, handles, result.release_value());
    }

    if (subcommand == "getElementById"sv) {
        if (argc < 3)
            return set_error(interp, "wrong # args: should be \"dom::document getElementById id\""sv);
        auto id = flystring_from_th8(argv[2], argl[2]);
        auto element = document.get_element_by_id(id);
        if (!element)
            return result_clear(interp);
        return set_result_node(interp, handles, element);
    }

    if (subcommand == "createElement"sv) {
        if (argc < 3)
            return set_error(interp, "wrong # args: should be \"dom::document createElement tagName\""sv);
        auto tag = string_from_th8(argv[2], argl[2]);
        Variant<String, DOM::ElementCreationOptions> options { DOM::ElementCreationOptions {} };
        auto result = document.create_element(tag, options);
        if (result.is_error())
            return set_error(interp, "createElement failed"sv);
        auto element = result.release_value();
        return set_result_node(interp, handles, element);
    }

    if (subcommand == "createTextNode"sv) {
        if (argc < 3)
            return set_error(interp, "wrong # args: should be \"dom::document createTextNode text\""sv);
        auto text_sv = StringView { argv[2], argl[2] };
        auto text_utf16 = Utf16String::from_utf8(text_sv);
        GC::Ref<DOM::Text> node = document.create_text_node(text_utf16);
        return set_result_node(interp, handles, node);
    }

    if (subcommand == "body"sv) {
        auto* body = document.body();
        if (!body)
            return result_clear(interp);
        return set_result_node(interp, handles, static_cast<DOM::Node*>(body));
    }

    if (subcommand == "head"sv) {
        auto* head = document.head();
        if (!head)
            return result_clear(interp);
        return set_result_node(interp, handles, static_cast<DOM::Node*>(head));
    }

    if (subcommand == "title"sv) {
        if (argc >= 3) {
            auto title_sv = StringView { argv[2], argl[2] };
            auto title_utf16 = Utf16String::from_utf8(title_sv);
            auto result = document.set_title(title_utf16);
            if (result.is_error())
                return set_error(interp, "set_title failed"sv);
            return set_result_string(interp, title_sv);
        }
        auto title_utf8 = document.title().to_utf8();
        return set_result_string(interp, title_utf8);
    }

    out_handled = false;
    return TH8_OK;
}

static int document_command(Th8_Interp* interp, void* ctx, int argc, char const** argv, size_t* argl)
{
    auto* bridge = static_cast<BridgeContext*>(ctx);
    auto& document = *bridge->document;
    auto& handles = *bridge->handles;

    if (argc < 2) {
        auto handle = register_object_command(interp, handles, document);
        return set_result_string(interp, handle);
    }

    bool handled = false;
    int rc = document_subcommand(interp, document, handles, argc, argv, argl, handled);
    if (handled)
        return rc;

    StringView subcommand { argv[1], argl[1] };
    return set_error(interp, ByteString::formatted("unknown document subcommand \"{}\"", subcommand));
}

// dom::console log|warn|error message...
static int console_command(Th8_Interp* interp, void* ctx, int argc, char const** argv, size_t* argl)
{
    (void)ctx;

    if (argc < 3)
        return set_error(interp, "wrong # args: should be \"dom::console log|warn|error message\""sv);

    StringView level { argv[1], argl[1] };

    StringBuilder message;
    for (int i = 2; i < argc; ++i) {
        if (i > 2)
            message.append(' ');
        message.append(StringView { argv[i], argl[i] });
    }

    if (level == "log"sv || level == "warn"sv || level == "error"sv) {
        dbgln("[TH8 console.{}] {}", level, message.string_view());
        return result_clear(interp);
    }

    return set_error(interp, "unknown console level: should be log, warn, or error"sv);
}

// $handle subcommand ?args...?
static int object_ensemble_command(Th8_Interp* interp, void* ctx, int argc, char const** argv, size_t* argl)
{
    auto* handles = static_cast<HandleTable*>(ctx);
    if (argc < 2)
        return set_error(interp, "wrong # args: should be \"$handle subcommand ?args...?\""sv);

    auto* object = resolve_handle(*handles, argv[0], argl[0]);
    if (!object)
        return set_error(interp, ByteString::formatted("invalid handle \"{}\"", StringView { argv[0], argl[0] }));

    StringView subcommand { argv[1], argl[1] };

    // ---- Document methods ----
    //
    // A captured document handle (`set doc [dom::document]`) resolves to
    // this node ensemble, but getElementById / createElement /
    // createTextNode / body / head / title / querySelector are
    // document-scoped, not node methods.  Delegate them to the shared
    // document dispatch when this handle is the Document; anything it
    // does not claim falls through to the node methods below (a Document
    // is also a Node, so appendChild / childNodes / etc. still apply).
    if (is<DOM::Document>(object)) {
        bool handled = false;
        int rc = document_subcommand(interp, static_cast<DOM::Document&>(*object),
            *handles, argc, argv, argl, handled);
        if (handled)
            return rc;
    }

    // ---- Node methods ----

    if (subcommand == "appendChild"sv) {
        if (argc < 3)
            return set_error(interp, "wrong # args: should be \"$node appendChild $child\""sv);
        auto* child = resolve_handle(*handles, argv[2], argl[2]);
        if (!child || !is<DOM::Node>(child) || !is<DOM::Node>(object))
            return set_error(interp, "appendChild: invalid node handle"sv);
        auto result = static_cast<DOM::Node&>(*object).append_child(static_cast<DOM::Node&>(*child));
        if (result.is_error())
            return set_error(interp, "appendChild failed"sv);
        return set_result_node(interp, *handles, result.release_value());
    }

    if (subcommand == "removeChild"sv) {
        if (argc < 3)
            return set_error(interp, "wrong # args: should be \"$node removeChild $child\""sv);
        auto* child = resolve_handle(*handles, argv[2], argl[2]);
        if (!child || !is<DOM::Node>(child) || !is<DOM::Node>(object))
            return set_error(interp, "removeChild: invalid node handle"sv);
        auto result = static_cast<DOM::Node&>(*object).remove_child(static_cast<DOM::Node&>(*child));
        if (result.is_error())
            return set_error(interp, "removeChild failed"sv);
        return set_result_node(interp, *handles, result.release_value());
    }

    if (subcommand == "insertBefore"sv) {
        if (argc < 3)
            return set_error(interp, "wrong # args: should be \"$node insertBefore $new ?$ref?\""sv);
        auto* new_child = resolve_handle(*handles, argv[2], argl[2]);
        if (!new_child || !is<DOM::Node>(new_child) || !is<DOM::Node>(object))
            return set_error(interp, "insertBefore: invalid node handle"sv);
        GC::Ptr<DOM::Node> ref_child;
        if (argc >= 4) {
            auto* ref = resolve_handle(*handles, argv[3], argl[3]);
            if (ref && is<DOM::Node>(ref))
                ref_child = static_cast<DOM::Node*>(ref);
        }
        static_cast<DOM::Node&>(*object).insert_before(static_cast<DOM::Node&>(*new_child), ref_child);
        return set_result_node(interp, *handles, static_cast<DOM::Node*>(new_child));
    }

    if (subcommand == "parentNode"sv) {
        if (!is<DOM::Node>(object))
            return set_error(interp, "parentNode: not a node"sv);
        return set_result_node(interp, *handles, static_cast<DOM::Node&>(*object).parent());
    }

    if (subcommand == "firstChild"sv) {
        if (!is<DOM::Node>(object))
            return set_error(interp, "firstChild: not a node"sv);
        return set_result_node(interp, *handles, static_cast<DOM::Node&>(*object).first_child());
    }

    if (subcommand == "lastChild"sv) {
        if (!is<DOM::Node>(object))
            return set_error(interp, "lastChild: not a node"sv);
        return set_result_node(interp, *handles, static_cast<DOM::Node&>(*object).last_child());
    }

    if (subcommand == "nextSibling"sv) {
        if (!is<DOM::Node>(object))
            return set_error(interp, "nextSibling: not a node"sv);
        return set_result_node(interp, *handles, static_cast<DOM::Node&>(*object).next_sibling());
    }

    if (subcommand == "previousSibling"sv) {
        if (!is<DOM::Node>(object))
            return set_error(interp, "previousSibling: not a node"sv);
        return set_result_node(interp, *handles, static_cast<DOM::Node&>(*object).previous_sibling());
    }

    // ---- textContent (getter/setter) ----

    if (subcommand == "textContent"sv) {
        if (!is<DOM::Node>(object))
            return set_error(interp, "textContent: not a node"sv);
        auto& node = static_cast<DOM::Node&>(*object);
        if (argc >= 3) {
            auto text_sv = StringView { argv[2], argl[2] };
            auto text_utf16 = Utf16String::from_utf8(text_sv);
            auto result = node.set_text_content(text_utf16);
            if (result.is_error())
                return set_error(interp, "set_text_content failed"sv);
            return set_result_string(interp, text_sv);
        }
        auto content = node.text_content();
        if (content.has_value()) {
            auto utf8 = content.value().to_utf8();
            return set_result_string(interp, utf8);
        }
        return result_clear(interp);
    }

    // ---- Event listener management ----

    if (subcommand == "addEventListener"sv) {
        if (argc < 4)
            return set_error(interp, "wrong # args: should be \"$elem addEventListener type procName\""sv);
        if (!is<DOM::EventTarget>(object))
            return set_error(interp, "addEventListener: not an event target"sv);

        auto event_type = flystring_from_th8(argv[2], argl[2]);
        auto proc_name = ByteString(StringView { argv[3], argl[3] });

        // We need the document to get the JS realm.  Walk up from the
        // object to find the owner document.
        auto& event_target = static_cast<DOM::EventTarget&>(*object);
        DOM::Document* doc = nullptr;
        if (is<DOM::Node>(object))
            doc = &static_cast<DOM::Node&>(*object).document();

        if (!doc)
            return set_error(interp, "addEventListener: cannot determine document"sv);

        int rc = add_th8_event_listener(event_target, event_type,
            move(proc_name), interp, *handles, *doc);
        if (rc != TH8_OK)
            return set_error(interp, "addEventListener failed"sv);
        return result_clear(interp);
    }

    // ---- Element methods ----

    if (subcommand == "getAttribute"sv) {
        if (argc < 3)
            return set_error(interp, "wrong # args: should be \"$elem getAttribute name\""sv);
        if (!is<DOM::Element>(object))
            return set_error(interp, "getAttribute: not an element"sv);
        auto name = flystring_from_th8(argv[2], argl[2]);
        auto value = static_cast<DOM::Element&>(*object).get_attribute(name);
        if (value.has_value())
            return set_result_string(interp, value.value());
        return result_clear(interp);
    }

    if (subcommand == "setAttribute"sv) {
        if (argc < 4)
            return set_error(interp, "wrong # args: should be \"$elem setAttribute name value\""sv);
        if (!is<DOM::Element>(object))
            return set_error(interp, "setAttribute: not an element"sv);
        auto name = flystring_from_th8(argv[2], argl[2]);
        auto value = string_from_th8(argv[3], argl[3]);
        static_cast<DOM::Element&>(*object).set_attribute_value(name, value);
        return result_clear(interp);
    }

    if (subcommand == "removeAttribute"sv) {
        if (argc < 3)
            return set_error(interp, "wrong # args: should be \"$elem removeAttribute name\""sv);
        if (!is<DOM::Element>(object))
            return set_error(interp, "removeAttribute: not an element"sv);
        auto name = flystring_from_th8(argv[2], argl[2]);
        static_cast<DOM::Element&>(*object).remove_attribute(name);
        return result_clear(interp);
    }

    if (subcommand == "hasAttribute"sv) {
        if (argc < 3)
            return set_error(interp, "wrong # args: should be \"$elem hasAttribute name\""sv);
        if (!is<DOM::Element>(object))
            return set_error(interp, "hasAttribute: not an element"sv);
        auto name = flystring_from_th8(argv[2], argl[2]);
        return set_result_bool(interp, static_cast<DOM::Element&>(*object).has_attribute(name));
    }

    if (subcommand == "tagName"sv) {
        if (!is<DOM::Element>(object))
            return set_error(interp, "tagName: not an element"sv);
        return set_result_string(interp, static_cast<DOM::Element&>(*object).tag_name());
    }

    if (subcommand == "innerHTML"sv) {
        if (!is<DOM::Element>(object))
            return set_error(interp, "innerHTML: not an element"sv);
        auto& element = static_cast<DOM::Element&>(*object);
        if (argc >= 3) {
            auto html_sv = StringView { argv[2], argl[2] };
            auto html_utf16 = Utf16String::from_utf8(html_sv);
            TrustedTypes::TrustedHTMLOrString trusted_html { html_utf16 };
            auto result = element.set_inner_html(trusted_html);
            if (result.is_error())
                return set_error(interp, "innerHTML set failed"sv);
            return result_clear(interp);
        }
        auto result = element.inner_html();
        if (result.is_error())
            return set_error(interp, "innerHTML get failed"sv);
        auto& value = result.value();
        if (value.has<Utf16String>()) {
            auto utf8 = value.get<Utf16String>().to_utf8();
            return set_result_string(interp, utf8);
        }
        return result_clear(interp);
    }

    // ---- querySelector on elements (ParentNode mixin) ----

    if (subcommand == "querySelector"sv) {
        if (argc < 3)
            return set_error(interp, "wrong # args: should be \"$elem querySelector selector\""sv);
        if (!is<DOM::ParentNode>(object))
            return set_error(interp, "querySelector: not a parent node"sv);
        auto selector = string_from_th8(argv[2], argl[2]);
        auto result = static_cast<DOM::ParentNode&>(*object).query_selector(selector);
        if (result.is_error())
            return set_error(interp, "querySelector failed"sv);
        return set_result_node(interp, *handles, result.release_value());
    }

    return set_error(interp, ByteString::formatted("unknown subcommand \"{}\" for handle \"{}\"",
        subcommand, StringView { argv[0], argl[0] }));
}

// dom::eval_js script
// Evaluate JavaScript in the document's JS realm.
// Gated by the "cross-eval" TH8-Script-Policy directive.
static int eval_js_command(Th8_Interp* interp, void* ctx, int argc, char const** argv, size_t* argl)
{
    auto* bridge = static_cast<BridgeContext*>(ctx);
    auto& document = *bridge->document;

    if (argc < 2)
        return set_error(interp, "wrong # args: should be \"dom::eval_js script\""sv);

    // [H11] If JavaScript execution is disabled on this document (e.g.,
    // a sandboxed iframe without `allow-scripts`, or a Content-Security-
    // Policy that blocks inline JS), TH8 must not be able to launder a
    // JS evaluation through dom::eval_js.  Reject before consulting the
    // cross-eval policy so the diagnostic explains the underlying cause.
    if (document.is_javascript_execution_disabled()) {
        return set_error(interp,
            "dom::eval_js: JavaScript execution is disabled on this "
            "document"sv);
    }

    // Check cross-eval policy gate.
    if (!document.th8_cross_eval_policy()) {
        return set_error(interp,
            "dom::eval_js: cross-eval not permitted; "
            "add <meta http-equiv=\"TH8-Script-Policy\" content=\"cross-eval\"> "
            "to enable"sv);
    }

    auto js_source = StringView { argv[1], argl[1] };

    // Evaluate JavaScript using the same ClassicScript path that
    // <script> tags use.  This ensures proper execution context
    // setup/teardown and security checks.
    auto& settings = document.relevant_settings_object();
    auto script = HTML::ClassicScript::create(
        "th8-cross-eval"sv, js_source, settings,
        document.url());
    auto completion = script->run(HTML::ClassicScript::RethrowErrors::Yes);

    if (completion.is_abrupt()) {
        auto err_str = completion.value().to_string_without_side_effects();
        return set_error(interp, err_str.bytes_as_string_view());
    }

    // Convert the JS result to a TH8 string.
    auto value = completion.value();

    if (value.is_undefined() || value.is_null())
        return result_clear(interp);

    if (value.is_boolean())
        return set_result_bool(interp, value.as_bool());

    // Convert everything else to string via to_string_without_side_effects
    // to avoid re-entering JS (which could cause reentrancy issues).
    auto result_str = value.to_string_without_side_effects();
    return set_result_string(interp, result_str.bytes_as_string_view());
}

// dom::release $handle
static int release_command(Th8_Interp* interp, void* ctx, int argc, char const** argv, size_t* argl)
{
    auto* bridge = static_cast<BridgeContext*>(ctx);

    if (argc < 2)
        return set_error(interp, "wrong # args: should be \"dom::release handle\""sv);

    StringView handle { argv[1], argl[1] };
    bridge->handles->release(handle);

    return result_clear(interp);
}

}
