// What the BVH passes share (GpuBvh in gpu_bvh.cppm). A tree of n leaves has 2n − 1 nodes of two word vectors each:
// internal nodes 0 .. n − 2 (node 0 is the root when n ≥ 2), then the leaves n − 1 .. 2n − 2 in the sorted order. A node's
// first vector holds its box's minimum (float bits) and its first child, the second its maximum and its second child; a
// leaf holds its primitive (the box's index) where an internal node holds its first child. A child is an internal node's
// index, or BVH_LEAF | the leaf's position in the sorted order (its node is n − 1 + position).
//
// Beside the nodes: the parent of each node (INVALID_INDEX at the root), an annotation per node (leaves under it, the last
// leaf position under it, its surface-area-heuristic cost), a counter per internal node (the bottom-up passes), the header
// (leaves, root reference, GpuBvhFlag bits), and for the four-wide layout one node per internal node that roots one (eight
// word vectors: minimums x, y, z of four children, maximums x, y, z, the children, their last leaf positions).
//
// The passes climbing from the leaves (bounds, treelets) declare `#pragma use_vulkan_memory_model` before including this:
// they read what other invocations of the same dispatch wrote, ordered by acquire-release counters.
#ifndef GPU_XPBD_BVH_GLSL
#define GPU_XPBD_BVH_GLSL

#include "primitives.glsl"

// The tree's buffers (bindless indices; every array starts at element 0).
struct Tree
{
    uint header;
    uint nodes;
    uint parents;
    uint annotations;
    uint counters;
    uint wide_nodes;
};

// Words of the header.
const uint HEADER_LEAVES = 0U;
const uint HEADER_ROOT = 1U;
const uint HEADER_FLAGS = 2U;

// Coherent views of the bindless buffers, for what the climbing passes exchange within a dispatch.
layout(set = 0, binding = 3, std430) coherent buffer GpuCoherentWordVectorBuffer
{
    uvec4 values[];
} gpu_coherent_word_vectors[];

layout(set = 0, binding = 3, std430) coherent buffer GpuCoherentWordBuffer
{
    uint words[];
} gpu_coherent_words[];

// An axis-aligned box.
struct Box
{
    vec3 minimum;
    vec3 maximum;
};

// The box containing no point (the union's identity).
Box empty_box()
{
    return Box(vec3(infinity()), vec3(-infinity()));
}

Box union_of(Box first, Box second)
{
    return Box(min(first.minimum, second.minimum), max(first.maximum, second.maximum));
}

// Whether two boxes overlap (touching counts, as physics::intersects(Aabb, Aabb)).
bool overlaps(Box first, Box second)
{
    return all(lessThanEqual(first.minimum, second.maximum)) && all(greaterThanEqual(first.maximum, second.minimum));
}

// 2(xy + yz + zx), evaluated exactly in this order (no fused operation), as the CPU's Aabb::surface_area.
float surface_area(Box box)
{
    precise vec3 size = box.maximum - box.minimum;
    precise float area = 2.0 * (size.x * size.y + size.y * size.z + size.z * size.x);
    return area;
}

// The surface area heuristic's cost of an internal node: its own, then its children's (in this association everywhere).
float internal_cost(float area, float first_cost, float second_cost)
{
    precise float cost = BVH_INTERNAL_COST * area + (first_cost + second_cost);
    return cost;
}

float leaf_cost(float area)
{
    precise float cost = BVH_LEAF_COST * area;
    return cost;
}

bool is_leaf(uint child)
{
    return (child & BVH_LEAF) != 0U;
}

// Component index of vector, chosen by selections: a component indexed at run time (vector[index]) of a vector in an array
// was miscompiled by the NVIDIA driver (writes to the fourth component lost), so the four-wide nodes never index one.
uint lane(uvec4 vector, uint index)
{
    return index == 0U ? vector.x : index == 1U ? vector.y : index == 2U ? vector.z : vector.w;
}

// The box of slot index of a four-wide node's rows (minimums x, y, z, maximums x, y, z).
Box wide_child_box(uvec4 rows[8], uint index)
{
    return Box(uintBitsToFloat(uvec3(lane(rows[0], index), lane(rows[1], index), lane(rows[2], index))),
               uintBitsToFloat(uvec3(lane(rows[3], index), lane(rows[4], index), lane(rows[5], index))));
}

// The node of a child of a tree of leaf_count leaves.
uint node_of(uint child, uint leaf_count)
{
    return is_leaf(child) ? leaf_count - 1U + (child & ~BVH_LEAF) : child;
}

// A node's two vectors as a box (and its two words through the out parameters).
Box node_box(uvec4 first, uvec4 second)
{
    return Box(uintBitsToFloat(first.xyz), uintBitsToFloat(second.xyz));
}

Box read_node(Tree tree, uint node, out uint first_child, out uint second_child)
{
    const uvec4 first = gpu_word_vectors[tree.nodes].values[2U * node];
    const uvec4 second = gpu_word_vectors[tree.nodes].values[2U * node + 1U];
    first_child = first.w;
    second_child = second.w;
    return node_box(first, second);
}

Box read_node_coherent(Tree tree, uint node, out uint first_child, out uint second_child)
{
    const uvec4 first = gpu_coherent_word_vectors[tree.nodes].values[2U * node];
    const uvec4 second = gpu_coherent_word_vectors[tree.nodes].values[2U * node + 1U];
    first_child = first.w;
    second_child = second.w;
    return node_box(first, second);
}

void write_node_coherent(Tree tree, uint node, Box box, uint first_child, uint second_child)
{
    gpu_coherent_word_vectors[tree.nodes].values[2U * node] = uvec4(floatBitsToUint(box.minimum), first_child);
    gpu_coherent_word_vectors[tree.nodes].values[2U * node + 1U] = uvec4(floatBitsToUint(box.maximum), second_child);
}

// An annotation: leaves under the node, the last leaf position under it, its cost (float bits), unused.
uvec4 make_annotation(uint leaves, uint last_leaf, float cost)
{
    return uvec4(leaves, last_leaf, floatBitsToUint(cost), 0U);
}

// The box under a ray from origin along direction (with its inverse, ±∞ where a component is 0) up to maximum: the
// distance where it enters (0 when it starts inside), or +∞ when it misses.
float ray_entry(Box box, vec3 origin, vec3 direction, vec3 inverse, float maximum)
{
    float near = 0.0;
    float far = maximum;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (direction[axis] == 0.0)
        {
            if (origin[axis] < box.minimum[axis] || origin[axis] > box.maximum[axis])
            {
                return infinity();
            }
        }
        else
        {
            const float first = (box.minimum[axis] - origin[axis]) * inverse[axis];
            const float second = (box.maximum[axis] - origin[axis]) * inverse[axis];
            near = max(near, min(first, second));
            far = min(far, max(first, second));
        }
    }
    return near <= far ? near : infinity();
}

#endif
