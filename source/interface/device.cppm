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

    /// @brief Queue @p index of type @p type (as requested in DeviceDescription::queues).
    /// @throws InvalidUsageError when no such queue was requested.
    [[nodiscard]] virtual auto queue(QueueType type, std::uint32_t index = 0) -> Queue& = 0;

    /// @brief Number of queues of type @p type.
    [[nodiscard]] virtual auto queue_count(QueueType type) const noexcept -> std::uint32_t = 0;

    /// @brief Deferred-destruction queue.
    [[nodiscard]] virtual auto deleter() noexcept -> Deleter& = 0;

    /// @brief Blocks until every queue is idle.
    virtual void wait_idle() = 0;

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
    [[nodiscard]] virtual auto create_swapchain(const SwapchainDescription& description) -> std::unique_ptr<Swapchain> = 0;

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
Device::reload_changed_shaders() -> std::vector<ShaderReload>
{
    throw_not_implemented("Device::poll_shader_changes");
}

} // namespace gfx
