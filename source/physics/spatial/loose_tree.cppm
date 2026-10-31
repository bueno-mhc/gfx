/// @file loose_tree.cppm
/// @brief Loose octrees and quadtrees (Ulrich's loose octree): a region split in 2^L cells per axis at level L; a
///        proxy goes in the deepest level whose cells, grown by the looseness, hold it whatever its position (its size
///        at most (looseness - 1) × the cell size), in the cell of its center. Cells are hashed per level (only those
///        used exist); a query visits, per level, the cells whose grown boxes overlap it — or every used cell of the level
///        when that is fewer. Proxies too large for level 1 or outside the region stay at level 0, which every query
///        visits. Depth, looseness (and the quadtree's plane) are template arguments: compile-time parameters.
export module gfx.physics.spatial:loose_tree;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.primitives;
import :structure;

export namespace gfx::physics
{

/// @brief The loose tree over @p Dimensions axes (3: an octree; 2: a quadtree over @p Plane's axes, the third axis
///        ignored when placing proxies — queries still test whole boxes).
/// @tparam Depth     The deepest level (1 to 20).
/// @tparam Looseness How much larger than its cell a cell's box is (greater than 1; 2 is usual).
template<std::uint32_t Dimensions, AxisPlane Plane, std::uint32_t Depth, float Looseness>
class LooseTree
{
    static_assert(Dimensions == 2 || Dimensions == 3, "a loose tree splits two axes (a quadtree) or three (an octree)");
    static_assert(Depth >= 1 && Depth <= 20, "a loose tree has 1 to 20 levels below the root (20 bits per coordinate)");
    static_assert(Looseness > 1.0F, "a loose tree's cells must be larger than their share of the region");

public:
    static constexpr SpatialStructureKind KIND = Dimensions == 3 ? SpatialStructureKind::OCTREE : SpatialStructureKind::QUADTREE;

    /// @brief A tree over [-1024, 1024] on each axis.
    LooseTree()
        : LooseTree(Aabb::from_center({}, {.x = 1024, .y = 1024, .z = 1024}))
    {
    }

    /// @brief A tree over @p region (its extents greater than 0 along the split axes).
    explicit LooseTree(const Aabb& region)
        : m_region{region}
    {
        for (const std::size_t axis : AXES)
        {
            if (!(component(region.extents(), axis) > 0.0F) || !std::isfinite(component(region.extents(), axis)))
            {
                spatial_detail::reject_loose_region(region);
            }
        }
    }

    auto insert(ColliderHandle key,
                const Aabb& box) -> SpatialProxyHandle
    {
        const auto [handle, slot] = m_proxies.add({.key = key, .box = box});
        link(slot);
        return handle;
    }

    void remove(SpatialProxyHandle proxy)
    {
        const std::uint32_t slot = m_proxies.slot_of(proxy, KIND == SpatialStructureKind::OCTREE ? "Octree::remove" : "Quadtree::remove");
        unlink(slot);
        m_proxies.release(slot);
    }

    /// @return Whether the proxy changed cell.
    auto update(SpatialProxyHandle proxy,
                const Aabb& box) -> bool
    {
        const std::uint32_t slot = m_proxies.slot_of(proxy, KIND == SpatialStructureKind::OCTREE ? "Octree::update" : "Quadtree::update");
        const auto [level, cell] = placement(box);
        Proxy& record = m_proxies[slot];
        if (level == record.level && cell == record.cell)
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
        return m_proxies[m_proxies.slot_of(proxy, "LooseTree::key")].key;
    }

    [[nodiscard]] auto box(SpatialProxyHandle proxy) const -> Aabb
    {
        return m_proxies[m_proxies.slot_of(proxy, "LooseTree::box")].box;
    }

    [[nodiscard]] auto size() const noexcept -> std::uint32_t
    {
        return m_proxies.size();
    }

    /// @brief The level @p proxy is at (0: the root).
    [[nodiscard]] auto level(SpatialProxyHandle proxy) const -> std::uint32_t
    {
        return m_proxies[m_proxies.slot_of(proxy, "LooseTree::level")].level;
    }

    void clear()
    {
        m_proxies.clear();
        for (spatial_detail::CellMap& cells : m_cells)
        {
            cells.clear();
        }
        m_level_counts.fill(0);
    }

    template<class Visitor>
    void query(const Aabb& box,
               Visitor&& visitor) const
    {
        visit(box, [&box](const Aabb& cell) { return intersects(cell, box); }, [&box](const Aabb& item) { return intersects(item, box); },
              [&](std::uint32_t slot) { return visitor(m_proxies[slot].key); });
    }

    template<class Visitor>
    void query(const Ray& ray,
               float maximum,
               Visitor&& visitor) const
    {
        visit(spatial_detail::bounds_of(ray, maximum), [&](const Aabb& cell) { return ray_distance(ray, cell, maximum).has_value(); },
              [&](const Aabb& item) { return ray_distance(ray, item, maximum).has_value(); },
              [&](std::uint32_t slot) { return visitor(m_proxies[slot].key, *ray_distance(ray, m_proxies[slot].box, maximum)); });
    }

    template<class Visitor>
    void query(const Segment& segment,
               Visitor&& visitor) const
    {
        visit(segment.bounds(), [&segment](const Aabb& cell) { return intersects(segment, cell); }, [&segment](const Aabb& item) { return intersects(segment, item); },
              [&](std::uint32_t slot) { return visitor(m_proxies[slot].key); });
    }

    template<class Visitor>
    void query(const Sphere& sphere,
               Visitor&& visitor) const
    {
        visit(sphere.bounds(), [&sphere](const Aabb& cell) { return intersects(cell, sphere); }, [&sphere](const Aabb& item) { return intersects(item, sphere); },
              [&](std::uint32_t slot) { return visitor(m_proxies[slot].key); });
    }

    template<class Visitor>
    void query(const Frustum& frustum,
               Visitor&& visitor) const
    {
        const auto visible = [&frustum](const Aabb& box) { return classify(frustum, box) != FrustumIntersection::OUTSIDE; };
        visit(spatial_detail::everything(), visible, visible, [&](std::uint32_t slot) { return visitor(m_proxies[slot].key); });
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
            visit(own, [&own](const Aabb& cell) { return intersects(cell, own); }, [&own](const Aabb& item) { return intersects(item, own); },
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

    /// @brief Calls `visitor(node)` for every occupied cell, coarsest level first: the cell's own box (on the axis a
    ///        quadtree does not split, and for level 0 — proxies too large or outside the region — the box around its
    ///        proxies), its level, its proxies.
    template<class Visitor>
    void nodes(Visitor&& visitor) const
    {
        for (std::uint32_t level = 0; level <= Depth; ++level)
        {
            if (m_level_counts[level] == 0)
            {
                continue;
            }
            m_cells[level].for_each([&](std::uint64_t cell, std::uint32_t head) {
                // The box around the cell's proxies, then the cell's own extent along the axes the tree splits.
                Aabb box = Aabb::empty();
                std::uint32_t count = 0;
                for (std::uint32_t slot = head; slot != spatial_detail::NONE; slot = m_proxies[slot].next)
                {
                    box = box.merged(m_proxies[slot].box);
                    ++count;
                }
                if (level > 0)
                {
                    std::array<float, 3> low{box.minimum.x, box.minimum.y, box.minimum.z};
                    std::array<float, 3> high{box.maximum.x, box.maximum.y, box.maximum.z};
                    for (std::size_t index = 0; index < Dimensions; ++index)
                    {
                        const std::size_t axis = AXES[index];
                        const float size = cell_size(level, axis);
                        const auto coordinate = static_cast<float>(cell >> (20U * index) & 0xFFFFFU);
                        low[axis] = component(m_region.minimum, axis) + coordinate * size;
                        high[axis] = low[axis] + size;
                    }
                    box = {.minimum = {.x = low[0], .y = low[1], .z = low[2]}, .maximum = {.x = high[0], .y = high[1], .z = high[2]}};
                }
                visitor(SpatialNode{.box = box, .depth = level, .proxies = count});
            });
        }
    }

private:
    /// The axes the tree splits.
    static constexpr std::array<std::size_t, Dimensions> AXES = [] {
        std::array<std::size_t, Dimensions> axes{};
        if constexpr (Dimensions == 3)
        {
            axes = {0, 1, 2};
        }
        else
        {
            axes = Plane == AxisPlane::XY ? std::array<std::size_t, Dimensions>{0, 1} : Plane == AxisPlane::XZ ? std::array<std::size_t, Dimensions>{0, 2}
                                                                                                                 : std::array<std::size_t, Dimensions>{1, 2};
        }
        return axes;
    }();

    struct Proxy
    {
        ColliderHandle key;
        Aabb box{};
        std::uint64_t cell = 0;
        std::uint32_t level = 0;
        std::uint32_t previous = spatial_detail::NONE;
        std::uint32_t next = spatial_detail::NONE;
    };

    [[nodiscard]] static auto component(Vector3 vector,
                                        std::size_t axis) noexcept -> float
    {
        return axis == 0 ? vector.x : axis == 1 ? vector.y : vector.z;
    }


    /// The cell size at @p level along @p axis.
    [[nodiscard]] auto cell_size(std::uint32_t level,
                                 std::size_t axis) const noexcept -> float
    {
        return component(m_region.extents(), axis) / static_cast<float>(1U << level);
    }

    /// The level and the cell key of a proxy with the box @p box.
    [[nodiscard]] auto placement(const Aabb& box) const noexcept -> std::pair<std::uint32_t, std::uint64_t>
    {
        const Vector3 center = box.center();
        const Vector3 extents = box.extents();
        for (std::size_t axis : AXES)
        {
            if (!(component(center, axis) >= component(m_region.minimum, axis) && component(center, axis) <= component(m_region.maximum, axis)))
            {
                return {0, 0};
            }
        }
        for (std::uint32_t level = Depth; level >= 1; --level)
        {
            bool fits = true;
            for (std::size_t axis : AXES)
            {
                fits = fits && component(extents, axis) <= (Looseness - 1.0F) * cell_size(level, axis);
            }
            if (fits)
            {
                std::uint64_t key = 0;
                for (std::size_t index = 0; index < Dimensions; ++index)
                {
                    const std::size_t axis = AXES[index];
                    const float position = (component(center, axis) - component(m_region.minimum, axis)) / cell_size(level, axis);
                    const auto coordinate = static_cast<std::uint64_t>(std::clamp(position, 0.0F, static_cast<float>((1U << level) - 1)));
                    key |= coordinate << (20U * index);
                }
                return {level, key};
            }
        }
        return {0, 0};
    }

    /// The grown box of cell @p key at @p level (unbounded at level 0).
    [[nodiscard]] auto loose_box(std::uint32_t level,
                                 std::uint64_t key) const noexcept -> Aabb
    {
        Aabb box = spatial_detail::everything();
        if (level == 0)
        {
            return box;
        }
        std::array<float, 3> low{box.minimum.x, box.minimum.y, box.minimum.z};
        std::array<float, 3> high{box.maximum.x, box.maximum.y, box.maximum.z};
        for (std::size_t index = 0; index < Dimensions; ++index)
        {
            const std::size_t axis = AXES[index];
            const float size = cell_size(level, axis);
            const float margin = (Looseness - 1.0F) * size * 0.5F;
            const auto coordinate = static_cast<float>(key >> (20U * index) & 0xFFFFFU);
            low[axis] = component(m_region.minimum, axis) + coordinate * size - margin;
            high[axis] = component(m_region.minimum, axis) + (coordinate + 1.0F) * size + margin;
        }
        return {.minimum = {.x = low[0], .y = low[1], .z = low[2]}, .maximum = {.x = high[0], .y = high[1], .z = high[2]}};
    }

    void link(std::uint32_t slot)
    {
        Proxy& record = m_proxies[slot];
        const auto [level, cell] = placement(record.box);
        record.level = level;
        record.cell = cell;
        record.previous = spatial_detail::NONE;
        record.next = m_cells[level].find(cell);
        if (record.next != spatial_detail::NONE)
        {
            m_proxies[record.next].previous = slot;
        }
        m_cells[level].set(cell, slot);
        ++m_level_counts[level];
    }

    void unlink(std::uint32_t slot)
    {
        Proxy& record = m_proxies[slot];
        if (record.previous != spatial_detail::NONE)
        {
            m_proxies[record.previous].next = record.next;
        }
        else
        {
            m_cells[record.level].set(record.cell, record.next);
        }
        if (record.next != spatial_detail::NONE)
        {
            m_proxies[record.next].previous = record.previous;
        }
        --m_level_counts[record.level];
    }

    /// Visits, until @p visitor returns false, the proxies passing @p item_test in the cells (around @p bounds) passing
    /// @p cell_test.
    template<class CellTest, class ItemTest, class SlotVisitor>
    void visit(const Aabb& bounds,
               CellTest&& cell_test,
               ItemTest&& item_test,
               SlotVisitor&& visitor) const
    {
        const auto list = [&](std::uint32_t head) {
            for (std::uint32_t slot = head; slot != spatial_detail::NONE; slot = m_proxies[slot].next)
            {
                if (item_test(m_proxies[slot].box) && !visitor(slot))
                {
                    return false;
                }
            }
            return true;
        };
        if (m_level_counts[0] > 0 && !list(m_cells[0].find(0)))
        {
            return;
        }
        for (std::uint32_t level = 1; level <= Depth; ++level)
        {
            if (m_level_counts[level] == 0)
            {
                continue;
            }
            // The cells whose grown boxes may overlap the bounds, clamped to the grid.
            std::array<std::int64_t, Dimensions> low{};
            std::array<std::int64_t, Dimensions> high{};
            double cells = 1.0;
            bool empty = false;
            for (std::size_t index = 0; index < Dimensions; ++index)
            {
                const std::size_t axis = AXES[index];
                const double size = cell_size(level, axis);
                const double margin = (static_cast<double>(Looseness) - 1.0) * size * 0.5;
                const double origin = component(m_region.minimum, axis);
                const double last = static_cast<double>((1U << level) - 1);
                const double from = std::floor((static_cast<double>(component(bounds.minimum, axis)) - origin - margin) / size);
                const double to = std::floor((static_cast<double>(component(bounds.maximum, axis)) - origin + margin) / size);
                low[index] = static_cast<std::int64_t>(std::clamp(from, 0.0, last));
                high[index] = static_cast<std::int64_t>(std::clamp(to, 0.0, last));
                empty = empty || to < 0.0 || from > last;
                cells *= static_cast<double>(high[index] - low[index] + 1);
            }
            if (empty)
            {
                continue;
            }
            bool keep_going = true;
            if (cells > static_cast<double>(m_cells[level].size()))
            {
                m_cells[level].for_each([&](std::uint64_t cell, std::uint32_t head) {
                    if (keep_going && cell_test(loose_box(level, cell)))
                    {
                        keep_going = list(head);
                    }
                });
            }
            else
            {
                std::array<std::int64_t, Dimensions> coordinate = low;
                while (keep_going)
                {
                    std::uint64_t cell = 0;
                    for (std::size_t index = 0; index < Dimensions; ++index)
                    {
                        cell |= static_cast<std::uint64_t>(coordinate[index]) << (20U * index);
                    }
                    const std::uint32_t head = m_cells[level].find(cell);
                    if (head != spatial_detail::NONE && cell_test(loose_box(level, cell)))
                    {
                        keep_going = list(head);
                    }
                    // The next coordinate, the first axis fastest.
                    std::size_t index = 0;
                    while (index < Dimensions && ++coordinate[index] > high[index])
                    {
                        coordinate[index] = low[index];
                        ++index;
                    }
                    if (index == Dimensions)
                    {
                        break;
                    }
                }
            }
            if (!keep_going)
            {
                return;
            }
        }
    }

private:
    Aabb m_region{};
    std::array<spatial_detail::CellMap, Depth + 1> m_cells{};
    std::array<std::uint32_t, Depth + 1> m_level_counts{};
    spatial_detail::ProxyStore<Proxy> m_proxies{};
};

/// @brief A loose octree (compile-time depth and looseness).
template<std::uint32_t Depth = 8, float Looseness = 2.0F>
using Octree = LooseTree<3, AxisPlane::XZ, Depth, Looseness>;

/// @brief A loose quadtree over the axes of @p Plane (XZ: flat worlds with +Y up).
template<AxisPlane Plane = AxisPlane::XZ, std::uint32_t Depth = 8, float Looseness = 2.0F>
using Quadtree = LooseTree<2, Plane, Depth, Looseness>;


} // namespace gfx::physics
