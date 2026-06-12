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
#   - fuzz_th8_handle_parse:  drive HandleTable::resolve with
#     adversarial handle strings (myobj42, obj-1, obj42extra, etc.) to
#     verify the strict ^obj([0-9]+)$ contract once M10 lands.

if (ENABLE_FUZZERS)
    add_executable(fuzz_th8_eval FuzzTH8Eval.cpp)
    target_link_libraries(fuzz_th8_eval PRIVATE LibTH8)
    target_compile_options(fuzz_th8_eval PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_th8_eval PRIVATE -fsanitize=fuzzer)
endif()
