/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGC/Ptr.h>
#include <LibJS/Forward.h>
#include <LibWeb/Forward.h>

namespace Web::TH8 {

// [H10-followup] Construct the `window.th8` namespace object exposing
// the JS->TH8 cross-eval entry point.  Currently a single method:
//   window.th8.eval(scriptText) -> string
//
// The object is created on first Window initialization (see
// HTML::Window::initialize_web_interfaces) and gated by the document's
// `cross-eval` TH8 policy AT CALL TIME -- not at attach time -- so the
// `th8` property is always present (matching how `window.atob` etc.
// behave) but throws SecurityError when the policy is not set.
GC::Ref<JS::Object> create_window_th8_namespace(JS::Realm&, GC::Ref<DOM::Document>);

}
