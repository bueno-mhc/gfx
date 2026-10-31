// A pass of the reduction of boxes (two vectors each: minimum, maximum) to the box around them: each workgroup reduces its
// tile into one box of the output (the level above's input, or the result at the top). An empty tile gives the empty box
// (+∞, −∞).
#include "primitives.glsl"
#include "workgroup.glsl"

layout(push_constant) uniform Push
{
    uint boxes_buffer;
    uint boxes_first;  // the first box (two vectors each)
    uint output_buffer;
    uint output_first; // the box of the first workgroup
    uint maximum;
    uint count_buffer;
    uint count_word;
} push;

void main()
{
    const uint count = resolve_count(push.maximum, push.count_buffer, push.count_word);
    vec3 minimum = vec3(infinity());
    vec3 maximum = vec3(-infinity());
    for (uint item = 0U; item < ITEMS_PER_INVOCATION; ++item)
    {
        const uint index = blocked_index(item);
        if (index < count)
        {
            const uint box = 2U * (push.boxes_first + index);
            minimum = min(minimum, gpu_vectors[push.boxes_buffer].values[box].xyz);
            maximum = max(maximum, gpu_vectors[push.boxes_buffer].values[box + 1U].xyz);
        }
    }
    workgroup_bounds(minimum, maximum);
    if (gl_LocalInvocationIndex == 0U)
    {
        const uint box = 2U * (push.output_first + gl_WorkGroupID.x);
        gpu_vectors[push.output_buffer].values[box] = vec4(minimum, 0.0);
        gpu_vectors[push.output_buffer].values[box + 1U] = vec4(maximum, 0.0);
    }
}
