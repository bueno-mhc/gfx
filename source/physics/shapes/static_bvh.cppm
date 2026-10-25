/// @file static_bvh.cppm
/// @brief A bounding volume hierarchy built once over a fixed set of boxes (a mesh's triangles, a compound's children):
///        top-down, binned surface-area heuristic (16 bins along the widest axis of the centroids), leaves of up to 4
///        items, median splits below depth 32 (so that up to 2³¹ items fit a traversal's fixed stack of 64). Queries visit the items whose boxes
///        overlap a box, those a ray may hit, or the pairs of items of two hierarchies whose boxes overlap.
export module gfx.physics.shapes:static_bvh;

import std;
import gfx.core.math;
import gfx.physics.primitives;

export namespace gfx::physics
{

/// @brief A node: a leaf of items `items()[first, first + count)` (count > 0), or an inner node whose children are
///        nodes `first` and `first + 1` (count 0).
struct StaticBvhNode
{
    Aabb bounds{};           ///< Around everything below.
    std::uint32_t first = 0; ///< The first item (leaf) or the first child (inner node).
    std::uint32_t count = 0; ///< Items of a leaf; 0 for an inner node.

    [[nodiscard]] constexpr auto is_leaf() const noexcept -> bool
    {
        return count > 0;
    }
};

/// @brief A hierarchy built once over boxes; item indices are the boxes' indices.
class StaticBvh
{
public:
    /// @brief Deepest tree a traversal's stack holds.
    static constexpr std::size_t MAXIMUM_DEPTH = 64;

    /// @brief An empty hierarchy.
    StaticBvh() = default;

    /// @brief The hierarchy of @p boxes.
    explicit StaticBvh(std::span<const Aabb> boxes);

    /// @brief The nodes, the root first (none when empty).
    [[nodiscard]] auto nodes() const noexcept -> std::span<const StaticBvhNode>
    {
        return m_nodes;
    }

    /// @brief The items in leaf order.
    [[nodiscard]] auto items() const noexcept -> std::span<const std::uint32_t>
    {
        return m_items;
    }

    /// @brief Whether it holds no item.
    [[nodiscard]] auto empty() const noexcept -> bool
    {
        return m_nodes.empty();
    }

    /// @brief The box around everything (empty when it holds nothing).
    [[nodiscard]] auto bounds() const noexcept -> Aabb
    {
        return m_nodes.empty() ? Aabb::empty() : m_nodes.front().bounds;
    }

    /// @brief The items' boxes, by item.
    [[nodiscard]] auto boxes() const noexcept -> std::span<const Aabb>
    {
        return m_boxes;
    }

    /// @brief Calls `visitor(item)` for each item whose box overlaps @p box.
    template<class Visitor>
    void query(const Aabb& box,
               Visitor&& visitor) const
    {
        traverse([&box](const Aabb& bounds) { return intersects(bounds, box); }, visitor);
    }

    /// @brief Calls `visitor(item)` for each item whose box the segment from @p origin along @p direction for
    ///        @p length may cross.
    template<class Visitor>
    void query_segment(Vector3 origin,
                       Vector3 direction,
                       float length,
                       Visitor&& visitor) const
    {
        traverse([&](const Aabb& bounds) { return primitives_detail::slab_interval(origin, direction * length, bounds, 1.0F).has_value(); }, visitor);
    }

    /// @brief Calls `visitor(this_item, other_item)` for each pair of items whose boxes overlap, @p other's boxes
    ///        moved into this hierarchy's frame by @p other_to_this (their boxes grow conservatively when rotated).
    template<class Visitor>
    void query_pairs(const StaticBvh& other,
                     const Transform& other_to_this,
                     Visitor&& visitor) const
    {
        if (m_nodes.empty() || other.m_nodes.empty())
        {
            return;
        }
        std::array<std::pair<std::uint32_t, std::uint32_t>, MAXIMUM_DEPTH * 2> stack{};
        std::size_t size = 0;
        stack[size++] = {0, 0};
        while (size > 0)
        {
            const auto [mine, theirs] = stack[--size];
            const StaticBvhNode& node = m_nodes[mine];
            const StaticBvhNode& other_node = other.m_nodes[theirs];
            if (!intersects(node.bounds, other_node.bounds.transformed(other_to_this)))
            {
                continue;
            }
            if (node.is_leaf() && other_node.is_leaf())
            {
                for (std::uint32_t a = node.first; a < node.first + node.count; ++a)
                {
                    for (std::uint32_t b = other_node.first; b < other_node.first + other_node.count; ++b)
                    {
                        if (intersects(m_boxes[m_items[a]], other.m_boxes[other.m_items[b]].transformed(other_to_this)))
                        {
                            visitor(m_items[a], other.m_items[b]);
                        }
                    }
                }
                continue;
            }
            // Descend the larger node (or the only inner one); the stack stays within twice the depth.
            const bool descend_mine = !node.is_leaf() && (other_node.is_leaf() || node.bounds.surface_area() >= other_node.bounds.surface_area());
            if (size + 2 > stack.size())
            {
                continue;
            }
            if (descend_mine)
            {
                stack[size++] = {node.first + 1, theirs};
                stack[size++] = {node.first, theirs};
            }
            else
            {
                stack[size++] = {mine, other_node.first + 1};
                stack[size++] = {mine, other_node.first};
            }
        }
    }

private:
    /// Visits the items of the leaves whose boxes (and their ancestors') pass @p test.
    template<class Test, class Visitor>
    void traverse(Test&& test,
                  Visitor& visitor) const
    {
        if (m_nodes.empty())
        {
            return;
        }
        std::array<std::uint32_t, MAXIMUM_DEPTH> stack{};
        std::size_t size = 0;
        stack[size++] = 0;
        while (size > 0)
        {
            const StaticBvhNode& node = m_nodes[stack[--size]];
            if (!test(node.bounds))
            {
                continue;
            }
            if (node.is_leaf())
            {
                for (std::uint32_t index = node.first; index < node.first + node.count; ++index)
                {
                    if (test(m_boxes[m_items[index]]))
                    {
                        visitor(m_items[index]);
                    }
                }
            }
            else
            {
                stack[size++] = node.first + 1;
                stack[size++] = node.first;
            }
        }
    }

private:
    std::vector<StaticBvhNode> m_nodes{};
    std::vector<std::uint32_t> m_items{};
    std::vector<Aabb> m_boxes{};
};

} // namespace gfx::physics
