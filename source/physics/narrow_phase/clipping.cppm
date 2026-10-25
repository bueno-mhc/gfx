/// @file clipping.cppm
/// @brief Manifolds from the features two shapes present to each other: the reference face (the polygon best aligned
///        with the contact normal) clips the incident one (a polygon or a segment) against its side planes
///        (Sutherland–Hodgman); the points below the reference plane, up to the speculative distance, become contacts
///        with feature ids. When no polygon faces the other shape (points, edges against edges), one point from the
///        closest points makes the manifold.
export module gfx.physics.narrow_phase:clipping;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.primitives;
import gfx.physics.shapes;
import :contact;

export namespace gfx::physics
{

/// @brief @p face moved by @p pose (vertices and normal).
[[nodiscard]] constexpr auto
posed_face(const SupportingFace& face,
           const Transform& pose) noexcept -> SupportingFace
{
    SupportingFace result = face;
    result.normal = rotate(pose.orientation, face.normal);
    for (Vector3& vertex : std::span{result.vertices}.first(result.count))
    {
        vertex = transform_point(pose, vertex);
    }
    return result;
}

namespace clipping_detail
{

/// No incident edge: the segment after the vertex runs along a reference side plane.
inline constexpr std::uint32_t NO_EDGE = 0xFFFFFFFFU;

/// Points a clipped polygon can have: the incident polygon's plus one per reference side.
inline constexpr std::size_t MAXIMUM_CLIPPED = 2 * MAXIMUM_FACE_VERTICES;

/// A vertex being clipped: where it is, its feature tag, and the incident edge the segment to the next vertex lies on.
struct ClipVertex
{
    Vector3 position{};
    std::uint32_t tag = 0;
    std::uint32_t edge_after = NO_EDGE;
};

/// A polygon being clipped.
struct ClipPolygon
{
    std::array<ClipVertex, MAXIMUM_CLIPPED> vertices{};
    std::uint32_t count = 0;
};

/// The part of @p input on the inner side of the plane through @p origin with outward normal @p normal (side @p side).
constexpr void
clip_polygon(const ClipPolygon& input,
             Vector3 origin,
             Vector3 normal,
             std::uint32_t side,
             ClipPolygon& output) noexcept
{
    output.count = 0;
    for (std::uint32_t index = 0; index < input.count && output.count + 2 <= MAXIMUM_CLIPPED; ++index)
    {
        const ClipVertex& from = input.vertices[index];
        const ClipVertex& to = input.vertices[(index + 1) % input.count];
        const float from_distance = dot(normal, from.position - origin);
        const float to_distance = dot(normal, to.position - origin);
        if (from_distance <= 0.0F)
        {
            output.vertices[output.count++] = from;
        }
        if ((from_distance <= 0.0F) != (to_distance <= 0.0F))
        {
            // The crossing: on the incident edge the segment lies on, or (a segment along an earlier side) at the
            // reference corner where the sides meet. Entering, the following segment stays on that edge.
            const float t = from_distance / (from_distance - to_distance);
            const bool entering = from_distance > 0.0F;
            const std::uint32_t tag = from.edge_after != NO_EDGE ? contact_features::clipped_edge(from.edge_after, side) : contact_features::reference_corner(side);
            output.vertices[output.count++] = {.position = from.position + (to.position - from.position) * t, .tag = tag, .edge_after = entering ? from.edge_after : NO_EDGE};
        }
    }
}

} // namespace clipping_detail

/// @brief The contact manifold of two shapes whose features facing each other are @p first_face and @p second_face
///        (world space), with margins, for the normal @p normal (first to second); std::nullopt when no polygon can be
///        the reference or no point is within @p settings' speculative distance.
[[nodiscard]] constexpr auto
face_manifold(const SupportingFace& first_face,
              const SupportingFace& second_face,
              float first_margin,
              float second_margin,
              Vector3 normal,
              const CollisionSettings& settings,
              ContactAlgorithm algorithm) noexcept -> std::optional<ContactManifold>
{
    // The reference: a polygon facing the other shape, the first's unless the second's is clearly better aligned.
    const bool first_polygon = first_face.count >= 3 && second_face.count >= 2;
    const bool second_polygon = second_face.count >= 3 && first_face.count >= 2;
    if (!first_polygon && !second_polygon)
    {
        return std::nullopt;
    }
    const float first_alignment = first_polygon ? dot(first_face.normal, normal) : -1.0F;
    const float second_alignment = second_polygon ? dot(second_face.normal, -normal) : -1.0F;
    const bool first_is_reference = first_alignment + 0.01F >= second_alignment;
    const SupportingFace& reference = first_is_reference ? first_face : second_face;
    const SupportingFace& incident = first_is_reference ? second_face : first_face;
    const float reference_margin = first_is_reference ? first_margin : second_margin;
    const float incident_margin = first_is_reference ? second_margin : first_margin;
    if (!(std::max(first_alignment, second_alignment) > 0.5F))
    {
        return std::nullopt;
    }

    // The incident polygon clipped by the reference's sides (Sutherland–Hodgman); a segment, parametrically.
    const Vector3 reference_normal = reference.normal;
    clipping_detail::ClipPolygon polygon{};
    if (incident.count == 2)
    {
        const Vector3 start = incident.vertices[0];
        const Vector3 along = incident.vertices[1] - start;
        float low = 0.0F;
        float high = 1.0F;
        std::uint32_t low_tag = contact_features::incident_vertex(0);
        std::uint32_t high_tag = contact_features::incident_vertex(1);
        for (std::uint32_t side = 0; side < reference.count && low <= high; ++side)
        {
            const Vector3 from = reference.vertices[side];
            const Vector3 outward = normalized(cross(reference.vertices[(side + 1) % reference.count] - from, reference_normal));
            const float start_distance = dot(outward, start - from);
            const float end_distance = dot(outward, incident.vertices[1] - from);
            if (start_distance > 0.0F && end_distance > 0.0F)
            {
                high = -1.0F;
            }
            else if (start_distance > 0.0F || end_distance > 0.0F)
            {
                const float t = start_distance / (start_distance - end_distance);
                if (start_distance > 0.0F && t > low)
                {
                    low = t;
                    low_tag = contact_features::clipped_edge(0, side);
                }
                else if (end_distance > 0.0F && t < high)
                {
                    high = t;
                    high_tag = contact_features::clipped_edge(0, side);
                }
            }
        }
        if (low <= high)
        {
            polygon.vertices[polygon.count++] = {.position = start + along * low, .tag = low_tag};
            polygon.vertices[polygon.count++] = {.position = start + along * high, .tag = high_tag};
        }
    }
    else
    {
        clipping_detail::ClipPolygon clipped{};
        for (std::uint32_t index = 0; index < incident.count; ++index)
        {
            polygon.vertices[polygon.count++] = {.position = incident.vertices[index], .tag = contact_features::incident_vertex(index), .edge_after = index};
        }
        for (std::uint32_t side = 0; side < reference.count && polygon.count > 0; ++side)
        {
            const Vector3 from = reference.vertices[side];
            const Vector3 to = reference.vertices[(side + 1) % reference.count];
            const Vector3 outward = normalized(cross(to - from, reference_normal));
            clipping_detail::clip_polygon(polygon, from, outward, side, clipped);
            polygon = clipped;
        }
    }

    // The points below the reference plane (and those just above, up to the speculative distance) become contacts.
    std::array<ContactPoint, clipping_detail::MAXIMUM_CLIPPED> candidates{};
    std::uint32_t candidate_count = 0;
    for (std::uint32_t index = 0; index < polygon.count; ++index)
    {
        const Vector3 point = polygon.vertices[index].position;
        const float height = dot(reference_normal, point - reference.vertices[0]);
        const float separation = height - reference_margin - incident_margin;
        if (separation > settings.speculative_distance)
        {
            continue;
        }
        const Vector3 on_reference = point - reference_normal * (height - reference_margin);
        const Vector3 on_incident = point - reference_normal * incident_margin;
        candidates[candidate_count++] = {
            .position = (on_reference + on_incident) * 0.5F,
            .separation = separation,
            .feature = {.local = contact_features::face_contact(reference.face, incident.face, polygon.vertices[index].tag, first_is_reference)}};
    }
    if (candidate_count == 0)
    {
        return std::nullopt;
    }
    ContactManifold manifold{.normal = first_is_reference ? reference_normal : -reference_normal, .algorithm = algorithm};
    reduce_contact_points(std::span{candidates}.first(candidate_count), manifold.normal, manifold);
    return manifold;
}

/// @brief A manifold of one point between the core points @p first_point and @p second_point whose cores are
///        @p core_separation apart along @p normal (negative: overlapping); std::nullopt beyond the speculative
///        distance.
[[nodiscard]] constexpr auto
point_manifold(Vector3 first_point,
               Vector3 second_point,
               Vector3 normal,
               float core_separation,
               float first_margin,
               float second_margin,
               std::uint32_t local_feature,
               const CollisionSettings& settings,
               ContactAlgorithm algorithm) noexcept -> std::optional<ContactManifold>
{
    const float separation = core_separation - first_margin - second_margin;
    if (separation > settings.speculative_distance)
    {
        return std::nullopt;
    }
    ContactManifold manifold{.normal = normal, .point_count = 1, .algorithm = algorithm};
    manifold.points[0] = {.position = ((first_point + normal * first_margin) + (second_point - normal * second_margin)) * 0.5F,
                          .separation = separation,
                          .feature = {.local = local_feature}};
    return manifold;
}

/// @brief The manifold of two convex shapes at their poses for the contact GJK or EPA found (normal from first to
///        second, the cores' closest or deepest points, their signed distance): face clipping when polygons face each
///        other, else one point.
template<FacedConvexShape First, FacedConvexShape Second>
[[nodiscard]] constexpr auto
contact_manifold(const First& first,
                 const Transform& first_pose,
                 const Second& second,
                 const Transform& second_pose,
                 Vector3 normal,
                 Vector3 first_point,
                 Vector3 second_point,
                 float core_separation,
                 const CollisionSettings& settings,
                 ContactAlgorithm algorithm) noexcept -> std::optional<ContactManifold>
{
    const float first_margin = first.margin();
    const float second_margin = second.margin();
    if (core_separation - first_margin - second_margin > settings.speculative_distance)
    {
        return std::nullopt;
    }
    const SupportingFace first_face = posed_face(first.supporting_face(rotate(conjugate(first_pose.orientation), normal)), first_pose);
    const SupportingFace second_face = posed_face(second.supporting_face(rotate(conjugate(second_pose.orientation), -normal)), second_pose);
    if (const std::optional<ContactManifold> faces = face_manifold(first_face, second_face, first_margin, second_margin, normal, settings, algorithm))
    {
        return faces;
    }
    return point_manifold(first_point, second_point, normal, core_separation, first_margin, second_margin,
                          contact_features::point_contact(first_face.face, second_face.face), settings, algorithm);
}

} // namespace gfx::physics
