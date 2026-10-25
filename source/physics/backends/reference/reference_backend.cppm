// The reference backend's internal partition: ReferenceBackend, a SteppedWorldBackend made only of the abstraction's
// components (a broad phase, a narrow phase, a solver, an integrator, each replaceable between steps), with what it keeps
// of every object by slot (forces to apply, the broad phase's proxies, joints' frames and impulses, triggers' overlaps)
// and of every touching pair (its manifolds, for events, islands and the bodies' contacts).
module gfx.physics.backends.reference:backend;

import std;
import gfx.core.errors;
import gfx.core.handles;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.narrow_phase;
import gfx.physics.spatial;
import gfx.physics.dynamics;
import gfx.physics.world;

namespace gfx::physics::reference_backend
{

/// What the backend keeps of a body.
struct BodyEntry
{
    RigidBodyHandle handle{};
    Vector3 force{};           ///< Accumulated for the next step (world space, through the center of mass).
    Vector3 torque{};          ///< Accumulated for the next step.
    Vector3 linear_impulse{};  ///< To apply at the next step.
    Vector3 angular_impulse{}; ///< To apply at the next step.
    float sleep_time = 0.0F;   ///< Seconds under the sleep thresholds.
    bool woken = false;        ///< An edit woke it.
};

/// What the backend keeps of a collider.
struct ColliderEntry
{
    ColliderHandle handle{};
    RigidBodyHandle body{};
    SpatialProxyHandle proxy{}; ///< In the broad phase.
    Aabb box = Aabb::empty();   ///< Its world box this step.
};

/// What the backend keeps of a joint or a user constraint.
struct JointEntry
{
    Transform frame_first{};                               ///< In the first body's frame (about its origin).
    Transform frame_second{};                              ///< In the second's (the world's when none).
    std::array<float, MAXIMUM_CONSTRAINT_ROWS> impulses{}; ///< Last step's, per row.
};

/// What the backend keeps of a trigger.
struct TriggerEntry
{
    TriggerHandle handle{};
    std::vector<ColliderHandle> overlaps{}; ///< Last step's, sorted.
};

/// A manifold of a pair of colliders.
struct PairManifold
{
    BroadPhasePair pair{};
    ContactManifold manifold{};  ///< From pair.first to pair.second.
    float approach_speed = 0.0F; ///< How fast its deepest point closed before the solve (HIT events).
};

/// A solver contact's points in its bodies' frames (about their centers of mass), to move them with the bodies between
/// sub-steps.
struct ContactGeometry
{
    Vector3 normal{};                                       ///< In the first body's orientation.
    std::array<Vector3, MAXIMUM_MANIFOLD_POINTS> first{};  ///< The first surface's points.
    std::array<Vector3, MAXIMUM_MANIFOLD_POINTS> second{}; ///< The second's.
};

/// A pair of colliders touching.
struct TouchingPair
{
    BroadPhasePair pair{};
    bool reported = false; ///< Whether its BEGIN was reported (both colliders report contact events).
};

/// The reference backend.
class ReferenceBackend final : public SteppedWorldBackend, private HandleIssuer
{
public:
    explicit ReferenceBackend(const PhysicsWorldDescription& description);
    ~ReferenceBackend() override;

    ReferenceBackend(const ReferenceBackend&) = delete;
    auto operator=(const ReferenceBackend&) -> ReferenceBackend& = delete;

    [[nodiscard]] auto identity() const noexcept -> BackendIdentity override
    {
        return PhysicsBackendKind::REFERENCE;
    }

    [[nodiscard]] auto pipeline() const noexcept -> PipelineKinds override;

    void apply(std::span<const WorldEdit> edits,
               WorldSnapshot& pending) override;

    void simulate(float seconds,
                  WorldSnapshot& pending) override;

    [[nodiscard]] auto broad_phase_component() noexcept -> BroadPhase* override;
    [[nodiscard]] auto narrow_phase_component() noexcept -> NarrowPhase* override;
    [[nodiscard]] auto solver_component() noexcept -> Solver* override;
    [[nodiscard]] auto integrator_component() noexcept -> Integrator* override;
    void replace_broad_phase(std::unique_ptr<BroadPhase> broad_phase) override;
    void replace_narrow_phase(std::unique_ptr<NarrowPhase> narrow_phase) override;
    void replace_solver(std::unique_ptr<Solver> solver) override;
    void replace_integrator(std::unique_ptr<Integrator> integrator) override;

    // ------------------------------------------------------------------------------------------------ shared by the units

    /// The slot of a handle.
    template<class Tag>
    [[nodiscard]] static auto slot(Handle<Tag> handle) noexcept -> std::uint32_t
    {
        return handle.index(handle_key());
    }

    /// The world box of @p collider where its body is in @p pending.
    [[nodiscard]] static auto collider_box(const WorldSnapshot& pending,
                                           const ColliderRecord& collider) noexcept -> Aabb;

    /// Collides the broad phase's pairs into the step's manifolds (reference_contacts.cpp).
    void find_contacts(float seconds,
                       const WorldSnapshot& pending);

    /// The contact events of the pairs that began or ended touching, and the touching pairs' contacts in @p pending.
    void report_contacts(WorldSnapshot& pending);

    /// The trigger events of this step (reference_contacts.cpp).
    void update_triggers(WorldSnapshot& pending);

    /// The joint frames of @p description from the bodies' poses in @p pending (reference_world.cpp).
    [[nodiscard]] static auto joint_entry(const JointDescription& description,
                                          const WorldSnapshot& pending) -> JointEntry;

    /// Ends the touches and overlaps of a collider being destroyed (events now).
    void forget_collider(ColliderHandle collider,
                         WorldSnapshot& pending);

    /// Inserts every live collider of @p pending into the broad phase (a new one).
    void insert_colliders(const WorldSnapshot& pending);

    /// Whether the pair of bodies is joined by an enabled joint or constraint that keeps them from colliding.
    [[nodiscard]] auto joined(RigidBodyHandle first,
                              RigidBodyHandle second) const noexcept -> bool;

    // ------------------------------------------------------------------------------------------------ state

    TaskScheduler* m_scheduler = nullptr;
    std::unique_ptr<BroadPhase> m_broad_phase{};
    std::unique_ptr<NarrowPhase> m_narrow_phase{};
    std::unique_ptr<Solver> m_solver{};
    std::unique_ptr<Integrator> m_integrator{};
    const WorldSnapshot* m_last_pending = nullptr; ///< The snapshot the last apply() saw (to insert colliders on replacement).

    std::vector<BodyEntry> m_bodies{};              ///< By body slot.
    std::vector<ColliderEntry> m_colliders{};       ///< By collider slot.
    std::vector<JointEntry> m_joints{};             ///< By joint slot.
    std::vector<JointEntry> m_constraints{};        ///< By constraint slot.
    std::vector<TriggerEntry> m_triggers{};         ///< By trigger slot.
    ManifoldCache m_manifold_cache{};
    std::vector<PairManifold> m_manifolds{};        ///< This step's, by pair then sub-shapes.
    std::vector<PairManifold> m_previous{};         ///< Last step's.
    std::vector<TouchingPair> m_touching{};         ///< This step's touching pairs, sorted.
    std::vector<TouchingPair> m_was_touching{};     ///< Last step's.
    std::vector<std::pair<RigidBodyHandle, RigidBodyHandle>> m_joined{}; ///< Bodies that may not collide, sorted.

    // Scratch kept between steps.
    std::vector<SolverBody> m_solver_bodies{};      ///< By body slot.
    std::vector<SolverContact> m_solver_contacts{};
    std::vector<ContactGeometry> m_contact_geometry{}; ///< Of each solver contact.
    std::vector<Vector3> m_body_forces{};            ///< By body slot: the step's forces and gravity.
    std::vector<Vector3> m_body_torques{};           ///< By body slot: the step's torques.
    std::vector<std::uint32_t> m_contact_manifolds{}; ///< The manifold of each solver contact.
    std::vector<SolverJoint> m_solver_joints{};
    std::vector<std::uint32_t> m_joint_slots{};      ///< The joint (or 0x80000000 | constraint) slot of each solver joint.
    std::vector<Vector3> m_centers{};
    std::vector<Vector3> m_velocities{};
    std::vector<Vector3> m_previous_centers{};
    std::vector<Vector3> m_forces{};
    std::vector<float> m_inverse_masses{};
    std::vector<std::uint32_t> m_integrated{};       ///< The body slots the integrator advances.
    std::vector<std::uint32_t> m_islands{};          ///< Union-find parents by body slot.
    std::vector<std::uint8_t> m_island_awake{};
    std::vector<float> m_island_sleep{};
    std::vector<ContactCollector> m_collectors{};    ///< One per chunk of pairs.
    std::vector<std::vector<PairManifold>> m_chunk_manifolds{};
    std::vector<ColliderHandle> m_overlaps{};
};

} // namespace gfx::physics::reference_backend
