/// @file analytic.cppm
/// @brief The pairs with exact, cheaper answers than GJK: sphere–sphere, sphere–capsule, capsule–capsule (two points
///        when the segments lie side by side), sphere–box, and the separating axis test for box–box and
///        triangle–triangle (face axes preferred to edge axes; face contacts clipped, edge contacts at the edges'
///        closest points). Each takes the shapes in its own order; the convex dispatch flips the result for the other.
export module gfx.physics.narrow_phase:analytic;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.primitives;
import gfx.physics.shapes;
import :contact;
import :clipping;

export namespace gfx::physics
{

namespace analytic_detail
{

/// The manifold of the core points @p first and @p second with margins: one point, the normal along the line between
/// them (or @p fallback when they coincide).
[[nodiscard]] constexpr auto
between_points(Vector3 first,
               Vector3 second,
               float first_margin,
               float second_margin,
               Vector3 fallback,
               std::uint32_t local_feature,
               const CollisionSettings& settings,
               ContactAlgorithm algorithm) noexcept -> std::optional<ContactManifold>
{
    const Vector3 gap = second - first;
    const float distance = length(gap);
    const Vector3 normal = distance > 1e-9F ? gap / distance : fallback;
    return point_manifold(first, second, normal, distance, first_margin, second_margin, local_feature, settings, algorithm);
}

/// A capsule's segment at @p pose.
[[nodiscard]] constexpr auto
segment_of(const CapsuleShape& capsule,
           const Transform& pose) noexcept -> Segment
{
    return {.start = transform_point(pose, {.y = -capsule.half_height}), .end = transform_point(pose, {.y = capsule.half_height})};
}

/// The direction perpendicular to @p axis nearest to @p preferred (any perpendicular when they are parallel).
[[nodiscard]] constexpr auto
perpendicular_toward(Vector3 axis,
                     Vector3 preferred) noexcept -> Vector3
{
    const Vector3 flat = preferred - axis * dot(preferred, axis);
    return length_squared(flat) > 1e-12F ? normalized(flat) : any_perpendicular(axis);
}

} // namespace analytic_detail

/// @brief Two spheres.
[[nodiscard]] constexpr auto
collide_spheres(const SphereShape& first,
                const Transform& first_pose,
                const SphereShape& second,
                const Transform& second_pose,
                const CollisionSettings& settings) noexcept -> std::optional<ContactManifold>
{
    return analytic_detail::between_points(first_pose.position, second_pose.position, first.radius, second.radius, {.y = 1.0F},
                                           contact_features::point_contact(0, 0), settings, ContactAlgorithm::SPHERE_SPHERE);
}

/// @brief A sphere and a capsule: the sphere's center against the closest point of the capsule's segment.
[[nodiscard]] constexpr auto
collide_sphere_capsule(const SphereShape& sphere,
                       const Transform& sphere_pose,
                       const CapsuleShape& capsule,
                       const Transform& capsule_pose,
                       const CollisionSettings& settings) noexcept -> std::optional<ContactManifold>
{
    const Segment axis = analytic_detail::segment_of(capsule, capsule_pose);
    const Vector3 closest = axis.closest_point(sphere_pose.position);
    const Vector3 fallback = analytic_detail::perpendicular_toward(normalized(axis.direction()), {.y = -1.0F});
    return analytic_detail::between_points(sphere_pose.position, closest, sphere.radius, capsule.radius, fallback, contact_features::point_contact(0, 0),
                                           settings, ContactAlgorithm::SPHERE_CAPSULE);
}

/// @brief Two capsules: the closest points of their segments; two points when the segments are parallel and overlap
///        along their length (a capsule lying on another stays stable).
[[nodiscard]] constexpr auto
collide_capsules(const CapsuleShape& first,
                 const Transform& first_pose,
                 const CapsuleShape& second,
                 const Transform& second_pose,
                 const CollisionSettings& settings) noexcept -> std::optional<ContactManifold>
{
    const Segment a = analytic_detail::segment_of(first, first_pose);
    const Segment b = analytic_detail::segment_of(second, second_pose);
    const SegmentClosestPoints closest = closest_points(a, b);
    const Vector3 axis = length_squared(a.direction()) > 0.0F ? normalized(a.direction()) : Vector3{.y = 1.0F};
    const Vector3 fallback = analytic_detail::perpendicular_toward(axis, second_pose.position - first_pose.position);
    std::optional<ContactManifold> single = analytic_detail::between_points(closest.first_point, closest.second_point, first.radius, second.radius,
                                                                            fallback, contact_features::point_contact(0, 0), settings,
                                                                            ContactAlgorithm::CAPSULE_CAPSULE);
    if (!single || length_squared(b.direction()) <= 0.0F || length_squared(a.direction()) <= 0.0F)
    {
        return single;
    }

    // Parallel segments: the part of b over a, its two ends each against their closest point of a.
    const Vector3 b_axis = normalized(b.direction());
    if (std::abs(dot(axis, b_axis)) < 0.995F)
    {
        return single;
    }
    const float length_a = a.length();
    const float b_start = std::clamp(dot(b.start - a.start, axis) / length_a, 0.0F, 1.0F);
    const float b_end = std::clamp(dot(b.end - a.start, axis) / length_a, 0.0F, 1.0F);
    const float low = std::min(b_start, b_end);
    const float high = std::max(b_start, b_end);
    if ((high - low) * length_a < 1e-3F * (first.radius + second.radius))
    {
        return single;
    }
    ContactManifold manifold{.normal = single->normal, .algorithm = ContactAlgorithm::CAPSULE_CAPSULE};
    for (const auto& [fraction, tag] : {std::pair{low, 1U}, std::pair{high, 2U}})
    {
        const Vector3 on_a = a.at(fraction);
        const Vector3 on_b = b.closest_point(on_a);
        const float separation = dot(on_b - on_a, manifold.normal) - first.radius - second.radius;
        if (separation <= settings.speculative_distance)
        {
            manifold.points[manifold.point_count++] = {.position = ((on_a + manifold.normal * first.radius) + (on_b - manifold.normal * second.radius)) * 0.5F,
                                                       .separation = separation,
                                                       .feature = {.local = contact_features::point_contact(tag, 0)}};
        }
    }
    return manifold.point_count > 0 ? std::optional{manifold} : single;
}

/// @brief A sphere and a box: the box's closest point to the center; a center inside the box leaves by the nearest
///        face. The feature is the box region the center is in (27 of them: inside, faces, edges, corners).
[[nodiscard]] constexpr auto
collide_sphere_box(const SphereShape& sphere,
                   const Transform& sphere_pose,
                   const BoxShape& box,
                   const Transform& box_pose,
                   const CollisionSettings& settings) noexcept -> std::optional<ContactManifold>
{
    // The center in the box's frame, clamped to the box.
    const Vector3 center = rotate(conjugate(box_pose.orientation), sphere_pose.position - box_pose.position);
    const Vector3 h = box.half_extents;
    const Vector3 clamped{.x = std::clamp(center.x, -h.x, h.x), .y = std::clamp(center.y, -h.y, h.y), .z = std::clamp(center.z, -h.z, h.z)};
    const auto region = [&](float value, float half) { return value < -half ? 0U : value > half ? 2U : 1U; };
    const std::uint32_t feature = region(center.x, h.x) + 3 * region(center.y, h.y) + 9 * region(center.z, h.z);
    if (clamped != center)
    {
        // Outside: from the box's closest point to the center.
        const Vector3 outward = normalized(center - clamped);
        return analytic_detail::between_points(sphere_pose.position, transform_point(box_pose, clamped), sphere.radius, box.radius,
                                               rotate(box_pose.orientation, -outward), contact_features::point_contact(0, feature), settings,
                                               ContactAlgorithm::SPHERE_BOX);
    }

    // Inside: out through the nearest face.
    const std::array<float, 3> depth{h.x - std::abs(center.x), h.y - std::abs(center.y), h.z - std::abs(center.z)};
    const std::size_t axis = depth[0] <= depth[1] && depth[0] <= depth[2] ? 0 : depth[1] <= depth[2] ? 1 : 2;
    const std::array<float, 3> coordinates{center.x, center.y, center.z};
    std::array<float, 3> face{};
    face[axis] = coordinates[axis] >= 0.0F ? 1.0F : -1.0F;
    const Vector3 outward = rotate(box_pose.orientation, {.x = face[0], .y = face[1], .z = face[2]});
    const Vector3 on_face = sphere_pose.position + outward * depth[axis];
    return point_manifold(sphere_pose.position, on_face, -outward, -depth[axis], sphere.radius, box.radius,
                          contact_features::point_contact(0, 27 + static_cast<std::uint32_t>(axis) * 2 + (face[axis] > 0.0F ? 1U : 0U)), settings,
                          ContactAlgorithm::SPHERE_BOX);
}

// ===================================================================================================== separating axes

namespace sat_detail
{

/// A polytope's vertices, face normals and edge directions (world space), for the separating axis test.
struct Features
{
    std::array<Vector3, 8> vertices{};
    std::uint32_t vertex_count = 0;
    std::array<Vector3, 3> face_normals{};
    std::uint32_t face_count = 0;
    std::array<Vector3, 3> edges{};
    std::uint32_t edge_count = 0;
};

[[nodiscard]] constexpr auto
features_of(const BoxShape& box,
            const Transform& pose) noexcept -> Features
{
    Features features{.vertex_count = 8, .face_count = 3, .edge_count = 3};
    const Aabb local = Aabb::from_center({}, box.half_extents);
    for (std::size_t corner = 0; corner < 8; ++corner)
    {
        features.vertices[corner] = transform_point(pose, local.corner(corner));
    }
    features.face_normals = {rotate(pose.orientation, {.x = 1.0F}), rotate(pose.orientation, {.y = 1.0F}), rotate(pose.orientation, {.z = 1.0F})};
    features.edges = features.face_normals;
    return features;
}

[[nodiscard]] constexpr auto
features_of(const TriangleShape& triangle,
            const Transform& pose) noexcept -> Features
{
    Features features{.vertex_count = 3, .face_count = 1, .edge_count = 3};
    features.vertices[0] = transform_point(pose, triangle.first);
    features.vertices[1] = transform_point(pose, triangle.second);
    features.vertices[2] = transform_point(pose, triangle.third);
    features.face_normals[0] = rotate(pose.orientation, triangle.normal());
    for (std::uint32_t edge = 0; edge < 3; ++edge)
    {
        features.edges[edge] = normalized(features.vertices[(edge + 1) % 3] - features.vertices[edge]);
    }
    return features;
}

/// The second polytope's separation from the first along @p axis (oriented so that it is the larger of the two
/// ways), and that orientation.
struct AxisSeparation
{
    float separation = 0.0F;
    Vector3 axis{};
};

[[nodiscard]] constexpr auto
separation_along(const Features& first,
                 const Features& second,
                 Vector3 axis) noexcept -> AxisSeparation
{
    float first_low = std::numeric_limits<float>::max();
    float first_high = std::numeric_limits<float>::lowest();
    float second_low = std::numeric_limits<float>::max();
    float second_high = std::numeric_limits<float>::lowest();
    for (const Vector3 vertex : std::span{first.vertices}.first(first.vertex_count))
    {
        first_low = std::min(first_low, dot(vertex, axis));
        first_high = std::max(first_high, dot(vertex, axis));
    }
    for (const Vector3 vertex : std::span{second.vertices}.first(second.vertex_count))
    {
        second_low = std::min(second_low, dot(vertex, axis));
        second_high = std::max(second_high, dot(vertex, axis));
    }
    const float forward = second_low - first_high;
    const float backward = first_low - second_high;
    return forward >= backward ? AxisSeparation{.separation = forward, .axis = axis} : AxisSeparation{.separation = backward, .axis = -axis};
}

/// The edge of @p shape parallel to its edge @p edge that is farthest along @p direction (world space).
[[nodiscard]] constexpr auto
supporting_edge(const BoxShape& box,
                const Transform& pose,
                std::uint32_t edge,
                Vector3 direction) noexcept -> Segment
{
    const Vector3 corner = box.support(rotate(conjugate(pose.orientation), direction));
    Vector3 start = corner;
    Vector3 end = corner;
    if (edge == 0)
    {
        start.x = -box.half_extents.x;
        end.x = box.half_extents.x;
    }
    else if (edge == 1)
    {
        start.y = -box.half_extents.y;
        end.y = box.half_extents.y;
    }
    else
    {
        start.z = -box.half_extents.z;
        end.z = box.half_extents.z;
    }
    return {.start = transform_point(pose, start), .end = transform_point(pose, end)};
}

[[nodiscard]] constexpr auto
supporting_edge(const TriangleShape& triangle,
                const Transform& pose,
                std::uint32_t edge,
                Vector3 /*direction*/) noexcept -> Segment
{
    const std::array<Vector3, 3> corners{triangle.first, triangle.second, triangle.third};
    return {.start = transform_point(pose, corners[edge]), .end = transform_point(pose, corners[(edge + 1) % 3])};
}

} // namespace sat_detail

/// @brief The separating axis test between two boxes, two triangles or a box and a triangle: the axis of least
///        penetration among the face normals and the edges' cross products; a face axis is kept unless an edge axis
///        is clearly better (then the edges' closest points make one contact).
template<class First, class Second>
[[nodiscard]] constexpr auto
collide_separating_axes(const First& first,
                        const Transform& first_pose,
                        const Second& second,
                        const Transform& second_pose,
                        const CollisionSettings& settings,
                        ContactAlgorithm algorithm) noexcept -> std::optional<ContactManifold>
{
    const sat_detail::Features a = sat_detail::features_of(first, first_pose);
    const sat_detail::Features b = sat_detail::features_of(second, second_pose);
    const float margins = first.margin() + second.margin();
    const float limit = settings.speculative_distance + margins;

    // The faces' axes.
    sat_detail::AxisSeparation best_face{.separation = std::numeric_limits<float>::lowest()};
    for (const Vector3 normal : std::span{a.face_normals}.first(a.face_count))
    {
        const sat_detail::AxisSeparation candidate = sat_detail::separation_along(a, b, normal);
        if (candidate.separation > limit)
        {
            return std::nullopt;
        }
        best_face = candidate.separation > best_face.separation ? candidate : best_face;
    }
    for (const Vector3 normal : std::span{b.face_normals}.first(b.face_count))
    {
        const sat_detail::AxisSeparation candidate = sat_detail::separation_along(a, b, normal);
        if (candidate.separation > limit)
        {
            return std::nullopt;
        }
        best_face = candidate.separation > best_face.separation ? candidate : best_face;
    }

    // The edges' axes (parallel edges give none).
    sat_detail::AxisSeparation best_edge{.separation = std::numeric_limits<float>::lowest()};
    std::uint32_t first_edge = 0;
    std::uint32_t second_edge = 0;
    for (std::uint32_t i = 0; i < a.edge_count; ++i)
    {
        for (std::uint32_t j = 0; j < b.edge_count; ++j)
        {
            const Vector3 across = cross(a.edges[i], b.edges[j]);
            if (length_squared(across) < 1e-6F)
            {
                continue;
            }
            const sat_detail::AxisSeparation candidate = sat_detail::separation_along(a, b, normalized(across));
            if (candidate.separation > limit)
            {
                return std::nullopt;
            }
            if (candidate.separation > best_edge.separation)
            {
                best_edge = candidate;
                first_edge = i;
                second_edge = j;
            }
        }
    }

    // A face contact unless an edge axis separates clearly more (faces give stable manifolds).
    const float scale = std::max(length(first.bounds().extents()), length(second.bounds().extents()));
    if (best_edge.separation > best_face.separation + 0.05F * std::abs(best_face.separation) + 1e-3F * scale)
    {
        const Segment on_first = sat_detail::supporting_edge(first, first_pose, first_edge, best_edge.axis);
        const Segment on_second = sat_detail::supporting_edge(second, second_pose, second_edge, -best_edge.axis);
        const SegmentClosestPoints closest = closest_points(on_first, on_second);
        return point_manifold(closest.first_point, closest.second_point, best_edge.axis, best_edge.separation, first.margin(), second.margin(),
                              contact_features::point_contact(64 + first_edge, 64 + second_edge), settings, algorithm);
    }
    const Vector3 normal = best_face.axis;
    const SupportingFace first_face = posed_face(first.supporting_face(rotate(conjugate(first_pose.orientation), normal)), first_pose);
    const SupportingFace second_face = posed_face(second.supporting_face(rotate(conjugate(second_pose.orientation), -normal)), second_pose);
    if (const std::optional<ContactManifold> faces = face_manifold(first_face, second_face, first.margin(), second.margin(), normal, settings, algorithm))
    {
        return faces;
    }
    return point_manifold(transform_point(first_pose, first.support(rotate(conjugate(first_pose.orientation), normal))),
                          transform_point(second_pose, second.support(rotate(conjugate(second_pose.orientation), -normal))), normal, best_face.separation,
                          first.margin(), second.margin(), contact_features::point_contact(first_face.face, second_face.face), settings, algorithm);
}

} // namespace gfx::physics
