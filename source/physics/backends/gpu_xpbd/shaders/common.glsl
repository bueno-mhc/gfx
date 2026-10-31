// What every shader of the GPU backend shares: its extensions (included first), the specialization constants the C++ side
// gives (gpu_resources.cppm: SPECIALIZATION_CONSTANTS, values from gfx.physics.enums and the graphics library), and the
// bindless storage buffers as typed arrays (aliases of the graphics library's bindless binding: set 0, binding 3).
// GPU_XPBD_SUBGROUPS, defined before it is included, selects the subgroup variant of the workgroup operations.
#ifndef GPU_XPBD_COMMON_GLSL
#define GPU_XPBD_COMMON_GLSL

#extension GL_EXT_nonuniform_qualifier : require
#ifdef GPU_XPBD_SUBGROUPS
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#endif

// ===================================================================================================== constants
// Their values come from the C++ side; the defaults make a forgotten one visible (nothing is simulated, nothing summed).
layout(local_size_x_id = 0) in;
layout(constant_id = 1) const uint BODY_SIMULATED = 0U;
layout(constant_id = 2) const uint BODY_DYNAMIC = 0U;
layout(constant_id = 3) const uint BODY_KINEMATIC = 0U;
layout(constant_id = 4) const uint BODY_AWAKE = 0U;
layout(constant_id = 5) const uint BODY_LOCK_SHIFT = 0U;
layout(constant_id = 6) const uint INVALID_INDEX = 0U;
layout(constant_id = 7) const uint ITEMS_PER_INVOCATION = 0U;
layout(constant_id = 8) const uint RADIX_BITS = 0U;

// Elements of a workgroup's tile, and digits of a radix pass.
const uint TILE_SIZE = gl_WorkGroupSize.x * ITEMS_PER_INVOCATION;
const uint RADIX_SIZE = 1U << RADIX_BITS;

// ===================================================================================================== buffers

layout(set = 0, binding = 3, std430) buffer GpuVectorBuffer
{
    vec4 values[];
} gpu_vectors[];

layout(set = 0, binding = 3, std430) buffer GpuWordBuffer
{
    uint words[];
} gpu_words[];

// Positive infinity (the minimum of an empty box).
float infinity()
{
    return uintBitsToFloat(0x7F800000U);
}

#endif
