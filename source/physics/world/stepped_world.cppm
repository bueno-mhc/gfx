/// @file stepped_world.cppm
/// @brief SteppedWorld: the PhysicsWorld every backend of the library is built on, and what a backend implements for it
///        (SteppedWorldBackend).
///
/// The framework does everything that does not depend on how bodies move: it issues handles, owns the objects the
/// application holds (generic RigidBody, Collider, Joint, Trigger and SoftBody classes), checks every description and
/// value when it is recorded, records the edits and hands them to the backend at the start of the next step in order,
/// runs steps on the caller or on its own thread, keeps the pending and published snapshots, publishes in wait_step(),
/// refuses queries during steps (the reader handshake) and answers queries itself (QueryEngine::ABSTRACTION: a BVH over
/// the colliders' boxes, then the narrow phase's queries) — the backend's own queries answer QueryEngine::BACKEND when it
/// has them. A backend applies the edits to its own world, advances it and writes the results into the pending
/// snapshot.
export module gfx.physics.world:stepped_world;

import std;
import gfx.core.errors;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.interface;
import :edits;
import :snapshot;

export namespace gfx::physics
{

class WorldShared;

/// @brief The kinds of a backend's own pipeline (what PhysicsWorld::broad_phase() ... describe).
struct PipelineKinds
{
    BroadPhaseKind broad_phase = BroadPhaseKind::BACKEND;            ///< Its broad phase.
    NarrowPhaseKind narrow_phase = NarrowPhaseKind::BACKEND;         ///< Its narrow phase.
    SolverKind solver = SolverKind::SEQUENTIAL_IMPULSE;              ///< Its solver.
    IntegratorKind integrator = IntegratorKind::SEMI_IMPLICIT_EULER; ///< How it integrates rigid bodies.
};

/// @brief The world as recorded so far (the edits not applied yet included): what a backend's check() may ask.
class RecordedWorld
{
public:
    /// @param shared The world's shared state (its edit lock held).
    explicit RecordedWorld(const WorldShared& shared) noexcept
        : m_shared{&shared}
    {
    }

    /// @brief The kind @p body has as recorded, or nothing if it names no live body.
    [[nodiscard]] auto body_kind(RigidBodyHandle body) const noexcept -> std::optional<BodyKind>;

    /// @brief The kinds of the shapes of @p body's live colliders.
    [[nodiscard]] auto collider_shape_kinds(RigidBodyHandle body) const -> std::vector<ShapeKind>;

    /// @brief The body @p collider is attached to, or nothing if it names no live collider.
    [[nodiscard]] auto collider_body(ColliderHandle collider) const noexcept -> std::optional<RigidBodyHandle>;

private:
    const WorldShared* m_shared;
};

/// @brief What a backend implements for SteppedWorld.
///
/// Each function is called on one thread at a time: check() on the recording thread under the world's edit lock;
/// apply() and simulate() on the stepping thread; the query functions from any thread between steps (concurrently).
class SteppedWorldBackend
{
public:
    virtual ~SteppedWorldBackend() = default;

    /// @brief The backend errors name ("[gfx::physics::BOX3D] ...").
    [[nodiscard]] virtual auto identity() const noexcept -> BackendIdentity = 0;

    /// @brief The kinds of its own pipeline.
    [[nodiscard]] virtual auto pipeline() const noexcept -> PipelineKinds = 0;

    /// @brief Throws the BackendCapabilityError of an edit it cannot apply (a six-DOF joint, a mesh on a dynamic body,
    ///        ...), when the edit is recorded. Accepts everything by default.
    /// @param edit     The edit, already checked by the framework.
    /// @param recorded The world as recorded so far (before @p edit).
    virtual void check(const WorldEdit& edit,
                       const RecordedWorld& recorded) const;

    /// @brief Applies @p edits, in order, to its own world. @p pending already has them applied to its records.
    virtual void apply(std::span<const WorldEdit> edits,
                       WorldSnapshot& pending) = 0;

    /// @brief Advances its world by @p seconds and writes the results into @p pending: body states, joint states,
    ///        soft-body particles, events, contacts, and the statistics it knows (phase times, pairs, contact points).
    virtual void simulate(float seconds,
                          WorldSnapshot& pending) = 0;

    /// @brief Its own ray cast (QueryEngine::BACKEND); nothing: the abstraction answers.
    [[nodiscard]] virtual auto ray_cast(const Ray& ray,
                                        float maximum,
                                        const QueryOptions& options) const noexcept -> std::optional<QueryResult<RayHit>>;

    /// @brief Its own ray cast of every hit; nothing: the abstraction answers.
    [[nodiscard]] virtual auto ray_cast_all(const Ray& ray,
                                            float maximum,
                                            std::span<RayHit> output,
                                            const QueryOptions& options) const noexcept -> std::optional<QueryResult<std::span<RayHit>>>;

    /// @brief Its own shape cast; nothing: the abstraction answers.
    [[nodiscard]] virtual auto shape_cast(const Shape& shape,
                                          const Transform& pose,
                                          Vector3 translation,
                                          const QueryOptions& options) const noexcept -> std::optional<QueryResult<ShapeCastHit>>;

    /// @brief Its own shape overlap; nothing: the abstraction answers.
    [[nodiscard]] virtual auto overlap(const Shape& shape,
                                       const Transform& pose,
                                       std::span<ColliderHandle> output,
                                       const QueryOptions& options) const noexcept -> std::optional<QueryResult<std::span<ColliderHandle>>>;

    /// @brief Its own box overlap; nothing: the abstraction answers.
    [[nodiscard]] virtual auto overlap(const Aabb& box,
                                       std::span<ColliderHandle> output,
                                       const QueryOptions& options) const noexcept -> std::optional<QueryResult<std::span<ColliderHandle>>>;

    /// @brief Its own closest point; nothing: the abstraction answers.
    [[nodiscard]] virtual auto closest_point(Vector3 point,
                                             float maximum,
                                             const QueryOptions& options) const noexcept -> std::optional<QueryResult<ClosestPointHit>>;

    /// @brief Replaces its broad phase.
    /// @throws BackendCapabilityError by default (PhysicsCapability::CUSTOM_BROAD_PHASE).
    virtual void replace_broad_phase(std::unique_ptr<BroadPhase> broad_phase);

    /// @brief Replaces its narrow phase.
    /// @throws BackendCapabilityError by default (CUSTOM_NARROW_PHASE).
    virtual void replace_narrow_phase(std::unique_ptr<NarrowPhase> narrow_phase);

    /// @brief Replaces its solver.
    /// @throws BackendCapabilityError by default (CUSTOM_SOLVER).
    virtual void replace_solver(std::unique_ptr<Solver> solver);

    /// @brief Replaces its integrator of rigid bodies.
    /// @throws BackendCapabilityError by default (CUSTOM_INTEGRATOR).
    virtual void replace_integrator(std::unique_ptr<Integrator> integrator);

protected:
    SteppedWorldBackend() = default;
    SteppedWorldBackend(const SteppedWorldBackend&) = default;
    auto operator=(const SteppedWorldBackend&) -> SteppedWorldBackend& = default;
};

/// @brief A world of a SteppedWorldBackend.
class SteppedWorld final : public PhysicsWorld
{
public:
    /// @brief A world of @p backend as @p description says.
    /// @throws PhysicsError if @p description is invalid or @p backend is null.
    SteppedWorld(const PhysicsWorldDescription& description,
                 std::unique_ptr<SteppedWorldBackend> backend);

    /// @brief Waits for a running step (its results are dropped), stops the world's thread, then destroys the
    ///        backend. The objects still held throw PhysicsError from then on.
    ~SteppedWorld() override;

    SteppedWorld(const SteppedWorld&) = delete;
    auto operator=(const SteppedWorld&) -> SteppedWorld& = delete;

    /// @brief The backend (tests and tools).
    [[nodiscard]] auto world_backend() noexcept -> SteppedWorldBackend&;

    [[nodiscard]] auto handle() const noexcept -> WorldHandle override;
    [[nodiscard]] auto description() const -> PhysicsWorldDescription override;

    [[nodiscard]] auto create_rigid_body(const RigidBodyDescription& description) -> std::unique_ptr<RigidBody> override;
    [[nodiscard]] auto create_soft_body(const SoftBodyDescription& description) -> std::unique_ptr<SoftBody> override;
    [[nodiscard]] auto create_joint(const JointDescription& description) -> std::unique_ptr<Joint> override;
    [[nodiscard]] auto create_trigger(const TriggerDescription& description) -> std::unique_ptr<Trigger> override;

    void set_gravity(Vector3 gravity) override;
    void set_solver_description(const SolverDescription& description) override;
    void set_sleep_description(const SleepDescription& description) override;

    void step(float seconds) override;
    void begin_step(float seconds) override;
    void wait_step() override;
    [[nodiscard]] auto is_stepping() const noexcept -> bool override;
    [[nodiscard]] auto step_count() const noexcept -> std::uint64_t override;

    [[nodiscard]] auto events() const -> PhysicsEvents override;
    [[nodiscard]] auto statistics() const -> StepStatistics override;

    [[nodiscard]] auto ray_cast(const Ray& ray,
                                float maximum,
                                const QueryOptions& options) const noexcept -> QueryResult<RayHit> override;
    [[nodiscard]] auto ray_cast_all(const Ray& ray,
                                    float maximum,
                                    std::span<RayHit> output,
                                    const QueryOptions& options) const noexcept -> QueryResult<std::span<RayHit>> override;
    [[nodiscard]] auto shape_cast(const Shape& shape,
                                  const Transform& pose,
                                  Vector3 translation,
                                  const QueryOptions& options) const noexcept -> QueryResult<ShapeCastHit> override;
    [[nodiscard]] auto overlap(const Shape& shape,
                               const Transform& pose,
                               std::span<ColliderHandle> output,
                               const QueryOptions& options) const noexcept -> QueryResult<std::span<ColliderHandle>> override;
    [[nodiscard]] auto overlap(const Aabb& box,
                               std::span<ColliderHandle> output,
                               const QueryOptions& options) const noexcept -> QueryResult<std::span<ColliderHandle>> override;
    [[nodiscard]] auto closest_point(Vector3 point,
                                     float maximum,
                                     const QueryOptions& options) const noexcept -> QueryResult<ClosestPointHit> override;
    [[nodiscard]] auto contacts_of(RigidBodyHandle body,
                                   std::span<ContactInfo> output) const noexcept -> QueryResult<std::span<ContactInfo>> override;

    [[nodiscard]] auto broad_phase() -> BroadPhase& override;
    [[nodiscard]] auto narrow_phase() -> NarrowPhase& override;
    [[nodiscard]] auto solver() -> Solver& override;
    [[nodiscard]] auto integrator() -> Integrator& override;
    void replace_broad_phase(std::unique_ptr<BroadPhase> broad_phase) override;
    void replace_narrow_phase(std::unique_ptr<NarrowPhase> narrow_phase) override;
    void replace_solver(std::unique_ptr<Solver> solver) override;
    void replace_integrator(std::unique_ptr<Integrator> integrator) override;

    /// @brief What only the stepping side touches (defined in the module's internal partition: opaque elsewhere).
    struct Implementation;

private:
    std::shared_ptr<WorldShared> m_shared{};
    std::unique_ptr<Implementation> m_implementation{};
};

} // namespace gfx::physics
