# ==============================================================================
# gfx::core: what the gfx libraries share (enumeration machinery, errors, handles and identifiers,
# value types, the native window handle, the handle tables backends use). `import gfx.core;`
# ==============================================================================
add_library(gfx_core)
add_library(gfx::core ALIAS gfx_core)

target_sources(gfx_core
    PUBLIC
        FILE_SET public_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            core.cppm
            enums.cppm
            errors.cppm
            handles.cppm
            types.cppm
            native_window.cppm
            # Storage behind the handles backends issue (std only); not part of the gfx.core umbrella.
            slot_table.cppm
            handle_table.cppm
    PRIVATE
        errors.cpp)

# Headers exist only where a module cannot serve (toolchain workarounds), e.g. <gfx/windows_headers.hpp>.
target_include_directories(gfx_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# Debug builds validate every command and compile shaders at run time (gfx::DEBUG_BUILD in enums.cppm).
target_compile_definitions(gfx_core PUBLIC $<$<CONFIG:Debug>:GFX_DEBUG_BUILD=1>)

target_compile_features(gfx_core PUBLIC cxx_std_23)
target_link_libraries(gfx_core
    PUBLIC gfx::toolchain_workarounds   # consumers need them too (see cmake/GfxProjectOptions.cmake)
    PRIVATE gfx::project_options)
