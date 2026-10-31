# Tests of the physics and camera libraries. They link gfx::physics_common or gfx::camera only: the libraries work without
# graphics, platform or UI.
gfx_add_test(gfx_physics_common_tests
    SOURCES
        enumeration_test.cpp
        handle_test.cpp
        error_test.cpp
        primitive_test.cpp
        intersection_test.cpp
        batch_test.cpp
    MODULES physics_test_support.cppm
    LIBRARIES gfx::physics_common)

# The camera: projections and matrices at compile time and against hand-computed values, camera objects, controllers.
gfx_add_test(gfx_camera_tests
    SOURCES
        camera_projection_test.cpp
        camera_test.cpp
        controller_test.cpp
    MODULES physics_test_support.cppm
    LIBRARIES gfx::camera)

# Shapes and the narrow phase: hulls, mass properties, meshes, GJK and EPA, manifolds, collision of any two shapes,
# queries against one shape; spatial structures against brute force, the broad phase, the task schedulers; shapes as
# meshes and debug lines.
gfx_add_test(gfx_physics_tests
    SOURCES
        shape_test.cpp
        gjk_epa_test.cpp
        contact_test.cpp
        collide_test.cpp
        shape_query_test.cpp
        spatial_test.cpp
        broad_phase_test.cpp
        debug_draw_test.cpp
    MODULES physics_test_support.cppm
    LIBRARIES gfx::physics)

# Dynamics: the integrators against analytic motion; soft bodies on their own (presets, models, XPBD steps); the
# sequential-impulse solver and the joints' rows.
gfx_add_test(gfx_physics_dynamics_tests
    SOURCES
        integrator_test.cpp
        soft_body_test.cpp
        solver_test.cpp
    MODULES physics_test_support.cppm
    LIBRARIES gfx::physics)

# The Box3D backend: simulations with known outcomes, events, determinism, capability errors, queries; the abstraction's
# soft bodies coupled with Box3D bodies.
if(GFX_PHYSICS_BACKEND_BOX3D)
    gfx_add_test(gfx_physics_box3d_tests
        SOURCES
            box3d_test.cpp
            box3d_soft_body_test.cpp
        LIBRARIES gfx::physics)
endif()

# The reference backend: the Box3D scenarios, what only it simulates, its components replaced, its soft bodies.
gfx_add_test(gfx_physics_reference_tests
    SOURCES reference_test.cpp
    LIBRARIES gfx::physics)

# The GPU backend on every GPU of the machine with the Vulkan validation layers (found by tests/graphics/vulkan; its
# fixtures in vulkan_test.hpp), and on the graphics tests' fake device (no compute) for its capability errors.
if(GFX_PHYSICS_BACKEND_GPU_XPBD)
    gfx_add_test(gfx_physics_gpu_xpbd_tests
        SOURCES gpu_xpbd_test.cpp
        MODULES ${PROJECT_SOURCE_DIR}/tests/graphics/interface/fake_backend.cppm
        MODULE_BASE_DIRECTORY ${PROJECT_SOURCE_DIR}/tests/graphics/interface
        LIBRARIES gfx::physics gfx::physics_gpu_xpbd gfx::gfx)
    set(_gfx_physics_layer_directory "")
    if(GFX_VULKAN_VALIDATION_LAYER_DIRECTORY)
        set(_gfx_physics_layer_directory "${GFX_VULKAN_VALIDATION_LAYER_DIRECTORY}")
    endif()
    target_compile_definitions(gfx_physics_gpu_xpbd_tests PRIVATE GFX_TEST_VULKAN_LAYER_DIRECTORY="${_gfx_physics_layer_directory}")
    target_include_directories(gfx_physics_gpu_xpbd_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/graphics/vulkan)
endif()

# Worlds: the interfaces' error contract, the factory and the null backend, SteppedWorld on a recording backend (deferred
# edits, publication, destruction, asynchronous steps, the reader handshake, queries, checks).
gfx_add_test(gfx_physics_world_tests
    SOURCES
        interface_default_test.cpp
        world_test.cpp
    MODULES world_test_support.cppm
    LIBRARIES gfx::physics)

# The physics and camera sources import only core, physics and camera modules, and the camera's only the physics
# foundations (the link side is checked when CMake configures: gfx_require_decoupled); the check itself finds a forbidden
# import in a fixture.
set(_gfx_import_check ${PROJECT_SOURCE_DIR}/cmake/scripts/GfxCheckModuleImports.cmake)
add_test(NAME PhysicsDecoupling.SourcesImportOnlyCoreAndPhysics
         COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${PROJECT_SOURCE_DIR}/source/physics "-DALLOWED=gfx.core;gfx.physics;gfx.camera"
                 -DEXCLUDE=backends/gpu_xpbd -P ${_gfx_import_check})
# The GPU backend imports graphics too, and only its interface (no platform, UI or camera).
add_test(NAME PhysicsDecoupling.GpuBackendImportsOnlyPhysicsAndGraphics
         COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${PROJECT_SOURCE_DIR}/source/physics/backends/gpu_xpbd
                 "-DALLOWED=gfx.core;gfx.physics;gfx.enums;gfx.errors;gfx.handles;gfx.interface;gfx.backend_factory"
                 -P ${_gfx_import_check})
set(_gfx_camera_imports "gfx.core;gfx.physics.enums;gfx.physics.handles;gfx.physics.errors;gfx.physics.primitives;gfx.camera")
add_test(NAME PhysicsDecoupling.CameraImportsNoPhysicsInterface
         COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${PROJECT_SOURCE_DIR}/source/physics/camera "-DALLOWED=${_gfx_camera_imports}"
                 -P ${_gfx_import_check})
add_test(NAME PhysicsDecoupling.CameraImportsOfThePhysicsInterfaceAreFound
         COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/decoupling_fixture/camera "-DALLOWED=${_gfx_camera_imports}"
                 -P ${_gfx_import_check})
set_tests_properties(PhysicsDecoupling.CameraImportsOfThePhysicsInterfaceAreFound PROPERTIES
    PASS_REGULAR_EXPRESSION "forbidden_camera_import\\.cppm imports gfx\\.physics\\.interface"
    FAIL_REGULAR_EXPRESSION "imports gfx\\.(core|physics\\.primitives|camera)")
add_test(NAME PhysicsDecoupling.ForbiddenImportsAreFound
         COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/decoupling_fixture "-DALLOWED=gfx.core;gfx.physics;gfx.camera"
                 -P ${_gfx_import_check})
set_tests_properties(PhysicsDecoupling.ForbiddenImportsAreFound PROPERTIES
    PASS_REGULAR_EXPRESSION "forbidden_import\\.cppm imports gfx\\.platform\n.*forbidden_import\\.cppm imports gfx\\.camerawork")
