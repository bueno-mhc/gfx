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

namespace
{

/// The library make_pipeline() takes pipelines from on this thread (PipelineLibraryScope).
thread_local GpuPipelineLibrary* t_library = nullptr;

/// A pipeline of @p description's shader with the specialization constants it declares, through @p cache (null: the
/// device's own), its module kept in @p modules.
auto
create_pipeline(Device& device,
                const ShaderModuleDescription& description,
                std::vector<std::unique_ptr<ShaderModule>>& modules,
                PipelineCacheHandle cache) -> std::shared_ptr<ComputePipeline>
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
    std::shared_ptr<ComputePipeline> pipeline =
        device.create_compute_pipeline({.shader = module->handle(), .specialization_constants = constants, .cache = cache, .debug_name = description.name});
    modules.push_back(std::move(module));
    return pipeline;
}

/// The libraries of the devices that have one (a device's library lives while one of its worlds holds it).
struct Libraries
{
    std::mutex mutex{};
    std::vector<std::pair<const Device*, std::weak_ptr<GpuPipelineLibrary>>> entries{};
};

auto
libraries() -> Libraries&
{
    static Libraries all;
    return all;
}

} // namespace

auto
make_pipeline(Device& device,
              const ShaderModuleDescription& description,
              std::vector<std::unique_ptr<ShaderModule>>& modules) -> std::shared_ptr<ComputePipeline>
{
    if (t_library != nullptr && &t_library->device() == &device)
    {
        return t_library->pipeline(description);
    }
    return create_pipeline(device, description, modules, {});
}

auto
GpuPipelineLibrary::of(Device& device,
                       const std::filesystem::path& cache_path) -> std::shared_ptr<GpuPipelineLibrary>
{
    Libraries& all = libraries();
    const std::scoped_lock lock{all.mutex};
    std::erase_if(all.entries, [](const auto& entry) { return entry.second.expired(); });
    for (const auto& [owner, library] : all.entries)
    {
        if (owner == &device)
        {
            if (std::shared_ptr<GpuPipelineLibrary> held = library.lock())
            {
                return held;
            }
        }
    }
    auto made = std::make_shared<GpuPipelineLibrary>(device, cache_path);
    all.entries.emplace_back(&device, made);
    return made;
}

auto
GpuPipelineLibrary::statistics(const Device& device) -> SharedPipelineStatistics
{
    Libraries& all = libraries();
    const std::scoped_lock lock{all.mutex};
    for (const auto& [owner, library] : all.entries)
    {
        if (owner != &device)
        {
            continue;
        }
        if (const std::shared_ptr<GpuPipelineLibrary> held = library.lock())
        {
            // The worlds are the holders but this one.
            const std::scoped_lock library_lock{held->m_mutex};
            return {.worlds = static_cast<std::uint32_t>(library.use_count() - 1),
                    .pipelines = static_cast<std::uint32_t>(held->m_made.size()),
                    .cache_file_loaded = held->m_cache_file_loaded,
                    .cache_writes = held->m_cache_writes};
        }
    }
    return {};
}

GpuPipelineLibrary::GpuPipelineLibrary(Device& device,
                                       std::filesystem::path cache_path)
    : m_device{device}
    , m_cache_path{std::move(cache_path)}
{
    // The cache file's bytes, when there is one to read (else, or unreadable: an empty cache).
    std::vector<std::byte> data;
    if (!m_cache_path.empty())
    {
        std::ifstream file{m_cache_path, std::ios::binary};
        if (file)
        {
            const std::vector<char> characters{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
            data.resize(characters.size());
            std::memcpy(data.data(), characters.data(), characters.size());
        }
    }
    m_cache = device.create_pipeline_cache({.initial_data = data, .debug_name = "GPU physics pipelines"});
    m_cache_file_loaded = !data.empty() && m_cache->loaded_initial_data();
}

GpuPipelineLibrary::~GpuPipelineLibrary()
{
    save();
}

auto
GpuPipelineLibrary::pipeline(const ShaderModuleDescription& description) -> std::shared_ptr<ComputePipeline>
{
    const std::scoped_lock lock{m_mutex};
    for (const Made& made : m_made)
    {
        if (made.description == &description)
        {
            return made.pipeline;
        }
    }
    std::shared_ptr<ComputePipeline> pipeline = create_pipeline(m_device, description, m_modules, m_cache->handle());
    m_made.push_back({.description = &description, .pipeline = pipeline});
    m_unsaved = true;
    return pipeline;
}

void
GpuPipelineLibrary::save() noexcept
{
    const std::scoped_lock lock{m_mutex};
    if (m_cache_path.empty() || !m_unsaved)
    {
        return;
    }
    try
    {
        // Written beside, then renamed over the file: a reader never sees half of it.
        const std::vector<std::byte> data = m_cache->serialize();
        std::filesystem::path written = m_cache_path;
        written += ".writing";
        {
            std::ofstream file{written, std::ios::binary | std::ios::trunc};
            file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!file)
            {
                return;
            }
        }
        std::filesystem::rename(written, m_cache_path);
        m_unsaved = false;
        ++m_cache_writes;
    }
    catch (...)
    {
        // The file only saves time: a world without it makes its pipelines as before.
    }
}

PipelineLibraryScope::PipelineLibraryScope(GpuPipelineLibrary* library) noexcept
    : m_enclosing{t_library}
    , m_open{true}
{
    t_library = library;
}

PipelineLibraryScope::~PipelineLibraryScope()
{
    end();
}

void
PipelineLibraryScope::end() noexcept
{
    if (m_open)
    {
        t_library = m_enclosing;
        m_open = false;
    }
}

GpuPipelines::GpuPipelines(Device& device)
    : m_device{&device}
    , m_library{t_library != nullptr && &t_library->device() == &device ? t_library : nullptr}
    , m_patch_bodies{make_pipeline(device, shaders::patch_bodies_comp, m_modules)}
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

auto
GpuPipelines::readback_compact() -> ComputePipelineHandle
{
    if (m_readback_compact == nullptr)
    {
        m_readback_compact = m_library != nullptr ? m_library->pipeline(shaders::readback_compact_comp)
                                                  : create_pipeline(*m_device, shaders::readback_compact_comp, m_modules, {});
    }
    return m_readback_compact->handle();
}

auto
GpuPipelines::update_velocities_fused() -> ComputePipelineHandle
{
    if (m_update_velocities_fused == nullptr)
    {
        m_update_velocities_fused = m_library != nullptr ? m_library->pipeline(shaders::velocity_update_fused_comp)
                                                         : create_pipeline(*m_device, shaders::velocity_update_fused_comp, m_modules, {});
    }
    return m_update_velocities_fused->handle();
}

} // namespace gfx::physics::gpu_xpbd
