// The GPU backend's internal partition: GpuXpbdBackend, a SteppedWorldBackend whose bodies live in GPU arrays (a CPU
// mirror of what the last step read back, by slot), stepped by one command buffer per step on a compute-capable queue;
// its colliders and joints are uploaded when they change; its soft bodies' particles are constraint nodes after the
// bodies, each with a sphere collider among the moving ones.
module gfx.physics.gpu_xpbd:backend;

import std;
import gfx.enums;
import gfx.handles;
import gfx.interface;
import gfx.profiler.graphics;
import gfx.core.handles;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.interface;
import gfx.physics.world;
import gfx.physics.shapes;
import :settings;
import :resources;
import :solver;
import :soft_bodies;

namespace gfx::physics::gpu_xpbd
{

/// What the backend keeps of a collider (by slot).
struct ColliderEntry
{
    ColliderHandle handle{};     ///< Null: a free slot.
    RigidBodyHandle body{};      ///< Its body.
    std::uint32_t shape = 0;     ///< Its shape in the narrow phase's table.
    bool contact_events = false; ///< Its configuration's contact_events (as of the last update_colliders()).
};

/// What the backend keeps of a trigger (by slot): its shape, the colliders it overlapped after the last step (sorted).
struct TriggerEntry
{
    TriggerHandle handle{};                ///< Null: a free slot.
    std::uint32_t shape = 0;               ///< Its shape in the narrow phase's table.
    std::vector<ColliderHandle> overlaps{}; ///< Sorted.
};

/// What the backend keeps of a joint (by slot): its frames, fixed when it was created.
struct JointEntry
{
    JointHandle handle{}; ///< Null: a free slot.
    JointFrames frames{}; ///< In its bodies' frames.
};

/// What the backend keeps of a soft body (by slot): its model and its constraints for the GPU, color after color (the
/// model's colors, then its tethers'), where its particles are on the GPU.
struct SoftBodyEntry
{
    SoftBodyHandle handle{};                       ///< Null: a free slot.
    std::shared_ptr<const SoftBodyModel> model{};  ///< Rest values, inverse masses, starting positions.
    std::vector<GpuSoftConstraint> constraints{};  ///< Its particles' indices in it.
    std::vector<std::uint32_t> color_starts{};     ///< Where each color begins in constraints, then where the last ends.
    std::uint32_t first_particle = 0;              ///< Its first particle on the GPU.
};

/// What the backend keeps of a body: what the last step read back (exactly: patches of untouched values reuse them) and
/// what the edits since then changed.
struct BodyEntry
{
    RigidBodyHandle handle{};
    Vector3 center{};           ///< Center of mass, as the GPU has it.
    Quaternion orientation{};   ///< As the GPU has it.
    Vector3 linear_velocity{};  ///< Of the center, as the GPU has it.
    Vector3 angular_velocity{}; ///< As the GPU has it.
    bool created = false;       ///< New in its slot since the last step.
    bool destroyed = false;     ///< Its slot was freed.
    bool pose_set = false;      ///< Its pose was set (its center comes from the record).
    bool velocity_set = false;  ///< Its velocities were set (from the record).
    bool reconfigured = false;  ///< Its kind, mass, damping, gravity scale or locks changed.
    bool woken = false;         ///< Something woke it.
    bool awake_set = false;     ///< Its awake state was set (from the record).
    bool pushed = false;        ///< Forces, torques or impulses.
    bool touched = false;       ///< In the slots this step's edits named (GpuXpbdBackend::m_touched_bodies).
    Vector3 force{};            ///< For the next step (world space, at the center).
    Vector3 torque{};           ///< For the next step.
    Vector3 linear_impulse{};   ///< To apply at the next step (at the center).
    Vector3 angular_impulse{};  ///< To apply at the next step.
};

/// A pair of colliders touching after a step (the smaller handle first) and where its contacts are.
struct TouchingPair
{
    ColliderHandle first{};          ///< The smaller handle.
    ColliderHandle second{};         ///< The larger.
    RigidBodyHandle first_body{};    ///< Its body.
    RigidBodyHandle second_body{};   ///< Its body.
    bool reported = false;           ///< Both colliders made contact events when it began (its END is reported too).
    bool resting = false;            ///< Its bodies all rested after the step (the next skips it): its contacts are kept.
    std::uint32_t first_contact = 0; ///< Its first contact in the list beside it (when there is one).
    std::uint32_t contact_count = 0; ///< Its contacts (one per manifold: compound children, triangles).
};

/// A contact the GPU reported, as the reports are sorted: its pair (the smaller collider handle first), its sub-shapes
/// (in that order), and where its report is (converted into a ContactInfo once, in sorted order).
struct ReportKey
{
    std::uint64_t pair = 0;                        ///< The pair as one number ordered as the handles are (first's value << 32 | second's).
    ColliderHandle first{};                        ///< The smaller handle.
    ColliderHandle second{};                       ///< The larger.
    RigidBodyHandle first_body{};                  ///< Its body.
    RigidBodyHandle second_body{};                 ///< Its body.
    std::uint32_t sub_shape_first = NO_SUB_SHAPE;  ///< The first collider's compound child or triangle.
    std::uint32_t sub_shape_second = NO_SUB_SHAPE; ///< The second's.
    std::uint32_t report = 0;                      ///< Its index in the readback.
    bool flip = false;                             ///< The narrow phase had the pair the other way.
    bool reported = false;                         ///< Both colliders make contact events.
    float approach_speed = 0.0F;                   ///< The fastest approach of its points at the step's start.
};

/// A report's place in the sorted order: its pair (ReportKey::pair) and its key's index.
struct ReportOrder
{
    std::uint64_t pair = 0; ///< As ReportKey::pair.
    std::uint32_t key = 0;  ///< Its ReportKey's index.
};

/// A query of the GPU (query.comp's Query).
struct GpuQuery
{
    std::uint32_t kind = 0;            ///< QueryKind (gpu_queries.cpp).
    std::uint32_t pair_capacity = 0;   ///< Colliders tested at most.
    std::uint32_t region_instance = 0; ///< The instance the query's region takes.
    std::uint32_t trees = 0;           ///< Bit 0: the moving tree is built; bit 1: the static tree.
    std::array<std::uint32_t, 4> filter{}; ///< Category low and high words, mask low and high words.
    std::uint32_t tables = 0;          ///< Bindless indices: the shape table,
    std::uint32_t colliders = 0;       ///< the colliders,
    std::uint32_t collider_list = 0;   ///< the instances' colliders,
    std::uint32_t instances = 0;       ///< the instances,
    std::uint32_t boxes = 0;           ///< their boxes,
    std::uint32_t moving_header = 0;   ///< the trees,
    std::uint32_t moving_nodes = 0;
    std::uint32_t static_header = 0;
    std::uint32_t static_nodes = 0;
    std::uint32_t pairs = 0;           ///< the pairs found,
    std::uint32_t results = 0;         ///< the results (a header, then three vectors per result),
    std::uint32_t sub_pairs = 0;       ///< the pieces,
    std::uint32_t piece_counts = 0;    ///< each pair's pieces,
    std::uint32_t piece_offsets = 0;   ///< and its first,
    std::uint32_t piece_results = 0;   ///< each piece's answer;
    std::uint32_t piece_capacity = 0;  ///< pieces tested at most.
    std::uint32_t moving_count = 0;    ///< Moving colliders' instances (the first).
    std::uint32_t static_count = 0;    ///< Static colliders' instances (after them).
    std::uint32_t shape = 0;           ///< The cast or overlapping shape's record.
    std::int32_t group = 0;            ///< The filter's group.
    std::array<float, 4> origin{};     ///< A ray's origin, the point, the shape's position, a box's minimum; w: the maximum.
    std::array<float, 4> direction{};  ///< A ray's direction, a cast's translation.
    std::array<float, 4> orientation{0.0F, 0.0F, 0.0F, 1.0F}; ///< The shape's orientation.
    std::array<float, 4> region_minimum{}; ///< The box a cast or an overlap reaches, a box query's box.
    std::array<float, 4> region_maximum{};
    std::array<std::uint32_t, 4> region{}; ///< x: the region's record in the shape table.
};
static_assert(sizeof(GpuQuery) == 208);

/// A query's result (query.comp): a collider, the sub-shape, the distance or fraction, whether it hit; the point, the
/// normal.
struct GpuQueryResult
{
    std::uint32_t collider = 0;
    std::uint32_t sub_shape = 0;
    float value = 0.0F;
    std::uint32_t hit = 0;
    std::array<float, 4> point{};
    std::array<float, 4> normal{};
};
static_assert(sizeof(GpuQueryResult) == 48);

/// A Vulkan instance and device a world made by the factory owns (empty for a world on the application's device).
struct OwnedDevice
{
    std::unique_ptr<Instance> instance{};
    std::unique_ptr<Device> device{};
};

class GpuXpbdBackend;

/// The world's broad phase as PhysicsWorld::broad_phase() shows it: the backend's own, so its operations are refused as the
/// framework's description of a backend's own broad phase refuses them (CUSTOM_BROAD_PHASE), except structure_nodes(),
/// which reads the two collider trees back (between steps).
class GpuBroadPhase final : public BroadPhase
{
public:
    explicit GpuBroadPhase(GpuXpbdBackend& backend) noexcept;

    [[nodiscard]] auto kind() const noexcept -> BroadPhaseKind override
    {
        return BroadPhaseKind::BACKEND;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return false;
    }

    auto insert(ColliderHandle collider,
                const Aabb& box,
                const CollisionFilter& filter) -> SpatialProxyHandle override;
    void remove(SpatialProxyHandle proxy) override;
    auto move(SpatialProxyHandle proxy,
              const Aabb& box,
              Vector3 displacement) -> bool override;
    void set_filter(SpatialProxyHandle proxy,
                    const CollisionFilter& filter) override;
    void update(TaskScheduler* scheduler) override;
    [[nodiscard]] auto pairs() const -> std::span<const BroadPhasePair> override;
    [[nodiscard]] auto new_pairs() const -> std::span<const BroadPhasePair> override;
    [[nodiscard]] auto ended_pairs() const -> std::span<const BroadPhasePair> override;

    /// The moving colliders' tree (the published poses), then the static colliders', each depth first from its root.
    void structure_nodes(std::vector<SpatialNode>& nodes) const override;

private:
    GpuXpbdBackend* m_backend = nullptr;
};

/// The GPU backend.
class GpuXpbdBackend final : public SteppedWorldBackend, private HandleIssuer
{
public:
    /// @param owned     The instance and device the world owns (or nothing).
    /// @param device    The device the world runs on (owned.device when owned).
    /// @param settings  The backend's settings.
    /// @param scheduler The world's task scheduler (PhysicsWorldDescription::task_scheduler): the CPU's share of a step
    ///                  (reading the contacts back) runs on it; null: on the stepping thread.
    /// @throws BackendCapabilityError if @p device lacks a required capability.
    GpuXpbdBackend(OwnedDevice owned,
                   Device& device,
                   const GpuXpbdDescription& settings,
                   TaskScheduler* scheduler);

    /// Waits for the last submission, then releases the GPU resources (the device's deleter frees them).
    ~GpuXpbdBackend() override;

    GpuXpbdBackend(const GpuXpbdBackend&) = delete;
    auto operator=(const GpuXpbdBackend&) -> GpuXpbdBackend& = delete;

    [[nodiscard]] auto identity() const noexcept -> BackendIdentity override
    {
        return PhysicsBackendKind::GPU_XPBD;
    }

    [[nodiscard]] auto pipeline() const noexcept -> PipelineKinds override;

    void check(const WorldEdit& edit,
               const RecordedWorld& recorded) const override;

    void apply(std::span<const WorldEdit> edits,
               WorldSnapshot& pending) override;

    void simulate(float seconds,
                  WorldSnapshot& pending) override;

    [[nodiscard]] auto simulates_soft_bodies() const noexcept -> bool override
    {
        return true;
    }

    /// Its contacts are converted into the published list in place (read_contacts()).
    [[nodiscard]] auto replaces_contacts() const noexcept -> bool override
    {
        return true;
    }

    [[nodiscard]] auto capabilities() const noexcept -> const GpuXpbdCapabilities&
    {
        return m_capabilities;
    }

    [[nodiscard]] auto broad_phase_component() noexcept -> BroadPhase* override
    {
        return &m_broad_phase;
    }

    /// The collider trees' nodes (GpuBroadPhase::structure_nodes()): read back between steps, with the queries' command
    /// buffer.
    void read_structure(std::vector<SpatialNode>& nodes);

    // ------------------------------------------------------------------------------------------------ queries on the GPU
    // One at a time (between steps, as the framework calls them); nothing (the abstraction answers) when the GPU cannot
    // answer exactly: no step yet, a hull to cast or test that the shape table lacks, more colliders or pieces than a query
    // holds; UNSUPPORTED when the GPU fails.

    [[nodiscard]] auto ray_cast(const Ray& ray,
                                float maximum,
                                const QueryOptions& options) const noexcept -> std::optional<QueryResult<RayHit>> override;

    [[nodiscard]] auto ray_cast_all(const Ray& ray,
                                    float maximum,
                                    std::span<RayHit> output,
                                    const QueryOptions& options) const noexcept -> std::optional<QueryResult<std::span<RayHit>>> override;

    [[nodiscard]] auto shape_cast(const Shape& shape,
                                  const Transform& pose,
                                  Vector3 translation,
                                  const QueryOptions& options) const noexcept -> std::optional<QueryResult<ShapeCastHit>> override;

    [[nodiscard]] auto overlap(const Shape& shape,
                               const Transform& pose,
                               std::span<ColliderHandle> output,
                               const QueryOptions& options) const noexcept -> std::optional<QueryResult<std::span<ColliderHandle>>> override;

    [[nodiscard]] auto overlap(const Aabb& box,
                               std::span<ColliderHandle> output,
                               const QueryOptions& options) const noexcept -> std::optional<QueryResult<std::span<ColliderHandle>>> override;

    [[nodiscard]] auto closest_point(Vector3 point,
                                     float maximum,
                                     const QueryOptions& options) const noexcept -> std::optional<QueryResult<ClosestPointHit>> override;

    // ------------------------------------------------------------------------------------------------ shared by the units

    /// The slot of a handle.
    template<class Tag>
    [[nodiscard]] static auto slot(Handle<Tag> handle) noexcept -> std::uint32_t
    {
        return handle.index(handle_key());
    }

    /// The number @p handle's order follows (handles compare their encoded generation and slot index).
    template<class Tag>
    [[nodiscard]] static auto ordering_value(Handle<Tag> handle) noexcept -> std::uint64_t
    {
        return (std::uint64_t{handle.generation(handle_key())} << HANDLE_INDEX_BITS) | handle.index(handle_key());
    }

    /// The entry of @p body's slot (grown when needed).
    [[nodiscard]] auto body_entry(RigidBodyHandle body) -> BodyEntry&;

    /// Marks the colliders changed (their data uploaded again); @p lists: which colliders are moving or static changed;
    /// @p statics: the static tree is rebuilt.
    void colliders_changed(bool lists,
                           bool statics) noexcept;

    /// The table index of @p shape (added the first time).
    [[nodiscard]] auto shape_index(const std::shared_ptr<const Shape>& shape) -> std::uint32_t;

    /// The entry of @p collider's slot (grown when needed).
    [[nodiscard]] auto collider_entry(ColliderHandle collider) -> ColliderEntry&;

    /// The entry of @p joint's slot (grown when needed).
    [[nodiscard]] auto joint_entry(JointHandle joint) -> JointEntry&;

    /// The entry of @p trigger's slot (grown when needed).
    [[nodiscard]] auto trigger_entry(TriggerHandle trigger) -> TriggerEntry&;

    /// The entry of @p soft_body's slot (grown when needed).
    [[nodiscard]] auto soft_body_entry(SoftBodyHandle soft_body) -> SoftBodyEntry&;

    /// Marks the soft bodies made or destroyed (their particles laid out and uploaded again), or (@p layout false) a
    /// particle moved by an edit (their state uploaded again).
    void soft_bodies_changed(bool layout) noexcept;

    /// @p collider is destroyed: the pairs it touched end now (END events for those reported; the dynamic bodies it
    /// touched wake), as the reference backend does.
    void forget_collider(ColliderHandle collider,
                         WorldSnapshot& pending);

private:
    /// The colliders' GPU data and lists from the records, when they changed; the bodies' radii.
    void update_colliders(const WorldSnapshot& pending);

    /// The joints' GPU data, their parts and the bodies they keep apart from the records, when they changed; then the
    /// pins of particles that share their corrections with a body or are compliant (joints of PIN parts).
    void update_joints(const WorldSnapshot& pending);

    /// The soft bodies' particles on the GPU: laid out when soft bodies were made or destroyed (with their constraints),
    /// their state uploaded then or when an edit moved one, their pins and settings when they changed.
    void update_soft_bodies(const WorldSnapshot& pending);

    /// The table index of the sphere of a particle of @p radius (added the first time).
    [[nodiscard]] auto particle_shape(float radius) -> std::uint32_t;

    /// How far a body may move in a step of @p seconds at most (the boxes' and contacts' speculative margin).
    [[nodiscard]] auto step_motion(const WorldSnapshot& pending,
                                   float seconds) const noexcept -> float;
    /// The patch of the body in @p slot from its record and entry (impulses applied, changes cleared).
    [[nodiscard]] auto make_patch(std::uint32_t slot,
                                  RigidBodyRecord* record,
                                  WorldSnapshot& pending) -> GpuBodyPatch;

    /// Records the step's command buffer: this step's patches into the arrays of @p body_count slots (or, replaying,
    ///        the step's start restored), the contacts' detection, then the substeps, sleep, and the copies into the
    ///        readback buffer.
    void record_step(std::uint32_t body_count,
                     const WorldSnapshot& pending,
                     float seconds,
                     bool replay);

    /// The contact counts the last step read back.
    [[nodiscard]] auto read_counts() -> GpuSolverCounts;

    /// Reads the step's results back into @p pending and the entries (sleep transitions and broken joints as events).
    void read_back(WorldSnapshot& pending);

    /// Submits the recorded command buffer (@p commands: the step's by default) and waits for it.
    void run_commands(CommandBuffer* commands = nullptr);

    /// PARTIAL_SUBMISSIONS: submits @p commands, a part of the step, without waiting (the step's last part signals and
    /// is waited for by run_commands()).
    void submit_part(CommandBuffer& commands);

    /// Runs @p query on the GPU (its buffers' indices filled here; @p shape: a cast's or overlap's convex shape): true when
    /// it answered (its results then in m_query_results), false when the abstraction answers (before the first step, a
    /// hull the shape table lacks, more than a query holds), the failure of @p operation when the GPU failed (UNSUPPORTED:
    /// a lost device, which the next step reports).
    [[nodiscard]] auto run_query(GpuQuery query,
                                 const Shape* shape,
                                 std::string_view operation) noexcept -> std::expected<bool, QueryFailure>;

    /// The results run_query() left (the header's counts, then the results).
    [[nodiscard]] auto query_results() noexcept -> std::span<const GpuQueryResult>;


    /// Reads the @p reported contacts of the step: the touching pairs and their contacts into @p pending (with last step's
    /// pairs whose bodies all rest: the GPU skipped them), BEGIN and HIT for the pairs that began, END for those that ended.
    /// @return The points of the pairs kept from last step.
    auto read_contacts(WorldSnapshot& pending,
                       std::uint32_t reported) -> std::uint32_t;

    /// Reads the @p overlaps of triggers and colliders the step found: ENTER, STAY and EXIT for each trigger in slot order,
    /// its colliders in handle order.
    void read_triggers(WorldSnapshot& pending,
                       std::uint32_t overlaps);

    // Declared first: destroyed last, after every resource of the device.
    OwnedDevice m_owned{};
    Device& m_device;
    TaskScheduler* m_scheduler = nullptr;
    bool m_layouts_dirty = true; ///< Colliders, joints and soft bodies to lay out again (always without INCREMENTAL_LAYOUTS).
    GpuXpbdCapabilities m_capabilities{};
    Queue* m_queue = nullptr;
    std::unique_ptr<CommandPool> m_command_pool{};
    std::unique_ptr<CommandBuffer> m_command_buffer{};
    std::vector<std::unique_ptr<CommandBuffer>> m_part_command_buffers{}; ///< PARTIAL_SUBMISSIONS: the parts after the first.
    CommandBuffer* m_step_commands = nullptr;                             ///< The step's last part (run_commands() submits it).
    CommandStatistics m_step_statistics{};                                ///< The step's parts' commands.
    std::unique_ptr<TimelineSemaphore> m_timeline{};
    std::uint64_t m_submitted = 0;
    std::shared_ptr<GpuPipelineLibrary> m_pipeline_library{}; ///< The device's (SHARED_PIPELINES), else null.
    PipelineLibraryScope m_library_scope;                     ///< Open while the constructor makes the pipelines.
    GpuPipelines m_pipelines;
    GpuConstraintSolver m_solver;
    GpuSoftBodies m_soft_bodies;
    profiler::GpuTimer m_gpu_timer; ///< The step's GPU timings: its phases always, its passes while a profiler records them.
    PhaseTimer m_phases;            ///< The phases (StepStatistics::phase_seconds), through m_gpu_timer.
    PassTimer m_passes{};           ///< The backend's own passes, through m_gpu_timer.

    // The body arrays (one storage buffer each).
    GpuArray m_centers{"body centers", sizeof(GpuVector)};
    GpuArray m_orientations{"body orientations", sizeof(GpuVector)};
    GpuArray m_linear_velocities{"body linear velocities", sizeof(GpuVector)};
    GpuArray m_angular_velocities{"body angular velocities", sizeof(GpuVector)};
    GpuArray m_inverse_inertias{"body inverse inertias", 2 * sizeof(GpuVector)};
    GpuArray m_inertias{"body inertias", 2 * sizeof(GpuVector)};
    GpuArray m_forces{"body forces", sizeof(GpuVector)};
    GpuArray m_torques{"body torques", sizeof(GpuVector)};
    GpuArray m_displacements{"body displacements", sizeof(GpuVector)};
    GpuArray m_rotations{"body rotations", sizeof(GpuVector)};
    GpuArray m_flags{"body flags", sizeof(std::uint32_t)};
    GpuArray m_previous_linear{"body linear velocities at the substep's start", sizeof(GpuVector)};
    GpuArray m_previous_angular{"body angular velocities at the substep's start", sizeof(GpuVector)};
    GpuArray m_sleep_timers{"body sleep timers", sizeof(std::uint32_t)};

    // The step's start (after its patches), restored when it is replayed: centers, orientations, linear and angular
    // velocities, flags, sleep timers.
    std::array<GpuArray, 6> m_backups{GpuArray{"body backup", sizeof(GpuVector)}, GpuArray{"body backup", sizeof(GpuVector)},
                                      GpuArray{"body backup", sizeof(GpuVector)}, GpuArray{"body backup", sizeof(GpuVector)},
                                      GpuArray{"body backup", sizeof(std::uint32_t)}, GpuArray{"body backup", sizeof(std::uint32_t)}};

    // Host buffers: the step's parameters and patches, the readback.
    HostBuffer m_parameters{"step parameters", MemoryLocation::HOST_UPLOAD, BufferUsage::STORAGE};
    HostBuffer m_patches{"body patches", MemoryLocation::HOST_UPLOAD, BufferUsage::STORAGE};
    HostBuffer m_readback{"step readback", MemoryLocation::HOST_READBACK, BufferUsage::TRANSFER_DESTINATION};
    HostBuffer m_report_readback{"contact reports readback", MemoryLocation::HOST_READBACK, BufferUsage::TRANSFER_DESTINATION};
    HostBuffer m_trigger_readback{"trigger overlaps readback", MemoryLocation::HOST_READBACK, BufferUsage::TRANSFER_DESTINATION};
    std::vector<std::unique_ptr<Buffer>> m_retired{}; ///< Buffers replaced during the step (released after it).

    // The CPU's side.
    std::vector<BodyEntry> m_bodies{};          ///< By body slot.
    std::vector<GpuBodyPatch> m_body_patches{}; ///< This step's (scratch).
    std::uint32_t m_body_count = 0;             ///< Body slots on the GPU.
    float m_substep_seconds = 0.0F;             ///< This step's substep duration.

    // Colliders and shapes.
    std::vector<ColliderEntry> m_colliders{};                          ///< By collider slot.
    std::unordered_map<const Shape*, std::uint32_t> m_shape_indices{}; ///< Shapes in the narrow phase's table.
    std::vector<std::shared_ptr<const Shape>> m_shapes{};              ///< Kept alive (the table holds copies; the keys are their addresses).
    std::vector<GpuCollider> m_collider_data{};                        ///< By collider slot (scratch).
    std::vector<std::uint32_t> m_moving_list{};                        ///< Colliders of dynamic and kinematic bodies.
    std::vector<std::uint32_t> m_static_list{};                        ///< Colliders of static bodies.
    std::vector<std::uint32_t> m_trigger_list{};                       ///< Triggers' entries in the colliders' data.
    std::vector<std::uint32_t> m_trigger_slots{};                      ///< The trigger slot of each of them.
    std::vector<std::byte> m_static_signature{};                       ///< The static colliders and their bodies' poses
                                                                       ///< the static tree was last built for.
    std::vector<float> m_body_radii{};                                 ///< How far each body's colliders reach from its center.
    bool m_colliders_dirty = false;                                    ///< Their data changed.
    bool m_lists_dirty = false;                                        ///< Which colliders are moving or static changed.
    bool m_statics_dirty = false;                                      ///< The static tree must be rebuilt.
    bool m_lists_changed = false;                                      ///< Since the last completed step (no anchors kept).

    // Triggers.
    std::vector<TriggerEntry> m_triggers{}; ///< By trigger slot.
    std::vector<std::pair<std::uint32_t, ColliderHandle>> m_overlaps{}; ///< A step's (scratch): trigger slots and colliders.

    // Joints.
    std::vector<JointEntry> m_joints{};                            ///< By joint slot.
    std::vector<GpuJoint> m_joint_data{};                          ///< As last uploaded, by joint slot.
    std::vector<GpuJointPart> m_part_data{};                       ///< As last uploaded.
    std::vector<std::array<std::uint32_t, 2>> m_joined_data{};     ///< As last uploaded.

    // Soft bodies.
    std::vector<SoftBodyEntry> m_soft_body_entries{};                  ///< By soft body slot.
    std::vector<GpuParticle> m_particle_data{};                        ///< As last uploaded.
    std::vector<GpuSoftBody> m_soft_body_data{};                       ///< As last uploaded.
    std::unordered_map<std::uint32_t, std::uint32_t> m_sphere_shapes{}; ///< Particle radii (bits) to their spheres in the table.
    std::uint32_t m_particle_count = 0;                                ///< Particles on the GPU.
    bool m_soft_layout_changed = false;                                ///< Soft bodies made or destroyed since the last step.
    bool m_particle_state_dirty = false;                               ///< A particle moved by an edit since the last step.

    // Queries: one at a time; their command buffer, pipeline, input, results, pairs and pieces; the region's and the
    // query shape's records in the shape table; whether the moving tree holds the published poses.
    std::mutex m_query_mutex{};
    std::unique_ptr<CommandPool> m_query_pool{};
    std::unique_ptr<CommandBuffer> m_query_commands{};
    std::vector<std::unique_ptr<ShaderModule>> m_query_modules{};
    std::shared_ptr<ComputePipeline> m_query_pipeline{};
    std::unique_ptr<Buffer> m_query_pairs{};
    std::unique_ptr<Buffer> m_query_pieces{};
    std::unique_ptr<Buffer> m_query_answers{}; ///< What the passes write (the results' copy is read).
    HostBuffer m_query_input{"query", MemoryLocation::HOST_UPLOAD, BufferUsage::STORAGE | BufferUsage::TRANSFER_SOURCE};
    HostBuffer m_query_results{"query results", MemoryLocation::HOST_READBACK, BufferUsage::TRANSFER_DESTINATION};
    std::array<std::uint32_t, 2> m_query_records{};
    bool m_query_tree_current = false;

    // The broad phase PhysicsWorld::broad_phase() shows; the collider trees read back for it.
    GpuBroadPhase m_broad_phase{*this};
    HostBuffer m_structure_readback{"collider trees read back", MemoryLocation::HOST_READBACK, BufferUsage::TRANSFER_DESTINATION};

    // Contacts: the pairs touching after the last step (sorted by their handles) and their contacts; this step's
    // (scratch, swapped with them when read).
    std::vector<TouchingPair> m_was_touching{};
    std::vector<ContactInfo> m_was_touching_contacts{}; ///< The contacts of last step's resting pairs only.
    std::vector<TouchingPair> m_touching{};
    std::vector<ContactInfo> m_touching_contacts{};
    std::vector<ReportKey> m_report_keys{};
    std::vector<ReportOrder> m_report_order{};         ///< The keys, sorted.
    std::vector<ReportOrder> m_report_order_scratch{}; ///< (The radix sort's other half.)
    std::vector<std::uint8_t> m_body_rests{};          ///< By body slot after read_back(): static, or dynamic and asleep.
    std::vector<std::uint8_t> m_body_sleep_changed{};  ///< By body slot (by listed body with COMPACT_READBACK) during read_back(): woke up or fell asleep.
    std::uint32_t m_unchanged_contact_steps = 0;       ///< Steps in a row whose pairs and contacts were the last step's (none reported, all carried over).
    bool m_rested = false;                             ///< After read_back(): no body moves (every dynamic one asleep, no kinematic one awake).
    bool m_edited = false;                             ///< apply() had edits since the last step.
    std::uint32_t m_was_touching_contact_count = 0;    ///< Contacts of last step's pairs (all carried over: the published list's size).
    std::uint32_t m_was_touching_points = 0;           ///< Their points.

    // The slots this step's edits named (sorted when the patches are made: slot order).
    std::vector<std::uint32_t> m_touched_bodies{};

    // COMPACT_READBACK: the bodies whose state may have changed, listed and gathered on the GPU (five vectors each); how many
    // the step's copy holds (the last step's count and a margin; the rest copied after it) and the readback's layout.
    GpuArray m_readback_listed{"read-back flags", sizeof(std::uint32_t)};
    GpuArray m_readback_positions{"read-back slots", sizeof(std::uint32_t)};
    GpuArray m_readback_count{"read-back count", sizeof(std::uint32_t)};
    GpuArray m_readback_bodies{"read-back bodies", 5 * sizeof(GpuVector)};
    bool m_compact_readback = false;               ///< This step's bodies are read back compacted.
    std::uint32_t m_compact_capacity = 0;          ///< Listed bodies the step's copy holds.
    std::uint32_t m_last_listed = 0;               ///< Bodies the last step listed.
    std::uint32_t m_report_copied = 0;             ///< Contact reports the step's copy holds.
    std::uint32_t m_last_reported = 0;             ///< Reports the last step wrote.
    std::size_t m_readback_counts_offset = 0;      ///< Where the readback holds the solver's counts,
    std::size_t m_readback_joints_offset = 0;      ///< the joints' results,
    std::size_t m_readback_particles_offset = 0;   ///< the particles' positions, then velocities.
};

} // namespace gfx::physics::gpu_xpbd
