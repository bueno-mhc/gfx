/// @file manifold_cache.cppm
/// @brief Last step's manifolds, by pair of colliders and sub-shapes, so that the solver's accumulated impulses carry
///        over to the points that come back with the same feature ids (each step: update() each fresh manifold, solve,
///        store() the solved ones, end_step()) (warm starting: a box resting on the ground
///        keeps its four contacts and their impulses step after step). An open-addressing table that grows only when
///        more than half full; manifolds not updated during a step are forgotten at its end. One thread uses a cache
///        at a time (the narrow phase may run in parallel; the cache is updated after it, in order).
export module gfx.physics.narrow_phase:manifold_cache;

import std;
import gfx.physics.enums;
import gfx.physics.handles;
import :contact;

export namespace gfx::physics
{

/// @brief What a cached manifold belongs to.
struct ManifoldKey
{
    ColliderHandle first{};                        ///< The first collider.
    ColliderHandle second{};                       ///< The second collider.
    std::uint32_t sub_shape_first = NO_SUB_SHAPE;  ///< The manifold's sub-shape of the first.
    std::uint32_t sub_shape_second = NO_SUB_SHAPE; ///< The manifold's sub-shape of the second.

    [[nodiscard]] constexpr auto operator==(const ManifoldKey&) const noexcept -> bool = default;
};

/// @brief Manifolds kept from one step to the next.
class ManifoldCache
{
public:
    /// @param capacity Manifolds it holds before growing (rounded up to a power of two, at least 16).
    explicit ManifoldCache(std::size_t capacity = 256);
    ~ManifoldCache();
    ManifoldCache(ManifoldCache&&) noexcept;
    auto operator=(ManifoldCache&&) noexcept -> ManifoldCache&;

    /// @brief Gives @p manifold's points the impulses of the points of last step's manifold of @p key that have their
    ///        feature ids, and keeps @p manifold for the next step.
    /// @return How many points got impulses.
    auto update(const ManifoldKey& key,
                ContactManifold& manifold) -> std::uint32_t;

    /// @brief Keeps @p manifold for @p key as it is (after the solver: its accumulated impulses for the next step).
    void store(const ManifoldKey& key,
               const ContactManifold& manifold);

    /// @brief The manifold kept for @p key, or null.
    [[nodiscard]] auto find(const ManifoldKey& key) const noexcept -> const ContactManifold*;

    /// @brief Ends a step: forgets the manifolds no update() touched since the previous end_step().
    void end_step();

    /// @brief Manifolds kept.
    [[nodiscard]] auto size() const noexcept -> std::size_t;

    /// @brief Forgets everything (the storage stays).
    void clear() noexcept;

private:
    struct State;

private:
    std::unique_ptr<State> m_state;
};
} // namespace gfx::physics
