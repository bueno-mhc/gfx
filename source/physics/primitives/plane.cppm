/// @file plane.cppm
/// @brief Plane: the points p with `dot(normal, p) == distance`; the normal points to its front.
export module gfx.physics.primitives:plane;

import std;
import gfx.core.math;

export namespace gfx::physics
{

/// @brief A plane: the points p with `dot(normal, p) == distance` (normal of unit length, pointing to the front).
///        The default is the plane y = 0 facing +Y.
struct Plane
{
    Vector3 normal{.y = 1}; ///< Unit normal, toward the front.
    float distance = 0;     ///< Signed distance of the plane from the origin, along the normal.

    /// @brief Equal normals and distances (exactly).
    [[nodiscard]] constexpr auto operator==(const Plane&) const noexcept -> bool = default;

    /// @brief The plane through @p point with the unit normal @p unit_normal.
    [[nodiscard]] static constexpr auto from_point_normal(Vector3 point,
                                                          Vector3 unit_normal) noexcept -> Plane
    {
        return {.normal = unit_normal, .distance = dot(unit_normal, point)};
    }

    /// @brief The plane through three points, its front the side they are counter-clockwise from.
    [[nodiscard]] static constexpr auto from_points(Vector3 first,
                                                    Vector3 second,
                                                    Vector3 third) noexcept -> Plane
    {
        return from_point_normal(first, normalized(cross(second - first, third - first)));
    }

    /// @brief The plane `a x + b y + c z + d = 0` of @p coefficients (a, b, c, d), its front where the expression is
    ///        positive. Coefficients with no direction (a = b = c = 0) give a plane everything is in front of.
    [[nodiscard]] static constexpr auto from_coefficients(Vector4 coefficients) noexcept -> Plane
    {
        const float size = length(to_vector3(coefficients));
        if (size <= 0)
        {
            return {.normal = {}, .distance = std::numeric_limits<float>::lowest()};
        }
        return {.normal = to_vector3(coefficients) / size, .distance = -coefficients.w / size};
    }

    /// @brief The signed distance of @p point: positive in front.
    [[nodiscard]] constexpr auto signed_distance(Vector3 point) const noexcept -> float
    {
        return dot(normal, point) - distance;
    }

    /// @brief The point of the plane closest to @p point.
    [[nodiscard]] constexpr auto project(Vector3 point) const noexcept -> Vector3
    {
        return point - normal * signed_distance(point);
    }

    /// @brief The same plane facing the other way.
    [[nodiscard]] constexpr auto flipped() const noexcept -> Plane
    {
        return {.normal = -normal, .distance = -distance};
    }
};

} // namespace gfx::physics
