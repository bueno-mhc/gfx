// The climb of the bottom-up BVH passes (Karras 2012, section 5): from its leaf, an invocation goes up through the parents;
// at each it counts itself on the parent's counter, and only the second to arrive — both children done — goes on, after
// processing the parent. The counter's acquire-release semantics order the children's writes (coherent stores) before the
// parent's reads (coherent loads). Unions of boxes are exact, so who arrives second does not change the result.
//
// The including pass declares `#pragma use_vulkan_memory_model` and GL_KHR_memory_scope_semantics, and defines process()
// (declared here): what it does at a node it reaches second.
#ifndef GPU_XPBD_BVH_CLIMB_GLSL
#define GPU_XPBD_BVH_CLIMB_GLSL

#include "bvh.glsl"

void process(Tree tree, uint leaf_count, uint node);

// Climbs from node (after its own processing) to the root, processing each parent this invocation reaches second.
void climb(Tree tree, uint leaf_count, uint node)
{
    uint current = node;
    for (;;)
    {
        const uint parent = gpu_coherent_words[tree.parents].words[current];
        if (parent == INVALID_INDEX)
        {
            return;
        }
        const uint arrived = atomicAdd(gpu_words[tree.counters].words[parent], 1U, gl_ScopeQueueFamily, gl_StorageSemanticsBuffer,
                                       gl_SemanticsAcquireRelease | gl_SemanticsMakeAvailable | gl_SemanticsMakeVisible);
        if (arrived == 0U)
        {
            return;
        }
        process(tree, leaf_count, parent);
        current = parent;
    }
}

// The union and the annotation of an internal node from its children (what the bounds pass and the treelet pass do at a
// node they do not restructure).
void combine_children(Tree tree, uint leaf_count, uint node)
{
    uint first_child;
    uint second_child;
    read_node_coherent(tree, node, first_child, second_child);
    uint unused_first;
    uint unused_second;
    const uint first_node = node_of(first_child, leaf_count);
    const uint second_node = node_of(second_child, leaf_count);
    const Box first_box = read_node_coherent(tree, first_node, unused_first, unused_second);
    const Box second_box = read_node_coherent(tree, second_node, unused_first, unused_second);
    const uvec4 first_annotation = gpu_coherent_word_vectors[tree.annotations].values[first_node];
    const uvec4 second_annotation = gpu_coherent_word_vectors[tree.annotations].values[second_node];

    const Box box = union_of(first_box, second_box);
    write_node_coherent(tree, node, box, first_child, second_child);
    gpu_coherent_word_vectors[tree.annotations].values[node] =
        make_annotation(first_annotation.x + second_annotation.x, max(first_annotation.y, second_annotation.y),
                        internal_cost(surface_area(box), uintBitsToFloat(first_annotation.z), uintBitsToFloat(second_annotation.z)));
}

#endif
