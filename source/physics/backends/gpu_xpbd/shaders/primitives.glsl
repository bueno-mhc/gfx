// What the data-parallel primitives share (GpuPrimitives in gpu_primitives.cppm): spans are a bindless index and a first
// element; a count is a constant, or a word a pass wrote when its buffer is valid (never more than the constant, the
// maximum the dispatches were sized for).
#ifndef GPU_XPBD_PRIMITIVES_GLSL
#define GPU_XPBD_PRIMITIVES_GLSL

#include "common.glsl"

// The count of a primitive.
uint resolve_count(uint maximum, uint count_buffer, uint count_word)
{
    return count_buffer == INVALID_INDEX ? maximum : min(gpu_words[count_buffer].words[count_word], maximum);
}

// Element item of this invocation in its workgroup's tile, the elements of a tile laid out invocation by invocation
// (consecutive elements per invocation: what an invocation-ordered scan needs).
uint blocked_index(uint item)
{
    return gl_WorkGroupID.x * TILE_SIZE + gl_LocalInvocationIndex * ITEMS_PER_INVOCATION + item;
}

// Element item of this invocation, the elements of a tile laid out round by round (consecutive invocations read
// consecutive elements: what keeps the radix passes' rounds in element order).
uint striped_index(uint item)
{
    return gl_WorkGroupID.x * TILE_SIZE + item * gl_WorkGroupSize.x + gl_LocalInvocationIndex;
}

#endif
