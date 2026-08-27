/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/HTML/Scripting/TH8Context.h>

#include <LibTH8/Interpreter.h>
#include <LibTH8/WebPlatform.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/TH8/DOMBridge.h>
#include <LibWeb/TH8/HandleTable.h>

namespace Web::HTML {

// [M2] Per-evaluate wall-clock bounding is provided by TH8's native
// cooperative deadline (Th8_SetTimeLimitMs), armed and cleared around
// each Th8_Eval in TH8Context::evaluate.  This replaced an earlier
// hand-rolled background watchdog thread that fired Th8_CancelEval; the
// native deadline is checked at the same step boundaries but needs no
// per-eval thread.

GC_DEFINE_ALLOCATOR(TH8Context);

GC::Ref<TH8Context> TH8Context::create(GC::Heap& heap, DOM::Document& document)
{
    return heap.allocate<TH8Context>(document);
}

TH8Context::TH8Context(DOM::Document& document)
    : m_document(document)
{
    initialize_interpreter();
}

TH8Context::~TH8Context()
{
    // [H3] Mark the context as destroying BEFORE any member teardown.
    // DOM-bridge callbacks fired during interpreter teardown consult
    // is_alive_for_dispatch() and bail out without dereferencing
    // half-destroyed members.
    m_destroying = true;

    // If a Th8_Eval frame is somehow still on stack, log loudly --
    // GC should never destroy a TH8Context while a script is mid-run.
    if (m_evaluate_depth != 0)
        dbgln("TH8Context: WARNING -- destructor running with evaluate_depth={}", m_evaluate_depth);

    // [H3] Tear the interpreter down FIRST.  Th8_DeleteInterp may
    // synchronously invoke command-destructor callbacks (e.g., the
    // BridgeContext destructor) that can read the handle table or
    // platform context.  Default reverse-declaration destruction
    // would have torn down m_handle_table first, leaving Th8_DeleteInterp
    // with a dangling HandleTable*.  Explicit ordered teardown:
    //   1. m_interpreter   (Th8_DeleteInterp synchronously fires callbacks
    //                       AND allocates/frees via interp->pPlatform->xMalloc,
    //                       so the platform descriptor must still be alive)
    //   2. m_handle_table  (no longer reachable from any callback)
    //   3. m_platform_context (no longer reachable; was only consulted
    //                          via the platform's xGetData slot which the
    //                          dead interpreter no longer reaches)
    //   4. m_platform_descriptor ([H8] owns the Th8_Platform the now-dead
    //                          interpreter pointed at; safe to free last)
    m_interpreter.clear();
    m_handle_table.clear();
    m_platform_context.clear();
    m_platform_descriptor.clear();
}

void TH8Context::initialize_interpreter()
{
    // The platform context backs the WebPlatform's xGetData callback
    // (sidecar lookup for the signed-only policy).  It must outlive
    // the Th8_Interp; making it a member ensures destruction order.
    m_platform_context = make<::TH8::WebPlatformContext>();
    // [H8] PlatformDescriptor owns the Th8_Platform value.  Th8_CreateInterp
    // does NOT copy it -- it stores a bare pointer (interp->pPlatform,
    // documented "not owned").  A local descriptor would therefore be freed
    // the moment initialize_interpreter() returns, leaving interp->pPlatform
    // dangling; main-thread eval survives only by reading the not-yet-reused
    // freed block, but the periodic wall-clock deadline check (Th8_GetTimeUs
    // via the platform's xTimeUs callback) later reads the block after it has
    // been overwritten and crashes calling a garbage callback pointer.  Keep
    // the descriptor alive as a member for the interpreter's full lifetime.
    m_platform_descriptor = ::TH8::create_web_content_platform(*m_platform_context);
    auto interpreter_or_error = ::TH8::Interpreter::create(m_platform_descriptor->raw());
    if (interpreter_or_error.is_error()) {
        dbgln("TH8Context: Failed to create interpreter: {}", interpreter_or_error.error());
        return;
    }

    m_interpreter = interpreter_or_error.release_value();

    // [M1] Resource-limit semantics for the per-document interpreter:
    //
    //   step_limit   : CUMULATIVE across every Th8_Eval against this
    //                  interpreter.  Multiple <script type="text/th8">
    //                  blocks on the same document share one budget
    //                  -- a runaway split across N <script> tags is
    //                  still bounded by the single page-level cap.
    //                  Th8_ResetStepCount() is NOT called per evaluate;
    //                  the budget is reset implicitly at TH8Context
    //                  construction (i.e., per Document lifetime).
    //                  Embedders that want a per-eval budget instead
    //                  must reset between evaluate() calls explicitly.
    //
    //   memory_limit : LIVE allocation total (Th8_GetAllocBytes).  A
    //                  script that allocates then frees has its
    //                  in-flight total decrease.  The cap fires when
    //                  the live total -- not the cumulative -- exceeds
    //                  the limit.  Shared across all <script> blocks
    //                  in the document (same allocator).
    //
    //   wall_clock   : PER-EVALUATE.  Armed via TH8's native deadline
    //                  (Th8_SetTimeLimitMs) around each Th8_Eval and
    //                  cleared afterwards; see TH8Context::evaluate.
    //
    // See Tests/LibWeb/Text/input/TH8/resource-limits.html and the
    // M1 cumulative-budget test for verification.
    m_interpreter->set_step_limit(::TH8::default_step_limit);
    m_interpreter->set_memory_limit(::TH8::default_memory_limit);

    // Create the handle table for DOM object references.
    m_handle_table = make<Web::TH8::HandleTable>(m_document->heap());

    // Register DOM bridge commands with the interpreter.
    Web::TH8::register_dom_commands(m_interpreter->raw(), *m_document, *m_handle_table);

    // If the document has opted into signed-only TH8 scripts via the
    // <meta http-equiv="TH8-Script-Policy" content="signed-only">
    // directive (or future TH8-Script-Policy: HTTP header), install
    // the signed-only policy and preload every trust anchor from the
    // build-time embedded keyring.  See Th8_GetEmbeddedKeyring in
    // th8.h; the canonical empty stub returns nEntries=0 so no scripts
    // pass verification until an embedder generates a real keyring via
    // `tclsh tools/mkkey.tcl --keyring`.
    if (m_document->th8_signed_only_policy()) {
        int preloaded = m_interpreter->install_signed_only_policy_with_embedded_keyring();
        if (preloaded < 0) {
            dbgln("TH8Context: Failed to install signed-only policy on interpreter.");
        } else {
            dbgln("TH8Context: Signed-only policy installed; {} trust anchor(s) preloaded "
                  "from the embedded keyring.", preloaded);
            if (preloaded == 0) {
                dbgln("TH8Context: WARNING -- signed-only policy is active but the embedded "
                      "keyring is empty (the canonical stub is linked).  No TH8 scripts will "
                      "verify.  Regenerate the keyring with "
                      "`tclsh tools/mkkey.tcl --keyring ...` and link the generated source "
                      "instead of th8_keyring_stub.c.");
            }
        }
    }
}

int TH8Context::evaluate(StringView script, StringView name)
{
    if (!m_interpreter) {
        dbgln("TH8Context: Cannot evaluate, interpreter not initialized");
        return TH8_ERROR;
    }

    // [H3] Re-entrancy guard.  A nested evaluate() reaching this method
    // means we are mid-Th8_Eval and a DOM bridge command (or JS via
    // cross-eval) is asking to start another top-level eval.  Reject
    // before any state mutation; the bridge / cross-eval caller sees
    // TH8_ERROR and can surface it as a script-visible error.
    if (m_evaluate_depth > 0) {
        dbgln("TH8Context: Refusing nested evaluate() at depth {}", m_evaluate_depth);
        return TH8_ERROR;
    }

    // [H3] Tear-down guard.  If the destructor has started running,
    // refuse new evaluations -- the interpreter is about to be (or has
    // just been) destroyed.  In practice this catches a GC-driven
    // destruction racing against a scheduled callback.
    if (m_destroying)
        return TH8_ERROR;

    ++m_evaluate_depth;
    int rc;
    {
        // [M2] Wall-clock deadline (TH8K-010).  TH8's native, cooperative,
        // in-interpreter deadline bounds the REAL run time of this
        // evaluation.  It replaces the former hand-rolled watchdog thread
        // that fired Th8_CancelEval from a background thread: both take
        // effect cooperatively at TH8's step boundaries, but the native
        // deadline needs no per-eval thread (no spawn/join, no polling
        // slice, no cross-thread cancel).  Armed just before the
        // synchronous Th8_Eval and cleared immediately after, so the bound
        // is scoped to exactly this evaluation (re-entrant cross-eval is
        // rejected at the top of this function anyway).  On expiry TH8
        // stops with a "time limit exceeded" error and returns TH8_ERROR.
        m_interpreter->set_time_limit_ms(::TH8::default_wall_clock_limit_ms);
        rc = m_interpreter->evaluate(script, name);
        m_interpreter->set_time_limit_ms(0);
    }
    // Clear any residual cancellation state (e.g. from an explicit
    // cancel()) so future evaluate() calls can proceed; TH8 keeps the
    // cancellation flag set until reset.  (The deadline itself reports via
    // a normal error, not the cancel flag, so this is a no-op for it.)
    if (rc != TH8_OK)
        m_interpreter->reset_cancel();
    --m_evaluate_depth;
    return rc;
}

StringView TH8Context::result_string() const
{
    if (!m_interpreter)
        return {};

    return m_interpreter->result_string();
}

void TH8Context::register_source(ByteString name, ByteString source)
{
    // [M11] Bound the source map so a page that reloads scripts in a
    // loop (e.g., live-reload, polling fetch) cannot grow it without
    // limit.  If we are at the cap and the incoming name is new, drop
    // the oldest entry first.  This is intentionally a coarse FIFO,
    // not LRU: the only consumer is DevTools' "sources" panel, which
    // does not need access-recency tracking.
    if (!m_sources.contains(name) && m_sources.size() >= max_source_entries) {
        if (!m_source_insertion_order.is_empty()) {
            auto victim = m_source_insertion_order.take_first();
            m_sources.remove(victim);
        }
    }
    if (!m_sources.contains(name))
        m_source_insertion_order.append(name);
    m_sources.set(move(name), move(source));
}

void TH8Context::register_signature_sidecar(ByteString script_name, ByteBuffer signature_bytes)
{
    if (!m_platform_context)
        return;
    // TH8's signed-only policy chain looks for the sidecar under
    // "<scriptname>.b64sig"; build that key here so the WebPlatform
    // xGetData callback can find it.
    auto sidecar_name = ByteString::formatted("{}.b64sig", script_name);
    m_platform_context->register_sidecar(move(sidecar_name), move(signature_bytes));
}

void TH8Context::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_document);
}

}
