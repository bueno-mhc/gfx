/// @file shapes.cppm
/// @brief `import gfx.physics.shapes;`: the collision shapes — convex shapes with closed forms and supporting faces
///        (:convex), hulls built by quickhull at compile or run time (:hull), mass properties (:mass), the static
///        bounding volume hierarchy (:static_bvh), triangle meshes and height fields with active edges (:mesh), and Shape
///        with compounds (:shape). Re-exports the primitives.
export module gfx.physics.shapes;

export import gfx.core.math;
export import gfx.physics.enums;
export import gfx.physics.errors;
export import gfx.physics.primitives;
export import :convex;
export import :hull;
export import :mass;
export import :static_bvh;
export import :mesh;
export import :shape;
