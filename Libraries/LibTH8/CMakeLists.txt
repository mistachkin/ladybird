set(SOURCES
    Interpreter.cpp
    WebPlatform.cpp
    Vendor/th8.c
)

ladybird_lib(LibTH8 th8)

# The amalgamation is C99; ensure it compiles as C.
set_source_files_properties(Vendor/th8.c PROPERTIES LANGUAGE C)

# Suppress warnings in vendored code.
set_source_files_properties(Vendor/th8.c PROPERTIES COMPILE_FLAGS "-w")

target_include_directories(LibTH8 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Vendor>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(LibTH8 PRIVATE LibCore)

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
