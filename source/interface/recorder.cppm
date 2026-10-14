/// @file recorder.cppm
/// @brief Typed command recording: Recorder<QueueType>, the rendering scopes, and the recorder of the
///        parts of a parallel rendering pass.
///
/// All are zero-cost, non-virtual wrappers around a CommandBuffer. Their member functions are
/// constrained by the capabilities of the queue type, so invalid recordings are compile errors:
/// - Recorder<QueueType::TRANSFER> has copies and barriers, but no dispatch;
/// - Recorder<QueueType::COMPUTE> adds compute pipelines, descriptor sets, push constants, dispatches;
/// - Recorder<QueueType::GRAPHICS> adds begin_rendering(), which returns the RenderingScope that holds
///   every draw command (draws cannot be recorded outside a rendering pass), and
///   begin_parallel_rendering(), which returns a ParallelRenderingScope: other threads record the draws
///   into secondary command buffers with RenderingPartRecorder, and the scope executes them in order.
/// All are RAII: recorders end their command buffer and scopes end their rendering pass.
export module gfx.interface:recorder;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import :common_types;
import :commands;
import :synchronization;
import :command_buffer;
import :pipeline_layout;

export namespace gfx
{

/// @brief Data that can be pushed as push constants: trivially copyable, and neither a pointer nor a
///        span (whose bytes would be the address, not the data).
template<class Data>
concept PushConstantData = std::is_trivially_copyable_v<Data> && !std::is_pointer_v<Data> &&
                           !requires { typename Data::element_type; Data::extent; };

/// @brief Stages a push-constant write of @p size bytes at @p offset must name: the stages of every
///        push-constant range of @p layout overlapping those bytes.
/// @throws InvalidUsageError when some of the bytes are outside every push-constant range.
[[nodiscard]] auto
push_constant_stages(const PipelineLayout& layout,
                     std::uint32_t offset,
                     std::uint32_t size) -> ShaderStage
{
    const std::vector<PushConstantRange>& ranges = layout.definition().push_constant_ranges;
    const std::uint32_t end = offset + size;

    // Every byte must be inside some range: walk from range to range.
    std::uint32_t position = offset;
    while (position < end)
    {
        const auto range = std::ranges::find_if(ranges, [position](const PushConstantRange& candidate)
        {
            return candidate.offset <= position && position < candidate.offset + candidate.size;
        });
        if (range == ranges.end())
        {
            std::string provided;
            for (const PushConstantRange& candidate : ranges)
            {
                provided += std::format("{}[{}, {}) for {}", provided.empty() ? "" : ", ", candidate.offset,
                                        candidate.offset + candidate.size, candidate.stages);
            }
            throw InvalidUsageError({
                .backend = layout.backend(),
                .operation = "push_constants",
                .problem = std::format("push-constant bytes [{}, {}) are not all inside the push-constant ranges of "
                                       "pipeline layout '{}' (byte {} is outside every range)",
                                       offset, end, layout.debug_name(), position),
                .suggestion = provided.empty() ? std::string{"the pipeline layout has no push constants"}
                                               : std::format("its ranges are {}", provided),
            });
        }
        position = range->offset + range->size;
    }

    // The write names the stages of every range it overlaps.
    ShaderStage stages = ShaderStage::NONE;
    for (const PushConstantRange& range : ranges)
    {
        if (range.offset < end && offset < range.offset + range.size)
        {
            stages |= range.stages;
        }
    }
    return stages;
}

/// @brief The commands allowed inside a rendering pass: dynamic state, graphics pipeline and resource
///        bindings, push constants and draws. Base of RenderingScope and RenderingPartRecorder.
class RenderingCommands
{
public:
    RenderingCommands(const RenderingCommands&) = delete;
    RenderingCommands(RenderingCommands&&) = delete;
    auto operator=(const RenderingCommands&) -> RenderingCommands& = delete;
    auto operator=(RenderingCommands&&) -> RenderingCommands& = delete;

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

    /// @brief Sets push-constant bytes (unchecked: the stages must match the layout's ranges).
    void push_constants(PipelineLayoutHandle layout,
                        ShaderStage stages,
                        std::uint32_t offset,
                        std::span<const std::byte> data)
    {
        m_command_buffer.record_push_constants(layout, stages, offset, data);
    }

    /// @brief Sets the push constants at @p offset to the bytes of @p data, for the stages whose
    ///        push-constant ranges in @p layout hold them.
    /// @throws InvalidUsageError when the bytes are not inside the layout's push-constant ranges.
    template<PushConstantData Data>
    void push_constants(const PipelineLayout& layout, const Data& data, std::uint32_t offset = 0)
    {
        const ShaderStage stages = push_constant_stages(layout, offset, sizeof(Data));
        m_command_buffer.record_push_constants(layout.handle(), stages, offset, std::as_bytes(std::span{&data, 1}));
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

protected:
    explicit RenderingCommands(CommandBuffer& command_buffer)
        : m_command_buffer{command_buffer}
    {}

    ~RenderingCommands() = default;

protected:
    CommandBuffer& m_command_buffer;
};

template<QueueType Type>
class Recorder;

/// @brief Commands of a dynamic rendering pass; obtained from Recorder::begin_rendering(), ends the
///        pass when destroyed.
template<QueueType Type>
class RenderingScope final : public RenderingCommands
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

private:
    friend class Recorder<Type>;

    explicit RenderingScope(CommandBuffer& command_buffer)
        : RenderingCommands{command_buffer}
    {}
};

/// @brief A rendering pass whose draws other threads record into secondary command buffers; obtained
///        from Recorder::begin_parallel_rendering(), ends the pass when destroyed. Inside the pass, the
///        primary command buffer only executes the recorded parts.
///
/// Workflow: hand inheritance() to the recording threads; each records a part with a
/// RenderingPartRecorder on a secondary command buffer of its own CommandPool; once every part's
/// recorder is destroyed, execute() runs the parts in the order given, whatever order they finished in.
class ParallelRenderingScope final
{
public:
    ~ParallelRenderingScope()
    {
        m_command_buffer.record_end_rendering();
    }

    ParallelRenderingScope(const ParallelRenderingScope&) = delete;
    ParallelRenderingScope(ParallelRenderingScope&&) = delete;
    auto operator=(const ParallelRenderingScope&) -> ParallelRenderingScope& = delete;
    auto operator=(ParallelRenderingScope&&) -> ParallelRenderingScope& = delete;

    /// @brief What the parts must be recorded with (safe to read from any thread).
    [[nodiscard]] auto inheritance() const noexcept -> const RenderingInheritance&
    {
        return m_inheritance;
    }

    /// @brief Executes recorded parts, in this order.
    void execute(std::span<const CommandBufferHandle> parts)
    {
        m_command_buffer.record_execute_commands(parts);
    }

    /// @brief Executes recorded parts, in this order.
    /// @throws InvalidUsageError when a part is not a secondary command buffer.
    void execute(std::span<const std::unique_ptr<CommandBuffer>> parts)
    {
        // Every part must be a secondary command buffer; collect their handles.
        std::vector<CommandBufferHandle> handles;
        handles.reserve(parts.size());
        for (const std::unique_ptr<CommandBuffer>& part : parts)
        {
            if (part->level() != CommandBufferLevel::SECONDARY)
            {
                throw InvalidUsageError({
                    .backend = part->backend(),
                    .operation = "ParallelRenderingScope::execute",
                    .problem = std::format("'{}' is a {} command buffer; the parts of a parallel rendering pass are "
                                           "secondary command buffers",
                                           part->debug_name(), part->level()),
                    .suggestion = "allocate the parts with CommandBufferLevel::SECONDARY and record them with RenderingPartRecorder",
                });
            }
            handles.push_back(part->handle());
        }
        execute(handles);
    }

private:
    template<QueueType Type>
    friend class Recorder;

    ParallelRenderingScope(CommandBuffer& command_buffer, const RenderingDescription& rendering)
        : m_command_buffer{command_buffer}
        , m_inheritance{command_buffer.record_begin_parallel_rendering(rendering)}
    {}

private:
    CommandBuffer& m_command_buffer;
    RenderingInheritance m_inheritance;
};

/// @brief Records one part of a parallel rendering pass into a secondary command buffer: begins it for
///        the pass described by an inheritance (ParallelRenderingScope::inheritance()) and ends it when
///        destroyed. Only commands allowed inside a rendering pass exist on it: barriers, copies and
///        nested passes do not compile.
class RenderingPartRecorder final : public RenderingCommands
{
public:
    /// @brief Begins recording the part.
    /// @throws InvalidUsageError when @p command_buffer is not a secondary command buffer of a graphics
    ///         queue, or @p inheritance does not come from a parallel rendering pass.
    RenderingPartRecorder(CommandBuffer& command_buffer, const RenderingInheritance& inheritance)
        : RenderingCommands{command_buffer}
    {
        // A secondary command buffer of a queue that renders, for a real pass. (No lambda here: GCC 16
        // writes an unreadable module file for a capturing lambda in the constructor of a class that
        // another partition declares, and :command_buffer declares this one; see
        // CompilationFailure.LambdaInConstructorCorruptsModuleFile.)
        if (command_buffer.level() != CommandBufferLevel::SECONDARY)
        {
            reject(command_buffer,
                   std::format("'{}' is a {} command buffer; parts are recorded into secondary command buffers",
                               command_buffer.debug_name(), command_buffer.level()),
                   "allocate it with CommandBufferLevel::SECONDARY");
        }
        if (!has_any(queue_type_capabilities(command_buffer.queue_type()), QueueCapability::GRAPHICS))
        {
            reject(command_buffer,
                   std::format("'{}' belongs to a {} queue, which cannot render", command_buffer.debug_name(),
                               command_buffer.queue_type()),
                   "allocate it from a command pool of a graphics queue");
        }
        if (!inheritance.primary.is_valid())
        {
            reject(command_buffer, "the inheritance does not describe a parallel rendering pass",
                   "use ParallelRenderingScope::inheritance() of the pass the part belongs to");
        }

        // Start recording the part.
        m_command_buffer.record_begin_rendering_part(inheritance);
    }

    ~RenderingPartRecorder()
    {
        m_command_buffer.record_end();
    }

    RenderingPartRecorder(const RenderingPartRecorder&) = delete;
    RenderingPartRecorder(RenderingPartRecorder&&) = delete;
    auto operator=(const RenderingPartRecorder&) -> RenderingPartRecorder& = delete;
    auto operator=(RenderingPartRecorder&&) -> RenderingPartRecorder& = delete;

private:
    [[noreturn]] static void reject(const CommandBuffer& command_buffer, std::string problem, std::string suggestion)
    {
        throw InvalidUsageError({
            .backend = command_buffer.backend(),
            .operation = "RenderingPartRecorder::RenderingPartRecorder",
            .problem = std::move(problem),
            .suggestion = std::move(suggestion),
        });
    }
};

/// @brief Records commands for queue type @p Type into a primary CommandBuffer: begins recording when
///        constructed, ends it when destroyed.
template<QueueType Type>
class Recorder
{
public:
    /// @brief Kinds of work this recorder records.
    static constexpr QueueCapability CAPABILITIES = queue_type_capabilities(Type);

    /// @brief Begins recording.
    /// @throws InvalidUsageError when @p command_buffer is a secondary command buffer, or belongs to a
    ///         queue type that cannot run every kind of work of @p Type (e.g. a compute recorder on a
    ///         transfer command buffer).
    explicit Recorder(CommandBuffer& command_buffer)
        : m_command_buffer{command_buffer}
    {
        // Secondary command buffers only record parts of parallel rendering passes.
        if (command_buffer.level() != CommandBufferLevel::PRIMARY)
        {
            throw InvalidUsageError({
                .backend = command_buffer.backend(),
                .operation = "Recorder::Recorder",
                .problem = std::format("'{}' is a secondary command buffer", command_buffer.debug_name()),
                .suggestion = "record parts of a parallel rendering pass with RenderingPartRecorder, and everything "
                              "else into primary command buffers",
            });
        }

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

    /// @brief Sets push-constant bytes (unchecked: the stages must match the layout's ranges).
    void push_constants(PipelineLayoutHandle layout, ShaderStage stages, std::uint32_t offset, std::span<const std::byte> data)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        m_command_buffer.record_push_constants(layout, stages, offset, data);
    }

    /// @brief Sets the push constants at @p offset to the bytes of @p data, for the stages whose
    ///        push-constant ranges in @p layout hold them.
    /// @throws InvalidUsageError when the bytes are not inside the layout's push-constant ranges.
    template<PushConstantData Data>
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    void push_constants(const PipelineLayout& layout, const Data& data, std::uint32_t offset = 0)
    {
        const ShaderStage stages = push_constant_stages(layout, offset, sizeof(Data));
        m_command_buffer.record_push_constants(layout.handle(), stages, offset, std::as_bytes(std::span{&data, 1}));
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

    /// @brief Begins a dynamic rendering pass recorded by several threads: each records a part into a
    ///        secondary command buffer (RenderingPartRecorder), and the returned scope executes the parts
    ///        in order and ends the pass when destroyed.
    [[nodiscard]] auto begin_parallel_rendering(const RenderingDescription& rendering) -> ParallelRenderingScope
        requires (has_any(CAPABILITIES, QueueCapability::GRAPHICS))
    {
        return ParallelRenderingScope{m_command_buffer, rendering};
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
