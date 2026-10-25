/// @file queries.cppm
/// @brief Queries against one shape at its pose (what a world's query engine runs on each candidate collider): ray
///        casts, casts of a convex shape along a translation, overlap tests and the closest point to a point.
///
/// Convex shapes are answered by conservative advancement over GJK distances (van den Bergen's ray cast: the shape lies
/// beyond the plane through its closest point, so the ray, or the moving shape, cannot touch it before reaching that
/// plane), spheres and triangles exactly; triangle meshes through their hierarchy, height fields by cells along the ray,
/// compounds through their children. One-sided meshes and height fields are hit from the front only. Every function
/// is noexcept and allocates nothing: any number of threads query at once.
export module gfx.physics.narrow_phase:queries;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.primitives;
import gfx.physics.shapes;

export namespace gfx::physics
{

/// @brief Where a ray hit a shape.
struct ShapeRayHit
{
    float distance = 0.0F;                  ///< Along the ray (0: it starts inside a convex shape).
    Vector3 normal{};                       ///< The surface's outward normal, world space (against the ray when it starts inside).
    std::uint32_t sub_shape = NO_SUB_SHAPE; ///< The triangle of a mesh or height field, or the child of a compound.
};

/// @brief Where a moving convex shape first touched another.
struct ShapeCastResult
{
    float fraction = 0.0F;                  ///< Of the translation, in [0, 1] (0: they overlap at the start).
    Vector3 point{};                        ///< The contact on the target, world space.
    Vector3 normal{.y = 1.0F};              ///< From the moving shape to the target.
    std::uint32_t sub_shape = NO_SUB_SHAPE; ///< The target's triangle or child.
};

/// @brief The point of a shape closest to a point.
struct ShapeClosestPoint
{
    float distance = 0.0F;                  ///< From the point (negative inside a convex shape).
    Vector3 point{};                        ///< On the surface, world space.
    std::uint32_t sub_shape = NO_SUB_SHAPE; ///< The triangle or child it is on.
};

/// @brief The first hit of @p ray on @p shape at @p pose within @p maximum.
[[nodiscard]] auto ray_cast_shape(const Shape& shape,
                                  const Transform& pose,
                                  const Ray& ray,
                                  float maximum) noexcept -> std::optional<ShapeRayHit>;

/// @brief Where the convex shape @p moving, starting at @p pose and moved by @p translation, first touches @p target
///        at @p target_pose; nothing if it does not, or if @p moving is not convex.
[[nodiscard]] auto cast_shape(const Shape& moving,
                              const Transform& pose,
                              Vector3 translation,
                              const Shape& target,
                              const Transform& target_pose) noexcept -> std::optional<ShapeCastResult>;

/// @brief Whether the convex shape @p first at @p pose touches or overlaps @p second at @p second_pose (false if @p first
///        is not convex). Mesh and height-field triangles count from both sides.
[[nodiscard]] auto shapes_overlap(const Shape& first,
                                  const Transform& pose,
                                  const Shape& second,
                                  const Transform& second_pose) noexcept -> bool;

/// @brief The point of @p shape at @p pose closest to @p point, if within @p maximum (inside a convex shape: its
///        nearest surface point, at a negative distance).
[[nodiscard]] auto closest_point_on_shape(const Shape& shape,
                                          const Transform& pose,
                                          Vector3 point,
                                          float maximum) noexcept -> std::optional<ShapeClosestPoint>;

} // namespace gfx::physics
