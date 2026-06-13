# Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
#
# SPDX-License-Identifier: BSD-2-Clause

"""
TH8 navigation-cancellation race -- PoC test #2.

Hammers the navigate-during-eval path repeatedly to give ASAN/LSAN a
chance to detect a leaked TH8Context, WebPlatformContext, or Th8_Interp
on the navigation tear-down path.

The test does not directly assert "no leak" -- LSAN runs at process
shutdown (when the WebDriver service exits), so a leak surfaces as a
nonzero exit code from the WebDriver binary, which the CI workflow
captures via the asan.log artifact upload.  This test's local
assertions verify only that the loop ran cleanly N times; the leak
signal comes from outside the test.

Run under the Sanitizer preset (the integration-tests.yml workflow
default) for the leak detector to be active.
"""

from __future__ import annotations

import time


ROUNDS = 3
NAV_BUDGET_MS = 500
READY_TIMEOUT_S = 1.0


def _wait_until_ready(webdriver) -> bool:
    deadline = time.monotonic() + READY_TIMEOUT_S
    while time.monotonic() < deadline:
        try:
            if webdriver.execute("return !!window.__th8_ready__;"):
                return True
        except Exception:
            pass
        time.sleep(0.05)
    return False


def test_no_leak_under_repeated_navigation(webdriver, fixture_url):
    page_a = fixture_url("page_a_long_th8.html")
    page_b = fixture_url("page_b_lands.html")

    webdriver.set_page_load_timeout(NAV_BUDGET_MS * 4)
    webdriver.set_script_timeout(500)

    for round_num in range(ROUNDS):
        # Force a fresh document for each iteration so the TH8Context
        # construction and destruction paths both run.  The leak would
        # be in the latter; cycling guarantees we hit it.
        webdriver.navigate(page_a)
        assert _wait_until_ready(webdriver), (
            f"round {round_num}: page A never signalled readiness"
        )

        # The watchdog must fire, navigation must complete.
        started = time.monotonic()
        webdriver.navigate(page_b)
        elapsed_ms = int((time.monotonic() - started) * 1000)

        landed = webdriver.execute(
            "return document.getElementById('landed') "
            "    ? document.getElementById('landed').textContent : null;"
        )
        assert landed == "yes", (
            f"round {round_num}: page B did not land (text={landed!r})"
        )
        assert elapsed_ms <= NAV_BUDGET_MS, (
            f"round {round_num}: navigation took {elapsed_ms} ms "
            f"(budget {NAV_BUDGET_MS} ms)"
        )

    # The actual leak assertion is delegated to LSAN at process exit;
    # see Tests/LibWeb/Integration/README.md "CI" section for the
    # ASAN_OPTIONS log_path setup that surfaces the failure.
