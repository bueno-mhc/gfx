# ==============================================================================
# The bridge between the profiler and the graphics library (docs/profiler-performance-design.md): GPU timings through
# QueryPool timestamps (`import gfx.profiler.graphics;`, GpuTimer). It depends on gfx::profiler and the graphics
# library; the profiler itself depends on neither graphics nor this bridge.
# ==============================================================================
add_library(gfx_profiler_graphics)
add_library(gfx::profiler_graphics ALIAS gfx_profiler_graphics)
target_sources(gfx_profiler_graphics
    PUBLIC
        FILE_SET public_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            profiler_graphics.cppm
    PRIVATE
        gpu_timer.cpp)

target_compile_features(gfx_profiler_graphics PUBLIC cxx_std_23)
target_link_libraries(gfx_profiler_graphics
    PUBLIC gfx::profiler gfx::graphics
    PRIVATE gfx::project_options)
