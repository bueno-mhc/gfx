/// @file recorder.cppm
/// @brief Typed command recording: Recorder<QueueType>, the rendering scopes, and the recorder of the
///        parts of a parallel rendering pass.
///
/// All are zero-cost, non-virtual wrappers around a CommandBuffer. Their member functions are
/// constrained by the capabilities of the queue type, so invalid recordings are compile errors:
/// - Recorder<QueueType::TRANSFER> has copies and barriers, but no dispatch;
/// - Recorder<QueueType::COMPUTE> adds compute and ray tracing pipelines, push constants, dispatches,
///   ray tracing and acceleration structure builds;
/// - Recorder<QueueType::GRAPHICS> adds blits, resolves, image clears, begin_rendering(), which returns
///   the RenderingScope that holds every draw command (draws cannot be recorded outside a rendering
///   pass), and begin_parallel_rendering(), which returns a ParallelRenderingScope: other threads record
///   the draws into secondary command buffers with RenderingPartRecorder, and the scope executes them
///   in order.
/// All are RAII: recorders end their command buffer and scopes end their rendering pass.
///
/// AZDO: recording allocates nothing, takes no lock and checks per-command arguments only at
/// ValidationLevel::COMMANDS (Debug builds by default). With the bindless pipeline layout, a draw needs
/// a pipeline bind (when it changes), push constants and the draw itself.
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
import :acceleration_structure;
import :ray_tracing_pipeline;

export namespace gfx
{

/// @brief Data that can be pushed as push constants: trivially copyable, and neither a pointer nor a
///        span (whose bytes would be the address, not the data).
template<class Data>
concept PushConstantData = std::is_trivially_copyable_v<Data> && !std::is_pointer_v<Data> &&
                           !requires { typename Data::element_type; Data::extent; };

/// @brief Stages a push-constant write of @p size bytes at @p offset must name: the stages of every
///        push-constant range of @p layout overlapping those bytes.
/// @throws InvalidUsageError at ValidationLevel::COMMANDS when some of the bytes are outside every
///         push-constant range.
[[nodiscard]] auto
push_constant_stages(const PipelineLayout& layout,
                     std::uint32_t offset,
                     std::uint32_t size) -> ShaderStage
{
    const std::vector<PushConstantRange>& ranges = layout.definition().push_constant_ranges;
    const std::uint32_t end = offset + size;

    // Every byte must be inside some range: walk from range to range.
    if (layout.validates(ValidationLevel::COMMANDS))
    {
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
///        bindings, push constants, attachment clears and draws. Base of RenderingScope and
///        RenderingPartRecorder.
class RenderingCommands
{
public:
    RenderingCommands(const RenderingCommands&) = delete;
    RenderingCommands(RenderingCommands&&) = delete;
    auto operator=(const RenderingCommands&) -> RenderingCommands& = delete;
    auto operator=(RenderingCommands&&) -> RenderingCommands& = delete;

    // ----------------------------------------------------------------------------- always-dynamic state

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

    /// @brief Sets the stencil reference value of @p faces.
    void set_stencil_reference(StencilFace faces, std::uint32_t reference)
    {
        m_command_buffer.record_set_stencil_reference(faces, reference);
    }

    /// @brief Sets the BlendFactor::CONSTANT_* values.
    void set_blend_constants(const std::array<float, 4>& constants)
    {
        m_command_buffer.record_set_blend_constants(constants);
    }

    /// @brief Sets the shading rate of the following draws (DeviceFeature::VARIABLE_RATE_SHADING).
    /// @param combiners How the rate combines with the primitive's rate, then with the attachment's rate.
    void set_fragment_shading_rate(ShadingRate rate,
                                   const std::array<ShadingRateCombiner, 2>& combiners = {ShadingRateCombiner::KEEP,
                                                                                          ShadingRateCombiner::KEEP})
    {
        m_command_buffer.record_set_fragment_shading_rate(rate, combiners);
    }

    // ----------------------------------------------------------------------------- state the pipeline declares dynamic

    /// @brief Sets the depth bias (DynamicState::DEPTH_BIAS).
    void set_depth_bias(float constant_factor, float clamp, float slope_factor)
    {
        m_command_buffer.record_set_depth_bias(constant_factor, clamp, slope_factor);
    }

    /// @brief Sets the depth bounds (DynamicState::DEPTH_BOUNDS).
    void set_depth_bounds(float minimum, float maximum)
    {
        m_command_buffer.record_set_depth_bounds(minimum, maximum);
    }

    /// @brief Sets the line width (DynamicState::LINE_WIDTH).
    void set_line_width(float width)
    {
        m_command_buffer.record_set_line_width(width);
    }

    /// @brief Sets the culled faces (DynamicState::CULL_MODE).
    void set_cull_mode(CullMode mode)
    {
        m_command_buffer.record_set_cull_mode(mode);
    }

    /// @brief Sets the winding of front faces (DynamicState::FRONT_FACE).
    void set_front_face(FrontFace front_face)
    {
        m_command_buffer.record_set_front_face(front_face);
    }

    /// @brief Sets the topology, within the pipeline's topology class (DynamicState::PRIMITIVE_TOPOLOGY).
    void set_primitive_topology(PrimitiveTopology topology)
    {
        m_command_buffer.record_set_primitive_topology(topology);
    }

    /// @brief Enables the depth test (DynamicState::DEPTH_TEST).
    void set_depth_test(bool enable)
    {
        m_command_buffer.record_set_depth_test(enable);
    }

    /// @brief Enables depth writes (DynamicState::DEPTH_WRITE).
    void set_depth_write(bool enable)
    {
        m_command_buffer.record_set_depth_write(enable);
    }

    /// @brief Sets the depth comparison (DynamicState::DEPTH_COMPARE).
    void set_depth_compare(CompareOperation operation)
    {
        m_command_buffer.record_set_depth_compare(operation);
    }

    /// @brief Enables the stencil test (DynamicState::STENCIL_TEST).
    void set_stencil_test(bool enable)
    {
        m_command_buffer.record_set_stencil_test(enable);
    }

    /// @brief Sets the stencil operations of @p faces (DynamicState::STENCIL_OPERATION).
    void set_stencil_operation(StencilFace faces,
                               StencilOperation fail,
                               StencilOperation pass,
                               StencilOperation depth_fail,
                               CompareOperation compare)
    {
        m_command_buffer.record_set_stencil_operation(faces, fail, pass, depth_fail, compare);
    }

    // ----------------------------------------------------------------------------- bindings

    /// @brief Binds a graphics pipeline for the following draws.
    void bind_pipeline(GraphicsPipelineHandle pipeline)
    {
        ++m_command_buffer.m_statistics.pipeline_binds;
        m_command_buffer.record_bind_graphics_pipeline(pipeline);
    }

    /// @brief Binds classic descriptor sets for graphics pipelines, starting at set @p first_set (the
    ///        bindless table needs no binding).
    void bind_descriptor_sets(PipelineLayoutHandle layout,
                              std::uint32_t first_set,
                              std::span<const DescriptorSetHandle> sets,
                              std::span<const std::uint32_t> dynamic_offsets = {})
    {
        ++m_command_buffer.m_statistics.descriptor_set_binds;
        m_command_buffer.record_bind_descriptor_sets(PipelineBindPoint::GRAPHICS, layout, first_set, sets, dynamic_offsets);
    }

    /// @brief Sets push-constant bytes (unchecked: the stages must match the layout's ranges).
    void push_constants(PipelineLayoutHandle layout,
                        ShaderStage stages,
                        std::uint32_t offset,
                        std::span<const std::byte> data)
    {
        ++m_command_buffer.m_statistics.push_constant_writes;
        m_command_buffer.record_push_constants(layout, stages, offset, data);
    }

    /// @brief Sets the push constants at @p offset to the bytes of @p data, for the stages whose
    ///        push-constant ranges in @p layout hold them.
    /// @throws InvalidUsageError at ValidationLevel::COMMANDS when the bytes are outside the ranges.
    template<PushConstantData Data>
    void push_constants(const PipelineLayout& layout, const Data& data, std::uint32_t offset = 0)
    {
        const ShaderStage stages = push_constant_stages(layout, offset, sizeof(Data));
        ++m_command_buffer.m_statistics.push_constant_writes;
        m_command_buffer.record_push_constants(layout.handle(), stages, offset, std::as_bytes(std::span{&data, 1}));
    }

    /// @brief Sets the push constants of the bindless pipeline layout (every stage) at @p offset: the
    ///        usual way to pass bindless indices and per-draw values.
    template<PushConstantData Data>
    void push_constants(const Data& data, std::uint32_t offset = 0)
    {
        push_constants(m_command_buffer.bindless_pipeline_layout(), data, offset);
    }

    /// @brief Binds vertex buffers starting at binding @p first_binding (bindless vertex pulling needs none).
    void bind_vertex_buffers(std::uint32_t first_binding, std::span<const VertexBufferBinding> buffers)
    {
        ++m_command_buffer.m_statistics.vertex_buffer_binds;
        m_command_buffer.record_bind_vertex_buffers(first_binding, buffers);
    }

    /// @brief Binds the index buffer.
    void bind_index_buffer(BufferHandle buffer, std::uint64_t offset, IndexType type)
    {
        ++m_command_buffer.m_statistics.vertex_buffer_binds;
        m_command_buffer.record_bind_index_buffer(buffer, offset, type);
    }

    // ----------------------------------------------------------------------------- clears and draws

    /// @brief Clears regions of attachments of the current pass.
    void clear_attachments(std::span<const ClearAttachment> attachments, std::span<const ClearRectangle> rectangles)
    {
        ++m_command_buffer.m_statistics.transfers;
        m_command_buffer.record_clear_attachments(attachments, rectangles);
    }

    /// @brief Draws non-indexed primitives.
    void draw(const DrawArguments& arguments)
    {
        ++m_command_buffer.m_statistics.draws;
        m_command_buffer.record_draw(arguments);
    }

    /// @brief Draws indexed primitives.
    void draw_indexed(const DrawIndexedArguments& arguments)
    {
        ++m_command_buffer.m_statistics.draws;
        m_command_buffer.record_draw_indexed(arguments);
    }

    /// @brief Draws with DrawArguments records read from a buffer (multi-draw).
    void draw_indirect(const IndirectDrawArguments& arguments)
    {
        ++m_command_buffer.m_statistics.indirect_draws;
        m_command_buffer.record_draw_indirect(arguments);
    }

    /// @brief Draws with DrawIndexedArguments records read from a buffer (multi-draw).
    void draw_indexed_indirect(const IndirectDrawArguments& arguments)
    {
        ++m_command_buffer.m_statistics.indirect_draws;
        m_command_buffer.record_draw_indexed_indirect(arguments);
    }

    /// @brief Draws with DrawArguments records whose number is read from a buffer (GPU-driven rendering;
    ///        DeviceFeature::DRAW_INDIRECT_COUNT).
    void draw_indirect_count(const IndirectDrawArguments& arguments, const IndirectCount& count)
    {
        check_indirect_count(count, "draw_indirect_count");
        ++m_command_buffer.m_statistics.indirect_draws;
        m_command_buffer.record_draw_indirect_count(arguments, count);
    }

    /// @brief Draws with DrawIndexedArguments records whose number is read from a buffer.
    void draw_indexed_indirect_count(const IndirectDrawArguments& arguments, const IndirectCount& count)
    {
        check_indirect_count(count, "draw_indexed_indirect_count");
        ++m_command_buffer.m_statistics.indirect_draws;
        m_command_buffer.record_draw_indexed_indirect_count(arguments, count);
    }

    /// @brief Launches task (or mesh) workgroups of a mesh pipeline (DeviceFeature::MESH_SHADER).
    void draw_mesh_tasks(const DrawMeshTasksArguments& arguments)
    {
        ++m_command_buffer.m_statistics.draws;
        m_command_buffer.record_draw_mesh_tasks(arguments);
    }

    /// @brief Launches mesh workgroups with DrawMeshTasksArguments records read from a buffer.
    void draw_mesh_tasks_indirect(const IndirectDrawArguments& arguments)
    {
        ++m_command_buffer.m_statistics.indirect_draws;
        m_command_buffer.record_draw_mesh_tasks_indirect(arguments);
    }

    /// @brief Launches mesh workgroups with records whose number is read from a buffer.
    void draw_mesh_tasks_indirect_count(const IndirectDrawArguments& arguments, const IndirectCount& count)
    {
        check_indirect_count(count, "draw_mesh_tasks_indirect_count");
        ++m_command_buffer.m_statistics.indirect_draws;
        m_command_buffer.record_draw_mesh_tasks_indirect_count(arguments, count);
    }

    // ----------------------------------------------------------------------------- debugging

    /// @brief Opens a labeled region shown by debugging tools (and a GPU breadcrumb, see CrashDiagnostic).
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

private:
    void check_indirect_count(const IndirectCount& count, std::string_view operation) const
    {
        if (m_command_buffer.validates(ValidationLevel::COMMANDS) && (!count.buffer.is_valid() || count.maximum_draw_count == 0))
        {
            throw InvalidUsageError({
                .backend = m_command_buffer.backend(),
                .operation = std::string{operation},
                .problem = std::format("the count buffer is {} and maximum_draw_count is {}", count.buffer,
                                       count.maximum_draw_count),
                .suggestion = "name the buffer holding the count, and an upper bound of at least 1",
            });
        }
    }
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

    /// @brief Executes recorded parts, in this order (without allocating).
    /// @throws InvalidUsageError at ValidationLevel::CREATION when a part is not a secondary command buffer.
    void execute(std::span<const std::unique_ptr<CommandBuffer>> parts)
    {
        // Forward the handles in fixed-size chunks: no allocation while recording.
        constexpr std::size_t CHUNK = 64;
        std::array<CommandBufferHandle, CHUNK> handles{};
        std::size_t used = 0;
        for (const std::unique_ptr<CommandBuffer>& part : parts)
        {
            if (m_command_buffer.validates(ValidationLevel::CREATION) && part->level() != CommandBufferLevel::SECONDARY)
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
            handles[used++] = part->handle();
            if (used == CHUNK)
            {
                execute(std::span<const CommandBufferHandle>{handles.data(), used});
                used = 0;
            }
        }
        if (used != 0)
        {
            execute(std::span<const CommandBufferHandle>{handles.data(), used});
        }
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
    /// @throws InvalidUsageError at ValidationLevel::CREATION when @p command_buffer is not a secondary
    ///         command buffer of a graphics queue, or @p inheritance does not come from a parallel pass.
    RenderingPartRecorder(CommandBuffer& command_buffer, const RenderingInheritance& inheritance)
        : RenderingCommands{command_buffer}
    {
        // A secondary command buffer of a queue that renders, for a real pass. (No lambda here: GCC 16
        // writes an unreadable module file for a capturing lambda in the constructor of a class that
        // another partition declares, and :command_buffer declares this one; see
        // CompilationFailure.LambdaInConstructorCorruptsModuleFile.)
        if (command_buffer.validates(ValidationLevel::CREATION))
        {
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
        }

        // Start recording the part.
        m_command_buffer.m_statistics = {};
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
///        constructed (the bindless table is bound from the start), ends it when destroyed.
template<QueueType Type>
class Recorder
{
public:
    /// @brief Kinds of work this recorder records.
    static constexpr QueueCapability CAPABILITIES = queue_type_capabilities(Type);

    /// @brief Begins recording.
    /// @throws InvalidUsageError at ValidationLevel::CREATION when @p command_buffer is a secondary
    ///         command buffer, or belongs to a queue type that cannot run every kind of work of @p Type
    ///         (e.g. a compute recorder on a transfer command buffer).
    explicit Recorder(CommandBuffer& command_buffer)
        : m_command_buffer{command_buffer}
    {
        if (command_buffer.validates(ValidationLevel::CREATION))
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
        }

        // Start recording.
        m_command_buffer.m_statistics = {};
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
        ++m_command_buffer.m_statistics.barriers;
        m_command_buffer.record_barrier(barriers);
    }

    /// @brief Copies bytes between buffers.
    void copy_buffer(BufferHandle source, BufferHandle destination, std::span<const BufferCopyRegion> regions)
        requires (has_any(CAPABILITIES, QueueCapability::TRANSFER))
    {
        ++m_command_buffer.m_statistics.transfers;
        m_command_buffer.record_copy_buffer(source, destination, regions);
    }

    /// @brief Copies texels from a buffer to an image (image in TRANSFER_DESTINATION or GENERAL layout).
    void copy_buffer_to_image(BufferHandle source,
                              ImageHandle destination,
                              ImageLayout destination_layout,
                              std::span<const BufferImageCopyRegion> regions)
        requires (has_any(CAPABILITIES, QueueCapability::TRANSFER))
    {
        ++m_command_buffer.m_statistics.transfers;
        m_command_buffer.record_copy_buffer_to_image(source, destination, destination_layout, regions);
    }

    /// @brief Copies texels from an image to a buffer (image in TRANSFER_SOURCE or GENERAL layout).
    void copy_image_to_buffer(ImageHandle source,
                              ImageLayout source_layout,
                              BufferHandle destination,
                              std::span<const BufferImageCopyRegion> regions)
        requires (has_any(CAPABILITIES, QueueCapability::TRANSFER))
    {
        ++m_command_buffer.m_statistics.transfers;
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
        ++m_command_buffer.m_statistics.transfers;
        m_command_buffer.record_copy_image(source, source_layout, destination, destination_layout, regions);
    }

    /// @brief Fills a buffer range with a repeated 32-bit value.
    void fill_buffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value)
        requires (has_any(CAPABILITIES, QueueCapability::TRANSFER))
    {
        ++m_command_buffer.m_statistics.transfers;
        m_command_buffer.record_fill_buffer(buffer, offset, size, value);
    }

    /// @brief Writes a few bytes (up to 65536) inline into a buffer.
    void update_buffer(BufferHandle buffer, std::uint64_t offset, std::span<const std::byte> data)
        requires (has_any(CAPABILITIES, QueueCapability::TRANSFER))
    {
        ++m_command_buffer.m_statistics.transfers;
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

    /// @brief Opens a labeled region shown by debugging tools (and a GPU breadcrumb, see CrashDiagnostic).
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
        ++m_command_buffer.m_statistics.transfers;
        m_command_buffer.record_clear_color_image(image, layout, color, range);
    }

    /// @brief Binds a compute pipeline for the following dispatches.
    void bind_pipeline(ComputePipelineHandle pipeline)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        ++m_command_buffer.m_statistics.pipeline_binds;
        m_command_buffer.record_bind_compute_pipeline(pipeline);
    }

    /// @brief Binds classic descriptor sets for compute pipelines, starting at set @p first_set (the
    ///        bindless table needs no binding).
    void bind_descriptor_sets(PipelineLayoutHandle layout,
                              std::uint32_t first_set,
                              std::span<const DescriptorSetHandle> sets,
                              std::span<const std::uint32_t> dynamic_offsets = {})
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        ++m_command_buffer.m_statistics.descriptor_set_binds;
        m_command_buffer.record_bind_descriptor_sets(PipelineBindPoint::COMPUTE, layout, first_set, sets, dynamic_offsets);
    }

    /// @brief Sets push-constant bytes (unchecked: the stages must match the layout's ranges).
    void push_constants(PipelineLayoutHandle layout, ShaderStage stages, std::uint32_t offset, std::span<const std::byte> data)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        ++m_command_buffer.m_statistics.push_constant_writes;
        m_command_buffer.record_push_constants(layout, stages, offset, data);
    }

    /// @brief Sets the push constants at @p offset to the bytes of @p data, for the stages whose
    ///        push-constant ranges in @p layout hold them.
    /// @throws InvalidUsageError at ValidationLevel::COMMANDS when the bytes are outside the ranges.
    template<PushConstantData Data>
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    void push_constants(const PipelineLayout& layout, const Data& data, std::uint32_t offset = 0)
    {
        const ShaderStage stages = push_constant_stages(layout, offset, sizeof(Data));
        ++m_command_buffer.m_statistics.push_constant_writes;
        m_command_buffer.record_push_constants(layout.handle(), stages, offset, std::as_bytes(std::span{&data, 1}));
    }

    /// @brief Sets the push constants of the bindless pipeline layout (every stage) at @p offset.
    template<PushConstantData Data>
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    void push_constants(const Data& data, std::uint32_t offset = 0)
    {
        push_constants(m_command_buffer.bindless_pipeline_layout(), data, offset);
    }

    /// @brief Dispatches compute workgroups.
    void dispatch(std::uint32_t group_count_x, std::uint32_t group_count_y = 1, std::uint32_t group_count_z = 1)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        ++m_command_buffer.m_statistics.dispatches;
        m_command_buffer.record_dispatch(group_count_x, group_count_y, group_count_z);
    }

    /// @brief Dispatches with a DispatchArguments record read from a buffer.
    void dispatch_indirect(BufferHandle buffer, std::uint64_t offset)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        ++m_command_buffer.m_statistics.dispatches;
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
        ++m_command_buffer.m_statistics.barriers;
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

    // ----------------------------------------------------------------------------- ray tracing (compute-capable queues)

    /// @brief Builds or updates acceleration structures; the builds run in parallel on the GPU.
    void build_acceleration_structures(std::span<const AccelerationStructureBuildDescription> builds)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        m_command_buffer.record_build_acceleration_structures(builds);
    }

    /// @brief Copies an acceleration structure, cloning or compacting it.
    void copy_acceleration_structure(AccelerationStructureHandle source,
                                     AccelerationStructureHandle destination,
                                     AccelerationStructureCopyMode mode = AccelerationStructureCopyMode::CLONE)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        m_command_buffer.record_copy_acceleration_structure(source, destination, mode);
    }

    /// @brief Writes the compacted sizes of @p structures into ACCELERATION_STRUCTURE_COMPACTED_SIZE
    ///        queries [first_query, first_query + structures.size()).
    void write_acceleration_structure_properties(std::span<const AccelerationStructureHandle> structures,
                                                 QueryPoolHandle pool,
                                                 std::uint32_t first_query)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        m_command_buffer.record_write_acceleration_structure_properties(structures, pool, first_query);
    }

    /// @brief Binds a ray tracing pipeline for the following trace_rays().
    void bind_pipeline(RayTracingPipelineHandle pipeline)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        ++m_command_buffer.m_statistics.pipeline_binds;
        m_command_buffer.record_bind_ray_tracing_pipeline(pipeline);
    }

    /// @brief Launches width x height x depth ray generation invocations.
    /// @throws InvalidUsageError at ValidationLevel::COMMANDS when the ray generation region is not exactly
    ///         one record.
    void trace_rays(const ShaderBindingTable& table, std::uint32_t width, std::uint32_t height = 1, std::uint32_t depth = 1)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        check_shader_binding_table(table);
        ++m_command_buffer.m_statistics.ray_traces;
        m_command_buffer.record_trace_rays(table, width, height, depth);
    }

    /// @brief Launches ray generation invocations whose dimensions (3 x uint32) are read from a buffer.
    void trace_rays_indirect(const ShaderBindingTable& table, BufferHandle buffer, std::uint64_t offset)
        requires (has_any(CAPABILITIES, QueueCapability::COMPUTE))
    {
        check_shader_binding_table(table);
        ++m_command_buffer.m_statistics.ray_traces;
        m_command_buffer.record_trace_rays_indirect(table, buffer, offset);
    }

    // ----------------------------------------------------------------------------- graphics queues

    /// @brief Copies regions between images with scaling and format conversion.
    void blit_image(ImageHandle source,
                    ImageLayout source_layout,
                    ImageHandle destination,
                    ImageLayout destination_layout,
                    std::span<const ImageBlitRegion> regions,
                    Filter filter = Filter::LINEAR)
        requires (has_any(CAPABILITIES, QueueCapability::GRAPHICS))
    {
        ++m_command_buffer.m_statistics.transfers;
        m_command_buffer.record_blit_image(source, source_layout, destination, destination_layout, regions, filter);
    }

    /// @brief Resolves a multisampled image into a single-sampled one.
    void resolve_image(ImageHandle source,
                       ImageLayout source_layout,
                       ImageHandle destination,
                       ImageLayout destination_layout,
                       std::span<const ImageResolveRegion> regions)
        requires (has_any(CAPABILITIES, QueueCapability::GRAPHICS))
    {
        ++m_command_buffer.m_statistics.transfers;
        m_command_buffer.record_resolve_image(source, source_layout, destination, destination_layout, regions);
    }

    /// @brief Clears a depth/stencil image outside rendering passes.
    void clear_depth_stencil_image(ImageHandle image,
                                   ImageLayout layout,
                                   float depth,
                                   std::uint32_t stencil,
                                   const ImageSubresourceRange& range)
        requires (has_any(CAPABILITIES, QueueCapability::GRAPHICS))
    {
        ++m_command_buffer.m_statistics.transfers;
        m_command_buffer.record_clear_depth_stencil_image(image, layout, depth, stencil, range);
    }

    /// @brief Fills mip levels 1 .. mip_level_count - 1 of a color image by successive linear blits from
    ///        level 0, with the barriers in between. Every level is in state @p before on entry and in
    ///        state @p after on exit.
    /// @param extent            Size of level 0.
    /// @param mip_level_count   Levels of the image.
    /// @param array_layer_count Layers processed.
    void generate_mipmaps(ImageHandle image,
                          Extent extent,
                          std::uint32_t mip_level_count,
                          ResourceState before,
                          ResourceState after,
                          std::uint32_t array_layer_count = 1)
        requires (has_any(CAPABILITIES, QueueCapability::GRAPHICS))
    {
        const auto levels = [&](std::uint32_t first, std::uint32_t count)
        {
            return ImageSubresourceRange{.aspects = ImageAspect::COLOR,
                                         .base_mip_level = first,
                                         .mip_level_count = count,
                                         .base_array_layer = 0,
                                         .array_layer_count = array_layer_count};
        };
        if (mip_level_count < 2)
        {
            // Nothing to generate: only move the image to its final state.
            const std::array transition{image_barrier(image, before, after, levels(0, 1))};
            barrier({.image_barriers = transition});
            return;
        }

        // Level 0 becomes the first source, every other level a destination.
        const std::array start{image_barrier(image, before, ResourceState::TRANSFER_SOURCE, levels(0, 1)),
                               image_barrier(image, before, ResourceState::TRANSFER_DESTINATION, levels(1, mip_level_count - 1))};
        barrier({.image_barriers = start});

        // Each level is a linear downscale of the previous one, then becomes the next source.
        const auto half = [](std::uint32_t value)
        {
            return std::max(value / 2, 1U);
        };
        const auto corner = [](const Extent& size)
        {
            return Offset{.x = static_cast<std::int32_t>(size.width),
                          .y = static_cast<std::int32_t>(size.height),
                          .z = static_cast<std::int32_t>(size.depth)};
        };
        Extent source = extent;
        for (std::uint32_t level = 1; level < mip_level_count; ++level)
        {
            const Extent destination{.width = half(source.width), .height = half(source.height), .depth = half(source.depth)};
            const std::array region{ImageBlitRegion{
                .source_subresource = {.aspects = ImageAspect::COLOR, .mip_level = level - 1, .base_array_layer = 0,
                                       .array_layer_count = array_layer_count},
                .source_bounds = {Offset{}, corner(source)},
                .destination_subresource = {.aspects = ImageAspect::COLOR, .mip_level = level, .base_array_layer = 0,
                                            .array_layer_count = array_layer_count},
                .destination_bounds = {Offset{}, corner(destination)},
            }};
            blit_image(image, ImageLayout::TRANSFER_SOURCE, image, ImageLayout::TRANSFER_DESTINATION, region, Filter::LINEAR);
            const std::array next{image_barrier(image, ResourceState::TRANSFER_DESTINATION, ResourceState::TRANSFER_SOURCE,
                                                levels(level, 1))};
            barrier({.image_barriers = next});
            source = destination;
        }

        // Every level ends in the requested state.
        const std::array finish{image_barrier(image, ResourceState::TRANSFER_SOURCE, after, levels(0, mip_level_count))};
        barrier({.image_barriers = finish});
    }

    /// @brief Begins a dynamic rendering pass; draw commands are recorded on the returned scope, which
    ///        ends the pass when destroyed.
    [[nodiscard]] auto begin_rendering(const RenderingDescription& rendering) -> RenderingScope<Type>
        requires (has_any(CAPABILITIES, QueueCapability::GRAPHICS))
    {
        ++m_command_buffer.m_statistics.rendering_passes;
        m_command_buffer.record_begin_rendering(rendering);
        return RenderingScope<Type>{m_command_buffer};
    }

    /// @brief Begins a dynamic rendering pass recorded by several threads: each records a part into a
    ///        secondary command buffer (RenderingPartRecorder), and the returned scope executes the parts
    ///        in order and ends the pass when destroyed.
    [[nodiscard]] auto begin_parallel_rendering(const RenderingDescription& rendering) -> ParallelRenderingScope
        requires (has_any(CAPABILITIES, QueueCapability::GRAPHICS))
    {
        ++m_command_buffer.m_statistics.rendering_passes;
        return ParallelRenderingScope{m_command_buffer, rendering};
    }

private:
    void check_shader_binding_table(const ShaderBindingTable& table) const
    {
        const ShaderBindingTableRegion& ray_generation = table.ray_generation;
        if (m_command_buffer.validates(ValidationLevel::COMMANDS) &&
            (!ray_generation.buffer.is_valid() || ray_generation.stride == 0 || ray_generation.size != ray_generation.stride))
        {
            throw InvalidUsageError({
                .backend = m_command_buffer.backend(),
                .operation = "trace_rays",
                .problem = std::format("the ray generation region must hold exactly one record, but its buffer is {}, "
                                       "its stride {} and its size {}",
                                       ray_generation.buffer, ray_generation.stride, ray_generation.size),
                .suggestion = "build the table with shader_binding_table_layout() and ShaderBindingTableLayout::table()",
            });
        }
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
