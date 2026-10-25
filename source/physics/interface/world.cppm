/// @file world.cppm
/// @brief PhysicsWorld: one simulation — its objects, its steps, its queries, its events and its pipeline.
///
/// **Changes are deferred.** Every change — creating or destroying an object, a setter, a force, an impulse — is
/// recorded, from any thread, and applied at the start of the next step in the order recorded. New objects get their
/// handles at once; getters return the state of the last completed step. So a replay of the same changes gives the same
/// simulation, whatever the timing of the threads that made them.
///
/// **Stepping.** step() runs a step and publishes its results; begin_step() starts one — on the world's own thread when
/// PhysicsWorldDescription::asynchronous_stepping is set, at once on the caller otherwise — and wait_step() waits for
/// it and publishes. Results (getters, events(), statistics()) change only in wait_step() (or step()), on the thread
/// that steps.
///
/// **Queries** (ray_cast(), shape_cast(), overlap(), closest_point(), contacts_of()) never throw and never allocate:
/// results go to the caller's storage, failures are QueryFailure values. Any number of threads query at once between
/// steps; a query made while a step runs returns QueryFailureKind::STEP_IN_PROGRESS at once, and a step waits for the
/// queries already running before it changes anything (a reader handshake).
///
/// **Objects** are owned by the application (std::unique_ptr) and must not outlive their world's usefulness: once the
/// world is destroyed their operations throw PhysicsError.
export module gfx.physics.interface:world;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.handles;
import gfx.physics.primitives;
import gfx.physics.shapes;
import :descriptions;
import :object;
import :objects;
import :pipeline;

export namespace gfx::physics
{

/// @brief A physics world.
class PhysicsWorld : public PhysicsObject
{
public:
    /// @brief Its handle (errors name it).
    [[nodiscard]] virtual auto handle() const noexcept -> WorldHandle = 0;

    /// @brief What it is after the last completed step: its description with the changes applied since.
    [[nodiscard]] virtual auto description() const -> PhysicsWorldDescription = 0;

    // ------------------------------------------------------------------------------------------------ objects

    /// @brief A rigid body, simulated from the next step.
    /// @throws PhysicsError if @p description is invalid (a pose or velocity that is not finite, negative damping, ...).
    [[nodiscard]] virtual auto create_rigid_body(const RigidBodyDescription& description) -> std::unique_ptr<RigidBody> = 0;

    /// @brief A soft body, simulated from the next step.
    /// @throws PhysicsError if @p description is invalid (no particle, an index out of range, ...).
    /// @throws BackendCapabilityError if the backend has no soft bodies (PhysicsCapability::SOFT_BODIES).
    [[nodiscard]] virtual auto create_soft_body(const SoftBodyDescription& description) -> std::unique_ptr<SoftBody> = 0;

    /// @brief A joint, simulated from the next step.
    /// @throws PhysicsError if @p description is invalid (a body that does not exist, a body joined to itself, limits
    ///         out of order, ...).
    /// @throws BackendCapabilityError if the backend lacks the joint's kind (Box3D: SIX_DEGREES_OF_FREEDOM).
    [[nodiscard]] virtual auto create_joint(const JointDescription& description) -> std::unique_ptr<Joint> = 0;

    /// @brief A trigger, reporting from the next step.
    /// @throws PhysicsError if @p description is invalid (no shape, a body that does not exist).
    [[nodiscard]] virtual auto create_trigger(const TriggerDescription& description) -> std::unique_ptr<Trigger> = 0;

    // ------------------------------------------------------------------------------------------------ settings

    /// @brief Changes the acceleration of gravity.
    /// @throws PhysicsError if @p gravity is not finite.
    virtual void set_gravity(Vector3 gravity) = 0;

    /// @brief Changes how the solver works.
    /// @throws PhysicsError if @p description is invalid (no sub-step, no iteration, a negative threshold, ...).
    virtual void set_solver_description(const SolverDescription& description) = 0;

    /// @brief Changes when bodies sleep.
    virtual void set_sleep_description(const SleepDescription& description) = 0;

    // ------------------------------------------------------------------------------------------------ stepping

    /// @brief Runs a step of @p seconds and publishes its results (begin_step() then wait_step()).
    /// @throws PhysicsError if @p seconds is not finite and positive, or a step is already running.
    virtual void step(float seconds) = 0;

    /// @brief Starts a step of @p seconds: the changes recorded so far are applied, then the simulation advances.
    /// @throws PhysicsError if @p seconds is not finite and positive, or a step is already running.
    virtual void begin_step(float seconds) = 0;

    /// @brief Waits for the running step (if any) and publishes its results.
    /// @throws Whatever the step threw (the results are not published; the changes it applied are kept).
    virtual void wait_step() = 0;

    /// @brief Whether a step is running (between begin_step() and wait_step()).
    [[nodiscard]] virtual auto is_stepping() const noexcept -> bool = 0;

    /// @brief Steps completed and published.
    [[nodiscard]] virtual auto step_count() const noexcept -> std::uint64_t = 0;

    // ------------------------------------------------------------------------------------------------ results of the last step

    /// @brief What the last completed step reported; valid until the next step is published.
    [[nodiscard]] virtual auto events() const -> PhysicsEvents = 0;

    /// @brief How the last completed step went.
    [[nodiscard]] virtual auto statistics() const -> StepStatistics = 0;

    // ------------------------------------------------------------------------------------------------ queries

    /// @brief The nearest collider @p ray hits within @p maximum.
    [[nodiscard]] virtual auto ray_cast(const Ray& ray,
                                        float maximum,
                                        const QueryOptions& options = {}) const noexcept -> QueryResult<RayHit> = 0;

    /// @brief Every collider @p ray hits within @p maximum, nearest first, into @p output.
    /// @return The hits written; QueryFailureKind::CAPACITY_EXCEEDED when there were more (@p output then holds the
    ///         nearest).
    [[nodiscard]] virtual auto ray_cast_all(const Ray& ray,
                                            float maximum,
                                            std::span<RayHit> output,
                                            const QueryOptions& options = {}) const noexcept -> QueryResult<std::span<RayHit>> = 0;

    /// @brief The first collider the convex @p shape at @p pose touches when moved by @p translation.
    [[nodiscard]] virtual auto shape_cast(const Shape& shape,
                                          const Transform& pose,
                                          Vector3 translation,
                                          const QueryOptions& options = {}) const noexcept -> QueryResult<ShapeCastHit> = 0;

    /// @brief The colliders the convex @p shape at @p pose touches or overlaps, into @p output (in handle order).
    /// @return The colliders written; CAPACITY_EXCEEDED when there were more.
    [[nodiscard]] virtual auto overlap(const Shape& shape,
                                       const Transform& pose,
                                       std::span<ColliderHandle> output,
                                       const QueryOptions& options = {}) const noexcept -> QueryResult<std::span<ColliderHandle>> = 0;

    /// @brief The colliders whose boxes overlap @p box, into @p output (in handle order).
    /// @return The colliders written; CAPACITY_EXCEEDED when there were more.
    [[nodiscard]] virtual auto overlap(const Aabb& box,
                                       std::span<ColliderHandle> output,
                                       const QueryOptions& options = {}) const noexcept -> QueryResult<std::span<ColliderHandle>> = 0;

    /// @brief The collider nearest to @p point within @p maximum, and its nearest point.
    [[nodiscard]] virtual auto closest_point(Vector3 point,
                                             float maximum,
                                             const QueryOptions& options = {}) const noexcept -> QueryResult<ClosestPointHit> = 0;

    /// @brief The contacts of @p body's colliders after the last completed step, into @p output.
    /// @return The contacts written; CAPACITY_EXCEEDED when there were more; INVALID_HANDLE if @p body names no body.
    [[nodiscard]] virtual auto contacts_of(RigidBodyHandle body,
                                           std::span<ContactInfo> output) const noexcept -> QueryResult<std::span<ContactInfo>> = 0;

    // ------------------------------------------------------------------------------------------------ pipeline

    /// @brief Its broad phase (the backend's own, described, or a component).
    [[nodiscard]] virtual auto broad_phase() -> BroadPhase& = 0;

    /// @brief Its narrow phase.
    [[nodiscard]] virtual auto narrow_phase() -> NarrowPhase& = 0;

    /// @brief Its solver.
    [[nodiscard]] virtual auto solver() -> Solver& = 0;

    /// @brief Its integrator of rigid bodies.
    [[nodiscard]] virtual auto integrator() -> Integrator& = 0;

    /// @brief Replaces its broad phase from the next step.
    /// @throws BackendCapabilityError if the backend runs its own (PhysicsCapability::CUSTOM_BROAD_PHASE).
    virtual void replace_broad_phase(std::unique_ptr<BroadPhase> broad_phase) = 0;

    /// @brief Replaces its narrow phase from the next step.
    /// @throws BackendCapabilityError if the backend runs its own (CUSTOM_NARROW_PHASE).
    virtual void replace_narrow_phase(std::unique_ptr<NarrowPhase> narrow_phase) = 0;

    /// @brief Replaces its solver from the next step.
    /// @throws BackendCapabilityError if the backend runs its own (CUSTOM_SOLVER).
    virtual void replace_solver(std::unique_ptr<Solver> solver) = 0;

    /// @brief Replaces its integrator of rigid bodies from the next step.
    /// @throws BackendCapabilityError if the backend integrates its bodies itself (CUSTOM_INTEGRATOR).
    virtual void replace_integrator(std::unique_ptr<Integrator> integrator) = 0;

protected:
    using PhysicsObject::PhysicsObject;

    /// @brief A handle for a new world (worlds are numbered in the order they are made).
    [[nodiscard]] static auto issue_world_handle() -> WorldHandle;

    /// @brief The QueryFailure of @p operation when the backend does not answer queries.
    [[nodiscard]] auto unsupported_query(std::string_view operation) const noexcept -> QueryFailure;
};

} // namespace gfx::physics
