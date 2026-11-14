# The physics sandbox and the physics comparison:
#   gfx_physics_sandbox [--backend sdl3|glfw] [--physics box3d|reference|gpu_xpbd] [--preset <name>] [--views <list>]
#                       [--device <index>] [--threads <count>] [--frames <count>] [--script | --tour] [--validation]
#                       [--capture <file.ppm>]
#   gfx_physics_comparison [--backend sdl3|glfw] [--scene pyramid|pile|wrecking|chains|cloth] [--size <factor>]
#                          [--left <physics backend>] [--right <physics backend>] [--real-time] [--device <index>]
#                          [--threads <count>] [--frames <count>] [--validation]

# The modules both programs use (the renderer and its shaders, the scenes, the sandbox's presets, the camera rig and the
# interaction), then each program's own: the sandbox's simulation controls, the comparison's large scenes. Each program
# compiles its modules itself: a static library of modules shared by the two made CMake give the programs a mix of the
# libraries' BMIs, compiled with and without the programs' flags ("import 'std' has CRC mismatch"), as for the tests'
# shared support modules (they too are compiled into each test executable).
set(_gfx_sandbox_shared_sources renderer.cpp scene.cpp presets.cpp interaction.cpp)
set(_gfx_sandbox_shared_modules renderer.cppm scene.cppm presets.cppm interaction.cppm)
set(_gfx_sources_gfx_physics_sandbox physics_sandbox.cpp controls.cpp)
set(_gfx_modules_gfx_physics_sandbox controls.cppm)
set(_gfx_sources_gfx_physics_comparison physics_comparison.cpp large_scenes.cpp)
set(_gfx_modules_gfx_physics_comparison large_scenes.cppm)

include(GfxDependencies)
include(GfxUiAtlas)
gfx_require_ui_test_fonts()

foreach(_gfx_program IN ITEMS gfx_physics_sandbox gfx_physics_comparison)
    add_executable(${_gfx_program})
    target_sources(${_gfx_program}
        PRIVATE
            ${_gfx_sandbox_shared_sources}
            ${_gfx_sources_${_gfx_program}}
        PRIVATE
            FILE_SET CXX_MODULES
            FILES
                ${_gfx_sandbox_shared_modules}
                ${_gfx_modules_${_gfx_program}})
    target_link_libraries(${_gfx_program} PRIVATE
        gfx::platform_graphics gfx::ui gfx::ui_platform gfx::physics gfx::camera gfx::project_options)

    # The GPU backend's worlds on the program's own device (its direct entry, module gfx.physics.gpu_xpbd).
    if(GFX_PHYSICS_BACKEND_GPU_XPBD)
        target_link_libraries(${_gfx_program} PRIVATE gfx::physics_gpu_xpbd)
        target_compile_definitions(${_gfx_program} PRIVATE GFX_SANDBOX_GPU_XPBD=1)
    else()
        target_compile_definitions(${_gfx_program} PRIVATE GFX_SANDBOX_GPU_XPBD=0)
    endif()

    # The renderer's shaders, compiled at build time into the module physics_sandbox.shaders.
    gfx_add_shaders(${_gfx_program}
        MODULE physics_sandbox.shaders
        NAMESPACE physics_sandbox::shaders
        SHADERS shaders/mesh.vert shaders/mesh.frag shaders/line.vert shaders/line.frag)

    # The UI's font: an atlas built here from a fetched OFL font (Noto Sans), embedded into the module physics_sandbox.font.
    gfx_add_ui_atlas(${_gfx_program}
        NAME sandbox_font
        FONT ${GFX_UI_TEST_FONT_DIRECTORY}/NotoSans.ttf
        CHARACTERS BASIC_LATIN LATIN_1_SUPPLEMENT
        KIND MTSDF SIZE 32 RANGE 4
        EMBED MODULE physics_sandbox.font NAMESPACE physics_sandbox)

    gfx_deploy_runtime_dependencies(${_gfx_program})
endforeach()

# The scripted run on each platform backend, one with each physics backend (SDL3 with Box3D, GLFW with the reference
# backend; the GPU backend on the first platform backend): a grab and a drag, a shot, a spawn, a removal and a flight, then a pause, a single step, slow motion, gravity,
# the solver, the broad phase and the integrator, every debug view, their outcomes checked; and the tour on the first
# platform backend: every preset on every physics backend, switched at run time, each scene checked. With the validation
# layers when the tests found them (tests/graphics/vulkan); a failed check or a validation error fails the run.
if(GFX_BUILD_TESTS)
    set(_gfx_sandbox_arguments --script)
    set(_gfx_sandbox_validation "")
    set(_gfx_sandbox_environment "")
    if(GFX_VULKAN_VALIDATION_LAYER_DIRECTORY)
        set(_gfx_sandbox_validation --validation)
        set(_gfx_sandbox_environment "VK_ADD_LAYER_PATH=${GFX_VULKAN_VALIDATION_LAYER_DIRECTORY}")
    endif()
    list(APPEND _gfx_sandbox_arguments ${_gfx_sandbox_validation})
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
            gfx_use_gpu_in_tests(PhysicsSandbox.${_gfx_backend})
            if(NOT TEST PhysicsSandbox.Tour)
                add_test(NAME PhysicsSandbox.Tour
                         COMMAND gfx_physics_sandbox --backend ${_gfx_backend_name} --tour ${_gfx_sandbox_validation})
                # 9 presets × every physics backend × 90 frames, paced by the display (FIFO): 2430 frames with three
                # backends, at least 49 s at 50 Hz, 232 s alone on this laptop in its silent power mode.
                set_tests_properties(PhysicsSandbox.Tour PROPERTIES
                    ENVIRONMENT "${_gfx_sandbox_environment}"
                    LABELS example
                    TIMEOUT 900)
                gfx_use_gpu_in_tests(PhysicsSandbox.Tour)
            endif()
            if(GFX_PHYSICS_BACKEND_GPU_XPBD AND NOT TEST PhysicsSandbox.GPU_XPBD)
                add_test(NAME PhysicsSandbox.GPU_XPBD
                         COMMAND gfx_physics_sandbox --backend ${_gfx_backend_name} --physics gpu_xpbd ${_gfx_sandbox_arguments})
                # The GPU backend under the validation layers: 60–80 ms a frame in Debug (the layers check each of a step's
                # hundreds of commands), 51 s for the script; twice its presents fell to one a second midway (the window's
                # presentation throttled), past 300 s.
                set_tests_properties(PhysicsSandbox.GPU_XPBD PROPERTIES
                    ENVIRONMENT "${_gfx_sandbox_environment}"
                    LABELS example
                    TIMEOUT 900)
                gfx_use_gpu_in_tests(PhysicsSandbox.GPU_XPBD)
            endif()
        endif()
    endforeach()

    # The comparison on a small pile (size 0.25: 500 bodies), 120 frames in lockstep on the first platform backend: Box3D
    # against the GPU backend (the reference backend for either not built), both stepped, no validation error.
    set(_gfx_comparison_left reference)
    if(GFX_PHYSICS_BACKEND_BOX3D)
        set(_gfx_comparison_left box3d)
    endif()
    set(_gfx_comparison_right reference)
    if(GFX_PHYSICS_BACKEND_GPU_XPBD)
        set(_gfx_comparison_right gpu_xpbd)
    endif()
    foreach(_gfx_backend IN ITEMS SDL3 GLFW)
        if(GFX_PLATFORM_BACKEND_${_gfx_backend} AND NOT TEST PhysicsComparison.Pile)
            string(TOLOWER ${_gfx_backend} _gfx_backend_name)
            add_test(NAME PhysicsComparison.Pile
                     COMMAND gfx_physics_comparison --backend ${_gfx_backend_name} --scene pile --size 0.25 --left ${_gfx_comparison_left}
                             --right ${_gfx_comparison_right} --frames 120 ${_gfx_sandbox_validation})
            set_tests_properties(PhysicsComparison.Pile PROPERTIES
                ENVIRONMENT "${_gfx_sandbox_environment}"
                LABELS example
                TIMEOUT 900)
            gfx_use_gpu_in_tests(PhysicsComparison.Pile)
        endif()
    endforeach()
endif()
