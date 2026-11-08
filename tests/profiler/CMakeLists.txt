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

# GPU timings (gfx::profiler_graphics) on every GPU of the machine, with the Vulkan validation layers when the Vulkan tests
# found them, and on the graphics tests' fake device.
if(GFX_BACKEND_VULKAN)
    gfx_add_test(gfx_profiler_graphics_tests
        SOURCES gpu_timer_test.cpp
        MODULES ${PROJECT_SOURCE_DIR}/tests/graphics/interface/fake_backend.cppm
        MODULE_BASE_DIRECTORY ${PROJECT_SOURCE_DIR}/tests/graphics/interface
        LIBRARIES gfx::profiler_graphics
        GPU_TESTS ^Gpu/)
    set(_gfx_profiler_layer_directory "")
    if(GFX_VULKAN_VALIDATION_LAYER_DIRECTORY)
        set(_gfx_profiler_layer_directory "${GFX_VULKAN_VALIDATION_LAYER_DIRECTORY}")
    endif()
    target_compile_definitions(gfx_profiler_graphics_tests PRIVATE GFX_TEST_VULKAN_LAYER_DIRECTORY="${_gfx_profiler_layer_directory}")
    target_include_directories(gfx_profiler_graphics_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/graphics/vulkan)
endif()
