# Toolchain conformance: C++23 features, `import std`, and GoogleTest headers mixed with modules.
gfx_add_test(gfx_toolchain_tests
    SOURCES toolchain_test.cpp
    MODULES toolchain_probe.cppm)

add_subdirectory(defects)
