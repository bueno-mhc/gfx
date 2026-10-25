/// @file bvh.cppm
/// @brief A dynamic bounding volume hierarchy (an AABB tree, as Box2D's): leaves are proxies; a leaf goes in beside the
///        node that grows the tree's surface area least (branch and bound on the cost of the path), and rotations keep
///        the tree balanced (heights differ by at most one, as an AVL tree's). The default broad phase's structure.
export module gfx.physics.spatial:bvh;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.primitives;
import :structure;

export namespace gfx::physics
{

/// @brief A dynamic AABB tree.
class Bvh
{
public:
    static constexpr SpatialStructureKind KIND = SpatialStructureKind::BVH;

    /// @brief A proxy for @p key with the box @p box.
    auto insert(ColliderHandle key,
                const Aabb& box) -> SpatialProxyHandle;

    /// @brief Removes @p proxy.
    /// @throws PhysicsError if @p proxy is not a live proxy of this tree.
    void remove(SpatialProxyHandle proxy);

    /// @brief Gives @p proxy the box @p box (the leaf moves in the tree).
    /// @return true (a leaf always moves).
    auto update(SpatialProxyHandle proxy,
                const Aabb& box) -> bool;

    [[nodiscard]] auto key(SpatialProxyHandle proxy) const -> ColliderHandle
    {
        return m_proxies[m_proxies.slot_of(proxy, "Bvh::key")].key;
    }

    [[nodiscard]] auto box(SpatialProxyHandle proxy) const -> Aabb
    {
        return m_nodes[m_proxies[m_proxies.slot_of(proxy, "Bvh::box")].node].box;
    }

    [[nodiscard]] auto size() const noexcept -> std::uint32_t
    {
        return m_proxies.size();
    }

    /// @brief The tree's height (0: one leaf; -1: empty).
    [[nodiscard]] auto height() const noexcept -> std::int32_t
    {
        return m_root == spatial_detail::NONE ? -1 : m_nodes[m_root].height;
    }

    void clear();

    /// @brief Calls `visitor(key)` for each proxy whose box overlaps @p box, until it returns false.
    template<class Visitor>
    void query(const Aabb& box,
               Visitor&& visitor) const
    {
        traverse([&box](const Aabb& node) { return intersects(node, box); }, [&](std::uint32_t leaf) { return visitor(m_proxies[m_nodes[leaf].proxy].key); });
    }

    /// @brief Calls `visitor(key, distance)` for each proxy whose box @p ray enters within @p maximum (the distance at
    ///        which it does), until it returns false.
    template<class Visitor>
    void query(const Ray& ray,
               float maximum,
               Visitor&& visitor) const
    {
        traverse([&](const Aabb& node) { return ray_distance(ray, node, maximum).has_value(); },
                 [&](std::uint32_t leaf) {
                     const std::optional<float> distance = ray_distance(ray, m_nodes[leaf].box, maximum);
                     return !distance || visitor(m_proxies[m_nodes[leaf].proxy].key, *distance);
                 });
    }

    /// @brief Calls `visitor(key)` for each proxy whose box @p segment crosses, until it returns false.
    template<class Visitor>
    void query(const Segment& segment,
               Visitor&& visitor) const
    {
        traverse([&segment](const Aabb& node) { return intersects(segment, node); }, [&](std::uint32_t leaf) { return visitor(m_proxies[m_nodes[leaf].proxy].key); });
    }

    /// @brief Calls `visitor(key)` for each proxy whose box @p sphere overlaps, until it returns false.
    template<class Visitor>
    void query(const Sphere& sphere,
               Visitor&& visitor) const
    {
        traverse([&sphere](const Aabb& node) { return intersects(node, sphere); }, [&](std::uint32_t leaf) { return visitor(m_proxies[m_nodes[leaf].proxy].key); });
    }

    /// @brief Calls `visitor(key)` for each proxy whose box is not outside @p frustum, until it returns false.
    template<class Visitor>
    void query(const Frustum& frustum,
               Visitor&& visitor) const
    {
        traverse([&frustum](const Aabb& node) { return classify(frustum, node) != FrustumIntersection::OUTSIDE; },
                 [&](std::uint32_t leaf) { return visitor(m_proxies[m_nodes[leaf].proxy].key); });
    }

    /// @brief Calls `visitor(first, second)` once for each pair of proxies whose boxes overlap.
    template<class Visitor>
    void pairs(Visitor&& visitor) const
    {
        // Each leaf against the tree; the pair is reported by its leaf of lower slot.
        for (std::uint32_t slot = 0; slot < m_proxies.slot_count(); ++slot)
        {
            if (!m_proxies.alive(slot))
            {
                continue;
            }
            const Aabb& own = m_nodes[m_proxies[slot].node].box;
            traverse([&own](const Aabb& node) { return intersects(node, own); },
                     [&](std::uint32_t leaf) {
                         if (m_nodes[leaf].proxy > slot)
                         {
                             visitor(m_proxies[slot].key, m_proxies[m_nodes[leaf].proxy].key);
                         }
                         return true;
                     });
        }
    }

    /// @brief Calls `visitor(key, box)` for every proxy.
    template<class Visitor>
    void iterate(Visitor&& visitor) const
    {
        for (std::uint32_t slot = 0; slot < m_proxies.slot_count(); ++slot)
        {
            if (m_proxies.alive(slot))
            {
                visitor(m_proxies[slot].key, m_nodes[m_proxies[slot].node].box);
            }
        }
    }

private:
    /// A node: a leaf (child1 NONE) holds a proxy's slot.
    struct Node
    {
        Aabb box{};
        std::uint32_t parent = spatial_detail::NONE;
        std::uint32_t child1 = spatial_detail::NONE;
        std::uint32_t child2 = spatial_detail::NONE;
        std::int32_t height = 0;
        std::uint32_t proxy = spatial_detail::NONE;
    };

    /// What a proxy is: its key and its leaf.
    struct Proxy
    {
        ColliderHandle key;
        std::uint32_t node = spatial_detail::NONE;
    };

    /// Visits the leaves under the nodes that pass @p test, until @p leaf_visitor returns false.
    template<class Test, class LeafVisitor>
    void traverse(Test&& test,
                  LeafVisitor&& leaf_visitor) const
    {
        if (m_root == spatial_detail::NONE)
        {
            return;
        }
        std::array<std::uint32_t, 256> stack{};
        std::size_t size = 0;
        stack[size++] = m_root;
        while (size > 0)
        {
            const std::uint32_t index = stack[--size];
            const Node& node = m_nodes[index];
            if (!test(node.box))
            {
                continue;
            }
            if (node.child1 == spatial_detail::NONE)
            {
                if (!leaf_visitor(index))
                {
                    return;
                }
            }
            else if (size + 2 <= stack.size())
            {
                stack[size++] = node.child2;
                stack[size++] = node.child1;
            }
        }
    }

    auto allocate_node() -> std::uint32_t;
    void free_node(std::uint32_t node);
    void insert_leaf(std::uint32_t leaf);
    void remove_leaf(std::uint32_t leaf);
    auto balance(std::uint32_t node) -> std::uint32_t;

private:
    std::vector<Node> m_nodes{};
    std::vector<std::uint32_t> m_free_nodes{};
    std::uint32_t m_root = spatial_detail::NONE;
    spatial_detail::ProxyStore<Proxy> m_proxies{};
};

} // namespace gfx::physics
