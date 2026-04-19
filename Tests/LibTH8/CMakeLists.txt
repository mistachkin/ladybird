# Fuzz targets for TH8 (libFuzzer).
#
# These are not built by default.  To build:
#   cmake -DENABLE_FUZZERS=ON ...
#   ninja fuzz_th8_eval

if (ENABLE_FUZZERS)
    add_executable(fuzz_th8_eval FuzzTH8Eval.cpp)
    target_link_libraries(fuzz_th8_eval PRIVATE LibTH8)
    target_compile_options(fuzz_th8_eval PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_th8_eval PRIVATE -fsanitize=fuzzer)
endif()
