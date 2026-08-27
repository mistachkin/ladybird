# Fuzz targets for TH8 (libFuzzer).
#
# These are not built by default.  To build:
#   cmake -DENABLE_FUZZERS=ON ...
#   ninja fuzz_th8_eval
#
# Currently fuzzed:
#   fuzz_th8_eval       -- TH8 parser + evaluator with the bare libc
#                          platform.  Covers Th8_Eval, expression
#                          evaluation, builtin command dispatch, parser
#                          edge cases.
#   fuzz_th8_dom_bridge -- the DOM bridge's untrusted-byte-handling
#                          surface: TypeConversion (string_from_th8 /
#                          flystring_from_th8 / set_result_string /
#                          set_error) and HandleTable::parse_handle_id,
#                          driven against arbitrary input.  Covers two of
#                          the three components named in audit B4
#                          (HandleTable, TypeConversion) without a live
#                          DOM tree.
#
# Follow-up work (audit item B4, remaining):
#   - The command ensemble dispatch (object_ensemble_command /
#     document_subcommand / register_dom_commands) is file-static in
#     DOMBridge.cpp and operates on real DOM::Node / DOM::Document
#     objects, so fuzzing it needs a GC-heap + live-Document fixture
#     (an event-loop-driven Page / navigable whose active_document()
#     the bridge is registered against).  That fixture is the remaining
#     blocker; consider the Web::Page setup in Tests/LibWeb/TestPage.cpp
#     plus a spin of Core::EventLoop to reach a ready about:blank
#     document.
#   - fuzz_th8_handle_parse:  the M10 concern (strict ^obj([0-9]+)$
#     handle parse) is covered both by fuzz_th8_dom_bridge above and by
#     Tests/LibWeb/TestTH8HandleTable.cpp (~40 hand-written adversarial
#     inputs).  No separate target needed.

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

    # DOM-bridge byte-handling fuzzer.  Links LibWeb for the Web::TH8::*
    # helpers (TypeConversion, HandleTable) and LibTH8 for the Th8_* C
    # API used to stand up a result buffer.  Heavier than fuzz_th8_eval
    # because it pulls in LibWeb, but only the pure conversion / parse
    # helpers are exercised -- no live document (see the B4 note above).
    add_executable(fuzz_th8_dom_bridge FuzzTH8DomBridge.cpp)
    target_link_libraries(fuzz_th8_dom_bridge PRIVATE LibTH8 LibWeb)
    target_compile_options(fuzz_th8_dom_bridge PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_th8_dom_bridge PRIVATE -fsanitize=fuzzer)
    target_include_directories(fuzz_th8_dom_bridge PRIVATE
        ${CMAKE_SOURCE_DIR}/Libraries/LibTH8/Vendor)
endif()
