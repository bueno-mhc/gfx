/// @file recorder.cppm
/// @brief Typed command recording: Recorder<QueueType> and RenderingScope<QueueType>.
///
/// Both are zero-cost, non-virtual wrappers around a CommandBuffer. Their member functions are
/// constrained by the capabilities of the queue type, so invalid recordings are compile errors:
/// - Recorder<QueueType::TRANSFER> has copies and barriers, but no dispatch;
/// - Recorder<QueueType::COMPUTE> adds compute pipelines, descriptor sets, push constants, dispatches;
/// - Recorder<QueueType::GRAPHICS> adds begin_rendering(), which returns the RenderingScope that holds
///   every draw command (draws cannot be recorded outside a rendering pass).
/// Both are RAII: the recorder ends the command buffer and the scope ends the rendering pass.
export module gfx.interface:recorder;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import :common_types;
import :commands;
import :synchronization;
import :command_buffer;
import :layout_compatibility;

export namespace gfx
{

/// @brief Commands of a dynamic rendering pass; obtained from Recorder::begin_rendering(), ends the
///        pass when destroyed.
template<QueueType Type>
class RenderingScope
{
public:
    ~RenderingScope()
    {
        m_command_buffer.record_end_rendering();
    }

    RenderingScope(const RenderingScope&) = delete;
    RenderingScope(RenderingScope&&) = delete;
    auto operator=(const RenderingScope&) -> RenderingScope& = delete;
    auto operator=(RenderingScope&&) -> RenderingScope& = delete;

    /// @brief Sets the viewport.
    void set_viewport(const Viewport& viewport)
    {
        m_command_buffer.record_set_viewport(viewport);
    }

    /// @brief Sets the scissor rectangle.
    void set_scissor(const Rectangle& scissor)
    {
        m_command_buffer.record_set_scissor(scissor);
    }

    /// @brief Binds a graphics pipeline for the following draws.
    void bind_pipeline(GraphicsPipelineHandle pipeline)
    {
        m_command_buffer.record_bind_graphics_pipeline(pipeline);
    }

    /// @brief Binds descriptor sets for graphics pipelines, starting at set @p first_set.
    void bind_descriptor_sets(PipelineLayoutHandle layout,
                              std::uint32_t first_set,
                              std::span<const DescriptorSetHandle> sets,
                              std::span<const std::uint32_t> dynamic_offsets = {})
    {
        m_command_buffer.record_bind_descriptor_sets(PipelineBindPoint::GRAPHICS, layout, first_set, sets, dynamic_offsets);
    }

    /// @brief Sets push-constant bytes.
    void push_constants(PipelineLayoutHandle layout,
                        ShaderStage stages,
                        std::uint32_t offset,
                        std::span<const std::byte> data)
    {
        m_command_buffer.record_push_constants(layout, stages, offset, data);
    }

    /// @brief Sets the push constants of @p Shader; @p Data must have exactly the declared size.
    template<ShaderDefinition Shader, class Data>
        requires PushConstantsFor<Data, Shader>
    void push_constants(PipelineLayoutHandle layout, const Data& data)
    {
        m_command_buffer.record_push_constants(layout, Shader::LAYOUT.stage, 0, std::as_bytes(std::span{&data, 1}));
    }

    /// @brief Binds vertex buffers starting at binding @p first_binding.
    void bind_vertex_buffers(std::uint32_t first_binding, std::span<const VertexBufferBinding> buffers)
    {
        m_command_buffer.record_bind_vertex_buffers(first_binding, buffers);
    }

    /// @brief Binds the index buffer.
    void bind_index_buffer(BufferHandle buffer, std::uint64_t offset, IndexType type)
    {
        m_command_buffer.record_bind_index_buffer(buffer, offset, type);
    }

    /// @brief Draws non-indexed primitives.
    void draw(const DrawArguments& arguments)
    {
        m_command_buffer.record_draw(arguments);
    }

    /// @brief Draws indexed primitives.
    void draw_indexed(const DrawIndexedArguments& arguments)
    {
        m_command_buffer.record_draw_indexed(arguments);
    }

    /// @brief Draws with arguments read from a buffer.
    void draw_indirect(const IndirectDrawArguments& arguments)
    {
        m_command_buffer.record_draw_indirect(arguments);
    }

    /// @brief Draws indexed primitives with arguments read from a buffer.
    void draw_indexed_indirect(const IndirectDrawArguments& arguments)
    {
        m_command_buffer.record_draw_indexed_indirect(arguments);
    }

private:
    friend class Recorder<Type>;

    explicit RenderingScope(CommandBuffer& command_buffer)
        : m_command_buffer{command_buffer}
    {}

private:
    CommandBuffer& m_command_buffer;
};

/// @brief Records commands for queue type @p Type into a CommandBuffer: begins recording when
///        constructed, ends it when destroyed.
template<QueueType Type>
class Recorder
{
public:
    /// @brief Kinds of work this recorder records.
    static constexpr QueueCapability CAPABILITIES = queue_type_capabilities(Type);

    /// @brief Begins recording.
    /// @throws InvalidUsageError when @p command_buffer belongs to a queue type that cannot run every
    ///         kind of work of @p Type (e.g. a compute recorder on a transfer command buffer).
    explicit Recorder(CommandBuffer& command_buffer)
        : m_command_buffer{command_buffer}
    {
        // A graphics command buffer accepts compute recording, not the other way around.
        if (!has_all(queue_type_capabilities(command_buffer.queue_type()), CAPABILITIES))
        {
            throw InvalidUsageError({
                .backend = command_buffer.backend(),
                .operation = "Recorder::Recorder",
                .problem = std::format("a {} recorder needs a command buffer whose queue runs {} work, but '{}' "
                                       "belongs to a {} queue",
                                       Type, CAPABILITIES, command_buffer.debug_name(), command_buffer.queue_type()),
                .suggestion = "allocate the command buffer from a command pool of a matching queue type",
            });
        }

        // Start recording.
        m_command_buffer.record_begin();
    }

    ~Recorder()
    {
        m_command_buffer.record_end();
    }

    Recorder(const Recorder&) = delete;
    Recorder(Recorder&&) = delete;
    auto operator=(const Recorder&) -> Recorder& = delete;
    auto operator=(Recorder&&) -> Recorder& = delete;

    // ----------------------------------------------------------------------------- every queue type

    /// @brief Records barriers (memory dependencies, layout transitions, ownership transfers).
    void barrier(const BarrierBatch& barriers)
    {
        m_command_buffer.record_barrier(barriers);
    }

    /// @brief Copies bytes between buffers.
    void copy_buffer(BufferHandle source, BufferHandle destination, std::span<const BufferCopyRegion> regions)
        requires (has_any(CAPABILITIES, QueueCapability::TRANSFER))
    {
        m_command_buffer.record_copy_buffer(source, destination, regions);
    }

    /// @brief Copies texels from a buffer to an image (image in TRANSFER_DESTINATION or GENERAL layout).
    void copy_buffer_to_image(BufferHandle source,
                              ImageHandle destination,
                              ImageLayout destination_layout,
                              std::span<const BufferImageCopyRegion> regions)
        requires (has_any(CAPABILITIES, QueueCapability::TRANSFER))
    {
        m_command_buffer.record_copy_buffer_to_image(source, destination, destination_layout, regions);
    }

    /// @brief Copies texels from an image to a buffer (image in TRANSFER_SOURCE or GENERAL layout).
    void copy_image_to_buffer(ImageHandle source,
                              ImageLayout source_layout,
                              BufferHandle destination,
                              std::span<const BufferImageCopyRegion> regions)
        requires (has_any(CAPABILITIES, QueueCapability::TRANSFER))
    {
        m_command_buffer.record_copy_image_to_buffer(source, source_layout, destination, regions);
    }

    /// @brief Copies texels between images.
    void copy_image(ImageHandle source,
                    ImageLayout source_layout,
                    ImageHandle destination,
                    ImageLayout destination_layout,
                    std::span<const ImageCopyRegion> regions)
        requires (has_any(CAPABILITIES, QueueCapability::TRANSFER))
    {
        m_command_buffer.record_copy_image(source, source_layout, destination, destination_layout, regions);
    }

    /// @brief Fills a buffer range with a repeated 32-bit value.
    void fill_buffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value)
        requires (has_any(CAPABILITIES, QueueCapability::TRANSFER))
    {
        m_command_buffer.record_fill_buffer(buffer, offset, size, value);
    }

    /// @brief Writes a few bytes (up to 65536) inline into a buffer.
    void update_buffer(BufferHandle buffer, std::uint64_t offset, std::span<const std::byte> data)
        requires (has_any(CAPABILITIES, QueueCapability::TRANSFER))
    {
        m_command_buffer.record_update_buffer(buffer, offset, data);
    }

    /// @brief Resets queries of a pool before they are written again.
    void reset_query_pool(QueryPoolHandle pool, std::uint32_t first, std::uint32_t count)
    {
        m_command_buffer.record_reset_query_pool(pool, first, count);
    }

    /// @brief Writes a GPU timestamp once every earlier command reaches @p stage.
    void write_timestamp(QueryPoolHandle pool, PipelineStage stage, std::uint32_t query)
    {
        m_command_buffer.record_write_timestamp(pool, stage, query);
    }

    /// @brief Opens a labeled region shown by debugging tools.
    void begin_debug_label(std::string_view name, const std::array<float, 4>& color = {1.0F, 1.0F, 1.0F, 1.0F})
    {
        m_command_buffer.record_begin_debug_label(name, color);
    }

    /// @brief Closes the innermost labeled region.
    void end_debug_label()
    {
        m_command_buffer.record_end_debug_label();
    }

    /// @brief Executes secondary command buffers.
    void execute_commands(std::span<const CommandBufferHandle> secondary_command_buffers)
    {
        m_command_buffer.record_execute_commands(secondary_command_buffers);
    }

    // ----------------------------------------------------------------------------- compute and graphics queues

    /// @brief Clears subresources of a color image outside rendering passes.
    void clear_color_image(ImageHandle image,
                           ImageLayout layout,
                           const std::array<float, 4>& color,
                           const ImageSubresourceRange& range = {})
        requires (has_any(CAPABILITIES, QueueCapability::GRAPHICS | QueueCapability::COMPUTE))
    {
        m_command_buffer.record_clear_color_image(image, layout, color, range);
    }

    /// @brief Binds a compute pipeline for the following dispatches.
    void bind_pipeline(ComputePipelineHandle pipeline)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        m_command_buffer.record_bind_compute_pipeline(pipeline);
    }

    /// @brief Binds descriptor sets for compute pipelines, starting at set @p first_set.
    void bind_descriptor_sets(PipelineLayoutHandle layout,
                              std::uint32_t first_set,
                              std::span<const DescriptorSetHandle> sets,
                              std::span<const std::uint32_t> dynamic_offsets = {})
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        m_command_buffer.record_bind_descriptor_sets(PipelineBindPoint::COMPUTE, layout, first_set, sets, dynamic_offsets);
    }

    /// @brief Sets push-constant bytes.
    void push_constants(PipelineLayoutHandle layout, ShaderStage stages, std::uint32_t offset, std::span<const std::byte> data)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        m_command_buffer.record_push_constants(layout, stages, offset, data);
    }

    /// @brief Sets the push constants of @p Shader; @p Data must have exactly the declared size.
    template<ShaderDefinition Shader, class Data>
        requires PushConstantsFor<Data, Shader> && (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    void push_constants(PipelineLayoutHandle layout, const Data& data)
    {
        m_command_buffer.record_push_constants(layout, Shader::LAYOUT.stage, 0, std::as_bytes(std::span{&data, 1}));
    }

    /// @brief Dispatches compute workgroups.
    void dispatch(std::uint32_t group_count_x, std::uint32_t group_count_y = 1, std::uint32_t group_count_z = 1)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        m_command_buffer.record_dispatch(group_count_x, group_count_y, group_count_z);
    }

    /// @brief Dispatches with workgroup counts read from a buffer.
    void dispatch_indirect(BufferHandle buffer, std::uint64_t offset)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        m_command_buffer.record_dispatch_indirect(buffer, offset);
    }

    /// @brief Records the source side of a split barrier.
    void set_event(EventHandle event, const BarrierBatch& barriers)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        m_command_buffer.record_set_event(event, barriers);
    }

    /// @brief Records the destination side of split barriers.
    void wait_events(std::span<const EventHandle> events, const BarrierBatch& barriers)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        m_command_buffer.record_wait_events(events, barriers);
    }

    /// @brief Resets an event once @p stages complete.
    void reset_event(EventHandle event, PipelineStage stages)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        m_command_buffer.record_reset_event(event, stages);
    }

    /// @brief Starts an occlusion or pipeline statistics query.
    void begin_query(QueryPoolHandle pool, std::uint32_t query)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        m_command_buffer.record_begin_query(pool, query);
    }

    /// @brief Ends an occlusion or pipeline statistics query.
    void end_query(QueryPoolHandle pool, std::uint32_t query)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        m_command_buffer.record_end_query(pool, query);
    }

    // ----------------------------------------------------------------------------- graphics queues

    /// @brief Begins a dynamic rendering pass; draw commands are recorded on the returned scope, which
    ///        ends the pass when destroyed.
    [[nodiscard]] auto begin_rendering(const RenderingDescription& rendering) -> RenderingScope<Type>
        requires (has_any(CAPABILITIES, QueueCapability::GRAPHICS))
    {
        m_command_buffer.record_begin_rendering(rendering);
        return RenderingScope<Type>{m_command_buffer};
    }

private:
    CommandBuffer& m_command_buffer;
};

/// @brief Recorder for graphics queues (graphics, compute and transfer commands).
using GraphicsRecorder = Recorder<QueueType::GRAPHICS>;

/// @brief Recorder for compute queues (compute and transfer commands).
using ComputeRecorder = Recorder<QueueType::COMPUTE>;

/// @brief Recorder for transfer queues (transfer commands).
using TransferRecorder = Recorder<QueueType::TRANSFER>;

} // namespace gfx
