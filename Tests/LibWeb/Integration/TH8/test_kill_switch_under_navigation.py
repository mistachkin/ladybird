# Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
#
# SPDX-License-Identifier: BSD-2-Clause

"""
TH8 navigation-cancellation race -- PoC test #6.

Verifies that flipping `Document::set_th8_disabled(true)` while TH8
is mid-evaluate, then navigating, tears the TH8Context down cleanly.

Currently SKIPPED: `set_th8_disabled` is intentionally an embedder
C++ API (Libraries/LibWeb/DOM/Document.h, audit item M15-followup);
no JS / WebDriver surface exposes it.  Wiring up case #6 needs ONE
of:

  (a) a `window.internals.disableTh8(true)` hook gated on the
      Internals object (which is already test-only in Ladybird and
      not exposed in shipping builds), or
  (b) a Ladybird-specific WebDriver extension endpoint (POST
      /session/:id/ladybird/disable-th8) that calls into the active
      document.

Both are small additions but neither is in the current PR scope.
Skeleton kept here so the test slot is reserved and the rationale is
discoverable when the hook lands.
"""

from __future__ import annotations

import pytest


@pytest.mark.skip(
    reason="Document::set_th8_disabled is C++-only today; needs a "
    "window.internals.disableTh8 hook or a Ladybird-specific "
    "WebDriver extension before this case can drive the toggle."
)
def test_kill_switch_under_navigation(webdriver, fixture_url):
    # Skeleton: navigate to a TH8-enabled page, flip the kill switch
    # while TH8 is mid-eval, navigate away, assert clean tear-down.
    raise NotImplementedError


@pytest.mark.skip(
    reason="depends on window.internals exposure of disableTh8; see "
    "the sibling test_kill_switch_under_navigation skip rationale."
)
def test_kill_switch_blocks_subsequent_scripts(webdriver, fixture_url):
    # Skeleton: once set_th8_disabled(true), every subsequent
    # <script type='text/th8'> on the document must be refused with
    # an error event (HTMLScriptElement::execute_script path).
    raise NotImplementedError
