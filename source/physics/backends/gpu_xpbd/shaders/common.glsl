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
layout(constant_id = 9) const uint BVH_LEAF = 0U;
layout(constant_id = 10) const uint BVH_STACK_SIZE = 1U;
layout(constant_id = 11) const uint TREELET_LEAVES = 2U; // (the smallest the treelet pass compiles with: arrays it sizes)
layout(constant_id = 12) const float BVH_INTERNAL_COST = 0.0;
layout(constant_id = 13) const float BVH_LEAF_COST = 0.0;
layout(constant_id = 14) const uint MORTON_BITS = 0U;
layout(constant_id = 15) const uint BVH_STACK_OVERFLOW = 0U;

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

// Vectors of words: what holds floats and indices side by side (BVH nodes: a box's corner and a child), so that the
// indices' bits never pass through a float.
layout(set = 0, binding = 3, std430) buffer GpuWordVectorBuffer
{
    uvec4 values[];
} gpu_word_vectors[];

// Positive infinity (the minimum of an empty box).
float infinity()
{
    return uintBitsToFloat(0x7F800000U);
}

#endif
