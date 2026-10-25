/// @file convex.cppm
/// @brief The convex shapes with a closed form: sphere, capsule, cylinder, cone, box, and the triangle meshes and height
///        fields are made of. Each is a constexpr value in its own frame (centered at the origin, axis +Y for capsules,
///        cylinders and cones) and a ConvexShape: support() gives the farthest point of its *core* (the shape without
///        its margin: a sphere's core is its center, a capsule's its segment, a rounded box's the box inside), margin()
///        the radius around the core, bounds() the box around the whole shape.
///
/// supporting_face(direction) gives the feature of the core that faces @p direction, as contact manifolds need it: a
/// polygon counter-clockwise around its outward normal (box faces, cylinder caps as octagons, a cone's base), a segment
/// (a capsule's axis, a cylinder's side line, a cone's slant) or a point; `face` identifies it for feature ids.
export module gfx.physics.shapes:convex;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.primitives;

export namespace gfx::physics
{

/// @brief The feature of a core that faces a direction (local space).
struct SupportingFace
{
    std::array<Vector3, MAXIMUM_FACE_VERTICES> vertices{}; ///< Counter-clockwise around normal when count >= 3.
    std::uint32_t count = 0;                               ///< 1 (a point), 2 (a segment) or more (a polygon).
    Vector3 normal{};                                      ///< The outward normal of a polygon; the direction asked otherwise.
    std::uint32_t face = 0;                                ///< Which feature of the shape it is (stable: feature ids use it).

    /// @brief The vertices in use.
    [[nodiscard]] constexpr auto points() const noexcept -> std::span<const Vector3>
    {
        return std::span{vertices}.first(count);
    }
};

namespace shape_detail
{

/// Supporting faces of curved shapes switch to a polygon or a segment within about 5.7° of the face's normal.
inline constexpr float FACE_ALIGNMENT = 0.995F;

/// Supporting faces of curved shapes switch to a segment when the direction is within about 5.7° of perpendicular to it.
inline constexpr float SIDE_ALIGNMENT = 0.1F;

/// Vertices of the polygon a cylinder cap or a cone base is approximated by.
inline constexpr std::uint32_t ROUND_FACE_VERTICES = 8;

/// The face made of the points @p points.
[[nodiscard]] constexpr auto
face_of(std::initializer_list<Vector3> points,
        Vector3 normal,
        std::uint32_t face) noexcept -> SupportingFace
{
    SupportingFace result{.normal = normal, .face = face};
    for (const Vector3 point : points)
    {
        result.vertices[result.count++] = point;
    }
    return result;
}

/// A regular polygon of ROUND_FACE_VERTICES vertices of radius @p radius at height @p y, counter-clockwise around +Y
/// (@p up) or -Y.
[[nodiscard]] constexpr auto
round_face(float radius,
           float y,
           bool up,
           std::uint32_t face) noexcept -> SupportingFace
{
    SupportingFace result{.count = ROUND_FACE_VERTICES, .normal = {.y = up ? 1.0F : -1.0F}, .face = face};
    for (std::uint32_t index = 0; index < ROUND_FACE_VERTICES; ++index)
    {
        // Counter-clockwise around +Y turns +X toward -Z.
        const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(index) / static_cast<float>(ROUND_FACE_VERTICES);
        const float side = up ? -1.0F : 1.0F;
        result.vertices[index] = {.x = radius * cosine(angle), .y = y, .z = side * radius * sine(angle)};
    }
    return result;
}

/// The cosine of the angle between @p direction and +Y.
[[nodiscard]] constexpr auto
axial(Vector3 direction) noexcept -> float
{
    const float size = length(direction);
    return size > 0.0F ? direction.y / size : 0.0F;
}

} // namespace shape_detail

// ===================================================================================================== sphere

/// @brief A sphere of radius `radius` centered at the origin: a point core with a margin.
struct SphereShape
{
    float radius = 0.5F; ///< Greater than 0.

    [[nodiscard]] constexpr auto operator==(const SphereShape&) const noexcept -> bool = default;

    /// @brief The core's farthest point along @p direction: the center.
    [[nodiscard]] constexpr auto support(Vector3 /*direction*/) const noexcept -> Vector3
    {
        return {};
    }

    /// @brief The radius.
    [[nodiscard]] constexpr auto margin() const noexcept -> float
    {
        return radius;
    }

    /// @brief The box around the sphere.
    [[nodiscard]] constexpr auto bounds() const noexcept -> Aabb
    {
        return Aabb::from_center({}, {.x = radius, .y = radius, .z = radius});
    }

    /// @brief The center (a point).
    [[nodiscard]] constexpr auto supporting_face(Vector3 direction) const noexcept -> SupportingFace
    {
        return shape_detail::face_of({Vector3{}}, direction, 0);
    }
};

// ===================================================================================================== capsule

/// @brief A capsule: the segment from (0, -half_height, 0) to (0, half_height, 0) with a margin of `radius`.
struct CapsuleShape
{
    float half_height = 0.5F; ///< Half the length of the segment, at least 0 (0: a sphere).
    float radius = 0.25F;     ///< Greater than 0.

    [[nodiscard]] constexpr auto operator==(const CapsuleShape&) const noexcept -> bool = default;

    /// @brief The segment's end along @p direction (the top one when perpendicular).
    [[nodiscard]] constexpr auto support(Vector3 direction) const noexcept -> Vector3
    {
        return {.y = direction.y >= 0.0F ? half_height : -half_height};
    }

    /// @brief The radius.
    [[nodiscard]] constexpr auto margin() const noexcept -> float
    {
        return radius;
    }

    /// @brief The box around the capsule.
    [[nodiscard]] constexpr auto bounds() const noexcept -> Aabb
    {
        return Aabb::from_center({}, {.x = radius, .y = half_height + radius, .z = radius});
    }

    /// @brief The segment (face 0) when @p direction is nearly perpendicular to the axis, else its top (1) or bottom (2) end.
    [[nodiscard]] constexpr auto supporting_face(Vector3 direction) const noexcept -> SupportingFace
    {
        const float along = shape_detail::axial(direction);
        if (along > -shape_detail::SIDE_ALIGNMENT && along < shape_detail::SIDE_ALIGNMENT)
        {
            return shape_detail::face_of({Vector3{.y = -half_height}, Vector3{.y = half_height}}, direction, 0);
        }
        return along > 0.0F ? shape_detail::face_of({Vector3{.y = half_height}}, direction, 1) : shape_detail::face_of({Vector3{.y = -half_height}}, direction, 2);
    }
};

// ===================================================================================================== cylinder

/// @brief A cylinder of radius `radius` from y = -half_height to y = half_height (no margin: its edges are sharp).
struct CylinderShape
{
    float half_height = 0.5F; ///< Greater than 0.
    float radius = 0.5F;      ///< Greater than 0.

    [[nodiscard]] constexpr auto operator==(const CylinderShape&) const noexcept -> bool = default;

    /// @brief The point of the rim (or of a cap's center line) farthest along @p direction.
    [[nodiscard]] constexpr auto support(Vector3 direction) const noexcept -> Vector3
    {
        const float across = square_root(direction.x * direction.x + direction.z * direction.z);
        const float y = direction.y >= 0.0F ? half_height : -half_height;
        return across > 0.0F ? Vector3{.x = radius * direction.x / across, .y = y, .z = radius * direction.z / across} : Vector3{.y = y};
    }

    /// @brief 0.
    [[nodiscard]] constexpr auto margin() const noexcept -> float
    {
        return 0.0F;
    }

    /// @brief The box around the cylinder.
    [[nodiscard]] constexpr auto bounds() const noexcept -> Aabb
    {
        return Aabb::from_center({}, {.x = radius, .y = half_height, .z = radius});
    }

    /// @brief A cap as an octagon (0 top, 1 bottom) near the axis, the side line (2) near perpendicular, else the rim point (3).
    [[nodiscard]] constexpr auto supporting_face(Vector3 direction) const noexcept -> SupportingFace
    {
        const float along = shape_detail::axial(direction);
        if (along >= shape_detail::FACE_ALIGNMENT || along <= -shape_detail::FACE_ALIGNMENT)
        {
            return along > 0.0F ? shape_detail::round_face(radius, half_height, true, 0) : shape_detail::round_face(radius, -half_height, false, 1);
        }
        if (along > -shape_detail::SIDE_ALIGNMENT && along < shape_detail::SIDE_ALIGNMENT)
        {
            const Vector3 rim = support({.x = direction.x, .z = direction.z});
            return shape_detail::face_of({Vector3{.x = rim.x, .y = -half_height, .z = rim.z}, Vector3{.x = rim.x, .y = half_height, .z = rim.z}}, direction, 2);
        }
        return shape_detail::face_of({support(direction)}, direction, 3);
    }
};

// ===================================================================================================== cone

/// @brief A cone with its apex at (0, half_height, 0) and a base of radius `radius` at y = -half_height (no margin).
struct ConeShape
{
    float half_height = 0.5F; ///< Greater than 0.
    float radius = 0.5F;      ///< Greater than 0.

    [[nodiscard]] constexpr auto operator==(const ConeShape&) const noexcept -> bool = default;

    /// @brief The apex, or the base's rim point, farthest along @p direction.
    [[nodiscard]] constexpr auto support(Vector3 direction) const noexcept -> Vector3
    {
        // The apex wins when the direction is within the apex's normal cone: its angle to +Y below the slant's.
        const float slant = square_root(radius * radius + 4.0F * half_height * half_height);
        if (direction.y > length(direction) * (radius / slant))
        {
            return {.y = half_height};
        }
        const float across = square_root(direction.x * direction.x + direction.z * direction.z);
        return across > 0.0F ? Vector3{.x = radius * direction.x / across, .y = -half_height, .z = radius * direction.z / across} : Vector3{.y = -half_height};
    }

    /// @brief 0.
    [[nodiscard]] constexpr auto margin() const noexcept -> float
    {
        return 0.0F;
    }

    /// @brief The box around the cone.
    [[nodiscard]] constexpr auto bounds() const noexcept -> Aabb
    {
        return Aabb::from_center({}, {.x = radius, .y = half_height, .z = radius});
    }

    /// @brief The base as an octagon (0) near -Y, a slant line from the apex (1) near the side's normal, else a point (2).
    [[nodiscard]] constexpr auto supporting_face(Vector3 direction) const noexcept -> SupportingFace
    {
        const float size = length(direction);
        if (size <= 0.0F)
        {
            return shape_detail::face_of({Vector3{.y = half_height}}, direction, 2);
        }
        if (direction.y / size <= -shape_detail::FACE_ALIGNMENT)
        {
            return shape_detail::round_face(radius, -half_height, false, 0);
        }
        const float across = square_root(direction.x * direction.x + direction.z * direction.z);
        if (across > 0.0F)
        {
            // The side's outward normal at the direction's azimuth: (cos φ · 2h, r, sin φ · 2h), normalized.
            const float height = 2.0F * half_height;
            const Vector3 side = normalized(Vector3{.x = direction.x / across * height, .y = radius, .z = direction.z / across * height});
            if (dot(side, direction) / size >= shape_detail::FACE_ALIGNMENT)
            {
                const Vector3 rim{.x = radius * direction.x / across, .y = -half_height, .z = radius * direction.z / across};
                return shape_detail::face_of({rim, Vector3{.y = half_height}}, direction, 1);
            }
        }
        return shape_detail::face_of({support(direction)}, direction, 2);
    }
};

// ===================================================================================================== box

/// @brief A box of half extents `half_extents`, rounded by `radius` (the core is the box; the shape is the core grown by
///        the radius, edges and corners rounded).
struct BoxShape
{
    Vector3 half_extents{.x = 0.5F, .y = 0.5F, .z = 0.5F}; ///< Each greater than 0.
    float radius = 0.0F;                                   ///< The rounding, at least 0.

    [[nodiscard]] constexpr auto operator==(const BoxShape&) const noexcept -> bool = default;

    /// @brief The corner farthest along @p direction.
    [[nodiscard]] constexpr auto support(Vector3 direction) const noexcept -> Vector3
    {
        return {.x = direction.x >= 0.0F ? half_extents.x : -half_extents.x,
                .y = direction.y >= 0.0F ? half_extents.y : -half_extents.y,
                .z = direction.z >= 0.0F ? half_extents.z : -half_extents.z};
    }

    /// @brief The rounding.
    [[nodiscard]] constexpr auto margin() const noexcept -> float
    {
        return radius;
    }

    /// @brief The box around the rounded box.
    [[nodiscard]] constexpr auto bounds() const noexcept -> Aabb
    {
        return Aabb::from_center({}, half_extents + Vector3{.x = radius, .y = radius, .z = radius});
    }

    /// @brief The face whose normal is closest to @p direction: face 2 a + (1 if positive), a the axis (0 x, 1 y, 2 z).
    [[nodiscard]] constexpr auto supporting_face(Vector3 direction) const noexcept -> SupportingFace
    {
        // The axis of the largest component, then the face's corners counter-clockwise around its outward normal:
        // with (a, b, c) cyclic, (b, c) runs (-, -), (+, -), (+, +), (-, +) around +a and the other way around -a.
        const std::array<float, 3> components{direction.x, direction.y, direction.z};
        std::size_t axis = 0;
        for (std::size_t index = 1; index < 3; ++index)
        {
            if (std::abs(components[index]) > std::abs(components[axis]))
            {
                axis = index;
            }
        }
        const bool positive = components[axis] >= 0.0F;
        const std::array<float, 3> half{half_extents.x, half_extents.y, half_extents.z};
        const std::size_t second = (axis + 1) % 3;
        const std::size_t third = (axis + 2) % 3;
        constexpr std::array<std::array<float, 2>, 4> AROUND{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
        SupportingFace face{.count = 4, .face = static_cast<std::uint32_t>(axis * 2 + (positive ? 1 : 0))};
        std::array<float, 3> normal{};
        normal[axis] = positive ? 1.0F : -1.0F;
        face.normal = {.x = normal[0], .y = normal[1], .z = normal[2]};
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
            const std::array<float, 2> signs = AROUND[positive ? corner : (4 - corner) % 4];
            std::array<float, 3> point{};
            point[axis] = positive ? half[axis] : -half[axis];
            point[second] = signs[0] * half[second];
            point[third] = signs[1] * half[third];
            face.vertices[corner] = {.x = point[0], .y = point[1], .z = point[2]};
        }
        return face;
    }
};

// ===================================================================================================== triangle

/// @brief A triangle (of a mesh or a height field), counter-clockwise around its front normal.
struct TriangleShape
{
    Vector3 first{};  ///< A vertex.
    Vector3 second{}; ///< The next vertex.
    Vector3 third{};  ///< The last vertex.

    [[nodiscard]] constexpr auto operator==(const TriangleShape&) const noexcept -> bool = default;

    /// @brief The front normal (unit length; the zero vector for a degenerate triangle).
    [[nodiscard]] constexpr auto normal() const noexcept -> Vector3
    {
        return normalized(cross(second - first, third - first));
    }

    /// @brief The vertex farthest along @p direction (the first of equals).
    [[nodiscard]] constexpr auto support(Vector3 direction) const noexcept -> Vector3
    {
        const float a = dot(first, direction);
        const float b = dot(second, direction);
        const float c = dot(third, direction);
        return a >= b && a >= c ? first : b >= c ? second : third;
    }

    /// @brief 0.
    [[nodiscard]] constexpr auto margin() const noexcept -> float
    {
        return 0.0F;
    }

    /// @brief The box around the triangle.
    [[nodiscard]] constexpr auto bounds() const noexcept -> Aabb
    {
        return Aabb{.minimum = component_minimum(first, component_minimum(second, third)), .maximum = component_maximum(first, component_maximum(second, third))};
    }

    /// @brief The triangle, its front (face 0) toward @p direction or its back (1).
    [[nodiscard]] constexpr auto supporting_face(Vector3 direction) const noexcept -> SupportingFace
    {
        const Vector3 front = normal();
        return dot(front, direction) >= 0.0F ? shape_detail::face_of({first, second, third}, front, 0) : shape_detail::face_of({first, third, second}, -front, 1);
    }
};

/// @brief A convex shape with a supporting face: what the narrow phase's general convex path takes.
template<class Shape>
concept FacedConvexShape = ConvexShape<Shape> && requires(const Shape& shape, Vector3 direction) {
    { shape.supporting_face(direction) } -> std::same_as<SupportingFace>;
};

} // namespace gfx::physics
