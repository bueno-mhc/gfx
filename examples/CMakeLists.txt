# ==============================================================================
# Examples: programs that show the libraries at work. With the tests, each also runs as a CTest test
# that closes itself after a number of frames (label "example").
# ==============================================================================
include(GfxRuntimeDeployment)

# A window presenting Vulkan frames driven by a compile-time input schema (platform + graphics).
if(GFX_BUILD_PLATFORM AND GFX_BACKEND_VULKAN AND (GFX_PLATFORM_BACKEND_SDL3 OR GFX_PLATFORM_BACKEND_GLFW))
    add_subdirectory(windowed)
endif()

# An editor-like UI (docking, menus, a table, inspectors, SDF text) over a scene, both drawn by a render thread
# (UI + platform + graphics, with the UI's atlas tool for its fonts).
if(GFX_BACKEND_VULKAN AND TARGET gfx_ui_platform AND TARGET gfx_ui_atlas_builder)
    add_subdirectory(ui_editor)
endif()

# A headless physics scene (Box3D or the reference backend) with a perspective and a map camera, checking the step,
# the queries and the camera math (physics + camera; no graphics, platform or UI).
if(TARGET gfx_physics AND TARGET gfx_camera)
    add_subdirectory(physics)
endif()

# The physics sandbox: a physics world (Box3D or the reference backend) drawn as lit meshes with Vulkan, seen through an
# orbit camera, with UI panels (physics + camera + graphics + platform + UI, with the UI's atlas tool for its font).
if(GFX_BACKEND_VULKAN AND TARGET gfx_ui_platform AND TARGET gfx_ui_atlas_builder AND TARGET gfx_physics AND TARGET gfx_camera)
    add_subdirectory(physics_sandbox)
endif()
