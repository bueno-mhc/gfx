# Tests of the profiler (docs/profiler-performance-design.md). They link gfx::profiler only (it depends on gfx::core
# only). Recording tests skip when GFX_PROFILER is off, where ProfilerCompiledOutTest checks that nothing is recorded;
# the overhead test runs only in optimized builds with the profiler on (the profile preset).
include(GfxDependencies)
gfx_require_nlohmann_json()

gfx_add_test(gfx_profiler_tests
    SOURCES
        enumeration_test.cpp
        recording_test.cpp
        report_test.cpp
        exporter_test.cpp
        determinism_test.cpp
        overhead_test.cpp
        chrome_json_reader.cpp     # plain translation unit: nlohmann/json cannot share one with import std
    LIBRARIES gfx::profiler gfx::nlohmann_json)

# The macros compile to nothing with GFX_PROFILER_ENABLED=0: same instructions as without them, no symbol, no string
# (cmake/scripts/GfxCheckProfilerCompileOut.cmake, on two small translation units compiled with -O2).
add_test(NAME ProfilerCompileOut.MacrosLeaveNoCodeAndNoData
         COMMAND ${CMAKE_COMMAND}
                 "-DCOMPILER=${CMAKE_CXX_COMPILER}"
                 "-DOBJDUMP=${CMAKE_OBJDUMP}"
                 "-DNM=${CMAKE_NM}"
                 "-DINCLUDE_DIRECTORY=${PROJECT_SOURCE_DIR}/source/profiler/include"
                 "-DSOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/compile_out"
                 "-DWORK_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/compile_out"
                 -P "${PROJECT_SOURCE_DIR}/cmake/scripts/GfxCheckProfilerCompileOut.cmake")
