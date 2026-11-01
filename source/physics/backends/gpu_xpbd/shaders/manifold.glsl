// Manifolds from the features two shapes present to each other (a port of clipping.cppm and of reduce_contact_points() in
// contact.cppm): the reference face clips the incident one (Sutherland–Hodgman, or a segment parametrically), the points
// below the reference plane up to the speculative distance become contacts with feature ids, reduced to four; one point
// from the closest points otherwise. And the general convex path (convex.cppm's collide_convex_general).
#ifndef GPU_XPBD_MANIFOLD_GLSL
#define GPU_XPBD_MANIFOLD_GLSL

#include "epa.glsl"

// Points a clipped polygon can have: the incident polygon's plus one per reference side (2 × MAXIMUM_FACE_VERTICES, given
// as a constant of its own: glslang checks constant indices into an array sized by a constant expression as if it held
// one element).
layout(constant_id = 38) const uint MAXIMUM_CLIPPED = 16U;

// No incident edge: the segment after the vertex runs along a reference side plane.
const uint NO_EDGE = 0xFFFFFFFFU;

// Two polygons being clipped (one read, one written), each vertex with its feature tag and the incident edge the
// segment to the next vertex lies on.
vec3 clip_positions[2][MAXIMUM_CLIPPED];
uint clip_tags[2][MAXIMUM_CLIPPED];
uint clip_edges[2][MAXIMUM_CLIPPED];
uint clip_counts[2];

// The candidate contact points of a face manifold (or of two manifolds merging).
vec3 candidate_positions[MAXIMUM_CLIPPED];
float candidate_separations[MAXIMUM_CLIPPED];
uint candidate_features[MAXIMUM_CLIPPED];
uint candidate_sub_first[MAXIMUM_CLIPPED];
uint candidate_sub_second[MAXIMUM_CLIPPED];
uint candidate_count;

// Appends the points of manifold to the candidates.
void add_candidates(Manifold manifold)
{
    for (uint point = 0U; point < manifold.count; ++point)
    {
        candidate_positions[candidate_count] = manifold.positions[point];
        candidate_separations[candidate_count] = manifold.separations[point];
        candidate_features[candidate_count] = manifold.features[point];
        candidate_sub_first[candidate_count] = manifold.point_sub_first[point];
        candidate_sub_second[candidate_count] = manifold.point_sub_second[point];
        ++candidate_count;
    }
}

// Candidate index as the point index of result.
void take_candidate(uint index, uint point)
{
    result.positions[point] = candidate_positions[index];
    result.separations[point] = candidate_separations[index];
    result.features[point] = candidate_features[index];
    result.point_sub_first[point] = candidate_sub_first[index];
    result.point_sub_second[point] = candidate_sub_second[index];
}

// The part of polygon source on the inner side of the plane through origin with outward normal (side side), into target.
void clip_polygon(uint source, vec3 origin, vec3 normal, uint side, uint target)
{
    clip_counts[target] = 0U;
    const uint count = clip_counts[source];
    for (uint index = 0U; index < count && clip_counts[target] + 2U <= MAXIMUM_CLIPPED; ++index)
    {
        const uint next = (index + 1U) % count;
        const vec3 from = clip_positions[source][index];
        const vec3 to = clip_positions[source][next];
        const float from_distance = dot(normal, from - origin);
        const float to_distance = dot(normal, to - origin);
        if (from_distance <= 0.0)
        {
            const uint place = clip_counts[target]++;
            clip_positions[target][place] = from;
            clip_tags[target][place] = clip_tags[source][index];
            clip_edges[target][place] = clip_edges[source][index];
        }
        if ((from_distance <= 0.0) != (to_distance <= 0.0))
        {
            // The crossing: on the incident edge the segment lies on, or at the reference corner where the sides meet.
            // Entering, the following segment stays on that edge.
            const float t = from_distance / (from_distance - to_distance);
            const bool entering = from_distance > 0.0;
            const uint edge = clip_edges[source][index];
            const uint place = clip_counts[target]++;
            clip_positions[target][place] = from + (to - from) * t;
            clip_tags[target][place] = edge != NO_EDGE ? clipped_edge(edge, side) : reference_corner(side);
            clip_edges[target][place] = entering ? edge : NO_EDGE;
        }
    }
}

// The squared length of offset in the plane perpendicular to normal, and the signed area (twice) of the triangle spanned by
// base and offset seen along normal, rounded as the CPU rounds them (precise: no fused multiply-adds — a point on the base's
// line spans exactly 0, as on the CPU, where a fused residue's sign would decide whether a duplicate point is kept).
float flat_length_squared(vec3 offset, vec3 normal)
{
    precise const float along = offset.x * normal.x + offset.y * normal.y + offset.z * normal.z;
    precise const vec3 flat_part = vec3(offset.x - normal.x * along, offset.y - normal.y * along, offset.z - normal.z * along);
    precise const float squared = flat_part.x * flat_part.x + flat_part.y * flat_part.y + flat_part.z * flat_part.z;
    return squared;
}

float spanned_area(vec3 base, vec3 offset, vec3 normal)
{
    precise const vec3 crossed =
        vec3(base.y * offset.z - base.z * offset.y, base.z * offset.x - base.x * offset.z, base.x * offset.y - base.y * offset.x);
    precise const float area = crossed.x * normal.x + crossed.y * normal.y + crossed.z * normal.z;
    return area;
}

// The candidates reduced into result: the deepest, the farthest from it, the one spanning the largest triangle with
// them, the one farthest on the other side of their line (in the plane perpendicular to normal).
void reduce_contact_points(vec3 normal)
{
    result.count = 0U;
    if (candidate_count <= MANIFOLD_POINTS)
    {
        for (uint index = 0U; index < candidate_count; ++index)
        {
            take_candidate(index, result.count);
            ++result.count;
        }
        return;
    }
    uint deepest = 0U;
    for (uint index = 1U; index < candidate_count; ++index)
    {
        deepest = candidate_separations[index] < candidate_separations[deepest] ? index : deepest;
    }
    const vec3 deepest_position = candidate_positions[deepest];
    uint farthest = deepest == 0U ? 1U : 0U;
    for (uint index = 0U; index < candidate_count; ++index)
    {
        if (flat_length_squared(candidate_positions[index] - deepest_position, normal) >
            flat_length_squared(candidate_positions[farthest] - deepest_position, normal))
        {
            farthest = index;
        }
    }
    const vec3 base = candidate_positions[farthest] - deepest_position;
    const uint NONE = 0xFFFFFFFFU;
    uint widest = NONE;
    float widest_area = 0.0;
    for (uint index = 0U; index < candidate_count; ++index)
    {
        const float area = spanned_area(base, candidate_positions[index] - deepest_position, normal);
        if (index != deepest && index != farthest && (widest == NONE || abs(area) > abs(widest_area)))
        {
            widest = index;
            widest_area = area;
        }
    }
    const uint chosen[3] = uint[3](deepest, farthest, widest);
    for (uint index = 0U; index < 3U; ++index)
    {
        take_candidate(chosen[index], index);
    }
    result.count = 3U;
    const float side = widest_area >= 0.0 ? 1.0 : -1.0;
    uint opposite = NONE;
    float opposite_area = 0.0;
    for (uint index = 0U; index < candidate_count; ++index)
    {
        const float area = spanned_area(base, candidate_positions[index] - deepest_position, normal);
        if (index != deepest && index != farthest && index != widest && -side * area > 0.0 && (opposite == NONE || -side * area > -side * opposite_area))
        {
            opposite = index;
            opposite_area = area;
        }
    }
    if (opposite != NONE)
    {
        take_candidate(opposite, 3U);
        result.count = 4U;
    }
}

// The manifold of the faces in slots 0 (the first shape's) and 1 (the second's), world space, for normal (first to
// second); false when no polygon can be the reference or no point is within the speculative distance.
bool face_manifold(float first_margin, float second_margin, vec3 normal, float speculative_distance, uint algorithm)
{
    // The reference: a polygon facing the other shape, the first's unless the second's is clearly better aligned.
    const bool first_polygon = face_counts[0] >= 3U && face_counts[1] >= 2U;
    const bool second_polygon = face_counts[1] >= 3U && face_counts[0] >= 2U;
    if (!first_polygon && !second_polygon)
    {
        return false;
    }
    const float first_alignment = first_polygon ? dot(face_normals[0], normal) : -1.0;
    const float second_alignment = second_polygon ? dot(face_normals[1], -normal) : -1.0;
    const bool first_is_reference = first_alignment + 0.01 >= second_alignment;
    const uint reference = first_is_reference ? 0U : 1U;
    const uint incident = first_is_reference ? 1U : 0U;
    const float reference_margin = first_is_reference ? first_margin : second_margin;
    const float incident_margin = first_is_reference ? second_margin : first_margin;
    if (!(max(first_alignment, second_alignment) > 0.5))
    {
        return false;
    }

    // The incident polygon clipped by the reference's sides; a segment, parametrically.
    const vec3 reference_normal = face_normals[reference];
    const uint reference_count = face_counts[reference];
    uint polygon = 0U;
    clip_counts[0] = 0U;
    if (face_counts[incident] == 2U)
    {
        const vec3 start = face_vertices[incident][0];
        const vec3 end = face_vertices[incident][1];
        const vec3 along = end - start;
        float low = 0.0;
        float high = 1.0;
        uint low_tag = incident_vertex(0U);
        uint high_tag = incident_vertex(1U);
        for (uint side = 0U; side < reference_count && low <= high; ++side)
        {
            const vec3 from = face_vertices[reference][side];
            const vec3 outward = normalized(cross(face_vertices[reference][(side + 1U) % reference_count] - from, reference_normal));
            const float start_distance = dot(outward, start - from);
            const float end_distance = dot(outward, end - from);
            if (start_distance > 0.0 && end_distance > 0.0)
            {
                high = -1.0;
            }
            else if (start_distance > 0.0 || end_distance > 0.0)
            {
                const float t = start_distance / (start_distance - end_distance);
                if (start_distance > 0.0 && t > low)
                {
                    low = t;
                    low_tag = clipped_edge(0U, side);
                }
                else if (end_distance > 0.0 && t < high)
                {
                    high = t;
                    high_tag = clipped_edge(0U, side);
                }
            }
        }
        if (low <= high)
        {
            clip_positions[0][0] = start + along * low;
            clip_tags[0][0] = low_tag;
            clip_positions[0][1] = start + along * high;
            clip_tags[0][1] = high_tag;
            clip_counts[0] = 2U;
        }
    }
    else
    {
        for (uint index = 0U; index < face_counts[incident]; ++index)
        {
            clip_positions[0][index] = face_vertices[incident][index];
            clip_tags[0][index] = incident_vertex(index);
            clip_edges[0][index] = index;
        }
        clip_counts[0] = face_counts[incident];
        for (uint side = 0U; side < reference_count && clip_counts[polygon] > 0U; ++side)
        {
            const vec3 from = face_vertices[reference][side];
            const vec3 to = face_vertices[reference][(side + 1U) % reference_count];
            const vec3 outward = normalized(cross(to - from, reference_normal));
            clip_polygon(polygon, from, outward, side, 1U - polygon);
            polygon = 1U - polygon;
        }
    }

    // The points below the reference plane (and those just above, up to the speculative distance) become contacts.
    candidate_count = 0U;
    const vec3 reference_origin = face_vertices[reference][0];
    for (uint index = 0U; index < clip_counts[polygon]; ++index)
    {
        const vec3 point = clip_positions[polygon][index];
        const float height = dot(reference_normal, point - reference_origin);
        const float separation = height - reference_margin - incident_margin;
        if (separation > speculative_distance)
        {
            continue;
        }
        const vec3 on_reference = point - reference_normal * (height - reference_margin);
        const vec3 on_incident = point - reference_normal * incident_margin;
        candidate_positions[candidate_count] = (on_reference + on_incident) * 0.5;
        candidate_separations[candidate_count] = separation;
        candidate_features[candidate_count] = face_contact(face_features[reference], face_features[incident], clip_tags[polygon][index], first_is_reference);
        candidate_sub_first[candidate_count] = NO_SUB_SHAPE;
        candidate_sub_second[candidate_count] = NO_SUB_SHAPE;
        ++candidate_count;
    }
    if (candidate_count == 0U)
    {
        return false;
    }
    result = empty_manifold();
    result.normal = first_is_reference ? reference_normal : -reference_normal;
    result.algorithm = algorithm;
    reduce_contact_points(result.normal);
    return true;
}

// A manifold of one point between the core points whose cores are core_separation apart along normal; false beyond the
// speculative distance.
bool point_manifold(vec3 first_point, vec3 second_point, vec3 normal, float core_separation, float first_margin, float second_margin, uint feature,
                    float speculative_distance, uint algorithm)
{
    const float separation = core_separation - first_margin - second_margin;
    if (separation > speculative_distance)
    {
        return false;
    }
    result = empty_manifold();
    result.normal = normal;
    result.count = 1U;
    result.algorithm = algorithm;
    result.positions[0] = ((first_point + normal * first_margin) + (second_point - normal * second_margin)) * 0.5;
    result.separations[0] = separation;
    result.features[0] = feature;
    return true;
}

// The manifold of the pair for the contact GJK or EPA found: face clipping when polygons face each other, else one point.
bool contact_manifold(vec3 normal, vec3 first_point, vec3 second_point, float core_separation, float speculative_distance, uint algorithm)
{
    const float first_margin = shape_first.margin;
    const float second_margin = shape_second.margin;
    if (core_separation - first_margin - second_margin > speculative_distance)
    {
        return false;
    }
    posed_supporting_face(shape_first, pose_first, normal, 0U);
    posed_supporting_face(shape_second, pose_second, -normal, 1U);
    if (face_manifold(first_margin, second_margin, normal, speculative_distance, algorithm))
    {
        return true;
    }
    return point_manifold(first_point, second_point, normal, core_separation, first_margin, second_margin, point_contact(face_features[0], face_features[1]),
                          speculative_distance, algorithm);
}

// The general convex path: GJK on the cores (shallow contacts: their distance against the margins), EPA when they
// overlap, the manifold from face clipping.
bool collide_convex_general(float speculative_distance)
{
    const GjkResult closest = gjk();
    const float margins = shape_first.margin + shape_second.margin;
    if (!closest.overlapping)
    {
        if (closest.distance - margins > speculative_distance)
        {
            return false;
        }
        if (closest.distance > 1e-6)
        {
            const vec3 normal = (closest.point_second - closest.point_first) / closest.distance;
            return contact_manifold(normal, closest.point_first, closest.point_second, closest.distance, speculative_distance, ALGORITHM_GJK);
        }
    }
    const EpaResult deepest = epa(closest);
    return contact_manifold(deepest.normal, deepest.point_first, deepest.point_second, -deepest.depth, speculative_distance, ALGORITHM_EPA);
}

#endif
