# Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
#
# SPDX-License-Identifier: BSD-2-Clause

"""
TH8 navigation-cancellation race -- PoC test #1.

Verifies that a TH8 script trapped in a tight loop on page A is
cancelled by the M2 wall-clock watchdog quickly enough that the
follow-up navigation to page B completes within budget.

If the watchdog is removed / disabled / mistuned, this test catches
it -- the loop would run to the 10M-step limit (could be seconds)
or, in a worst case, until the renderer is killed externally.

See ../README.md for prerequisites and run instructions.
"""

from __future__ import annotations

import time

import pytest


# Budget for the navigation from page A (TH8 in infinite loop) to page B.
#
# Comprises:
#   ~250 ms: M2 wall-clock watchdog default
#   ~10  ms: Th8_Eval unwind + clean_up_after_running_script
#   ~50  ms: IPC + WebDriver round-trip overhead (loopback)
#   ~190 ms: slack for slow CI runners, sanitizer builds, etc.
#
# Sanitizer / debug builds typically need 2-4x; consider parameterizing
# the budget when wiring into CI.
NAV_BUDGET_MS = 500

# Soft cap on how long we wait for the page-A "TH8 is now looping"
# signal.  If TH8 has not started within this window the test is
# inconclusive (we cannot tell if the watchdog fired).
READY_TIMEOUT_S = 1.0


def _wait_until_ready(webdriver) -> bool:
    deadline = time.monotonic() + READY_TIMEOUT_S
    while time.monotonic() < deadline:
        try:
            ready = webdriver.execute("return !!window.__th8_ready__;")
        except Exception:
            ready = None
        if ready:
            return True
        time.sleep(0.05)
    return False


def test_watchdog_meets_navigation_budget(webdriver, fixture_url):
    page_a = fixture_url("page_a_long_th8.html")
    page_b = fixture_url("page_b_lands.html")

    # Cap the navigation timeout so a hung renderer fails the test
    # with a clear message instead of silently waiting 5 minutes
    # for the WebDriver default.
    webdriver.set_page_load_timeout(NAV_BUDGET_MS * 4)
    webdriver.set_script_timeout(500)

    # 1. Land on page A.  The fixture flips __th8_ready__ via
    #    cross-eval BEFORE entering the infinite loop, so a successful
    #    navigate() here only proves "page A's parser ran", not
    #    "TH8 is looping".  That's what step 2 is for.
    webdriver.navigate(page_a)

    # 2. Confirm TH8 actually started executing the loop.
    assert _wait_until_ready(webdriver), (
        "page A never signalled __th8_ready__; either TH8 did not start "
        "or the cross-eval shim failed -- the watchdog claim cannot be "
        "verified."
    )

    # 3. Trigger the navigation while TH8 is mid-loop, and time it.
    #    The WebDriver `navigate` call blocks until the page-load
    #    event fires on page B; the renderer must (a) cancel TH8,
    #    (b) unwind, (c) process the navigation, (d) load page B,
    #    (e) signal page-load -- all within NAV_BUDGET_MS.
    started = time.monotonic()
    webdriver.navigate(page_b)
    elapsed_ms = int((time.monotonic() - started) * 1000)

    # 4. Assert we landed on page B cleanly.  A renderer crash here
    #    would surface as a WebDriverError from execute() (session
    #    gone) rather than as a wrong return value.
    landed = webdriver.execute(
        "return document.getElementById('landed') "
        "    ? document.getElementById('landed').textContent : null;"
    )
    assert landed == "yes", (
        f"page B did not load cleanly (read landed={landed!r}); "
        "renderer may have crashed or navigation hung."
    )

    # 5. Assert the budget held.  This is the load-bearing assertion
    #    for the M2 wall-clock watchdog -- without it, the loop would
    #    run to the 10M-step limit and this would fail by seconds.
    assert elapsed_ms <= NAV_BUDGET_MS, (
        f"navigation from page A to page B took {elapsed_ms} ms "
        f"(budget {NAV_BUDGET_MS} ms); the M2 wall-clock watchdog "
        "likely did not fire in time -- check default_wall_clock_limit_ms "
        "in Libraries/LibTH8/WebPlatform.h."
    )
