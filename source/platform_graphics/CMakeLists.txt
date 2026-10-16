# ==============================================================================
# gfx::platform_graphics: the bridge between platform windows and graphics swapchains
# (`import gfx.platform.graphics;`, which re-exports gfx and gfx.platform). Built with the platform
# library; neither library depends on it.
# ==============================================================================
add_library(gfx_platform_graphics)
add_library(gfx::platform_graphics ALIAS gfx_platform_graphics)

target_sources(gfx_platform_graphics
    PUBLIC
        FILE_SET public_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES platform_graphics.cppm
    PRIVATE
        platform_graphics.cpp)

target_compile_features(gfx_platform_graphics PUBLIC cxx_std_23)
target_link_libraries(gfx_platform_graphics
    PUBLIC gfx::graphics gfx::platform
    PRIVATE gfx::project_options)
