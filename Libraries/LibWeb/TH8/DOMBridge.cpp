/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/Utf16String.h>
#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Element.h>
#include <LibWeb/DOM/Node.h>
#include <LibWeb/DOM/ParentNode.h>
#include <LibWeb/DOM/Text.h>
#include <LibWeb/HTML/HTMLHeadElement.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/TrustedTypes/TrustedHTML.h>
#include <LibWeb/TH8/DOMBridge.h>
#include <LibWeb/TH8/HandleTable.h>
#include <LibWeb/TH8/TypeConversion.h>

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

// Helper to resolve a handle from argv and get the PlatformObject.
static Bindings::PlatformObject* resolve_handle(HandleTable& handles, char const* str, size_t len)
{
    return handles.resolve(StringView { str, len });
}

void register_dom_commands(Th8_Interp* interp, DOM::Document& document, HandleTable& handles)
{
    // Allocate bridge context on the heap; it lives as long as the interpreter.
    auto* ctx = new BridgeContext { &document, &handles, interp };

    Th8_CreateCommand(interp, "dom::document",
        reinterpret_cast<Th8_CommandProc>(document_command), ctx, nullptr, nullptr);

    Th8_CreateCommand(interp, "dom::console",
        reinterpret_cast<Th8_CommandProc>(console_command), ctx, nullptr, nullptr);

    Th8_CreateCommand(interp, "dom::release",
        reinterpret_cast<Th8_CommandProc>(release_command), ctx, nullptr, nullptr);
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
static int document_command(Th8_Interp* interp, void* ctx, int argc, char const** argv, size_t* argl)
{
    auto* bridge = static_cast<BridgeContext*>(ctx);
    auto& document = *bridge->document;
    auto& handles = *bridge->handles;

    if (argc < 2) {
        auto handle = register_object_command(interp, handles, document);
        return set_result_string(interp, handle);
    }

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
        // insert_before returns void; result is the inserted child.
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
