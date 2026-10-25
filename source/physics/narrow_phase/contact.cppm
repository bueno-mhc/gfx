/// @file contact.cppm
/// @brief What the narrow phase produces: contact points with feature ids and accumulated impulses, manifolds of up to
///        MAXIMUM_MANIFOLD_POINTS points sharing a normal, the settings of collision, and the reduction of many points
///        to four.
///
/// A manifold's normal points from the first shape to the second; a point's separation is along it (negative: the
/// shapes overlap). A feature id names the features that made a point — for a face contact the reference face, the
/// incident face and the incident vertex or the pair (incident edge, reference side) the point was clipped from — so
/// that the same contact keeps its id from step to step and the solver's impulses can be carried over (warm starting).
export module gfx.physics.narrow_phase:contact;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.primitives;

export namespace gfx::physics
{

/// @brief The features a contact point comes from.
struct ContactFeature
{
    std::uint32_t sub_shape_first = NO_SUB_SHAPE;  ///< The triangle of the first shape (meshes, height fields), or NO_SUB_SHAPE.
    std::uint32_t sub_shape_second = NO_SUB_SHAPE; ///< The triangle of the second shape, or NO_SUB_SHAPE.
    std::uint32_t local = 0;                       ///< The features within the two (contact_features).

    [[nodiscard]] constexpr auto operator==(const ContactFeature&) const noexcept -> bool = default;
};

/// @brief A point of contact.
struct ContactPoint
{
    Vector3 position{};                     ///< Midway between the two surfaces (world space).
    float separation = 0.0F;                ///< Along the manifold's normal: negative when the shapes overlap.
    ContactFeature feature{};               ///< What made it.
    float normal_impulse = 0.0F;            ///< The solver's accumulated impulse along the normal (carried over by ManifoldCache).
    std::array<float, 2> tangent_impulse{}; ///< The solver's accumulated friction impulses.
};

/// @brief Up to four contact points sharing a normal.
struct ContactManifold
{
    Vector3 normal{.y = 1.0F};                                  ///< Unit length, from the first shape to the second.
    std::array<ContactPoint, MAXIMUM_MANIFOLD_POINTS> points{}; ///< The first point_count are in use.
    std::uint32_t point_count = 0;                              ///< At most MAXIMUM_MANIFOLD_POINTS.
    std::uint32_t sub_shape_first = NO_SUB_SHAPE;               ///< The first shape's compound child or triangle.
    std::uint32_t sub_shape_second = NO_SUB_SHAPE;              ///< The second shape's compound child or triangle.
    ContactAlgorithm algorithm = ContactAlgorithm::GJK;         ///< What computed it.

    /// @brief The points in use.
    [[nodiscard]] constexpr auto contacts() const noexcept -> std::span<const ContactPoint>
    {
        return std::span{points}.first(point_count);
    }

    /// @brief The smallest separation of its points (the deepest).
    [[nodiscard]] constexpr auto deepest() const noexcept -> float
    {
        float smallest = std::numeric_limits<float>::max();
        for (const ContactPoint& point : contacts())
        {
            smallest = std::min(smallest, point.separation);
        }
        return smallest;
    }
};

/// @brief How contacts are made.
struct CollisionSettings
{
    float speculative_distance = 0.02F;  ///< Contacts are made up to this separation (speculative contacts), at least 0.
    float merge_cosine = 0.999F;         ///< Manifolds of one pair (different triangles) whose normals' cosine is above it merge.
    std::uint32_t maximum_manifolds = 8; ///< Manifolds per pair of shapes at most (the deepest are kept), at least 1.
};

/// @brief The `local` part of contact features.
namespace contact_features
{

/// @brief An incident polygon's vertex @p vertex.
[[nodiscard]] constexpr auto
incident_vertex(std::uint32_t vertex) noexcept -> std::uint32_t
{
    return vertex & 0x1FU;
}

/// @brief Where the incident polygon's edge @p edge crosses the reference face's side @p side.
[[nodiscard]] constexpr auto
clipped_edge(std::uint32_t edge,
             std::uint32_t side) noexcept -> std::uint32_t
{
    return 0x400U | (edge & 0x1FU) << 5U | (side & 0x1FU);
}

/// @brief The reference face's corner @p corner (inside the incident polygon).
[[nodiscard]] constexpr auto
reference_corner(std::uint32_t corner) noexcept -> std::uint32_t
{
    return 0x400U | 0x3E0U | (corner & 0x1FU);
}

/// @brief A point of a face contact: reference face, incident face (10 bits each), the point's tag (11 bits), and
///        whether the reference face is the first shape's.
[[nodiscard]] constexpr auto
face_contact(std::uint32_t reference_face,
             std::uint32_t incident_face,
             std::uint32_t tag,
             bool first_is_reference) noexcept -> std::uint32_t
{
    return (first_is_reference ? 0x80000000U : 0U) | (reference_face & 0x3FFU) << 21U | (incident_face & 0x3FFU) << 11U | (tag & 0x7FFU);
}

/// @brief A single contact between feature @p first of the first shape and @p second of the second (points, edges).
[[nodiscard]] constexpr auto
point_contact(std::uint32_t first,
              std::uint32_t second) noexcept -> std::uint32_t
{
    return 0x40000000U | (first & 0x7FFFU) << 15U | (second & 0x7FFFU);
}

/// @brief @p local with the shapes' roles exchanged.
[[nodiscard]] constexpr auto
flipped(std::uint32_t local) noexcept -> std::uint32_t
{
    if ((local & 0x40000000U) != 0)
    {
        return 0x40000000U | (local & 0x7FFFU) << 15U | (local >> 15U & 0x7FFFU);
    }
    return local ^ 0x80000000U;
}

} // namespace contact_features

/// @brief @p manifold with its shapes exchanged: the normal reversed, sub-shapes and features swapped.
[[nodiscard]] constexpr auto
flipped(const ContactManifold& manifold) noexcept -> ContactManifold
{
    ContactManifold result = manifold;
    result.normal = -manifold.normal;
    result.sub_shape_first = manifold.sub_shape_second;
    result.sub_shape_second = manifold.sub_shape_first;
    for (ContactPoint& point : std::span{result.points}.first(result.point_count))
    {
        point.feature = {.sub_shape_first = point.feature.sub_shape_second,
                         .sub_shape_second = point.feature.sub_shape_first,
                         .local = contact_features::flipped(point.feature.local)};
    }
    return result;
}

/// @brief Puts into @p manifold at most MAXIMUM_MANIFOLD_POINTS of @p points that keep the contact's shape: the
///        deepest, the farthest from it, the one spanning the largest triangle with them, and the one farthest on the
///        other side of their line (distances measured in the plane perpendicular to @p normal).
constexpr void
reduce_contact_points(std::span<const ContactPoint> points,
                      Vector3 normal,
                      ContactManifold& manifold) noexcept
{
    manifold.point_count = 0;
    if (points.size() <= MAXIMUM_MANIFOLD_POINTS)
    {
        for (const ContactPoint& point : points)
        {
            manifold.points[manifold.point_count++] = point;
        }
        return;
    }
    const auto flat = [normal](Vector3 vector) { return vector - normal * dot(vector, normal); };
    std::size_t deepest = 0;
    for (std::size_t index = 1; index < points.size(); ++index)
    {
        deepest = points[index].separation < points[deepest].separation ? index : deepest;
    }
    std::size_t farthest = deepest == 0 ? 1 : 0;
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        if (length_squared(flat(points[index].position - points[deepest].position)) > length_squared(flat(points[farthest].position - points[deepest].position)))
        {
            farthest = index;
        }
    }
    const Vector3 base = points[farthest].position - points[deepest].position;
    const auto area = [&](std::size_t index) { return dot(cross(base, points[index].position - points[deepest].position), normal); };
    std::size_t widest = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        if (index != deepest && index != farthest && (widest == std::numeric_limits<std::size_t>::max() || std::abs(area(index)) > std::abs(area(widest))))
        {
            widest = index;
        }
    }
    manifold.points[manifold.point_count++] = points[deepest];
    manifold.points[manifold.point_count++] = points[farthest];
    manifold.points[manifold.point_count++] = points[widest];
    const float side = area(widest) >= 0.0F ? 1.0F : -1.0F;
    std::size_t opposite = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        if (index != deepest && index != farthest && index != widest && -side * area(index) > 0.0F
            && (opposite == std::numeric_limits<std::size_t>::max() || -side * area(index) > -side * area(opposite)))
        {
            opposite = index;
        }
    }
    if (opposite != std::numeric_limits<std::size_t>::max())
    {
        manifold.points[manifold.point_count++] = points[opposite];
    }
}

} // namespace gfx::physics
