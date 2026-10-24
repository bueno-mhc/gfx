# Tests of the physics and camera libraries. They link gfx::physics_common only: the libraries work without graphics,
# platform or UI.
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

# The physics and camera sources import only core, physics and camera modules (the link side is checked when CMake
# configures: gfx_require_decoupled); the check itself finds a forbidden import in a fixture.
set(_gfx_import_check ${PROJECT_SOURCE_DIR}/cmake/scripts/GfxCheckModuleImports.cmake)
add_test(NAME PhysicsDecoupling.SourcesImportOnlyCoreAndPhysics
         COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${PROJECT_SOURCE_DIR}/source/physics "-DALLOWED=gfx.core;gfx.physics;gfx.camera"
                 -P ${_gfx_import_check})
add_test(NAME PhysicsDecoupling.ForbiddenImportsAreFound
         COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/decoupling_fixture "-DALLOWED=gfx.core;gfx.physics;gfx.camera"
                 -P ${_gfx_import_check})
set_tests_properties(PhysicsDecoupling.ForbiddenImportsAreFound PROPERTIES
    PASS_REGULAR_EXPRESSION "forbidden_import\\.cppm imports gfx\\.platform\n.*forbidden_import\\.cppm imports gfx\\.camerawork")
