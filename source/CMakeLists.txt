# ==============================================================================
# The gfx library. Its public API is a set of C++ modules; `import gfx;` reaches all of it.
# ==============================================================================
add_library(gfx)
add_library(gfx::gfx ALIAS gfx)

target_sources(gfx
    PUBLIC
        FILE_SET public_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            gfx.cppm
            enums.cppm
            errors.cppm
            handles.cppm
    PRIVATE
        errors.cpp)

# Headers exist only where a module cannot serve (toolchain workarounds), e.g. <gfx/windows_headers.hpp>.
target_include_directories(gfx PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(gfx PUBLIC cxx_std_23)
target_link_libraries(gfx
    PUBLIC gfx::toolchain_workarounds   # consumers need them too (see cmake/GfxProjectOptions.cmake)
    PRIVATE gfx::project_options)
