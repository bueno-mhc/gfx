# ==============================================================================
# The physics and camera libraries (docs/physics-design.md), decoupled from graphics, platform and UI: they link
# gfx::core only, which the build checks (gfx_require_decoupled) along with the modules their sources import.
#   gfx::physics_common  enumerations, handles, errors, geometric primitives (`import gfx.physics.primitives;`)
#   gfx::camera          cameras and camera controllers (`import gfx.camera;`), on gfx::physics_common only
#   gfx::physics         collision shapes and the narrow phase (`import gfx.physics.shapes;`, `gfx.physics.narrow_phase`),
#                        spatial structures and the broad phase (`gfx.physics.spatial`), task schedulers (`gfx.physics.tasks`);
#                        later the world interface, spatial structures, dynamics and the backends
# ==============================================================================
include(GfxDependencies)
include(GfxDecoupling)
gfx_require_simde()

set(GFX_PHYSICS_FORBIDDEN_DEPENDENCIES gfx gfx_platform gfx_platform_graphics gfx_ui gfx_ui_platform gfx_ui_atlas_tools
    CACHE INTERNAL "Targets the physics and camera libraries must not depend on")

add_library(gfx_physics_common)
add_library(gfx::physics_common ALIAS gfx_physics_common)
target_sources(gfx_physics_common
    PUBLIC
        FILE_SET public_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            enums.cppm
            handles.cppm
            errors.cppm
            primitives/primitives.cppm
            primitives/plane.cppm
            primitives/aabb.cppm
            primitives/sphere.cppm
            primitives/obb.cppm
            primitives/ray.cppm
            primitives/segment.cppm
            primitives/convex_hull.cppm
            primitives/frustum.cppm
            primitives/convex_shape.cppm
            primitives/intersection.cppm
            primitives/batch.cppm
    PRIVATE
        errors.cpp
        primitives/convex_hull.cpp
        primitives/primitive_kernels.cpp)   # plain translation unit: SIMDe's headers cannot share one with import std

target_compile_features(gfx_physics_common PUBLIC cxx_std_23)
target_link_libraries(gfx_physics_common
    PUBLIC gfx::core
    PRIVATE gfx::project_options gfx::simde)
gfx_require_decoupled(gfx_physics_common FORBIDDEN ${GFX_PHYSICS_FORBIDDEN_DEPENDENCIES})

# ------------------------------------------------------------------------------ camera
# The interface and the factory are public modules; the standard implementation is a module of a private file set,
# imported only by the factory's implementation unit. The camera never depends on the physics interface.
add_library(gfx_camera)
add_library(gfx::camera ALIAS gfx_camera)
target_sources(gfx_camera
    PUBLIC
        FILE_SET public_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            camera/camera.cppm
            camera/camera_factory.cppm
            camera/interface/interface.cppm
            camera/interface/projection.cppm
            camera/interface/camera_object.cppm
            camera/interface/controller.cppm
    PRIVATE
        FILE_SET backend_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            camera/backends/standard/standard.cppm
    PRIVATE
        camera/interface/camera.cpp
        camera/interface/controller.cpp
        camera/backends/standard/standard_camera.cpp
        camera/backends/standard/standard_controllers.cpp
        camera/camera_factory.cpp)

target_compile_features(gfx_camera PUBLIC cxx_std_23)
target_link_libraries(gfx_camera
    PUBLIC gfx::physics_common
    PRIVATE gfx::project_options)
gfx_require_decoupled(gfx_camera FORBIDDEN ${GFX_PHYSICS_FORBIDDEN_DEPENDENCIES} gfx_physics)

# ------------------------------------------------------------------------------ physics
add_library(gfx_physics)
add_library(gfx::physics ALIAS gfx_physics)
target_sources(gfx_physics
    PUBLIC
        FILE_SET public_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            shapes/shapes.cppm
            shapes/convex.cppm
            shapes/hull.cppm
            shapes/mass.cppm
            shapes/static_bvh.cppm
            shapes/mesh.cppm
            shapes/shape.cppm
            narrow_phase/narrow_phase.cppm
            narrow_phase/contact.cppm
            narrow_phase/gjk.cppm
            narrow_phase/epa.cppm
            narrow_phase/clipping.cppm
            narrow_phase/analytic.cppm
            narrow_phase/convex.cppm
            narrow_phase/collide.cppm
            narrow_phase/manifold_cache.cppm
            tasks/tasks.cppm
            spatial/spatial.cppm
            spatial/structure.cppm
            spatial/bvh.cppm
            spatial/loose_tree.cppm
            spatial/bsp.cppm
            spatial/uniform_grid.cppm
            spatial/sweep_and_prune.cppm
            spatial/broad_phase.cppm
    PRIVATE
        tasks/tasks.cpp
        spatial/spatial.cpp
        spatial/bvh.cpp
        spatial/bsp.cpp
        narrow_phase/collide.cpp
        narrow_phase/manifold_cache.cpp
        shapes/hull.cpp
        shapes/static_bvh.cpp
        shapes/mesh.cpp
        shapes/shape.cpp
        shapes/shape_kernels.cpp)   # plain translation unit: SIMDe's headers cannot share one with import std

target_compile_features(gfx_physics PUBLIC cxx_std_23)
target_link_libraries(gfx_physics
    PUBLIC gfx::physics_common
    PRIVATE gfx::project_options gfx::simde)
gfx_require_decoupled(gfx_physics FORBIDDEN ${GFX_PHYSICS_FORBIDDEN_DEPENDENCIES} gfx_camera)
