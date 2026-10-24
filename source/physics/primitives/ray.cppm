/// @file ray.cppm
/// @brief Ray: an origin and a unit direction (the half line of the points at distances 0 and more).
export module gfx.physics.primitives:ray;

import std;
import gfx.core.math;

export namespace gfx::physics
{

/// @brief A half line: `origin + direction * t` for t ≥ 0; the direction has unit length, so t is a distance.
struct Ray
{
    Vector3 origin{};             ///< Where it starts.
    Vector3 direction{.z = -1};   ///< Where it goes (unit length).

    /// @brief Equal origins and directions (exactly).
    [[nodiscard]] constexpr auto operator==(const Ray&) const noexcept -> bool = default;

    /// @brief The ray from @p start through @p through (distinct points).
    [[nodiscard]] static constexpr auto from_points(Vector3 start,
                                                    Vector3 through) noexcept -> Ray
    {
        return {.origin = start, .direction = normalized(through - start)};
    }

    /// @brief The point at @p distance along the ray.
    [[nodiscard]] constexpr auto at(float distance) const noexcept -> Vector3
    {
        return origin + direction * distance;
    }
};

} // namespace gfx::physics
