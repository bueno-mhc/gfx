# ==============================================================================
# The physics and camera libraries (docs/physics-design.md), decoupled from graphics, platform and UI: they link
# gfx::core only, which the build checks (gfx_require_decoupled) along with the modules their sources import.
#   gfx::physics_common  enumerations, handles, errors, geometric primitives (`import gfx.physics.primitives;`)
#   gfx::camera          cameras and camera controllers (`import gfx.camera;`), on gfx::physics_common only
#   gfx::physics_components  collision shapes and the narrow phase (`import gfx.physics.shapes;`, `gfx.physics.narrow_phase`),
#                        spatial structures and the broad phase (`gfx.physics.spatial`), task schedulers (`gfx.physics.tasks`),
#                        the world interface (`gfx.physics.interface`), integrators and XPBD soft bodies (`gfx.physics.dynamics`)
#                        and the framework of backends (`gfx.physics.world`)
#   gfx::physics         the above, the factory and its backends (`import gfx.physics;`)
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
# gfx::physics_components holds everything a backend is composed of: shapes, the narrow phase, task schedulers, spatial
# structures and the broad phase, the interface (gfx.physics.interface), the dynamics components (gfx.physics.dynamics)
# and the framework backends are built on (gfx.physics.world). gfx::physics adds the factory and the umbrella module, with the backends in a private file set.
# A custom backend links gfx::physics_components (no cycle) and is named by GFX_PHYSICS_CUSTOM_BACKEND_TARGET.
set(GFX_PHYSICS_CUSTOM_BACKEND_TARGET "" CACHE STRING
    "A target whose public module gfx.physics.backends.custom is the factory's PhysicsBackendKind::CUSTOM (empty: none)")

add_library(gfx_physics_components)
add_library(gfx::physics_components ALIAS gfx_physics_components)
target_sources(gfx_physics_components
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
            narrow_phase/queries.cppm
            tasks/tasks.cppm
            spatial/spatial.cppm
            spatial/structure.cppm
            spatial/bvh.cppm
            spatial/loose_tree.cppm
            spatial/bsp.cppm
            spatial/uniform_grid.cppm
            spatial/sweep_and_prune.cppm
            spatial/broad_phase.cppm
            spatial/spatial_structure.cppm
            interface/interface.cppm
            interface/descriptions.cppm
            interface/object.cppm
            interface/objects.cppm
            interface/pipeline.cppm
            interface/world.cppm
            dynamics/dynamics.cppm
            dynamics/integrators.cppm
            dynamics/presets.cppm
            dynamics/soft_body_solver.cppm
            dynamics/sequential_impulses.cppm
            dynamics/xpbd_math.cppm
            world/world.cppm
            world/edits.cppm
            world/snapshot.cppm
            world/stepped_world.cppm
            world/state.cppm
            debug_draw/debug_draw.cppm
    PRIVATE
        tasks/tasks.cpp
        spatial/spatial.cpp
        spatial/bvh.cpp
        spatial/bsp.cpp
        narrow_phase/collide.cpp
        narrow_phase/manifold_cache.cpp
        narrow_phase/queries.cpp
        shapes/hull.cpp
        shapes/static_bvh.cpp
        shapes/mesh.cpp
        shapes/shape.cpp
        shapes/shape_kernels.cpp   # plain translation unit: SIMDe's headers cannot share one with import std
        interface/interface.cpp
        dynamics/integrators.cpp
        dynamics/presets.cpp
        dynamics/soft_body_model.cpp
        dynamics/soft_body_solver.cpp
        dynamics/joint_rows.cpp
        dynamics/sequential_impulses.cpp
        world/stepped_world.cpp
        world/snapshot.cpp
        world/validation.cpp
        world/world_objects.cpp
        world/world_queries.cpp
        debug_draw/debug_draw.cpp)

target_compile_features(gfx_physics_components PUBLIC cxx_std_23)
target_link_libraries(gfx_physics_components
    PUBLIC gfx::physics_common
    PRIVATE gfx::project_options gfx::simde)
gfx_require_decoupled(gfx_physics_components FORBIDDEN ${GFX_PHYSICS_FORBIDDEN_DEPENDENCIES} gfx_camera)

# The GPU backend (docs/gpu-xpbd-design.md): XPBD on the graphics library's compute, a target of its own because it
# depends on graphics, which the rest of the physics library must not. With GFX_PHYSICS_BACKEND_GPU_XPBD, gfx::physics
# links it for the factory's PhysicsBackendKind::GPU_XPBD, and applications running it on their own device link
# gfx::physics_gpu_xpbd (`import gfx.physics.gpu_xpbd;`).
if(GFX_PHYSICS_BACKEND_GPU_XPBD)
    add_library(gfx_physics_gpu_xpbd)
    add_library(gfx::physics_gpu_xpbd ALIAS gfx_physics_gpu_xpbd)
    target_sources(gfx_physics_gpu_xpbd
        PUBLIC
            FILE_SET public_modules TYPE CXX_MODULES
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
            FILES
                backends/gpu_xpbd/gpu_xpbd.cppm
                backends/gpu_xpbd/gpu_xpbd_settings.cppm
        PRIVATE
            FILE_SET backend_modules TYPE CXX_MODULES
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
            FILES
                backends/gpu_xpbd/gpu_probing.cppm
                backends/gpu_xpbd/gpu_resources.cppm
                backends/gpu_xpbd/gpu_xpbd_backend.cppm
        PRIVATE
            backends/gpu_xpbd/gpu_xpbd.cpp
            backends/gpu_xpbd/capabilities.cpp
            backends/gpu_xpbd/gpu_resources.cpp
            backends/gpu_xpbd/gpu_xpbd_world.cpp)
    target_compile_features(gfx_physics_gpu_xpbd PUBLIC cxx_std_23)
    target_link_libraries(gfx_physics_gpu_xpbd
        PUBLIC gfx::physics_components gfx::graphics
        PRIVATE gfx::project_options)
    gfx_add_shaders(gfx_physics_gpu_xpbd
        MODULE gfx.physics.gpu_xpbd.shaders
        NAMESPACE gfx::physics::gpu_xpbd::shaders
        SHADERS
            backends/gpu_xpbd/shaders/patch_bodies.comp
            backends/gpu_xpbd/shaders/xpbd_integrate.comp
            backends/gpu_xpbd/shaders/xpbd_velocity_update.comp
        INCLUDE_DIRECTORIES backends/gpu_xpbd/shaders)
    gfx_require_decoupled(gfx_physics_gpu_xpbd FORBIDDEN gfx_platform gfx_platform_graphics gfx_ui gfx_ui_platform gfx_ui_atlas_tools gfx_camera)
endif()

# The backends: the null and reference ones always; Box3D with GFX_PHYSICS_BACKEND_BOX3D (fetched and pinned).
set(_gfx_physics_backend_modules
    backends/null/null.cppm
    backends/reference/reference.cppm
    backends/reference/reference_backend.cppm)
set(_gfx_physics_backend_sources
    backends/null/null_world.cpp
    backends/reference/reference_world.cpp
    backends/reference/reference_contacts.cpp)
if(GFX_PHYSICS_BACKEND_BOX3D)
    gfx_require_box3d()
    list(APPEND _gfx_physics_backend_modules
        backends/box3d/box3d.cppm
        backends/box3d/box3d_backend.cppm)
    list(APPEND _gfx_physics_backend_sources
        backends/box3d/box3d_world.cpp
        backends/box3d/box3d_shapes.cpp
        backends/box3d/box3d_joints.cpp
        backends/box3d/box3d_queries.cpp)
endif()

add_library(gfx_physics)
add_library(gfx::physics ALIAS gfx_physics)
target_sources(gfx_physics
    PUBLIC
        FILE_SET public_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            physics.cppm
            physics_factory.cppm
    PRIVATE
        FILE_SET backend_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES ${_gfx_physics_backend_modules}
    PRIVATE
        ${_gfx_physics_backend_sources}
        physics_factory.cpp)

target_compile_features(gfx_physics PUBLIC cxx_std_23)
target_link_libraries(gfx_physics
    PUBLIC gfx::physics_components
    PRIVATE gfx::project_options)
if(GFX_PHYSICS_BACKEND_BOX3D)
    target_link_libraries(gfx_physics PRIVATE gfx::box3d)
    target_compile_definitions(gfx_physics PRIVATE GFX_WITH_PHYSICS_BOX3D_BACKEND)
    # Box3D's structs are initialized with designated initializers that leave most members zero.
    set_source_files_properties(backends/box3d/box3d_backend.cppm backends/box3d/box3d_world.cpp backends/box3d/box3d_shapes.cpp
        backends/box3d/box3d_joints.cpp backends/box3d/box3d_queries.cpp
        PROPERTIES COMPILE_OPTIONS -Wno-missing-field-initializers)
endif()
if(GFX_PHYSICS_CUSTOM_BACKEND_TARGET)
    target_link_libraries(gfx_physics PRIVATE ${GFX_PHYSICS_CUSTOM_BACKEND_TARGET})
    target_compile_definitions(gfx_physics PRIVATE GFX_WITH_PHYSICS_CUSTOM_BACKEND)
endif()
# With the GPU backend, gfx::physics depends on graphics through it (and only then).
set(_gfx_physics_forbidden ${GFX_PHYSICS_FORBIDDEN_DEPENDENCIES} gfx_camera)
if(GFX_PHYSICS_BACKEND_GPU_XPBD)
    target_link_libraries(gfx_physics PRIVATE gfx::physics_gpu_xpbd)
    target_compile_definitions(gfx_physics PRIVATE GFX_WITH_PHYSICS_GPU_XPBD_BACKEND)
    list(REMOVE_ITEM _gfx_physics_forbidden gfx)
endif()
gfx_require_decoupled(gfx_physics FORBIDDEN ${_gfx_physics_forbidden})
