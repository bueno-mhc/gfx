// Operations over a workgroup's invocations: sums, exclusive prefix sums, minimums and maximums. Two variants, with one
// result: subgroup arithmetic within each subgroup, then across the subgroups (GPU_XPBD_SUBGROUPS: at most
// GPU_XPBD_MINIMUM_SUBGROUP_SIZE subgroups of at least as many invocations, which the C++ side checks), or a Hillis–Steele
// scan and a tree reduction in shared memory (the portable variant). Every function is called by every invocation of the
// workgroup in uniform control flow (they hold barriers); integer sums and floating-point minimums and maximums are exact,
// so the order the variants combine values in changes no bit of the results.
#ifndef GPU_XPBD_WORKGROUP_GLSL
#define GPU_XPBD_WORKGROUP_GLSL

#include "common.glsl"

shared uint workgroup_words[gl_WorkGroupSize.x];
shared vec4 workgroup_minimums[gl_WorkGroupSize.x];
shared vec4 workgroup_maximums[gl_WorkGroupSize.x];
shared uint workgroup_total;

// The sum of value over the workgroup.
uint workgroup_add(uint value)
{
#ifdef GPU_XPBD_SUBGROUPS
    const uint subgroup_total = subgroupAdd(value);
    if (subgroupElect())
    {
        workgroup_words[gl_SubgroupID] = subgroup_total;
    }
    barrier();
    if (gl_SubgroupID == 0U)
    {
        const uint total = subgroupAdd(gl_SubgroupInvocationID < gl_NumSubgroups ? workgroup_words[gl_SubgroupInvocationID] : 0U);
        if (subgroupElect())
        {
            workgroup_total = total;
        }
    }
    barrier();
    const uint total = workgroup_total;
    barrier();
    return total;
#else
    const uint index = gl_LocalInvocationIndex;
    workgroup_words[index] = value;
    barrier();
    for (uint half_size = gl_WorkGroupSize.x / 2U; half_size > 0U; half_size /= 2U)
    {
        if (index < half_size)
        {
            workgroup_words[index] += workgroup_words[index + half_size];
        }
        barrier();
    }
    const uint total = workgroup_words[0];
    barrier();
    return total;
#endif
}

// The sum of value over the invocations before this one (in invocation order), and the workgroup's sum in total.
uint workgroup_exclusive_add(uint value, out uint total)
{
#ifdef GPU_XPBD_SUBGROUPS
    // Within each subgroup, then the subgroups' sums scanned by the first subgroup.
    const uint inclusive = subgroupInclusiveAdd(value);
    const uint subgroup_total = subgroupAdd(value);
    if (subgroupElect())
    {
        workgroup_words[gl_SubgroupID] = subgroup_total;
    }
    barrier();
    if (gl_SubgroupID == 0U)
    {
        const uint lane = gl_SubgroupInvocationID;
        const uint sum = lane < gl_NumSubgroups ? workgroup_words[lane] : 0U;
        const uint before = subgroupExclusiveAdd(sum);
        const uint all = subgroupAdd(sum);
        if (lane < gl_NumSubgroups)
        {
            workgroup_words[lane] = before;
        }
        if (subgroupElect())
        {
            workgroup_total = all;
        }
    }
    barrier();
    total = workgroup_total;
    const uint result = workgroup_words[gl_SubgroupID] + inclusive - value;
    barrier();
    return result;
#else
    // Hillis–Steele: each step adds the value a power of two before (read, barrier, write, barrier).
    const uint index = gl_LocalInvocationIndex;
    workgroup_words[index] = value;
    barrier();
    for (uint offset = 1U; offset < gl_WorkGroupSize.x; offset *= 2U)
    {
        const uint added = index >= offset ? workgroup_words[index - offset] : 0U;
        barrier();
        workgroup_words[index] += added;
        barrier();
    }
    total = workgroup_words[gl_WorkGroupSize.x - 1U];
    const uint result = workgroup_words[index] - value;
    barrier();
    return result;
#endif
}

// The minimum and maximum of a box (minimum, maximum) over the workgroup, component by component.
void workgroup_bounds(inout vec3 minimum, inout vec3 maximum)
{
#ifdef GPU_XPBD_SUBGROUPS
    const vec3 subgroup_minimum = subgroupMin(minimum);
    const vec3 subgroup_maximum = subgroupMax(maximum);
    if (subgroupElect())
    {
        workgroup_minimums[gl_SubgroupID] = vec4(subgroup_minimum, 0.0);
        workgroup_maximums[gl_SubgroupID] = vec4(subgroup_maximum, 0.0);
    }
    barrier();
    if (gl_SubgroupID == 0U)
    {
        const bool present = gl_SubgroupInvocationID < gl_NumSubgroups;
        const vec3 lowest = subgroupMin(present ? workgroup_minimums[gl_SubgroupInvocationID].xyz : vec3(infinity()));
        const vec3 highest = subgroupMax(present ? workgroup_maximums[gl_SubgroupInvocationID].xyz : vec3(-infinity()));
        if (subgroupElect())
        {
            workgroup_minimums[0] = vec4(lowest, 0.0);
            workgroup_maximums[0] = vec4(highest, 0.0);
        }
    }
    barrier();
    minimum = workgroup_minimums[0].xyz;
    maximum = workgroup_maximums[0].xyz;
    barrier();
#else
    const uint index = gl_LocalInvocationIndex;
    workgroup_minimums[index] = vec4(minimum, 0.0);
    workgroup_maximums[index] = vec4(maximum, 0.0);
    barrier();
    for (uint half_size = gl_WorkGroupSize.x / 2U; half_size > 0U; half_size /= 2U)
    {
        if (index < half_size)
        {
            workgroup_minimums[index] = min(workgroup_minimums[index], workgroup_minimums[index + half_size]);
            workgroup_maximums[index] = max(workgroup_maximums[index], workgroup_maximums[index + half_size]);
        }
        barrier();
    }
    minimum = workgroup_minimums[0].xyz;
    maximum = workgroup_maximums[0].xyz;
    barrier();
#endif
}

#endif
