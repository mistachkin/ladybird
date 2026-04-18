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
