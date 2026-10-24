# ==============================================================================
# gfx::core: what the gfx libraries share (enumeration machinery, errors, handles and identifiers,
# value types, the native window handle, the handle tables backends use). `import gfx.core;`
# ==============================================================================
add_library(gfx_core)
add_library(gfx::core ALIAS gfx_core)

# The math module's run-time SIMD paths: cglm (vectors, matrices, quaternions) and SIMDe (wide kernels).
include(GfxDependencies)
gfx_require_cglm()
gfx_require_simde()

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
            # Vectors, quaternions, matrices, transforms (constexpr; SIMD at run time); imported on its own.
            math.cppm
    PRIVATE
        errors.cpp
        math_kernels.cpp)   # plain translation unit: SIMDe's headers cannot share one with import std

# cglm's static inline functions are called from the math module's exported inline functions: GCC accepts
# that exposure of the global module fragment's translation-unit-local functions (warning
# -Wexpose-global-module-tu-local, an error with -Werror) and importers get their definitions from the module.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(math.cppm PROPERTIES COMPILE_OPTIONS -Wno-expose-global-module-tu-local)
endif()

# Headers exist only where a module cannot serve (toolchain workarounds), e.g. <gfx/windows_headers.hpp>.
target_include_directories(gfx_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# Debug builds validate every command and compile shaders at run time (gfx::DEBUG_BUILD in enums.cppm).
target_compile_definitions(gfx_core PUBLIC $<$<CONFIG:Debug>:GFX_DEBUG_BUILD=1>)

target_compile_features(gfx_core PUBLIC cxx_std_23)
target_link_libraries(gfx_core
    PUBLIC
        gfx::toolchain_workarounds   # consumers need them too (see cmake/GfxProjectOptions.cmake)
        gfx::simd_options            # the math's inline SIMD code is compiled in consumers too
    PRIVATE gfx::project_options gfx::cglm gfx::simde)
