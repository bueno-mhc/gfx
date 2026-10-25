// The internal state of SteppedWorld (not exported): WorldShared — what the world and its objects share (the edit lock,
// the edits recorded, the handles' slots, what the recorded edits make of each body, the published snapshot) and
// outlives the world while objects hold it —, SteppedWorld::Implementation — what only the stepping side touches (the
// backend, the pending snapshot, the query tree, the handshake, the stepping thread) —, and the functions the units
// share (object factories, description checks).
module gfx.physics.world:state;

import std;
import gfx.core.errors;
import gfx.core.handles;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.handles;
import gfx.physics.interface;
import :edits;
import :snapshot;
import :stepped_world;

namespace gfx::physics
{

/// A slot's index and generation (what a handle encodes).
struct SlotNumber
{
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
};

/// The slots of one kind of object.
struct SlotList
{
    std::vector<std::uint8_t> generations{}; ///< The generation of each slot's current (or last) object.
    std::vector<std::uint8_t> live{};        ///< 1 from the object's creation until its destruction is recorded.
    std::vector<std::uint32_t> free{};       ///< Slots released (reused last in, first out).
};

/// What the recorded edits make of a body (what checks and cascading destructions need).
struct BodyRecording
{
    BodyKind kind = BodyKind::DYNAMIC;
    std::vector<ColliderHandle> colliders{};
    std::vector<TriggerHandle> triggers{};
    std::vector<JointHandle> joints{};
};

/// What a world and its objects share.
class WorldShared : private HandleIssuer
{
public:
    WorldShared(WorldHandle world_handle,
                BackendIdentity backend_identity) noexcept
        : world{world_handle}
        , identity{backend_identity}
    {
    }

    /// The key that encodes and decodes handles (the module's units only).
    [[nodiscard]] static constexpr auto key() noexcept -> HandleKey
    {
        return handle_key();
    }

    /// The world's handle and backend (fixed).
    WorldHandle world{};
    BackendIdentity identity;

    // ------------------------------------------------------------------------------------------------ edits (under mutex)

    /// Locks everything below except the published snapshot.
    mutable std::mutex mutex{};

    /// The backend's checks; null once the world is destroyed.
    SteppedWorldBackend* backend = nullptr;

    /// The edits recorded since the last step started.
    std::vector<WorldEdit> recorded{};

    SlotList rigid_bodies{};
    SlotList colliders{};
    SlotList joints{};
    SlotList triggers{};
    SlotList soft_bodies{};

    std::vector<BodyRecording> body_recordings{};                            ///< By body slot.
    std::vector<RigidBodyHandle> collider_bodies{};                          ///< By collider slot.
    std::vector<ShapeKind> collider_shapes{};                                ///< By collider slot.
    std::vector<RigidBodyHandle> trigger_bodies{};                           ///< By trigger slot (null: fixed in the world).
    std::vector<std::pair<RigidBodyHandle, RigidBodyHandle>> joint_bodies{}; ///< By joint slot.
    std::vector<std::uint32_t> soft_body_particles{};                        ///< By soft body slot.

    // ------------------------------------------------------------------------------------------------ published (under published_mutex)

    /// Shared by getters, exclusive while a step is published.
    mutable std::shared_mutex published_mutex{};

    /// The results of the last completed step.
    WorldSnapshot published{};

    // ------------------------------------------------------------------------------------------------ operations (stepped_world.cpp)

    /// Records @p edit: throws PhysicsError if the world was destroyed or an object it names is not alive, and the
    /// backend's BackendCapabilityError if it cannot apply it.
    void record(WorldEdit edit,
                std::string_view operation);

    /// Records the destruction @p edit (and, for a body, those of its colliders, triggers and joints first); nothing
    /// once the world or the object is gone.
    void record_destruction(const WorldEdit& edit) noexcept;

    /// Issues a new object's handle and records its creation (after the backend's check).
    [[nodiscard]] auto create_rigid_body(const RigidBodyDescription& description) -> RigidBodyHandle;
    [[nodiscard]] auto create_collider(RigidBodyHandle body,
                                       const ColliderDescription& description,
                                       const MassProperties& unit_mass) -> ColliderHandle;
    [[nodiscard]] auto create_joint(const JointDescription& description) -> JointHandle;
    [[nodiscard]] auto create_trigger(const TriggerDescription& description) -> TriggerHandle;
    [[nodiscard]] auto create_soft_body(const SoftBodyDescription& description) -> SoftBodyHandle;

    /// Whether @p handle names a live object (its destruction not recorded).
    [[nodiscard]] auto is_live(RigidBodyHandle handle) const noexcept -> bool;
    [[nodiscard]] auto is_live(ColliderHandle handle) const noexcept -> bool;
    [[nodiscard]] auto is_live(JointHandle handle) const noexcept -> bool;
    [[nodiscard]] auto is_live(TriggerHandle handle) const noexcept -> bool;
    [[nodiscard]] auto is_live(SoftBodyHandle handle) const noexcept -> bool;

    /// Frees the slots of objects whose destruction was applied (when the step is published; takes the lock).
    void release(std::span<const RigidBodyHandle> rigid_bodies_released,
                 std::span<const ColliderHandle> colliders_released,
                 std::span<const JointHandle> joints_released,
                 std::span<const TriggerHandle> triggers_released,
                 std::span<const SoftBodyHandle> soft_bodies_released) noexcept;
};

/// What only the stepping side of a SteppedWorld touches.
struct SteppedWorld::Implementation
{
    std::unique_ptr<SteppedWorldBackend> backend{};
    TaskScheduler* scheduler = nullptr;
    bool asynchronous = false;

    // ------------------------------------------------------------------------------------------------ stepping thread
    WorldSnapshot pending{};
    std::vector<WorldEdit> stepping_edits{};
    std::vector<RigidBodyHandle> released_bodies{};
    std::vector<ColliderHandle> released_colliders{};
    std::vector<JointHandle> released_joints{};
    std::vector<TriggerHandle> released_triggers{};
    std::vector<SoftBodyHandle> released_soft_bodies{};
    std::vector<std::uint8_t> mass_changed{};

    // ------------------------------------------------------------------------------------------------ queries (rebuilt when published)
    Bvh query_tree{};
    std::vector<SpatialProxyHandle> collider_proxies{}; ///< By collider slot.
    std::vector<ColliderHandle> proxy_colliders{};      ///< The collider each proxy was made for, by collider slot.

    // ------------------------------------------------------------------------------------------------ the handshake
    mutable std::atomic<bool> stepping{false};
    mutable std::atomic<std::uint32_t> readers{0};
    std::atomic<std::uint64_t> completed{0};

    // ------------------------------------------------------------------------------------------------ asynchronous stepping
    std::mutex step_mutex{};
    std::condition_variable_any step_condition{};
    bool step_requested = false;
    bool step_finished = false;
    float step_seconds = 0.0F;
    std::exception_ptr step_error{};
    std::jthread worker{};

    // ------------------------------------------------------------------------------------------------ the pipeline as described
    std::unique_ptr<BroadPhase> broad_phase{};
    std::unique_ptr<NarrowPhase> narrow_phase{};
    std::unique_ptr<Solver> solver{};
    std::unique_ptr<Integrator> integrator{};

    // ------------------------------------------------------------------------------------------------ (stepped_world.cpp)

    /// The stepping thread of an asynchronous world: a step each time one is requested, until @p stop.
    void work(std::stop_token stop,
              WorldShared& shared);

    /// A step on this thread: the edits, the backend's simulation, the boxes, the statistics.
    void run_step(WorldShared& shared,
                  float seconds);

    /// Applies @p edits to the pending records (and collects the destroyed objects).
    void apply_to_records(std::span<const WorldEdit> edits);

    /// The mass properties of the bodies whose colliders or settings changed.
    void update_masses();

    /// The colliders', triggers' and soft bodies' world boxes from the pending poses and particles.
    void update_bounds();

    /// Copies the pending snapshot into the published one and frees the destroyed objects' slots.
    void publish(WorldShared& shared);

    /// The query tree over the published colliders' boxes.
    void update_query_tree(const WorldSnapshot& published);
};

/// RAII: a query between steps (the reader side of the handshake). Refused (active() false) while a step runs.
class QueryScope
{
public:
    explicit QueryScope(const SteppedWorld::Implementation& implementation) noexcept;
    ~QueryScope();

    QueryScope(const QueryScope&) = delete;
    auto operator=(const QueryScope&) -> QueryScope& = delete;

    [[nodiscard]] auto active() const noexcept -> bool
    {
        return m_active;
    }

private:
    const SteppedWorld::Implementation* m_implementation;
    bool m_active;
};

/// Throws the PhysicsError of an operation on an object of a destroyed world.
[[noreturn]] void reject_destroyed_world(const BackendIdentity& backend,
                                         WorldHandle world,
                                         std::string_view operation);

// ===================================================================================================== objects (world_objects.cpp)

[[nodiscard]] auto make_rigid_body(const std::shared_ptr<WorldShared>& shared,
                                   RigidBodyHandle handle,
                                   const RigidBodyDescription& initial) -> std::unique_ptr<RigidBody>;
[[nodiscard]] auto make_joint(const std::shared_ptr<WorldShared>& shared,
                              JointHandle handle,
                              const JointDescription& initial) -> std::unique_ptr<Joint>;
[[nodiscard]] auto make_trigger(const std::shared_ptr<WorldShared>& shared,
                                TriggerHandle handle,
                                const TriggerDescription& initial) -> std::unique_ptr<Trigger>;
[[nodiscard]] auto make_soft_body(const std::shared_ptr<WorldShared>& shared,
                                  SoftBodyHandle handle,
                                  const SoftBodyDescription& initial) -> std::unique_ptr<SoftBody>;

// ===================================================================================================== checks (validation.cpp)

/// Each throws the PhysicsError of an invalid value, naming @p operation and the objects in @p context; those returning
/// a value give it normalized (orientations of unit length, axes of unit length).
[[nodiscard]] auto checked_pose(const Transform& pose,
                                const char* what,
                                const BackendIdentity& backend,
                                std::string_view operation,
                                const PhysicsErrorContext& context) -> Transform;
void check_vector(Vector3 value,
                  const char* what,
                  const BackendIdentity& backend,
                  std::string_view operation,
                  const PhysicsErrorContext& context);
void check_non_negative(float value,
                        const char* what,
                        const BackendIdentity& backend,
                        std::string_view operation,
                        const PhysicsErrorContext& context);
void check_finite(float value,
                  const char* what,
                  const BackendIdentity& backend,
                  std::string_view operation,
                  const PhysicsErrorContext& context);
void check_mass_properties(const MassProperties& properties,
                           const BackendIdentity& backend,
                           std::string_view operation,
                           const PhysicsErrorContext& context);
void check_material(const SurfaceMaterial& material,
                    const BackendIdentity& backend,
                    std::string_view operation,
                    const PhysicsErrorContext& context);
[[nodiscard]] auto checked_description(const RigidBodyDescription& description,
                                       const BackendIdentity& backend,
                                       std::string_view operation,
                                       const PhysicsErrorContext& context) -> RigidBodyDescription;
[[nodiscard]] auto checked_description(const ColliderDescription& description,
                                       const BackendIdentity& backend,
                                       std::string_view operation,
                                       const PhysicsErrorContext& context) -> ColliderDescription;
[[nodiscard]] auto checked_description(const TriggerDescription& description,
                                       const BackendIdentity& backend,
                                       std::string_view operation,
                                       const PhysicsErrorContext& context) -> TriggerDescription;
[[nodiscard]] auto checked_description(const JointDescription& description,
                                       const BackendIdentity& backend,
                                       std::string_view operation,
                                       const PhysicsErrorContext& context) -> JointDescription;
void check_description(const SoftBodyDescription& description,
                       const BackendIdentity& backend,
                       std::string_view operation,
                       const PhysicsErrorContext& context);
void check_pin(const ParticlePin& pin,
               std::uint32_t particles,
               const BackendIdentity& backend,
               std::string_view operation,
               const PhysicsErrorContext& context);
void check_description(const SolverDescription& description,
                       const BackendIdentity& backend,
                       std::string_view operation,
                       const PhysicsErrorContext& context);
void check_description(const SleepDescription& description,
                       const BackendIdentity& backend,
                       std::string_view operation,
                       const PhysicsErrorContext& context);
void check_description(const PhysicsWorldDescription& description,
                       const BackendIdentity& backend,
                       std::string_view operation,
                       const PhysicsErrorContext& context);

/// Throws the PhysicsError of @p operation: @p problem, with @p suggestion.
[[noreturn]] void reject(const BackendIdentity& backend,
                         std::string_view operation,
                         std::string problem,
                         std::string suggestion,
                         const PhysicsErrorContext& context);

} // namespace gfx::physics
