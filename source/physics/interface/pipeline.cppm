/// @file pipeline.cppm
/// @brief A world's pipeline: its broad phase (candidate pairs), narrow phase (contacts), solver (constraints) and
///        integrator (time). A backend reports its own — a description whose operations throw (is_external() false) —
///        or runs components of the abstraction (is_external() true), which a backend that allows it lets the
///        application replace (PhysicsWorld::replace_broad_phase() ...). The abstraction's broad phase on any spatial
///        structure and its GJK narrow phase are here; its integrators and its sequential-impulse solver are in
///        gfx.physics.dynamics.
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

    /// @brief Its structure's nodes or cells into @p nodes (cleared first, its storage kept): what a debug view draws
    ///        (SpatialNode). Between steps.
    /// @throws BackendCapabilityError if the world runs the backend's own broad phase (CUSTOM_BROAD_PHASE).
    virtual void structure_nodes(std::vector<SpatialNode>& nodes) const = 0;

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

    void structure_nodes(std::vector<SpatialNode>& nodes) const override
    {
        nodes.clear();
        m_broad_phase.structure().nodes([&nodes](const SpatialNode& node) { nodes.push_back(node); });
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

/// @brief A body as a solver sees it, about its center of mass.
struct SolverBody
{
    Vector3 center{};                                                      ///< Its center of mass, world space.
    Quaternion orientation{};                                              ///< Its orientation.
    Vector3 linear_velocity{};                                             ///< Of its center of mass.
    Vector3 angular_velocity{};                                            ///< Radians per second, world axes.
    float inverse_mass = 0.0F;                                             ///< 0: constraints do not move it (static, kinematic, asleep).
    Matrix3 inverse_inertia{.columns = {Vector3{}, Vector3{}, Vector3{}}}; ///< About its center of mass, world space, 0 about the axes it may not turn about.
    Vector3 linear_freedom{.x = 1.0F, .y = 1.0F, .z = 1.0F};               ///< 0 along the world axes it may not move along.
};

/// @brief The contacts of a pair of colliders as a solver solves them.
struct SolverContact
{
    std::uint32_t first = 0;    ///< The first collider's body, an index into the bodies.
    std::uint32_t second = 0;   ///< The second collider's body.
    ContactManifold manifold{}; ///< From the first to the second; last step's impulses in (warm starting), this step's out.
    Friction friction{};        ///< The pair's coefficients (static, dynamic, rolling resistance in meters).
    float restitution = 0.0F;   ///< The pair's.
    Vector3 rolling_impulse{};  ///< The accumulated rolling-resistance impulse, in and out like the points'.
    float first_margin = 0.0F;  ///< The first shape's margin where it touches (a sphere's or capsule's radius, a rounding):
                                ///< a solver tracks the point on the shape's core, which turns with the body, and the margin
                                ///< along the normal (a point fixed on a spinning sphere's surface would turn away).
    float second_margin = 0.0F; ///< The second shape's.
};

/// @brief A joint or a user constraint as a solver solves it.
struct SolverJoint
{
    std::uint32_t first = 0;                               ///< Its first body, an index into the bodies.
    std::uint32_t second = NO_SOLVER_BODY;                 ///< Its second body, or NO_SOLVER_BODY: the world.
    JointDescription description{};                        ///< A joint's kind and settings (not used for user constraints).
    const UserConstraint* user = nullptr;                  ///< A user constraint (not null), or a joint (null).
    Transform frame_first{};                               ///< A joint's frame, about the first body's center of mass and in its orientation.
    Transform frame_second{};                              ///< About the second body's (the world's for NO_SOLVER_BODY).
    std::array<float, MAXIMUM_CONSTRAINT_ROWS> impulses{}; ///< Accumulated per row: last step's in (warm starting), this step's out.
    Vector3 force{};                                       ///< Out: the force on the second body over the step (world space); for rows that move only the first body, the reaction to the force on it.
    Vector3 torque{};                                      ///< Out: the torque on the second body over the step, about its center of mass (likewise).
};

/// @brief A step of a solver.
struct SolverStep
{
    float seconds = 0.0F;                ///< Its duration, greater than 0.
    SolverDescription settings{};        ///< Iterations, warm starting, thresholds.
    std::span<SolverBody> bodies{};      ///< The bodies, moved by the step.
    std::span<SolverContact> contacts{}; ///< The contacts.
    std::span<SolverJoint> joints{};     ///< The joints and user constraints.
    TaskScheduler* scheduler = nullptr;  ///< Splits the work when given.
};

/// @brief What solves a world's constraints (contacts, joints, user constraints): the abstraction's sequential impulses
///        (gfx.physics.dynamics), or a description of a backend's own (Box3D: SOFT_STEP).
class Solver : public PhysicsObject
{
public:
    /// @brief Its kind.
    [[nodiscard]] virtual auto kind() const noexcept -> SolverKind = 0;

    /// @brief Whether it is a component the world runs (true), or a description of the backend's own (false).
    [[nodiscard]] virtual auto is_external() const noexcept -> bool = 0;

    /// @brief Solves @p step: the velocity constraints of its contacts and joints (warm started from their impulses), the
    ///        bodies moved by their velocities over the step, then their positions corrected. The bodies' velocities
    ///        already include the step's forces. Leaves the accumulated impulses in the contacts and joints, and the
    ///        joints' forces and torques.
    /// @throws BackendCapabilityError by default (CUSTOM_SOLVER).
    virtual void solve(const SolverStep& step) = 0;

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
