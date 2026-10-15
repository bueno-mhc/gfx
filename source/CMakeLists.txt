# ==============================================================================
# The gfx libraries: core (shared), graphics (`import gfx;`) and platform (`import gfx.platform;`).
# ==============================================================================
add_subdirectory(core)
add_subdirectory(graphics)
if(GFX_BUILD_PLATFORM)
    add_subdirectory(platform)
endif()
