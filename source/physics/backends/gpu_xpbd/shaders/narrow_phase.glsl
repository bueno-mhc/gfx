// What the narrow-phase passes share (GpuNarrowPhase in gpu_narrow_phase.cppm): the shape table, posed instances, the
// convex shapes' support functions and supporting faces, the Minkowski difference, contact features and manifolds. A
// port of the CPU's narrow phase (gfx.physics.shapes :convex and :hull, gfx.physics.narrow_phase): the same operations in
// the same order, the same tolerances and the same ties, so that its manifolds carry the CPU's feature ids.
#ifndef GPU_XPBD_NARROW_PHASE_GLSL
#define GPU_XPBD_NARROW_PHASE_GLSL

#include "primitives.glsl"

// ===================================================================================================== constants
// From gfx.physics.enums and gfx.physics.shapes (gpu_resources.cppm: SPECIALIZATION_CONSTANTS). Shape kinds that match no
// shape, and limits of nothing, make a forgotten one visible; array sizes default to what compiles.
layout(constant_id = 16) const uint SHAPE_SPHERE = 0xFFFFFFF0U;
layout(constant_id = 17) const uint SHAPE_CAPSULE = 0xFFFFFFF1U;
layout(constant_id = 18) const uint SHAPE_CYLINDER = 0xFFFFFFF2U;
layout(constant_id = 19) const uint SHAPE_CONE = 0xFFFFFFF3U;
layout(constant_id = 20) const uint SHAPE_BOX = 0xFFFFFFF4U;
layout(constant_id = 21) const uint SHAPE_HULL = 0xFFFFFFF5U;
layout(constant_id = 22) const uint ALGORITHM_SPHERE_SPHERE = 0U;
layout(constant_id = 23) const uint ALGORITHM_SPHERE_CAPSULE = 0U;
layout(constant_id = 24) const uint ALGORITHM_CAPSULE_CAPSULE = 0U;
layout(constant_id = 25) const uint ALGORITHM_SPHERE_BOX = 0U;
layout(constant_id = 26) const uint ALGORITHM_BOX_BOX = 0U;
layout(constant_id = 27) const uint ALGORITHM_GJK = 0U;
layout(constant_id = 28) const uint ALGORITHM_EPA = 0U;
layout(constant_id = 29) const uint MAXIMUM_FACE_VERTICES = 8U;
layout(constant_id = 30) const uint MAXIMUM_GJK_ITERATIONS = 0U;
layout(constant_id = 31) const uint MAXIMUM_EPA_VERTICES = 8U;
layout(constant_id = 32) const uint MAXIMUM_EPA_FACES = 8U;
layout(constant_id = 33) const uint NO_SUB_SHAPE = 0U;
layout(constant_id = 34) const float FACE_ALIGNMENT = 2.0;
layout(constant_id = 35) const float SIDE_ALIGNMENT = 0.0;
layout(constant_id = 36) const uint ROUND_FACE_VERTICES = 3U;
layout(constant_id = 37) const float GEOMETRIC_TOLERANCE = 0.0;

// Points a manifold holds (the GpuManifold layout's: the C++ side checks MAXIMUM_MANIFOLD_POINTS against it).
const uint MANIFOLD_POINTS = 4U;

// Float's epsilon (2⁻²³) and largest finite value.
float float_epsilon()
{
    return uintBitsToFloat(0x34000000U);
}

float float_maximum()
{
    return uintBitsToFloat(0x7F7FFFFFU);
}

// ===================================================================================================== tables

// The shape table (bindless indices the including pass sets from its push constants): three vectors per shape (kind,
// first hull vertex, hull vertices, first hull face; hull faces; sizes and margin), a vector per hull vertex, two per hull
// face (its plane; its first index and count), a word per face corner (a vertex of its hull).
uint shape_records;
uint hull_vertices;
uint hull_faces;
uint hull_indices;

struct ConvexShape
{
    uint kind;
    uint first_vertex;
    uint vertex_count;
    uint first_face;
    uint face_count;
    vec3 size;    // sphere: radius; capsule, cylinder, cone: half height, radius; box: half extents
    float margin; // the radius around the core
};

ConvexShape load_shape(uint index)
{
    const uvec4 first = gpu_word_vectors[shape_records].values[3U * index];
    const uvec4 second = gpu_word_vectors[shape_records].values[3U * index + 1U];
    const vec4 third = gpu_vectors[shape_records].values[3U * index + 2U];
    return ConvexShape(first.x, first.y, first.z, first.w, second.x, third.xyz, third.w);
}

// A frame in the world: a position and a unit quaternion (x, y, z, w).
struct Pose
{
    vec3 position;
    vec4 orientation;
};

// An instance (GpuInstance): its pose, its shape (the first vector's w, as words).
Pose load_instance(uint instances_buffer, uint index, out uint shape)
{
    const uvec4 first = gpu_word_vectors[instances_buffer].values[2U * index];
    shape = first.w;
    return Pose(uintBitsToFloat(first.xyz), gpu_vectors[instances_buffer].values[2U * index + 1U]);
}

// ===================================================================================================== math (gfx.core.math)

vec3 rotate_vector(vec4 rotation, vec3 vector)
{
    // v + 2w (u × v) + 2u × (u × v), with u the vector part.
    const vec3 axis = rotation.xyz;
    const vec3 twice_cross = cross(axis, vector) * 2.0;
    return vector + twice_cross * rotation.w + cross(axis, twice_cross);
}

vec4 conjugate_rotation(vec4 rotation)
{
    return vec4(-rotation.xyz, rotation.w);
}

vec3 transform_point(Pose pose, vec3 point)
{
    return rotate_vector(pose.orientation, point) + pose.position;
}

float length_squared(vec3 vector)
{
    return dot(vector, vector);
}

float vector_length(vec3 vector)
{
    return sqrt(dot(vector, vector));
}

// The vector at unit length; the zero vector stays zero.
vec3 normalized(vec3 vector)
{
    const float size = vector_length(vector);
    return size > 0.0 ? vector / size : vec3(0.0);
}

// A unit vector perpendicular to vector (crossed with the axis least aligned with it).
vec3 any_perpendicular(vec3 vector)
{
    const vec3 size = abs(vector);
    const vec3 axis = size.x <= size.y && size.x <= size.z ? vec3(1.0, 0.0, 0.0) : size.y <= size.z ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    return normalized(cross(vector, axis));
}

// ===================================================================================================== segments

struct Segment
{
    vec3 start;
    vec3 end;
};

vec3 segment_at(Segment segment, float fraction)
{
    return segment.start + (segment.end - segment.start) * fraction;
}

vec3 segment_closest_point(Segment segment, vec3 point)
{
    const vec3 along = segment.end - segment.start;
    const float squared = length_squared(along);
    const float fraction = squared <= GEOMETRIC_TOLERANCE * GEOMETRIC_TOLERANCE ? 0.0 : clamp(dot(point - segment.start, along) / squared, 0.0, 1.0);
    return segment_at(segment, fraction);
}

// The closest points of two segments (Ericson, Real-Time Collision Detection 5.1.9), as physics::closest_points.
void segment_closest_points(Segment first, Segment second, out vec3 first_point, out vec3 second_point)
{
    const vec3 d1 = first.end - first.start;
    const vec3 d2 = second.end - second.start;
    const vec3 r = first.start - second.start;
    const float a = dot(d1, d1);
    const float e = dot(d2, d2);
    const float f = dot(d2, r);
    const float degenerate = GEOMETRIC_TOLERANCE * GEOMETRIC_TOLERANCE;
    float s = 0.0;
    float t = 0.0;
    if (a <= degenerate && e <= degenerate)
    {
        s = 0.0;
        t = 0.0;
    }
    else if (a <= degenerate)
    {
        t = clamp(f / e, 0.0, 1.0);
    }
    else
    {
        const float c = dot(d1, r);
        if (e <= degenerate)
        {
            s = clamp(-c / a, 0.0, 1.0);
        }
        else
        {
            const float b = dot(d1, d2);
            const float denominator = a * e - b * b;
            s = denominator > degenerate ? clamp((b * f - c * e) / denominator, 0.0, 1.0) : 0.0;
            t = (b * s + f) / e;
            if (t < 0.0)
            {
                t = 0.0;
                s = clamp(-c / a, 0.0, 1.0);
            }
            else if (t > 1.0)
            {
                t = 1.0;
                s = clamp((b - c) / a, 0.0, 1.0);
            }
        }
    }
    first_point = segment_at(first, s);
    second_point = segment_at(second, t);
}

// ===================================================================================================== support functions

vec3 hull_vertex(ConvexShape shape, uint vertex)
{
    return gpu_vectors[hull_vertices].values[shape.first_vertex + vertex].xyz;
}

// The core's farthest point along direction (the shape's frame), as each shape's support().
vec3 shape_support(ConvexShape shape, vec3 direction)
{
    if (shape.kind == SHAPE_SPHERE)
    {
        return vec3(0.0);
    }
    if (shape.kind == SHAPE_CAPSULE)
    {
        return vec3(0.0, direction.y >= 0.0 ? shape.size.x : -shape.size.x, 0.0);
    }
    if (shape.kind == SHAPE_CYLINDER)
    {
        const float half_height = shape.size.x;
        const float radius = shape.size.y;
        const float across = sqrt(direction.x * direction.x + direction.z * direction.z);
        const float y = direction.y >= 0.0 ? half_height : -half_height;
        return across > 0.0 ? vec3(radius * direction.x / across, y, radius * direction.z / across) : vec3(0.0, y, 0.0);
    }
    if (shape.kind == SHAPE_CONE)
    {
        // The apex wins within its normal cone (its angle to +Y below the slant's).
        const float half_height = shape.size.x;
        const float radius = shape.size.y;
        const float slant = sqrt(radius * radius + 4.0 * half_height * half_height);
        if (direction.y > vector_length(direction) * (radius / slant))
        {
            return vec3(0.0, half_height, 0.0);
        }
        const float across = sqrt(direction.x * direction.x + direction.z * direction.z);
        return across > 0.0 ? vec3(radius * direction.x / across, -half_height, radius * direction.z / across) : vec3(0.0, -half_height, 0.0);
    }
    if (shape.kind == SHAPE_BOX)
    {
        return vec3(direction.x >= 0.0 ? shape.size.x : -shape.size.x, direction.y >= 0.0 ? shape.size.y : -shape.size.y,
                    direction.z >= 0.0 ? shape.size.z : -shape.size.z);
    }

    // A hull: the first vertex farthest along it.
    if (shape.vertex_count == 0U)
    {
        return vec3(0.0);
    }
    uint best = 0U;
    float largest = dot(hull_vertex(shape, 0U), direction);
    for (uint vertex = 1U; vertex < shape.vertex_count; ++vertex)
    {
        const float along = dot(hull_vertex(shape, vertex), direction);
        if (along > largest)
        {
            largest = along;
            best = vertex;
        }
    }
    return hull_vertex(shape, best);
}

// ===================================================================================================== supporting faces
// Two slots (0: the first shape's, 1: the second's): the feature of a core facing a direction, as SupportingFace — a
// polygon counter-clockwise around its outward normal, a segment or a point, and its feature index.

vec3 face_vertices[2][MAXIMUM_FACE_VERTICES];
uint face_counts[2];
vec3 face_normals[2];
uint face_features[2];

void point_face(uint slot, vec3 point, vec3 normal, uint feature)
{
    face_vertices[slot][0] = point;
    face_counts[slot] = 1U;
    face_normals[slot] = normal;
    face_features[slot] = feature;
}

void segment_face(uint slot, vec3 start, vec3 end, vec3 normal, uint feature)
{
    face_vertices[slot][0] = start;
    face_vertices[slot][1] = end;
    face_counts[slot] = 2U;
    face_normals[slot] = normal;
    face_features[slot] = feature;
}

// A regular polygon of ROUND_FACE_VERTICES vertices at height y, counter-clockwise around +Y (up) or -Y.
void round_face(uint slot, float radius, float y, bool up, uint feature)
{
    for (uint index = 0U; index < ROUND_FACE_VERTICES; ++index)
    {
        // Counter-clockwise around +Y turns +X toward -Z.
        const float angle = 2.0 * 3.14159274 * float(index) / float(ROUND_FACE_VERTICES);
        const float side = up ? -1.0 : 1.0;
        face_vertices[slot][index] = vec3(radius * cos(angle), y, side * radius * sin(angle));
    }
    face_counts[slot] = ROUND_FACE_VERTICES;
    face_normals[slot] = vec3(0.0, up ? 1.0 : -1.0, 0.0);
    face_features[slot] = feature;
}

// The cosine of the angle between direction and +Y.
float axial(vec3 direction)
{
    const float size = vector_length(direction);
    return size > 0.0 ? direction.y / size : 0.0;
}

// The corners of a box face counter-clockwise around its normal: (b, c) of the face's axis a runs (-, -), (+, -), (+, +),
// (-, +) around +a and the other way around -a.
const vec2 BOX_FACE_CORNERS[4] = vec2[4](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));

void supporting_face(ConvexShape shape, vec3 direction, uint slot)
{
    if (shape.kind == SHAPE_SPHERE)
    {
        point_face(slot, vec3(0.0), direction, 0U);
        return;
    }
    if (shape.kind == SHAPE_CAPSULE)
    {
        const float half_height = shape.size.x;
        const float along = axial(direction);
        if (along > -SIDE_ALIGNMENT && along < SIDE_ALIGNMENT)
        {
            segment_face(slot, vec3(0.0, -half_height, 0.0), vec3(0.0, half_height, 0.0), direction, 0U);
        }
        else if (along > 0.0)
        {
            point_face(slot, vec3(0.0, half_height, 0.0), direction, 1U);
        }
        else
        {
            point_face(slot, vec3(0.0, -half_height, 0.0), direction, 2U);
        }
        return;
    }
    if (shape.kind == SHAPE_CYLINDER)
    {
        const float half_height = shape.size.x;
        const float along = axial(direction);
        if (along >= FACE_ALIGNMENT || along <= -FACE_ALIGNMENT)
        {
            if (along > 0.0)
            {
                round_face(slot, shape.size.y, half_height, true, 0U);
            }
            else
            {
                round_face(slot, shape.size.y, -half_height, false, 1U);
            }
            return;
        }
        if (along > -SIDE_ALIGNMENT && along < SIDE_ALIGNMENT)
        {
            const vec3 rim = shape_support(shape, vec3(direction.x, 0.0, direction.z));
            segment_face(slot, vec3(rim.x, -half_height, rim.z), vec3(rim.x, half_height, rim.z), direction, 2U);
            return;
        }
        point_face(slot, shape_support(shape, direction), direction, 3U);
        return;
    }
    if (shape.kind == SHAPE_CONE)
    {
        const float half_height = shape.size.x;
        const float radius = shape.size.y;
        const float size = vector_length(direction);
        if (size <= 0.0)
        {
            point_face(slot, vec3(0.0, half_height, 0.0), direction, 2U);
            return;
        }
        if (direction.y / size <= -FACE_ALIGNMENT)
        {
            round_face(slot, radius, -half_height, false, 0U);
            return;
        }
        const float across = sqrt(direction.x * direction.x + direction.z * direction.z);
        if (across > 0.0)
        {
            // The side's outward normal at the direction's azimuth: (cos φ · 2h, r, sin φ · 2h), normalized.
            const float height = 2.0 * half_height;
            const vec3 side = normalized(vec3(direction.x / across * height, radius, direction.z / across * height));
            if (dot(side, direction) / size >= FACE_ALIGNMENT)
            {
                const vec3 rim = vec3(radius * direction.x / across, -half_height, radius * direction.z / across);
                segment_face(slot, rim, vec3(0.0, half_height, 0.0), direction, 1U);
                return;
            }
        }
        point_face(slot, shape_support(shape, direction), direction, 2U);
        return;
    }
    if (shape.kind == SHAPE_BOX)
    {
        // The axis of the largest component (the first on ties), its sign, then the corners.
        const float components[3] = float[3](direction.x, direction.y, direction.z);
        const float half_size[3] = float[3](shape.size.x, shape.size.y, shape.size.z);
        uint axis = 0U;
        for (uint index = 1U; index < 3U; ++index)
        {
            if (abs(components[index]) > abs(components[axis]))
            {
                axis = index;
            }
        }
        const bool positive = components[axis] >= 0.0;
        const uint second = (axis + 1U) % 3U;
        const uint third = (axis + 2U) % 3U;
        float normal[3] = float[3](0.0, 0.0, 0.0);
        normal[axis] = positive ? 1.0 : -1.0;
        for (uint corner = 0U; corner < 4U; ++corner)
        {
            const vec2 signs = BOX_FACE_CORNERS[positive ? corner : (4U - corner) % 4U];
            float point[3] = float[3](0.0, 0.0, 0.0);
            point[axis] = positive ? half_size[axis] : -half_size[axis];
            point[second] = signs.x * half_size[second];
            point[third] = signs.y * half_size[third];
            face_vertices[slot][corner] = vec3(point[0], point[1], point[2]);
        }
        face_counts[slot] = 4U;
        face_normals[slot] = vec3(normal[0], normal[1], normal[2]);
        face_features[slot] = axis * 2U + (positive ? 1U : 0U);
        return;
    }

    // A hull: the face whose normal is closest to the direction (the first on ties); more than MAXIMUM_FACE_VERTICES
    // corners give that many, evenly spaced.
    if (shape.face_count == 0U)
    {
        point_face(slot, shape_support(shape, direction), direction, 0U);
        face_counts[slot] = shape.vertex_count == 0U ? 0U : 1U;
        return;
    }
    uint best = 0U;
    float largest = dot(gpu_vectors[hull_faces].values[2U * shape.first_face].xyz, direction);
    for (uint face = 1U; face < shape.face_count; ++face)
    {
        const float along = dot(gpu_vectors[hull_faces].values[2U * (shape.first_face + face)].xyz, direction);
        if (along > largest)
        {
            largest = along;
            best = face;
        }
    }
    const uvec4 corners = gpu_word_vectors[hull_faces].values[2U * (shape.first_face + best) + 1U];
    const uint count = min(corners.y, MAXIMUM_FACE_VERTICES);
    for (uint index = 0U; index < count; ++index)
    {
        const uint taken = index * corners.y / count;
        face_vertices[slot][index] = hull_vertex(shape, gpu_words[hull_indices].words[corners.x + taken]);
    }
    face_counts[slot] = count;
    face_normals[slot] = gpu_vectors[hull_faces].values[2U * (shape.first_face + best)].xyz;
    face_features[slot] = best;
}

// The face of slot moved by pose (vertices and normal).
void pose_face(uint slot, Pose pose)
{
    face_normals[slot] = rotate_vector(pose.orientation, face_normals[slot]);
    for (uint index = 0U; index < face_counts[slot]; ++index)
    {
        face_vertices[slot][index] = transform_point(pose, face_vertices[slot][index]);
    }
}

// The supporting face of shape at pose along direction (world space), posed, into slot.
void posed_supporting_face(ConvexShape shape, Pose pose, vec3 direction, uint slot)
{
    supporting_face(shape, rotate_vector(conjugate_rotation(pose.orientation), direction), slot);
    pose_face(slot, pose);
}

// ===================================================================================================== the pair

// The two shapes of the pair being collided (the general convex path's Minkowski difference reads them).
ConvexShape shape_first;
ConvexShape shape_second;
Pose pose_first;
Pose pose_second;

// A point of the Minkowski difference (first − second) and the points of each core that made it (world space).
struct SupportPoint
{
    vec3 first;
    vec3 second;
    vec3 difference;
};

SupportPoint minkowski_support(vec3 direction)
{
    const vec3 on_first = transform_point(pose_first, shape_support(shape_first, rotate_vector(conjugate_rotation(pose_first.orientation), direction)));
    const vec3 on_second = transform_point(pose_second, shape_support(shape_second, rotate_vector(conjugate_rotation(pose_second.orientation), -direction)));
    return SupportPoint(on_first, on_second, on_first - on_second);
}

// The core's farthest point of shape at pose along direction (world space).
vec3 posed_support(ConvexShape shape, Pose pose, vec3 direction)
{
    return transform_point(pose, shape_support(shape, rotate_vector(conjugate_rotation(pose.orientation), direction)));
}

// ===================================================================================================== features (contact_features)

uint incident_vertex(uint vertex)
{
    return vertex & 0x1FU;
}

uint clipped_edge(uint edge, uint side)
{
    return 0x400U | (edge & 0x1FU) << 5U | (side & 0x1FU);
}

uint reference_corner(uint corner)
{
    return 0x400U | 0x3E0U | (corner & 0x1FU);
}

uint face_contact(uint reference_face, uint incident_face, uint tag, bool first_is_reference)
{
    return (first_is_reference ? 0x80000000U : 0U) | (reference_face & 0x3FFU) << 21U | (incident_face & 0x3FFU) << 11U | (tag & 0x7FFU);
}

uint point_contact(uint first, uint second)
{
    return 0x40000000U | (first & 0x7FFFU) << 15U | (second & 0x7FFFU);
}

uint flipped_feature(uint local)
{
    if ((local & 0x40000000U) != 0U)
    {
        return 0x40000000U | (local & 0x7FFFU) << 15U | (local >> 15U & 0x7FFFU);
    }
    return local ^ 0x80000000U;
}

// ===================================================================================================== manifolds

// Up to four points sharing a normal (first to second); a point's separation is along it.
struct Manifold
{
    vec3 normal;
    uint count;
    uint algorithm;
    vec3 positions[MANIFOLD_POINTS];
    float separations[MANIFOLD_POINTS];
    uint features[MANIFOLD_POINTS];
};

Manifold empty_manifold()
{
    Manifold manifold;
    manifold.normal = vec3(0.0);
    manifold.count = 0U;
    manifold.algorithm = 0U;
    for (uint point = 0U; point < MANIFOLD_POINTS; ++point)
    {
        manifold.positions[point] = vec3(0.0);
        manifold.separations[point] = 0.0;
        manifold.features[point] = 0U;
    }
    return manifold;
}

// The manifold with its shapes exchanged: the normal reversed, the features' roles exchanged.
void flip_manifold(inout Manifold manifold)
{
    manifold.normal = -manifold.normal;
    for (uint point = 0U; point < manifold.count; ++point)
    {
        manifold.features[point] = flipped_feature(manifold.features[point]);
    }
}

// The manifold of the pair: what the passes write.
Manifold result;

#endif
