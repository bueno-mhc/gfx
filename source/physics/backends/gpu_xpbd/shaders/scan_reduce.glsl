// The first pass of an exclusive scan of 32-bit words (reduce, then scan the sums, then scan within each tile): each
// workgroup sums its tile of the input into its partial. Tiles past the count sum to 0.
#include "primitives.glsl"
#include "workgroup.glsl"

layout(push_constant) uniform Push
{
    uint input_buffer;
    uint input_first;
    uint partials_buffer;
    uint partials_first;
    uint maximum;
    uint count_buffer;
    uint count_word;
} push;

void main()
{
    const uint count = resolve_count(push.maximum, push.count_buffer, push.count_word);
    uint sum = 0U;
    for (uint item = 0U; item < ITEMS_PER_INVOCATION; ++item)
    {
        const uint index = blocked_index(item);
        if (index < count)
        {
            sum += gpu_words[push.input_buffer].words[push.input_first + index];
        }
    }
    const uint total = workgroup_add(sum);
    if (gl_LocalInvocationIndex == 0U)
    {
        gpu_words[push.partials_buffer].words[push.partials_first + gl_WorkGroupID.x] = total;
    }
}
