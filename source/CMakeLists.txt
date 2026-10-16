# ==============================================================================
# The gfx libraries: core (shared), graphics (`import gfx;`), platform (`import gfx.platform;`) and
# the bridge between platform windows and graphics swapchains (`import gfx.platform.graphics;`).
# ==============================================================================
add_subdirectory(core)
add_subdirectory(graphics)
if(GFX_BUILD_PLATFORM)
    add_subdirectory(platform)
    add_subdirectory(platform_graphics)
endif()
