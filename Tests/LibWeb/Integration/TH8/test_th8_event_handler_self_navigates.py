# Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
#
# SPDX-License-Identifier: BSD-2-Clause

"""
TH8 navigation-cancellation race -- PoC test #3.

A TH8 click handler navigates the document mid-dispatch via
cross-eval (TH8 -> JS -> window.location).  The TH8 evaluate frame
must unwind cleanly through the prepare_to_run_script /
clean_up_after_running_script wrapping (H4) before the navigation
processes.

If H4 regresses, this test typically fails as a UAF caught by ASAN
(the document is torn down while the TH8 frame still references
DOM handles).
"""

from __future__ import annotations

import time


NAV_BUDGET_MS = 800
LOAD_TIMEOUT_S = 2.0


def _wait_for_url_change(webdriver, original_url: str) -> str | None:
    """Poll the WebDriver URL until it changes from `original_url`."""
    deadline = time.monotonic() + LOAD_TIMEOUT_S
    while time.monotonic() < deadline:
        try:
            current = webdriver.get_url()
        except Exception:
            current = None
        if current and current != original_url:
            return current
        time.sleep(0.05)
    return None


def test_th8_event_handler_self_navigates(webdriver, fixture_url):
    page_c = fixture_url("page_c_th8_event_navigates.html")

    webdriver.set_page_load_timeout(NAV_BUDGET_MS * 4)
    webdriver.set_script_timeout(500)

    # Land on page C; the TH8 script wires the click handler.
    webdriver.navigate(page_c)
    original_url = webdriver.get_url()

    # Verify the handler is wired (TH8 ran addEventListener).  We can't
    # observe TH8 state directly from JS, but the page renders
    # synchronously so by the time get_url() returns, addEventListener
    # has executed.
    status = webdriver.execute(
        "return document.getElementById('status').textContent;"
    )
    assert status == "armed", f"page C did not initialize (status={status!r})"

    # Fire the click from JS.  WebDriver's execute_script runs in the
    # current document's JS context; the .click() dispatches the click
    # event which the TH8 handler picks up and routes through
    # dom::eval_js to navigate.
    webdriver.execute(
        "document.getElementById('trigger').click();"
    )

    # Poll for the URL to change.  A clean run produces page B; a
    # UAF / crash would manifest as a hung or dropped session.
    new_url = _wait_for_url_change(webdriver, original_url)
    assert new_url is not None, (
        f"page C did not navigate after click; still at {original_url}"
    )
    assert new_url.endswith("page_b_lands.html"), (
        f"unexpected destination URL: {new_url}"
    )

    # Confirm page B is actually rendered (catches a partial navigation
    # that resolved the URL but failed to commit the document).
    landed = webdriver.execute(
        "return document.getElementById('landed') "
        "    ? document.getElementById('landed').textContent : null;"
    )
    assert landed == "yes", (
        f"page B did not land cleanly (text={landed!r})"
    )
