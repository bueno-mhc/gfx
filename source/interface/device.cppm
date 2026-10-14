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
import :layout_compatibility;
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
import :typed_objects;

export namespace gfx
{

class PhysicalDevice;

/// @brief The logical device. Creation functions are thread-safe; objects must be destroyed before
///        the device. Destroying an object the GPU still uses is safe: the Deleter releases it later.
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

    /// @brief Compiles a shader (GLSL to SPIR-V) or loads SPIR-V, and reflects its layout.
    /// @throws ShaderCompilationError when the source does not compile.
    /// @throws ShaderLayoutMismatchError when the reflected layout differs from expected_layout.
    [[nodiscard]] virtual auto create_shader_module(const ShaderModuleDescription& description) -> std::unique_ptr<ShaderModule> = 0;

    /// @brief Creates a descriptor set layout.
    [[nodiscard]] virtual auto create_descriptor_set_layout(const DescriptorSetLayoutDescription& description)
        -> std::unique_ptr<DescriptorSetLayout> = 0;

    /// @brief Creates a pipeline layout.
    [[nodiscard]] virtual auto create_pipeline_layout(const PipelineLayoutDescription& description)
        -> std::unique_ptr<PipelineLayout> = 0;

    /// @brief Creates a compute pipeline.
    /// @throws ShaderLayoutMismatchError when the shader does not match the pipeline layout.
    [[nodiscard]] virtual auto create_compute_pipeline(const ComputePipelineDescription& description)
        -> std::unique_ptr<ComputePipeline> = 0;

    /// @brief Creates a graphics pipeline.
    /// @throws ShaderLayoutMismatchError when a shader does not match the pipeline layout, the vertex
    ///         layout or the color attachments.
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

    // ----------------------------------------------------------------------------- typed creation (checked at compile time)

    /// @brief Creates the module of a shader known at compile time; the layout reflected from the
    ///        compiled code must equal Shader::LAYOUT.
    template<ShaderDefinition Shader>
    [[nodiscard]] auto create_shader() -> TypedShaderModule<Shader>
    {
        return TypedShaderModule<Shader>{create_shader_module({
            .stage = Shader::LAYOUT.stage,
            .source_format = ShaderSourceFormat::GLSL,
            .glsl_source = Shader::SOURCE,
            .spirv = {},
            .entry_point = "main",
            .name = Shader::NAME,
            .expected_layout = Shader::LAYOUT,
        })};
    }

    /// @brief Creates the descriptor set layouts (one per set index) and the pipeline layout of a
    ///        compile-time definition.
    template<const PipelineLayoutDefinition& Definition>
    [[nodiscard]] auto create_pipeline_layout(std::string_view debug_name = {}) -> TypedPipelineLayout<Definition>
    {
        // One descriptor set layout per set index, holding the definition's bindings of that set.
        std::vector<std::unique_ptr<DescriptorSetLayout>> set_layouts;
        std::vector<DescriptorSetLayoutHandle> set_layout_handles;
        for (std::uint32_t set = 0; set < Definition.set_count(); ++set)
        {
            std::vector<DescriptorSetLayoutBinding> bindings;
            for (const LayoutBinding& binding : Definition.bindings())
            {
                if (binding.set == set)
                {
                    bindings.push_back({
                        .binding = binding.binding,
                        .type = binding.type,
                        .count = binding.count,
                        .stages = binding.stages,
                        .options = binding.options,
                    });
                }
            }
            set_layouts.push_back(create_descriptor_set_layout({.bindings = bindings, .debug_name = debug_name}));
            set_layout_handles.push_back(set_layouts.back()->handle());
        }

        // The push constants form one range, visible to every stage that reads them.
        const std::array<PushConstantRange, 1> push_constant_range{
            PushConstantRange{.stages = Definition.push_constant_stages, .offset = 0, .size = Definition.push_constant_size}};
        const std::span<const PushConstantRange> push_constant_ranges =
            Definition.push_constant_size > 0 ? std::span<const PushConstantRange>{push_constant_range}
                                              : std::span<const PushConstantRange>{};

        // Create the pipeline layout and hand over every object.
        std::unique_ptr<PipelineLayout> pipeline_layout = create_pipeline_layout(PipelineLayoutDescription{
            .set_layouts = set_layout_handles,
            .push_constant_ranges = push_constant_ranges,
            .debug_name = debug_name,
        });
        return TypedPipelineLayout<Definition>{std::move(set_layouts), std::move(pipeline_layout)};
    }

    /// @brief Creates a compute pipeline; the shader's stage, bindings and push constants are checked
    ///        against the pipeline layout at compile time.
    template<const PipelineLayoutDefinition& Definition, ShaderDefinition Shader>
    [[nodiscard]] auto create_compute_pipeline(const TypedPipelineLayout<Definition>& layout,
                                               const TypedShaderModule<Shader>& shader,
                                               std::string_view debug_name = {}) -> std::unique_ptr<ComputePipeline>
    {
        // Proven at compile time; a mismatch fails here and prints the complete LayoutCheck.
        static_assert(LayoutDiagnostic<check_shader_stage(Shader::LAYOUT, ShaderStage::COMPUTE)>::OK);
        static_assert(LayoutDiagnostic<check_pipeline_layout(Shader::LAYOUT, Definition)>::OK);

        return create_compute_pipeline(ComputePipelineDescription{
            .layout = layout.handle(),
            .shader = shader.handle(),
            .debug_name = debug_name,
        });
    }

    /// @brief Creates a graphics pipeline; shader stages, bindings, push constants, vertex inputs and
    ///        fragment outputs are checked at compile time against the pipeline layout, @p Vertices
    ///        and @p Attachments.
    template<const VertexLayoutDefinition& Vertices,
             const AttachmentLayoutDefinition& Attachments,
             const PipelineLayoutDefinition& Definition,
             ShaderDefinition VertexShader,
             ShaderDefinition FragmentShader>
    [[nodiscard]] auto create_graphics_pipeline(const TypedPipelineLayout<Definition>& layout,
                                                const TypedShaderModule<VertexShader>& vertex_shader,
                                                const TypedShaderModule<FragmentShader>& fragment_shader,
                                                const GraphicsPipelineState& state = {}) -> std::unique_ptr<GraphicsPipeline>
    {
        // Proven at compile time; a mismatch fails here and prints the complete LayoutCheck.
        static_assert(LayoutDiagnostic<check_shader_stage(VertexShader::LAYOUT, ShaderStage::VERTEX)>::OK);
        static_assert(LayoutDiagnostic<check_shader_stage(FragmentShader::LAYOUT, ShaderStage::FRAGMENT)>::OK);
        static_assert(LayoutDiagnostic<check_pipeline_layout(VertexShader::LAYOUT, Definition)>::OK);
        static_assert(LayoutDiagnostic<check_pipeline_layout(FragmentShader::LAYOUT, Definition)>::OK);
        static_assert(LayoutDiagnostic<check_vertex_input(VertexShader::LAYOUT, Vertices)>::OK);
        static_assert(LayoutDiagnostic<check_fragment_output(FragmentShader::LAYOUT, Attachments)>::OK);

        return create_graphics_pipeline(GraphicsPipelineDescription{
            .layout = layout.handle(),
            .vertex_shader = vertex_shader.handle(),
            .fragment_shader = fragment_shader.handle(),
            .vertex_bindings = Vertices.bindings(),
            .vertex_attributes = Vertices.attributes(),
            .color_formats = Attachments.color_formats(),
            .depth_format = Attachments.depth_format,
            .stencil_format = Attachments.stencil_format,
            .samples = Attachments.samples,
            .state = state,
        });
    }
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

} // namespace gfx
