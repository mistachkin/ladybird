# Fuzz targets for TH8 (libFuzzer).
#
# These are not built by default.  To build:
#   cmake -DENABLE_FUZZERS=ON ...
#   ninja fuzz_th8_eval
#
# Currently fuzzed:
#   fuzz_th8_eval     -- TH8 parser + evaluator with the bare libc platform.
#                        Covers Th8_Eval, expression evaluation, builtin
#                        command dispatch, parser edge cases.
#
# Follow-up work (audit item B4):
#   - fuzz_th8_dom_bridge:  exercise the LibWeb DOM bridge
#     (HandleTable, TypeConversion, DOMBridge::register_dom_commands)
#     against a minimal mock Document.  Blocked on a libFuzzer-friendly
#     fixture for the GC heap + DOM tree; consider following the
#     LibWeb/Layout test-fixture pattern under Tests/LibWeb/.
#   - fuzz_th8_handle_parse:  the M10 concern (strict ^obj([0-9]+)$
#     handle parse) is now covered by
#     Tests/LibWeb/TestTH8HandleTable.cpp, which drives the pure
#     HandleTable::parse_handle_id() helper against ~40 adversarial
#     inputs (missing prefix, wrong-case prefix, junk prefix, sign
#     characters, hex-shape digits, embedded whitespace, u64 overflow,
#     high-byte / NUL bytes).  A dedicated libFuzzer target no longer
#     buys additional coverage against the parse contract; leave here
#     as a note in case the parse grows richer semantics later.

if (ENABLE_FUZZERS)
    add_executable(fuzz_th8_eval FuzzTH8Eval.cpp)
    target_link_libraries(fuzz_th8_eval PRIVATE LibTH8)
    target_compile_options(fuzz_th8_eval PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_th8_eval PRIVATE -fsanitize=fuzzer)
    # [L19] The fuzz target drives the raw C API (Th8_GetLibcPlatform,
    # Th8_CreateInterp, etc.) directly so it can mirror the smallest
    # possible host setup.  Grant it private access to the vendored
    # header that the public LibTH8 surface no longer exposes.
    target_include_directories(fuzz_th8_eval PRIVATE
        ${CMAKE_SOURCE_DIR}/Libraries/LibTH8/Vendor)
endif()
