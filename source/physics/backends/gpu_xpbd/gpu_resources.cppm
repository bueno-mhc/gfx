// The GPU backend's internal partition of GPU resources: the layouts the CPU and the shaders share (each checked here by
// size and offset; shaders/step.glsl declares the same), growable device arrays and host buffers, the compute pipelines
// of the passes, and their GPU timing (the step's phases, the passes' scopes).
module;

#include <stddef.h>

module gfx.physics.gpu_xpbd:resources;

import std;
import gfx.enums;
import gfx.handles;
import gfx.interface;
import gfx.profiler.graphics;
import gfx.physics.enums;
import gfx.physics.shapes;
import :settings;

namespace gfx::physics::gpu_xpbd
{

// ===================================================================================================== shared layouts

/// Four floats: a vec4 of the shaders (std430).
using GpuVector = std::array<float, 4>;

/// What every pass of a step reads (one storage buffer written by the CPU before each step; StepParameters in step.glsl).
struct GpuStepParameters
{
    GpuVector gravity{};                           ///< xyz: the world's gravity; w: the substep's duration.
    std::uint32_t body_count = 0;                  ///< Body slots on the GPU.
    std::uint32_t patch_count = 0;                 ///< Body patches of this step.
    std::uint32_t substeps = 1;                    ///< Substeps of this step.
    std::uint32_t particle_count = 0;              ///< Soft-body particles on the GPU (constraint nodes body_count + p).
    std::uint32_t body_centers = 0;                ///< Bindless indices of the body arrays: centers of mass (w: inverse mass),
    std::uint32_t body_orientations = 0;           ///< orientations,
    std::uint32_t body_linear_velocities = 0;      ///< linear velocities of the centers (w: linear damping),
    std::uint32_t body_angular_velocities = 0;     ///< angular velocities (w: angular damping),
    std::uint32_t body_inverse_inertias = 0;       ///< inverse inertias in the body frame (two vectors each),
    std::uint32_t body_inertias = 0;               ///< inertias in the body frame (two vectors each),
    std::uint32_t body_forces = 0;                 ///< forces of this step,
    std::uint32_t body_torques = 0;                ///< torques of this step,
    std::uint32_t body_displacements = 0;          ///< displacements of the centers in the substep,
    std::uint32_t body_rotations = 0;              ///< rotation vectors of the substep,
    std::uint32_t body_flags = 0;                  ///< flags (GpuBodyFlag, then the motion locks),
    std::uint32_t body_patches = 0;                ///< of this step's patches,
    std::uint32_t body_previous_linear = 0;        ///< the linear velocities at the substep's start,
    std::uint32_t body_previous_angular = 0;       ///< the angular velocities at the substep's start,
    std::uint32_t sleep_timers = 0;                ///< the seconds each body stayed slow;
    std::uint32_t particle_positions = 0;          ///< the particles' positions,
    std::uint32_t particle_velocities = 0;         ///< velocities,
    std::uint32_t particle_displacements = 0;      ///< displacements in the substep,
    std::uint32_t particle_previous_velocities = 0; ///< velocities at the substep's start,
    std::uint32_t particles = 0;                   ///< what changes only with edits (GpuParticle),
    std::uint32_t soft_bodies = 0;                 ///< and the soft bodies' settings (GpuSoftBody).
    std::array<std::uint32_t, 3> reserved{};       ///< (Alignment.)
};
static_assert(sizeof(GpuStepParameters) == 128 && offsetof(GpuStepParameters, body_count) == 16 && offsetof(GpuStepParameters, body_centers) == 32 &&
              offsetof(GpuStepParameters, body_patches) == 76 && offsetof(GpuStepParameters, sleep_timers) == 88 &&
              offsetof(GpuStepParameters, soft_bodies) == 112);

/// A body written whole by the patch pass (BodyPatch in step.glsl): created, edited, woken or put to sleep, given
/// forces or impulses.
struct GpuBodyPatch
{
    std::uint32_t slot = 0;               ///< The body's slot.
    std::uint32_t flags = 0;              ///< Its flags (0: the slot is freed).
    std::array<std::uint32_t, 2> reserved{}; ///< (Alignment.)
    GpuVector center{};                   ///< Center of mass; w: inverse mass (0 unless dynamic).
    GpuVector orientation{};              ///< Quaternion x, y, z, w.
    GpuVector linear_velocity{};          ///< Of the center; w: linear damping.
    GpuVector angular_velocity{};         ///< w: angular damping.
    GpuVector inverse_inertia_diagonal{}; ///< Body frame: xx, yy, zz; w: gravity scale.
    GpuVector inverse_inertia_off{};      ///< Body frame: xy, xz, yz.
    GpuVector inertia_diagonal{};         ///< Body frame: xx, yy, zz.
    GpuVector inertia_off{};              ///< Body frame: xy, xz, yz.
    GpuVector force{};                    ///< This step's force (world space, at the center).
    GpuVector torque{};                   ///< This step's torque.
};
static_assert(sizeof(GpuBodyPatch) == 176 && offsetof(GpuBodyPatch, center) == 16 && offsetof(GpuBodyPatch, torque) == 160);

/// What the step reads back of a body (one array each, in this order in the readback buffer).
struct GpuBodyReadback
{
    GpuVector center{};           ///< w: inverse mass.
    GpuVector orientation{};
    GpuVector linear_velocity{};  ///< w: linear damping.
    GpuVector angular_velocity{}; ///< w: angular damping.
};

/// The specialization constants of the passes (ids as shaders/common.glsl and shaders/narrow_phase.glsl declare them;
/// floats by their bits): the workgroup size, the body flags, the invalid bindless index, the elements per invocation, the
/// radix bits; the BVHs' leaf bit, stack size, treelet leaves, node costs, Morton bits and overflow flag; the narrow
/// phase's shape kinds, contact algorithms, limits (face vertices, GJK iterations, EPA storage), the absent sub-shape,
/// the convex shapes' alignment thresholds and round faces, the geometric tolerance, the clipped polygons' capacity; the
/// kinds of triangle meshes, height fields and compounds, the triangle pairs' algorithm, the static hierarchies' stack, the
/// compounds descended at once, the triangles' edge-on tolerance; the solver's sleeping flag and colors; the joint part
/// kinds, the absent body (the world), the joint flags; the pin part, the soft-body constraint kinds; the touching slop.
inline constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 68> SPECIALIZATION_CONSTANTS{{
    {0, GPU_XPBD_WORKGROUP_SIZE},
    {1, std::to_underlying(GpuBodyFlag::SIMULATED)},
    {2, std::to_underlying(GpuBodyFlag::DYNAMIC)},
    {3, std::to_underlying(GpuBodyFlag::KINEMATIC)},
    {4, std::to_underlying(GpuBodyFlag::AWAKE)},
    {5, GPU_XPBD_BODY_LOCK_SHIFT},
    {6, INVALID_BINDLESS_INDEX},
    {7, GPU_XPBD_ITEMS_PER_INVOCATION},
    {8, GPU_XPBD_RADIX_BITS},
    {9, GPU_XPBD_BVH_LEAF_FLAG},
    {10, GPU_XPBD_BVH_STACK_SIZE},
    {11, GPU_XPBD_TREELET_LEAVES},
    {12, std::bit_cast<std::uint32_t>(GPU_XPBD_BVH_INTERNAL_COST)},
    {13, std::bit_cast<std::uint32_t>(GPU_XPBD_BVH_LEAF_COST)},
    {14, GPU_XPBD_MORTON_BITS},
    {15, std::to_underlying(GpuBvhFlag::STACK_OVERFLOW)},
    {16, std::to_underlying(ShapeKind::SPHERE)},
    {17, std::to_underlying(ShapeKind::CAPSULE)},
    {18, std::to_underlying(ShapeKind::CYLINDER)},
    {19, std::to_underlying(ShapeKind::CONE)},
    {20, std::to_underlying(ShapeKind::BOX)},
    {21, std::to_underlying(ShapeKind::CONVEX_HULL)},
    {22, std::to_underlying(ContactAlgorithm::SPHERE_SPHERE)},
    {23, std::to_underlying(ContactAlgorithm::SPHERE_CAPSULE)},
    {24, std::to_underlying(ContactAlgorithm::CAPSULE_CAPSULE)},
    {25, std::to_underlying(ContactAlgorithm::SPHERE_BOX)},
    {26, std::to_underlying(ContactAlgorithm::BOX_BOX)},
    {27, std::to_underlying(ContactAlgorithm::GJK)},
    {28, std::to_underlying(ContactAlgorithm::EPA)},
    {29, MAXIMUM_FACE_VERTICES},
    {30, MAXIMUM_GJK_ITERATIONS},
    {31, MAXIMUM_EPA_VERTICES},
    {32, MAXIMUM_EPA_FACES},
    {33, NO_SUB_SHAPE},
    {34, std::bit_cast<std::uint32_t>(shape_detail::FACE_ALIGNMENT)},
    {35, std::bit_cast<std::uint32_t>(shape_detail::SIDE_ALIGNMENT)},
    {36, shape_detail::ROUND_FACE_VERTICES},
    {37, std::bit_cast<std::uint32_t>(GEOMETRIC_TOLERANCE)},
    {38, 2 * MAXIMUM_FACE_VERTICES},
    {39, std::to_underlying(ShapeKind::TRIANGLE_MESH)},
    {40, std::to_underlying(ShapeKind::HEIGHT_FIELD)},
    {41, std::to_underlying(ShapeKind::COMPOUND)},
    {42, std::to_underlying(ContactAlgorithm::TRIANGLE_TRIANGLE)},
    {43, static_cast<std::uint32_t>(StaticBvh::MAXIMUM_DEPTH)},
    {44, 2 * GPU_XPBD_COMPOUND_DEPTH},
    {45, std::bit_cast<std::uint32_t>(TRIANGLE_FACE_TOLERANCE)},
    {46, std::to_underlying(GpuBodyFlag::SLEEPS)},
    {47, MAXIMUM_GPU_XPBD_COLORS},
    {48, std::to_underlying(GpuJointPartKind::ANCHORS)},
    {49, std::to_underlying(GpuJointPartKind::DISTANCE)},
    {50, std::to_underlying(GpuJointPartKind::ALIGNED_AXIS)},
    {51, std::to_underlying(GpuJointPartKind::ORIENTATION)},
    {52, std::to_underlying(GpuJointPartKind::ANGLE)},
    {53, std::to_underlying(GpuJointPartKind::SWING)},
    {54, std::to_underlying(GpuJointPartKind::LINEAR_MOTOR)},
    {55, std::to_underlying(GpuJointPartKind::ANGULAR_MOTOR)},
    {56, NO_SOLVER_BODY},
    {57, std::to_underlying(GpuJointFlag::ACTIVE)},
    {58, std::to_underlying(GpuJointFlag::SOLVED)},
    {59, std::to_underlying(GpuJointFlag::BROKEN)},
    {60, std::to_underlying(GpuJointPartKind::PIN)},
    {61, std::to_underlying(SoftBodyConstraintKind::STRETCH)},
    {62, std::to_underlying(SoftBodyConstraintKind::SHEAR)},
    {63, std::to_underlying(SoftBodyConstraintKind::BENDING)},
    {64, std::to_underlying(SoftBodyConstraintKind::VOLUME)},
    {65, std::to_underlying(SoftBodyConstraintKind::DIHEDRAL)},
    {66, std::to_underlying(SoftBodyConstraintKind::TETHER)},
    {67, std::bit_cast<std::uint32_t>(LINEAR_SLOP)},
}};
static_assert(MAXIMUM_EPA_VERTICES <= 256 && MAXIMUM_FACE_VERTICES <= 32,
              "the GPU's EPA packs a face's corners in 8 bits each; contact features keep 5 bits of a face's vertices");

/// Workgroups covering @p count invocations.
[[nodiscard]] constexpr auto
workgroups(std::uint32_t count) noexcept -> std::uint32_t
{
    return (count + GPU_XPBD_WORKGROUP_SIZE - 1) / GPU_XPBD_WORKGROUP_SIZE;
}

// ===================================================================================================== buffers

/// A device-local array of fixed-size elements, read and written by the passes through its bindless index, grown by
/// powers of two (the old contents copied into the new buffer by the step that grows it).
class GpuArray
{
public:
    /// @param usage What the buffer is used for beyond storage and copies (INDIRECT for dispatch arguments).
    GpuArray(std::string_view name,
             std::uint32_t element_size,
             BufferUsage usage = BufferUsage::NONE);

    /// Makes room for @p count elements: a larger buffer, the first @p kept elements of the old one copied into it by
    /// @p recorder, the old buffer moved into @p retired (the copy reads it: it lives until the step completes).
    /// @return Whether it grew.
    auto reserve(Device& device,
                 ComputeRecorder& recorder,
                 std::uint32_t count,
                 std::uint32_t kept,
                 std::vector<std::unique_ptr<Buffer>>& retired) -> bool;

    [[nodiscard]] auto buffer() const noexcept -> const Buffer&;
    [[nodiscard]] auto index() const noexcept -> std::uint32_t;
    [[nodiscard]] auto capacity() const noexcept -> std::uint32_t;
    [[nodiscard]] auto element_size() const noexcept -> std::uint32_t;

private:
    std::string m_name{};
    std::uint32_t m_element_size = 0;
    BufferUsage m_usage = BufferUsage::NONE;
    std::uint32_t m_capacity = 0;
    std::unique_ptr<Buffer> m_buffer{};
};

/// A persistently mapped buffer the CPU writes (HOST_UPLOAD, read by the shaders) or reads (HOST_READBACK, written by
/// copies), grown by powers of two between steps.
class HostBuffer
{
public:
    HostBuffer(std::string_view name,
               MemoryLocation location,
               BufferUsage usage);

    /// Makes room for @p size bytes (contents lost when it grows; the old buffer moved into @p retired).
    void reserve(Device& device,
                 std::uint64_t size,
                 std::vector<std::unique_ptr<Buffer>>& retired);

    [[nodiscard]] auto buffer() noexcept -> Buffer&;
    [[nodiscard]] auto index() const noexcept -> std::uint32_t;
    [[nodiscard]] auto bytes() noexcept -> std::span<std::byte>;

private:
    std::string m_name{};
    MemoryLocation m_location = MemoryLocation::HOST_UPLOAD;
    BufferUsage m_usage = BufferUsage::NONE;
    std::unique_ptr<Buffer> m_buffer{};
};

// ===================================================================================================== GPU timing

/// The sites of a step's phases as GPU scopes (by StepPhase; EDITS is the framework's, on the CPU).
inline constexpr std::array<profiler::ProfileSite, ENUMERATOR_COUNT<StepPhase>> PHASE_SITES{
    profiler::ProfileSite{"GPU XPBD: edits", profiler::ProfilerCategory::GPU},
    profiler::ProfileSite{"GPU XPBD: broad phase", profiler::ProfilerCategory::GPU},
    profiler::ProfileSite{"GPU XPBD: narrow phase", profiler::ProfilerCategory::GPU},
    profiler::ProfileSite{"GPU XPBD: solver", profiler::ProfilerCategory::GPU},
    profiler::ProfileSite{"GPU XPBD: soft bodies", profiler::ProfilerCategory::GPU},
    profiler::ProfileSite{"GPU XPBD: events", profiler::ProfilerCategory::GPU},
};

/// The phases of a step (StepStatistics::phase_seconds) as GPU scopes of the step's GpuTimer, timed in every build when
/// the queue writes timestamps; the passes' scopes nest in them. Each mark ends the phase begun at the mark before and
/// begins another; after the step (GpuTimer::finish_frame()), the phases' scopes summed.
class PhaseTimer
{
public:
    /// @param timer The step's timer (it outlives this).
    explicit PhaseTimer(profiler::GpuTimer& timer) noexcept;

    /// Whether the queue writes timestamps (else the phases are not timed on the GPU).
    [[nodiscard]] auto enabled() const noexcept -> bool;

    /// Begins @p phase, ending the one begun before (nothing when @p phase is the one begun).
    void mark(ComputeRecorder& recorder,
              StepPhase phase);

    /// Ends the phase begun last.
    void end(ComputeRecorder& recorder);

    /// After the timer read the step's frame: the seconds of each phase marked into @p seconds (the others untouched).
    void read(std::span<double, ENUMERATOR_COUNT<StepPhase>> seconds);

private:
    profiler::GpuTimer* m_timer;
    std::optional<StepPhase> m_phase{};
    profiler::GpuScopeToken m_token{};
    std::vector<profiler::GpuScopeRecord> m_scopes{}; ///< Read back (scratch).
};

/// Closes a pass's GPU scope (PassTimer::scope()) when destroyed.
class PassScope
{
public:
    PassScope(profiler::GpuTimer* timer,
              ComputeRecorder& recorder,
              profiler::GpuScopeToken token) noexcept
        : m_timer{timer}
        , m_recorder{&recorder}
        , m_token{token}
    {
    }

    ~PassScope()
    {
        if (m_timer != nullptr)
        {
            m_timer->end_scope(*m_recorder, m_token);
        }
    }

    PassScope(const PassScope&) = delete;
    PassScope(PassScope&&) = delete;
    auto operator=(const PassScope&) -> PassScope& = delete;
    auto operator=(PassScope&&) -> PassScope& = delete;

private:
    profiler::GpuTimer* m_timer;
    ComputeRecorder* m_recorder;
    profiler::GpuScopeToken m_token;
};

/// What the passes of a component are timed through (the provider of PROFILE_GPU_SCOPE): the step's GpuTimer (each scope
/// timed while a profiler records the GPU category with the timer added), or nothing (a component used alone).
class PassTimer
{
public:
    void set(profiler::GpuTimer* timer) noexcept
    {
        m_timer = timer;
    }

    [[nodiscard]] auto timer() const noexcept -> profiler::GpuTimer*
    {
        return m_timer;
    }

    /// Opens a GPU scope of @p site in @p recorder's commands, closed when the returned object is destroyed.
    [[nodiscard]] auto scope(ComputeRecorder& recorder,
                             const profiler::ProfileSite& site) const -> PassScope
    {
        if (m_timer == nullptr)
        {
            return PassScope{nullptr, recorder, {}};
        }
        return PassScope{m_timer, recorder, m_timer->begin_scope(recorder, site)};
    }

private:
    profiler::GpuTimer* m_timer = nullptr;
};

// ===================================================================================================== pipelines

/// A compute pipeline of @p description's shader with the specialization constants it declares (the module kept in
/// @p modules: it lives as long as the pipeline) — or, while a PipelineLibraryScope of @p device's library is open on
/// this thread, the library's (made once per device, @p modules untouched).
[[nodiscard]] auto make_pipeline(Device& device,
                                 const ShaderModuleDescription& description,
                                 std::vector<std::unique_ptr<ShaderModule>>& modules) -> std::shared_ptr<ComputePipeline>;

/// The compute pipelines of a device and their shader modules, shared by the GPU worlds made on it with SHARED_PIPELINES
/// (each pipeline made at its first request, through a pipeline cache read from and written to a file when one is named).
/// Kept while a world holds it; thread-safe.
class GpuPipelineLibrary
{
public:
    /// The library of @p device: the one a world of the device holds, else a new one (its cache read from
    /// @p cache_path when given and readable: a file another driver wrote leaves the cache empty).
    [[nodiscard]] static auto of(Device& device,
                                 const std::filesystem::path& cache_path) -> std::shared_ptr<GpuPipelineLibrary>;

    /// What the library of @p device holds now (zeros when there is none).
    [[nodiscard]] static auto statistics(const Device& device) -> SharedPipelineStatistics;

    GpuPipelineLibrary(Device& device,
                       std::filesystem::path cache_path);

    /// Writes the cache file (save()), then releases the pipelines (the device's deleter frees what the GPU may use).
    ~GpuPipelineLibrary();

    GpuPipelineLibrary(const GpuPipelineLibrary&) = delete;
    auto operator=(const GpuPipelineLibrary&) -> GpuPipelineLibrary& = delete;

    [[nodiscard]] auto device() const noexcept -> const Device&
    {
        return m_device;
    }

    /// The pipeline of @p description's shader (made at the first request).
    [[nodiscard]] auto pipeline(const ShaderModuleDescription& description) -> std::shared_ptr<ComputePipeline>;

    /// Writes the cache to its file when pipelines were made since it was read or written (a failure is ignored: the
    /// file only saves time).
    void save() noexcept;

private:
    /// A pipeline made, by its shader's description (the generated description objects live as long as the program).
    struct Made
    {
        const ShaderModuleDescription* description = nullptr;
        std::shared_ptr<ComputePipeline> pipeline{};
    };

    Device& m_device;
    std::filesystem::path m_cache_path{};
    std::unique_ptr<PipelineCache> m_cache{};
    bool m_cache_file_loaded = false;
    std::mutex m_mutex{};
    std::vector<std::unique_ptr<ShaderModule>> m_modules{};
    std::vector<Made> m_made{};
    bool m_unsaved = false;
    std::uint32_t m_cache_writes = 0;
};

/// While it lives (or until end()), make_pipeline() on this thread takes the pipelines of @p library's device from it
/// (null: makes them, as without a library). Scopes nest: end() restores the enclosing one.
class PipelineLibraryScope
{
public:
    explicit PipelineLibraryScope(GpuPipelineLibrary* library) noexcept;
    ~PipelineLibraryScope();

    PipelineLibraryScope(const PipelineLibraryScope&) = delete;
    auto operator=(const PipelineLibraryScope&) -> PipelineLibraryScope& = delete;

    /// Closes it (once).
    void end() noexcept;

private:
    GpuPipelineLibrary* m_enclosing = nullptr;
    bool m_open = false;
};

/// The compute pipelines of a step's passes.
class GpuPipelines
{
public:
    explicit GpuPipelines(Device& device);

    [[nodiscard]] auto patch_bodies() const noexcept -> ComputePipelineHandle;
    [[nodiscard]] auto integrate() const noexcept -> ComputePipelineHandle;
    [[nodiscard]] auto update_velocities() const noexcept -> ComputePipelineHandle;

private:
    std::vector<std::unique_ptr<ShaderModule>> m_modules{};
    std::shared_ptr<ComputePipeline> m_patch_bodies{};
    std::shared_ptr<ComputePipeline> m_integrate{};
    std::shared_ptr<ComputePipeline> m_update_velocities{};
};

// ===================================================================================================== barriers

/// The barrier between two passes of the step: every shader write before is visible to every shader read and write after.
inline constexpr GlobalMemoryBarrier COMPUTE_TO_COMPUTE{.source_stages = PipelineStage::COMPUTE_SHADER,
                                                         .source_access = MemoryAccess::SHADER_STORAGE_WRITE,
                                                         .destination_stages = PipelineStage::COMPUTE_SHADER,
                                                         .destination_access = MemoryAccess::SHADER_STORAGE_READ | MemoryAccess::SHADER_STORAGE_WRITE};

/// The step's first barrier: whatever earlier submissions and this step's copies and fills wrote, before its passes.
inline constexpr GlobalMemoryBarrier BEFORE_PASSES{.source_stages = PipelineStage::ALL_COMMANDS,
                                                    .source_access = MemoryAccess::TRANSFER_WRITE | MemoryAccess::SHADER_STORAGE_WRITE,
                                                    .destination_stages = PipelineStage::COMPUTE_SHADER | PipelineStage::TRANSFER,
                                                    .destination_access = MemoryAccess::SHADER_STORAGE_READ | MemoryAccess::SHADER_STORAGE_WRITE |
                                                                          MemoryAccess::TRANSFER_READ | MemoryAccess::TRANSFER_WRITE};

/// The copies and fills of arrays that grew (GpuArray::reserve()), before other copies and fills write the same arrays
/// (the step's cleared forces, the colliders' upload): transfers write in no particular order without it.
inline constexpr GlobalMemoryBarrier TRANSFER_TO_TRANSFER{.source_stages = PipelineStage::TRANSFER,
                                                           .source_access = MemoryAccess::TRANSFER_WRITE,
                                                           .destination_stages = PipelineStage::TRANSFER,
                                                           .destination_access = MemoryAccess::TRANSFER_READ | MemoryAccess::TRANSFER_WRITE};

/// The passes' writes, before the copies into the readback buffer and the fills that clear this step's forces.
inline constexpr GlobalMemoryBarrier COMPUTE_TO_TRANSFER{.source_stages = PipelineStage::COMPUTE_SHADER,
                                                          .source_access = MemoryAccess::SHADER_STORAGE_WRITE,
                                                          .destination_stages = PipelineStage::TRANSFER,
                                                          .destination_access = MemoryAccess::TRANSFER_READ | MemoryAccess::TRANSFER_WRITE};

/// The copies into the readback buffer, before the CPU reads it.
inline constexpr GlobalMemoryBarrier TRANSFER_TO_HOST{.source_stages = PipelineStage::TRANSFER,
                                                       .source_access = MemoryAccess::TRANSFER_WRITE,
                                                       .destination_stages = PipelineStage::HOST,
                                                       .destination_access = MemoryAccess::HOST_READ};

/// A pass's writes, before indirect dispatches read their arguments (and later compute passes everything).
inline constexpr GlobalMemoryBarrier COMPUTE_TO_INDIRECT{.source_stages = PipelineStage::COMPUTE_SHADER,
                                                          .source_access = MemoryAccess::SHADER_STORAGE_WRITE,
                                                          .destination_stages = PipelineStage::DRAW_INDIRECT | PipelineStage::COMPUTE_SHADER,
                                                          .destination_access = MemoryAccess::INDIRECT_COMMAND_READ | MemoryAccess::SHADER_STORAGE_READ |
                                                                                MemoryAccess::SHADER_STORAGE_WRITE};

/// Records a pass: its pipeline, its push constants, @p workgroup_count workgroups, then the barrier to the next pass.
template<class Push>
void
record_pass(ComputeRecorder& recorder,
            const ComputePipeline& pipeline,
            const Push& push,
            std::uint32_t workgroup_count,
            const GlobalMemoryBarrier& barrier = COMPUTE_TO_COMPUTE)
{
    recorder.bind_pipeline(pipeline.handle());
    recorder.push_constants(push);
    recorder.dispatch(workgroup_count);
    recorder.barrier({.memory_barriers = std::span{&barrier, 1}});
}

} // namespace gfx::physics::gpu_xpbd
