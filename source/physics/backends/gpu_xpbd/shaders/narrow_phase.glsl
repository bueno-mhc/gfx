// What the narrow-phase passes share (GpuNarrowPhase in gpu_narrow_phase.cppm): the shape table (convex shapes, triangle
// meshes and height fields with their triangles, compounds with their children, the CPU's static hierarchies), poses and
// boxes, the convex shapes' and triangles' support functions and supporting faces, the Minkowski difference, contact
// features and manifolds. A port of the CPU's narrow phase (gfx.physics.shapes, gfx.physics.narrow_phase): the same
// operations in the same order, the same tolerances and the same ties, so that its manifolds carry the CPU's feature ids.
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
layout(constant_id = 39) const uint SHAPE_TRIANGLE_MESH = 0xFFFFFFF6U;
layout(constant_id = 40) const uint SHAPE_HEIGHT_FIELD = 0xFFFFFFF7U;
layout(constant_id = 41) const uint SHAPE_COMPOUND = 0xFFFFFFF8U;
layout(constant_id = 42) const uint ALGORITHM_TRIANGLE_TRIANGLE = 0U;
layout(constant_id = 43) const uint STATIC_BVH_DEPTH = 2U;   // StaticBvh::MAXIMUM_DEPTH: a static hierarchy's traversal stack
layout(constant_id = 44) const uint COMPOUND_LEVELS = 2U;    // compounds being descended at once: GPU_XPBD_COMPOUND_DEPTH per side
layout(constant_id = 45) const float TRIANGLE_FACE_TOLERANCE = 0.0;

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

// The shape table's arrays (bindless indices, read from the table's own buffer by load_tables()):
//   shape_records    six vectors per shape (ShapeRecord);
//   table_vertices   a vector per hull or mesh vertex;
//   hull_faces       two vectors per hull face (its plane; its first corner in hull_indices and its corners);
//   hull_indices     a word per hull face corner (a vertex of its hull);
//   mesh_triangles   a word vector per mesh triangle (its three vertices, absolute; its active edges' bits);
//   bvh_nodes        two word vectors per static hierarchy node (minimum and its first child or item, absolute; maximum
//                    and its items, 0 for an inner node) — the CPU's StaticBvh as built, so traversals visit its order;
//   bvh_items        a word per hierarchy item (a triangle or child of its shape);
//   bvh_boxes        two vectors per hierarchy item (its box), at the shape's first box + the item;
//   field_heights    a word per height field sample (a float, as given), row by row;
//   field_edges      a word per height field triangle: its active edges' bits;
//   compound_children two vectors per compound child (position and shape index; orientation).
uint shape_records;
uint table_vertices;
uint hull_faces;
uint hull_indices;
uint mesh_triangles;
uint bvh_nodes;
uint bvh_items;
uint bvh_boxes;
uint field_heights;
uint field_edges;
uint compound_children;

void load_tables(uint tables)
{
    const uvec4 first = gpu_word_vectors[tables].values[0];
    const uvec4 second = gpu_word_vectors[tables].values[1];
    const uvec4 third = gpu_word_vectors[tables].values[2];
    shape_records = first.x;
    table_vertices = first.y;
    hull_faces = first.z;
    hull_indices = first.w;
    mesh_triangles = second.x;
    bvh_nodes = second.y;
    bvh_items = second.z;
    bvh_boxes = second.w;
    field_heights = third.x;
    field_edges = third.y;
    compound_children = third.z;
}

// A shape of the table (GpuShapeRecord): kind, flags (bit 0: two-sided mesh); two ranges (hull: vertices, faces; mesh:
// vertices, triangles; height field: first sample and rows, first edge word and columns; compound: children); its static
// hierarchy (first node, first item, first box); sizes (sphere: radius; capsule, cylinder, cone: half height, radius; box:
// half extents; height field: scale) and margin; its box in its own frame.
struct ShapeRecord
{
    uint kind;
    uint flags;
    uint first;
    uint count;
    uint second_first;
    uint second_count;
    uint first_node;
    uint first_item;
    uint first_box;
    vec3 size;
    float margin;
    vec3 bounds_minimum;
    vec3 bounds_maximum;
};

ShapeRecord load_record(uint index)
{
    const uint base = 6U * index;
    const uvec4 head = gpu_word_vectors[shape_records].values[base];
    const uvec4 ranges = gpu_word_vectors[shape_records].values[base + 1U];
    const uvec4 hierarchy = gpu_word_vectors[shape_records].values[base + 2U];
    const vec4 size = gpu_vectors[shape_records].values[base + 3U];
    return ShapeRecord(head.x, head.y, ranges.x, ranges.y, ranges.z, ranges.w, hierarchy.x, hierarchy.y, hierarchy.z, size.xyz, size.w,
                       gpu_vectors[shape_records].values[base + 4U].xyz, gpu_vectors[shape_records].values[base + 5U].xyz);
}

bool is_convex(ShapeRecord record)
{
    return record.kind != SHAPE_TRIANGLE_MESH && record.kind != SHAPE_HEIGHT_FIELD && record.kind != SHAPE_COMPOUND;
}

// A convex piece being collided: a convex shape, or a triangle (kind SHAPE_TRIANGLE_MESH, its corners in its frame). The
// corners are three members, not an array: no array member indexed at run time in a struct the support function takes by
// value in every GJK and EPA iteration.
struct ConvexShape
{
    uint kind;
    uint first_vertex;
    uint vertex_count;
    uint first_face;
    uint face_count;
    vec3 size;    // sphere: radius; capsule, cylinder, cone: half height, radius; box: half extents
    float margin; // the radius around the core
    vec3 corner_a;
    vec3 corner_b;
    vec3 corner_c;
};

ConvexShape convex_shape(ShapeRecord record)
{
    return ConvexShape(record.kind, record.first, record.count, record.second_first, record.second_count, record.size, record.margin, vec3(0.0),
                       vec3(0.0), vec3(0.0));
}

// A triangle's corner index (0, 1 or 2), by selection.
vec3 triangle_corner(ConvexShape shape, uint index)
{
    return index == 0U ? shape.corner_a : index == 1U ? shape.corner_b : shape.corner_c;
}

ConvexShape load_shape(uint index)
{
    return convex_shape(load_record(index));
}

ConvexShape triangle_shape(vec3 first, vec3 second, vec3 third)
{
    return ConvexShape(SHAPE_TRIANGLE_MESH, 0U, 0U, 0U, 0U, vec3(0.0), 0.0, first, second, third);
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

Pose identity_pose()
{
    return Pose(vec3(0.0), vec4(0.0, 0.0, 0.0, 1.0));
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

// The Hamilton product: second applied first (as gfx::operator*(Quaternion, Quaternion) at compile time).
vec4 multiply_rotations(vec4 first, vec4 second)
{
    return vec4(first.w * second.x + first.x * second.w + first.y * second.z - first.z * second.y,
                first.w * second.y - first.x * second.z + first.y * second.w + first.z * second.x,
                first.w * second.z + first.x * second.y - first.y * second.x + first.z * second.w,
                first.w * second.w - first.x * second.x - first.y * second.y - first.z * second.z);
}

// The composition: second applied first, then first (as gfx::operator*(Transform, Transform)).
Pose compose(Pose first, Pose second)
{
    return Pose(transform_point(first, second.position), multiply_rotations(first.orientation, second.orientation));
}

Pose inverse_pose(Pose pose)
{
    const vec4 rotation = conjugate_rotation(pose.orientation);
    return Pose(-rotate_vector(rotation, pose.position), rotation);
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

// ===================================================================================================== boxes

struct Aabb
{
    vec3 minimum;
    vec3 maximum;
};

// Whether two boxes overlap (touching counts).
bool intersects(Aabb first, Aabb second)
{
    return all(lessThanEqual(first.minimum, second.maximum)) && all(greaterThanEqual(first.maximum, second.minimum));
}

Aabb expanded(Aabb box, float distance)
{
    return Aabb(box.minimum - vec3(distance), box.maximum + vec3(distance));
}

// The box holding box moved by pose (Arvo's method on the rotation's matrix, as Aabb::transformed()).
Aabb transformed(Aabb box, Pose pose)
{
    const float x = pose.orientation.x;
    const float y = pose.orientation.y;
    const float z = pose.orientation.z;
    const float w = pose.orientation.w;
    const vec3 columns[3] = vec3[3](vec3(1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y + w * z), 2.0 * (x * z - w * y)),
                                    vec3(2.0 * (x * y - w * z), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z + w * x)),
                                    vec3(2.0 * (x * z + w * y), 2.0 * (y * z - w * x), 1.0 - 2.0 * (x * x + y * y)));
    const float lows[3] = float[3](box.minimum.x, box.minimum.y, box.minimum.z);
    const float highs[3] = float[3](box.maximum.x, box.maximum.y, box.maximum.z);
    Aabb result = Aabb(pose.position, pose.position);
    for (uint column = 0U; column < 3U; ++column)
    {
        const vec3 first = columns[column] * lows[column];
        const vec3 second = columns[column] * highs[column];
        result.minimum += min(first, second);
        result.maximum += max(first, second);
    }
    return result;
}

Aabb triangle_bounds(vec3 first, vec3 second, vec3 third)
{
    return Aabb(min(first, min(second, third)), max(first, max(second, third)));
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
    return gpu_vectors[table_vertices].values[shape.first_vertex + vertex].xyz;
}

// A triangle's front normal (unit length; zero for a degenerate triangle).
vec3 triangle_normal(vec3 first, vec3 second, vec3 third)
{
    return normalized(cross(second - first, third - first));
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
    if (shape.kind == SHAPE_TRIANGLE_MESH)
    {
        // The vertex farthest along it (the first of equals).
        const float a = dot(shape.corner_a, direction);
        const float b = dot(shape.corner_b, direction);
        const float c = dot(shape.corner_c, direction);
        return a >= b && a >= c ? shape.corner_a : b >= c ? shape.corner_b : shape.corner_c;
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

    if (shape.kind == SHAPE_TRIANGLE_MESH)
    {
        // The triangle, its front (face 0) toward the direction (or within TRIANGLE_FACE_TOLERANCE of its plane) or its back (1).
        const vec3 front = triangle_normal(shape.corner_a, shape.corner_b, shape.corner_c);
        const bool facing = dot(front, direction) >= -TRIANGLE_FACE_TOLERANCE * length(direction);
        face_vertices[slot][0] = shape.corner_a;
        face_vertices[slot][1] = facing ? shape.corner_b : shape.corner_c;
        face_vertices[slot][2] = facing ? shape.corner_c : shape.corner_b;
        face_counts[slot] = 3U;
        face_normals[slot] = facing ? front : -front;
        face_features[slot] = facing ? 0U : 1U;
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

// The box around a convex shape at pose, from its support function along the six axes (as convex_bounds()).
Aabb convex_bounds(ConvexShape shape, Pose pose)
{
    const vec3 low = vec3(-dot(posed_support(shape, pose, vec3(-1.0, 0.0, 0.0)), vec3(-1.0, 0.0, 0.0)),
                          -dot(posed_support(shape, pose, vec3(0.0, -1.0, 0.0)), vec3(0.0, -1.0, 0.0)),
                          -dot(posed_support(shape, pose, vec3(0.0, 0.0, -1.0)), vec3(0.0, 0.0, -1.0)));
    const vec3 high = vec3(dot(posed_support(shape, pose, vec3(1.0, 0.0, 0.0)), vec3(1.0, 0.0, 0.0)),
                           dot(posed_support(shape, pose, vec3(0.0, 1.0, 0.0)), vec3(0.0, 1.0, 0.0)),
                           dot(posed_support(shape, pose, vec3(0.0, 0.0, 1.0)), vec3(0.0, 0.0, 1.0)));
    return Aabb(low - shape.margin, high + shape.margin);
}

// The box around any shape at pose (as Shape::bounds(pose)): convex shapes' from their support functions, the others'
// their own box moved.
Aabb shape_bounds_at(ShapeRecord record, Pose pose)
{
    if (is_convex(record))
    {
        return convex_bounds(convex_shape(record), pose);
    }
    return transformed(Aabb(record.bounds_minimum, record.bounds_maximum), pose);
}

// ===================================================================================================== meshes, fields, compounds

// Triangle index of a mesh or a height field: its corners in the shape's frame, its active edges' bits.
void soup_triangle(ShapeRecord soup, uint index, out vec3 first, out vec3 second, out vec3 third, out uint active_edges)
{
    if (soup.kind == SHAPE_TRIANGLE_MESH)
    {
        const uvec4 triangle = gpu_word_vectors[mesh_triangles].values[soup.second_first + index];
        first = gpu_vectors[table_vertices].values[triangle.x].xyz;
        second = gpu_vectors[table_vertices].values[triangle.y].xyz;
        third = gpu_vectors[table_vertices].values[triangle.z].xyz;
        active_edges = triangle.w;
        return;
    }

    // A height field: half 0 = (r, c), (r + 1, c), (r + 1, c + 1); half 1 = (r, c), (r + 1, c + 1), (r, c + 1).
    const uint columns = soup.second_count;
    const uint cell = index / 2U;
    const uint row = cell / (columns - 1U);
    const uint column = cell % (columns - 1U);
    const vec3 scale = soup.size;
    const vec3 corner = vec3(float(column) * scale.x, uintBitsToFloat(gpu_words[field_heights].words[soup.first + row * columns + column]) * scale.y,
                             float(row) * scale.z);
    const vec3 below = vec3(float(column) * scale.x, uintBitsToFloat(gpu_words[field_heights].words[soup.first + (row + 1U) * columns + column]) * scale.y,
                            float(row + 1U) * scale.z);
    const vec3 diagonal = vec3(float(column + 1U) * scale.x,
                               uintBitsToFloat(gpu_words[field_heights].words[soup.first + (row + 1U) * columns + column + 1U]) * scale.y,
                               float(row + 1U) * scale.z);
    const vec3 beside = vec3(float(column + 1U) * scale.x, uintBitsToFloat(gpu_words[field_heights].words[soup.first + row * columns + column + 1U]) * scale.y,
                             float(row) * scale.z);
    first = corner;
    second = index % 2U == 0U ? below : diagonal;
    third = index % 2U == 0U ? diagonal : beside;
    active_edges = gpu_words[field_edges].words[soup.second_first + index];
}

// A static hierarchy's node: its box, its first child or item (absolute), its items (0: an inner node).
Aabb node_box(uint node, out uint first, out uint count)
{
    const uvec4 low = gpu_word_vectors[bvh_nodes].values[2U * node];
    const uvec4 high = gpu_word_vectors[bvh_nodes].values[2U * node + 1U];
    first = low.w;
    count = high.w;
    return Aabb(uintBitsToFloat(low.xyz), uintBitsToFloat(high.xyz));
}

Aabb item_box(ShapeRecord record, uint item)
{
    return Aabb(gpu_vectors[bvh_boxes].values[2U * (record.first_box + item)].xyz, gpu_vectors[bvh_boxes].values[2U * (record.first_box + item) + 1U].xyz);
}

// Child index of a compound: its shape and its pose in the compound's frame.
Pose compound_child(ShapeRecord compound, uint index, out uint shape)
{
    const uvec4 first = gpu_word_vectors[compound_children].values[2U * (compound.first + index)];
    shape = first.w;
    return Pose(uintBitsToFloat(first.xyz), gpu_vectors[compound_children].values[2U * (compound.first + index) + 1U]);
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

// Up to four points sharing a normal (first to second); a point's separation is along it; a point's feature names its
// sub-shapes (triangles; NO_SUB_SHAPE for convex shapes) and its local features; the manifold names the sub-shapes it
// comes from (compound children or triangles).
struct Manifold
{
    vec3 normal;
    uint count;
    uint algorithm;
    uint sub_shape_first;
    uint sub_shape_second;
    vec3 positions[MANIFOLD_POINTS];
    float separations[MANIFOLD_POINTS];
    uint features[MANIFOLD_POINTS];
    uint point_sub_first[MANIFOLD_POINTS];
    uint point_sub_second[MANIFOLD_POINTS];
};

Manifold empty_manifold()
{
    Manifold manifold;
    manifold.normal = vec3(0.0);
    manifold.count = 0U;
    manifold.algorithm = 0U;
    manifold.sub_shape_first = NO_SUB_SHAPE;
    manifold.sub_shape_second = NO_SUB_SHAPE;
    for (uint point = 0U; point < MANIFOLD_POINTS; ++point)
    {
        manifold.positions[point] = vec3(0.0);
        manifold.separations[point] = 0.0;
        manifold.features[point] = 0U;
        manifold.point_sub_first[point] = NO_SUB_SHAPE;
        manifold.point_sub_second[point] = NO_SUB_SHAPE;
    }
    return manifold;
}

// The manifold with its shapes exchanged: the normal reversed, sub-shapes and features exchanged.
void flip_manifold(inout Manifold manifold)
{
    manifold.normal = -manifold.normal;
    const uint first = manifold.sub_shape_first;
    manifold.sub_shape_first = manifold.sub_shape_second;
    manifold.sub_shape_second = first;
    for (uint point = 0U; point < manifold.count; ++point)
    {
        manifold.features[point] = flipped_feature(manifold.features[point]);
        const uint point_first = manifold.point_sub_first[point];
        manifold.point_sub_first[point] = manifold.point_sub_second[point];
        manifold.point_sub_second[point] = point_first;
    }
}

// The smallest separation of its points (the deepest), as ContactManifold::deepest().
float deepest_separation(Manifold manifold)
{
    float smallest = float_maximum();
    for (uint point = 0U; point < manifold.count; ++point)
    {
        smallest = min(smallest, manifold.separations[point]);
    }
    return smallest;
}

// Manifolds in a buffer (GpuManifold, eleven vectors): header (instances, points, algorithm), sub-shapes, normal, four
// points (position and separation; sub-shapes and local feature). Unused points are zero.
const uint MANIFOLD_VECTORS = 11U;

void store_manifold(uint storage, uint slot, uint first_instance, uint second_instance, Manifold manifold)
{
    const uint base = MANIFOLD_VECTORS * slot;
    gpu_word_vectors[storage].values[base] = uvec4(first_instance, second_instance, manifold.count, manifold.count > 0U ? manifold.algorithm : 0U);
    gpu_word_vectors[storage].values[base + 1U] = manifold.count > 0U ? uvec4(manifold.sub_shape_first, manifold.sub_shape_second, 0U, 0U) : uvec4(0U);
    gpu_vectors[storage].values[base + 2U] = vec4(manifold.count > 0U ? manifold.normal : vec3(0.0), 0.0);
    for (uint point = 0U; point < MANIFOLD_POINTS; ++point)
    {
        const bool used = point < manifold.count;
        gpu_vectors[storage].values[base + 3U + 2U * point] = used ? vec4(manifold.positions[point], manifold.separations[point]) : vec4(0.0);
        gpu_word_vectors[storage].values[base + 4U + 2U * point] =
            used ? uvec4(manifold.point_sub_first[point], manifold.point_sub_second[point], manifold.features[point], 0U) : uvec4(0U);
    }
}

Manifold load_manifold(uint storage, uint slot, out uint first_instance, out uint second_instance)
{
    const uint base = MANIFOLD_VECTORS * slot;
    const uvec4 header = gpu_word_vectors[storage].values[base];
    const uvec4 subs = gpu_word_vectors[storage].values[base + 1U];
    first_instance = header.x;
    second_instance = header.y;
    Manifold manifold = empty_manifold();
    manifold.count = min(header.z, MANIFOLD_POINTS);
    manifold.algorithm = header.w;
    manifold.sub_shape_first = subs.x;
    manifold.sub_shape_second = subs.y;
    manifold.normal = gpu_vectors[storage].values[base + 2U].xyz;
    for (uint point = 0U; point < manifold.count; ++point)
    {
        const vec4 position = gpu_vectors[storage].values[base + 3U + 2U * point];
        const uvec4 feature = gpu_word_vectors[storage].values[base + 4U + 2U * point];
        manifold.positions[point] = position.xyz;
        manifold.separations[point] = position.w;
        manifold.point_sub_first[point] = feature.x;
        manifold.point_sub_second[point] = feature.y;
        manifold.features[point] = feature.z;
    }
    return manifold;
}

// The manifold of the pair: what the passes write.
Manifold result;

// ===================================================================================================== sub-pairs

// What a candidate pair is made of, as physics::collide() visits it: two convex shapes (their world poses), a convex shape
// and a triangle of a mesh or a height field (shape a convex, shape b the triangles' shape; collided in its frame; FLIPPED:
// the triangles' shape is the pair's first), or two triangles (shape a's and shape b's). The manifold's sub-shapes are
// recorded as collide() records them.
const uint SUB_PAIR_CONVEX = 0U;
const uint SUB_PAIR_CONVEX_TRIANGLE = 1U;
const uint SUB_PAIR_TRIANGLES = 2U;
const uint SUB_PAIR_FLIPPED = 0x100U;
const uint SUB_PAIR_VECTORS = 7U;

struct SubPair
{
    uint first_instance;
    uint second_instance;
    uint kind;
    uint sub_first;
    uint sub_second;
    uint shape_a;
    uint shape_b;
    uint triangle_a;
    uint triangle_b;
    Pose pose_a;
    Pose pose_b;
};

void store_sub_pair(uint storage, uint slot, SubPair pair)
{
    const uint base = SUB_PAIR_VECTORS * slot;
    gpu_word_vectors[storage].values[base] = uvec4(pair.first_instance, pair.second_instance, pair.kind, 0U);
    gpu_word_vectors[storage].values[base + 1U] = uvec4(pair.sub_first, pair.sub_second, pair.shape_a, pair.shape_b);
    gpu_word_vectors[storage].values[base + 2U] = uvec4(pair.triangle_a, pair.triangle_b, 0U, 0U);
    gpu_vectors[storage].values[base + 3U] = vec4(pair.pose_a.position, 0.0);
    gpu_vectors[storage].values[base + 4U] = pair.pose_a.orientation;
    gpu_vectors[storage].values[base + 5U] = vec4(pair.pose_b.position, 0.0);
    gpu_vectors[storage].values[base + 6U] = pair.pose_b.orientation;
}

SubPair load_sub_pair(uint storage, uint slot)
{
    const uint base = SUB_PAIR_VECTORS * slot;
    const uvec4 head = gpu_word_vectors[storage].values[base];
    const uvec4 shapes = gpu_word_vectors[storage].values[base + 1U];
    const uvec4 triangles = gpu_word_vectors[storage].values[base + 2U];
    return SubPair(head.x, head.y, head.z, shapes.x, shapes.y, shapes.z, shapes.w, triangles.x, triangles.y,
                   Pose(gpu_vectors[storage].values[base + 3U].xyz, gpu_vectors[storage].values[base + 4U]),
                   Pose(gpu_vectors[storage].values[base + 5U].xyz, gpu_vectors[storage].values[base + 6U]));
}

#endif
