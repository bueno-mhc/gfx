# Toolchain defect detectors. Each test PASSES while the defect it describes still exists, which
# justifies the matching workaround. When one starts FAILING, the toolchain was fixed: remove the
# workaround named in the comment and this test.
if(NOT (MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    return()
endif()

# Workaround: -Wl,--allow-multiple-definition in cmake/GfxProjectOptions.cmake.
gfx_add_compilation_failure_test(ImportStdDuplicatesStaticLocals
    SOURCES import_std_first_formatter.cpp import_std_second_formatter.cpp
    EXPECTED_DIAGNOSTIC "multiple definition of"
    WITHOUT_PROJECT_OPTIONS)
# Optimized builds fold this particular static local away; -O0 reproduces the defect in every configuration.
target_compile_options(gfx_compilation_failure_ImportStdDuplicatesStaticLocals PRIVATE -O0)

# Workaround: source/include/gfx/windows_headers.hpp.
gfx_add_compilation_failure_test(WindowsHeadersConflictWithModules
    SOURCES windows_headers_with_modules.cpp
    EXPECTED_DIAGNOSTIC "conflicting language linkage for imported declaration"
    WITHOUT_PROJECT_OPTIONS)
