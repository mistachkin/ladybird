# Tests/LibWeb/Integration -- WebDriver-driven integration tests.
#
# Gated behind ENABLE_INTEGRATION_TESTS (default OFF) so the regular
# build is unaffected.  When enabled, this directory registers a
# single CTest entry that drives pytest against the Integration/
# tree.  The conftest.py spawns the WebDriver service on a free
# loopback port; LADYBIRD_WEBDRIVER_BIN env var points it at the
# built binary.
#
# Run locally with:
#   cmake --preset Sanitizer -DENABLE_INTEGRATION_TESTS=ON
#   cmake --build --preset Sanitizer --target WebDriver
#   ctest --preset Sanitizer -R LibWeb_Integration --output-on-failure

if (NOT ENABLE_INTEGRATION_TESTS)
    return()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME LibWeb_Integration
    COMMAND
        ${Python3_EXECUTABLE} -m pytest -v
            ${CMAKE_CURRENT_SOURCE_DIR}
)

# Pass the path to the freshly-built WebDriver binary through to the
# pytest fixture (conftest.py:webdriver_service skips if absent).
# $<TARGET_FILE:WebDriver> resolves at test time, so it picks up the
# correct per-config output directory (Sanitizer/Release/etc.).
set_tests_properties(LibWeb_Integration PROPERTIES
    ENVIRONMENT
        "LADYBIRD_WEBDRIVER_BIN=$<TARGET_FILE:WebDriver>;LADYBIRD_SOURCE_DIR=${LADYBIRD_SOURCE_DIR}"
)

# Plain `ctest` does not rebuild dependencies; users must invoke
# `cmake --build --target WebDriver` first.  If the binary is
# missing, conftest.py raises pytest.skip with an actionable
# message rather than a cryptic failure.
