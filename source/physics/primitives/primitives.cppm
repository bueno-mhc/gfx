/// @file primitives.cppm
/// @brief gfx.physics.primitives: the abstract geometric primitives the physics and camera libraries are built on —
///        Plane, Aabb, Sphere, Obb, Ray, Segment, ConvexHullView / FixedConvexHull / ConvexHull, Frustum — with
///        constexpr constructors and tests (classification against planes and frustums, overlaps, ray distances,
///        closest points), the ConvexShape concept GJK and EPA work on, and SIMD batch tests (frustum culling of many
///        boxes or spheres). Spatial structures and narrow phases are written against these, never against a backend's
///        geometry. Re-exports gfx.core.math (vectors, quaternions, matrices) and gfx.physics.enums.
export module gfx.physics.primitives;

export import gfx.core.math;
export import gfx.physics.enums;
export import :plane;
export import :aabb;
export import :sphere;
export import :obb;
export import :ray;
export import :segment;
export import :convex_hull;
export import :frustum;
export import :convex_shape;
export import :intersection;
export import :batch;
