// What the solver's passes share (GpuConstraintSolver in gpu_solver.cppm): the solver's parameters and push constants,
// the words of its counts, the layouts of colliders, instances, manifolds, contact constraints and joints, the
// constraints (joints first, then contacts) as the graph, the coloring and the islands see them, and the nodes they join
// — the rigid bodies, then the soft bodies' particles (node body_count + p: a body without orientation or inertia) — as
// the constraint projections and the velocity pass see them.
#ifndef GPU_XPBD_SOLVER_GLSL
#define GPU_XPBD_SOLVER_GLSL

#define GPU_XPBD_SOLVER_PUSH
#include "step.glsl"

layout(constant_id = 46) const uint BODY_SLEEPS = 0U;
layout(constant_id = 47) const uint MAXIMUM_COLORS = 32U;

// GpuJointPartKind, the absent body (NO_SOLVER_BODY: the world), GpuJointFlag.
layout(constant_id = 48) const uint JOINT_ANCHORS = 0U;
layout(constant_id = 49) const uint JOINT_DISTANCE = 0U;
layout(constant_id = 50) const uint JOINT_ALIGNED_AXIS = 0U;
layout(constant_id = 51) const uint JOINT_ORIENTATION = 0U;
layout(constant_id = 52) const uint JOINT_ANGLE = 0U;
layout(constant_id = 53) const uint JOINT_SWING = 0U;
layout(constant_id = 54) const uint JOINT_LINEAR_MOTOR = 0U;
layout(constant_id = 55) const uint JOINT_ANGULAR_MOTOR = 0U;
layout(constant_id = 56) const uint NO_BODY = 0U;
layout(constant_id = 57) const uint JOINT_ACTIVE = 0U;
layout(constant_id = 58) const uint JOINT_SOLVED = 0U;
layout(constant_id = 59) const uint JOINT_BROKEN = 0U;
layout(constant_id = 60) const uint JOINT_PIN = 0U;

// ===================================================================================================== layouts
// GpuSolverParameters in gpu_solver.cppm.
struct SolverParameters
{
    float speculative_distance;
    float contact_hertz;
    float maximum_push_speed;
    float restitution_threshold;
    float step_seconds;
    float sleep_linear_threshold;
    float sleep_angular_threshold;
    float sleep_time;
    uint sleep_enabled;
    uint moving_count;
    uint static_count;
    uint within_capacity;
    uint between_capacity;
    uint manifold_capacity;
    uint body_count;
    uint colliders;
    uint collider_list;
    uint instances;
    uint raw_pairs;
    uint counts;
    uint previous_counts;
    uint pair_flags;
    uint pair_indices;
    uint pairs;
    uint manifolds;
    uint predecessors;
    uint contacts;
    uint previous_contacts;
    uint multipliers;
    uint graph_keys;
    uint graph_values;
    uint graph_sorted_keys;
    uint graph_sorted_values;
    uint body_begin;
    uint body_end;
    uint colors_first;
    uint colors_second;
    uint color_keys;
    uint color_values;
    uint color_sorted_keys;
    uint color_lists;
    uint color_begin;
    uint color_end;
    uint labels;
    uint island_flags;
    uint island_timers;
    uint corrections;
    uint joint_count;
    uint joined_count;
    uint joints;
    uint joint_parts;
    uint joint_multipliers;
    uint joint_impulses;
    uint joint_results;
    uint joined_pairs;
    uint node_count; // the rigid bodies and the particles
    uint overflow_counts; // per node: its overflow constraints, all (positions) and contacts (velocities)
    float world_speculative_distance; // the world's own
    uint boxes;           // the instances' boxes (grown by speculative_distance)
    uint pair_capacity;   // kept pairs at most
    uint report_capacity; // contact reports at most
    uint report_flags;    // a word per manifold slot: reported or not
    uint report_indices;  // the reported slots, compacted
    uint reports;         // their reports (GpuContactReport)
    uint trigger_count;   // triggers (their instances after the moving and static colliders')
    uint trigger_within_capacity;   // the trigger tree's pairs with the moving tree at most
    uint trigger_between_capacity;  // with the static tree at most
    uint trigger_pair_capacity;     // kept (and overlapping) at most
    uint trigger_manifold_capacity; // their manifold slots at most
    uint trigger_raw_pairs;
    uint trigger_pair_flags;
    uint trigger_pair_indices;
    uint trigger_pairs;
    uint trigger_ranges;
    uint trigger_manifolds;
    uint trigger_results;
    uint tables; // the narrow phase's shape table (the contacts' margins)
};

layout(set = 0, binding = 3, std430) readonly buffer GpuSolverParameterBuffer
{
    SolverParameters parameters;
} gpu_solver_parameters[];

layout(push_constant) uniform Push
{
    uint parameters; // bindless index of the step's parameters
    uint solver;     // and of the solver's
    uint argument;   // the pass's: a color key, a round
    uint mode;       // the pass's mode bits
} push;

#define SOLVER gpu_solver_parameters[push.solver].parameters

#include "particles.glsl"

// The words of the counts (GpuSolverCounts in gpu_solver.cppm).
const uint COUNT_WITHIN_PAIRS = 0U;
const uint COUNT_BETWEEN_PAIRS = 1U;
const uint COUNT_CANDIDATES = 2U;
const uint COUNT_MANIFOLD_SLOTS = 3U;
const uint COUNT_COLORS = 4U;
const uint COUNT_OVERFLOW_CONSTRAINTS = 5U;
const uint COUNT_ISLAND_CHANGES = 6U;
const uint COUNT_ACTIVE_CONSTRAINTS = 7U;
const uint COUNT_CONTACT_POINTS = 8U;
const uint COUNT_SLOTS = 9U;
const uint COUNT_GRAPH_ENTRIES = 10U;
const uint COUNT_CONSTRAINTS = 11U;
const uint COUNT_REPORTED = 12U;
const uint COUNT_TOUCHING_POINTS = 13U;
const uint COUNT_TRIGGER_MOVING_PAIRS = 14U;
const uint COUNT_TRIGGER_STATIC_PAIRS = 15U;
const uint COUNT_TRIGGER_CANDIDATES = 16U;
const uint COUNT_TRIGGER_SLOTS = 17U;
const uint COUNT_TRIGGER_OVERLAPS = 18U;

uint count_word(uint word)
{
    return gpu_words[SOLVER.counts].words[word];
}

// The manifold slots of this step: what the narrow phase needed, at most its capacity.
uint slot_count()
{
    return count_word(COUNT_SLOTS);
}

// The constraints of this step: the joint slots (constraints 0 .. joint_count - 1), then the manifold slots.
uint constraint_count()
{
    return count_word(COUNT_CONSTRAINTS);
}

// Colors: 0 .. MAXIMUM_COLORS - 1, then the overflow set (solved Jacobi-style), then inactive constraints; uncolored
// while the coloring runs. A constraint's color key is its color × 2 + its kind (joints 0, contacts 1): the lists by key
// give each pass the constraints of one color and kind.
const uint UNCOLORED = 0xFFFFFFFFU;
const uint OVERFLOW_COLOR = MAXIMUM_COLORS;
const uint INACTIVE_COLOR = MAXIMUM_COLORS + 1U;

uint color_key(uint color, uint constraint)
{
    return 2U * color + (constraint < SOLVER.joint_count ? 0U : 1U);
}

// A hash of words (a murmur finalizer chain): the coloring's priority of a constraint, from what identifies it.
uint hash_words(uvec4 words)
{
    uint hash = 0x9E3779B9U;
    for (uint index = 0U; index < 4U; ++index)
    {
        uint value = words[index] * 0xCC9E2D51U;
        value = (value << 15U) | (value >> 17U);
        hash ^= value * 0x1B873593U;
        hash = ((hash << 13U) | (hash >> 19U)) * 5U + 0xE6546B64U;
    }
    hash ^= hash >> 16U;
    hash *= 0x85EBCA6BU;
    hash ^= hash >> 13U;
    hash *= 0xC2B2AE35U;
    return hash ^ (hash >> 16U);
}

// A collider (GpuCollider): its pose relative to its body's center of mass, in the body's frame (the shape in the first
// vector's w); its body (a node: a particle's collider is its sphere), flags (bit 0: live; bit 1: a particle's), group;
// its filter (category and mask, two words each); its material.
const uint COLLIDER_VECTORS = 5U;
const uint COLLIDER_LIVE = 1U;
const uint COLLIDER_PARTICLE = 2U;
const uint COLLIDER_TRIGGER = 4U;

struct Collider
{
    vec3 position;
    vec4 orientation;
    uint body;
    uint shape;
    uint flags;
    int group;
    uvec4 filter_words; // category low, high; mask low, high
    vec4 material;      // static friction, dynamic friction, rolling resistance, restitution
};

// Whether two colliders' filters let them meet (CollisionFilter's should_collide(): a common non-zero group decides
// alone, else each one's category in the other's mask).
bool should_collide(Collider first, Collider second)
{
    if (first.group == second.group && first.group != 0)
    {
        return first.group > 0;
    }
    const bool first_in_second = ((first.filter_words.x & second.filter_words.z) | (first.filter_words.y & second.filter_words.w)) != 0U;
    const bool second_in_first = ((second.filter_words.x & first.filter_words.z) | (second.filter_words.y & first.filter_words.w)) != 0U;
    return first_in_second && second_in_first;
}

Collider load_collider(uint slot)
{
    const uint base = COLLIDER_VECTORS * slot;
    const uvec4 header = gpu_word_vectors[SOLVER.colliders].values[base + 2U];
    return Collider(gpu_vectors[SOLVER.colliders].values[base].xyz, gpu_vectors[SOLVER.colliders].values[base + 1U], header.x, header.y, header.z,
                    int(header.w), gpu_word_vectors[SOLVER.colliders].values[base + 3U], gpu_vectors[SOLVER.colliders].values[base + 4U]);
}

// The collider of an instance (instances: the moving colliders' list, then the static colliders').
uint instance_collider(uint instance)
{
    return gpu_words[SOLVER.collider_list].words[instance];
}

// A contact constraint (GpuContact), one per manifold slot: its bodies, points, flags; its normal (first to second; w:
// restitution); its material (static and dynamic friction, rolling resistance; w: its coloring priority); per point, the
// anchors on the first and second bodies' cores (relative to their centers of mass, body frames: the point moved into the
// shape by its margin along the normal, so a turning round shape keeps its point under it; w of the first: the separation
// at the step's start) and the friction anchors (the material points on the surfaces; w of the first: 1 when static
// friction gave way); last, the margins of the two shapes where they touch (x: the first's, y: the second's). Flags: active
// (the manifold has points), touching (a point touches, or will within the step: counted by the Jacobi gather's mass
// splitting), pushed (the solver pushed its bodies apart in a substep: reported). The second anchor's w: the point's approach
// speed at the step's start.
const uint CONTACT_VECTORS = 20U;
const uint CONTACT_MARGINS = 19U;
const uint CONTACT_ACTIVE = 1U;
const uint CONTACT_TOUCHING = 2U;
const uint CONTACT_PUSHED = 4U;

uint contact_base(uint slot)
{
    return CONTACT_VECTORS * slot;
}

// A joint (GpuJoint), by joint slot: its bodies (the second NO_BODY for the world), flags and part count; its first part;
// its frame on the first body (position about the center of mass, body frame; w: the breaking force) and orientation;
// its frame on the second (w: the breaking torque) and orientation.
const uint JOINT_VECTORS = 6U;

uint joint_base(uint joint)
{
    return JOINT_VECTORS * joint;
}

// A joint's part (GpuJointPart): its kind and axis; its lower bounds (w: spring frequency); its upper bounds (w: spring
// damping ratio).
const uint JOINT_PART_VECTORS = 3U;

// What the graph, the coloring and the islands see of a constraint: its two bodies (the second NO_BODY for the world),
// whether it is in use, its coloring priority.
struct Constraint
{
    uint first;
    uint second;
    bool in_use;
    uint priority;
};

Constraint load_constraint(uint constraint)
{
    if (constraint < SOLVER.joint_count)
    {
        const uvec4 header = gpu_word_vectors[SOLVER.joints].values[joint_base(constraint)];
        return Constraint(header.x, header.y, (header.z & JOINT_ACTIVE) != 0U, hash_words(uvec4(constraint, 0xFFFFFFFFU, 0xFFFFFFFFU, 0x4A4F494EU)));
    }
    const uint base = contact_base(constraint - SOLVER.joint_count);
    const uvec4 header = gpu_word_vectors[SOLVER.contacts].values[base];
    return Constraint(header.x, header.y, (header.w & CONTACT_ACTIVE) != 0U, gpu_word_vectors[SOLVER.contacts].values[base + 2U].w);
}

// Whether a node is a particle, and which.
bool is_particle(uint node)
{
    return node != NO_BODY && node >= PARAMETERS.body_count;
}

uint particle_of(uint node)
{
    return node - PARAMETERS.body_count;
}

// A node's flags (none for the world; a particle's GpuBodyFlag bits).
uint body_flags_of(uint body)
{
    if (body == NO_BODY)
    {
        return 0U;
    }
    return is_particle(body) ? particle_header(particle_of(body)).y : gpu_words[PARAMETERS.body_flags].words[body];
}

// A node's center (of mass) and orientation (a particle's: unturned).
vec3 node_center(uint node)
{
    return is_particle(node) ? gpu_vectors[PARAMETERS.particle_positions].values[particle_of(node)].xyz : gpu_vectors[PARAMETERS.body_centers].values[node].xyz;
}

vec4 node_orientation(uint node)
{
    return is_particle(node) ? vec4(0.0, 0.0, 0.0, 1.0) : gpu_vectors[PARAMETERS.body_orientations].values[node];
}

// The body that moves through constraints: awake and dynamic (asleep and other bodies keep still: infinite mass).
struct Body
{
    vec3 center;
    vec4 orientation;
    vec3 inverse_mass;     // per world axis (0 along locked axes)
    mat3 inverse_inertia;  // world frame (0 about locked axes)
    vec3 displacement;     // of the center, this substep
    vec3 rotation;         // rotation vector, this substep
    vec3 linear_velocity;
    vec3 angular_velocity;
    bool moves;
};

bool body_is_dynamic_awake(uint flags)
{
    return (flags & BODY_SIMULATED) != 0U && (flags & BODY_DYNAMIC) != 0U && (flags & BODY_AWAKE) != 0U;
}

// The world inverse inertia of a body (0 about its locked axes), as xpbd_integrate.comp computes it.
mat3 world_inverse_inertia(uint slot, uint flags, vec4 orientation)
{
    const vec4 inverse_diagonal = gpu_vectors[PARAMETERS.body_inverse_inertias].values[2U * slot];
    const vec3 inverse_off = gpu_vectors[PARAMETERS.body_inverse_inertias].values[2U * slot + 1U].xyz;
    const mat3 rotation = rotation_matrix(orientation);
    const vec3 angular_freedom = body_freedom(flags, true);
    const mat3 freedom = mat3(vec3(angular_freedom.x, 0.0, 0.0), vec3(0.0, angular_freedom.y, 0.0), vec3(0.0, 0.0, angular_freedom.z));
    return freedom * rotation * symmetric_matrix(inverse_diagonal.xyz, inverse_off) * transpose(rotation) * freedom;
}

Body load_body(uint slot)
{
    if (slot == NO_BODY)
    {
        // The world: at the origin, unturned, still.
        return Body(vec3(0.0), vec4(0.0, 0.0, 0.0, 1.0), vec3(0.0), mat3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), false);
    }
    if (is_particle(slot))
    {
        // A particle: a point mass (no turn).
        const uint particle = particle_of(slot);
        const uvec4 header = particle_header(particle);
        const bool moves = body_is_dynamic_awake(header.y);
        return Body(gpu_vectors[PARAMETERS.particle_positions].values[particle].xyz, vec4(0.0, 0.0, 0.0, 1.0), vec3(moves ? uintBitsToFloat(header.x) : 0.0), mat3(0.0),
                    gpu_vectors[PARAMETERS.particle_displacements].values[particle].xyz, vec3(0.0), gpu_vectors[PARAMETERS.particle_velocities].values[particle].xyz,
                    vec3(0.0), moves);
    }
    const uint flags = gpu_words[PARAMETERS.body_flags].words[slot];
    const vec4 center = gpu_vectors[PARAMETERS.body_centers].values[slot];
    const vec4 orientation = gpu_vectors[PARAMETERS.body_orientations].values[slot];
    Body body;
    body.center = center.xyz;
    body.orientation = orientation;
    body.moves = body_is_dynamic_awake(flags);
    body.inverse_mass = body.moves ? body_freedom(flags, false) * center.w : vec3(0.0);
    body.inverse_inertia = body.moves ? world_inverse_inertia(slot, flags, orientation) : mat3(0.0);
    body.displacement = gpu_vectors[PARAMETERS.body_displacements].values[slot].xyz;
    body.rotation = gpu_vectors[PARAMETERS.body_rotations].values[slot].xyz;
    body.linear_velocity = gpu_vectors[PARAMETERS.body_linear_velocities].values[slot].xyz;
    body.angular_velocity = gpu_vectors[PARAMETERS.body_angular_velocities].values[slot].xyz;
    return body;
}

// The pose and the substep's displacement and rotation of a body the projections moved.
void store_body_pose(uint slot, Body body)
{
    if (!body.moves)
    {
        return;
    }
    if (is_particle(slot))
    {
        gpu_vectors[PARAMETERS.particle_positions].values[particle_of(slot)].xyz = body.center;
        gpu_vectors[PARAMETERS.particle_displacements].values[particle_of(slot)].xyz = body.displacement;
        return;
    }
    gpu_vectors[PARAMETERS.body_centers].values[slot].xyz = body.center;
    gpu_vectors[PARAMETERS.body_orientations].values[slot] = body.orientation;
    gpu_vectors[PARAMETERS.body_displacements].values[slot].xyz = body.displacement;
    gpu_vectors[PARAMETERS.body_rotations].values[slot].xyz = body.rotation;
}

// The velocities of a body the velocity pass changed.
void store_body_velocities(uint slot, Body body)
{
    if (!body.moves)
    {
        return;
    }
    if (is_particle(slot))
    {
        gpu_vectors[PARAMETERS.particle_velocities].values[particle_of(slot)].xyz = body.linear_velocity;
        return;
    }
    gpu_vectors[PARAMETERS.body_linear_velocities].values[slot].xyz = body.linear_velocity;
    gpu_vectors[PARAMETERS.body_angular_velocities].values[slot].xyz = body.angular_velocity;
}

// A node's part in the overflow set (mass splitting, jacobi_gather.comp): its inverse mass and inertia times its count of
// overflow constraints of the pass (@p velocities: the velocity pass's, contacts only), at least 1. The gather divides the
// node's corrections by the same count, so each constraint gives both sides the same impulse whatever the count.
void split_node(inout Body body, uint node, bool velocities)
{
    if (!body.moves)
    {
        return;
    }
    const float parts = float(max(gpu_words[SOLVER.overflow_counts].words[2U * node + (velocities ? 1U : 0U)], 1U));
    body.inverse_mass *= parts;
    body.inverse_inertia *= parts;
}

// The generalized inverse mass of a body at the lever r along direction (Müller et al. 2020, eq. 2).
float generalized_inverse_mass(Body body, vec3 r, vec3 direction)
{
    const vec3 turn = cross(r, direction);
    return dot(direction, body.inverse_mass * direction) + dot(turn, body.inverse_inertia * turn);
}

// Turns an orientation by a small rotation vector (Müller et al. 2020, eq. 8), normalized.
vec4 turned(vec4 orientation, vec3 rotation)
{
    return normalize(orientation + 0.5 * quaternion_multiply(vec4(rotation, 0.0), orientation));
}

// A positional correction p (a multiplier times a gradient) applied at the lever r: the center moves, the orientation
// turns, and the substep's displacement and rotation take them (the velocity update divides them by the substep).
void correct_position(inout Body body, vec3 r, vec3 p)
{
    const vec3 move = body.inverse_mass * p;
    const vec3 turn = body.inverse_inertia * cross(r, p);
    body.center += move;
    body.orientation = turned(body.orientation, turn);
    body.displacement += move;
    body.rotation += turn;
}

// An angular correction p (a multiplier times a gradient, an axis): the orientation turns, the substep's rotation takes it.
void correct_rotation(inout Body body, vec3 p)
{
    const vec3 turn = body.inverse_inertia * p;
    body.orientation = turned(body.orientation, turn);
    body.rotation += turn;
}

// An impulse p applied at the lever r.
void correct_velocity(inout Body body, vec3 r, vec3 p)
{
    body.linear_velocity += body.inverse_mass * p;
    body.angular_velocity += body.inverse_inertia * cross(r, p);
}

// The constraint list of a pass: the constraints of color key push.argument (a color's joints or contacts; the overflow
// set's for OVERFLOW_COLOR).
uint color_count(uint key)
{
    return gpu_words[SOLVER.color_end].words[key] - gpu_words[SOLVER.color_begin].words[key];
}

uint color_constraint(uint key, uint index)
{
    return gpu_words[SOLVER.color_lists].words[gpu_words[SOLVER.color_begin].words[key] + index];
}

// The levers from the bodies' centers to a contact's point on each surface: the anchors on the cores, turned with the
// bodies, then out along the normal by the shapes' margins.
void contact_levers(uint base, uint point, Body first, Body second, vec3 normal, out vec3 lever_first, out vec3 lever_second)
{
    const uint anchors = base + 3U + 4U * point;
    const vec2 margins = gpu_vectors[SOLVER.contacts].values[base + CONTACT_MARGINS].xy;
    lever_first = rotate_by(first.orientation, gpu_vectors[SOLVER.contacts].values[anchors].xyz) + normal * margins.x;
    lever_second = rotate_by(second.orientation, gpu_vectors[SOLVER.contacts].values[anchors + 1U].xyz) - normal * margins.y;
}

#endif
