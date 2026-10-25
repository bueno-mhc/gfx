// ManifoldCache: linear probing over a power-of-two table; end_step() keeps the entries of the step in a second
// vector and puts them back (no tombstones), so neither updates nor steps allocate once the table is large enough.
module gfx.physics.narrow_phase;

import std;
import gfx.physics.enums;
import gfx.physics.handles;

namespace gfx::physics
{

namespace
{

/// A 64-bit mix (splitmix64's finalizer).
[[nodiscard]] auto
mix(std::uint64_t value) noexcept -> std::uint64_t
{
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

} // namespace

/// The table: entries, the kept entries' scratch, the count and the step.
struct ManifoldCache::State
{
    struct Entry
    {
        ManifoldKey key{};
        ContactManifold manifold{};
        std::uint32_t step = 0;
        bool used = false;
    };

    std::vector<Entry> entries{};
    std::vector<Entry> kept{};
    std::size_t count = 0;
    std::uint32_t step = 1;

    [[nodiscard]] auto slot_of(const ManifoldKey& key) const noexcept -> std::size_t
    {
        // The handles' and sub-shapes' bits mixed, then the first slot that holds the key or is free.
        const std::uint64_t handles = static_cast<std::uint64_t>(std::hash<ColliderHandle>{}(key.first)) << 32U ^ std::hash<ColliderHandle>{}(key.second);
        const std::uint64_t subs = static_cast<std::uint64_t>(key.sub_shape_first) << 32U | key.sub_shape_second;
        const std::size_t mask = entries.size() - 1;
        std::size_t slot = static_cast<std::size_t>(mix(handles ^ mix(subs))) & mask;
        while (entries[slot].used && !(entries[slot].key == key))
        {
            slot = (slot + 1) & mask;
        }
        return slot;
    }

    void insert(const Entry& entry) noexcept
    {
        Entry& slot = entries[slot_of(entry.key)];
        count += slot.used ? 0 : 1;
        slot = entry;
        slot.used = true;
    }

    void grow()
    {
        // Twice the slots; the entries put back.
        kept.clear();
        for (const Entry& entry : entries)
        {
            if (entry.used)
            {
                kept.push_back(entry);
            }
        }
        entries.assign(entries.size() * 2, Entry{});
        count = 0;
        for (const Entry& entry : kept)
        {
            insert(entry);
        }
    }
};

ManifoldCache::ManifoldCache(std::size_t capacity)
    : m_state{std::make_unique<State>()}
{
    m_state->entries.resize(std::bit_ceil(std::max<std::size_t>(capacity, 16)));
}

ManifoldCache::~ManifoldCache() = default;
ManifoldCache::ManifoldCache(ManifoldCache&&) noexcept = default;
auto ManifoldCache::operator=(ManifoldCache&&) noexcept -> ManifoldCache& = default;

auto
ManifoldCache::update(const ManifoldKey& key,
                      ContactManifold& manifold) -> std::uint32_t
{
    // Last step's manifold of the key gives its impulses to the points with the same features.
    std::uint32_t matched = 0;
    State::Entry& existing = m_state->entries[m_state->slot_of(key)];
    if (existing.used)
    {
        for (ContactPoint& point : std::span{manifold.points}.first(manifold.point_count))
        {
            for (const ContactPoint& previous : existing.manifold.contacts())
            {
                if (previous.feature == point.feature)
                {
                    point.normal_impulse = previous.normal_impulse;
                    point.tangent_impulse = previous.tangent_impulse;
                    ++matched;
                    break;
                }
            }
        }
        existing.manifold = manifold;
        existing.step = m_state->step;
        return matched;
    }

    // A new key: kept (the table doubles past half full).
    if ((m_state->count + 1) * 2 > m_state->entries.size())
    {
        m_state->grow();
    }
    m_state->insert({.key = key, .manifold = manifold, .step = m_state->step, .used = true});
    return matched;
}

void
ManifoldCache::store(const ManifoldKey& key,
                     const ContactManifold& manifold)
{
    State::Entry& existing = m_state->entries[m_state->slot_of(key)];
    if (existing.used)
    {
        existing.manifold = manifold;
        existing.step = m_state->step;
        return;
    }
    if ((m_state->count + 1) * 2 > m_state->entries.size())
    {
        m_state->grow();
    }
    m_state->insert({.key = key, .manifold = manifold, .step = m_state->step, .used = true});
}

auto
ManifoldCache::find(const ManifoldKey& key) const noexcept -> const ContactManifold*
{
    const State::Entry& entry = m_state->entries[m_state->slot_of(key)];
    return entry.used ? &entry.manifold : nullptr;
}

void
ManifoldCache::end_step()
{
    // The entries of this step, put back into a cleared table (linear probing needs no tombstones then).
    State& state = *m_state;
    state.kept.clear();
    for (const State::Entry& entry : state.entries)
    {
        if (entry.used && entry.step == state.step)
        {
            state.kept.push_back(entry);
        }
    }
    for (State::Entry& entry : state.entries)
    {
        entry.used = false;
    }
    state.count = 0;
    for (const State::Entry& entry : state.kept)
    {
        state.insert(entry);
    }
    ++state.step;
}

auto
ManifoldCache::size() const noexcept -> std::size_t
{
    return m_state->count;
}

void
ManifoldCache::clear() noexcept
{
    for (State::Entry& entry : m_state->entries)
    {
        entry.used = false;
    }
    m_state->count = 0;
}

} // namespace gfx::physics