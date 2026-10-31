// What the passes of a step share: the step's parameters and the body patches, the push constants naming the
// parameters, quaternion and body helpers (common.glsl has the constants and the typed buffers).
#ifndef GPU_XPBD_STEP_GLSL
#define GPU_XPBD_STEP_GLSL

#include "common.glsl"

// ===================================================================================================== layouts
// GpuStepParameters in gpu_resources.cppm.
struct StepParameters
{
    vec4 gravity; // xyz: the world's gravity; w: the substep's duration
    uint body_count;
    uint patch_count;
    uint substeps;
    uint reserved;
    uint body_centers;
    uint body_orientations;
    uint body_linear_velocities;
    uint body_angular_velocities;
    uint body_inverse_inertias;
    uint body_inertias;
    uint body_forces;
    uint body_torques;
    uint body_displacements;
    uint body_rotations;
    uint body_flags;
    uint body_patches;
};

// GpuBodyPatch in gpu_resources.cppm.
struct BodyPatch
{
    uint slot;
    uint flags;
    uint reserved_0;
    uint reserved_1;
    vec4 center;                   // w: inverse mass
    vec4 orientation;
    vec4 linear_velocity;          // w: linear damping
    vec4 angular_velocity;         // w: angular damping
    vec4 inverse_inertia_diagonal; // w: gravity scale
    vec4 inverse_inertia_off;
    vec4 inertia_diagonal;
    vec4 inertia_off;
    vec4 force;
    vec4 torque;
};

layout(set = 0, binding = 3, std430) readonly buffer GpuParameterBuffer
{
    StepParameters parameters;
} gpu_parameters[];

layout(set = 0, binding = 3, std430) readonly buffer GpuBodyPatchBuffer
{
    BodyPatch patches[];
} gpu_body_patches[];

layout(push_constant) uniform Push
{
    uint parameters; // bindless index of the step's parameters
} push;

#define PARAMETERS gpu_parameters[push.parameters].parameters

// ===================================================================================================== quaternions (x, y, z, w)

// The Hamilton product a b.
vec4 quaternion_multiply(vec4 a, vec4 b)
{
    return vec4(a.w * b.xyz + b.w * a.xyz + cross(a.xyz, b.xyz), a.w * b.w - dot(a.xyz, b.xyz));
}

// The conjugate (the inverse rotation of a unit quaternion).
vec4 quaternion_conjugate(vec4 q)
{
    return vec4(-q.xyz, q.w);
}

// The rotation matrix of a unit quaternion.
mat3 rotation_matrix(vec4 q)
{
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;
    return mat3(vec3(1.0 - 2.0 * (yy + zz), 2.0 * (xy + wz), 2.0 * (xz - wy)),
                vec3(2.0 * (xy - wz), 1.0 - 2.0 * (xx + zz), 2.0 * (yz + wx)),
                vec3(2.0 * (xz + wy), 2.0 * (yz - wx), 1.0 - 2.0 * (xx + yy)));
}

// The symmetric matrix of a diagonal (xx, yy, zz) and off-diagonal terms (xy, xz, yz).
mat3 symmetric_matrix(vec3 diagonal, vec3 off)
{
    return mat3(vec3(diagonal.x, off.x, off.y), vec3(off.x, diagonal.y, off.z), vec3(off.y, off.z, diagonal.z));
}

// ===================================================================================================== bodies

// 1 along the world axes a body may move along (linear) or turn about (angular), 0 along the locked ones.
vec3 body_freedom(uint flags, bool angular)
{
    const uint locks = (flags >> BODY_LOCK_SHIFT) >> (angular ? 3U : 0U);
    return vec3((locks & 1U) == 0U ? 1.0 : 0.0, (locks & 2U) == 0U ? 1.0 : 0.0, (locks & 4U) == 0U ? 1.0 : 0.0);
}

// Whether a body of these flags moves in this step (an awake dynamic or kinematic body).
bool body_moves(uint flags)
{
    return (flags & BODY_SIMULATED) != 0U && (flags & BODY_AWAKE) != 0U && (flags & (BODY_DYNAMIC | BODY_KINEMATIC)) != 0U;
}

#endif
