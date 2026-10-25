/// @file pipeline.cppm
/// @brief A world's pipeline: its broad phase (candidate pairs), narrow phase (contacts), solver (constraints) and
///        integrator (time). A backend reports its own — a description whose operations throw (is_external() false) —
///        or runs components of the abstraction (is_external() true), which a backend that allows it lets the
///        application replace (PhysicsWorld::replace_broad_phase() ...). The abstraction's broad phase on any spatial
///        structure and its GJK narrow phase are here; its solver and integrators come with the soft bodies and the
///        reference backend.
export module gfx.physics.interface:pipeline;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.primitives;
import gfx.physics.shapes;
import gfx.physics.narrow_phase;
import gfx.physics.spatial;
import gfx.physics.tasks;
import :descriptions;
import :object;

export namespace gfx::physics
{

// ===================================================================================================== broad phase

/// @brief What finds the candidate pairs of a world.
class BroadPhase : public PhysicsObject
{
public:
    /// @brief Its kind.
    [[nodiscard]] virtual auto kind() const noexcept -> BroadPhaseKind = 0;

    /// @brief Whether it is a component the world runs (true), or a description of the backend's own (false: its
    ///        operations throw).
    [[nodiscard]] virtual auto is_external() const noexcept -> bool = 0;

    /// @brief A proxy for @p collider with the box @p box, filtered by @p filter.
    virtual auto insert(ColliderHandle collider,
                        const Aabb& box,
                        const CollisionFilter& filter) -> SpatialProxyHandle = 0;

    /// @brief Forgets @p proxy.
    virtual void remove(SpatialProxyHandle proxy) = 0;

    /// @brief The collider of @p proxy now has the box @p box after moving by @p displacement.
    /// @return Whether the proxy moved in the structure.
    virtual auto move(SpatialProxyHandle proxy,
                      const Aabb& box,
                      Vector3 displacement) -> bool = 0;

    /// @brief Gives the collider of @p proxy the filter @p filter.
    virtual void set_filter(SpatialProxyHandle proxy,
                            const CollisionFilter& filter) = 0;

    /// @brief Finds the pairs (on @p scheduler's threads when given).
    virtual void update(TaskScheduler* scheduler) = 0;

    /// @brief The pairs of the last update, sorted.
    [[nodiscard]] virtual auto pairs() const -> std::span<const BroadPhasePair> = 0;

    /// @brief The pairs that began at the last update, sorted.
    [[nodiscard]] virtual auto new_pairs() const -> std::span<const BroadPhasePair> = 0;

    /// @brief The pairs that ended at the last update, sorted.
    [[nodiscard]] virtual auto ended_pairs() const -> std::span<const BroadPhasePair> = 0;

protected:
    using PhysicsObject::PhysicsObject;
};

/// @brief The broad phase kind of a spatial broad phase on a structure of @p kind.
[[nodiscard]] constexpr auto
broad_phase_kind(SpatialStructureKind kind) noexcept -> BroadPhaseKind
{
    switch (kind)
    {
        case SpatialStructureKind::BVH:
            return BroadPhaseKind::BVH;
        case SpatialStructureKind::OCTREE:
            return BroadPhaseKind::OCTREE;
        case SpatialStructureKind::QUADTREE:
            return BroadPhaseKind::QUADTREE;
        case SpatialStructureKind::BSP:
            return BroadPhaseKind::BSP;
        case SpatialStructureKind::UNIFORM_GRID:
            return BroadPhaseKind::UNIFORM_GRID;
        case SpatialStructureKind::SWEEP_AND_PRUNE:
            return BroadPhaseKind::SWEEP_AND_PRUNE;
    }
    return BroadPhaseKind::BACKEND;
}

/// @brief The abstraction's broad phase on @p Structure, as a component.
template<SpatialStructureConcept Structure>
class SpatialBroadPhaseComponent final : public BroadPhase
{
public:
    /// @param structure   The structure (empty).
    /// @param description How it fattens boxes and splits its work.
    /// @throws PhysicsError if @p description is invalid.
    explicit SpatialBroadPhaseComponent(Structure structure = Structure{},
                                        const BroadPhaseDescription& description = {})
        : BroadPhase{PhysicsSubsystem::SPATIAL}
        , m_broad_phase{std::move(structure), description}
    {
    }

    /// @brief The broad phase itself.
    [[nodiscard]] auto broad_phase() noexcept -> SpatialBroadPhase<Structure>&
    {
        return m_broad_phase;
    }

    [[nodiscard]] auto kind() const noexcept -> BroadPhaseKind override
    {
        return broad_phase_kind(Structure::KIND);
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return true;
    }

    auto insert(ColliderHandle collider,
                const Aabb& box,
                const CollisionFilter& filter) -> SpatialProxyHandle override
    {
        return m_broad_phase.insert(collider, box, filter);
    }

    void remove(SpatialProxyHandle proxy) override
    {
        m_broad_phase.remove(proxy);
    }

    auto move(SpatialProxyHandle proxy,
              const Aabb& box,
              Vector3 displacement) -> bool override
    {
        return m_broad_phase.move(proxy, box, displacement);
    }

    void set_filter(SpatialProxyHandle proxy,
                    const CollisionFilter& filter) override
    {
        m_broad_phase.set_filter(proxy, filter);
    }

    void update(TaskScheduler* scheduler) override
    {
        m_broad_phase.update(scheduler);
    }

    [[nodiscard]] auto pairs() const -> std::span<const BroadPhasePair> override
    {
        return m_broad_phase.pairs();
    }

    [[nodiscard]] auto new_pairs() const -> std::span<const BroadPhasePair> override
    {
        return m_broad_phase.new_pairs();
    }

    [[nodiscard]] auto ended_pairs() const -> std::span<const BroadPhasePair> override
    {
        return m_broad_phase.ended_pairs();
    }

private:
    SpatialBroadPhase<Structure> m_broad_phase;
};

// ===================================================================================================== narrow phase

/// @brief What computes the contacts of candidate pairs.
class NarrowPhase : public PhysicsObject
{
public:
    /// @brief Its kind.
    [[nodiscard]] virtual auto kind() const noexcept -> NarrowPhaseKind = 0;

    /// @brief Whether it is a component the world runs (true), or a description of the backend's own (false).
    [[nodiscard]] virtual auto is_external() const noexcept -> bool = 0;

    /// @brief The contact manifolds of @p first and @p second into @p contacts (cleared first); pure: threads collide
    ///        different pairs at once, each with its own collector.
    virtual void collide(const ShapeInstance& first,
                         const ShapeInstance& second,
                         ContactCollector& contacts,
                         const CollisionSettings& settings) const = 0;

protected:
    using PhysicsObject::PhysicsObject;
};

/// @brief The abstraction's narrow phase (GJK, EPA, clipping, analytic pairs: gfx::physics::collide()), as a component.
class GjkNarrowPhase final : public NarrowPhase
{
public:
    GjkNarrowPhase() noexcept
        : NarrowPhase{PhysicsSubsystem::NARROW_PHASE}
    {
    }

    [[nodiscard]] auto kind() const noexcept -> NarrowPhaseKind override
    {
        return NarrowPhaseKind::GJK_EPA;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return true;
    }

    void collide(const ShapeInstance& first,
                 const ShapeInstance& second,
                 ContactCollector& contacts,
                 const CollisionSettings& settings) const override
    {
        gfx::physics::collide(first, second, contacts, settings);
    }
};

// ===================================================================================================== solver

/// @brief What solves a world's constraints (contacts, joints, attachments). Its operations come with the reference
///        backend; a backend reports its own (Box3D: SOFT_STEP).
class Solver : public PhysicsObject
{
public:
    /// @brief Its kind.
    [[nodiscard]] virtual auto kind() const noexcept -> SolverKind = 0;

    /// @brief Whether it is a component the world runs (true), or a description of the backend's own (false).
    [[nodiscard]] virtual auto is_external() const noexcept -> bool = 0;

protected:
    using PhysicsObject::PhysicsObject;
};

// ===================================================================================================== integrator

/// @brief What an integrator needs from the states it advances.
struct IntegratorRequirements
{
    bool previous_positions = false;     ///< IntegrationState::previous_positions must be given (Verlet).
    std::uint32_t force_evaluations = 1; ///< Force evaluations per step (Runge–Kutta 4: 4, through the force field).

    [[nodiscard]] constexpr auto operator==(const IntegratorRequirements&) const noexcept -> bool = default;
};

/// @brief Forces at given positions and velocities (written into the last span), for integrators that evaluate them
///        several times per step.
using ForceField = std::function<void(std::span<const Vector3> positions, std::span<const Vector3> velocities, std::span<Vector3> forces)>;

/// @brief States an integrator advances: particles or bodies, as structures of arrays of one length.
struct IntegrationState
{
    std::span<Vector3> positions{};          ///< Advanced in place.
    std::span<Vector3> velocities{};         ///< Advanced in place.
    std::span<const float> inverse_masses{}; ///< 0: does not move.
    std::span<const Vector3> forces{};       ///< Accumulated forces (besides gravity), or empty.
    std::span<Vector3> previous_positions{}; ///< When requirements() asks for them.
    Vector3 gravity{};                       ///< Acceleration of every state with a non-zero inverse mass.
    const ForceField* force_field = nullptr; ///< When requirements() asks for more than one force evaluation.
};

/// @brief What advances states in time.
class Integrator : public PhysicsObject
{
public:
    /// @brief Its kind.
    [[nodiscard]] virtual auto kind() const noexcept -> IntegratorKind = 0;

    /// @brief Whether it is a component the world runs (true), or a description of the backend's own (false: the
    ///        backend integrates its rigid bodies itself).
    [[nodiscard]] virtual auto is_external() const noexcept -> bool = 0;

    /// @brief What it needs from the states.
    [[nodiscard]] virtual auto requirements() const noexcept -> IntegratorRequirements = 0;

    /// @brief Advances @p state by @p seconds (explicit schemes), or predicts its positions (XPBD: before projection).
    virtual void predict(const IntegrationState& state,
                         float seconds) = 0;

    /// @brief Ends a step of @p seconds (XPBD: velocities from the projected positions; nothing for explicit schemes).
    virtual void finalize(const IntegrationState& state,
                          float seconds) = 0;

protected:
    using PhysicsObject::PhysicsObject;
};

} // namespace gfx::physics
