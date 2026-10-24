/// @file aabb.cppm
/// @brief Aabb: an axis-aligned bounding box. What spatial structures store; the bounds of every shape.
export module gfx.physics.primitives:aabb;

import std;
import gfx.core.math;

export namespace gfx::physics
{

/// @brief An axis-aligned box between two corners. The default is the box of the origin alone; Aabb::empty() is the
///        identity of merging (its minimum above its maximum).
struct Aabb
{
    Vector3 minimum{}; ///< The corner of the smallest coordinates.
    Vector3 maximum{}; ///< The corner of the largest coordinates.

    /// @brief Equal corners (exactly).
    [[nodiscard]] constexpr auto operator==(const Aabb&) const noexcept -> bool = default;

    /// @brief The box of nothing: merging a box with it gives that box.
    [[nodiscard]] static constexpr auto empty() noexcept -> Aabb
    {
        constexpr float largest = std::numeric_limits<float>::max();
        return {.minimum = {.x = largest, .y = largest, .z = largest}, .maximum = {.x = -largest, .y = -largest, .z = -largest}};
    }

    /// @brief The box centered on @p middle extending @p half_size along each axis.
    [[nodiscard]] static constexpr auto from_center(Vector3 middle,
                                                    Vector3 half_size) noexcept -> Aabb
    {
        return {.minimum = middle - half_size, .maximum = middle + half_size};
    }

    /// @brief The smallest box holding @p points (Aabb::empty() for none).
    [[nodiscard]] static constexpr auto from_points(std::span<const Vector3> points) noexcept -> Aabb
    {
        Aabb box = empty();
        for (const Vector3 point : points)
        {
            box.minimum = component_minimum(box.minimum, point);
            box.maximum = component_maximum(box.maximum, point);
        }
        return box;
    }

    /// @brief Whether the box holds nothing (a minimum above its maximum on an axis).
    [[nodiscard]] constexpr auto is_empty() const noexcept -> bool
    {
        return minimum.x > maximum.x || minimum.y > maximum.y || minimum.z > maximum.z;
    }

    /// @brief The center.
    [[nodiscard]] constexpr auto center() const noexcept -> Vector3
    {
        return (minimum + maximum) * 0.5F;
    }

    /// @brief Half the size along each axis.
    [[nodiscard]] constexpr auto half_extents() const noexcept -> Vector3
    {
        return (maximum - minimum) * 0.5F;
    }

    /// @brief The size along each axis.
    [[nodiscard]] constexpr auto extents() const noexcept -> Vector3
    {
        return maximum - minimum;
    }

    /// @brief The area of the surface (the surface-area heuristic of trees).
    [[nodiscard]] constexpr auto surface_area() const noexcept -> float
    {
        const Vector3 size = extents();
        return 2.0F * (size.x * size.y + size.y * size.z + size.z * size.x);
    }

    /// @brief The volume.
    [[nodiscard]] constexpr auto volume() const noexcept -> float
    {
        const Vector3 size = extents();
        return size.x * size.y * size.z;
    }

    /// @brief Whether @p point is inside or on the boundary.
    [[nodiscard]] constexpr auto contains(Vector3 point) const noexcept -> bool
    {
        return point.x >= minimum.x && point.x <= maximum.x && point.y >= minimum.y && point.y <= maximum.y && point.z >= minimum.z
               && point.z <= maximum.z;
    }

    /// @brief Whether @p other is entirely inside (or on the boundary).
    [[nodiscard]] constexpr auto contains(const Aabb& other) const noexcept -> bool
    {
        return contains(other.minimum) && contains(other.maximum);
    }

    /// @brief The smallest box holding this one and @p other.
    [[nodiscard]] constexpr auto merged(const Aabb& other) const noexcept -> Aabb
    {
        return {.minimum = component_minimum(minimum, other.minimum), .maximum = component_maximum(maximum, other.maximum)};
    }

    /// @brief The smallest box holding this one and @p point.
    [[nodiscard]] constexpr auto merged(Vector3 point) const noexcept -> Aabb
    {
        return {.minimum = component_minimum(minimum, point), .maximum = component_maximum(maximum, point)};
    }

    /// @brief The box grown by @p margin on every side (shrunk for a negative margin).
    [[nodiscard]] constexpr auto expanded(float margin) const noexcept -> Aabb
    {
        const Vector3 grow{.x = margin, .y = margin, .z = margin};
        return {.minimum = minimum - grow, .maximum = maximum + grow};
    }

    /// @brief The point of the box closest to @p point (the point itself when inside).
    [[nodiscard]] constexpr auto closest_point(Vector3 point) const noexcept -> Vector3
    {
        return component_maximum(minimum, component_minimum(maximum, point));
    }

    /// @brief The squared distance from @p point to the box (0 inside).
    [[nodiscard]] constexpr auto squared_distance(Vector3 point) const noexcept -> float
    {
        return distance_squared(point, closest_point(point));
    }

    /// @brief Corner @p index (0 to 7): bit 0 picks the maximum x, bit 1 the maximum y, bit 2 the maximum z.
    [[nodiscard]] constexpr auto corner(std::size_t index) const noexcept -> Vector3
    {
        return {.x = (index & 1U) != 0 ? maximum.x : minimum.x, .y = (index & 2U) != 0 ? maximum.y : minimum.y,
                .z = (index & 4U) != 0 ? maximum.z : minimum.z};
    }

    /// @brief The box holding this one transformed by @p matrix (affine; Arvo's method: exact for the transformed
    ///        corners).
    [[nodiscard]] constexpr auto transformed(const Matrix4& matrix) const noexcept -> Aabb
    {
        // Each axis of the result: the translation plus, per source axis, the smaller and larger product.
        const Vector3 translation = to_vector3(matrix.columns[3]);
        Aabb result{.minimum = translation, .maximum = translation};
        for (std::size_t column = 0; column < 3; ++column)
        {
            const Vector3 axis = to_vector3(matrix.columns[column]);
            const float low = column == 0 ? minimum.x : column == 1 ? minimum.y : minimum.z;
            const float high = column == 0 ? maximum.x : column == 1 ? maximum.y : maximum.z;
            const Vector3 first = axis * low;
            const Vector3 second = axis * high;
            result.minimum += component_minimum(first, second);
            result.maximum += component_maximum(first, second);
        }
        return result;
    }

    /// @brief The box holding this one moved by the rigid transform @p transform.
    [[nodiscard]] constexpr auto transformed(const Transform& transform) const noexcept -> Aabb
    {
        return transformed(Matrix4::from_transform(transform));
    }

    /// @brief The corner farthest along @p direction (the support function of a convex shape without margin).
    [[nodiscard]] constexpr auto support(Vector3 direction) const noexcept -> Vector3
    {
        return {.x = direction.x >= 0 ? maximum.x : minimum.x, .y = direction.y >= 0 ? maximum.y : minimum.y, .z = direction.z >= 0 ? maximum.z : minimum.z};
    }

    /// @brief No rounding: 0.
    [[nodiscard]] constexpr auto margin() const noexcept -> float
    {
        return 0;
    }

    /// @brief The box itself.
    [[nodiscard]] constexpr auto bounds() const noexcept -> Aabb
    {
        return *this;
    }
};

/// @brief Whether two boxes overlap (touching counts).
[[nodiscard]] constexpr auto
intersects(const Aabb& first,
           const Aabb& second) noexcept -> bool
{
    return first.minimum.x <= second.maximum.x && first.maximum.x >= second.minimum.x && first.minimum.y <= second.maximum.y
           && first.maximum.y >= second.minimum.y && first.minimum.z <= second.maximum.z && first.maximum.z >= second.minimum.z;
}

} // namespace gfx::physics
