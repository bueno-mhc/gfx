# The physics sandbox: gfx_physics_sandbox [--backend sdl3|glfw] [--physics box3d|reference] [--device <index>]
#                                          [--threads <count>] [--frames <count>] [--script] [--validation] [--capture <file.ppm>]
add_executable(gfx_physics_sandbox)
target_sources(gfx_physics_sandbox
    PRIVATE
        physics_sandbox.cpp
        renderer.cpp
        scene.cpp
        interaction.cpp
    PRIVATE
        FILE_SET CXX_MODULES
        FILES
            renderer.cppm
            scene.cppm
            interaction.cppm)
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

# The scripted run on each platform backend, one with each physics backend (SDL3 with Box3D, GLFW with the reference
# backend): a grab and a drag, a shot, a spawn, a removal and a flight, their outcomes checked, with the validation layers
# when the tests found them (tests/graphics/vulkan); a failed check or a validation error fails the run.
if(GFX_BUILD_TESTS)
    set(_gfx_sandbox_arguments --script)
    set(_gfx_sandbox_environment "")
    if(GFX_VULKAN_VALIDATION_LAYER_DIRECTORY)
        list(APPEND _gfx_sandbox_arguments --validation)
        set(_gfx_sandbox_environment "VK_ADD_LAYER_PATH=${GFX_VULKAN_VALIDATION_LAYER_DIRECTORY}")
    endif()
    set(_gfx_sandbox_physics_SDL3 reference)
    if(GFX_PHYSICS_BACKEND_BOX3D)
        set(_gfx_sandbox_physics_SDL3 box3d)
    endif()
    set(_gfx_sandbox_physics_GLFW reference)
    foreach(_gfx_backend IN ITEMS SDL3 GLFW)
        if(GFX_PLATFORM_BACKEND_${_gfx_backend})
            string(TOLOWER ${_gfx_backend} _gfx_backend_name)
            add_test(NAME PhysicsSandbox.${_gfx_backend}
                     COMMAND gfx_physics_sandbox --backend ${_gfx_backend_name} --physics ${_gfx_sandbox_physics_${_gfx_backend}}
                             ${_gfx_sandbox_arguments})
            set_tests_properties(PhysicsSandbox.${_gfx_backend} PROPERTIES
                ENVIRONMENT "${_gfx_sandbox_environment}"
                LABELS example)
        endif()
    endforeach()
endif()
