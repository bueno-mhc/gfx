# The physics example: gfx_physics_example [--backend box3d|reference|gpu_xpbd] [--steps <count>] [--threads <count>]
add_executable(gfx_physics_example physics_example.cpp)
target_link_libraries(gfx_physics_example PRIVATE gfx::physics gfx::camera gfx::project_options)
gfx_deploy_runtime_dependencies(gfx_physics_example)

# One automated run per backend that simulates: four seconds of the scene on four threads; a failed check fails the run.
if(GFX_BUILD_TESTS)
    set(_gfx_physics_backends REFERENCE)
    if(GFX_PHYSICS_BACKEND_BOX3D)
        list(PREPEND _gfx_physics_backends BOX3D)
    endif()
    if(GFX_PHYSICS_BACKEND_GPU_XPBD)
        list(APPEND _gfx_physics_backends GPU_XPBD)
    endif()
    foreach(_gfx_backend IN LISTS _gfx_physics_backends)
        string(TOLOWER ${_gfx_backend} _gfx_backend_name)
        add_test(NAME PhysicsExample.${_gfx_backend}
                 COMMAND gfx_physics_example --backend ${_gfx_backend_name} --steps 240 --threads 4)
        set_tests_properties(PhysicsExample.${_gfx_backend} PROPERTIES LABELS example)
    endforeach()
    # The GPU backend makes its own device (on the most capable GPU).
    if(GFX_PHYSICS_BACKEND_GPU_XPBD)
        gfx_use_gpu_in_tests(PhysicsExample.GPU_XPBD)
    endif()
endif()
