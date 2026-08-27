/*
 * Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/Optional.h>
#include <AK/StringView.h>
#include <LibWeb/Export.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/Scripting/Script.h>

namespace Web::HTML {

// Non-standard: TH8 scripting language support.
// TH8Script is the analog of ClassicScript for the TH8 language.
// TH8 source is stored as text and evaluated at run-time (not parse-time),
// since TH8 has no separate parse/compile phase.
class WEB_API TH8Script final : public Script {
    GC_CELL(TH8Script, Script);
    GC_DECLARE_ALLOCATOR(TH8Script);

public:
    virtual ~TH8Script() override;

    static GC::Ref<TH8Script> create(ByteString filename, StringView source,
        EnvironmentSettingsObject&, URL::URL base_url);

    enum class RethrowErrors {
        No,
        Yes,
    };

    JS::Completion run(RethrowErrors = RethrowErrors::No);

    StringView source() const { return m_source; }

    // Optional signature sidecar (raw bytes of the `<src>.b64sig`
    // file).  Set by the fetch path for scripts declared with
    // `text/th8+signed`; consumed by TH8Script::run, which registers
    // the bytes with the TH8Context's WebPlatform before evaluation
    // so the TH8 signed-only policy chain can verify the signature.
    void set_signature_sidecar(ByteBuffer bytes) { m_signature_sidecar = move(bytes); }
    bool has_signature_sidecar() const { return m_signature_sidecar.has_value(); }

private:
    TH8Script(URL::URL base_url, ByteString filename, EnvironmentSettingsObject&);

    virtual bool is_th8_script() const final { return true; }
    virtual void visit_edges(Cell::Visitor&) override;

    ByteString m_source;
    Optional<ByteBuffer> m_signature_sidecar;
};

}

template<>
inline bool JS::Script::HostDefined::fast_is<Web::HTML::TH8Script>() const { return is_th8_script(); }
