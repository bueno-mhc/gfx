/// @file frustum.cppm
/// @brief Frustum: six planes bounding what a projection sees, normals pointing inward.
export module gfx.physics.primitives:frustum;

import std;
import gfx.core.math;
import gfx.physics.enums;
import :plane;

export namespace gfx::physics
{

/// @brief The six planes of a view volume in the order of FrustumPlane, normals pointing inward (inside is in front
///        of every plane). Planes are named for clip space: BOTTOM is y = -w, which is the top of the screen when the
///        normalized device y axis points down (Vulkan without a flipped viewport).
struct Frustum
{
    std::array<Plane, 6> planes{}; ///< LEFT, RIGHT, BOTTOM, TOP, NEAR_PLANE, FAR_PLANE.

    /// @brief Equal planes (exactly).
    [[nodiscard]] constexpr auto operator==(const Frustum&) const noexcept -> bool = default;

    /// @brief The frustum of the projective matrix @p matrix (Gribb and Hartmann): a projection gives it in view space,
    ///        a view-projection in world space. @p depth is the clip-space depth range the matrix maps to; with
    ///        @p reversed_depth (near at 1, far at 0) the NEAR_PLANE and FAR_PLANE entries are exchanged so that they
    ///        keep their meaning. Planes of an infinite far plane have no direction: everything is in front of them.
    [[nodiscard]] static constexpr auto from_matrix(const Matrix4& matrix,
                                                    DepthRange depth = DepthRange::ZERO_TO_ONE,
                                                    bool reversed_depth = false) noexcept -> Frustum
    {
        // The rows of the matrix: clip x = row 0 · p, and so on; inside is -w <= x <= w (and the depth's range).
        const auto row = [&matrix](std::size_t index) {
            return Vector4{.x = matrix.element(index, 0), .y = matrix.element(index, 1), .z = matrix.element(index, 2), .w = matrix.element(index, 3)};
        };
        const Vector4 x = row(0);
        const Vector4 y = row(1);
        const Vector4 z = row(2);
        const Vector4 w = row(3);
        const Vector4 low_depth = depth == DepthRange::ZERO_TO_ONE ? z : w + z;
        const Vector4 high_depth = w - z;
        const Plane near_plane = Plane::from_coefficients(reversed_depth ? high_depth : low_depth);
        const Plane far_plane = Plane::from_coefficients(reversed_depth ? low_depth : high_depth);
        return {.planes = {Plane::from_coefficients(w + x), Plane::from_coefficients(w - x), Plane::from_coefficients(w + y),
                           Plane::from_coefficients(w - y), near_plane, far_plane}};
    }

    /// @brief The plane @p which.
    [[nodiscard]] constexpr auto plane(FrustumPlane which) const noexcept -> const Plane&
    {
        return planes[std::to_underlying(which)];
    }

    /// @brief Whether @p point is inside or on the boundary.
    [[nodiscard]] constexpr auto contains(Vector3 point) const noexcept -> bool
    {
        return std::ranges::all_of(planes, [point](const Plane& bound) { return bound.signed_distance(point) >= 0; });
    }
};

} // namespace gfx::physics
