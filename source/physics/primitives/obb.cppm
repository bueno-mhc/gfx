/// @file obb.cppm
/// @brief Obb: an oriented bounding box (a center, half extents along its own axes, an orientation).
export module gfx.physics.primitives:obb;

import std;
import gfx.core.math;
import gfx.physics.enums;
import :aabb;
import :sphere;

export namespace gfx::physics
{

/// @brief A box rotated by @p orientation about its center.
struct Obb
{
    Vector3 center{};          ///< The center.
    Vector3 half_extents{};    ///< Half the size along each of its axes.
    Quaternion orientation{};  ///< The rotation of its axes (unit quaternion).

    /// @brief Equal centers, extents and orientations (exactly).
    [[nodiscard]] constexpr auto operator==(const Obb&) const noexcept -> bool = default;

    /// @brief The box @p box placed by the rigid transform @p transform.
    [[nodiscard]] static constexpr auto from_aabb(const Aabb& box,
                                                  const Transform& transform = {}) noexcept -> Obb
    {
        return {.center = transform_point(transform, box.center()), .half_extents = box.half_extents(), .orientation = transform.orientation};
    }

    /// @brief Its three unit axes (the rotated x, y and z).
    [[nodiscard]] constexpr auto axes() const noexcept -> std::array<Vector3, 3>
    {
        return {rotate(orientation, {.x = 1}), rotate(orientation, {.y = 1}), rotate(orientation, {.z = 1})};
    }

    /// @brief Corner @p index (0 to 7): bit 0 picks +x, bit 1 +y, bit 2 +z along its own axes.
    [[nodiscard]] constexpr auto corner(std::size_t index) const noexcept -> Vector3
    {
        const Vector3 local{.x = (index & 1U) != 0 ? half_extents.x : -half_extents.x, .y = (index & 2U) != 0 ? half_extents.y : -half_extents.y,
                            .z = (index & 4U) != 0 ? half_extents.z : -half_extents.z};
        return center + rotate(orientation, local);
    }

    /// @brief Its point closest to @p point (the point itself when inside).
    [[nodiscard]] constexpr auto closest_point(Vector3 point) const noexcept -> Vector3
    {
        const Vector3 local = rotate(conjugate(orientation), point - center);
        const Vector3 clamped{.x = std::clamp(local.x, -half_extents.x, half_extents.x), .y = std::clamp(local.y, -half_extents.y, half_extents.y),
                              .z = std::clamp(local.z, -half_extents.z, half_extents.z)};
        return center + rotate(orientation, clamped);
    }

    /// @brief Whether @p point is inside or on the boundary (within GEOMETRIC_TOLERANCE).
    [[nodiscard]] constexpr auto contains(Vector3 point) const noexcept -> bool
    {
        const Vector3 local = component_absolute(rotate(conjugate(orientation), point - center));
        return local.x <= half_extents.x + GEOMETRIC_TOLERANCE && local.y <= half_extents.y + GEOMETRIC_TOLERANCE
               && local.z <= half_extents.z + GEOMETRIC_TOLERANCE;
    }

    /// @brief The box moved by the rigid transform @p transform.
    [[nodiscard]] constexpr auto transformed(const Transform& transform) const noexcept -> Obb
    {
        return {.center = transform_point(transform, center), .half_extents = half_extents, .orientation = transform.orientation * orientation};
    }

    /// @brief The corner farthest along @p direction.
    [[nodiscard]] constexpr auto support(Vector3 direction) const noexcept -> Vector3
    {
        const Vector3 local = rotate(conjugate(orientation), direction);
        const Vector3 corner_local{.x = local.x >= 0 ? half_extents.x : -half_extents.x, .y = local.y >= 0 ? half_extents.y : -half_extents.y,
                                   .z = local.z >= 0 ? half_extents.z : -half_extents.z};
        return center + rotate(orientation, corner_local);
    }

    /// @brief No rounding: 0.
    [[nodiscard]] constexpr auto margin() const noexcept -> float
    {
        return 0;
    }

    /// @brief The axis-aligned bounding box.
    [[nodiscard]] constexpr auto bounds() const noexcept -> Aabb
    {
        // The extent along each world axis: the absolute rotated half extents summed.
        const Matrix3 rotation = Matrix3::from_rotation(orientation);
        const Vector3 reach = component_absolute(rotation.columns[0]) * half_extents.x + component_absolute(rotation.columns[1]) * half_extents.y
                              + component_absolute(rotation.columns[2]) * half_extents.z;
        return Aabb::from_center(center, reach);
    }
};

/// @brief Whether two oriented boxes overlap (separating axis test on their 15 axes; touching counts).
[[nodiscard]] constexpr auto
intersects(const Obb& first,
           const Obb& second) noexcept -> bool
{
    // The second box in the first's frame: rotation R, translation t; |R| with a tolerance for near-parallel edges.
    const std::array<Vector3, 3> first_axes = first.axes();
    const std::array<Vector3, 3> second_axes = second.axes();
    std::array<std::array<float, 3>, 3> rotation{};
    std::array<std::array<float, 3>, 3> absolute{};
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            rotation[row][column] = dot(first_axes[row], second_axes[column]);
            absolute[row][column] = std::abs(rotation[row][column]) + GEOMETRIC_TOLERANCE;
        }
    }
    const Vector3 offset = second.center - first.center;
    const std::array<float, 3> translation{dot(offset, first_axes[0]), dot(offset, first_axes[1]), dot(offset, first_axes[2])};
    const std::array<float, 3> a{first.half_extents.x, first.half_extents.y, first.half_extents.z};
    const std::array<float, 3> b{second.half_extents.x, second.half_extents.y, second.half_extents.z};

    // The first box's axes, then the second's.
    for (std::size_t index = 0; index < 3; ++index)
    {
        const float reach = a[index] + b[0] * absolute[index][0] + b[1] * absolute[index][1] + b[2] * absolute[index][2];
        if (std::abs(translation[index]) > reach)
        {
            return false;
        }
    }
    for (std::size_t index = 0; index < 3; ++index)
    {
        const float reach = a[0] * absolute[0][index] + a[1] * absolute[1][index] + a[2] * absolute[2][index] + b[index];
        const float along = translation[0] * rotation[0][index] + translation[1] * rotation[1][index] + translation[2] * rotation[2][index];
        if (std::abs(along) > reach)
        {
            return false;
        }
    }

    // The nine cross products of an axis of each.
    for (std::size_t i = 0; i < 3; ++i)
    {
        const std::size_t i1 = (i + 1) % 3;
        const std::size_t i2 = (i + 2) % 3;
        for (std::size_t j = 0; j < 3; ++j)
        {
            const std::size_t j1 = (j + 1) % 3;
            const std::size_t j2 = (j + 2) % 3;
            const float reach = a[i1] * absolute[i2][j] + a[i2] * absolute[i1][j] + b[j1] * absolute[i][j2] + b[j2] * absolute[i][j1];
            const float along = translation[i2] * rotation[i1][j] - translation[i1] * rotation[i2][j];
            if (std::abs(along) > reach)
            {
                return false;
            }
        }
    }
    return true;
}

/// @brief Whether an oriented box and an axis-aligned one overlap.
[[nodiscard]] constexpr auto
intersects(const Obb& oriented,
           const Aabb& box) noexcept -> bool
{
    return intersects(oriented, Obb::from_aabb(box));
}

/// @brief Whether an oriented box and a sphere overlap (touching counts).
[[nodiscard]] constexpr auto
intersects(const Obb& box,
           const Sphere& sphere) noexcept -> bool
{
    return distance_squared(box.closest_point(sphere.center), sphere.center) <= sphere.radius * sphere.radius;
}

} // namespace gfx::physics
