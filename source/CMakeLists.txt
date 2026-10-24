# ==============================================================================
# The gfx libraries: core (shared), graphics (`import gfx;`), platform (`import gfx.platform;`), the
# bridge between platform windows and graphics swapchains (`import gfx.platform.graphics;`) and the UI
# (`import gfx.ui;`).
# ==============================================================================
add_subdirectory(core)
add_subdirectory(graphics)
if(GFX_BUILD_PLATFORM)
    add_subdirectory(platform)
    add_subdirectory(platform_graphics)
endif()
if(GFX_BUILD_UI)
    add_subdirectory(ui)
endif()
if(GFX_BUILD_PHYSICS)
    add_subdirectory(physics)
endif()
