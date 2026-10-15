/// @file command_buffer.cppm
/// @brief CommandBuffer: commands recorded for one queue type and submitted to a queue.
///
/// Commands are recorded through Recorder<QueueType> (and RenderingScope<QueueType>), whose member
/// functions only exist for queue types that can run them: a dispatch does not compile on a transfer
/// recorder and a draw does not compile outside a rendering scope. The recording primitives below are
/// therefore protected: backends implement them, users never call them directly.
///
/// AZDO: every command buffer begins with the device's bindless table bound for every bind point
/// (backends do it in record_begin() and record_begin_rendering_part()), so pipelines using the bindless
/// pipeline layout need no descriptor binding at all.
///
/// One rendering pass can be recorded by several threads: Recorder::begin_parallel_rendering() begins
/// it on a primary command buffer, each thread records a part into its own secondary command buffer
/// with a RenderingPartRecorder, and the primary executes the parts in order.
export module gfx.interface:command_buffer;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :common_types;
import :commands;
import :synchronization;
import :pipeline_layout;
import :acceleration_structure;
import :ray_tracing_pipeline;

export namespace gfx
{

template<QueueType Type>
class Recorder;

template<QueueType Type>
class RenderingScope;

class RenderingCommands;
class ParallelRenderingScope;
class RenderingPartRecorder;

/// @brief Commands recorded by one thread for one queue type. Not thread-safe, like every native
///        command buffer: threads record in parallel into different command buffers, each allocated from
///        the thread's own CommandPool (see FrameCommandPools), and the results are submitted in order.
class CommandBuffer : public BackendObject
{
public:
    /// @brief Handle naming this command buffer in submissions.
    [[nodiscard]] virtual auto handle() const noexcept -> CommandBufferHandle = 0;

    /// @brief Queue type of the pool it was allocated from.
    [[nodiscard]] virtual auto queue_type() const noexcept -> QueueType = 0;

    /// @brief Primary or secondary.
    [[nodiscard]] virtual auto level() const noexcept -> CommandBufferLevel = 0;

    /// @brief Discards the recorded commands (pool created with RESETTABLE_COMMAND_BUFFERS).
    virtual void reset() = 0;

    /// @brief The device's bindless pipeline layout (what push constants use by default).
    [[nodiscard]] virtual auto bindless_pipeline_layout() const noexcept -> const PipelineLayout& = 0;

    /// @brief What the last recording recorded (draws, binds, barriers, ...).
    [[nodiscard]] auto statistics() const noexcept -> const CommandStatistics&
    {
        return m_statistics;
    }

protected:
    template<QueueType Type>
    friend class Recorder;

    template<QueueType Type>
    friend class RenderingScope;

    friend class RenderingCommands;
    friend class ParallelRenderingScope;
    friend class RenderingPartRecorder;

    // Recording primitives, called only by Recorder and RenderingScope. The noexcept ones run in
    // destructors: a backend reports their failures when the command buffer is submitted.
    virtual void record_begin() = 0;
    virtual void record_begin_rendering_part(const RenderingInheritance& inheritance) = 0;
    virtual void record_end() noexcept = 0;
    virtual void record_barrier(const BarrierBatch& barriers) = 0;
    virtual void record_copy_buffer(BufferHandle source, BufferHandle destination, std::span<const BufferCopyRegion> regions) = 0;
    virtual void record_copy_buffer_to_image(BufferHandle source, ImageHandle destination, ImageLayout destination_layout,
                                             std::span<const BufferImageCopyRegion> regions) = 0;
    virtual void record_copy_image_to_buffer(ImageHandle source, ImageLayout source_layout, BufferHandle destination,
                                             std::span<const BufferImageCopyRegion> regions) = 0;
    virtual void record_copy_image(ImageHandle source, ImageLayout source_layout, ImageHandle destination,
                                   ImageLayout destination_layout, std::span<const ImageCopyRegion> regions) = 0;
    virtual void record_fill_buffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value) = 0;
    virtual void record_update_buffer(BufferHandle buffer, std::uint64_t offset, std::span<const std::byte> data) = 0;
    virtual void record_clear_color_image(ImageHandle image, ImageLayout layout, const std::array<float, 4>& color,
                                          const ImageSubresourceRange& range) = 0;
    virtual void record_bind_compute_pipeline(ComputePipelineHandle pipeline) = 0;
    virtual void record_bind_graphics_pipeline(GraphicsPipelineHandle pipeline) = 0;
    virtual void record_bind_descriptor_sets(PipelineBindPoint bind_point, PipelineLayoutHandle layout, std::uint32_t first_set,
                                             std::span<const DescriptorSetHandle> sets,
                                             std::span<const std::uint32_t> dynamic_offsets) = 0;
    virtual void record_push_constants(PipelineLayoutHandle layout, ShaderStage stages, std::uint32_t offset,
                                       std::span<const std::byte> data) = 0;
    virtual void record_dispatch(std::uint32_t group_count_x, std::uint32_t group_count_y, std::uint32_t group_count_z) = 0;
    virtual void record_dispatch_indirect(BufferHandle buffer, std::uint64_t offset) = 0;
    virtual void record_set_event(EventHandle event, const BarrierBatch& barriers) = 0;
    virtual void record_wait_events(std::span<const EventHandle> events, const BarrierBatch& barriers) = 0;
    virtual void record_reset_event(EventHandle event, PipelineStage stages) = 0;
    virtual void record_begin_rendering(const RenderingDescription& rendering) = 0;
    [[nodiscard]] virtual auto record_begin_parallel_rendering(const RenderingDescription& rendering) -> RenderingInheritance = 0;
    virtual void record_end_rendering() noexcept = 0;
    virtual void record_set_viewport(const Viewport& viewport) = 0;
    virtual void record_set_scissor(const Rectangle& scissor) = 0;
    virtual void record_bind_vertex_buffers(std::uint32_t first_binding, std::span<const VertexBufferBinding> buffers) = 0;
    virtual void record_bind_index_buffer(BufferHandle buffer, std::uint64_t offset, IndexType type) = 0;
    virtual void record_draw(const DrawArguments& arguments) = 0;
    virtual void record_draw_indexed(const DrawIndexedArguments& arguments) = 0;
    virtual void record_draw_indirect(const IndirectDrawArguments& arguments) = 0;
    virtual void record_draw_indexed_indirect(const IndirectDrawArguments& arguments) = 0;
    virtual void record_reset_query_pool(QueryPoolHandle pool, std::uint32_t first, std::uint32_t count) = 0;
    virtual void record_write_timestamp(QueryPoolHandle pool, PipelineStage stage, std::uint32_t query) = 0;
    virtual void record_begin_query(QueryPoolHandle pool, std::uint32_t query) = 0;
    virtual void record_end_query(QueryPoolHandle pool, std::uint32_t query) = 0;
    virtual void record_begin_debug_label(std::string_view name, const std::array<float, 4>& color) = 0;
    virtual void record_end_debug_label() = 0;
    virtual void record_execute_commands(std::span<const CommandBufferHandle> secondary_command_buffers) = 0;
    virtual void record_blit_image(ImageHandle source, ImageLayout source_layout, ImageHandle destination, ImageLayout destination_layout, std::span<const ImageBlitRegion> regions, Filter filter) = 0;
    virtual void record_resolve_image(ImageHandle source, ImageLayout source_layout, ImageHandle destination, ImageLayout destination_layout, std::span<const ImageResolveRegion> regions) = 0;
    virtual void record_clear_depth_stencil_image(ImageHandle image, ImageLayout layout, float depth, std::uint32_t stencil, const ImageSubresourceRange& range) = 0;
    virtual void record_clear_attachments(std::span<const ClearAttachment> attachments, std::span<const ClearRectangle> rectangles) = 0;
    virtual void record_set_stencil_reference(StencilFace faces, std::uint32_t reference) = 0;
    virtual void record_set_blend_constants(const std::array<float, 4>& constants) = 0;
    virtual void record_set_depth_bias(float constant_factor, float clamp, float slope_factor) = 0;
    virtual void record_set_depth_bounds(float minimum, float maximum) = 0;
    virtual void record_set_line_width(float width) = 0;
    virtual void record_set_cull_mode(CullMode mode) = 0;
    virtual void record_set_front_face(FrontFace front_face) = 0;
    virtual void record_set_primitive_topology(PrimitiveTopology topology) = 0;
    virtual void record_set_depth_test(bool enable) = 0;
    virtual void record_set_depth_write(bool enable) = 0;
    virtual void record_set_depth_compare(CompareOperation operation) = 0;
    virtual void record_set_stencil_test(bool enable) = 0;
    virtual void record_set_stencil_operation(StencilFace faces, StencilOperation fail, StencilOperation pass, StencilOperation depth_fail, CompareOperation compare) = 0;
    virtual void record_set_fragment_shading_rate(ShadingRate rate, const std::array<ShadingRateCombiner, 2>& combiners) = 0;
    virtual void record_draw_indirect_count(const IndirectDrawArguments& arguments, const IndirectCount& count) = 0;
    virtual void record_draw_indexed_indirect_count(const IndirectDrawArguments& arguments, const IndirectCount& count) = 0;
    virtual void record_draw_mesh_tasks(const DrawMeshTasksArguments& arguments) = 0;
    virtual void record_draw_mesh_tasks_indirect(const IndirectDrawArguments& arguments) = 0;
    virtual void record_draw_mesh_tasks_indirect_count(const IndirectDrawArguments& arguments, const IndirectCount& count) = 0;
    virtual void record_bind_ray_tracing_pipeline(RayTracingPipelineHandle pipeline) = 0;
    virtual void record_trace_rays(const ShaderBindingTable& table, std::uint32_t width, std::uint32_t height, std::uint32_t depth) = 0;
    virtual void record_trace_rays_indirect(const ShaderBindingTable& table, BufferHandle buffer, std::uint64_t offset) = 0;
    virtual void record_build_acceleration_structures(std::span<const AccelerationStructureBuildDescription> builds) = 0;
    virtual void record_copy_acceleration_structure(AccelerationStructureHandle source, AccelerationStructureHandle destination, AccelerationStructureCopyMode mode) = 0;
    virtual void record_write_acceleration_structure_properties(std::span<const AccelerationStructureHandle> structures, QueryPoolHandle pool, std::uint32_t first_query) = 0;

private:
    CommandStatistics m_statistics{};
};

// Default definitions: a backend forwards to these for the operations it does not implement.

void
CommandBuffer::reset()
{
    throw_not_implemented("CommandBuffer::reset");
}

void
CommandBuffer::record_begin()
{
    throw_not_implemented("CommandBuffer::record_begin");
}

void
CommandBuffer::record_begin_rendering_part(const RenderingInheritance& /*inheritance*/)
{
    throw_not_implemented("CommandBuffer::record_begin_rendering_part");
}

void
CommandBuffer::record_barrier(const BarrierBatch& /*barriers*/)
{
    throw_not_implemented("CommandBuffer::record_barrier");
}

void
CommandBuffer::record_copy_buffer(BufferHandle /*source*/,
                                  BufferHandle /*destination*/,
                                  std::span<const BufferCopyRegion> /*regions*/)
{
    throw_not_implemented("CommandBuffer::record_copy_buffer");
}

void
CommandBuffer::record_copy_buffer_to_image(BufferHandle /*source*/,
                                           ImageHandle /*destination*/,
                                           ImageLayout /*destination_layout*/,
                                           std::span<const BufferImageCopyRegion> /*regions*/)
{
    throw_not_implemented("CommandBuffer::record_copy_buffer_to_image");
}

void
CommandBuffer::record_copy_image_to_buffer(ImageHandle /*source*/,
                                           ImageLayout /*source_layout*/,
                                           BufferHandle /*destination*/,
                                           std::span<const BufferImageCopyRegion> /*regions*/)
{
    throw_not_implemented("CommandBuffer::record_copy_image_to_buffer");
}

void
CommandBuffer::record_copy_image(ImageHandle /*source*/,
                                 ImageLayout /*source_layout*/,
                                 ImageHandle /*destination*/,
                                 ImageLayout /*destination_layout*/,
                                 std::span<const ImageCopyRegion> /*regions*/)
{
    throw_not_implemented("CommandBuffer::record_copy_image");
}

void
CommandBuffer::record_fill_buffer(BufferHandle /*buffer*/,
                                  std::uint64_t /*offset*/,
                                  std::uint64_t /*size*/,
                                  std::uint32_t /*value*/)
{
    throw_not_implemented("CommandBuffer::record_fill_buffer");
}

void
CommandBuffer::record_update_buffer(BufferHandle /*buffer*/,
                                    std::uint64_t /*offset*/,
                                    std::span<const std::byte> /*data*/)
{
    throw_not_implemented("CommandBuffer::record_update_buffer");
}

void
CommandBuffer::record_clear_color_image(ImageHandle /*image*/,
                                        ImageLayout /*layout*/,
                                        const std::array<float, 4>& /*color*/,
                                        const ImageSubresourceRange& /*range*/)
{
    throw_not_implemented("CommandBuffer::record_clear_color_image");
}

void
CommandBuffer::record_bind_compute_pipeline(ComputePipelineHandle /*pipeline*/)
{
    throw_not_implemented("CommandBuffer::record_bind_compute_pipeline");
}

void
CommandBuffer::record_bind_graphics_pipeline(GraphicsPipelineHandle /*pipeline*/)
{
    throw_not_implemented("CommandBuffer::record_bind_graphics_pipeline");
}

void
CommandBuffer::record_bind_descriptor_sets(PipelineBindPoint /*bind_point*/,
                                           PipelineLayoutHandle /*layout*/,
                                           std::uint32_t /*first_set*/,
                                           std::span<const DescriptorSetHandle> /*sets*/,
                                           std::span<const std::uint32_t> /*dynamic_offsets*/)
{
    throw_not_implemented("CommandBuffer::record_bind_descriptor_sets");
}

void
CommandBuffer::record_push_constants(PipelineLayoutHandle /*layout*/,
                                     ShaderStage /*stages*/,
                                     std::uint32_t /*offset*/,
                                     std::span<const std::byte> /*data*/)
{
    throw_not_implemented("CommandBuffer::record_push_constants");
}

void
CommandBuffer::record_dispatch(std::uint32_t /*group_count_x*/,
                               std::uint32_t /*group_count_y*/,
                               std::uint32_t /*group_count_z*/)
{
    throw_not_implemented("CommandBuffer::record_dispatch");
}

void
CommandBuffer::record_dispatch_indirect(BufferHandle /*buffer*/,
                                        std::uint64_t /*offset*/)
{
    throw_not_implemented("CommandBuffer::record_dispatch_indirect");
}

void
CommandBuffer::record_set_event(EventHandle /*event*/,
                                const BarrierBatch& /*barriers*/)
{
    throw_not_implemented("CommandBuffer::record_set_event");
}

void
CommandBuffer::record_wait_events(std::span<const EventHandle> /*events*/,
                                  const BarrierBatch& /*barriers*/)
{
    throw_not_implemented("CommandBuffer::record_wait_events");
}

void
CommandBuffer::record_reset_event(EventHandle /*event*/,
                                  PipelineStage /*stages*/)
{
    throw_not_implemented("CommandBuffer::record_reset_event");
}

void
CommandBuffer::record_begin_rendering(const RenderingDescription& /*rendering*/)
{
    throw_not_implemented("CommandBuffer::record_begin_rendering");
}

auto
CommandBuffer::record_begin_parallel_rendering(const RenderingDescription& /*rendering*/) -> RenderingInheritance
{
    throw_not_implemented("CommandBuffer::record_begin_parallel_rendering");
}

void
CommandBuffer::record_set_viewport(const Viewport& /*viewport*/)
{
    throw_not_implemented("CommandBuffer::record_set_viewport");
}

void
CommandBuffer::record_set_scissor(const Rectangle& /*scissor*/)
{
    throw_not_implemented("CommandBuffer::record_set_scissor");
}

void
CommandBuffer::record_bind_vertex_buffers(std::uint32_t /*first_binding*/,
                                          std::span<const VertexBufferBinding> /*buffers*/)
{
    throw_not_implemented("CommandBuffer::record_bind_vertex_buffers");
}

void
CommandBuffer::record_bind_index_buffer(BufferHandle /*buffer*/,
                                        std::uint64_t /*offset*/,
                                        IndexType /*type*/)
{
    throw_not_implemented("CommandBuffer::record_bind_index_buffer");
}

void
CommandBuffer::record_draw(const DrawArguments& /*arguments*/)
{
    throw_not_implemented("CommandBuffer::record_draw");
}

void
CommandBuffer::record_draw_indexed(const DrawIndexedArguments& /*arguments*/)
{
    throw_not_implemented("CommandBuffer::record_draw_indexed");
}

void
CommandBuffer::record_draw_indirect(const IndirectDrawArguments& /*arguments*/)
{
    throw_not_implemented("CommandBuffer::record_draw_indirect");
}

void
CommandBuffer::record_draw_indexed_indirect(const IndirectDrawArguments& /*arguments*/)
{
    throw_not_implemented("CommandBuffer::record_draw_indexed_indirect");
}

void
CommandBuffer::record_reset_query_pool(QueryPoolHandle /*pool*/,
                                       std::uint32_t /*first*/,
                                       std::uint32_t /*count*/)
{
    throw_not_implemented("CommandBuffer::record_reset_query_pool");
}

void
CommandBuffer::record_write_timestamp(QueryPoolHandle /*pool*/,
                                      PipelineStage /*stage*/,
                                      std::uint32_t /*query*/)
{
    throw_not_implemented("CommandBuffer::record_write_timestamp");
}

void
CommandBuffer::record_begin_query(QueryPoolHandle /*pool*/,
                                  std::uint32_t /*query*/)
{
    throw_not_implemented("CommandBuffer::record_begin_query");
}

void
CommandBuffer::record_end_query(QueryPoolHandle /*pool*/,
                                std::uint32_t /*query*/)
{
    throw_not_implemented("CommandBuffer::record_end_query");
}

void
CommandBuffer::record_begin_debug_label(std::string_view /*name*/,
                                        const std::array<float, 4>& /*color*/)
{
    throw_not_implemented("CommandBuffer::record_begin_debug_label");
}

void
CommandBuffer::record_end_debug_label()
{
    throw_not_implemented("CommandBuffer::record_end_debug_label");
}

void
CommandBuffer::record_execute_commands(std::span<const CommandBufferHandle> /*secondary_command_buffers*/)
{
    throw_not_implemented("CommandBuffer::record_execute_commands");
}

void
CommandBuffer::record_blit_image(ImageHandle /*source*/,
                                 ImageLayout /*source_layout*/,
                                 ImageHandle /*destination*/,
                                 ImageLayout /*destination_layout*/,
                                 std::span<const ImageBlitRegion> /*regions*/,
                                 Filter /*filter*/)
{
    throw_not_implemented("CommandBuffer::record_blit_image");
}

void
CommandBuffer::record_resolve_image(ImageHandle /*source*/,
                                    ImageLayout /*source_layout*/,
                                    ImageHandle /*destination*/,
                                    ImageLayout /*destination_layout*/,
                                    std::span<const ImageResolveRegion> /*regions*/)
{
    throw_not_implemented("CommandBuffer::record_resolve_image");
}

void
CommandBuffer::record_clear_depth_stencil_image(ImageHandle /*image*/,
                                                ImageLayout /*layout*/,
                                                float /*depth*/,
                                                std::uint32_t /*stencil*/,
                                                const ImageSubresourceRange& /*range*/)
{
    throw_not_implemented("CommandBuffer::record_clear_depth_stencil_image");
}

void
CommandBuffer::record_clear_attachments(std::span<const ClearAttachment> /*attachments*/,
                                        std::span<const ClearRectangle> /*rectangles*/)
{
    throw_not_implemented("CommandBuffer::record_clear_attachments");
}

void
CommandBuffer::record_set_stencil_reference(StencilFace /*faces*/,
                                            std::uint32_t /*reference*/)
{
    throw_not_implemented("CommandBuffer::record_set_stencil_reference");
}

void
CommandBuffer::record_set_blend_constants(const std::array<float, 4>& /*constants*/)
{
    throw_not_implemented("CommandBuffer::record_set_blend_constants");
}

void
CommandBuffer::record_set_depth_bias(float /*constant_factor*/,
                                     float /*clamp*/,
                                     float /*slope_factor*/)
{
    throw_not_implemented("CommandBuffer::record_set_depth_bias");
}

void
CommandBuffer::record_set_depth_bounds(float /*minimum*/,
                                       float /*maximum*/)
{
    throw_not_implemented("CommandBuffer::record_set_depth_bounds");
}

void
CommandBuffer::record_set_line_width(float /*width*/)
{
    throw_not_implemented("CommandBuffer::record_set_line_width");
}

void
CommandBuffer::record_set_cull_mode(CullMode /*mode*/)
{
    throw_not_implemented("CommandBuffer::record_set_cull_mode");
}

void
CommandBuffer::record_set_front_face(FrontFace /*front_face*/)
{
    throw_not_implemented("CommandBuffer::record_set_front_face");
}

void
CommandBuffer::record_set_primitive_topology(PrimitiveTopology /*topology*/)
{
    throw_not_implemented("CommandBuffer::record_set_primitive_topology");
}

void
CommandBuffer::record_set_depth_test(bool /*enable*/)
{
    throw_not_implemented("CommandBuffer::record_set_depth_test");
}

void
CommandBuffer::record_set_depth_write(bool /*enable*/)
{
    throw_not_implemented("CommandBuffer::record_set_depth_write");
}

void
CommandBuffer::record_set_depth_compare(CompareOperation /*operation*/)
{
    throw_not_implemented("CommandBuffer::record_set_depth_compare");
}

void
CommandBuffer::record_set_stencil_test(bool /*enable*/)
{
    throw_not_implemented("CommandBuffer::record_set_stencil_test");
}

void
CommandBuffer::record_set_stencil_operation(StencilFace /*faces*/,
                                            StencilOperation /*fail*/,
                                            StencilOperation /*pass*/,
                                            StencilOperation /*depth_fail*/,
                                            CompareOperation /*compare*/)
{
    throw_not_implemented("CommandBuffer::record_set_stencil_operation");
}

void
CommandBuffer::record_set_fragment_shading_rate(ShadingRate /*rate*/,
                                                const std::array<ShadingRateCombiner, 2>& /*combiners*/)
{
    throw_not_implemented("CommandBuffer::record_set_fragment_shading_rate");
}

void
CommandBuffer::record_draw_indirect_count(const IndirectDrawArguments& /*arguments*/,
                                          const IndirectCount& /*count*/)
{
    throw_not_implemented("CommandBuffer::record_draw_indirect_count");
}

void
CommandBuffer::record_draw_indexed_indirect_count(const IndirectDrawArguments& /*arguments*/,
                                                  const IndirectCount& /*count*/)
{
    throw_not_implemented("CommandBuffer::record_draw_indexed_indirect_count");
}

void
CommandBuffer::record_draw_mesh_tasks(const DrawMeshTasksArguments& /*arguments*/)
{
    throw_not_implemented("CommandBuffer::record_draw_mesh_tasks");
}

void
CommandBuffer::record_draw_mesh_tasks_indirect(const IndirectDrawArguments& /*arguments*/)
{
    throw_not_implemented("CommandBuffer::record_draw_mesh_tasks_indirect");
}

void
CommandBuffer::record_draw_mesh_tasks_indirect_count(const IndirectDrawArguments& /*arguments*/,
                                                     const IndirectCount& /*count*/)
{
    throw_not_implemented("CommandBuffer::record_draw_mesh_tasks_indirect_count");
}

void
CommandBuffer::record_bind_ray_tracing_pipeline(RayTracingPipelineHandle /*pipeline*/)
{
    throw_not_implemented("CommandBuffer::record_bind_ray_tracing_pipeline");
}

void
CommandBuffer::record_trace_rays(const ShaderBindingTable& /*table*/,
                                 std::uint32_t /*width*/,
                                 std::uint32_t /*height*/,
                                 std::uint32_t /*depth*/)
{
    throw_not_implemented("CommandBuffer::record_trace_rays");
}

void
CommandBuffer::record_trace_rays_indirect(const ShaderBindingTable& /*table*/,
                                          BufferHandle /*buffer*/,
                                          std::uint64_t /*offset*/)
{
    throw_not_implemented("CommandBuffer::record_trace_rays_indirect");
}

void
CommandBuffer::record_build_acceleration_structures(std::span<const AccelerationStructureBuildDescription> /*builds*/)
{
    throw_not_implemented("CommandBuffer::record_build_acceleration_structures");
}

void
CommandBuffer::record_copy_acceleration_structure(AccelerationStructureHandle /*source*/,
                                                  AccelerationStructureHandle /*destination*/,
                                                  AccelerationStructureCopyMode /*mode*/)
{
    throw_not_implemented("CommandBuffer::record_copy_acceleration_structure");
}

void
CommandBuffer::record_write_acceleration_structure_properties(std::span<const AccelerationStructureHandle> /*structures*/,
                                                              QueryPoolHandle /*pool*/,
                                                              std::uint32_t /*first_query*/)
{
    throw_not_implemented("CommandBuffer::record_write_acceleration_structure_properties");
}

} // namespace gfx
