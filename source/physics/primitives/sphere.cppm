/// @file sphere.cppm
/// @brief Sphere: a center and a radius.
export module gfx.physics.primitives:sphere;

import std;
import gfx.core.math;
import :aabb;

export namespace gfx::physics
{

/// @brief A sphere (a ball: its inside included).
struct Sphere
{
    Vector3 center{};  ///< The center.
    float radius = 0;  ///< The radius (0: a point).

    /// @brief Equal centers and radii (exactly).
    [[nodiscard]] constexpr auto operator==(const Sphere&) const noexcept -> bool = default;

    /// @brief A sphere holding @p points: centered on their bounding box, through the farthest one (not the smallest
    ///        sphere, within a few percent of it). A point for none.
    [[nodiscard]] static constexpr auto from_points(std::span<const Vector3> points) noexcept -> Sphere
    {
        if (points.empty())
        {
            return {};
        }
        const Vector3 middle = Aabb::from_points(points).center();
        float farthest = 0;
        for (const Vector3 point : points)
        {
            farthest = std::max(farthest, distance_squared(point, middle));
        }
        return {.center = middle, .radius = square_root(farthest)};
    }

    /// @brief Whether @p point is inside or on the surface.
    [[nodiscard]] constexpr auto contains(Vector3 point) const noexcept -> bool
    {
        return distance_squared(point, center) <= radius * radius;
    }

    /// @brief The smallest sphere holding this one and @p other.
    [[nodiscard]] constexpr auto merged(const Sphere& other) const noexcept -> Sphere
    {
        // One holds the other, or the sphere spans both along the line of their centers.
        const float between = distance(center, other.center);
        if (between + other.radius <= radius)
        {
            return *this;
        }
        if (between + radius <= other.radius)
        {
            return other;
        }
        const float merged_radius = (between + radius + other.radius) * 0.5F;
        const Vector3 direction = (other.center - center) / between;
        return {.center = center + direction * (merged_radius - radius), .radius = merged_radius};
    }

    /// @brief The sphere moved by the rigid transform @p transform.
    [[nodiscard]] constexpr auto transformed(const Transform& transform) const noexcept -> Sphere
    {
        return {.center = transform_point(transform, center), .radius = radius};
    }

    /// @brief The center: the support function of the sphere's core (a point), its radius being its margin.
    [[nodiscard]] constexpr auto support(Vector3 /*direction*/) const noexcept -> Vector3
    {
        return center;
    }

    /// @brief The radius: what rounds the core point into the sphere.
    [[nodiscard]] constexpr auto margin() const noexcept -> float
    {
        return radius;
    }

    /// @brief The bounding box.
    [[nodiscard]] constexpr auto bounds() const noexcept -> Aabb
    {
        return Aabb::from_center(center, {.x = radius, .y = radius, .z = radius});
    }
};

/// @brief Whether two spheres overlap (touching counts).
[[nodiscard]] constexpr auto
intersects(const Sphere& first,
           const Sphere& second) noexcept -> bool
{
    const float reach = first.radius + second.radius;
    return distance_squared(first.center, second.center) <= reach * reach;
}

/// @brief Whether a box and a sphere overlap (touching counts).
[[nodiscard]] constexpr auto
intersects(const Aabb& box,
           const Sphere& sphere) noexcept -> bool
{
    return box.squared_distance(sphere.center) <= sphere.radius * sphere.radius;
}

/// @brief Whether a sphere and a box overlap (touching counts).
[[nodiscard]] constexpr auto
intersects(const Sphere& sphere,
           const Aabb& box) noexcept -> bool
{
    return intersects(box, sphere);
}

} // namespace gfx::physics
