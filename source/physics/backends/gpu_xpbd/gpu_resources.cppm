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
    std::uint32_t body_patches = 0;                ///< and of this step's patches.
};
static_assert(sizeof(GpuStepParameters) == 80 && offsetof(GpuStepParameters, body_count) == 16 && offsetof(GpuStepParameters, body_centers) == 32 &&
              offsetof(GpuStepParameters, body_patches) == 76);

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

/// The specialization constants of the passes (ids as shaders/common.glsl declares them): the workgroup size, the body
/// flags, the invalid bindless index, the elements per invocation and the radix bits.
inline constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 9> SPECIALIZATION_CONSTANTS{{
    {0, GPU_XPBD_WORKGROUP_SIZE},
    {1, std::to_underlying(GpuBodyFlag::SIMULATED)},
    {2, std::to_underlying(GpuBodyFlag::DYNAMIC)},
    {3, std::to_underlying(GpuBodyFlag::KINEMATIC)},
    {4, std::to_underlying(GpuBodyFlag::AWAKE)},
    {5, GPU_XPBD_BODY_LOCK_SHIFT},
    {6, INVALID_BINDLESS_INDEX},
    {7, GPU_XPBD_ITEMS_PER_INVOCATION},
    {8, GPU_XPBD_RADIX_BITS},
}};

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
    GpuArray(std::string_view name,
             std::uint32_t element_size);

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

} // namespace gfx::physics::gpu_xpbd
