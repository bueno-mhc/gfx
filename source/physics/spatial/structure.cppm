/// @file structure.cppm
/// @brief What every spatial structure shares: the concept they satisfy (SpatialStructureConcept), the storage of their
///        proxies (slots that issue SpatialProxyHandle values and refuse stale ones), the open-addressing map from cells
///        to their lists (grids, loose trees), and the query helpers (the box a query shape fits in, the exact test).
///
/// A structure stores, per proxy, a key (the ColliderHandle it stands for) and a box, and answers queries by box, ray,
/// segment, sphere and frustum (visitors return false to stop), the pairs of proxies whose boxes overlap (each once),
/// and iteration; it shows its nodes or cells (SpatialNode) to debug views. It stores the boxes it is given: fattening is the broad phase's (SpatialBroadPhase). Queries are
/// const and may run on several threads at once while nothing changes the structure.
export module gfx.physics.spatial:structure;

import std;
import gfx.core.enums;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.handles;
import gfx.physics.primitives;

export namespace gfx::physics
{

namespace spatial_detail
{

/// No slot, no cell, no node.
inline constexpr std::uint32_t NONE = 0xFFFFFFFFU;

/// Throws the PhysicsError of a stale or foreign proxy handle.
[[noreturn]] void reject_proxy(std::string_view operation,
                               SpatialProxyHandle proxy);

/// Throws the PhysicsError of a loose tree's region without extent.
[[noreturn]] void reject_loose_region(const Aabb& region);

/// Proxies' slots: the payload of each live proxy, its generation, and the free slots (last freed first).
template<class Payload>
class ProxyStore : private HandleIssuer
{
public:
    /// A new proxy holding @p payload: its handle and slot.
    auto add(const Payload& payload) -> std::pair<SpatialProxyHandle, std::uint32_t>
    {
        std::uint32_t slot = NONE;
        if (!m_free.empty())
        {
            slot = m_free.back();
            m_free.pop_back();
            m_entries[slot].payload = payload;
            m_entries[slot].alive = true;
        }
        else
        {
            slot = static_cast<std::uint32_t>(m_entries.size());
            m_entries.push_back({.payload = payload, .generation = 1, .alive = true});
        }
        ++m_size;
        return {handle_of(slot), slot};
    }

    /// The slot of the live proxy @p proxy.
    /// @throws PhysicsError if it is null, removed or from another structure's past.
    [[nodiscard]] auto slot_of(SpatialProxyHandle proxy,
                               std::string_view operation) const -> std::uint32_t
    {
        const std::uint32_t slot = proxy.index(handle_key());
        if (!proxy.is_valid() || slot >= m_entries.size() || !m_entries[slot].alive || m_entries[slot].generation != proxy.generation(handle_key()))
        {
            reject_proxy(operation, proxy);
        }
        return slot;
    }

    /// Frees @p slot (its next handle gets the next generation).
    void release(std::uint32_t slot)
    {
        Entry& entry = m_entries[slot];
        entry.alive = false;
        entry.generation = entry.generation >= MAXIMUM_HANDLE_GENERATION ? 1 : entry.generation + 1;
        m_free.push_back(slot);
        --m_size;
    }

    [[nodiscard]] auto handle_of(std::uint32_t slot) const -> SpatialProxyHandle
    {
        return SpatialProxyHandle::create(handle_key(), slot, m_entries[slot].generation);
    }

    [[nodiscard]] auto operator[](std::uint32_t slot) noexcept -> Payload&
    {
        return m_entries[slot].payload;
    }

    [[nodiscard]] auto operator[](std::uint32_t slot) const noexcept -> const Payload&
    {
        return m_entries[slot].payload;
    }

    /// Whether @p slot holds a live proxy.
    [[nodiscard]] auto alive(std::uint32_t slot) const noexcept -> bool
    {
        return slot < m_entries.size() && m_entries[slot].alive;
    }

    /// Slots in use or freed (live ones are below it).
    [[nodiscard]] auto slot_count() const noexcept -> std::uint32_t
    {
        return static_cast<std::uint32_t>(m_entries.size());
    }

    /// Live proxies.
    [[nodiscard]] auto size() const noexcept -> std::uint32_t
    {
        return m_size;
    }

    /// Forgets every proxy (their handles become stale).
    void clear()
    {
        for (std::uint32_t slot = 0; slot < m_entries.size(); ++slot)
        {
            if (m_entries[slot].alive)
            {
                release(slot);
            }
        }
    }

private:
    struct Entry
    {
        Payload payload{};
        std::uint32_t generation = 1;
        bool alive = false;
    };

    std::vector<Entry> m_entries{};
    std::vector<std::uint32_t> m_free{};
    std::uint32_t m_size = 0;
};

/// An open-addressing map from 64-bit cell keys to a 32-bit value (the head of the cell's list), with removal by
/// backward shifting (no tombstones); it grows past half full.
class CellMap
{
public:
    /// The value of @p key, or NONE.
    [[nodiscard]] auto find(std::uint64_t key) const noexcept -> std::uint32_t
    {
        if (m_slots.empty())
        {
            return NONE;
        }
        for (std::size_t slot = home(key);; slot = (slot + 1) & (m_slots.size() - 1))
        {
            if (m_slots[slot].value == NONE)
            {
                return NONE;
            }
            if (m_slots[slot].key == key)
            {
                return m_slots[slot].value;
            }
        }
    }

    /// Sets the value of @p key (NONE removes it).
    void set(std::uint64_t key,
             std::uint32_t value)
    {
        if (value == NONE)
        {
            remove(key);
            return;
        }
        if ((m_count + 1) * 2 > m_slots.size())
        {
            grow();
        }
        std::size_t slot = home(key);
        while (m_slots[slot].value != NONE && m_slots[slot].key != key)
        {
            slot = (slot + 1) & (m_slots.size() - 1);
        }
        m_count += m_slots[slot].value == NONE ? 1U : 0U;
        m_slots[slot] = {.key = key, .value = value};
    }

    /// Calls `visitor(key, value)` for every entry (in table order).
    template<class Visitor>
    void for_each(Visitor&& visitor) const
    {
        for (const Slot& slot : m_slots)
        {
            if (slot.value != NONE)
            {
                visitor(slot.key, slot.value);
            }
        }
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t
    {
        return m_count;
    }

    void clear() noexcept
    {
        for (Slot& slot : m_slots)
        {
            slot.value = NONE;
        }
        m_count = 0;
    }

private:
    struct Slot
    {
        std::uint64_t key = 0;
        std::uint32_t value = NONE;
    };

    [[nodiscard]] auto home(std::uint64_t key) const noexcept -> std::size_t
    {
        // splitmix64's finalizer.
        key ^= key >> 30U;
        key *= 0xBF58476D1CE4E5B9ULL;
        key ^= key >> 27U;
        key *= 0x94D049BB133111EBULL;
        key ^= key >> 31U;
        return static_cast<std::size_t>(key) & (m_slots.size() - 1);
    }

    void remove(std::uint64_t key) noexcept
    {
        if (m_slots.empty())
        {
            return;
        }
        std::size_t slot = home(key);
        while (m_slots[slot].value != NONE && m_slots[slot].key != key)
        {
            slot = (slot + 1) & (m_slots.size() - 1);
        }
        if (m_slots[slot].value == NONE)
        {
            return;
        }
        // Backward shift: later entries of the run move up when their home allows it.
        const std::size_t mask = m_slots.size() - 1;
        std::size_t hole = slot;
        for (std::size_t next = (hole + 1) & mask; m_slots[next].value != NONE; next = (next + 1) & mask)
        {
            const std::size_t wanted = home(m_slots[next].key);
            const bool movable = hole <= next ? (wanted <= hole || wanted > next) : (wanted <= hole && wanted > next);
            if (movable)
            {
                m_slots[hole] = m_slots[next];
                hole = next;
            }
        }
        m_slots[hole].value = NONE;
        --m_count;
    }

    void grow()
    {
        std::vector<Slot> old = std::move(m_slots);
        m_slots.assign(std::max<std::size_t>(16, old.size() * 2), Slot{});
        m_count = 0;
        for (const Slot& slot : old)
        {
            if (slot.value != NONE)
            {
                set(slot.key, slot.value);
            }
        }
    }

    std::vector<Slot> m_slots{};
    std::size_t m_count = 0;
};

/// The box a ray from its origin to @p maximum fits in (everything for an unbounded ray).
[[nodiscard]] constexpr auto
bounds_of(const Ray& ray,
          float maximum) noexcept -> Aabb
{
    if (!(maximum < std::numeric_limits<float>::max()))
    {
        constexpr float LARGEST = std::numeric_limits<float>::max();
        return {.minimum = {.x = -LARGEST, .y = -LARGEST, .z = -LARGEST}, .maximum = {.x = LARGEST, .y = LARGEST, .z = LARGEST}};
    }
    const Vector3 end = ray.at(maximum);
    return {.minimum = component_minimum(ray.origin, end), .maximum = component_maximum(ray.origin, end)};
}

/// Everything (the box of a frustum whose region is not bounded by its planes' box test).
[[nodiscard]] constexpr auto
everything() noexcept -> Aabb
{
    constexpr float LARGEST = std::numeric_limits<float>::max();
    return {.minimum = {.x = -LARGEST, .y = -LARGEST, .z = -LARGEST}, .maximum = {.x = LARGEST, .y = LARGEST, .z = LARGEST}};
}

} // namespace spatial_detail

/// @brief A node or a cell of a spatial structure, as a debug view draws it: a tree's node, a loose tree's or a grid's
///        occupied cell, a sweep-and-prune proxy's intervals.
struct SpatialNode
{
    Aabb box{};                ///< Its bounds (a cell's own box, not the larger one a loose tree tests).
    std::uint32_t depth = 0;   ///< Its level: 0 at the root (trees), the cell level (loose trees), 0 elsewhere.
    std::uint32_t proxies = 0; ///< The proxies listed in it (0 for a tree's inner node).

    [[nodiscard]] constexpr auto operator==(const SpatialNode&) const noexcept -> bool = default;
};

/// @brief A spatial structure: proxies (a collider key and a box) inserted, removed and updated, queries by box, ray,
///        segment, sphere and frustum, overlapping pairs, iteration, its nodes.
template<class Structure>
concept SpatialStructureConcept = requires(Structure& structure, const Structure& reader, ColliderHandle key, Aabb box, Ray ray, Segment segment,
                                           Sphere sphere, Frustum frustum, SpatialProxyHandle proxy) {
    { Structure::KIND } -> std::convertible_to<SpatialStructureKind>;
    { structure.insert(key, box) } -> std::same_as<SpatialProxyHandle>;
    structure.remove(proxy);
    { structure.update(proxy, box) } -> std::same_as<bool>;
    { reader.key(proxy) } -> std::same_as<ColliderHandle>;
    { reader.box(proxy) } -> std::same_as<Aabb>;
    { reader.size() } -> std::same_as<std::uint32_t>;
    reader.query(box, [](ColliderHandle) { return true; });
    reader.query(ray, 1.0F, [](ColliderHandle, float) { return true; });
    reader.query(segment, [](ColliderHandle) { return true; });
    reader.query(sphere, [](ColliderHandle) { return true; });
    reader.query(frustum, [](ColliderHandle) { return true; });
    reader.pairs([](ColliderHandle, ColliderHandle) {});
    reader.iterate([](ColliderHandle, const Aabb&) {});
    reader.nodes([](const SpatialNode&) {});
};

} // namespace gfx::physics
