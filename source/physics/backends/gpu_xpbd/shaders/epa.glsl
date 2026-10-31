// EPA from GJK's final simplex around the origin: the penetration depth, the normal (first to second) and the cores'
// deepest points. A port of physics::epa() (epa.cppm): the tetrahedron grown from the simplex, the octahedron of the axes'
// support points when the origin is on the start's boundary, horizon edges, the best face kept, fixed storage
// (MAXIMUM_EPA_VERTICES, MAXIMUM_EPA_FACES).
#ifndef GPU_XPBD_EPA_GLSL
#define GPU_XPBD_EPA_GLSL

#include "gjk.glsl"

// The polytope: its vertices; its faces (corners packed 8 bits each, counter-clockwise from outside), their outward
// normals, distances from the origin and whether they are alive; the horizon's edges (from | to << 16).
SupportPoint polytope_vertices[MAXIMUM_EPA_VERTICES];
uint polytope_vertex_count;
uint polytope_corners[MAXIMUM_EPA_FACES];
vec3 polytope_normals[MAXIMUM_EPA_FACES];
float polytope_distances[MAXIMUM_EPA_FACES];
bool polytope_alive[MAXIMUM_EPA_FACES];
uint polytope_face_count;
uint horizon_edges[3U * MAXIMUM_EPA_FACES];

// The tetrahedron EPA starts from.
SupportPoint tetrahedron[4];
uint tetrahedron_count;

const vec3 EPA_AXES[6] = vec3[6](vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, -1.0, 0.0), vec3(0.0, 0.0, 1.0),
                                 vec3(0.0, 0.0, -1.0));

uint face_corner(uint face, uint corner)
{
    return (polytope_corners[face] >> (8U * corner)) & 0xFFU;
}

// Adds the face (a, b, c) in the first dead slot, else after the others; false when the storage is full.
bool add_face(uint a, uint b, uint c)
{
    uint slot = polytope_face_count;
    for (uint index = 0U; index < polytope_face_count; ++index)
    {
        if (!polytope_alive[index])
        {
            slot = index;
            break;
        }
    }
    if (slot >= MAXIMUM_EPA_FACES)
    {
        return false;
    }
    const vec3 pa = polytope_vertices[a].difference;
    const vec3 normal = normalized(cross(polytope_vertices[b].difference - pa, polytope_vertices[c].difference - pa));
    polytope_corners[slot] = a | b << 8U | c << 16U;
    polytope_normals[slot] = normal;
    polytope_distances[slot] = dot(normal, pa);
    polytope_alive[slot] = length_squared(normal) > 0.0;
    polytope_face_count = max(polytope_face_count, slot + 1U);
    return true;
}

// The live face nearest the origin (the first of equals), or MAXIMUM_EPA_FACES.
uint nearest_face()
{
    uint best = MAXIMUM_EPA_FACES;
    for (uint index = 0U; index < polytope_face_count; ++index)
    {
        if (polytope_alive[index] && (best == MAXIMUM_EPA_FACES || polytope_distances[index] < polytope_distances[best]))
        {
            best = index;
        }
    }
    return best;
}

// The barycentric coordinates of point in the triangle (a, b, c).
vec3 barycentric(vec3 point, vec3 a, vec3 b, vec3 c)
{
    const vec3 v0 = b - a;
    const vec3 v1 = c - a;
    const vec3 v2 = point - a;
    const float d00 = dot(v0, v0);
    const float d01 = dot(v0, v1);
    const float d11 = dot(v1, v1);
    const float d20 = dot(v2, v0);
    const float d21 = dot(v2, v1);
    const float denominator = d00 * d11 - d01 * d01;
    if (!(abs(denominator) > 0.0))
    {
        return vec3(1.0, 0.0, 0.0);
    }
    const float v = (d11 * d20 - d01 * d21) / denominator;
    const float w = (d00 * d21 - d01 * d20) / denominator;
    return vec3(1.0 - v - w, v, w);
}

// Replaces the polytope with the octahedron of the six axes' support points when that holds the origin strictly inside
// and is convex (epa_detail::start_from_axes); keeps it otherwise.
void start_from_axes()
{
    // The tetrahedron's polytope, kept to restore.
    SupportPoint kept_vertices[4];
    uint kept_corners[4];
    vec3 kept_normals[4];
    float kept_distances[4];
    bool kept_alive[4];
    const uint kept_vertex_count = polytope_vertex_count;
    const uint kept_face_count = polytope_face_count;
    for (uint index = 0U; index < 4U; ++index)
    {
        kept_vertices[index] = polytope_vertices[index];
        kept_corners[index] = polytope_corners[index];
        kept_normals[index] = polytope_normals[index];
        kept_distances[index] = polytope_distances[index];
        kept_alive[index] = polytope_alive[index];
    }

    // The six points, the faces wound outward (seen from their centroid).
    const uvec3 faces[8] = uvec3[8](uvec3(0U, 2U, 4U), uvec3(2U, 1U, 4U), uvec3(1U, 3U, 4U), uvec3(3U, 0U, 4U), uvec3(2U, 0U, 5U), uvec3(1U, 2U, 5U),
                                    uvec3(3U, 1U, 5U), uvec3(0U, 3U, 5U));
    polytope_vertex_count = 0U;
    polytope_face_count = 0U;
    vec3 middle = vec3(0.0);
    float scale = 0.0;
    for (uint axis = 0U; axis < 6U; ++axis)
    {
        const SupportPoint point = minkowski_support(EPA_AXES[axis]);
        polytope_vertices[polytope_vertex_count++] = point;
        middle += point.difference / 6.0;
        scale = max(scale, vector_length(point.difference));
    }
    for (uint index = 0U; index < 8U; ++index)
    {
        const uvec3 face = faces[index];
        const vec3 a = polytope_vertices[face.x].difference;
        const bool inward = dot(cross(polytope_vertices[face.y].difference - a, polytope_vertices[face.z].difference - a), middle - a) > 0.0;
        add_face(face.x, inward ? face.z : face.y, inward ? face.y : face.z);
    }

    // Usable when the origin is inside every face and no point is in front of a face.
    const float tolerance = 1e-5 * scale;
    bool usable = true;
    for (uint index = 0U; index < polytope_face_count && usable; ++index)
    {
        if (!polytope_alive[index] || polytope_distances[index] <= tolerance)
        {
            usable = false;
        }
        for (uint vertex = 0U; vertex < polytope_vertex_count && usable; ++vertex)
        {
            if (dot(polytope_normals[index], polytope_vertices[vertex].difference - polytope_vertices[face_corner(index, 0U)].difference) > tolerance)
            {
                usable = false;
            }
        }
    }
    if (usable)
    {
        return;
    }
    polytope_vertex_count = kept_vertex_count;
    polytope_face_count = kept_face_count;
    for (uint index = 0U; index < 4U; ++index)
    {
        polytope_vertices[index] = kept_vertices[index];
        polytope_corners[index] = kept_corners[index];
        polytope_normals[index] = kept_normals[index];
        polytope_distances[index] = kept_distances[index];
        polytope_alive[index] = kept_alive[index];
    }
}

// Adds the support point along direction to the tetrahedron unless it repeats one.
bool add_distinct(vec3 direction)
{
    const SupportPoint point = minkowski_support(direction);
    for (uint index = 0U; index < tetrahedron_count; ++index)
    {
        if (length_squared(tetrahedron[index].difference - point.difference) <= 1e-12)
        {
            return false;
        }
    }
    tetrahedron[tetrahedron_count++] = point;
    return true;
}

// What EPA found.
struct EpaResult
{
    vec3 normal;     // moving the second shape by depth along it separates the cores
    float depth;     // at least 0
    vec3 point_first;
    vec3 point_second;
};

EpaResult epa(GjkResult start)
{
    EpaResult result;
    result.normal = vec3(0.0, 1.0, 0.0);
    result.depth = 0.0;
    result.point_first = start.point_first;
    result.point_second = start.point_second;

    // A tetrahedron from the simplex (a sliver grows again from its first point), points added along directions that
    // give volume.
    tetrahedron_count = 0U;
    for (uint index = 0U; index < start.simplex.size; ++index)
    {
        tetrahedron[tetrahedron_count++] = start.simplex.points[index];
    }
    if (tetrahedron_count == 4U && is_sliver(start.simplex))
    {
        tetrahedron_count = 1U;
    }
    if (tetrahedron_count == 0U)
    {
        add_distinct(EPA_AXES[0]);
    }
    for (uint axis = 0U; tetrahedron_count == 1U && axis < 6U; ++axis)
    {
        add_distinct(EPA_AXES[axis]);
    }
    if (tetrahedron_count == 2U)
    {
        const vec3 line = tetrahedron[1].difference - tetrahedron[0].difference;
        const vec3 across = any_perpendicular(line);
        const vec3 around[4] = vec3[4](across, -across, cross(line, across), -cross(line, across));
        for (uint index = 0U; tetrahedron_count == 2U && index < 4U; ++index)
        {
            const SupportPoint point = minkowski_support(around[index]);
            if (length_squared(cross(point.difference - tetrahedron[0].difference, line)) > 1e-12 * length_squared(line))
            {
                tetrahedron[tetrahedron_count++] = point;
            }
        }
    }
    if (tetrahedron_count == 3U)
    {
        const vec3 normal = cross(tetrahedron[1].difference - tetrahedron[0].difference, tetrahedron[2].difference - tetrahedron[0].difference);
        const SupportPoint up = minkowski_support(normal);
        const SupportPoint down = minkowski_support(-normal);
        const float up_height = abs(dot(up.difference - tetrahedron[0].difference, normal));
        const float down_height = abs(dot(down.difference - tetrahedron[0].difference, normal));
        if (max(up_height, down_height) > 1e-9 * vector_length(normal))
        {
            tetrahedron[tetrahedron_count++] = up_height >= down_height ? up : down;
        }
        else
        {
            // Flat: the cores only touch.
            result.normal = normalized(normal);
            return result;
        }
    }
    if (tetrahedron_count < 4U)
    {
        return result;
    }

    // The tetrahedron's faces, wound to face away from its fourth vertex.
    polytope_vertex_count = 0U;
    polytope_face_count = 0U;
    for (uint index = 0U; index < 4U; ++index)
    {
        polytope_vertices[polytope_vertex_count++] = tetrahedron[index];
    }
    const bool flip = dot(cross(tetrahedron[1].difference - tetrahedron[0].difference, tetrahedron[2].difference - tetrahedron[0].difference),
                          tetrahedron[3].difference - tetrahedron[0].difference) > 0.0;
    const uint b = flip ? 2U : 1U;
    const uint c = flip ? 1U : 2U;
    add_face(0U, b, c);
    add_face(0U, 3U, b);
    add_face(b, 3U, c);
    add_face(c, 3U, 0U);

    // The origin on the start's boundary: the octahedron of the axes' support points starts instead when it holds it.
    float extent = 0.0;
    for (uint index = 0U; index < 4U; ++index)
    {
        extent = max(extent, vector_length(tetrahedron[index].difference));
    }
    const uint closest = nearest_face();
    if (closest < MAXIMUM_EPA_FACES && polytope_distances[closest] <= 1e-5 * extent)
    {
        start_from_axes();
    }

    // Expand the nearest face toward its support point until it adds (almost) nothing; the best face so far stands
    // should the nearest distance drop (near-coplanar points broke the polytope).
    uint nearest = nearest_face();
    bool best_alive = false;
    vec3 best_normal = vec3(0.0);
    float best_distance = 0.0;
    SupportPoint best_corners[3];
    if (nearest < MAXIMUM_EPA_FACES)
    {
        best_alive = polytope_alive[nearest];
        best_normal = polytope_normals[nearest];
        best_distance = polytope_distances[nearest];
        for (uint corner = 0U; corner < 3U; ++corner)
        {
            best_corners[corner] = polytope_vertices[face_corner(nearest, corner)];
        }
    }
    for (uint iteration = 0U; iteration < MAXIMUM_EPA_VERTICES && nearest < MAXIMUM_EPA_FACES; ++iteration)
    {
        const vec3 face_normal = polytope_normals[nearest];
        const float face_distance = polytope_distances[nearest];
        const SupportPoint point = minkowski_support(face_normal);
        const float reach = dot(point.difference, face_normal);
        const float tolerance = max(1e-4 * abs(reach), 1e-6);
        if (reach - face_distance <= tolerance)
        {
            break;
        }
        if (polytope_vertex_count >= MAXIMUM_EPA_VERTICES)
        {
            break;
        }
        const uint added = polytope_vertex_count;
        polytope_vertices[polytope_vertex_count++] = point;

        // The faces the new point sees go (the expanded one always; the others clearly in front of it); their edges seen
        // once form the horizon.
        const float visibility = 1e-5 * max(1.0, abs(reach));
        uint edge_count = 0U;
        for (uint index = 0U; index < polytope_face_count; ++index)
        {
            if (!polytope_alive[index] ||
                (index != nearest && dot(polytope_normals[index], point.difference - polytope_vertices[face_corner(index, 0U)].difference) <= visibility))
            {
                continue;
            }
            polytope_alive[index] = false;
            for (uint corner = 0U; corner < 3U; ++corner)
            {
                const uint from = face_corner(index, corner);
                const uint to = face_corner(index, (corner + 1U) % 3U);
                bool seen_twice = false;
                for (uint other = 0U; other < edge_count; ++other)
                {
                    if (horizon_edges[other] == (to | from << 16U))
                    {
                        horizon_edges[other] = horizon_edges[--edge_count];
                        seen_twice = true;
                        break;
                    }
                }
                if (!seen_twice && edge_count < 3U * MAXIMUM_EPA_FACES)
                {
                    horizon_edges[edge_count++] = from | to << 16U;
                }
            }
        }
        bool stored = true;
        for (uint index = 0U; index < edge_count && stored; ++index)
        {
            stored = add_face(horizon_edges[index] & 0xFFFFU, horizon_edges[index] >> 16U, added);
        }
        nearest = nearest_face();
        if (!stored)
        {
            break;
        }
        if (nearest >= MAXIMUM_EPA_FACES || polytope_distances[nearest] < best_distance - tolerance)
        {
            break;
        }
        best_alive = polytope_alive[nearest];
        best_normal = polytope_normals[nearest];
        best_distance = polytope_distances[nearest];
        for (uint corner = 0U; corner < 3U; ++corner)
        {
            best_corners[corner] = polytope_vertices[face_corner(nearest, corner)];
        }
    }
    if (!best_alive)
    {
        return result;
    }

    // The best face's point closest to the origin, in barycentric coordinates: the cores' deepest points.
    const vec3 weights = barycentric(best_normal * best_distance, best_corners[0].difference, best_corners[1].difference, best_corners[2].difference);
    result.normal = best_normal;
    result.depth = max(best_distance, 0.0);
    result.point_first = best_corners[0].first * weights.x + best_corners[1].first * weights.y + best_corners[2].first * weights.z;
    result.point_second = best_corners[0].second * weights.x + best_corners[1].second * weights.y + best_corners[2].second * weights.z;
    return result;
}

#endif
