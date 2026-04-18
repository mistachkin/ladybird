/*
 * Copyright (c) 2025, Tim Flynn <trflynn89@ladybird.org>
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullRefPtr.h>
#include <LibDevTools/Actor.h>
#include <LibDevTools/Forward.h>

namespace TH8 {
class Interpreter;
}

namespace DevTools {

class DEVTOOLS_API ThreadActor final : public Actor {
public:
    static constexpr auto base_name = "thread"sv;

    static NonnullRefPtr<ThreadActor> create(DevToolsServer&, String name);
    virtual ~ThreadActor() override;

    void set_interpreter(TH8::Interpreter* interpreter) { m_interpreter = interpreter; }

private:
    ThreadActor(DevToolsServer&, String name);

    virtual void handle_message(Message const&) override;

    void handle_attach(Message const&);
    void handle_detach(Message const&);
    void handle_resume(Message const&);
    void handle_interrupt(Message const&);
    void handle_sources(Message const&);
    void handle_frames(Message const&);
    void handle_set_breakpoint(Message const&);
    void handle_remove_breakpoint(Message const&);

    bool m_attached { false };
    TH8::Interpreter* m_interpreter { nullptr };
};

}
