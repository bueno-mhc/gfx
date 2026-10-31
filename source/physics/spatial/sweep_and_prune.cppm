/// @file sweep_and_prune.cppm
/// @brief Sweep and prune: proxies kept sorted by the lower end of their boxes along one axis (x) or along each of the
///        three; a moved proxy slides to its place by insertion (cheap when motion is coherent). The pair search sweeps
///        the sorted list (with three axes, along the axis whose centers spread most), keeping the proxies whose span
///        has not ended yet. Suits small, coherent scenes.
export module gfx.physics.spatial:sweep_and_prune;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.primitives;
import :structure;

export namespace gfx::physics
{

/// @brief Sweep and prune along x (@p Axes 1) or along the best of three sorted axes (@p Axes 3).
template<std::uint32_t Axes = 1>
class SweepAndPrune
{
    static_assert(Axes == 1 || Axes == 3, "sweep and prune sorts along one axis or along three");

public:
    static constexpr SpatialStructureKind KIND = SpatialStructureKind::SWEEP_AND_PRUNE;

    auto insert(ColliderHandle key,
                const Aabb& box) -> SpatialProxyHandle
    {
        const auto [handle, slot] = m_proxies.add({.key = key, .box = box});
        for (std::uint32_t axis = 0; axis < Axes; ++axis)
        {
            m_proxies[slot].position[axis] = static_cast<std::uint32_t>(m_order[axis].size());
            m_order[axis].push_back(slot);
            settle(axis, slot);
        }
        return handle;
    }

    void remove(SpatialProxyHandle proxy)
    {
        const std::uint32_t slot = m_proxies.slot_of(proxy, "SweepAndPrune::remove");
        for (std::uint32_t axis = 0; axis < Axes; ++axis)
        {
            std::vector<std::uint32_t>& order = m_order[axis];
            const std::uint32_t position = m_proxies[slot].position[axis];
            order.erase(order.begin() + position);
            for (std::uint32_t index = position; index < order.size(); ++index)
            {
                m_proxies[order[index]].position[axis] = index;
            }
        }
        m_proxies.release(slot);
    }

    /// @return Whether the proxy moved in a sorted order.
    auto update(SpatialProxyHandle proxy,
                const Aabb& box) -> bool
    {
        const std::uint32_t slot = m_proxies.slot_of(proxy, "SweepAndPrune::update");
        m_proxies[slot].box = box;
        bool moved = false;
        for (std::uint32_t axis = 0; axis < Axes; ++axis)
        {
            const std::uint32_t before = m_proxies[slot].position[axis];
            settle(axis, slot);
            moved = moved || before != m_proxies[slot].position[axis];
        }
        return moved;
    }

    [[nodiscard]] auto key(SpatialProxyHandle proxy) const -> ColliderHandle
    {
        return m_proxies[m_proxies.slot_of(proxy, "SweepAndPrune::key")].key;
    }

    [[nodiscard]] auto box(SpatialProxyHandle proxy) const -> Aabb
    {
        return m_proxies[m_proxies.slot_of(proxy, "SweepAndPrune::box")].box;
    }

    [[nodiscard]] auto size() const noexcept -> std::uint32_t
    {
        return m_proxies.size();
    }

    void clear()
    {
        m_proxies.clear();
        for (std::vector<std::uint32_t>& order : m_order)
        {
            order.clear();
        }
    }

    template<class Visitor>
    void query(const Aabb& box,
               Visitor&& visitor) const
    {
        visit(box, [&box](const Aabb& item) { return intersects(item, box); }, [&](std::uint32_t slot) { return visitor(m_proxies[slot].key); });
    }

    template<class Visitor>
    void query(const Ray& ray,
               float maximum,
               Visitor&& visitor) const
    {
        visit(spatial_detail::bounds_of(ray, maximum), [&](const Aabb& item) { return ray_distance(ray, item, maximum).has_value(); },
              [&](std::uint32_t slot) { return visitor(m_proxies[slot].key, *ray_distance(ray, m_proxies[slot].box, maximum)); });
    }

    template<class Visitor>
    void query(const Segment& segment,
               Visitor&& visitor) const
    {
        visit(segment.bounds(), [&segment](const Aabb& item) { return intersects(segment, item); }, [&](std::uint32_t slot) { return visitor(m_proxies[slot].key); });
    }

    template<class Visitor>
    void query(const Sphere& sphere,
               Visitor&& visitor) const
    {
        visit(sphere.bounds(), [&sphere](const Aabb& item) { return intersects(item, sphere); }, [&](std::uint32_t slot) { return visitor(m_proxies[slot].key); });
    }

    template<class Visitor>
    void query(const Frustum& frustum,
               Visitor&& visitor) const
    {
        visit(spatial_detail::everything(), [&frustum](const Aabb& item) { return classify(frustum, item) != FrustumIntersection::OUTSIDE; },
              [&](std::uint32_t slot) { return visitor(m_proxies[slot].key); });
    }

    template<class Visitor>
    void pairs(Visitor&& visitor) const
    {
        // The sweep: each proxy against those after it whose lower end is not past its upper end.
        const std::uint32_t axis = sweep_axis();
        const std::vector<std::uint32_t>& order = m_order[axis];
        for (std::size_t index = 0; index < order.size(); ++index)
        {
            const Proxy& first = m_proxies[order[index]];
            const float end = component(first.box.maximum, axis);
            for (std::size_t other = index + 1; other < order.size(); ++other)
            {
                const Proxy& second = m_proxies[order[other]];
                if (component(second.box.minimum, axis) > end)
                {
                    break;
                }
                if (intersects(first.box, second.box))
                {
                    visitor(first.key, second.key);
                }
            }
        }
    }

    template<class Visitor>
    void iterate(Visitor&& visitor) const
    {
        for (const std::uint32_t slot : m_order[0])
        {
            visitor(m_proxies[slot].key, m_proxies[slot].box);
        }
    }

    /// @brief Calls `visitor(node)` for every proxy in the order of the first axis: no hierarchy, only intervals sorted
    ///        on each axis, so a node is a proxy's box (its intervals), depth 0, one proxy.
    template<class Visitor>
    void nodes(Visitor&& visitor) const
    {
        for (const std::uint32_t slot : m_order[0])
        {
            visitor(SpatialNode{.box = m_proxies[slot].box, .depth = 0, .proxies = 1});
        }
    }

private:
    struct Proxy
    {
        ColliderHandle key;
        Aabb box{};
        std::array<std::uint32_t, Axes> position{};
    };

    [[nodiscard]] static auto component(Vector3 vector,
                                        std::uint32_t axis) noexcept -> float
    {
        return axis == 0 ? vector.x : axis == 1 ? vector.y : vector.z;
    }

    /// Slides @p slot to its place in the order of @p axis (by lower end, then slot).
    void settle(std::uint32_t axis,
                std::uint32_t slot)
    {
        std::vector<std::uint32_t>& order = m_order[axis];
        const auto before = [&](std::uint32_t first, std::uint32_t second) {
            const float a = component(m_proxies[first].box.minimum, axis);
            const float b = component(m_proxies[second].box.minimum, axis);
            return a < b || (a == b && first < second);
        };
        std::uint32_t position = m_proxies[slot].position[axis];
        while (position > 0 && before(slot, order[position - 1]))
        {
            order[position] = order[position - 1];
            m_proxies[order[position]].position[axis] = position;
            --position;
        }
        while (position + 1 < order.size() && before(order[position + 1], slot))
        {
            order[position] = order[position + 1];
            m_proxies[order[position]].position[axis] = position;
            ++position;
        }
        order[position] = slot;
        m_proxies[slot].position[axis] = position;
    }

    /// The sorted axis the centers spread most along (x with one axis).
    [[nodiscard]] auto sweep_axis() const noexcept -> std::uint32_t
    {
        if constexpr (Axes == 1)
        {
            return 0;
        }
        else
        {
            std::array<double, 3> sum{};
            std::array<double, 3> squares{};
            for (const std::uint32_t slot : m_order[0])
            {
                const Vector3 center = m_proxies[slot].box.center();
                for (std::uint32_t axis = 0; axis < 3; ++axis)
                {
                    sum[axis] += component(center, axis);
                    squares[axis] += static_cast<double>(component(center, axis)) * component(center, axis);
                }
            }
            std::uint32_t best = 0;
            for (std::uint32_t axis = 1; axis < 3; ++axis)
            {
                const auto spread = [&](std::uint32_t which) { return squares[which] - sum[which] * sum[which] / std::max<double>(1.0, static_cast<double>(m_order[0].size())); };
                best = spread(axis) > spread(best) ? axis : best;
            }
            return best;
        }
    }

    /// Visits, until @p visitor returns false, the proxies passing @p item_test whose lower x end is at most @p bounds'
    /// upper one (the order on x ends the scan there).
    template<class ItemTest, class SlotVisitor>
    void visit(const Aabb& bounds,
               ItemTest&& item_test,
               SlotVisitor&& visitor) const
    {
        for (const std::uint32_t slot : m_order[0])
        {
            const Proxy& proxy = m_proxies[slot];
            if (proxy.box.minimum.x > bounds.maximum.x)
            {
                return;
            }
            if (proxy.box.maximum.x >= bounds.minimum.x && item_test(proxy.box) && !visitor(slot))
            {
                return;
            }
        }
    }

private:
    std::array<std::vector<std::uint32_t>, Axes> m_order{};
    spatial_detail::ProxyStore<Proxy> m_proxies{};
};

} // namespace gfx::physics
