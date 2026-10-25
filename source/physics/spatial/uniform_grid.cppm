/// @file uniform_grid.cppm
/// @brief A hashed grid of cubic cells of a compile-time size: a proxy is listed in every cell its box covers (up to
///        MAXIMUM_CELLS_PER_PROXY; larger proxies go to a list every query and pair search visits). A pair or a query
///        hit is reported once, by the cell holding the smallest corner of the two boxes' overlap (no bookkeeping, so
///        queries stay const and concurrent). Suits many similar small objects (particles, soft bodies' self-collision).
export module gfx.physics.spatial:uniform_grid;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.primitives;
import :structure;

export namespace gfx::physics
{

/// @brief A hashed uniform grid of cubic cells of side @p CellSize.
template<float CellSize = 1.0F>
class UniformGrid
{
    static_assert(CellSize > 0.0F, "a grid's cells have a size");

public:
    static constexpr SpatialStructureKind KIND = SpatialStructureKind::UNIFORM_GRID;

    /// @brief Cells a proxy is listed in at most; larger ones are visited by every query.
    static constexpr std::int64_t MAXIMUM_CELLS_PER_PROXY = 64;

    auto insert(ColliderHandle key,
                const Aabb& box) -> SpatialProxyHandle
    {
        const auto [handle, slot] = m_proxies.add({.key = key, .box = box});
        link(slot);
        return handle;
    }

    void remove(SpatialProxyHandle proxy)
    {
        const std::uint32_t slot = m_proxies.slot_of(proxy, "UniformGrid::remove");
        unlink(slot);
        m_proxies.release(slot);
    }

    /// @return Whether the proxy's cells changed.
    auto update(SpatialProxyHandle proxy,
                const Aabb& box) -> bool
    {
        const std::uint32_t slot = m_proxies.slot_of(proxy, "UniformGrid::update");
        Proxy& record = m_proxies[slot];
        const CellRange before = range_of(record.box);
        const CellRange after = range_of(box);
        if (before == after)
        {
            record.box = box;
            return false;
        }
        unlink(slot);
        record.box = box;
        link(slot);
        return true;
    }

    [[nodiscard]] auto key(SpatialProxyHandle proxy) const -> ColliderHandle
    {
        return m_proxies[m_proxies.slot_of(proxy, "UniformGrid::key")].key;
    }

    [[nodiscard]] auto box(SpatialProxyHandle proxy) const -> Aabb
    {
        return m_proxies[m_proxies.slot_of(proxy, "UniformGrid::box")].box;
    }

    [[nodiscard]] auto size() const noexcept -> std::uint32_t
    {
        return m_proxies.size();
    }

    void clear()
    {
        m_proxies.clear();
        m_cells.clear();
        m_entries.clear();
        m_free_entries.clear();
        m_large.clear();
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
        // In each cell, the pairs whose overlap starts there; then the large proxies against everything.
        m_cells.for_each([&](std::uint64_t cell, std::uint32_t head) {
            for (std::uint32_t first = head; first != spatial_detail::NONE; first = m_entries[first].next)
            {
                const Proxy& a = m_proxies[m_entries[first].proxy];
                for (std::uint32_t second = m_entries[first].next; second != spatial_detail::NONE; second = m_entries[second].next)
                {
                    const Proxy& b = m_proxies[m_entries[second].proxy];
                    if (intersects(a.box, b.box) && canonical_cell(a.box, b.box) == cell)
                    {
                        visitor(a.key, b.key);
                    }
                }
            }
        });
        for (const std::uint32_t large : m_large)
        {
            for (std::uint32_t slot = 0; slot < m_proxies.slot_count(); ++slot)
            {
                if (slot != large && m_proxies.alive(slot) && !(m_proxies[slot].large && slot < large)
                    && intersects(m_proxies[large].box, m_proxies[slot].box))
                {
                    visitor(m_proxies[large].key, m_proxies[slot].key);
                }
            }
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
    /// Coordinates are clamped to ±2^20 cells (21 bits each in the key).
    static constexpr std::int64_t LIMIT = 1 << 20;

    /// Queries over more cells than this visit every proxy instead.
    static constexpr double MAXIMUM_QUERY_CELLS = 4096.0;

    struct CellRange
    {
        std::array<std::int64_t, 3> low{};
        std::array<std::int64_t, 3> high{};

        [[nodiscard]] auto operator==(const CellRange&) const noexcept -> bool = default;

        [[nodiscard]] auto cells() const noexcept -> double
        {
            return static_cast<double>(high[0] - low[0] + 1) * static_cast<double>(high[1] - low[1] + 1) * static_cast<double>(high[2] - low[2] + 1);
        }
    };

    struct Proxy
    {
        ColliderHandle key;
        Aabb box{};
        std::uint32_t first_entry = spatial_detail::NONE;
        std::uint32_t large_index = spatial_detail::NONE;
        bool large = false;
    };

    /// A proxy's listing in a cell.
    struct Entry
    {
        std::uint32_t proxy = spatial_detail::NONE;
        std::uint64_t cell = 0;
        std::uint32_t previous = spatial_detail::NONE;
        std::uint32_t next = spatial_detail::NONE;
        std::uint32_t next_of_proxy = spatial_detail::NONE;
    };

    [[nodiscard]] static auto coordinate(float value) noexcept -> std::int64_t
    {
        return static_cast<std::int64_t>(std::clamp(std::floor(static_cast<double>(value) / static_cast<double>(CellSize)), -static_cast<double>(LIMIT),
                                                     static_cast<double>(LIMIT - 1)));
    }

    [[nodiscard]] static auto range_of(const Aabb& box) noexcept -> CellRange
    {
        return {.low = {coordinate(box.minimum.x), coordinate(box.minimum.y), coordinate(box.minimum.z)},
                .high = {coordinate(box.maximum.x), coordinate(box.maximum.y), coordinate(box.maximum.z)}};
    }

    [[nodiscard]] static auto key_of(std::int64_t x,
                                     std::int64_t y,
                                     std::int64_t z) noexcept -> std::uint64_t
    {
        return static_cast<std::uint64_t>(x + LIMIT) | static_cast<std::uint64_t>(y + LIMIT) << 21U | static_cast<std::uint64_t>(z + LIMIT) << 42U;
    }

    /// The cell holding the smallest corner of the overlap of @p first and @p second.
    [[nodiscard]] static auto canonical_cell(const Aabb& first,
                                             const Aabb& second) noexcept -> std::uint64_t
    {
        const Vector3 corner = component_maximum(first.minimum, second.minimum);
        return key_of(coordinate(corner.x), coordinate(corner.y), coordinate(corner.z));
    }

    void link(std::uint32_t slot)
    {
        Proxy& record = m_proxies[slot];
        const CellRange range = range_of(record.box);
        record.first_entry = spatial_detail::NONE;
        if (range.cells() > static_cast<double>(MAXIMUM_CELLS_PER_PROXY))
        {
            record.large = true;
            record.large_index = static_cast<std::uint32_t>(m_large.size());
            m_large.push_back(slot);
            return;
        }
        record.large = false;
        for (std::int64_t z = range.low[2]; z <= range.high[2]; ++z)
        {
            for (std::int64_t y = range.low[1]; y <= range.high[1]; ++y)
            {
                for (std::int64_t x = range.low[0]; x <= range.high[0]; ++x)
                {
                    const std::uint64_t cell = key_of(x, y, z);
                    std::uint32_t entry = spatial_detail::NONE;
                    if (!m_free_entries.empty())
                    {
                        entry = m_free_entries.back();
                        m_free_entries.pop_back();
                    }
                    else
                    {
                        entry = static_cast<std::uint32_t>(m_entries.size());
                        m_entries.push_back({});
                    }
                    const std::uint32_t head = m_cells.find(cell);
                    m_entries[entry] = {.proxy = slot, .cell = cell, .previous = spatial_detail::NONE, .next = head, .next_of_proxy = m_proxies[slot].first_entry};
                    if (head != spatial_detail::NONE)
                    {
                        m_entries[head].previous = entry;
                    }
                    m_cells.set(cell, entry);
                    m_proxies[slot].first_entry = entry;
                }
            }
        }
    }

    void unlink(std::uint32_t slot)
    {
        Proxy& record = m_proxies[slot];
        if (record.large)
        {
            // Swap-remove from the large list.
            const std::uint32_t moved = m_large.back();
            m_large[record.large_index] = moved;
            m_proxies[moved].large_index = record.large_index;
            m_large.pop_back();
            record.large = false;
            return;
        }
        for (std::uint32_t entry = record.first_entry; entry != spatial_detail::NONE;)
        {
            const Entry listing = m_entries[entry];
            if (listing.previous != spatial_detail::NONE)
            {
                m_entries[listing.previous].next = listing.next;
            }
            else
            {
                m_cells.set(listing.cell, listing.next);
            }
            if (listing.next != spatial_detail::NONE)
            {
                m_entries[listing.next].previous = listing.previous;
            }
            m_free_entries.push_back(entry);
            entry = listing.next_of_proxy;
        }
        record.first_entry = spatial_detail::NONE;
    }

    /// Visits, until @p visitor returns false, the proxies passing @p item_test among those near @p bounds, each once.
    template<class ItemTest, class SlotVisitor>
    void visit(const Aabb& bounds,
               ItemTest&& item_test,
               SlotVisitor&& visitor) const
    {
        const CellRange range = range_of(bounds);
        if (range.cells() > MAXIMUM_QUERY_CELLS)
        {
            for (std::uint32_t slot = 0; slot < m_proxies.slot_count(); ++slot)
            {
                if (m_proxies.alive(slot) && item_test(m_proxies[slot].box) && !visitor(slot))
                {
                    return;
                }
            }
            return;
        }
        for (const std::uint32_t large : m_large)
        {
            if (item_test(m_proxies[large].box) && !visitor(large))
            {
                return;
            }
        }
        for (std::int64_t z = range.low[2]; z <= range.high[2]; ++z)
        {
            for (std::int64_t y = range.low[1]; y <= range.high[1]; ++y)
            {
                for (std::int64_t x = range.low[0]; x <= range.high[0]; ++x)
                {
                    const std::uint64_t cell = key_of(x, y, z);
                    for (std::uint32_t entry = m_cells.find(cell); entry != spatial_detail::NONE; entry = m_entries[entry].next)
                    {
                        const std::uint32_t slot = m_entries[entry].proxy;
                        const Aabb& item = m_proxies[slot].box;
                        if (intersects(item, bounds) && canonical_cell(item, bounds) == cell && item_test(item) && !visitor(slot))
                        {
                            return;
                        }
                    }
                }
            }
        }
    }

private:
    spatial_detail::CellMap m_cells{};
    std::vector<Entry> m_entries{};
    std::vector<std::uint32_t> m_free_entries{};
    std::vector<std::uint32_t> m_large{};
    spatial_detail::ProxyStore<Proxy> m_proxies{};
};

} // namespace gfx::physics
