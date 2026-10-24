/// @file convex_shape.cppm
/// @brief ConvexShape: what GJK, EPA and every support-function algorithm need from a convex shape.
export module gfx.physics.primitives:convex_shape;

import std;
import gfx.core.math;
import :aabb;

export namespace gfx::physics
{

/// @brief A convex shape described by its support function: `support(direction)` is the point of its **core**
///        farthest along the direction, `margin()` the radius that rounds the core into the shape (a sphere is a
///        point with a margin, a capsule a segment with one; boxes and hulls have none), `bounds()` its bounding box
///        with the margin included. GJK works on the cores and adds the margins, which keeps shallow contacts exact.
template<class Shape>
concept ConvexShape = requires(const Shape& shape, Vector3 direction) {
    { shape.support(direction) } -> std::same_as<Vector3>;
    { shape.margin() } -> std::same_as<float>;
    { shape.bounds() } -> std::same_as<Aabb>;
};

/// @brief The point of @p shape (its margin included) farthest along @p direction (which must not be zero).
template<ConvexShape Shape>
[[nodiscard]] constexpr auto
support_point(const Shape& shape,
              Vector3 direction) noexcept -> Vector3
{
    return shape.support(direction) + normalized(direction) * shape.margin();
}

} // namespace gfx::physics
