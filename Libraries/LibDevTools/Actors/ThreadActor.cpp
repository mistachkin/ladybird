/*
 * Copyright (c) 2025, Tim Flynn <trflynn89@ladybird.org>
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <LibDevTools/Actors/SourceActor.h>
#include <LibDevTools/Actors/TabActor.h>
#include <LibDevTools/Actors/ThreadActor.h>
#include <LibDevTools/DevToolsServer.h>
#if LADYBIRD_ENABLE_TH8
#    include <LibTH8/Interpreter.h>
#endif

namespace DevTools {

NonnullRefPtr<ThreadActor> ThreadActor::create(DevToolsServer& devtools, String name, WeakPtr<TabActor> tab)
{
    return adopt_ref(*new ThreadActor(devtools, move(name), move(tab)));
}

ThreadActor::ThreadActor(DevToolsServer& devtools, String name, WeakPtr<TabActor> tab)
    : Actor(devtools, move(name))
    , m_tab(move(tab))
{
}

ThreadActor::~ThreadActor()
{
    for (auto const& actor : m_source_actors) {
        if (auto source_actor = actor.value.strong_ref())
            devtools().unregister_actor(source_actor->name());
    }
}

void ThreadActor::handle_message(Message const& message)
{
    if (message.type == "attach"sv)
        return handle_attach(message);
    if (message.type == "detach"sv)
        return handle_detach(message);
    if (message.type == "resume"sv)
        return handle_resume(message);
    if (message.type == "interrupt"sv)
        return handle_interrupt(message);
    if (message.type == "frames"sv)
        return handle_frames(message);
    if (message.type == "setBreakpoint"sv)
        return handle_set_breakpoint(message);
    if (message.type == "removeBreakpoint"sv)
        return handle_remove_breakpoint(message);

    if (message.type == "reconfigure"sv || message.type == "skipBreakpoints"sv) {
        JsonObject response;
        send_response(message, move(response));
        return;
    }

    if (message.type == "getAvailableEventBreakpoints"sv) {
        JsonObject response;
        JsonArray breakpoints;
        response.set("value"sv, move(breakpoints));
        send_response(message, move(response));
        return;
    }

    if (message.type == "sources"sv) {
        auto tab = m_tab.strong_ref();
        if (!tab) {
            JsonObject response;
            JsonArray sources;
            response.set("sources"sv, move(sources));
            send_response(message, move(response));
            return;
        }

        devtools().delegate().retrieve_sources(tab->description(),
            async_handler<ThreadActor>(message, [](auto& self, auto sources, auto& response) {
                response.set("sources"sv, self.serialize_sources(sources));
            }));
        return;
    }

    send_unrecognized_packet_type_error(message);
}

void ThreadActor::handle_attach(Message const& message)
{
    m_attached = true;

#if LADYBIRD_ENABLE_TH8
    // Install the debug callback so breakpoints and stepping work.
    if (m_interpreter) {
        m_interpreter->set_debug_callback([](Th8_Interp*, int event, char const*,
                                               size_t, int, int, void*) -> int {
            if (event == TH8_DEBUG_BREAKPOINT)
                return TH8_BREAK;
            return TH8_OK;
        },
            nullptr);
    }
#endif

    JsonObject response;
    response.set("type"sv, "paused"_string);
    response.set("why"sv, JsonObject {});
    send_response(message, move(response));
}

void ThreadActor::handle_detach(Message const& message)
{
    m_attached = false;

#if LADYBIRD_ENABLE_TH8
    // Remove the debug callback and clear all breakpoints.
    if (m_interpreter) {
        m_interpreter->set_debug_callback(nullptr, nullptr);
        m_interpreter->clear_all_breakpoints();
        m_interpreter->set_step_mode(TH8_STEP_NONE);
    }
#endif

    JsonObject response;
    response.set("type"sv, "detached"_string);
    send_response(message, move(response));
}

void ThreadActor::handle_resume(Message const& message)
{
#if LADYBIRD_ENABLE_TH8
    if (m_interpreter) {
        m_interpreter->set_step_mode(TH8_STEP_NONE);
        m_interpreter->thaw();
    }
#endif

    JsonObject response;
    response.set("type"sv, "resumed"_string);
    send_response(message, move(response));
}

void ThreadActor::handle_interrupt(Message const& message)
{
#if LADYBIRD_ENABLE_TH8
    if (m_interpreter)
        m_interpreter->freeze();
#endif

    JsonObject response;
    response.set("type"sv, "paused"_string);
    JsonObject why;
    why.set("type"sv, "interrupted"_string);
    response.set("why"sv, move(why));
    send_response(message, move(response));
}

void ThreadActor::handle_frames(Message const& message)
{
    JsonArray frames;

#if LADYBIRD_ENABLE_TH8
    if (m_interpreter && m_interpreter->is_suspended()) {
        int count = m_interpreter->frame_count();

        for (int i = 0; i < count; ++i) {
            char const* proc_name = nullptr;
            size_t proc_length = 0;
            char const* script_name = nullptr;
            size_t script_length = 0;
            int line = 0;

            if (m_interpreter->frame_info(i, &proc_name, &proc_length,
                    &script_name, &script_length, &line)
                != TH8_OK) {
                continue;
            }

            JsonObject frame;
            frame.set("index"sv, JsonValue { i });
            frame.set("displayName"sv,
                MUST(String::from_utf8({ proc_name ? proc_name : "", proc_length })));
            frame.set("source"sv,
                MUST(String::from_utf8({ script_name ? script_name : "", script_length })));
            frame.set("line"sv, JsonValue { line });
            frames.must_append(move(frame));
        }
    }
#endif

    JsonObject response;
    response.set("frames"sv, move(frames));
    send_response(message, move(response));
}

void ThreadActor::handle_set_breakpoint(Message const& message)
{
    auto script = get_required_parameter<String>(message, "source"sv);
    if (!script.has_value())
        return;

    auto line = get_required_parameter<i64>(message, "line"sv);
    if (!line.has_value())
        return;

    JsonObject response;

#if LADYBIRD_ENABLE_TH8
    if (m_interpreter) {
        int breakpoint_id = m_interpreter->set_breakpoint(script->bytes_as_string_view(),
            static_cast<int>(*line));

        if (breakpoint_id >= 0) {
            response.set("breakpointId"sv, JsonValue { breakpoint_id });
        } else {
            response.set("error"sv, "failed to set breakpoint"_string);
        }
    } else {
        response.set("error"sv, "no interpreter"_string);
    }
#else
    (void)line;
    response.set("error"sv, "TH8 disabled"_string);
#endif

    send_response(message, move(response));
}

void ThreadActor::handle_remove_breakpoint(Message const& message)
{
    auto breakpoint_id = get_required_parameter<i64>(message, "breakpointId"sv);
    if (!breakpoint_id.has_value())
        return;

    JsonObject response;

#if LADYBIRD_ENABLE_TH8
    if (m_interpreter) {
        int rc = m_interpreter->clear_breakpoint(static_cast<int>(*breakpoint_id));
        response.set("removed"sv, JsonValue { rc == TH8_OK });
    } else {
        response.set("error"sv, "no interpreter"_string);
    }
#else
    (void)breakpoint_id;
    response.set("error"sv, "TH8 disabled"_string);
#endif

    send_response(message, move(response));
}

JsonObject ThreadActor::serialize_source(Web::HTML::ScriptRegistry::Description const& source)
{
    return source_actor_for(source).serialize_source();
}

JsonArray ThreadActor::serialize_sources(Vector<Web::HTML::ScriptRegistry::Description> const& sources)
{
    prune_source_actors(sources);

    JsonArray serialized_sources;
    for (auto const& source : sources)
        serialized_sources.must_append(serialize_source(source));
    return serialized_sources;
}

void ThreadActor::prune_source_actors(Vector<Web::HTML::ScriptRegistry::Description> const& sources)
{
    HashTable<Web::HTML::ScriptRegistry::Identifier> current_sources;
    for (auto const& source : sources)
        current_sources.set(source.id);

    Vector<Web::HTML::ScriptRegistry::Identifier> stale_sources;
    for (auto const& actor : m_source_actors) {
        if (!current_sources.contains(actor.key))
            stale_sources.append(actor.key);
    }

    for (auto const& source_id : stale_sources) {
        auto actor = m_source_actors.take(source_id);
        if (actor.has_value()) {
            if (auto source_actor = actor->strong_ref())
                devtools().unregister_actor(source_actor->name());
        }
    }
}

SourceActor& ThreadActor::source_actor_for(Web::HTML::ScriptRegistry::Description const& source)
{
    if (auto actor = m_source_actors.find(source.id); actor != m_source_actors.end()) {
        if (auto source_actor = actor->value.strong_ref())
            return *source_actor;
    }

    auto& actor = devtools().register_actor<SourceActor>(m_tab, source);
    m_source_actors.set(source.id, actor);
    return actor;
}

}
