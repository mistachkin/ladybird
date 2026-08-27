/*
 * Copyright (c) 2025, Tim Flynn <trflynn89@ladybird.org>
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/HashTable.h>
#include <AK/NonnullRefPtr.h>
#include <LibDevTools/Actor.h>
#include <LibDevTools/DevToolsDelegate.h>
#include <LibDevTools/Forward.h>
#if LADYBIRD_ENABLE_TH8
#    include <LibTH8/Forward.h>
#else
// Build with ENABLE_TH8=OFF still needs the forward decl so the
// m_interpreter / set_interpreter member signatures parse cleanly.
namespace TH8 {
class Interpreter;
}
#endif

namespace DevTools {

class DEVTOOLS_API ThreadActor final : public Actor {
public:
    static constexpr auto base_name = "thread"sv;

    static NonnullRefPtr<ThreadActor> create(DevToolsServer&, String name, WeakPtr<TabActor>);
    virtual ~ThreadActor() override;

    void set_interpreter(TH8::Interpreter* interpreter) { m_interpreter = interpreter; }

    JsonObject serialize_source(Web::HTML::ScriptRegistry::Description const&);
    JsonArray serialize_sources(Vector<Web::HTML::ScriptRegistry::Description> const&);

private:
    ThreadActor(DevToolsServer&, String name, WeakPtr<TabActor>);

    virtual void handle_message(Message const&) override;

    void handle_attach(Message const&);
    void handle_detach(Message const&);
    void handle_resume(Message const&);
    void handle_interrupt(Message const&);
    void handle_frames(Message const&);
    void handle_set_breakpoint(Message const&);
    void handle_remove_breakpoint(Message const&);

    void prune_source_actors(Vector<Web::HTML::ScriptRegistry::Description> const&);
    SourceActor& source_actor_for(Web::HTML::ScriptRegistry::Description const&);

    bool m_attached { false };
    TH8::Interpreter* m_interpreter { nullptr };

    WeakPtr<TabActor> m_tab;
    HashMap<Web::HTML::ScriptRegistry::Identifier, WeakPtr<SourceActor>> m_source_actors;
};

}
