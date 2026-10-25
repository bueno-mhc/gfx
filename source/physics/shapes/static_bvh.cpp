// Building a StaticBvh: top-down over the items' boxes, binned surface-area heuristic along the widest axis of their
// centers, leaves of up to LEAF_SIZE items, median splits from MEDIAN_DEPTH on (and when the bins cannot split).
module gfx.physics.shapes;

import std;
import gfx.core.math;
import gfx.physics.primitives;

namespace gfx::physics
{

namespace
{

constexpr std::uint32_t LEAF_SIZE = 4;
constexpr std::uint32_t BIN_COUNT = 16;
constexpr std::uint32_t MEDIAN_DEPTH = 32;

/// The component @p axis of @p vector.
[[nodiscard]] auto
component(Vector3 vector,
          std::size_t axis) noexcept -> float
{
    return axis == 0 ? vector.x : axis == 1 ? vector.y : vector.z;
}

/// What a build reads and writes.
struct Builder
{
    std::span<const Aabb> boxes;
    std::vector<Vector3> centers;
    std::vector<StaticBvhNode>& nodes;
    std::vector<std::uint32_t>& items;

    /// Builds node @p node over items [@p begin, @p end) at depth @p depth.
    void build(std::uint32_t node,
               std::uint32_t begin,
               std::uint32_t end,
               std::uint32_t depth)
    {
        // The node's box and its items' centers' box.
        Aabb bounds = Aabb::empty();
        Aabb center_bounds = Aabb::empty();
        for (std::uint32_t index = begin; index < end; ++index)
        {
            bounds = bounds.merged(boxes[items[index]]);
            center_bounds = center_bounds.merged(centers[items[index]]);
        }
        nodes[node] = {.bounds = bounds, .first = begin, .count = end - begin};
        const std::uint32_t count = end - begin;
        const Vector3 extent = center_bounds.extents();
        const std::size_t axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : extent.y >= extent.z ? 1 : 2;
        const float low = component(center_bounds.minimum, axis);
        const float width = component(extent, axis);
        if (count <= LEAF_SIZE || !(width > 0.0F))
        {
            return;
        }

        // The split: the cheapest of the bins' boundaries by surface area × count, or the median.
        std::uint32_t middle = begin;
        if (depth < MEDIAN_DEPTH)
        {
            std::array<Aabb, BIN_COUNT> bin_bounds{};
            std::array<std::uint32_t, BIN_COUNT> bin_counts{};
            bin_bounds.fill(Aabb::empty());
            const auto bin_of = [&](std::uint32_t item) {
                const float position = (component(centers[item], axis) - low) / width * static_cast<float>(BIN_COUNT);
                return std::min(static_cast<std::uint32_t>(std::max(position, 0.0F)), BIN_COUNT - 1);
            };
            for (std::uint32_t index = begin; index < end; ++index)
            {
                const std::uint32_t bin = bin_of(items[index]);
                bin_bounds[bin] = bin_bounds[bin].merged(boxes[items[index]]);
                ++bin_counts[bin];
            }
            std::array<float, BIN_COUNT> right_costs{};
            Aabb accumulated = Aabb::empty();
            std::uint32_t accumulated_count = 0;
            for (std::uint32_t bin = BIN_COUNT - 1; bin > 0; --bin)
            {
                accumulated = accumulated.merged(bin_bounds[bin]);
                accumulated_count += bin_counts[bin];
                right_costs[bin] = accumulated_count == 0 ? 0.0F : accumulated.surface_area() * static_cast<float>(accumulated_count);
            }
            float best_cost = bounds.surface_area() * static_cast<float>(count);
            std::uint32_t best_split = 0;
            accumulated = Aabb::empty();
            accumulated_count = 0;
            for (std::uint32_t split = 1; split < BIN_COUNT; ++split)
            {
                accumulated = accumulated.merged(bin_bounds[split - 1]);
                accumulated_count += bin_counts[split - 1];
                if (accumulated_count == 0 || accumulated_count == count)
                {
                    continue;
                }
                const float cost = accumulated.surface_area() * static_cast<float>(accumulated_count) + right_costs[split];
                if (cost < best_cost)
                {
                    best_cost = cost;
                    best_split = split;
                }
            }
            if (best_split > 0)
            {
                const auto partition = std::partition(items.begin() + begin, items.begin() + end, [&](std::uint32_t item) { return bin_of(item) < best_split; });
                middle = static_cast<std::uint32_t>(partition - items.begin());
            }
            else if (count <= 4 * LEAF_SIZE)
            {
                // No split is cheaper than a leaf of a few items: keep it.
                return;
            }
        }
        if (middle == begin || middle == end)
        {
            middle = begin + count / 2;
            std::nth_element(items.begin() + begin, items.begin() + middle, items.begin() + end,
                             [&](std::uint32_t first, std::uint32_t second) { return component(centers[first], axis) < component(centers[second], axis); });
        }

        // Two children, side by side.
        const auto left = static_cast<std::uint32_t>(nodes.size());
        nodes.resize(nodes.size() + 2);
        nodes[node] = {.bounds = bounds, .first = left, .count = 0};
        build(left, begin, middle, depth + 1);
        build(left + 1, middle, end, depth + 1);
    }
};

} // namespace

StaticBvh::StaticBvh(std::span<const Aabb> boxes)
{
    if (boxes.empty())
    {
        return;
    }
    m_boxes.assign(boxes.begin(), boxes.end());
    m_items.resize(boxes.size());
    std::iota(m_items.begin(), m_items.end(), 0U);
    Builder builder{.boxes = boxes, .centers = {}, .nodes = m_nodes, .items = m_items};
    builder.centers.reserve(boxes.size());
    for (const Aabb& box : boxes)
    {
        builder.centers.push_back(box.center());
    }
    m_nodes.reserve(boxes.size() * 2);
    m_nodes.resize(1);
    builder.build(0, 0, static_cast<std::uint32_t>(boxes.size()), 0);
}

} // namespace gfx::physics
