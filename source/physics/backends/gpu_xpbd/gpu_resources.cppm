// The GPU backend's internal partition of GPU resources: the layouts the CPU and the shaders share (each checked here by
// size and offset; shaders/step.glsl declares the same), growable device arrays and host buffers, and the compute
// pipelines of the passes.
module;

#include <stddef.h>

module gfx.physics.gpu_xpbd:resources;

import std;
import gfx.enums;
import gfx.handles;
import gfx.interface;
import gfx.physics.enums;
import gfx.physics.shapes;

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
    std::uint32_t reserved = 0;                    ///< (Alignment.)
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
    std::uint32_t sleep_timers = 0;                ///< and the seconds each body stayed slow.
    std::uint32_t reserved_1 = 0;                  ///< (Alignment.)
};
static_assert(sizeof(GpuStepParameters) == 96 && offsetof(GpuStepParameters, body_count) == 16 && offsetof(GpuStepParameters, body_centers) == 32 &&
              offsetof(GpuStepParameters, body_patches) == 76 && offsetof(GpuStepParameters, sleep_timers) == 88);

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
/// kinds, the absent body (the world), the joint flags.
inline constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 60> SPECIALIZATION_CONSTANTS{{
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

// ===================================================================================================== pipelines

/// A compute pipeline of @p description's shader with the specialization constants it declares (the module kept in
/// @p modules: it lives as long as the pipeline).
[[nodiscard]] auto make_pipeline(Device& device,
                                 const ShaderModuleDescription& description,
                                 std::vector<std::unique_ptr<ShaderModule>>& modules) -> std::unique_ptr<ComputePipeline>;

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
    std::unique_ptr<ComputePipeline> m_patch_bodies{};
    std::unique_ptr<ComputePipeline> m_integrate{};
    std::unique_ptr<ComputePipeline> m_update_velocities{};
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
