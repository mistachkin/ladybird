# Copyright (c) 2026, Joe Mistachkin <joe@mistachkin.com>
#
# SPDX-License-Identifier: BSD-2-Clause

"""
Minimal WebDriver REST client used by the integration test PoC.

Talks to Ladybird's WebDriver service (Services/WebDriver/, route
table at Libraries/LibWeb/WebDriver/Client.cpp) using stdlib urllib.
No external dependencies; pytest is the only test framework piece
the rest of the harness pulls in.

Only the routes the PoC actually exercises are wrapped here.  Add
more wrappers as needed -- the underlying methods (`_get`, `_post`,
`_delete`) accept any path.
"""

from __future__ import annotations

import json
import urllib.error
import urllib.request


class WebDriverError(RuntimeError):
    """Raised on a non-2xx response or a {error: ...} payload."""


class WebDriverClient:
    def __init__(self, base_url: str = "http://127.0.0.1:8000") -> None:
        self.base = base_url.rstrip("/")
        self.session_id: str | None = None

    # --- raw HTTP -----------------------------------------------------

    def _request(self, method: str, path: str, body: dict | None = None) -> dict:
        url = f"{self.base}{path}"
        data = None
        headers = {}
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            headers["Content-Type"] = "application/json"
        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(req, timeout=30) as response:
                payload = json.loads(response.read().decode("utf-8") or "{}")
        except urllib.error.HTTPError as e:
            # WebDriver returns errors as JSON bodies too.
            try:
                payload = json.loads(e.read().decode("utf-8") or "{}")
            except Exception:
                payload = {"value": {"error": "http_error", "message": str(e)}}
            raise WebDriverError(f"{method} {path}: {payload}") from e

        # Per WebDriver spec, errors are wrapped in `value: {error, message}`.
        value = payload.get("value")
        if isinstance(value, dict) and value.get("error"):
            raise WebDriverError(f"{method} {path}: {value}")
        return payload

    def _get(self, path: str) -> dict:
        return self._request("GET", path)

    def _post(self, path: str, body: dict | None = None) -> dict:
        return self._request("POST", path, body or {})

    def _delete(self, path: str) -> dict:
        return self._request("DELETE", path)

    # --- session -------------------------------------------------------

    def new_session(self) -> "WebDriverClient":
        # Empty capability set -- Ladybird's WebDriver accepts a bare
        # `alwaysMatch: {}` per spec.  Add caps if a test needs them
        # (e.g. headless was already requested at service launch).
        payload = self._post("/session", {
            "capabilities": {"alwaysMatch": {}},
        })
        # Spec says session id lives at value.sessionId; some servers
        # mirror it at the top level.  Tolerate both.
        value = payload.get("value", {})
        self.session_id = value.get("sessionId") or payload.get("sessionId")
        if not self.session_id:
            raise WebDriverError(f"no sessionId in /session response: {payload}")
        return self

    def quit(self) -> None:
        if self.session_id is None:
            return
        try:
            self._delete(f"/session/{self.session_id}")
        finally:
            self.session_id = None

    # --- timeouts ------------------------------------------------------

    def set_page_load_timeout(self, milliseconds: int) -> None:
        """Cap navigation latency so a hung renderer fails the test."""
        self._post(
            f"/session/{self.session_id}/timeouts",
            {"pageLoad": int(milliseconds)},
        )

    def set_script_timeout(self, milliseconds: int) -> None:
        self._post(
            f"/session/{self.session_id}/timeouts",
            {"script": int(milliseconds)},
        )

    # --- navigation ----------------------------------------------------

    def navigate(self, url: str) -> None:
        self._post(f"/session/{self.session_id}/url", {"url": url})

    def get_url(self) -> str:
        return self._get(f"/session/{self.session_id}/url")["value"]

    def back(self) -> None:
        self._post(f"/session/{self.session_id}/back")

    def forward(self) -> None:
        self._post(f"/session/{self.session_id}/forward")

    # --- script execution ---------------------------------------------

    def execute(self, script: str, *args):
        """Run synchronous JS in the current document; return the value."""
        payload = self._post(
            f"/session/{self.session_id}/execute/sync",
            {"script": script, "args": list(args)},
        )
        return payload["value"]
