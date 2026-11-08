# ==============================================================================
# The profiler (docs/profiler-performance-design.md): CPU scopes, counters, sizes, markers and asynchronous spans
# recorded into per-thread lock-free rings, sessions of frames, traces (Chrome Tracing JSON, a binary format) and
# reports. `import gfx.profiler;` and the PROFILE_* macros of <gfx/profiler.hpp>. It depends on gfx::core only (and
# zstd, privately, for compressed binary traces), so every gfx library may use it; the build checks it.
# With GFX_PROFILER off, GFX_PROFILER_ENABLED is 0 for the library and its users: the macros expand to nothing and the
# recording functions return at once.
# ==============================================================================
include(GfxDependencies)
include(GfxDecoupling)
gfx_require_zstd()

add_library(gfx_profiler)
add_library(gfx::profiler ALIAS gfx_profiler)
target_sources(gfx_profiler
    PUBLIC
        FILE_SET public_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            enums.cppm
            handles.cppm
            errors.cppm
            profiler.cppm
            site.cppm
            trace.cppm
            session.cppm
            report.cppm
            exporters.cppm
            state.cppm
    PRIVATE
        errors.cpp
        recording.cpp
        profiler.cpp
        trace.cpp
        report.cpp
        chrome_json.cpp
        binary_trace.cpp)

target_compile_features(gfx_profiler PUBLIC cxx_std_23)
target_include_directories(gfx_profiler PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_definitions(gfx_profiler PUBLIC GFX_PROFILER_ENABLED=$<BOOL:${GFX_PROFILER}>)
target_link_libraries(gfx_profiler
    PUBLIC gfx::core
    PRIVATE gfx::project_options gfx::zstd)
gfx_require_decoupled(gfx_profiler
    FORBIDDEN gfx gfx_platform gfx_platform_graphics gfx_ui gfx_ui_platform gfx_ui_atlas_tools gfx_physics_common gfx_camera
              gfx_physics_components gfx_physics gfx_physics_gpu_xpbd)
