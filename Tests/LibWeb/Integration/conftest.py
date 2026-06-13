# Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
#
# SPDX-License-Identifier: BSD-2-Clause

"""
pytest fixtures shared by every test under Tests/LibWeb/Integration.

Two layers:
  1. Session-scoped `webdriver_service` -- starts the WebDriver
     binary as a subprocess, polls /status until it answers, yields
     the base URL, terminates on session teardown.
  2. Function-scoped `webdriver` -- opens a fresh WebDriver session
     on top of the service, deletes it at the end of the test.

Plus a `fixture_url` helper that maps a fixture file name to a
file:// URL the renderer can load.  Tests requiring a real origin
should spin up an HTTP server fixture instead (see Integration/README.md).
"""

from __future__ import annotations

import os
import pathlib
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

import pytest

# Re-export so test modules can `from conftest import WebDriverError`.
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from webdriver_client import WebDriverClient, WebDriverError  # noqa: E402,F401


def _pick_free_port() -> int:
    s = socket.socket()
    try:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]
    finally:
        s.close()


def _wait_for_status(base_url: str, deadline_seconds: float) -> None:
    """Poll /status until 200, or raise after `deadline_seconds`."""
    deadline = time.monotonic() + deadline_seconds
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(f"{base_url}/status", timeout=1) as r:
                if r.status == 200:
                    return
        except (urllib.error.URLError, ConnectionError, OSError) as e:
            last_error = e
        time.sleep(0.05)
    raise RuntimeError(
        f"WebDriver service at {base_url} did not respond to /status "
        f"within {deadline_seconds}s (last error: {last_error})"
    )


@pytest.fixture(scope="session")
def webdriver_service():
    """Spawn the WebDriver service on a free loopback port; tear down at end."""
    binary = os.environ.get("LADYBIRD_WEBDRIVER_BIN")
    if not binary:
        pytest.skip(
            "LADYBIRD_WEBDRIVER_BIN not set; build with "
            "`cmake --build <preset> --target WebDriver` and export it."
        )
    if not pathlib.Path(binary).is_file():
        pytest.skip(f"LADYBIRD_WEBDRIVER_BIN={binary!r} is not a regular file")

    port = _pick_free_port()
    base_url = f"http://127.0.0.1:{port}"

    # Use --headless so the test does not require a display server in CI.
    proc = subprocess.Popen(
        [binary, "--headless", "--listen-address=127.0.0.1", f"--port={port}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        _wait_for_status(base_url, deadline_seconds=10.0)
    except Exception:
        proc.kill()
        out, err = proc.communicate(timeout=2)
        raise RuntimeError(
            "WebDriver service failed to start.\n"
            f"stdout: {out.decode(errors='replace')}\n"
            f"stderr: {err.decode(errors='replace')}"
        )

    yield base_url

    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)


@pytest.fixture
def webdriver(webdriver_service):
    """Open a fresh WebDriver session per test."""
    client = WebDriverClient(base_url=webdriver_service).new_session()
    try:
        yield client
    finally:
        try:
            client.quit()
        except WebDriverError:
            # Session may already be dead (renderer crash); not the
            # teardown's problem.
            pass


@pytest.fixture
def fixture_url():
    """
    Map `name` -> file:// URL of `fixtures/<name>` next to the calling
    test module.  Tests that need cross-origin / proper Content-Type
    headers should not use this -- use a real HTTP fixture server.
    """
    def builder(name: str, *, base_dir: pathlib.Path | None = None) -> str:
        base = base_dir or (pathlib.Path(_current_test_dir()) / "fixtures")
        path = (base / name).resolve()
        if not path.is_file():
            raise FileNotFoundError(f"fixture not found: {path}")
        return path.as_uri()
    return builder


def _current_test_dir() -> str:
    """Inspect the call stack for the calling test module's directory."""
    import inspect
    for frame in inspect.stack():
        filename = frame.filename
        if filename.endswith(".py") and "/test_" in filename:
            return os.path.dirname(filename)
    # Fallback: the conftest's own directory (one above the fixture dirs).
    return os.path.dirname(__file__)
