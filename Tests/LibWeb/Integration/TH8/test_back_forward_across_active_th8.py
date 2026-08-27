# Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
#
# SPDX-License-Identifier: BSD-2-Clause

"""
TH8 navigation-cancellation race -- PoC test #5.

Drives a four-step sequence:
    1. Navigate to page E (TH8 runs, sets a marker)
    2. Navigate to page B (page B has its own marker)
    3. Back -> page E (TH8 runs again on the restored / reloaded doc)
    4. Forward -> page B

Each transition must land cleanly.  Whether Ladybird uses BFCache or
reload-from-scratch is observable from the test (BFCache would keep
the marker; reload restarts TH8 and re-sets it -- both are OK as
long as the marker reads correctly).

This is the broadest-coverage case in the suite: page E's
TH8Context is constructed and destroyed at least twice in one test
run.  ASAN catches lifecycle UAFs on the destruction path.
"""

from __future__ import annotations

import time

import pytest


NAV_BUDGET_MS = 800
SETTLE_TIMEOUT_S = 2.0


def _wait_for_marker(webdriver, element_id: str, expected_text: str) -> bool:
    """Poll the DOM until the named element reads `expected_text`."""
    deadline = time.monotonic() + SETTLE_TIMEOUT_S
    while time.monotonic() < deadline:
        try:
            text = webdriver.execute(
                f"return document.getElementById('{element_id}') ? "
                f"document.getElementById('{element_id}').textContent : null;"
            )
        except Exception:
            text = None
        if text == expected_text:
            return True
        time.sleep(0.05)
    return False


def test_back_forward_across_active_th8(webdriver, fixture_url):
    page_e = fixture_url("page_e_th8_with_marker.html")
    page_b = fixture_url("page_b_lands.html")

    webdriver.set_page_load_timeout(NAV_BUDGET_MS * 4)
    webdriver.set_script_timeout(500)

    # 1. Land on page E.  TH8 runs and writes the marker.
    webdriver.navigate(page_e)
    assert _wait_for_marker(webdriver, "marker", "th8-ran"), (
        "page E marker was not set; TH8 did not run on first load"
    )

    # 2. Navigate to page B.
    webdriver.navigate(page_b)
    assert _wait_for_marker(webdriver, "landed", "yes"), (
        "page B did not land after forward navigation"
    )

    # 3. Back -- whether BFCache restores or reload re-runs, the marker
    #    must read 'th8-ran' on a clean trip.
    webdriver.back()
    assert _wait_for_marker(webdriver, "marker", "th8-ran"), (
        "page E marker missing after back navigation"
    )

    # 4. Forward -- back to page B.
    webdriver.forward()
    assert _wait_for_marker(webdriver, "landed", "yes"), (
        "page B did not re-land after forward navigation"
    )


@pytest.mark.skip(
    reason="full race variant requires TH8 to be MID-EVAL when back "
    "is pressed; that needs the M2 wall-clock watchdog to fire "
    "inside the back navigation, which is timing-fragile under "
    "file:// fixtures.  HTTP server fixture would let us pin it."
)
def test_back_while_th8_mid_eval(webdriver, fixture_url):
    # Skeleton placeholder for the timing-sensitive variant.
    raise NotImplementedError
