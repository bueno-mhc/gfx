/// @file bsp.cppm
/// @brief A binary space partition by axis-aligned planes: a node splits space at a plane chosen by the surface-area
///        heuristic over its proxies (16 bins along the axis of widest spread); proxies wholly on one side go down,
///        proxies the plane cuts stay at the node. Leaves split past LEAF_CAPACITY proxies; inserts and removals are
///        incremental, and the tree is rebuilt once the changes since the last build outnumber the proxies, or as soon
///        as a split would go deeper than about twice a balanced tree (inserts in sorted order). Each node keeps a box around everything below it for queries. Suits static-heavy
///        scenes.
export module gfx.physics.spatial:bsp;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.primitives;
import :structure;

export namespace gfx::physics
{

/// @brief A binary space partition by axis-aligned planes.
class Bsp
{
public:
    static constexpr SpatialStructureKind KIND = SpatialStructureKind::BSP;

    /// @brief Proxies a leaf holds before it splits.
    static constexpr std::uint32_t LEAF_CAPACITY = 8;

    /// @brief The deepest a node can be.
    static constexpr std::uint32_t MAXIMUM_DEPTH = 48;

    Bsp();

    auto insert(ColliderHandle key,
                const Aabb& box) -> SpatialProxyHandle;

    void remove(SpatialProxyHandle proxy);

    /// @return Whether the proxy changed node.
    auto update(SpatialProxyHandle proxy,
                const Aabb& box) -> bool;

    [[nodiscard]] auto key(SpatialProxyHandle proxy) const -> ColliderHandle
    {
        return m_proxies[m_proxies.slot_of(proxy, "Bsp::key")].key;
    }

    [[nodiscard]] auto box(SpatialProxyHandle proxy) const -> Aabb
    {
        return m_proxies[m_proxies.slot_of(proxy, "Bsp::box")].box;
    }

    [[nodiscard]] auto size() const noexcept -> std::uint32_t
    {
        return m_proxies.size();
    }

    /// @brief Builds the tree anew from its proxies (also done by itself after many changes).
    void rebuild();

    /// @brief The tree's depth (0: the root alone).
    [[nodiscard]] auto depth() const noexcept -> std::uint32_t;

    void clear();

    template<class Visitor>
    void query(const Aabb& box,
               Visitor&& visitor) const
    {
        traverse([&box](const Aabb& region) { return intersects(region, box); }, [&](std::uint32_t slot) { return visitor(m_proxies[slot].key); });
    }

    template<class Visitor>
    void query(const Ray& ray,
               float maximum,
               Visitor&& visitor) const
    {
        traverse([&](const Aabb& region) { return ray_distance(ray, region, maximum).has_value(); },
                 [&](std::uint32_t slot) { return visitor(m_proxies[slot].key, *ray_distance(ray, m_proxies[slot].box, maximum)); });
    }

    template<class Visitor>
    void query(const Segment& segment,
               Visitor&& visitor) const
    {
        traverse([&segment](const Aabb& region) { return intersects(segment, region); }, [&](std::uint32_t slot) { return visitor(m_proxies[slot].key); });
    }

    template<class Visitor>
    void query(const Sphere& sphere,
               Visitor&& visitor) const
    {
        traverse([&sphere](const Aabb& region) { return intersects(region, sphere); }, [&](std::uint32_t slot) { return visitor(m_proxies[slot].key); });
    }

    template<class Visitor>
    void query(const Frustum& frustum,
               Visitor&& visitor) const
    {
        traverse([&frustum](const Aabb& region) { return classify(frustum, region) != FrustumIntersection::OUTSIDE; },
                 [&](std::uint32_t slot) { return visitor(m_proxies[slot].key); });
    }

    template<class Visitor>
    void pairs(Visitor&& visitor) const
    {
        for (std::uint32_t slot = 0; slot < m_proxies.slot_count(); ++slot)
        {
            if (!m_proxies.alive(slot))
            {
                continue;
            }
            const Aabb& own = m_proxies[slot].box;
            traverse([&own](const Aabb& region) { return intersects(region, own); },
                     [&](std::uint32_t other) {
                         if (other > slot)
                         {
                             visitor(m_proxies[slot].key, m_proxies[other].key);
                         }
                         return true;
                     });
        }
    }

    template<class Visitor>
    void iterate(Visitor&& visitor) const
    {
        for (std::uint32_t slot = 0; slot < m_proxies.slot_count(); ++slot)
        {
            if (m_proxies.alive(slot))
            {
                visitor(m_proxies[slot].key, m_proxies[slot].box);
            }
        }
    }

private:
    /// A node: a leaf (axis 3) or a plane `component(axis) = split` with children `first` and `first + 1` (below,
    /// above); its proxies (a list from `head`) are its leaf's or those its plane cuts.
    struct Node
    {
        Aabb bounds = Aabb::empty();
        float split = 0.0F;
        std::uint32_t axis = 3;
        std::uint32_t first = spatial_detail::NONE;
        std::uint32_t head = spatial_detail::NONE;
        std::uint32_t count = 0;
        std::uint32_t depth = 0;
        std::uint32_t split_attempt = 0;
    };

    struct Proxy
    {
        ColliderHandle key;
        Aabb box{};
        std::uint32_t node = spatial_detail::NONE;
        std::uint32_t previous = spatial_detail::NONE;
        std::uint32_t next = spatial_detail::NONE;
    };

    /// Visits, until @p visitor returns false, the proxies passing @p item_test of the nodes whose bounds pass @p test.
    template<class Test, class SlotVisitor>
    void traverse(Test&& test,
                  SlotVisitor&& visitor) const
    {
        std::array<std::uint32_t, 2 * (MAXIMUM_DEPTH + 2)> stack{};
        std::size_t size = 0;
        stack[size++] = 0;
        while (size > 0)
        {
            const Node& node = m_nodes[stack[--size]];
            if (node.count == 0 && node.axis == 3)
            {
                continue;
            }
            if (!test(node.bounds))
            {
                continue;
            }
            for (std::uint32_t slot = node.head; slot != spatial_detail::NONE; slot = m_proxies[slot].next)
            {
                if (test(m_proxies[slot].box) && !visitor(slot))
                {
                    return;
                }
            }
            if (node.axis != 3)
            {
                stack[size++] = node.first + 1;
                stack[size++] = node.first;
            }
        }
    }

    void place(std::uint32_t slot);
    void link(std::uint32_t slot,
              std::uint32_t node);
    void unlink(std::uint32_t slot);
    void split(std::uint32_t node);

private:
    std::vector<Node> m_nodes{};
    spatial_detail::ProxyStore<Proxy> m_proxies{};
    std::uint32_t m_changes = 0;
    bool m_unbalanced = false;
};

} // namespace gfx::physics
