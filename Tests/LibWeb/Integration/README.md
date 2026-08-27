# Tests/LibWeb/Integration -- WebDriver-driven integration tests

**Status: proof of concept.**  Not wired into the default build or CI.

This directory hosts tests that span more than a single HTML document.
The existing single-page harness under `Tests/LibWeb/test-web/` can
only load one URL per test and observe in-process JavaScript state;
the scenarios under `Integration/` need a real browser session
driven from outside the renderer process so they can:

- drive `navigate -> wait -> navigate` flows deterministically,
- observe renderer-process state across the navigation boundary
  (crash, hang, leak),
- enforce wall-clock budgets ("navigation must complete in <= 500 ms"),
- catch races that only fire when the test driver and the renderer
  are in different processes.

The harness talks to Ladybird's existing `WebDriver` service
(`Services/WebDriver/`, route table in
`Libraries/LibWeb/WebDriver/Client.cpp`) over plain HTTP.  No
heavyweight client library is pulled in -- Python stdlib `urllib`
plus pytest is enough.

## Running locally via CTest

```sh
# 1. Configure with integration tests enabled.
cmake --preset Sanitizer -DENABLE_INTEGRATION_TESTS=ON

# 2. Build Ladybird plus the WebDriver target explicitly.  WebDriver
#    is not in the default install set on every preset.
cmake --build --preset Sanitizer --target WebDriver

# 3. Install pytest into the venv your build is using.
python3 -m pip install --user pytest

# 4. Run only the integration entries via CTest filter.
ctest --preset Sanitizer -R LibWeb_Integration --output-on-failure
```

The `Tests/LibWeb/Integration/CMakeLists.txt` registers a single
`LibWeb_Integration` CTest entry that invokes `pytest -v` against the
whole `Integration/` tree.  Each `test_*.py` module is a separate
pytest unit; `conftest.py` spawns the WebDriver service on a free
loopback port per session and tears it down at the end.

## Running without CTest

You can drive `pytest` directly without CMake when iterating:

```sh
export LADYBIRD_WEBDRIVER_BIN=$(pwd)/Build/Sanitizer/bin/WebDriver
python3 -m pytest -v Tests/LibWeb/Integration/TH8/
```

The conftest looks at `LADYBIRD_WEBDRIVER_BIN` and skips the entire
session if it's unset or points at a non-file, with an actionable
message.

## CI

The integration tests run via the standalone workflow
`.github/workflows/integration-tests.yml`.  This is deliberately
**separate from upstream's `ci.yml`** so the harness works on a fork
without depending on Blacksmith / self-hosted runners, the
`ghcr.io/ladybirdbrowser/ladybird-ci` container, or the
`VCPKG_CACHE_SAS` secret -- none of which a fork can use.

The workflow:
- runs on `push` to master and feature branches, on `pull_request`,
  and via `workflow_dispatch` (manual trigger),
- uses GitHub-hosted `ubuntu-latest`,
- installs the system deps inline (no container),
- caches ccache + vcpkg-binary-cache,
- builds with `cmake --preset Sanitizer -DENABLE_INTEGRATION_TESTS=ON`,
- builds only the `WebDriver` target (not the GUI),
- runs `ctest --preset Sanitizer -R LibWeb_Integration`,
- uploads the ASAN/UBSAN log files as an artifact on failure.

ASAN/UBSAN are active, so leaks and UAFs caught by the integration
tests (the high-value reason this harness exists) surface as test
failures with stack traces in the uploaded log artifacts.

If a new integration test would slow CI prohibitively, gate it with
a pytest marker (`@pytest.mark.slow`) and skip the marker in CI
until the test is ready.

### When upstreaming

If this harness ever lands in
[upstream](https://github.com/LadybirdBrowser/ladybird) and they want
it in their main matrix, the `lagom-template.yml` already has the
`add_subdirectory("Integration")` wiring in `Tests/LibWeb/CMakeLists.txt`
and the `ENABLE_INTEGRATION_TESTS` option in
`Meta/CMake/cmake_options.cmake`.  Upstream would add a matrix entry
to `ci.yml` that sets `-DENABLE_INTEGRATION_TESTS=ON`, builds
`WebDriver`, and runs `ctest -R LibWeb_Integration`.  The standalone
fork workflow can be deleted at that point.

## What's here

| Path                                    | Role                                          |
|-----------------------------------------|-----------------------------------------------|
| `webdriver_client.py`                   | Minimal urllib-based WebDriver REST client    |
| `conftest.py`                           | pytest fixtures: WebDriver lifecycle + URLs   |
| `TH8/`                                  | TH8 scripting tests (this PoC)                |
| `TH8/fixtures/`                         | HTML fixtures the tests navigate to           |
| `TH8/test_*.py`                         | Individual test cases                         |

## What's NOT here yet

- HTTP fixture server.  The PoC navigates to `file://` URLs; that's
  fine for the watchdog test but doesn't exercise origin / CSP /
  service-worker paths the audit also wants covered.  Mirror
  `Tests/LibWeb/Fixtures/http-test-server.py` here when adding tests
  that need a real origin.
- Cases 2-6 from `TH8/README.md` (no-leak, event-handler
  self-navigate, sidecar-into-dead-document, back-forward,
  kill-switch).  Slots reserved; harness ready.
