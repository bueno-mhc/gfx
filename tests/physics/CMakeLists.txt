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

# Shapes and the narrow phase: hulls, mass properties, meshes, GJK and EPA, manifolds, collision of any two shapes.
gfx_add_test(gfx_physics_tests
    SOURCES
        shape_test.cpp
        gjk_epa_test.cpp
        contact_test.cpp
        collide_test.cpp
    MODULES physics_test_support.cppm
    LIBRARIES gfx::physics)

# The physics and camera sources import only core, physics and camera modules, and the camera's only the physics
# foundations (the link side is checked when CMake configures: gfx_require_decoupled); the check itself finds a forbidden
# import in a fixture.
set(_gfx_import_check ${PROJECT_SOURCE_DIR}/cmake/scripts/GfxCheckModuleImports.cmake)
add_test(NAME PhysicsDecoupling.SourcesImportOnlyCoreAndPhysics
         COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${PROJECT_SOURCE_DIR}/source/physics "-DALLOWED=gfx.core;gfx.physics;gfx.camera"
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
