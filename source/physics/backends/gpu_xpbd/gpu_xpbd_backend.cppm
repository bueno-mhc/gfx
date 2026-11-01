// The GPU backend's internal partition: GpuXpbdBackend, a SteppedWorldBackend whose bodies live in GPU arrays (a CPU
// mirror of what the last step read back, by slot), stepped by one command buffer per step on a compute-capable queue.
module gfx.physics.gpu_xpbd:backend;

import std;
import gfx.enums;
import gfx.handles;
import gfx.interface;
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

namespace gfx::physics::gpu_xpbd
{

/// What the backend keeps of a collider (by slot).
struct ColliderEntry
{
    ColliderHandle handle{};  ///< Null: a free slot.
    RigidBodyHandle body{};   ///< Its body.
    std::uint32_t shape = 0;  ///< Its shape in the narrow phase's table.
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
    Vector3 force{};            ///< For the next step (world space, at the center).
    Vector3 torque{};           ///< For the next step.
    Vector3 linear_impulse{};   ///< To apply at the next step (at the center).
    Vector3 angular_impulse{};  ///< To apply at the next step.
};

/// A Vulkan instance and device a world made by the factory owns (empty for a world on the application's device).
struct OwnedDevice
{
    std::unique_ptr<Instance> instance{};
    std::unique_ptr<Device> device{};
};

/// The GPU backend.
class GpuXpbdBackend final : public SteppedWorldBackend, private HandleIssuer
{
public:
    /// @param owned    The instance and device the world owns (or nothing).
    /// @param device   The device the world runs on (owned.device when owned).
    /// @param settings The backend's settings.
    /// @throws BackendCapabilityError if @p device lacks a required capability.
    GpuXpbdBackend(OwnedDevice owned,
                   Device& device,
                   const GpuXpbdDescription& settings);

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

    [[nodiscard]] auto capabilities() const noexcept -> const GpuXpbdCapabilities&
    {
        return m_capabilities;
    }

    // ------------------------------------------------------------------------------------------------ shared by the units

    /// The slot of a handle.
    template<class Tag>
    [[nodiscard]] static auto slot(Handle<Tag> handle) noexcept -> std::uint32_t
    {
        return handle.index(handle_key());
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

private:
    /// The colliders' GPU data and lists from the records, when they changed; the bodies' radii.
    void update_colliders(const WorldSnapshot& pending);

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

    /// Reads the step's results back into @p pending and the entries (sleep transitions as events).
    void read_back(WorldSnapshot& pending);

    // Declared first: destroyed last, after every resource of the device.
    OwnedDevice m_owned{};
    Device& m_device;
    GpuXpbdCapabilities m_capabilities{};
    Queue* m_queue = nullptr;
    std::unique_ptr<CommandPool> m_command_pool{};
    std::unique_ptr<CommandBuffer> m_command_buffer{};
    std::unique_ptr<TimelineSemaphore> m_timeline{};
    std::uint64_t m_submitted = 0;
    GpuPipelines m_pipelines;
    GpuContactSolver m_solver;

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
    std::vector<std::byte> m_static_signature{};                       ///< The static colliders and their bodies' poses
                                                                       ///< the static tree was last built for.
    std::vector<float> m_body_radii{};                                 ///< How far each body's colliders reach from its center.
    bool m_colliders_dirty = false;                                    ///< Their data changed.
    bool m_lists_dirty = false;                                        ///< Which colliders are moving or static changed.
    bool m_statics_dirty = false;                                      ///< The static tree must be rebuilt.
    bool m_lists_changed = false;                                      ///< Since the last completed step (no anchors kept).
};

} // namespace gfx::physics::gpu_xpbd
