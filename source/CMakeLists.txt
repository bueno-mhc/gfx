# ==============================================================================
# The gfx libraries: core (shared), the profiler (`import gfx.profiler;`, on core only), graphics (`import gfx;`),
# platform (`import gfx.platform;`), the bridge between platform windows and graphics swapchains
# (`import gfx.platform.graphics;`), the UI (`import gfx.ui;`) and physics (`import gfx.physics;`).
# ==============================================================================
add_subdirectory(core)
add_subdirectory(profiler)
add_subdirectory(graphics)
add_subdirectory(profiler_graphics)   # the profiler's GPU timings through the graphics library (QueryPool)
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
