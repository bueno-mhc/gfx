// The GPU backend's BVH (phase G2) against CPU references, on each GPU of the machine and in both variants of the
// primitives, with the Vulkan validation layers: Karras trees (the hierarchy the CPU builds from the same codes, codes
// within a cell of the CPU's, exact unions, annotations and costs), treelet rounds (the topology the CPU's restructuring of
// the same tree gives, bit for bit; costs never higher), four-wide nodes (the CPU's collapse), pairs within a tree and
// between two equal to brute force in every layout, the nearest box along rays, refits, counts written by a pass, the
// same bits twice and in both variants, the nodes as structure_nodes() lists them, misuses; and the layouts measured.
#include "gpu_test.hpp"

namespace
{

using namespace gfx::physics;
namespace gpu = gfx::physics::gpu_xpbd;
using gpu::GpuBvh;

constexpr std::uint32_t LEAF = GPU_XPBD_BVH_LEAF_FLAG;
constexpr std::uint32_t NO_CHILD = 0xFFFFFFFFU;
constexpr float INFINITE_DISTANCE = std::numeric_limits<float>::infinity();

/// A box as the GPU reads it: two vectors (minimum, maximum; w unused).
struct TestBox
{
    std::array<float, 4> minimum{};
    std::array<float, 4> maximum{};
};

/// A box on the CPU.
struct CpuBox
{
    std::array<float, 3> minimum{INFINITE_DISTANCE, INFINITE_DISTANCE, INFINITE_DISTANCE};
    std::array<float, 3> maximum{-INFINITE_DISTANCE, -INFINITE_DISTANCE, -INFINITE_DISTANCE};

    [[nodiscard]] auto operator==(const CpuBox&) const -> bool = default;
};

auto
box_of(const TestBox& box) -> CpuBox
{
    return {.minimum = {box.minimum[0], box.minimum[1], box.minimum[2]}, .maximum = {box.maximum[0], box.maximum[1], box.maximum[2]}};
}

auto
box_of(const gpu::GpuBvhNode& node) -> CpuBox
{
    return {.minimum = node.minimum, .maximum = node.maximum};
}

auto
union_of(const CpuBox& first,
         const CpuBox& second) -> CpuBox
{
    CpuBox box;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        box.minimum[axis] = std::min(first.minimum[axis], second.minimum[axis]);
        box.maximum[axis] = std::max(first.maximum[axis], second.maximum[axis]);
    }
    return box;
}

/// As bvh.glsl's surface_area (and Aabb::surface_area): 2(xy + yz + zx), in this order.
auto
surface_area(const CpuBox& box) -> float
{
    const float x = box.maximum[0] - box.minimum[0];
    const float y = box.maximum[1] - box.minimum[1];
    const float z = box.maximum[2] - box.minimum[2];
    return 2.0F * (x * y + y * z + z * x);
}

auto
internal_cost(float area,
              float first_cost,
              float second_cost) -> float
{
    return GPU_XPBD_BVH_INTERNAL_COST * area + (first_cost + second_cost);
}

auto
overlaps(const CpuBox& first,
         const CpuBox& second) -> bool
{
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        if (first.minimum[axis] > second.maximum[axis] || first.maximum[axis] < second.minimum[axis])
        {
            return false;
        }
    }
    return true;
}

auto
is_leaf(std::uint32_t child) -> bool
{
    return (child & LEAF) != 0;
}

auto
node_of(std::uint32_t child,
        std::uint32_t leaf_count) -> std::uint32_t
{
    return is_leaf(child) ? leaf_count - 1 + (child & ~LEAF) : child;
}

/// Random boxes: centers in a cube of half side @p spread, half extents in [0.05, @p size]; every seventh box centered
/// on the previous one (equal Morton codes).
auto
random_boxes(std::size_t count,
             std::uint32_t seed,
             float spread = 100.0F,
             float size = 2.0F) -> std::vector<TestBox>
{
    std::mt19937 generator{seed};
    std::uniform_real_distribution<float> coordinate{-spread, spread};
    std::uniform_real_distribution<float> extent{0.05F, size};
    std::vector<TestBox> boxes(count);
    std::array<float, 3> center{};
    for (std::size_t index = 0; index < count; ++index)
    {
        if (index % 7 != 6)
        {
            center = {coordinate(generator), coordinate(generator), coordinate(generator)};
        }
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const float half = extent(generator);
            boxes[index].minimum[axis] = center[axis] - half;
            boxes[index].maximum[axis] = center[axis] + half;
        }
    }
    return boxes;
}

/// Boxes as a physics scene has them: most in sixteen clusters, many on a floor, a few large ones.
auto
scene_boxes(std::size_t count,
            std::uint32_t seed) -> std::vector<TestBox>
{
    std::mt19937 generator{seed};
    std::uniform_real_distribution<float> uniform{-1.0F, 1.0F};
    std::normal_distribution<float> spread{0.0F, 4.0F};
    std::array<std::array<float, 3>, 16> clusters{};
    for (std::array<float, 3>& cluster : clusters)
    {
        cluster = {150.0F * uniform(generator), 10.0F + 10.0F * uniform(generator), 150.0F * uniform(generator)};
    }
    std::vector<TestBox> boxes(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        std::array<float, 3> center{};
        float half = 0;
        const std::size_t kind = index % 10;
        if (kind < 6)
        {
            const std::array<float, 3>& cluster = clusters[index % clusters.size()];
            center = {cluster[0] + spread(generator), cluster[1] + spread(generator), cluster[2] + spread(generator)};
            half = 0.65F + 0.35F * uniform(generator);
        }
        else if (kind < 9)
        {
            center = {200.0F * uniform(generator), 0.5F + 0.5F * uniform(generator), 200.0F * uniform(generator)};
            half = 0.4F + 0.2F * uniform(generator);
        }
        else
        {
            center = {200.0F * uniform(generator), 20.0F * uniform(generator), 200.0F * uniform(generator)};
            half = 12.5F + 7.5F * uniform(generator);
        }
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            boxes[index].minimum[axis] = center[axis] - half;
            boxes[index].maximum[axis] = center[axis] + half;
        }
    }
    return boxes;
}

/// A tree read back.
struct ReadTree
{
    gpu::GpuBvhHeader header{};
    std::vector<gpu::GpuBvhNode> nodes{};
    std::vector<gpu::GpuBvhAnnotation> annotations{};
    std::vector<std::uint32_t> codes{};
    std::vector<gpu::GpuWideBvhNode> wide_nodes{};
};

/// The bytes of @p values.
template<class Value>
auto
bytes_of(const std::vector<Value>& values) -> std::vector<std::byte>
{
    const std::span<const std::byte> bytes = std::as_bytes(std::span{values});
    return {bytes.begin(), bytes.end()};
}

// ===================================================================================================== CPU references

/// The children of each internal node of a Karras tree over @p codes (Karras 2012, figure 4; as bvh_hierarchy.comp).
auto
karras_children(std::span<const std::uint32_t> codes) -> std::vector<std::array<std::uint32_t, 2>>
{
    const int leaf_count = static_cast<int>(codes.size());
    const auto delta = [&](int first, int second) -> int {
        if (second < 0 || second >= leaf_count)
        {
            return -1;
        }
        const auto first_index = static_cast<std::size_t>(first);
        const auto second_index = static_cast<std::size_t>(second);
        if (codes[first_index] != codes[second_index])
        {
            return std::countl_zero(codes[first_index] ^ codes[second_index]);
        }
        return 32 + std::countl_zero(static_cast<std::uint32_t>(first ^ second));
    };
    std::vector<std::array<std::uint32_t, 2>> children;
    for (int node = 0; node + 1 < leaf_count; ++node)
    {
        const int direction = delta(node, node + 1) - delta(node, node - 1) >= 0 ? 1 : -1;
        const int minimum_prefix = delta(node, node - direction);
        int bound = 2;
        while (delta(node, node + bound * direction) > minimum_prefix)
        {
            bound *= 2;
        }
        int length = 0;
        for (int step = bound / 2; step >= 1; step /= 2)
        {
            if (delta(node, node + (length + step) * direction) > minimum_prefix)
            {
                length += step;
            }
        }
        const int other = node + length * direction;
        const int node_prefix = delta(node, other);
        int split = 0;
        int step = length;
        do
        {
            step = (step + 1) / 2;
            if (delta(node, node + (split + step) * direction) > node_prefix)
            {
                split += step;
            }
        } while (step > 1);
        const int gamma = node + split * direction + std::min(direction, 0);
        const auto leaf_or_node = [](bool leaf, int index) { return leaf ? LEAF | static_cast<std::uint32_t>(index) : static_cast<std::uint32_t>(index); };
        children.push_back({leaf_or_node(std::min(node, other) == gamma, gamma), leaf_or_node(std::max(node, other) == gamma + 1, gamma + 1)});
    }
    return children;
}

/// The cells of a 30-bit Morton code (x, y, z).
auto
morton_cells(std::uint32_t code) -> std::array<std::uint32_t, 3>
{
    const auto compact = [](std::uint32_t value) {
        value &= 0x09249249U;
        value = (value | (value >> 2U)) & 0x030C30C3U;
        value = (value | (value >> 4U)) & 0x0300F00FU;
        value = (value | (value >> 8U)) & 0x030000FFU;
        value = (value | (value >> 16U)) & 0x000003FFU;
        return value;
    };
    return {compact(code >> 2U), compact(code >> 1U), compact(code)};
}

/// The cells of the center of @p box in @p bounds (as bvh_morton.comp, up to the GPU's division).
auto
expected_cells(const TestBox& box,
               const CpuBox& bounds) -> std::array<std::uint32_t, 3>
{
    std::array<std::uint32_t, 3> cells{};
    constexpr float CELLS = 1 << GPU_XPBD_MORTON_BITS;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        const float center = (box.minimum[axis] + box.maximum[axis]) * 0.5F;
        const float extent = bounds.maximum[axis] - bounds.minimum[axis];
        const float position = extent > 0 ? (center - bounds.minimum[axis]) / extent : 0.0F;
        cells[axis] = static_cast<std::uint32_t>(std::clamp(position * CELLS, 0.0F, CELLS - 1.0F));
    }
    return cells;
}

/// What a subtree holds.
struct Subtree
{
    CpuBox box{};
    std::uint32_t leaves = 0;
    std::uint32_t first_leaf = 0;
    std::uint32_t last_leaf = 0;
    float cost = 0;
};

/// The problems of a tree read back (none: empty), against its boxes: each leaf and each primitive reached once from the
/// root, leaf boxes the boxes, internal boxes the exact unions of their children's, the annotations (counts, last leaves,
/// costs bit for bit), a depth within the stack, and (@p contiguous: no treelet rounds) contiguous leaf ranges.
auto
tree_problems(const ReadTree& tree,
              std::span<const TestBox> boxes,
              bool contiguous) -> std::vector<std::string>
{
    std::vector<std::string> problems;
    const auto problem = [&](std::string text) {
        if (problems.size() < 8)
        {
            problems.push_back(std::move(text));
        }
    };
    const auto leaf_count = static_cast<std::uint32_t>(boxes.size());
    if (tree.header.leaf_count != leaf_count)
    {
        problem(std::format("the header counts {} leaves, not {}", tree.header.leaf_count, leaf_count));
        return problems;
    }
    if (tree.header.flags != GpuBvhFlag::NONE)
    {
        problem(std::format("the header's flags are {}", tree.header.flags));
    }
    if (leaf_count == 0)
    {
        return problems;
    }
    if (tree.header.root != (leaf_count >= 2 ? 0 : LEAF))
    {
        problem(std::format("the root is {:#x}", tree.header.root));
    }

    std::vector<bool> reached(2 * std::size_t{leaf_count} - 1);
    std::vector<bool> primitives(leaf_count);
    const auto visit = [&](this const auto& self, std::uint32_t child, std::uint32_t depth) -> Subtree {
        const std::uint32_t node = node_of(child, leaf_count);
        if (node >= reached.size() || reached[node])
        {
            problem(std::format("node {} is out of range or reached twice", node));
            return {};
        }
        reached[node] = true;
        if (depth > GPU_XPBD_BVH_STACK_SIZE)
        {
            problem(std::format("node {} is {} deep", node, depth));
        }
        const gpu::GpuBvhNode& stored = tree.nodes[node];
        const gpu::GpuBvhAnnotation& annotation = tree.annotations[node];
        Subtree subtree;
        if (is_leaf(child))
        {
            // A leaf: its primitive's box, unchanged.
            const std::uint32_t position = child & ~LEAF;
            if (stored.first >= leaf_count || primitives[stored.first])
            {
                problem(std::format("leaf {} has primitive {}, out of range or seen before", position, stored.first));
                return {};
            }
            primitives[stored.first] = true;
            subtree = {.box = box_of(stored), .leaves = 1, .first_leaf = position, .last_leaf = position, .cost = GPU_XPBD_BVH_LEAF_COST * surface_area(box_of(stored))};
            if (subtree.box != box_of(boxes[stored.first]))
            {
                problem(std::format("leaf {} does not hold the box of primitive {}", position, stored.first));
            }
        }
        else
        {
            // An internal node: the union of its children's.
            const Subtree first = self(stored.first, depth + 1);
            const Subtree second = self(stored.second, depth + 1);
            subtree.box = union_of(first.box, second.box);
            subtree.leaves = first.leaves + second.leaves;
            subtree.first_leaf = std::min(first.first_leaf, second.first_leaf);
            subtree.last_leaf = std::max(first.last_leaf, second.last_leaf);
            subtree.cost = internal_cost(surface_area(subtree.box), first.cost, second.cost);
            if (box_of(stored) != subtree.box)
            {
                problem(std::format("node {}'s box is not the union of its children's", node));
            }
            if (contiguous && subtree.last_leaf - subtree.first_leaf + 1 != subtree.leaves)
            {
                problem(std::format("node {}'s leaves are not contiguous", node));
            }
        }
        if (annotation.leaf_count != subtree.leaves || annotation.last_leaf != subtree.last_leaf ||
            std::bit_cast<std::uint32_t>(annotation.cost) != std::bit_cast<std::uint32_t>(subtree.cost))
        {
            problem(std::format("node {}'s annotation is ({}, {}, {}), expected ({}, {}, {})", node, annotation.leaf_count, annotation.last_leaf,
                                annotation.cost, subtree.leaves, subtree.last_leaf, subtree.cost));
        }
        return subtree;
    };
    visit(tree.header.root, 0);
    if (std::ranges::count(reached, false) != 0)
    {
        problem(std::format("{} nodes are not reached from the root", std::ranges::count(reached, false)));
    }
    return problems;
}

/// A treelet round of @p tree on the CPU, as bvh_treelet.comp: bottom up (children first), the nodes over at least
/// @p minimum_leaves leaves restructured, the others' annotations computed again.
void
treelet_round(ReadTree& tree,
              std::uint32_t minimum_leaves)
{
    const std::uint32_t leaf_count = tree.header.leaf_count;
    const auto box = [&](std::uint32_t child) { return box_of(tree.nodes[node_of(child, leaf_count)]); };
    const auto cost = [&](std::uint32_t child) { return tree.annotations[node_of(child, leaf_count)].cost; };
    const auto combine = [&](std::uint32_t node) {
        const gpu::GpuBvhNode stored = tree.nodes[node];
        const CpuBox union_box = union_of(box(stored.first), box(stored.second));
        const gpu::GpuBvhAnnotation& first = tree.annotations[node_of(stored.first, leaf_count)];
        const gpu::GpuBvhAnnotation& second = tree.annotations[node_of(stored.second, leaf_count)];
        tree.nodes[node].minimum = union_box.minimum;
        tree.nodes[node].maximum = union_box.maximum;
        tree.annotations[node] = {.leaf_count = first.leaf_count + second.leaf_count,
                                  .last_leaf = std::max(first.last_leaf, second.last_leaf),
                                  .cost = internal_cost(surface_area(union_box), first.cost, second.cost)};
    };
    const auto restructure = [&](std::uint32_t root) {
        // The treelet.
        const gpu::GpuBvhNode root_node = tree.nodes[root];
        const float current_cost = internal_cost(surface_area(box_of(root_node)), cost(root_node.first), cost(root_node.second));
        std::vector<std::uint32_t> subtrees{root_node.first, root_node.second};
        std::vector<std::uint32_t> internal_nodes{root};
        while (subtrees.size() < GPU_XPBD_TREELET_LEAVES)
        {
            int largest = -1;
            float largest_area = -1;
            for (std::size_t subtree = 0; subtree < subtrees.size(); ++subtree)
            {
                if (!is_leaf(subtrees[subtree]) && surface_area(box(subtrees[subtree])) > largest_area)
                {
                    largest = static_cast<int>(subtree);
                    largest_area = surface_area(box(subtrees[subtree]));
                }
            }
            if (largest < 0)
            {
                break;
            }
            const auto opened_index = static_cast<std::size_t>(largest);
            const std::uint32_t opened = subtrees[opened_index];
            internal_nodes.push_back(opened);
            subtrees[opened_index] = tree.nodes[opened].first;
            subtrees.push_back(tree.nodes[opened].second);
        }

        // The least costs over the subsets.
        const auto count = static_cast<std::uint32_t>(subtrees.size());
        const std::uint32_t full = (1U << count) - 1U;
        std::vector<float> costs(full + 1);
        std::vector<CpuBox> boxes(full + 1);
        std::vector<std::uint32_t> splits(full + 1);
        for (std::uint32_t subset = 1; subset <= full; ++subset)
        {
            CpuBox subset_box;
            for (std::uint32_t subtree = 0; subtree < count; ++subtree)
            {
                if ((subset & (1U << subtree)) != 0)
                {
                    subset_box = union_of(subset_box, box(subtrees[subtree]));
                }
            }
            boxes[subset] = subset_box;
            if (std::popcount(subset) == 1)
            {
                costs[subset] = cost(subtrees[static_cast<std::size_t>(std::countr_zero(subset))]);
                continue;
            }
            const std::uint32_t lowest = subset & (~subset + 1U);
            const std::uint32_t rest = subset ^ lowest;
            float best = INFINITE_DISTANCE;
            std::uint32_t best_part = 0;
            for (std::uint32_t others = rest;; others = (others - 1U) & rest)
            {
                const std::uint32_t part = lowest | others;
                if (part != subset)
                {
                    const float split_cost = costs[part] + costs[subset ^ part];
                    if (split_cost < best)
                    {
                        best = split_cost;
                        best_part = part;
                    }
                }
                if (others == 0)
                {
                    break;
                }
            }
            costs[subset] = GPU_XPBD_BVH_INTERNAL_COST * surface_area(subset_box) + best;
            splits[subset] = best_part;
        }
        if (!(costs[full] < current_cost))
        {
            combine(root);
            return;
        }

        // The rebuild.
        std::vector<gpu::GpuBvhAnnotation> subtree_annotations;
        for (const std::uint32_t subtree : subtrees)
        {
            subtree_annotations.push_back(tree.annotations[node_of(subtree, leaf_count)]);
        }
        std::vector<std::pair<std::uint32_t, std::uint32_t>> pending{{full, root}};
        std::size_t next_internal = 1;
        while (!pending.empty())
        {
            const auto [subset, node] = pending.back();
            pending.pop_back();
            std::array<std::uint32_t, 2> children{};
            for (std::size_t side = 0; side < 2; ++side)
            {
                const std::uint32_t part = side == 0 ? splits[subset] : subset ^ splits[subset];
                if (std::popcount(part) == 1)
                {
                    children[side] = subtrees[static_cast<std::size_t>(std::countr_zero(part))];
                }
                else
                {
                    children[side] = internal_nodes[next_internal++];
                    pending.emplace_back(part, children[side]);
                }
            }
            std::uint32_t leaves = 0;
            std::uint32_t last_leaf = 0;
            for (std::uint32_t subtree = 0; subtree < count; ++subtree)
            {
                if ((subset & (1U << subtree)) != 0)
                {
                    leaves += subtree_annotations[subtree].leaf_count;
                    last_leaf = std::max(last_leaf, subtree_annotations[subtree].last_leaf);
                }
            }
            tree.nodes[node] = {.minimum = boxes[subset].minimum, .first = children[0], .maximum = boxes[subset].maximum, .second = children[1]};
            tree.annotations[node] = {.leaf_count = leaves, .last_leaf = last_leaf, .cost = costs[subset]};
        }
    };

    // Post order: a node after both its subtrees.
    const auto visit = [&](this const auto& self, std::uint32_t child) -> void {
        if (is_leaf(child))
        {
            return;
        }
        self(tree.nodes[child].first);
        self(tree.nodes[child].second);
        if (tree.annotations[child].leaf_count >= minimum_leaves)
        {
            restructure(child);
        }
        else
        {
            combine(child);
        }
    };
    if (leaf_count >= 2)
    {
        visit(0);
    }
}

/// The four-wide nodes of @p tree on the CPU, as bvh_wide.comp: (binary index, node) of each internal node rooting one.
auto
wide_nodes(const ReadTree& tree) -> std::vector<std::pair<std::uint32_t, gpu::GpuWideBvhNode>>
{
    const std::uint32_t leaf_count = tree.header.leaf_count;
    std::vector<std::pair<std::uint32_t, gpu::GpuWideBvhNode>> result;
    if (leaf_count < 2)
    {
        return result;
    }
    std::vector<std::uint32_t> pending{0};
    while (!pending.empty())
    {
        const std::uint32_t node = pending.back();
        pending.pop_back();
        std::vector<std::uint32_t> members{tree.nodes[node].first, tree.nodes[node].second};
        while (members.size() < 4)
        {
            int largest = -1;
            float largest_area = -1;
            for (std::size_t member = 0; member < members.size(); ++member)
            {
                if (!is_leaf(members[member]) && surface_area(box_of(tree.nodes[members[member]])) > largest_area)
                {
                    largest = static_cast<int>(member);
                    largest_area = surface_area(box_of(tree.nodes[members[member]]));
                }
            }
            if (largest < 0)
            {
                break;
            }
            const auto opened = static_cast<std::size_t>(largest);
            const std::uint32_t opened_node = members[opened];
            members[opened] = tree.nodes[opened_node].first;
            members.push_back(tree.nodes[opened_node].second);
        }
        gpu::GpuWideBvhNode wide;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            wide.minimums[axis].fill(INFINITE_DISTANCE);
            wide.maximums[axis].fill(-INFINITE_DISTANCE);
        }
        wide.children.fill(NO_CHILD);
        for (std::size_t slot = 0; slot < members.size(); ++slot)
        {
            const std::uint32_t child_node = node_of(members[slot], leaf_count);
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                wide.minimums[axis][slot] = tree.nodes[child_node].minimum[axis];
                wide.maximums[axis][slot] = tree.nodes[child_node].maximum[axis];
            }
            wide.children[slot] = members[slot];
            wide.last_leaves[slot] = tree.annotations[child_node].last_leaf;
            if (!is_leaf(members[slot]))
            {
                pending.push_back(members[slot]);
            }
        }
        result.emplace_back(node, wide);
    }
    return result;
}

/// The overlapping pairs of @p boxes (the smaller index first), sorted.
auto
brute_force_pairs(std::span<const TestBox> boxes) -> std::vector<std::pair<std::uint32_t, std::uint32_t>>
{
    // A sweep along x.
    std::vector<std::uint32_t> order(boxes.size());
    std::ranges::iota(order, 0U);
    std::ranges::sort(order, {}, [&](std::uint32_t index) { return boxes[index].minimum[0]; });
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;
    for (std::size_t first = 0; first < order.size(); ++first)
    {
        const CpuBox first_box = box_of(boxes[order[first]]);
        for (std::size_t second = first + 1; second < order.size() && boxes[order[second]].minimum[0] <= first_box.maximum[0]; ++second)
        {
            if (overlaps(first_box, box_of(boxes[order[second]])))
            {
                pairs.emplace_back(std::min(order[first], order[second]), std::max(order[first], order[second]));
            }
        }
    }
    std::ranges::sort(pairs);
    return pairs;
}

/// The overlapping pairs of a box of @p first and a box of @p second, sorted.
auto
brute_force_pairs(std::span<const TestBox> first,
                  std::span<const TestBox> second) -> std::vector<std::pair<std::uint32_t, std::uint32_t>>
{
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;
    for (std::uint32_t first_index = 0; first_index < first.size(); ++first_index)
    {
        for (std::uint32_t second_index = 0; second_index < second.size(); ++second_index)
        {
            if (overlaps(box_of(first[first_index]), box_of(second[second_index])))
            {
                pairs.emplace_back(first_index, second_index);
            }
        }
    }
    return pairs;
}

/// Where a ray enters @p box grown by @p margin on every side (+∞: it misses), as bvh.glsl's ray_entry with divisions.
auto
ray_entry(const CpuBox& box,
          const gpu::GpuRay& ray,
          float maximum,
          float margin) -> float
{
    float near = 0;
    float far = maximum;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        const float low = box.minimum[axis] - margin;
        const float high = box.maximum[axis] + margin;
        if (ray.direction[axis] == 0)
        {
            if (ray.origin[axis] < low || ray.origin[axis] > high)
            {
                return INFINITE_DISTANCE;
            }
            continue;
        }
        const float first = (low - ray.origin[axis]) / ray.direction[axis];
        const float second = (high - ray.origin[axis]) / ray.direction[axis];
        near = std::max(near, std::min(first, second));
        far = std::min(far, std::max(first, second));
    }
    return near <= far ? near : INFINITE_DISTANCE;
}

/// The nearest entry of @p ray into any of @p boxes grown by @p margin.
auto
nearest_entry(std::span<const TestBox> boxes,
              const gpu::GpuRay& ray,
              float margin) -> float
{
    float nearest = INFINITE_DISTANCE;
    for (const TestBox& box : boxes)
    {
        nearest = std::min(nearest, ray_entry(box_of(box), ray, ray.maximum_distance, margin));
    }
    return nearest;
}

// ===================================================================================================== fixture

/// Trees on one GPU, with the primitives in one variant.
class GpuBvhTest : public gfx::tests::GpuTest
{
protected:
    void release_gpu_objects() override
    {
        m_trees.clear();
    }

    /// A tree of @p description reserved for @p maximum leaves, kept until the test ends.
    auto make_tree(const gpu::GpuBvhDescription& description,
                   std::uint32_t maximum) -> GpuBvh&
    {
        m_trees.push_back(std::make_unique<GpuBvh>(*m_device, *m_primitives, description));
        m_trees.back()->reserve(maximum);
        return *m_trees.back();
    }

    /// A buffer holding @p boxes.
    auto boxes_buffer(std::span<const TestBox> boxes) -> gfx::Buffer&
    {
        return elements_buffer(boxes);
    }

    /// The tree of @p count leaves of @p tree, read back.
    auto read_tree(const GpuBvh& tree,
                   std::uint32_t count) -> ReadTree
    {
        ReadTree result;
        result.header = read<gpu::GpuBvhHeader>(tree.header_buffer(), 1)[0];
        const std::size_t node_count = count == 0 ? 0 : 2 * std::size_t{count} - 1;
        result.nodes = read<gpu::GpuBvhNode>(tree.node_buffer(), node_count);
        result.annotations = read<gpu::GpuBvhAnnotation>(tree.annotation_buffer(), node_count);
        result.codes = words(tree.code_buffer(), count);
        if (tree.wide_node_buffer() != nullptr && count >= 2)
        {
            result.wide_nodes = read<gpu::GpuWideBvhNode>(*tree.wide_node_buffer(), count - 1);
        }
        return result;
    }

    /// Builds @p tree over the first @p count boxes of @p boxes, reads it back.
    auto build(GpuBvh& tree,
               gfx::Buffer& boxes,
               std::uint32_t count) -> ReadTree
    {
        record([&](gfx::ComputeRecorder& recorder) { tree.build(recorder, gpu::span_of(boxes), {.maximum = count}); });
        return read_tree(tree, count);
    }

    /// The pairs @p tree finds (with @p other when given: this tree's boxes first), at most @p capacity of them, and how
    /// many it counted.
    auto find_pairs(GpuBvh& tree,
                    const GpuBvh* other,
                    std::uint32_t capacity) -> std::pair<std::vector<std::pair<std::uint32_t, std::uint32_t>>, std::uint32_t>
    {
        gfx::Buffer& output = words_buffer(2 * std::size_t{capacity} + 2);
        gfx::Buffer& count = words_buffer(1);
        record([&](gfx::ComputeRecorder& recorder) {
            if (other != nullptr)
            {
                tree.find_pairs(recorder, *other, gpu::span_of(output), gpu::span_of(count), capacity);
            }
            else
            {
                tree.find_pairs(recorder, gpu::span_of(output), gpu::span_of(count), capacity);
            }
        });
        const std::uint32_t total = words(count, 1)[0];
        const std::uint32_t written = std::min(total, capacity);
        const std::vector<std::uint32_t> found = words(output, 2 * std::size_t{written} + 2);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;
        for (std::size_t pair = 0; pair < written; ++pair)
        {
            pairs.emplace_back(found[2 * pair], found[2 * pair + 1]);
        }
        EXPECT_EQ(found[2 * written], gfx::tests::UNTOUCHED) << "nothing written past the capacity";
        return {pairs, total};
    }

    /// The hits of @p rays in @p tree.
    auto cast_rays(GpuBvh& tree,
                   std::span<const gpu::GpuRay> rays) -> std::vector<gpu::GpuRayHit>
    {
        gfx::Buffer& input = elements_buffer(rays);
        gfx::Buffer& output = words_buffer(2 * rays.size());
        record([&](gfx::ComputeRecorder& recorder) {
            tree.cast_rays(recorder, gpu::span_of(input), gpu::span_of(output), {.maximum = static_cast<std::uint32_t>(rays.size())});
        });
        return read<gpu::GpuRayHit>(output, rays.size());
    }

    /// The fastest of five runs of @p recording, in milliseconds (recording, submission and waiting included).
    template<class Recording>
    auto milliseconds(Recording&& recording) -> double
    {
        double fastest = std::numeric_limits<double>::infinity();
        for (int run_index = 0; run_index < 5; ++run_index)
        {
            const auto start = std::chrono::steady_clock::now();
            record(recording);
            fastest = std::min(fastest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        return fastest;
    }

    std::vector<std::unique_ptr<GpuBvh>> m_trees;
};

/// Random rays among boxes spread over a cube of half side @p spread: random directions, axis-aligned ones, short ones,
/// rays starting at a box's center.
auto
random_rays(std::size_t count,
            std::uint32_t seed,
            std::span<const TestBox> boxes,
            float spread) -> std::vector<gpu::GpuRay>
{
    std::mt19937 generator{seed};
    std::uniform_real_distribution<float> coordinate{-1.2F * spread, 1.2F * spread};
    std::normal_distribution<float> normal{0.0F, 1.0F};
    std::vector<gpu::GpuRay> rays(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        gpu::GpuRay& ray = rays[index];
        ray.origin = {coordinate(generator), coordinate(generator), coordinate(generator)};
        std::array<float, 3> direction{normal(generator), normal(generator), normal(generator)};
        if (index % 5 == 1)
        {
            // Along an axis.
            const std::size_t axis = index % 3;
            direction = {0, 0, 0};
            direction[axis] = index % 2 == 0 ? 1.0F : -1.0F;
        }
        if (index % 5 == 2 && !boxes.empty())
        {
            // From inside a box.
            const TestBox& box = boxes[index % boxes.size()];
            ray.origin = {(box.minimum[0] + box.maximum[0]) * 0.5F, (box.minimum[1] + box.maximum[1]) * 0.5F, (box.minimum[2] + box.maximum[2]) * 0.5F};
        }
        const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
        ray.direction = {direction[0] / length, direction[1] / length, direction[2] / length};
        ray.maximum_distance = index % 5 == 3 ? 10.0F : 4.0F * spread;
    }
    return rays;
}

// ===================================================================================================== tests

TEST_P(GpuBvhTest, BuildsAreKarrasTreesOfTheirBoxes)
{
    for (const std::uint32_t count : {0U, 1U, 2U, 3U, 64U, 1000U, 4097U, 100'000U})
    {
        const std::vector<TestBox> boxes = random_boxes(count, count + 11);
        GpuBvh& tree = make_tree({}, count);
        const ReadTree built = build(tree, boxes_buffer(boxes), count);
        const std::vector<std::string> problems = tree_problems(built, boxes, true);
        EXPECT_TRUE(problems.empty()) << count << " boxes: " << (problems.empty() ? "" : problems.front()) << " (" << problems.size() << " problems)";
        if (count == 0)
        {
            continue;
        }

        // The codes: sorted, within a cell of the CPU's per axis (the GPU's division is not exact), equal codes in the
        // boxes' order.
        CpuBox bounds;
        for (const TestBox& box : boxes)
        {
            bounds = union_of(bounds, box_of(box));
        }
        EXPECT_TRUE(std::ranges::is_sorted(built.codes)) << count;
        std::size_t far_codes = 0;
        std::size_t unstable = 0;
        for (std::uint32_t position = 0; position < count; ++position)
        {
            const std::uint32_t primitive = built.nodes[count - 1 + position].first;
            const std::array<std::uint32_t, 3> cells = morton_cells(built.codes[position]);
            const std::array<std::uint32_t, 3> expected = expected_cells(boxes[primitive], bounds);
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                far_codes += cells[axis] + 1 < expected[axis] || expected[axis] + 1 < cells[axis] ? 1U : 0U;
            }
            unstable += position > 0 && built.codes[position - 1] == built.codes[position] && built.nodes[count - 2 + position].first > primitive ? 1U : 0U;
        }
        EXPECT_EQ(far_codes, 0U) << count << " boxes";
        EXPECT_EQ(unstable, 0U) << count << " boxes: equal codes keep the boxes' order";

        // The hierarchy the CPU builds from the same codes.
        const std::vector<std::array<std::uint32_t, 2>> children = karras_children(built.codes);
        std::size_t different = 0;
        for (std::uint32_t node = 0; node + 1 < count; ++node)
        {
            different += built.nodes[node].first != children[node][0] || built.nodes[node].second != children[node][1] ? 1U : 0U;
        }
        EXPECT_EQ(different, 0U) << count << " boxes: internal nodes unlike the CPU's";
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuBvhTest, TreeletRoundsGiveTheCpuRestructuring)
{
    // Uniform and clustered boxes; one to three rounds, each against the CPU's rounds over the GPU's plain tree.
    for (const auto& [name, boxes] : {std::pair{"uniform", random_boxes(20'000, 5)}, std::pair{"scene", scene_boxes(20'000, 6)}})
    {
        const auto count = static_cast<std::uint32_t>(boxes.size());
        gfx::Buffer& input = boxes_buffer(boxes);
        ReadTree expected = build(make_tree({}, count), input, count);
        float previous_cost = expected.annotations[0].cost;
        for (std::uint32_t rounds = 1; rounds <= 3; ++rounds)
        {
            treelet_round(expected, GPU_XPBD_TREELET_LEAVES << (rounds - 1));
            const ReadTree restructured = build(make_tree({.treelet_rounds = rounds}, count), input, count);
            const std::vector<std::string> problems = tree_problems(restructured, boxes, false);
            EXPECT_TRUE(problems.empty()) << name << ", " << rounds << " rounds: " << (problems.empty() ? "" : problems.front());
            EXPECT_EQ(bytes_of(restructured.nodes), bytes_of(expected.nodes)) << name << ", " << rounds << " rounds: nodes unlike the CPU's";
            EXPECT_EQ(bytes_of(restructured.annotations), bytes_of(expected.annotations)) << name << ", " << rounds << " rounds";
            EXPECT_LE(restructured.annotations[0].cost, previous_cost) << name << ", " << rounds << " rounds";
            previous_cost = restructured.annotations[0].cost;
        }
        EXPECT_LT(previous_cost, build(make_tree({}, count), input, count).annotations[0].cost) << name << ": the rounds lowered the cost";
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuBvhTest, FourWideNodesCollapseTheBinaryTree)
{
    for (const std::uint32_t rounds : {0U, 2U})
    {
        for (const std::uint32_t count : {2U, 3U, 5U, 1000U, 30'000U})
        {
            const std::vector<TestBox> boxes = scene_boxes(count, count + rounds);
            const ReadTree built = build(make_tree({.layout = GpuBvhLayout::FOUR_WIDE, .treelet_rounds = rounds}, count), boxes_buffer(boxes), count);
            EXPECT_TRUE(tree_problems(built, boxes, rounds == 0).empty()) << count;

            // Every node the CPU's collapse makes, bit for bit; every leaf under one of them once.
            std::size_t different = 0;
            std::size_t leaves = 0;
            std::string first_difference;
            for (const auto& [node, wide] : wide_nodes(built))
            {
                const auto gpu_words = std::bit_cast<std::array<std::uint32_t, 32>>(built.wide_nodes[node]);
                const auto cpu_words = std::bit_cast<std::array<std::uint32_t, 32>>(wide);
                if (gpu_words != cpu_words)
                {
                    ++different;
                    if (first_difference.empty())
                    {
                        first_difference = std::format("node {}: GPU {::08x}, CPU {::08x}", node, gpu_words, cpu_words);
                    }
                }
                leaves += static_cast<std::size_t>(std::ranges::count_if(wide.children, [](std::uint32_t child) { return child != NO_CHILD && is_leaf(child); }));
            }
            EXPECT_EQ(different, 0U) << count << " boxes, " << rounds << " rounds; " << first_difference;
            EXPECT_EQ(leaves, count) << count << " boxes, " << rounds << " rounds";
        }
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuBvhTest, PairsAreTheBruteForcePairsInEveryLayout)
{
    for (const std::uint32_t count : {0U, 1U, 2U, 500U, 20'000U})
    {
        const std::vector<TestBox> boxes = count < 1000 ? random_boxes(count, count, 5.0F) : scene_boxes(count, count);
        const std::vector<std::pair<std::uint32_t, std::uint32_t>> expected = brute_force_pairs(boxes);
        gfx::Buffer& input = boxes_buffer(boxes);
        const auto capacity = static_cast<std::uint32_t>(expected.size() + 10);
        std::optional<std::vector<std::pair<std::uint32_t, std::uint32_t>>> first_order;
        for (const GpuBvhLayout layout : {GpuBvhLayout::BINARY, GpuBvhLayout::FOUR_WIDE})
        {
            for (const std::uint32_t rounds : {0U, 2U})
            {
                GpuBvh& tree = make_tree({.layout = layout, .treelet_rounds = rounds}, count);
                build(tree, input, count);
                auto [pairs, total] = find_pairs(tree, nullptr, capacity);
                EXPECT_EQ(total, expected.size()) << count << " boxes, " << gfx::to_string(layout) << ", " << rounds << " rounds";

                // The same pairs found twice, in the same order; and through the other overload.
                const auto [again, again_total] = find_pairs(tree, &tree, capacity);
                EXPECT_EQ(again, pairs) << count << " boxes, " << gfx::to_string(layout) << ", " << rounds << " rounds";
                std::ranges::sort(pairs);
                EXPECT_EQ(pairs, expected) << count << " boxes, " << gfx::to_string(layout) << ", " << rounds << " rounds";

                // Fewer places than pairs: the count is still the whole, the places hold the first pairs.
                if (expected.size() > 4)
                {
                    const auto [some, some_total] = find_pairs(tree, nullptr, 4);
                    EXPECT_EQ(some_total, expected.size());
                    EXPECT_EQ(some, std::vector(again.begin(), again.begin() + 4));
                }
                EXPECT_EQ(read<gpu::GpuBvhHeader>(tree.header_buffer(), 1)[0].flags, GpuBvhFlag::NONE);
            }
        }
    }

    // Between two trees (the first's boxes first), whatever the layouts.
    const std::vector<TestBox> moving = scene_boxes(3'000, 31);
    const std::vector<TestBox> resting = scene_boxes(1'000, 32);
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> expected = brute_force_pairs(moving, resting);
    gfx::Buffer& moving_boxes = boxes_buffer(moving);
    gfx::Buffer& resting_boxes = boxes_buffer(resting);
    for (const GpuBvhLayout layout : {GpuBvhLayout::BINARY, GpuBvhLayout::FOUR_WIDE})
    {
        GpuBvh& first = make_tree({.layout = layout}, 3'000);
        GpuBvh& second = make_tree({.layout = layout, .treelet_rounds = 1}, 1'000);
        build(first, moving_boxes, 3'000);
        build(second, resting_boxes, 1'000);
        auto [pairs, total] = find_pairs(first, &second, static_cast<std::uint32_t>(expected.size() + 10));
        EXPECT_EQ(total, expected.size()) << gfx::to_string(layout);
        std::ranges::sort(pairs);
        EXPECT_EQ(pairs, expected) << gfx::to_string(layout);
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuBvhTest, RaysEnterTheNearestBox)
{
    // A hit is the nearest box within a margin (the GPU multiplies by the direction's inverse, not exact): its box grown
    // by the margin contains the entry, and no box shrunk by it is entered earlier; a miss enters no box shrunk by it.
    constexpr float MARGIN = 1.0e-3F;
    const std::vector<TestBox> boxes = scene_boxes(20'000, 41);
    const std::vector<gpu::GpuRay> rays = random_rays(2'000, 42, boxes, 200.0F);
    std::vector<float> earliest_entries;
    for (const gpu::GpuRay& ray : rays)
    {
        earliest_entries.push_back(nearest_entry(boxes, ray, -MARGIN));
    }
    gfx::Buffer& input = boxes_buffer(boxes);
    for (const GpuBvhLayout layout : {GpuBvhLayout::BINARY, GpuBvhLayout::FOUR_WIDE})
    {
        for (const std::uint32_t rounds : {0U, 2U})
        {
            GpuBvh& tree = make_tree({.layout = layout, .treelet_rounds = rounds}, 20'000);
            build(tree, input, 20'000);
            const std::vector<gpu::GpuRayHit> hits = cast_rays(tree, rays);
            std::size_t wrong = 0;
            std::size_t hit_count = 0;
            for (std::size_t index = 0; index < rays.size(); ++index)
            {
                const gpu::GpuRay& ray = rays[index];
                const gpu::GpuRayHit& hit = hits[index];
                const float earliest = earliest_entries[index];
                if (hit.primitive == NO_CHILD)
                {
                    wrong += earliest != INFINITE_DISTANCE || hit.distance != ray.maximum_distance ? 1U : 0U;
                    continue;
                }
                ++hit_count;
                const float entry = ray_entry(box_of(boxes[hit.primitive]), ray, ray.maximum_distance, MARGIN);
                wrong += entry > hit.distance + MARGIN || earliest + MARGIN < hit.distance ? 1U : 0U;
            }
            EXPECT_EQ(wrong, 0U) << gfx::to_string(layout) << ", " << rounds << " rounds";
            EXPECT_GT(hit_count, rays.size() / 4) << "rays that hit something";
            EXPECT_EQ(read<gpu::GpuBvhHeader>(tree.header_buffer(), 1)[0].flags, GpuBvhFlag::NONE);
        }
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuBvhTest, RefitsFollowMovedBoxes)
{
    // The same leaves and children, the moved boxes, the pairs among them.
    constexpr std::uint32_t COUNT = 10'000;
    const std::vector<TestBox> boxes = scene_boxes(COUNT, 51);
    std::vector<TestBox> moved = boxes;
    std::mt19937 generator{52};
    std::uniform_real_distribution<float> offset{-3.0F, 3.0F};
    for (TestBox& box : moved)
    {
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const float shift = offset(generator);
            box.minimum[axis] += shift;
            box.maximum[axis] += shift + 0.1F * std::abs(offset(generator));
        }
    }
    for (const GpuBvhLayout layout : {GpuBvhLayout::BINARY, GpuBvhLayout::FOUR_WIDE})
    {
        GpuBvh& tree = make_tree({.layout = layout}, COUNT);
        const ReadTree before = build(tree, boxes_buffer(boxes), COUNT);
        gfx::Buffer& moved_boxes = boxes_buffer(moved);
        record([&](gfx::ComputeRecorder& recorder) { tree.refit(recorder, gpu::span_of(moved_boxes)); });
        const ReadTree after = read_tree(tree, COUNT);
        const std::vector<std::string> problems = tree_problems(after, moved, true);
        EXPECT_TRUE(problems.empty()) << gfx::to_string(layout) << ": " << (problems.empty() ? "" : problems.front());
        bool same_children = true;
        for (std::size_t node = 0; node < before.nodes.size(); ++node)
        {
            same_children = same_children && before.nodes[node].first == after.nodes[node].first && before.nodes[node].second == after.nodes[node].second;
        }
        EXPECT_TRUE(same_children) << gfx::to_string(layout);
        if (layout == GpuBvhLayout::FOUR_WIDE)
        {
            std::size_t different = 0;
            for (const auto& [node, wide] : wide_nodes(after))
            {
                different += std::bit_cast<std::array<std::uint32_t, 32>>(after.wide_nodes[node]) != std::bit_cast<std::array<std::uint32_t, 32>>(wide) ? 1U : 0U;
            }
            EXPECT_EQ(different, 0U);
        }
        const std::vector<std::pair<std::uint32_t, std::uint32_t>> expected = brute_force_pairs(moved);
        auto [pairs, total] = find_pairs(tree, nullptr, static_cast<std::uint32_t>(expected.size() + 10));
        std::ranges::sort(pairs);
        EXPECT_EQ(pairs, expected) << gfx::to_string(layout);
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuBvhTest, CountsWrittenByAPassAreHonored)
{
    // A tree of the 1234 boxes a word counts, sized for 5000: the tree of those 1234 boxes.
    constexpr std::uint32_t MAXIMUM = 5'000;
    constexpr std::uint32_t COUNT = 1'234;
    const std::vector<TestBox> boxes = scene_boxes(MAXIMUM, 61);
    gfx::Buffer& input = boxes_buffer(boxes);
    gfx::Buffer& count = words_buffer(1, std::array{COUNT});
    GpuBvh& counted = make_tree({.layout = GpuBvhLayout::FOUR_WIDE, .treelet_rounds = 1}, MAXIMUM);
    record([&](gfx::ComputeRecorder& recorder) { counted.build(recorder, gpu::span_of(input), {.maximum = MAXIMUM, .word = gpu::span_of(count)}); });
    const ReadTree from_word = read_tree(counted, COUNT);
    const ReadTree direct = build(make_tree({.layout = GpuBvhLayout::FOUR_WIDE, .treelet_rounds = 1}, COUNT), input, COUNT);
    EXPECT_TRUE(tree_problems(from_word, std::span{boxes}.first(COUNT), false).empty());
    EXPECT_EQ(bytes_of(from_word.nodes), bytes_of(direct.nodes));
    EXPECT_EQ(bytes_of(from_word.annotations), bytes_of(direct.annotations));
    auto [pairs, total] = find_pairs(counted, nullptr, 100'000);
    std::ranges::sort(pairs);
    EXPECT_EQ(pairs, brute_force_pairs(std::span{boxes}.first(COUNT)));
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuBvhTest, TheSameBoxesGiveTheSameBits)
{
    // Twice on this variant, once on the other variant of the primitives: the same nodes, annotations, four-wide nodes,
    // pairs in the same order.
    constexpr std::uint32_t COUNT = 30'000;
    const std::vector<TestBox> boxes = scene_boxes(COUNT, 71);
    gfx::Buffer& input = boxes_buffer(boxes);
    const gpu::GpuBvhDescription description{.layout = GpuBvhLayout::FOUR_WIDE, .treelet_rounds = 3};
    const GpuVariantChoice other_variant = std::get<1>(GetParam()) == GpuVariantChoice::AUTOMATIC ? GpuVariantChoice::PORTABLE : GpuVariantChoice::AUTOMATIC;
    gpu::GpuPrimitives other_primitives{*m_device, gpu::capabilities(*m_device, {.subgroups = other_variant})};
    GpuBvh first{*m_device, *m_primitives, description};
    GpuBvh second{*m_device, *m_primitives, description};
    GpuBvh third{*m_device, other_primitives, description};
    std::vector<ReadTree> trees;
    std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> pair_lists;
    for (GpuBvh* tree : {&first, &second, &third})
    {
        tree->reserve(COUNT);
        trees.push_back(build(*tree, input, COUNT));
        pair_lists.push_back(find_pairs(*tree, nullptr, 200'000).first);
    }
    for (std::size_t index = 1; index < trees.size(); ++index)
    {
        EXPECT_EQ(bytes_of(trees[index].nodes), bytes_of(trees[0].nodes)) << index;
        EXPECT_EQ(bytes_of(trees[index].annotations), bytes_of(trees[0].annotations)) << index;
        EXPECT_EQ(trees[index].codes, trees[0].codes) << index;
        EXPECT_EQ(pair_lists[index], pair_lists[0]) << index;
        bool same_wide = true;
        for (const auto& [node, wide] : wide_nodes(trees[0]))
        {
            same_wide = same_wide && std::bit_cast<std::array<std::uint32_t, 32>>(trees[index].wide_nodes[node]) ==
                                         std::bit_cast<std::array<std::uint32_t, 32>>(trees[0].wide_nodes[node]);
        }
        EXPECT_TRUE(same_wide) << index;
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

TEST_P(GpuBvhTest, StructureNodesListTheTree)
{
    for (const std::uint32_t count : {0U, 1U, 2U, 777U})
    {
        const std::vector<TestBox> boxes = random_boxes(count, count + 81);
        const ReadTree built = build(make_tree({}, count), boxes_buffer(boxes), count);
        std::vector<SpatialNode> nodes{SpatialNode{.depth = 99}};
        gpu::structure_nodes(built.header, built.nodes, nodes);
        ASSERT_EQ(nodes.size(), count == 0 ? 1 : 2 * std::size_t{count}) << "appended after what was there";
        EXPECT_EQ(nodes[0].depth, 99U);
        if (count == 0)
        {
            continue;
        }
        EXPECT_EQ(nodes[1].depth, 0U) << "the root first";
        CpuBox all;
        for (const TestBox& box : boxes)
        {
            all = union_of(all, box_of(box));
        }
        EXPECT_EQ(nodes[1].box.minimum.x, all.minimum[0]);
        EXPECT_EQ(nodes[1].box.maximum.z, all.maximum[2]);
        EXPECT_EQ(std::ranges::count_if(nodes, [](const SpatialNode& node) { return node.proxies == 1; }), static_cast<std::ptrdiff_t>(count))
            << "one proxy per leaf";
    }
}

TEST_P(GpuBvhTest, MisusesAreRefused)
{
    GpuBvh& tree = make_tree({}, 100);
    EXPECT_THROW(tree.reserve(GPU_XPBD_MAXIMUM_ELEMENTS + 1), gfx::InvalidUsageError);
    gfx::Buffer& scratch = words_buffer(16);
    run<gfx::QueueType::COMPUTE>([&](gfx::ComputeRecorder& recorder) {
        EXPECT_THROW(tree.refit(recorder, gpu::span_of(scratch)), gfx::InvalidUsageError) << "nothing built";
        EXPECT_THROW(tree.find_pairs(recorder, gpu::span_of(scratch), gpu::span_of(scratch), 1), gfx::InvalidUsageError) << "nothing built";
        EXPECT_THROW(tree.cast_rays(recorder, gpu::span_of(scratch), gpu::span_of(scratch), {.maximum = 1}), gfx::InvalidUsageError) << "nothing built";
        EXPECT_THROW(tree.build(recorder, gpu::span_of(scratch), {.maximum = tree.reserved() + 1}), gfx::InvalidUsageError) << "above the reserve";
    });
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuBvhTest,
                         testing::Combine(testing::ValuesIn(gfx::tests::gpu_physical_devices()),
                                          testing::Values(GpuVariantChoice::AUTOMATIC, GpuVariantChoice::PORTABLE)),
                         gfx::tests::gpu_test_name);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuBvhTest);

// ===================================================================================================== measurements

/// The layouts and treelet rounds measured on each GPU (printed; the trees and the pair counts checked against each
/// other): what decides the backend's defaults.
class GpuBvhMeasurementTest : public GpuBvhTest
{
};

TEST_P(GpuBvhMeasurementTest, LayoutsAndTreeletsAreMeasured)
{
    constexpr std::uint32_t COUNT = 100'000;
    const std::string device_name = m_physical_device->properties().name;
    for (const auto& [name, boxes] : {std::pair{"uniform", random_boxes(COUNT, 91, 100.0F, 2.0F)}, std::pair{"scene", scene_boxes(COUNT, 92)}})
    {
        gfx::Buffer& input = boxes_buffer(boxes);
        const std::vector<gpu::GpuRay> rays = random_rays(100'000, 93, boxes, 200.0F);
        gfx::Buffer& ray_buffer = elements_buffer(std::span{rays});
        gfx::Buffer& hits = words_buffer(2 * rays.size());
        gfx::Buffer& pair_buffer = words_buffer(2 * 4'000'000);
        gfx::Buffer& pair_count = words_buffer(1);
        std::optional<std::uint32_t> pairs_found;
        for (const std::uint32_t rounds : {0U, 1U, 2U, 3U})
        {
            for (const GpuBvhLayout layout : {GpuBvhLayout::BINARY, GpuBvhLayout::FOUR_WIDE})
            {
                GpuBvh& tree = make_tree({.layout = layout, .treelet_rounds = rounds}, COUNT);
                const double build_time = milliseconds([&](gfx::ComputeRecorder& recorder) { tree.build(recorder, gpu::span_of(input), {.maximum = COUNT}); });
                const float cost = read<gpu::GpuBvhAnnotation>(tree.annotation_buffer(), 1)[0].cost;
                const double pairs_time = milliseconds(
                    [&](gfx::ComputeRecorder& recorder) { tree.find_pairs(recorder, gpu::span_of(pair_buffer), gpu::span_of(pair_count), 4'000'000); });
                const std::uint32_t found = words(pair_count, 1)[0];
                const double rays_time = milliseconds([&](gfx::ComputeRecorder& recorder) {
                    tree.cast_rays(recorder, gpu::span_of(ray_buffer), gpu::span_of(hits), {.maximum = static_cast<std::uint32_t>(rays.size())});
                });
                std::cout << std::format("{} | {} {} boxes | {} rounds | {:9} | build {:7.2f} ms | SAH {:12.0f} | {} pairs {:7.2f} ms | {} rays {:7.2f} ms\n",
                                         device_name, name, COUNT, rounds, gfx::to_string(layout), build_time, cost, found, pairs_time, rays.size(), rays_time);
                EXPECT_EQ(found, pairs_found.value_or(found)) << "every tree finds the same pairs";
                pairs_found = found;
                EXPECT_EQ(read<gpu::GpuBvhHeader>(tree.header_buffer(), 1)[0].flags, GpuBvhFlag::NONE);
            }
        }
    }
    EXPECT_TRUE(take_validation_messages().empty());
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuBvhMeasurementTest,
                         testing::Combine(testing::ValuesIn(gfx::tests::gpu_physical_devices()), testing::Values(GpuVariantChoice::AUTOMATIC)),
                         gfx::tests::gpu_test_name);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuBvhMeasurementTest);

} // namespace
