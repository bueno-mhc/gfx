// The GPU backend's resources: growable device arrays, mapped host buffers, the compute pipelines of the passes.
module gfx.physics.gpu_xpbd;

import std;
import gfx.enums;
import gfx.handles;
import gfx.interface;
import gfx.profiler.graphics;
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

// ===================================================================================================== PhaseTimer

PhaseTimer::PhaseTimer(profiler::GpuTimer& timer) noexcept
    : m_timer{&timer}
{
}

auto
PhaseTimer::enabled() const noexcept -> bool
{
    return m_timer->alignment() != profiler::GpuClockAlignment::UNAVAILABLE;
}

void
PhaseTimer::mark(ComputeRecorder& recorder,
                 StepPhase phase)
{
    if (m_phase == phase)
    {
        return;
    }
    end(recorder);
    m_token = m_timer->begin_scope(recorder, PHASE_SITES[std::to_underlying(phase)], profiler::GpuScopeTiming::ALWAYS);
    m_phase = phase;
}

void
PhaseTimer::end(ComputeRecorder& recorder)
{
    if (m_phase)
    {
        m_timer->end_scope(recorder, m_token);
        m_phase.reset();
    }
}

void
PhaseTimer::read(std::span<double, ENUMERATOR_COUNT<StepPhase>> seconds)
{
    // Each phase's timed scopes summed.
    m_scopes.clear();
    m_timer->latest(m_scopes);
    std::array<bool, ENUMERATOR_COUNT<StepPhase>> marked{};
    for (const profiler::GpuScopeRecord& scope : m_scopes)
    {
        for (std::size_t phase = 0; phase < PHASE_SITES.size(); ++phase)
        {
            if (scope.site != &PHASE_SITES[phase] || scope.status != profiler::GpuScopeStatus::TIMED)
            {
                continue;
            }
            if (!marked[phase])
            {
                seconds[phase] = 0.0;
                marked[phase] = true;
            }
            seconds[phase] += static_cast<double>(scope.end - scope.begin) * 1.0e-9;
        }
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
