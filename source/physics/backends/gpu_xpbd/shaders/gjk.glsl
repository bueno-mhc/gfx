// GJK between the cores of the pair (narrow_phase.glsl's shape_first/second at pose_first/second): their distance, the
// closest points, and — when they overlap — the simplex EPA starts from. A port of physics::gjk() (gjk.cppm): Voronoi
// regions of Ericson's ClosestPtPointTriangle, the sliver test, the same tolerances and order of operations.
#ifndef GPU_XPBD_GJK_GLSL
#define GPU_XPBD_GJK_GLSL

#include "narrow_phase.glsl"

// A simplex: its points, their barycentric weights, its point closest to the origin.
struct Simplex
{
    SupportPoint points[4];
    float weights[4];
    uint size;
    vec3 closest;
};

void keep_one(inout Simplex simplex, uint first)
{
    const SupportPoint a = simplex.points[first];
    simplex.points[0] = a;
    simplex.weights[0] = 1.0;
    simplex.size = 1U;
    simplex.closest = vec3(0.0) + a.difference * 1.0;
}

void keep_two(inout Simplex simplex, uint first, float first_weight, uint second, float second_weight)
{
    const SupportPoint a = simplex.points[first];
    const SupportPoint b = simplex.points[second];
    simplex.points[0] = a;
    simplex.points[1] = b;
    simplex.weights[0] = first_weight;
    simplex.weights[1] = second_weight;
    simplex.size = 2U;
    simplex.closest = vec3(0.0) + a.difference * first_weight + b.difference * second_weight;
}

void keep_three(inout Simplex simplex, float first_weight, float second_weight, float third_weight)
{
    simplex.weights[0] = first_weight;
    simplex.weights[1] = second_weight;
    simplex.weights[2] = third_weight;
    simplex.size = 3U;
    simplex.closest = vec3(0.0) + simplex.points[0].difference * first_weight + simplex.points[1].difference * second_weight +
                      simplex.points[2].difference * third_weight;
}

void solve_segment(inout Simplex simplex)
{
    const vec3 a = simplex.points[0].difference;
    const vec3 b = simplex.points[1].difference;
    const vec3 ab = b - a;
    const float squared = dot(ab, ab);
    const float t = squared > 0.0 ? -dot(a, ab) / squared : 0.0;
    if (t <= 0.0)
    {
        keep_one(simplex, 0U);
    }
    else if (t >= 1.0)
    {
        keep_one(simplex, 1U);
    }
    else
    {
        keep_two(simplex, 0U, 1.0 - t, 1U, t);
    }
}

void solve_triangle(inout Simplex simplex)
{
    const vec3 a = simplex.points[0].difference;
    const vec3 b = simplex.points[1].difference;
    const vec3 c = simplex.points[2].difference;
    const vec3 ab = b - a;
    const vec3 ac = c - a;
    const float d1 = -dot(ab, a);
    const float d2 = -dot(ac, a);
    if (d1 <= 0.0 && d2 <= 0.0)
    {
        keep_one(simplex, 0U);
        return;
    }
    const float d3 = -dot(ab, b);
    const float d4 = -dot(ac, b);
    if (d3 >= 0.0 && d4 <= d3)
    {
        keep_one(simplex, 1U);
        return;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
    {
        const float v = d1 / (d1 - d3);
        keep_two(simplex, 0U, 1.0 - v, 1U, v);
        return;
    }
    const float d5 = -dot(ab, c);
    const float d6 = -dot(ac, c);
    if (d6 >= 0.0 && d5 <= d6)
    {
        keep_one(simplex, 2U);
        return;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
    {
        const float w = d2 / (d2 - d6);
        keep_two(simplex, 0U, 1.0 - w, 2U, w);
        return;
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0)
    {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        keep_two(simplex, 1U, 1.0 - w, 2U, w);
        return;
    }
    const float denominator = va + vb + vc;
    if (!(denominator > 0.0))
    {
        // A degenerate triangle: the best of its edges.
        simplex.size = 2U;
        solve_segment(simplex);
        return;
    }
    const float v = vb / denominator;
    const float w = vc / denominator;
    keep_three(simplex, 1.0 - v - w, v, w);
}

// Whether the origin and d are on opposite sides of the plane (a, b, c) (degenerate planes count as opposite).
bool origin_outside(vec3 a, vec3 b, vec3 c, vec3 d)
{
    const vec3 normal = cross(b - a, c - a);
    const float origin_side = -dot(a, normal);
    const float other_side = dot(d - a, normal);
    return origin_side * other_side <= 0.0 && !(origin_side == 0.0 && other_side != 0.0);
}

// The faces of a tetrahedron, each with the vertex opposite.
const uvec4 TETRAHEDRON_FACES[4] = uvec4[4](uvec4(0U, 1U, 2U, 3U), uvec4(0U, 2U, 3U, 1U), uvec4(0U, 3U, 1U, 2U), uvec4(1U, 3U, 2U, 0U));

Simplex triangle_of(Simplex simplex, uvec4 face)
{
    Simplex triangle;
    triangle.points[0] = simplex.points[face.x];
    triangle.points[1] = simplex.points[face.y];
    triangle.points[2] = simplex.points[face.z];
    triangle.points[3] = simplex.points[face.z];
    triangle.weights = float[4](0.0, 0.0, 0.0, 0.0);
    triangle.size = 3U;
    triangle.closest = vec3(0.0);
    return triangle;
}

// The tetrahedron: the closest of the faces the origin is outside of; false when the origin is inside.
bool solve_tetrahedron(inout Simplex simplex)
{
    bool outside_any = false;
    float best = float_maximum();
    Simplex chosen = simplex;
    for (uint index = 0U; index < 4U; ++index)
    {
        const uvec4 face = TETRAHEDRON_FACES[index];
        if (!origin_outside(simplex.points[face.x].difference, simplex.points[face.y].difference, simplex.points[face.z].difference,
                            simplex.points[face.w].difference))
        {
            continue;
        }
        outside_any = true;
        Simplex triangle = triangle_of(simplex, face);
        solve_triangle(triangle);
        const float distance = length_squared(triangle.closest);
        if (distance < best)
        {
            best = distance;
            chosen = triangle;
        }
    }
    if (!outside_any)
    {
        return false;
    }
    simplex = chosen;
    return true;
}

// Whether the tetrahedron is a sliver: its volume within the rounding error of its corners (gjk_detail::is_sliver).
bool is_sliver(Simplex simplex)
{
    const float SAFETY = 16.0;
    float largest = 0.0;
    for (uint point = 0U; point < 4U; ++point)
    {
        const vec3 difference = simplex.points[point].difference;
        largest = max(largest, max(abs(difference.x), max(abs(difference.y), abs(difference.z))));
    }
    const vec3 first = simplex.points[1].difference - simplex.points[0].difference;
    const vec3 second = simplex.points[2].difference - simplex.points[0].difference;
    const vec3 third = simplex.points[3].difference - simplex.points[0].difference;
    const float volume = dot(first, cross(second, third));
    const float areas = vector_length(cross(second, third)) + vector_length(cross(third, first)) + vector_length(cross(first, second));
    const float bound = float_epsilon() * largest * areas + float_epsilon() * vector_length(first) * vector_length(second) * vector_length(third);
    return abs(volume) <= SAFETY * bound;
}

// The face of the tetrahedron closest to the origin.
Simplex closest_face(Simplex simplex)
{
    Simplex best = triangle_of(simplex, TETRAHEDRON_FACES[0]);
    float nearest = float_maximum();
    const uvec3 faces[4] = uvec3[4](uvec3(0U, 1U, 2U), uvec3(0U, 2U, 3U), uvec3(0U, 3U, 1U), uvec3(1U, 3U, 2U));
    for (uint index = 0U; index < 4U; ++index)
    {
        Simplex triangle = triangle_of(simplex, uvec4(faces[index], 0U));
        solve_triangle(triangle);
        if (length_squared(triangle.closest) < nearest)
        {
            nearest = length_squared(triangle.closest);
            best = triangle;
        }
    }
    return best;
}

// What GJK found.
struct GjkResult
{
    bool overlapping;
    float distance;
    vec3 point_first;
    vec3 point_second;
    Simplex simplex;
};

GjkResult gjk()
{
    // Start from the support point toward the second shape's center from the first's.
    vec3 start = pose_second.position - pose_first.position;
    start = length_squared(start) > 0.0 ? start : vec3(1.0, 0.0, 0.0);
    Simplex simplex;
    simplex.points[0] = minkowski_support(-start);
    simplex.points[1] = simplex.points[0];
    simplex.points[2] = simplex.points[0];
    simplex.points[3] = simplex.points[0];
    simplex.weights = float[4](1.0, 0.0, 0.0, 0.0);
    simplex.size = 1U;
    simplex.closest = simplex.points[0].difference;
    const float scale = max(vector_length(simplex.closest), vector_length(start)) + 1.0;
    const float touching = 1e-6 * scale;

    bool overlapping = false;
    for (uint iteration = 1U; iteration < MAXIMUM_GJK_ITERATIONS; ++iteration)
    {
        const vec3 v = simplex.closest;
        const float squared = dot(v, v);
        if (squared <= touching * touching)
        {
            overlapping = true;
            break;
        }

        // The support point against v; no progress (within the tolerance), or a point already in the simplex: done.
        const SupportPoint w = minkowski_support(-v);
        if (squared - dot(v, w.difference) <= 1e-5 * squared)
        {
            break;
        }
        bool repeated = false;
        for (uint index = 0U; index < simplex.size; ++index)
        {
            repeated = repeated || all(equal(simplex.points[index].difference, w.difference));
        }
        if (repeated)
        {
            break;
        }

        // The grown simplex's closest point; a simplex that stops getting closer keeps the previous one.
        Simplex grown = simplex;
        grown.points[grown.size] = w;
        grown.size += 1U;
        if (grown.size == 2U)
        {
            solve_segment(grown);
        }
        else if (grown.size == 3U)
        {
            solve_triangle(grown);
        }
        else if (!solve_tetrahedron(grown))
        {
            // A sliver cannot tell whether it holds the origin: its nearest face decides.
            if (is_sliver(grown))
            {
                const Simplex face = closest_face(grown);
                if (length_squared(face.closest) > touching * touching)
                {
                    if (length_squared(face.closest) < squared)
                    {
                        simplex = face;
                    }
                    break;
                }
            }
            simplex = grown;
            overlapping = true;
            break;
        }
        if (length_squared(grown.closest) >= squared)
        {
            break;
        }
        simplex = grown;
    }

    // The closest points from the weights, or the simplex for EPA.
    GjkResult result;
    result.overlapping = overlapping;
    result.distance = overlapping ? 0.0 : vector_length(simplex.closest);
    result.point_first = vec3(0.0);
    result.point_second = vec3(0.0);
    for (uint index = 0U; index < simplex.size; ++index)
    {
        result.point_first += simplex.points[index].first * simplex.weights[index];
        result.point_second += simplex.points[index].second * simplex.weights[index];
    }
    result.simplex = simplex;
    return result;
}

#endif
