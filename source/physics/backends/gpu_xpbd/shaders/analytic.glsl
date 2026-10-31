// The pairs with exact answers cheaper than GJK (a port of analytic.cppm): sphere–sphere, sphere–capsule, capsule–capsule
// (two points when the segments lie side by side), sphere–box, and the separating axis test for box–box (face axes preferred
// to edge axes). Each takes its shapes in its own order; the collide pass flips the result for the other.
#ifndef GPU_XPBD_ANALYTIC_GLSL
#define GPU_XPBD_ANALYTIC_GLSL

#include "manifold.glsl"

// The manifold of the core points first and second with margins: one point, the normal along the line between them (or
// fallback when they coincide).
bool between_points(vec3 first, vec3 second, float first_margin, float second_margin, vec3 fallback, uint feature, float speculative_distance,
                    uint algorithm)
{
    const vec3 gap = second - first;
    const float distance = vector_length(gap);
    const vec3 normal = distance > 1e-9 ? gap / distance : fallback;
    return point_manifold(first, second, normal, distance, first_margin, second_margin, feature, speculative_distance, algorithm);
}

// A capsule's segment at pose.
Segment capsule_segment(ConvexShape capsule, Pose pose)
{
    return Segment(transform_point(pose, vec3(0.0, -capsule.size.x, 0.0)), transform_point(pose, vec3(0.0, capsule.size.x, 0.0)));
}

// The direction perpendicular to axis nearest to preferred (any perpendicular when they are parallel).
vec3 perpendicular_toward(vec3 axis, vec3 preferred)
{
    const vec3 flat_part = preferred - axis * dot(preferred, axis);
    return length_squared(flat_part) > 1e-12 ? normalized(flat_part) : any_perpendicular(axis);
}

bool collide_spheres(ConvexShape first, Pose first_pose, ConvexShape second, Pose second_pose, float speculative_distance)
{
    return between_points(first_pose.position, second_pose.position, first.margin, second.margin, vec3(0.0, 1.0, 0.0), point_contact(0U, 0U),
                          speculative_distance, ALGORITHM_SPHERE_SPHERE);
}

// The sphere's center against the closest point of the capsule's segment.
bool collide_sphere_capsule(ConvexShape sphere, Pose sphere_pose, ConvexShape capsule, Pose capsule_pose, float speculative_distance)
{
    const Segment axis = capsule_segment(capsule, capsule_pose);
    const vec3 closest = segment_closest_point(axis, sphere_pose.position);
    const vec3 fallback = perpendicular_toward(normalized(axis.end - axis.start), vec3(0.0, -1.0, 0.0));
    return between_points(sphere_pose.position, closest, sphere.margin, capsule.margin, fallback, point_contact(0U, 0U), speculative_distance,
                          ALGORITHM_SPHERE_CAPSULE);
}

// The closest points of the segments; two points when they are parallel and overlap along their length.
bool collide_capsules(ConvexShape first, Pose first_pose, ConvexShape second, Pose second_pose, float speculative_distance)
{
    const Segment a = capsule_segment(first, first_pose);
    const Segment b = capsule_segment(second, second_pose);
    vec3 first_point;
    vec3 second_point;
    segment_closest_points(a, b, first_point, second_point);
    const vec3 a_direction = a.end - a.start;
    const vec3 b_direction = b.end - b.start;
    const vec3 axis = length_squared(a_direction) > 0.0 ? normalized(a_direction) : vec3(0.0, 1.0, 0.0);
    const vec3 fallback = perpendicular_toward(axis, second_pose.position - first_pose.position);
    const bool single = between_points(first_point, second_point, first.margin, second.margin, fallback, point_contact(0U, 0U), speculative_distance,
                                       ALGORITHM_CAPSULE_CAPSULE);
    if (!single || length_squared(b_direction) <= 0.0 || length_squared(a_direction) <= 0.0)
    {
        return single;
    }

    // Parallel segments: the part of b over a, its two ends each against their closest point of a.
    const vec3 b_axis = normalized(b_direction);
    if (abs(dot(axis, b_axis)) < 0.995)
    {
        return single;
    }
    const float length_a = vector_length(a_direction);
    const float b_start = clamp(dot(b.start - a.start, axis) / length_a, 0.0, 1.0);
    const float b_end = clamp(dot(b.end - a.start, axis) / length_a, 0.0, 1.0);
    const float low = min(b_start, b_end);
    const float high = max(b_start, b_end);
    if ((high - low) * length_a < 1e-3 * (first.margin + second.margin))
    {
        return single;
    }
    const Manifold single_result = result;
    result = empty_manifold();
    result.normal = single_result.normal;
    result.algorithm = ALGORITHM_CAPSULE_CAPSULE;
    const float fractions[2] = float[2](low, high);
    for (uint end = 0U; end < 2U; ++end)
    {
        const vec3 on_a = segment_at(a, fractions[end]);
        const vec3 on_b = segment_closest_point(b, on_a);
        const float separation = dot(on_b - on_a, result.normal) - first.margin - second.margin;
        if (separation <= speculative_distance)
        {
            result.positions[result.count] = ((on_a + result.normal * first.margin) + (on_b - result.normal * second.margin)) * 0.5;
            result.separations[result.count] = separation;
            result.features[result.count] = point_contact(end + 1U, 0U);
            ++result.count;
        }
    }
    if (result.count == 0U)
    {
        result = single_result;
    }
    return true;
}

// The box's closest point to the sphere's center; a center inside leaves by the nearest face. The feature is the box
// region the center is in (27: inside, faces, edges, corners).
bool collide_sphere_box(ConvexShape sphere, Pose sphere_pose, ConvexShape box, Pose box_pose, float speculative_distance)
{
    const vec3 center = rotate_vector(conjugate_rotation(box_pose.orientation), sphere_pose.position - box_pose.position);
    const vec3 h = box.size;
    const vec3 clamped = vec3(clamp(center.x, -h.x, h.x), clamp(center.y, -h.y, h.y), clamp(center.z, -h.z, h.z));
    const uint region_x = center.x < -h.x ? 0U : center.x > h.x ? 2U : 1U;
    const uint region_y = center.y < -h.y ? 0U : center.y > h.y ? 2U : 1U;
    const uint region_z = center.z < -h.z ? 0U : center.z > h.z ? 2U : 1U;
    const uint feature = region_x + 3U * region_y + 9U * region_z;
    if (any(notEqual(clamped, center)))
    {
        // Outside: from the box's closest point to the center.
        const vec3 outward = normalized(center - clamped);
        return between_points(sphere_pose.position, transform_point(box_pose, clamped), sphere.margin, box.margin,
                              rotate_vector(box_pose.orientation, -outward), point_contact(0U, feature), speculative_distance, ALGORITHM_SPHERE_BOX);
    }

    // Inside: out through the nearest face.
    const float depth[3] = float[3](h.x - abs(center.x), h.y - abs(center.y), h.z - abs(center.z));
    const uint axis = depth[0] <= depth[1] && depth[0] <= depth[2] ? 0U : depth[1] <= depth[2] ? 1U : 2U;
    const float coordinates[3] = float[3](center.x, center.y, center.z);
    float face[3] = float[3](0.0, 0.0, 0.0);
    face[axis] = coordinates[axis] >= 0.0 ? 1.0 : -1.0;
    const vec3 outward = rotate_vector(box_pose.orientation, vec3(face[0], face[1], face[2]));
    const vec3 on_face = sphere_pose.position + outward * depth[axis];
    return point_manifold(sphere_pose.position, on_face, -outward, -depth[axis], sphere.margin, box.margin,
                          point_contact(0U, 27U + axis * 2U + (face[axis] > 0.0 ? 1U : 0U)), speculative_distance, ALGORITHM_SPHERE_BOX);
}

// ===================================================================================================== separating axes

// The two boxes' corners, face normals (their edges' directions too), world space.
vec3 box_corners[2][8];
vec3 box_axes[2][3];

void box_features(uint slot, ConvexShape box, Pose pose)
{
    for (uint corner = 0U; corner < 8U; ++corner)
    {
        const vec3 local = vec3((corner & 1U) != 0U ? box.size.x : -box.size.x, (corner & 2U) != 0U ? box.size.y : -box.size.y,
                                (corner & 4U) != 0U ? box.size.z : -box.size.z);
        box_corners[slot][corner] = transform_point(pose, local);
    }
    box_axes[slot][0] = rotate_vector(pose.orientation, vec3(1.0, 0.0, 0.0));
    box_axes[slot][1] = rotate_vector(pose.orientation, vec3(0.0, 1.0, 0.0));
    box_axes[slot][2] = rotate_vector(pose.orientation, vec3(0.0, 0.0, 1.0));
}

// The second box's separation from the first along axis, oriented the way it is larger (w: the separation).
vec4 separation_along(vec3 axis)
{
    float first_low = float_maximum();
    float first_high = -float_maximum();
    float second_low = float_maximum();
    float second_high = -float_maximum();
    for (uint corner = 0U; corner < 8U; ++corner)
    {
        first_low = min(first_low, dot(box_corners[0][corner], axis));
        first_high = max(first_high, dot(box_corners[0][corner], axis));
        second_low = min(second_low, dot(box_corners[1][corner], axis));
        second_high = max(second_high, dot(box_corners[1][corner], axis));
    }
    const float forward = second_low - first_high;
    const float backward = first_low - second_high;
    return forward >= backward ? vec4(axis, forward) : vec4(-axis, backward);
}

// The edge of a box parallel to its edge (axis) that is farthest along direction (world space).
Segment supporting_edge(ConvexShape box, Pose pose, uint edge, vec3 direction)
{
    const vec3 corner = shape_support(box, rotate_vector(conjugate_rotation(pose.orientation), direction));
    vec3 start = corner;
    vec3 end = corner;
    if (edge == 0U)
    {
        start.x = -box.size.x;
        end.x = box.size.x;
    }
    else if (edge == 1U)
    {
        start.y = -box.size.y;
        end.y = box.size.y;
    }
    else
    {
        start.z = -box.size.z;
        end.z = box.size.z;
    }
    return Segment(transform_point(pose, start), transform_point(pose, end));
}

// The axis of least penetration among the face normals and the edges' cross products; a face axis is kept unless an
// edge axis separates clearly more (then the edges' closest points make one contact).
bool collide_boxes(ConvexShape first, Pose first_pose, ConvexShape second, Pose second_pose, float speculative_distance)
{
    box_features(0U, first, first_pose);
    box_features(1U, second, second_pose);
    const float margins = first.margin + second.margin;
    const float limit = speculative_distance + margins;

    // The faces' axes.
    vec4 best_face = vec4(0.0, 0.0, 0.0, -float_maximum());
    for (uint slot = 0U; slot < 2U; ++slot)
    {
        for (uint index = 0U; index < 3U; ++index)
        {
            const vec4 candidate = separation_along(box_axes[slot][index]);
            if (candidate.w > limit)
            {
                return false;
            }
            best_face = candidate.w > best_face.w ? candidate : best_face;
        }
    }

    // The edges' axes (parallel edges give none).
    vec4 best_edge = vec4(0.0, 0.0, 0.0, -float_maximum());
    uint first_edge = 0U;
    uint second_edge = 0U;
    for (uint i = 0U; i < 3U; ++i)
    {
        for (uint j = 0U; j < 3U; ++j)
        {
            const vec3 across = cross(box_axes[0][i], box_axes[1][j]);
            if (length_squared(across) < 1e-6)
            {
                continue;
            }
            const vec4 candidate = separation_along(normalized(across));
            if (candidate.w > limit)
            {
                return false;
            }
            if (candidate.w > best_edge.w)
            {
                best_edge = candidate;
                first_edge = i;
                second_edge = j;
            }
        }
    }

    // A face contact unless an edge axis separates clearly more (faces give stable manifolds).
    const float scale = max(vector_length(2.0 * (first.size + vec3(first.margin))), vector_length(2.0 * (second.size + vec3(second.margin))));
    if (best_edge.w > best_face.w + 0.05 * abs(best_face.w) + 1e-3 * scale)
    {
        const Segment on_first = supporting_edge(first, first_pose, first_edge, best_edge.xyz);
        const Segment on_second = supporting_edge(second, second_pose, second_edge, -best_edge.xyz);
        vec3 first_point;
        vec3 second_point;
        segment_closest_points(on_first, on_second, first_point, second_point);
        return point_manifold(first_point, second_point, best_edge.xyz, best_edge.w, first.margin, second.margin,
                              point_contact(64U + first_edge, 64U + second_edge), speculative_distance, ALGORITHM_BOX_BOX);
    }
    const vec3 normal = best_face.xyz;
    posed_supporting_face(first, first_pose, normal, 0U);
    posed_supporting_face(second, second_pose, -normal, 1U);
    if (face_manifold(first.margin, second.margin, normal, speculative_distance, ALGORITHM_BOX_BOX))
    {
        return true;
    }
    return point_manifold(posed_support(first, first_pose, normal), posed_support(second, second_pose, -normal), normal, best_face.w, first.margin,
                          second.margin, point_contact(face_features[0], face_features[1]), speculative_distance, ALGORITHM_BOX_BOX);
}

#endif
