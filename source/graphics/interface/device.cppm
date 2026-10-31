/// @file device.cppm
/// @brief Device: the logical device. It creates every resource, pipeline, command pool and
///        synchronization object, and owns the queues and the deferred-destruction queue.
export module gfx.interface:device;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import :backend_object;
import :common_types;
import :shader_layout;
import :device_memory;
import :buffer;
import :image;
import :image_view;
import :sampler;
import :shader_module;
import :descriptor_set_layout;
import :pipeline_layout;
import :descriptor_set;
import :descriptor_pool;
import :compute_pipeline;
import :graphics_pipeline;
import :command_buffer;
import :command_pool;
import :fence;
import :semaphore;
import :timeline_semaphore;
import :event;
import :query_pool;
import :queue;
import :swapchain;
import :deleter;
import :shader_validation;
import :bindless;
import :buffer_view;
import :pipeline_cache;
import :acceleration_structure;
import :ray_tracing_pipeline;
import :sparse_binding;

export namespace gfx
{

class PhysicalDevice;

/// @brief The logical device. Creation functions are thread-safe (worker threads may create objects
///        while others record); objects must be destroyed before the device. Destroying an object the
///        GPU still uses is safe: the Deleter releases it later.
class Device : public BackendObject
{
public:
    // ----------------------------------------------------------------------------- device

    /// @brief Physical device it was created from.
    [[nodiscard]] virtual auto physical_device() const noexcept -> const PhysicalDevice& = 0;

    /// @brief Whether @p feature is enabled: CORE_FEATURES and DeviceDescription::required_features always are, and a
    ///        backend enables the features that cost nothing whenever the physical device supports them (others, such
    ///        as SHADER_INTEGER64_ATOMICS or RAY_TRACING, only when required).
    [[nodiscard]] virtual auto enabled(DeviceFeature feature) const noexcept -> bool = 0;

    /// @brief Queue @p index of type @p type (as requested in DeviceDescription::queues).
    /// @throws InvalidUsageError when no such queue was requested.
    [[nodiscard]] virtual auto queue(QueueType type, std::uint32_t index = 0) -> Queue& = 0;

    /// @brief Number of queues of type @p type.
    [[nodiscard]] virtual auto queue_count(QueueType type) const noexcept -> std::uint32_t = 0;

    /// @brief Deferred-destruction queue.
    [[nodiscard]] virtual auto deleter() noexcept -> Deleter& = 0;

    /// @brief Blocks until every queue is idle.
    virtual void wait_idle() = 0;

    // ----------------------------------------------------------------------------- bindless resources

    /// @brief The pipeline layout every pipeline uses unless its description names another: the bindless
    ///        table in set BINDLESS_SET and BINDLESS_PUSH_CONSTANT_SIZE bytes of push constants for every
    ///        stage (bindless_pipeline_layout_definition()).
    [[nodiscard]] virtual auto bindless_pipeline_layout() const noexcept -> const PipelineLayout& = 0;

    /// @brief How full the bindless table is.
    [[nodiscard]] virtual auto bindless_usage() const noexcept -> BindlessTableUsage = 0;

    // ----------------------------------------------------------------------------- memory

    /// @brief Budget and usage of every memory heap (DeviceFeature::MEMORY_BUDGET): stream less, or free
    ///        memory, when usage approaches the budget.
    [[nodiscard]] virtual auto memory_budget() const -> std::vector<MemoryHeapBudget> = 0;

    /// @brief The library's own memory blocks and sub-allocations.
    [[nodiscard]] virtual auto memory_statistics() const -> MemoryStatistics = 0;

    // ----------------------------------------------------------------------------- memory and resources

    /// @brief Memory a buffer with this description needs (to place it in a DeviceMemory).
    [[nodiscard]] virtual auto memory_requirements(const BufferDescription& description) const -> MemoryRequirements = 0;

    /// @brief Memory an image with this description needs (to place it in a DeviceMemory).
    [[nodiscard]] virtual auto memory_requirements(const ImageDescription& description) const -> MemoryRequirements = 0;

    /// @brief Allocates device memory for placed (aliased) resources.
    [[nodiscard]] virtual auto allocate_memory(const DeviceMemoryDescription& description) -> std::unique_ptr<DeviceMemory> = 0;

    /// @brief Creates a buffer.
    [[nodiscard]] virtual auto create_buffer(const BufferDescription& description) -> std::unique_ptr<Buffer> = 0;

    /// @brief Creates an image.
    [[nodiscard]] virtual auto create_image(const ImageDescription& description) -> std::unique_ptr<Image> = 0;

    /// @brief Creates an image view.
    [[nodiscard]] virtual auto create_image_view(const ImageViewDescription& description) -> std::unique_ptr<ImageView> = 0;

    /// @brief Creates a sampler.
    [[nodiscard]] virtual auto create_sampler(const SamplerDescription& description) -> std::unique_ptr<Sampler> = 0;

    /// @brief Creates a formatted view of a buffer range (texel buffer).
    [[nodiscard]] virtual auto create_buffer_view(const BufferViewDescription& description) -> std::unique_ptr<BufferView> = 0;

    /// @brief How a sparse image is divided into tiles.
    [[nodiscard]] virtual auto sparse_image_requirements(ImageHandle image) const -> SparseImageRequirements = 0;

    // ----------------------------------------------------------------------------- shaders and pipelines

    /// @brief Creates a shader module from SPIR-V (or, in Debug builds, from GLSL compiled at run time)
    ///        and reflects its layout from the SPIR-V. Backends first call
    ///        validate_shader_module_description().
    /// @throws InvalidUsageError when the description is invalid, e.g. GLSL in a Release build.
    /// @throws ShaderCompilationError when GLSL source does not compile.
    [[nodiscard]] virtual auto create_shader_module(const ShaderModuleDescription& description) -> std::unique_ptr<ShaderModule> = 0;

    /// @brief Creates a descriptor set layout.
    [[nodiscard]] virtual auto create_descriptor_set_layout(const DescriptorSetLayoutDescription& description)
        -> std::unique_ptr<DescriptorSetLayout> = 0;

    /// @brief Creates a pipeline layout.
    [[nodiscard]] virtual auto create_pipeline_layout(const PipelineLayoutDescription& description)
        -> std::unique_ptr<PipelineLayout> = 0;

    /// @brief Creates a compute pipeline. Backends check it with validate_compute_pipeline().
    /// @throws ShaderLayoutMismatchError when the shader does not match the pipeline layout.
    [[nodiscard]] virtual auto create_compute_pipeline(const ComputePipelineDescription& description)
        -> std::unique_ptr<ComputePipeline> = 0;

    /// @brief Creates a graphics pipeline. Backends check it with validate_graphics_pipeline().
    /// @throws ShaderLayoutMismatchError when a shader does not match the pipeline layout, the vertex
    ///         attributes or the color attachments.
    [[nodiscard]] virtual auto create_graphics_pipeline(const GraphicsPipelineDescription& description)
        -> std::unique_ptr<GraphicsPipeline> = 0;

    /// @brief Creates a descriptor pool.
    [[nodiscard]] virtual auto create_descriptor_pool(const DescriptorPoolDescription& description)
        -> std::unique_ptr<DescriptorPool> = 0;

    /// @brief Creates a pipeline cache, loading the data of an earlier run.
    [[nodiscard]] virtual auto create_pipeline_cache(const PipelineCacheDescription& description)
        -> std::unique_ptr<PipelineCache> = 0;

    /// @brief Creates a ray tracing pipeline (DeviceFeature::RAY_TRACING). Backends check it with
    ///        validate_ray_tracing_pipeline().
    /// @throws ShaderLayoutMismatchError when a shader does not match the pipeline layout.
    [[nodiscard]] virtual auto create_ray_tracing_pipeline(const RayTracingPipelineDescription& description)
        -> std::unique_ptr<RayTracingPipeline> = 0;

    // ----------------------------------------------------------------------------- acceleration structures

    /// @brief Storage and scratch memory a build of @p description needs.
    [[nodiscard]] virtual auto acceleration_structure_build_sizes(const AccelerationStructureBuildDescription& description) const
        -> AccelerationStructureBuildSizes = 0;

    /// @brief Creates an acceleration structure in buffer storage (DeviceFeature::RAY_TRACING or RAY_QUERY).
    [[nodiscard]] virtual auto create_acceleration_structure(const AccelerationStructureDescription& description)
        -> std::unique_ptr<AccelerationStructure> = 0;

    // ----------------------------------------------------------------------------- commands and synchronization

    /// @brief Creates a command pool (one per recording thread and queue type).
    [[nodiscard]] virtual auto create_command_pool(const CommandPoolDescription& description) -> std::unique_ptr<CommandPool> = 0;

    /// @brief Creates a fence.
    [[nodiscard]] virtual auto create_fence(const FenceDescription& description) -> std::unique_ptr<Fence> = 0;

    /// @brief Creates a binary semaphore.
    [[nodiscard]] virtual auto create_semaphore(const SemaphoreDescription& description) -> std::unique_ptr<Semaphore> = 0;

    /// @brief Creates a timeline semaphore.
    [[nodiscard]] virtual auto create_timeline_semaphore(const TimelineSemaphoreDescription& description)
        -> std::unique_ptr<TimelineSemaphore> = 0;

    /// @brief Creates an event (DeviceFeature::EVENTS).
    [[nodiscard]] virtual auto create_event(const EventDescription& description) -> std::unique_ptr<Event> = 0;

    /// @brief Creates a query pool.
    [[nodiscard]] virtual auto create_query_pool(const QueryPoolDescription& description) -> std::unique_ptr<QueryPool> = 0;

    /// @brief Creates a swapchain for a window (DeviceFeature::PRESENTATION).
    /// @throws InvalidUsageError when the description names no window or no queue of the device;
    ///         BackendCapabilityError when the presenting queue cannot present to the window, or the window's
    ///         surface lacks the format and color space, presentation mode or usage (the error lists what
    ///         it supports; PhysicalDevice::surface_support() too).
    [[nodiscard]] virtual auto create_swapchain(const SwapchainDescription& description) -> std::unique_ptr<Swapchain> = 0;

    // ----------------------------------------------------------------------------- frame capture

    /// @brief Capture tool attached to the process (RenderDoc, PIX, Nsight Graphics), if any.
    [[nodiscard]] virtual auto capture_tool() const noexcept -> CaptureTool = 0;

    /// @brief Starts capturing every queue's work in the attached tool, e.g. around one frame.
    /// @throws BackendCapabilityError when no capture tool is attached.
    virtual void begin_capture(std::string_view title) = 0;

    /// @brief Ends the capture begun by begin_capture().
    virtual void end_capture() = 0;

    // ----------------------------------------------------------------------------- layouts from shaders

    /// @brief Creates the pipeline layout that exactly provides what @p shaders use, with one descriptor
    ///        set layout per set index: bindings named and visible as the reflected layouts say, and one
    ///        push-constant range covering every shader's push constants.
    /// @throws ShaderLayoutMismatchError when two shaders declare one binding differently.
    [[nodiscard]] auto create_pipeline_layout_from(std::span<const ShaderModule* const> shaders,
                                                   std::string_view debug_name = {}) -> ReflectedPipelineLayout
    {
        // Merge what the shaders use.
        const PipelineLayoutDefinition definition = pipeline_layout_definition_from(shaders);

        // One descriptor set layout per set index, holding the bindings of that set.
        std::vector<std::unique_ptr<DescriptorSetLayout>> set_layouts;
        std::vector<DescriptorSetLayoutHandle> set_layout_handles;
        for (std::uint32_t set = 0; set < definition.set_count(); ++set)
        {
            const std::vector<DescriptorSetLayoutBinding> bindings = definition.set_bindings(set);
            set_layouts.push_back(create_descriptor_set_layout({.bindings = bindings, .debug_name = debug_name}));
            set_layout_handles.push_back(set_layouts.back()->handle());
        }

        // The pipeline layout over them; the result owns it together with the set layouts.
        std::unique_ptr<PipelineLayout> pipeline_layout = create_pipeline_layout({
            .set_layouts = set_layout_handles,
            .push_constant_ranges = definition.push_constant_ranges,
            .debug_name = debug_name,
        });
        return ReflectedPipelineLayout{std::move(set_layouts), std::move(pipeline_layout)};
    }

    /// @brief Creates the pipeline layout that exactly provides what @p shaders use (see the overload).
    [[nodiscard]] auto create_pipeline_layout_from(std::initializer_list<const ShaderModule*> shaders,
                                                   std::string_view debug_name = {}) -> ReflectedPipelineLayout
    {
        return create_pipeline_layout_from(std::span<const ShaderModule* const>{shaders.begin(), shaders.size()}, debug_name);
    }

    // ----------------------------------------------------------------------------- shader hot reload

    /// @brief Debug builds: recompiles every shader module whose source file changed since the last
    ///        call, and replaces its code (and rebuilds the pipelines using it, behind the same handles)
    ///        when the new code still fits their pipeline layouts. A file that does not compile or no
    ///        longer fits keeps its old code, and the report says why. Release builds return no report.
    ///
    /// Call it once per frame, when no thread is recording commands.
    [[nodiscard]] auto poll_shader_changes() -> std::vector<ShaderReload>
    {
        if constexpr (SHADER_HOT_RELOAD)
        {
            return reload_changed_shaders();
        }
        else
        {
            return {};
        }
    }

protected:
    /// @brief Backend part of poll_shader_changes() (called in Debug builds only).
    [[nodiscard]] virtual auto reload_changed_shaders() -> std::vector<ShaderReload> = 0;
};

// Default definitions: a backend forwards to these for the operations it does not implement.

auto
Device::queue(QueueType /*type*/,
              std::uint32_t /*index*/) -> Queue&
{
    throw_not_implemented("Device::queue");
}

void
Device::wait_idle()
{
    throw_not_implemented("Device::wait_idle");
}

auto
Device::memory_requirements(const BufferDescription& /*description*/) const -> MemoryRequirements
{
    throw_not_implemented("Device::memory_requirements(BufferDescription)");
}

auto
Device::memory_requirements(const ImageDescription& /*description*/) const -> MemoryRequirements
{
    throw_not_implemented("Device::memory_requirements(ImageDescription)");
}

auto
Device::allocate_memory(const DeviceMemoryDescription& /*description*/) -> std::unique_ptr<DeviceMemory>
{
    throw_not_implemented("Device::allocate_memory");
}

auto
Device::create_buffer(const BufferDescription& /*description*/) -> std::unique_ptr<Buffer>
{
    throw_not_implemented("Device::create_buffer");
}

auto
Device::create_image(const ImageDescription& /*description*/) -> std::unique_ptr<Image>
{
    throw_not_implemented("Device::create_image");
}

auto
Device::create_image_view(const ImageViewDescription& /*description*/) -> std::unique_ptr<ImageView>
{
    throw_not_implemented("Device::create_image_view");
}

auto
Device::create_sampler(const SamplerDescription& /*description*/) -> std::unique_ptr<Sampler>
{
    throw_not_implemented("Device::create_sampler");
}

auto
Device::create_shader_module(const ShaderModuleDescription& /*description*/) -> std::unique_ptr<ShaderModule>
{
    throw_not_implemented("Device::create_shader_module");
}

auto
Device::create_descriptor_set_layout(const DescriptorSetLayoutDescription& /*description*/)
    -> std::unique_ptr<DescriptorSetLayout>
{
    throw_not_implemented("Device::create_descriptor_set_layout");
}

auto
Device::create_pipeline_layout(const PipelineLayoutDescription& /*description*/) -> std::unique_ptr<PipelineLayout>
{
    throw_not_implemented("Device::create_pipeline_layout");
}

auto
Device::create_compute_pipeline(const ComputePipelineDescription& /*description*/) -> std::unique_ptr<ComputePipeline>
{
    throw_not_implemented("Device::create_compute_pipeline");
}

auto
Device::create_graphics_pipeline(const GraphicsPipelineDescription& /*description*/) -> std::unique_ptr<GraphicsPipeline>
{
    throw_not_implemented("Device::create_graphics_pipeline");
}

auto
Device::create_descriptor_pool(const DescriptorPoolDescription& /*description*/) -> std::unique_ptr<DescriptorPool>
{
    throw_not_implemented("Device::create_descriptor_pool");
}

auto
Device::create_command_pool(const CommandPoolDescription& /*description*/) -> std::unique_ptr<CommandPool>
{
    throw_not_implemented("Device::create_command_pool");
}

auto
Device::create_fence(const FenceDescription& /*description*/) -> std::unique_ptr<Fence>
{
    throw_not_implemented("Device::create_fence");
}

auto
Device::create_semaphore(const SemaphoreDescription& /*description*/) -> std::unique_ptr<Semaphore>
{
    throw_not_implemented("Device::create_semaphore");
}

auto
Device::create_timeline_semaphore(const TimelineSemaphoreDescription& /*description*/) -> std::unique_ptr<TimelineSemaphore>
{
    throw_not_implemented("Device::create_timeline_semaphore");
}

auto
Device::create_event(const EventDescription& /*description*/) -> std::unique_ptr<Event>
{
    throw_not_implemented("Device::create_event");
}

auto
Device::create_query_pool(const QueryPoolDescription& /*description*/) -> std::unique_ptr<QueryPool>
{
    throw_not_implemented("Device::create_query_pool");
}

auto
Device::create_swapchain(const SwapchainDescription& /*description*/) -> std::unique_ptr<Swapchain>
{
    throw_not_implemented("Device::create_swapchain");
}

auto
Device::memory_budget() const -> std::vector<MemoryHeapBudget>
{
    throw_not_implemented("Device::memory_budget");
}

auto
Device::memory_statistics() const -> MemoryStatistics
{
    throw_not_implemented("Device::memory_statistics");
}

auto
Device::create_buffer_view(const BufferViewDescription& /*description*/) -> std::unique_ptr<BufferView>
{
    throw_not_implemented("Device::create_buffer_view");
}

auto
Device::sparse_image_requirements(ImageHandle /*image*/) const -> SparseImageRequirements
{
    throw_not_implemented("Device::sparse_image_requirements");
}

auto
Device::create_pipeline_cache(const PipelineCacheDescription& /*description*/) -> std::unique_ptr<PipelineCache>
{
    throw_not_implemented("Device::create_pipeline_cache");
}

auto
Device::create_ray_tracing_pipeline(const RayTracingPipelineDescription& /*description*/) -> std::unique_ptr<RayTracingPipeline>
{
    throw_not_implemented("Device::create_ray_tracing_pipeline");
}

auto
Device::acceleration_structure_build_sizes(const AccelerationStructureBuildDescription& /*description*/) const
    -> AccelerationStructureBuildSizes
{
    throw_not_implemented("Device::acceleration_structure_build_sizes");
}

auto
Device::create_acceleration_structure(const AccelerationStructureDescription& /*description*/)
    -> std::unique_ptr<AccelerationStructure>
{
    throw_not_implemented("Device::create_acceleration_structure");
}

void
Device::begin_capture(std::string_view /*title*/)
{
    throw_not_implemented("Device::begin_capture");
}

void
Device::end_capture()
{
    throw_not_implemented("Device::end_capture");
}

auto
Device::reload_changed_shaders() -> std::vector<ShaderReload>
{
    throw_not_implemented("Device::poll_shader_changes");
}

} // namespace gfx
