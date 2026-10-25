/// @file broad_phase.cppm
/// @brief The broad phase over any spatial structure (SpatialBroadPhase<Structure>): colliders' boxes fattened by a
///        margin and their predicted motion, so a proxy moves in the structure only when its box leaves the fat one;
///        collision filters (64-bit layers, groups, an optional custom filter); and each update(), the candidate pairs —
///        fat boxes overlapping, filters agreeing — sorted by (smaller collider, larger), with the pairs that began and
///        ended since the last update. With a task scheduler, the pair search splits into tasks (each proxy's query of
///        the structure, the pair kept by its smaller collider), each writing its own buffer; the merged buffers sorted
///        give the same pairs whatever the thread count — the structure's own pair search when single-threaded.
export module gfx.physics.spatial:broad_phase;

import std;
import gfx.core.enums;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.handles;
import gfx.physics.primitives;
import gfx.physics.tasks;
import :structure;

export namespace gfx::physics
{

/// @brief What a collider is and what it collides with (Box3D's model).
struct CollisionFilter
{
    LayerMask category = LayerMask::LAYER_0; ///< The layers it belongs to.
    LayerMask mask = ALL_LAYERS;             ///< The layers it collides with.
    std::int32_t group = 0;                  ///< Equal and non-zero: positive always collides, negative never (overrides the layers).

    [[nodiscard]] constexpr auto operator==(const CollisionFilter&) const noexcept -> bool = default;
};

/// @brief Whether colliders with the filters @p first and @p second collide.
[[nodiscard]] constexpr auto
should_collide(const CollisionFilter& first,
               const CollisionFilter& second) noexcept -> bool
{
    if (first.group == second.group && first.group != 0)
    {
        return first.group > 0;
    }
    return has_any(first.category, second.mask) && has_any(second.category, first.mask);
}

/// @brief A candidate pair: two colliders, the smaller handle first.
struct BroadPhasePair
{
    ColliderHandle first;  ///< The smaller handle.
    ColliderHandle second; ///< The larger handle.

    [[nodiscard]] constexpr auto operator<=>(const BroadPhasePair&) const noexcept = default;
};

/// @brief How a broad phase fattens boxes and splits its work.
struct BroadPhaseDescription
{
    float margin = 0.1F;                    ///< Added on every side of a box, at least 0.
    float prediction = 2.0F;                ///< The displacement given to move() stretches the box by this many times it, at least 0.
    std::uint32_t proxies_per_task = 128;   ///< Proxies a task of the parallel pair search queries, at least 1.
};

namespace spatial_detail
{

/// Throws the PhysicsError of an invalid broad phase description.
[[noreturn]] void reject_broad_phase(const BroadPhaseDescription& description);

/// Throws the PhysicsError of a null collider handle.
[[noreturn]] void reject_null_collider(std::string_view operation);

} // namespace spatial_detail

/// @brief A broad phase on @p Structure (any SpatialStructureConcept).
template<SpatialStructureConcept Structure>
class SpatialBroadPhase : private HandleIssuer
{
public:
    /// @brief A broad phase on @p structure (empty) fattening as @p description says.
    /// @throws PhysicsError if @p description is invalid.
    explicit SpatialBroadPhase(Structure structure = Structure{},
                               const BroadPhaseDescription& description = {})
        : m_structure{std::move(structure)}
        , m_description{description}
    {
        if (!(description.margin >= 0.0F) || !std::isfinite(description.margin) || !(description.prediction >= 0.0F) || !std::isfinite(description.prediction)
            || description.proxies_per_task == 0)
        {
            spatial_detail::reject_broad_phase(description);
        }
    }

    /// @brief A proxy for @p collider (not null), its box @p box fattened, filtered by @p filter.
    auto insert(ColliderHandle collider,
                const Aabb& box,
                const CollisionFilter& filter = {}) -> SpatialProxyHandle
    {
        if (!collider.is_valid())
        {
            spatial_detail::reject_null_collider("SpatialBroadPhase::insert");
        }
        const SpatialProxyHandle proxy = m_structure.insert(collider, box.expanded(m_description.margin));
        const std::uint32_t index = collider.index(handle_key());
        if (index >= m_filters.size())
        {
            m_filters.resize(static_cast<std::size_t>(index) + 1);
        }
        m_filters[index] = filter;
        return proxy;
    }

    void remove(SpatialProxyHandle proxy)
    {
        m_structure.remove(proxy);
    }

    /// @brief The collider of @p proxy now has the box @p box after moving by @p displacement this step.
    /// @return Whether the proxy moved in the structure (its box left its fat box).
    auto move(SpatialProxyHandle proxy,
              const Aabb& box,
              Vector3 displacement = {}) -> bool
    {
        if (m_structure.box(proxy).contains(box))
        {
            return false;
        }
        // The new fat box: the margin, and the predicted motion on the side it goes to.
        Aabb fat = box.expanded(m_description.margin);
        const Vector3 ahead = displacement * m_description.prediction;
        fat.minimum += component_minimum(ahead, {});
        fat.maximum += component_maximum(ahead, {});
        m_structure.update(proxy, fat);
        return true;
    }

    /// @brief Gives the collider of @p proxy the filter @p filter.
    void set_filter(SpatialProxyHandle proxy,
                    const CollisionFilter& filter)
    {
        m_filters[m_structure.key(proxy).index(handle_key())] = filter;
    }

    /// @brief A test every candidate pair must also pass (called from several threads at once with a scheduler).
    void set_custom_filter(std::function<bool(ColliderHandle, ColliderHandle)> filter)
    {
        m_custom_filter = std::move(filter);
    }

    /// @brief The fat box of @p proxy (what the structure holds).
    [[nodiscard]] auto fat_box(SpatialProxyHandle proxy) const -> Aabb
    {
        return m_structure.box(proxy);
    }

    /// @brief Finds the pairs (on @p scheduler's threads when given), and those that began and ended since last time.
    void update(TaskScheduler* scheduler = nullptr)
    {
        std::swap(m_pairs, m_previous);
        m_pairs.clear();
        if (scheduler == nullptr || scheduler->concurrency() <= 1)
        {
            m_structure.pairs([this](ColliderHandle first, ColliderHandle second) {
                if (accepts(first, second))
                {
                    m_pairs.push_back(first < second ? BroadPhasePair{first, second} : BroadPhasePair{second, first});
                }
            });
        }
        else
        {
            // The proxies, then one task per range of them: each proxy's query, its pairs with larger colliders.
            m_items.clear();
            m_structure.iterate([this](ColliderHandle key, const Aabb& box) { m_items.push_back({key, box}); });
            const auto tasks = static_cast<std::uint32_t>((m_items.size() + m_description.proxies_per_task - 1) / m_description.proxies_per_task);
            if (m_buffers.size() < tasks)
            {
                m_buffers.resize(tasks);
            }
            scheduler->parallel_for(tasks, 1, [this](std::uint32_t begin, std::uint32_t end) {
                for (std::uint32_t task = begin; task < end; ++task)
                {
                    std::vector<BroadPhasePair>& buffer = m_buffers[task];
                    buffer.clear();
                    const std::size_t first_item = static_cast<std::size_t>(task) * m_description.proxies_per_task;
                    const std::size_t last_item = std::min(m_items.size(), first_item + m_description.proxies_per_task);
                    for (std::size_t item = first_item; item < last_item; ++item)
                    {
                        const ColliderHandle own = m_items[item].first;
                        m_structure.query(m_items[item].second, [&](ColliderHandle other) {
                            if (own < other && accepts(own, other))
                            {
                                buffer.push_back({own, other});
                            }
                            return true;
                        });
                    }
                }
            });
            for (std::uint32_t task = 0; task < tasks; ++task)
            {
                m_pairs.insert(m_pairs.end(), m_buffers[task].begin(), m_buffers[task].end());
            }
        }
        std::ranges::sort(m_pairs);
        m_pairs.erase(std::ranges::unique(m_pairs).begin(), m_pairs.end());

        // What began and what ended.
        m_new.clear();
        m_ended.clear();
        std::ranges::set_difference(m_pairs, m_previous, std::back_inserter(m_new));
        std::ranges::set_difference(m_previous, m_pairs, std::back_inserter(m_ended));
    }

    /// @brief The pairs of the last update(), sorted.
    [[nodiscard]] auto pairs() const noexcept -> std::span<const BroadPhasePair>
    {
        return m_pairs;
    }

    /// @brief The pairs of the last update() that the one before had not, sorted.
    [[nodiscard]] auto new_pairs() const noexcept -> std::span<const BroadPhasePair>
    {
        return m_new;
    }

    /// @brief The pairs of the update() before the last that the last has not, sorted.
    [[nodiscard]] auto ended_pairs() const noexcept -> std::span<const BroadPhasePair>
    {
        return m_ended;
    }

    [[nodiscard]] auto structure() const noexcept -> const Structure&
    {
        return m_structure;
    }

    [[nodiscard]] auto size() const noexcept -> std::uint32_t
    {
        return m_structure.size();
    }

private:
    [[nodiscard]] auto accepts(ColliderHandle first,
                               ColliderHandle second) const -> bool
    {
        return should_collide(m_filters[first.index(handle_key())], m_filters[second.index(handle_key())]) && (!m_custom_filter || m_custom_filter(first, second));
    }

private:
    Structure m_structure;
    BroadPhaseDescription m_description{};
    std::vector<CollisionFilter> m_filters{};
    std::function<bool(ColliderHandle, ColliderHandle)> m_custom_filter{};
    std::vector<BroadPhasePair> m_pairs{};
    std::vector<BroadPhasePair> m_previous{};
    std::vector<BroadPhasePair> m_new{};
    std::vector<BroadPhasePair> m_ended{};
    std::vector<std::pair<ColliderHandle, Aabb>> m_items{};
    std::vector<std::vector<BroadPhasePair>> m_buffers{};
};

} // namespace gfx::physics
