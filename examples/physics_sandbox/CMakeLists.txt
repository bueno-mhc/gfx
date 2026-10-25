# The physics sandbox: gfx_physics_sandbox [--backend sdl3|glfw] [--physics box3d|reference] [--device <index>]
#                                          [--threads <count>] [--frames <count>] [--validation] [--capture <file.ppm>]
add_executable(gfx_physics_sandbox)
target_sources(gfx_physics_sandbox
    PRIVATE
        physics_sandbox.cpp
        renderer.cpp
        scene.cpp
    PRIVATE
        FILE_SET CXX_MODULES
        FILES
            renderer.cppm
            scene.cppm)
target_link_libraries(gfx_physics_sandbox PRIVATE
    gfx::platform_graphics gfx::ui gfx::ui_platform gfx::physics gfx::camera gfx::project_options)
gfx_deploy_runtime_dependencies(gfx_physics_sandbox)

# Its shaders, compiled at build time into the module physics_sandbox.shaders.
gfx_add_shaders(gfx_physics_sandbox
    MODULE physics_sandbox.shaders
    NAMESPACE physics_sandbox::shaders
    SHADERS shaders/mesh.vert shaders/mesh.frag shaders/line.vert shaders/line.frag)

# The UI's font: an atlas built here from a fetched OFL font (Noto Sans), embedded into the module physics_sandbox.font.
include(GfxDependencies)
include(GfxUiAtlas)
gfx_require_ui_test_fonts()
gfx_add_ui_atlas(gfx_physics_sandbox
    NAME sandbox_font
    FONT ${GFX_UI_TEST_FONT_DIRECTORY}/NotoSans.ttf
    CHARACTERS BASIC_LATIN LATIN_1_SUPPLEMENT
    KIND MTSDF SIZE 32 RANGE 4
    EMBED MODULE physics_sandbox.font NAMESPACE physics_sandbox)

# One automated run per platform backend: 240 frames of one physics step each, with the validation layers when the tests
# found them (tests/graphics/vulkan); a validation error fails the run.
if(GFX_BUILD_TESTS)
    set(_gfx_sandbox_arguments --frames 240)
    set(_gfx_sandbox_environment "")
    if(GFX_VULKAN_VALIDATION_LAYER_DIRECTORY)
        list(APPEND _gfx_sandbox_arguments --validation)
        set(_gfx_sandbox_environment "VK_ADD_LAYER_PATH=${GFX_VULKAN_VALIDATION_LAYER_DIRECTORY}")
    endif()
    foreach(_gfx_backend IN ITEMS SDL3 GLFW)
        if(GFX_PLATFORM_BACKEND_${_gfx_backend})
            string(TOLOWER ${_gfx_backend} _gfx_backend_name)
            add_test(NAME PhysicsSandbox.${_gfx_backend}
                     COMMAND gfx_physics_sandbox --backend ${_gfx_backend_name} ${_gfx_sandbox_arguments})
            set_tests_properties(PhysicsSandbox.${_gfx_backend} PROPERTIES
                ENVIRONMENT "${_gfx_sandbox_environment}"
                LABELS example)
        endif()
    endforeach()
endif()
