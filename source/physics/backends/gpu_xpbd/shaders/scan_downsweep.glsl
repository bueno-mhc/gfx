// The last pass of an exclusive scan of 32-bit words: each workgroup scans its tile, starting from its tile's offset (the
// scanned partials of the level above), or from 0 at the top level, which also writes the total. The output may be the
// input: each invocation reads its elements before it writes them, and no other invocation touches them.
#include "primitives.glsl"
#include "workgroup.glsl"

layout(push_constant) uniform Push
{
    uint input_buffer;
    uint input_first;
    uint output_buffer;
    uint output_first;
    uint offsets_buffer; // the scanned partials of the level above (INVALID_INDEX: none, the top level)
    uint offsets_first;
    uint maximum;
    uint count_buffer;
    uint count_word;
    uint total_buffer;   // where the top level writes the sum of every element (INVALID_INDEX: nowhere)
    uint total_word;
} push;

void main()
{
    const uint count = resolve_count(push.maximum, push.count_buffer, push.count_word);

    // This invocation's sum, its place in the workgroup, the tile's offset.
    uint sum = 0U;
    for (uint item = 0U; item < ITEMS_PER_INVOCATION; ++item)
    {
        const uint index = blocked_index(item);
        if (index < count)
        {
            sum += gpu_words[push.input_buffer].words[push.input_first + index];
        }
    }
    uint total;
    uint running = workgroup_exclusive_add(sum, total);
    if (push.offsets_buffer != INVALID_INDEX)
    {
        running += gpu_words[push.offsets_buffer].words[push.offsets_first + gl_WorkGroupID.x];
    }

    // Each element gets the sum of the elements before it.
    for (uint item = 0U; item < ITEMS_PER_INVOCATION; ++item)
    {
        const uint index = blocked_index(item);
        if (index < count)
        {
            const uint value = gpu_words[push.input_buffer].words[push.input_first + index];
            gpu_words[push.output_buffer].words[push.output_first + index] = running;
            running += value;
        }
    }
    if (push.offsets_buffer == INVALID_INDEX && push.total_buffer != INVALID_INDEX && gl_LocalInvocationIndex == 0U)
    {
        gpu_words[push.total_buffer].words[push.total_word] = total;
    }
}
