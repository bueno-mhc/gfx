# Presentation to platform windows through gfx.platform.graphics: visible windows of every platform
# backend built, on every GPU of the machine, with the Vulkan validation layers when installed (their
# directory, GFX_VULKAN_VALIDATION_LAYER_DIRECTORY, comes from tests/graphics/vulkan).
if(GFX_BACKEND_VULKAN AND (GFX_PLATFORM_BACKEND_SDL3 OR GFX_PLATFORM_BACKEND_GLFW))
    gfx_add_test(gfx_presentation_tests
        SOURCES presentation_test.cpp
        LIBRARIES gfx::platform_graphics)
    set(_gfx_layer_directory "")
    if(GFX_VULKAN_VALIDATION_LAYER_DIRECTORY)
        set(_gfx_layer_directory "${GFX_VULKAN_VALIDATION_LAYER_DIRECTORY}")
    endif()
    target_compile_definitions(gfx_presentation_tests PRIVATE GFX_TEST_VULKAN_LAYER_DIRECTORY="${_gfx_layer_directory}")
    target_include_directories(gfx_presentation_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/graphics/vulkan)
    # With the UI library, the UI renderer draws into the windows too.
    if(TARGET gfx_ui)
        target_link_libraries(gfx_presentation_tests PRIVATE gfx::ui)
        target_compile_definitions(gfx_presentation_tests PRIVATE GFX_TEST_WITH_UI=1)
    else()
        target_compile_definitions(gfx_presentation_tests PRIVATE GFX_TEST_WITH_UI=0)
    endif()
endif()
