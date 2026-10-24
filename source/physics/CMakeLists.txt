# ==============================================================================
# The physics and camera libraries (docs/physics-design.md), decoupled from graphics, platform and UI: they link
# gfx::core only, which the build checks (gfx_require_decoupled) along with the modules their sources import.
#   gfx::physics_common  enumerations, handles, errors, geometric primitives (`import gfx.physics.primitives;`)
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
