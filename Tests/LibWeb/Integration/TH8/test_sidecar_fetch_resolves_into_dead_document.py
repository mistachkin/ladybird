# Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
#
# SPDX-License-Identifier: BSD-2-Clause

"""
TH8 navigation-cancellation race -- PoC test #4.

Loads a signed TH8 script whose sidecar (.b64sig) is missing, then
navigates away.  The full audit case wants the sidecar fetch to STILL
BE IN FLIGHT when the document is navigated away from -- i.e., the
callback fires against a dead Document.  Reproducing the timing
reliably needs a slow-response HTTP fixture server which the file://
PoC harness does not have today.

What this degenerate test covers:
  * the signed-only fail-closed path (missing sidecar -> verification
    fails -> script does not run),
  * the post-failure tear-down path when the document is navigated
    away from a few milliseconds later,
  * absence of crash / UAF (asserted at session shutdown by ASAN).

Mark the full timing-sensitive variant skipped until the HTTP fixture
lands.
"""

from __future__ import annotations

import time

import pytest


NAV_BUDGET_MS = 500


def test_sidecar_missing_then_navigate_does_not_crash(webdriver, fixture_url):
    page_d = fixture_url("page_d_signed_missing_sidecar.html")
    page_b = fixture_url("page_b_lands.html")

    webdriver.set_page_load_timeout(NAV_BUDGET_MS * 4)
    webdriver.set_script_timeout(500)

    webdriver.navigate(page_d)
    status = webdriver.execute(
        "return document.getElementById('status').textContent;"
    )
    assert status == "loaded", (
        f"page D did not finish loading (status={status!r})"
    )

    # Immediately navigate away.  This kicks off the document teardown
    # while any post-eval / post-fetch state on the old TH8Context is
    # still settling.
    started = time.monotonic()
    webdriver.navigate(page_b)
    elapsed_ms = int((time.monotonic() - started) * 1000)

    landed = webdriver.execute(
        "return document.getElementById('landed') "
        "    ? document.getElementById('landed').textContent : null;"
    )
    assert landed == "yes", f"page B did not land (text={landed!r})"
    assert elapsed_ms <= NAV_BUDGET_MS, (
        f"navigation took {elapsed_ms} ms (budget {NAV_BUDGET_MS} ms)"
    )


@pytest.mark.skip(
    reason="needs an HTTP fixture server that can delay the .b64sig "
    "response so the fetch is in-flight when the document is destroyed; "
    "see Tests/LibWeb/Integration/README.md for the deferred work."
)
def test_sidecar_fetch_in_flight_during_navigation(webdriver, fixture_url):
    # Skeleton placeholder for the full timing-sensitive variant.
    raise NotImplementedError
