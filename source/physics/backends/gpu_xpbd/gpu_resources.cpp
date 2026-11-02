// The GPU backend's resources: growable device arrays, mapped host buffers, the compute pipelines of the passes.
module gfx.physics.gpu_xpbd;

import std;
import gfx.enums;
import gfx.handles;
import gfx.interface;
import gfx.physics.enums;
import gfx.physics.gpu_xpbd.shaders;
import :resources;

namespace gfx::physics::gpu_xpbd
{

namespace
{

/// The capacity of a buffer holding @p count elements: a power of two, at least @p minimum.
[[nodiscard]] auto
grown_capacity(std::uint64_t count,
               std::uint64_t minimum) noexcept -> std::uint64_t
{
    return std::bit_ceil(std::max(count, minimum));
}

} // namespace

// ===================================================================================================== GpuArray

GpuArray::GpuArray(std::string_view name,
                   std::uint32_t element_size,
                   BufferUsage usage)
    : m_name{name}
    , m_element_size{element_size}
    , m_usage{usage}
{
}

auto
GpuArray::reserve(Device& device,
                  ComputeRecorder& recorder,
                  std::uint32_t count,
                  std::uint32_t kept,
                  std::vector<std::unique_ptr<Buffer>>& retired) -> bool
{
    if (m_buffer != nullptr && count <= m_capacity)
    {
        return false;
    }

    // A buffer twice as large at least; the kept elements copied, the rest zeroed (free slots, no force).
    const auto capacity = static_cast<std::uint32_t>(grown_capacity(count, 64));
    std::unique_ptr<Buffer> buffer = device.create_buffer({.size = std::uint64_t{capacity} * m_element_size,
                                                           .usage = BufferUsage::STORAGE | BufferUsage::TRANSFER_SOURCE | BufferUsage::TRANSFER_DESTINATION | m_usage,
                                                           .debug_name = m_name});
    const std::uint32_t copied = m_buffer != nullptr ? std::min(kept, m_capacity) : 0;
    if (copied > 0)
    {
        const std::array regions{BufferCopyRegion{.size = std::uint64_t{copied} * m_element_size}};
        recorder.copy_buffer(m_buffer->handle(), buffer->handle(), regions);
    }
    recorder.fill_buffer(buffer->handle(), std::uint64_t{copied} * m_element_size, WHOLE_SIZE, 0);
    if (m_buffer != nullptr)
    {
        retired.push_back(std::move(m_buffer));
    }
    m_buffer = std::move(buffer);
    m_capacity = capacity;
    return true;
}

auto
GpuArray::buffer() const noexcept -> const Buffer&
{
    return *m_buffer;
}

auto
GpuArray::index() const noexcept -> std::uint32_t
{
    return m_buffer->bindless_index().value();
}

auto
GpuArray::capacity() const noexcept -> std::uint32_t
{
    return m_capacity;
}

auto
GpuArray::element_size() const noexcept -> std::uint32_t
{
    return m_element_size;
}

// ===================================================================================================== HostBuffer

StepTimer::StepTimer(Device& device,
                     bool enabled,
                     std::uint32_t family)
{
    // A pool of GPU_XPBD_TIMESTAMPS queries; the ticks' length and meaningful bits from the device.
    if (!enabled)
    {
        return;
    }
    const PhysicalDevice& physical_device = device.physical_device();
    const std::span<const QueueFamilyProperties> families = physical_device.queue_families();
    const std::uint32_t bits = family < families.size() ? families[family].timestamp_valid_bits : 0;
    if (bits == 0)
    {
        return;
    }
    m_pool = device.create_query_pool({.type = QueryType::TIMESTAMP, .count = GPU_XPBD_TIMESTAMPS, .debug_name = "GPU physics: step phases"});
    m_nanoseconds_per_tick = physical_device.properties().limits.timestamp_period_nanoseconds;
    m_mask = bits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1;
    m_phases.reserve(GPU_XPBD_TIMESTAMPS);
    m_ticks.reserve(GPU_XPBD_TIMESTAMPS);
}

auto
StepTimer::enabled() const noexcept -> bool
{
    return m_pool != nullptr;
}

void
StepTimer::begin(ComputeRecorder& recorder)
{
    m_phases.clear();
    m_ended = false;
    if (m_pool != nullptr)
    {
        recorder.reset_query_pool(m_pool->handle(), 0, GPU_XPBD_TIMESTAMPS);
    }
}

void
StepTimer::mark(ComputeRecorder& recorder,
                StepPhase phase)
{
    // One query kept for the end.
    if (m_pool == nullptr || m_ended || (!m_phases.empty() && m_phases.back() == phase) || m_phases.size() + 1 >= GPU_XPBD_TIMESTAMPS)
    {
        return;
    }
    recorder.write_timestamp(m_pool->handle(), PipelineStage::ALL_COMMANDS, static_cast<std::uint32_t>(m_phases.size()));
    m_phases.push_back(phase);
}

void
StepTimer::end(ComputeRecorder& recorder)
{
    if (m_pool == nullptr || m_ended || m_phases.empty())
    {
        return;
    }
    recorder.write_timestamp(m_pool->handle(), PipelineStage::ALL_COMMANDS, static_cast<std::uint32_t>(m_phases.size()));
    m_ended = true;
}

void
StepTimer::read(std::span<double, ENUMERATOR_COUNT<StepPhase>> seconds)
{
    // Each phase's segments summed (ticks wrap within their valid bits).
    if (m_pool == nullptr || !m_ended)
    {
        return;
    }
    m_ticks.resize(m_phases.size() + 1);
    if (!m_pool->results(0, m_ticks, true))
    {
        return;
    }
    std::array<bool, ENUMERATOR_COUNT<StepPhase>> marked{};
    for (const StepPhase phase : m_phases)
    {
        const auto index = static_cast<std::size_t>(std::to_underlying(phase));
        if (!marked[index])
        {
            seconds[index] = 0.0;
            marked[index] = true;
        }
    }
    for (std::size_t segment = 0; segment < m_phases.size(); ++segment)
    {
        const std::uint64_t ticks = (m_ticks[segment + 1] - m_ticks[segment]) & m_mask;
        seconds[static_cast<std::size_t>(std::to_underlying(m_phases[segment]))] += static_cast<double>(ticks) * m_nanoseconds_per_tick * 1.0e-9;
    }
}

HostBuffer::HostBuffer(std::string_view name,
                       MemoryLocation location,
                       BufferUsage usage)
    : m_name{name}
    , m_location{location}
    , m_usage{usage}
{
}

void
HostBuffer::reserve(Device& device,
                    std::uint64_t size,
                    std::vector<std::unique_ptr<Buffer>>& retired)
{
    if (m_buffer != nullptr && size <= m_buffer->size())
    {
        return;
    }
    std::unique_ptr<Buffer> buffer =
        device.create_buffer({.size = grown_capacity(size, 256), .usage = m_usage, .location = m_location, .debug_name = m_name});
    if (m_buffer != nullptr)
    {
        retired.push_back(std::move(m_buffer));
    }
    m_buffer = std::move(buffer);
}

auto
HostBuffer::buffer() noexcept -> Buffer&
{
    return *m_buffer;
}

auto
HostBuffer::index() const noexcept -> std::uint32_t
{
    return m_buffer->bindless_index().value();
}

auto
HostBuffer::bytes() noexcept -> std::span<std::byte>
{
    return m_buffer->mapped_memory();
}

// ===================================================================================================== GpuPipelines

auto
make_pipeline(Device& device,
              const ShaderModuleDescription& description,
              std::vector<std::unique_ptr<ShaderModule>>& modules) -> std::unique_ptr<ComputePipeline>
{
    // The constants the shader declares, from gfx.physics.enums (a shader declares those it uses only).
    std::unique_ptr<ShaderModule> module = device.create_shader_module(description);
    std::vector<SpecializationConstant> constants;
    for (const auto& [id, value] : SPECIALIZATION_CONSTANTS)
    {
        const auto declared = [id](const SpecializationConstantInfo& constant) { return constant.id == id; };
        if (std::ranges::any_of(module->layout().specialization_constants, declared))
        {
            constants.push_back(specialization_constant(id, value));
        }
    }
    std::unique_ptr<ComputePipeline> pipeline =
        device.create_compute_pipeline({.shader = module->handle(), .specialization_constants = constants, .debug_name = description.name});
    modules.push_back(std::move(module));
    return pipeline;
}

GpuPipelines::GpuPipelines(Device& device)
    : m_patch_bodies{make_pipeline(device, shaders::patch_bodies_comp, m_modules)}
    , m_integrate{make_pipeline(device, shaders::xpbd_integrate_comp, m_modules)}
    , m_update_velocities{make_pipeline(device, shaders::xpbd_velocity_update_comp, m_modules)}
{
}

auto
GpuPipelines::patch_bodies() const noexcept -> ComputePipelineHandle
{
    return m_patch_bodies->handle();
}

auto
GpuPipelines::integrate() const noexcept -> ComputePipelineHandle
{
    return m_integrate->handle();
}

auto
GpuPipelines::update_velocities() const noexcept -> ComputePipelineHandle
{
    return m_update_velocities->handle();
}

} // namespace gfx::physics::gpu_xpbd
