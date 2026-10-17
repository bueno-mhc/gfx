# The windowed example: gfx_windowed_example [--backend sdl3|glfw] [--device <index>] [--frames <count>] [--validation] [--metrics]
add_executable(gfx_windowed_example windowed_example.cpp)
target_link_libraries(gfx_windowed_example PRIVATE gfx::platform_graphics gfx::project_options)
gfx_deploy_runtime_dependencies(gfx_windowed_example)

# The metrics overlay is drawn by the UI library, with a font atlas built here from a fetched OFL font
# (Noto Sans); without the UI library or its atlas tools, the example has no overlay.
if(TARGET gfx_ui AND TARGET gfx_ui_atlas_builder)
    include(GfxDependencies)
    include(GfxUiAtlas)
    gfx_require_ui_test_fonts()
    gfx_add_ui_atlas(gfx_windowed_example
        NAME overlay_font
        FONT ${GFX_UI_TEST_FONT_DIRECTORY}/NotoSans.ttf
        CHARACTERS BASIC_LATIN
        KIND MTSDF SIZE 32 RANGE 4
        EMBED MODULE windowed.fonts)
    target_link_libraries(gfx_windowed_example PRIVATE gfx::ui)
    target_compile_definitions(gfx_windowed_example PRIVATE GFX_EXAMPLE_WITH_UI=1)
else()
    target_compile_definitions(gfx_windowed_example PRIVATE GFX_EXAMPLE_WITH_UI=0)
endif()

# One automated run per platform backend: 120 frames with the metrics overlay (drawn in Debug builds), with
# the validation layers when the tests found them (tests/graphics/vulkan); any validation error fails the run.
if(GFX_BUILD_TESTS)
    set(_gfx_example_arguments --frames 120 --metrics)
    set(_gfx_example_environment "")
    if(GFX_VULKAN_VALIDATION_LAYER_DIRECTORY)
        list(APPEND _gfx_example_arguments --validation)
        set(_gfx_example_environment "VK_ADD_LAYER_PATH=${GFX_VULKAN_VALIDATION_LAYER_DIRECTORY}")
    endif()
    foreach(_gfx_backend IN ITEMS SDL3 GLFW)
        if(GFX_PLATFORM_BACKEND_${_gfx_backend})
            string(TOLOWER ${_gfx_backend} _gfx_backend_name)
            add_test(NAME WindowedExample.${_gfx_backend}
                     COMMAND gfx_windowed_example --backend ${_gfx_backend_name} ${_gfx_example_arguments})
            set_tests_properties(WindowedExample.${_gfx_backend} PROPERTIES
                ENVIRONMENT "${_gfx_example_environment}"
                LABELS example)
        endif()
    endforeach()
endif()
