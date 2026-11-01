// The pairs with exact answers cheaper than GJK (a port of analytic.cppm): sphere–sphere, sphere–capsule, capsule–capsule
// (two points when the segments lie side by side), sphere–box, and the separating axis test for box–box and
// triangle–triangle (face axes preferred to edge axes); and the dispatch of two convex pieces (collide_convex()).
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

// The two polytopes' vertices, face normals and edge directions, world space (a box: 8 corners, 3 normals, its edges along
// them; a triangle: 3 vertices, its normal, its 3 edges), as sat_detail::features_of().
vec3 sat_corners[2][8];
uint sat_corner_counts[2];
vec3 sat_face_axes[2][3];
uint sat_face_axis_counts[2];
vec3 sat_edge_axes[2][3];

void sat_features(uint slot, ConvexShape shape, Pose pose)
{
    if (shape.kind == SHAPE_BOX)
    {
        for (uint corner = 0U; corner < 8U; ++corner)
        {
            const vec3 local = vec3((corner & 1U) != 0U ? shape.size.x : -shape.size.x, (corner & 2U) != 0U ? shape.size.y : -shape.size.y,
                                    (corner & 4U) != 0U ? shape.size.z : -shape.size.z);
            sat_corners[slot][corner] = transform_point(pose, local);
        }
        sat_corner_counts[slot] = 8U;
        sat_face_axes[slot][0] = rotate_vector(pose.orientation, vec3(1.0, 0.0, 0.0));
        sat_face_axes[slot][1] = rotate_vector(pose.orientation, vec3(0.0, 1.0, 0.0));
        sat_face_axes[slot][2] = rotate_vector(pose.orientation, vec3(0.0, 0.0, 1.0));
        sat_face_axis_counts[slot] = 3U;
        for (uint edge = 0U; edge < 3U; ++edge)
        {
            sat_edge_axes[slot][edge] = sat_face_axes[slot][edge];
        }
        return;
    }
    sat_corners[slot][0] = transform_point(pose, shape.corner_a);
    sat_corners[slot][1] = transform_point(pose, shape.corner_b);
    sat_corners[slot][2] = transform_point(pose, shape.corner_c);
    sat_corner_counts[slot] = 3U;
    sat_face_axes[slot][0] = rotate_vector(pose.orientation, triangle_normal(shape.corner_a, shape.corner_b, shape.corner_c));
    sat_face_axis_counts[slot] = 1U;
    for (uint edge = 0U; edge < 3U; ++edge)
    {
        sat_edge_axes[slot][edge] = normalized(sat_corners[slot][(edge + 1U) % 3U] - sat_corners[slot][edge]);
    }
}

// The second polytope's separation from the first along axis, oriented the way it is larger (w: the separation).
vec4 separation_along(vec3 axis)
{
    float first_low = float_maximum();
    float first_high = -float_maximum();
    float second_low = float_maximum();
    float second_high = -float_maximum();
    // Constant bounds (the loops unroll, the corners stay in registers), the counts as guards.
    for (uint corner = 0U; corner < 8U; ++corner)
    {
        if (corner < sat_corner_counts[0])
        {
            first_low = min(first_low, dot(sat_corners[0][corner], axis));
            first_high = max(first_high, dot(sat_corners[0][corner], axis));
        }
        if (corner < sat_corner_counts[1])
        {
            second_low = min(second_low, dot(sat_corners[1][corner], axis));
            second_high = max(second_high, dot(sat_corners[1][corner], axis));
        }
    }
    const float forward = second_low - first_high;
    const float backward = first_low - second_high;
    return forward >= backward ? vec4(axis, forward) : vec4(-axis, backward);
}

// The edge of a box parallel to its edge (axis) farthest along direction, or a triangle's edge (world space).
Segment supporting_edge(ConvexShape shape, Pose pose, uint edge, vec3 direction)
{
    if (shape.kind != SHAPE_BOX)
    {
        return Segment(transform_point(pose, triangle_corner(shape, edge)), transform_point(pose, triangle_corner(shape, (edge + 1U) % 3U)));
    }
    const vec3 corner = shape_support(shape, rotate_vector(conjugate_rotation(pose.orientation), direction));
    vec3 start = corner;
    vec3 end = corner;
    if (edge == 0U)
    {
        start.x = -shape.size.x;
        end.x = shape.size.x;
    }
    else if (edge == 1U)
    {
        start.y = -shape.size.y;
        end.y = shape.size.y;
    }
    else
    {
        start.z = -shape.size.z;
        end.z = shape.size.z;
    }
    return Segment(transform_point(pose, start), transform_point(pose, end));
}

// The diagonal of a polytope's box in its own frame (its bounds' extents).
float sat_scale(ConvexShape shape)
{
    if (shape.kind == SHAPE_BOX)
    {
        return vector_length(2.0 * (shape.size + vec3(shape.margin)));
    }
    const Aabb bounds = triangle_bounds(shape.corner_a, shape.corner_b, shape.corner_c);
    return vector_length(bounds.maximum - bounds.minimum);
}

// The separating axis test between two boxes or two triangles (collide_separating_axes): the axis of least penetration
// among the face normals and the edges' cross products; a face axis is kept unless an edge axis separates clearly more
// (then the edges' closest points make one contact).
bool collide_separating_axes(ConvexShape first, Pose first_pose, ConvexShape second, Pose second_pose, float speculative_distance, uint algorithm)
{
    sat_features(0U, first, first_pose);
    sat_features(1U, second, second_pose);
    const float margins = first.margin + second.margin;
    const float limit = speculative_distance + margins;

    // The faces' axes.
    vec4 best_face = vec4(0.0, 0.0, 0.0, -float_maximum());
    for (uint slot = 0U; slot < 2U; ++slot)
    {
        for (uint index = 0U; index < 3U; ++index)
        {
            if (index >= sat_face_axis_counts[slot])
            {
                break;
            }
            const vec4 candidate = separation_along(sat_face_axes[slot][index]);
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
            const vec3 across = cross(sat_edge_axes[0][i], sat_edge_axes[1][j]);
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
    const float scale = max(sat_scale(first), sat_scale(second));
    if (best_edge.w > best_face.w + 0.05 * abs(best_face.w) + 1e-3 * scale)
    {
        const Segment on_first = supporting_edge(first, first_pose, first_edge, best_edge.xyz);
        const Segment on_second = supporting_edge(second, second_pose, second_edge, -best_edge.xyz);
        vec3 first_point;
        vec3 second_point;
        segment_closest_points(on_first, on_second, first_point, second_point);
        return point_manifold(first_point, second_point, best_edge.xyz, best_edge.w, first.margin, second.margin,
                              point_contact(64U + first_edge, 64U + second_edge), speculative_distance, algorithm);
    }
    const vec3 normal = best_face.xyz;
    posed_supporting_face(first, first_pose, normal, 0U);
    posed_supporting_face(second, second_pose, -normal, 1U);
    if (face_manifold(first.margin, second.margin, normal, speculative_distance, algorithm))
    {
        return true;
    }
    return point_manifold(posed_support(first, first_pose, normal), posed_support(second, second_pose, -normal), normal, best_face.w, first.margin,
                          second.margin, point_contact(face_features[0], face_features[1]), speculative_distance, algorithm);
}

// The manifold of two convex pieces at their poses, as collide_convex() dispatches it: the analytic pairs in either order
// (flipped for the reversed order), separating axes for two boxes and for two triangles, GJK, EPA and clipping otherwise.
// The result in `result`; the pieces in shape_first/second and pose_first/second.
bool collide_pieces(float speculative_distance)
{
    result = empty_manifold();
    const uint a = shape_first.kind;
    const uint b = shape_second.kind;
    if (a == SHAPE_SPHERE && b == SHAPE_SPHERE)
    {
        return collide_spheres(shape_first, pose_first, shape_second, pose_second, speculative_distance);
    }
    if (a == SHAPE_SPHERE && b == SHAPE_CAPSULE)
    {
        return collide_sphere_capsule(shape_first, pose_first, shape_second, pose_second, speculative_distance);
    }
    if (a == SHAPE_CAPSULE && b == SHAPE_SPHERE)
    {
        const bool found = collide_sphere_capsule(shape_second, pose_second, shape_first, pose_first, speculative_distance);
        if (found)
        {
            flip_manifold(result);
        }
        return found;
    }
    if (a == SHAPE_CAPSULE && b == SHAPE_CAPSULE)
    {
        return collide_capsules(shape_first, pose_first, shape_second, pose_second, speculative_distance);
    }
    if (a == SHAPE_SPHERE && b == SHAPE_BOX)
    {
        return collide_sphere_box(shape_first, pose_first, shape_second, pose_second, speculative_distance);
    }
    if (a == SHAPE_BOX && b == SHAPE_SPHERE)
    {
        const bool found = collide_sphere_box(shape_second, pose_second, shape_first, pose_first, speculative_distance);
        if (found)
        {
            flip_manifold(result);
        }
        return found;
    }
    if ((a == SHAPE_BOX && b == SHAPE_BOX) || (a == SHAPE_TRIANGLE_MESH && b == SHAPE_TRIANGLE_MESH))
    {
        // One call for both (GLSL inlines every call).
        return collide_separating_axes(shape_first, pose_first, shape_second, pose_second, speculative_distance,
                                       a == SHAPE_BOX ? ALGORITHM_BOX_BOX : ALGORITHM_TRIANGLE_TRIANGLE);
    }
    return collide_convex_general(speculative_distance);
}

#endif
