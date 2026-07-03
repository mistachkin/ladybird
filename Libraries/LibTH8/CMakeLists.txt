set(SOURCES
    Interpreter.cpp
    WebPlatform.cpp
    Vendor/th8.c
)

ladybird_lib(LibTH8 th8 EXPLICIT_SYMBOL_EXPORT)

# The amalgamation is C99; ensure it compiles as C.
set_source_files_properties(Vendor/th8.c PROPERTIES LANGUAGE C)

# [L18] Suppress warnings in vendored code.  Use a generator expression
# so the right flag fires for both GCC/Clang (-w) and MSVC (/w) --
# ENABLE_TH8 is now valid on both POSIX and Windows (WebPlatform.cpp
# has a Th8_GetWin32Platform arm), so the MSVC branch is actually
# reachable.
set_source_files_properties(Vendor/th8.c PROPERTIES COMPILE_OPTIONS
    "$<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-w>;$<$<C_COMPILER_ID:MSVC>:/w>")

# [L19] Vendor/th8.h is now PRIVATE: only LibTH8's own translation units
# (Interpreter.cpp, WebPlatform.cpp) get the full vendored upstream
# surface.  Consumers reach only the hand-curated Libraries/LibTH8/CAPI.h
# subset, which mirrors the small set of Th8_*/TH8_* symbols the bridge
# layer actually calls.  Adding a new consumer-side direct call requires
# mirroring its prototype in CAPI.h first.
target_include_directories(LibTH8 PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Vendor>
)
find_package(OpenSSL REQUIRED)
target_link_libraries(LibTH8 PRIVATE LibCore OpenSSL::SSL OpenSSL::Crypto)

# -----------------------------------------------------------------------
# TH8 feature configuration for web content scripting.
#
# Enable only the features needed for browser-embedded scripting.
# Security-sensitive features (binary loading, cryptography, libcurl,
# file system access) are deliberately disabled.
# -----------------------------------------------------------------------

target_compile_definitions(LibTH8 PUBLIC
    # Core language features.
    TH8_ENABLE_EXPRESSIONS      # expr command, math, comparisons
    TH8_ENABLE_VARIABLES        # set, unset, global, upvar, array
    TH8_ENABLE_REGEXP           # regexp, regsub (Spencer engine)
    TH8_ENABLE_BIGINT           # Arbitrary-precision integers (libtommath)
    TH8_ENABLE_CRYPTOGRAPHY     # Script signing and verification

    # Plugins: the command families available to scripts.
    TH8_PLUGIN_CONTROL          # if, switch, catch, try, return, break, continue, eval, error
    TH8_PLUGIN_EXPRESSIONS      # expr, fpclassify
    TH8_PLUGIN_FORMATTING       # format, scan
    TH8_PLUGIN_INTROSPECTION    # info, pid
    TH8_PLUGIN_IO               # puts, gets (routed to DevTools console via NullIO)
    TH8_PLUGIN_LISTS            # list, lindex, lappend, lsort, dict, split, join
    TH8_PLUGIN_LOOPING          # for, foreach, while
    TH8_PLUGIN_MANAGEMENT       # namespace, rename, interp cancel
    TH8_PLUGIN_PROCEDURES       # proc, apply, nproc, napply, tailcall
    TH8_PLUGIN_STRINGS          # string, concat, base64
    TH8_PLUGIN_TIMEKEEPING      # after, clock, time
    TH8_PLUGIN_VARIABLES        # variable command (namespace variables)

    # Deliberately NOT enabled:
    #   TH8_ENABLE_LOAD          - Binary loading (dlopen) is a security risk
    #   TH8_ENABLE_LIBCURL       - Browser has its own networking
    #   TH8_ENABLE_UNBOUND       - Browser has its own DNS
    #   TH8_ENABLE_FAULT_INJECTION - Testing infrastructure only
    #   TH8_ENABLE_TEST_KEY      - Test signing key only
    #   TH8_PLUGIN_EXTENSIBILITY - Requires TH8_ENABLE_LOAD
    #   TH8_PLUGIN_FILE_SYSTEMS  - File access blocked by NullIO platform
    #   TH8_PLUGIN_KEYVALUE      - Not yet needed
    #   TH8_USE_MIMALLOC         - LadyBird manages its own allocators
    #   TH8_USE_BESTLINE         - Interactive shell only
)
