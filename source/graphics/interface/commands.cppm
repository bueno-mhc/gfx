/// @file commands.cppm
/// @brief Arguments of recorded commands: copy regions, draws, vertex buffers, dynamic rendering and
///        parallel rendering passes.
export module gfx.interface:commands;

import std;
import gfx.enums;
import gfx.handles;
import :common_types;

export namespace gfx
{

/// @brief Bytes copied between two buffers.
struct BufferCopyRegion
{
    std::uint64_t source_offset = 0;      ///< First byte read.
    std::uint64_t destination_offset = 0; ///< First byte written.
    std::uint64_t size = 0;               ///< Bytes copied.
};

/// @brief Texels copied between a buffer and an image.
struct BufferImageCopyRegion
{
    std::uint64_t buffer_offset = 0;           ///< First byte in the buffer.
    std::uint32_t buffer_row_length = 0;       ///< Texels per buffer row (0 = tightly packed).
    std::uint32_t buffer_image_height = 0;     ///< Rows per buffer image (0 = tightly packed).
    ImageSubresourceLayers image_subresource{};///< Mip level and layers of the image.
    Offset image_offset{};                     ///< First texel in the image.
    Extent image_extent{};                     ///< Texels copied.
};

/// @brief Texels copied between two images with scaling and format conversion (blit).
struct ImageBlitRegion
{
    ImageSubresourceLayers source_subresource{};      ///< Source mip level and layers.
    std::array<Offset, 2> source_bounds{};            ///< Opposite corners of the source box.
    ImageSubresourceLayers destination_subresource{}; ///< Destination mip level and layers.
    std::array<Offset, 2> destination_bounds{};       ///< Opposite corners of the destination box.
};

/// @brief Samples of a multisampled image resolved into a single-sampled image.
struct ImageResolveRegion
{
    ImageSubresourceLayers source_subresource{};      ///< Source mip level and layers.
    Offset source_offset{};                           ///< First source texel.
    ImageSubresourceLayers destination_subresource{}; ///< Destination mip level and layers.
    Offset destination_offset{};                      ///< First destination texel.
    Extent extent{};                                  ///< Texels resolved.
};

/// @brief An attachment cleared inside a rendering pass (RenderingCommands::clear_attachments()).
struct ClearAttachment
{
    ImageAspect aspects = ImageAspect::COLOR;       ///< COLOR, or DEPTH and/or STENCIL.
    std::uint32_t color_attachment = 0;             ///< Color attachment location (COLOR only).
    std::array<float, 4> color{0.0F, 0.0F, 0.0F, 1.0F}; ///< Clear color.
    float depth = 1.0F;                             ///< Clear depth.
    std::uint32_t stencil = 0;                      ///< Clear stencil.
};

/// @brief Area and layers of the attachments cleared by clear_attachments().
struct ClearRectangle
{
    Rectangle area{};              ///< Area.
    std::uint32_t base_layer = 0;  ///< First layer.
    std::uint32_t layer_count = 1; ///< Layers.
};

/// @brief Texels copied between two images.
struct ImageCopyRegion
{
    ImageSubresourceLayers source_subresource{};      ///< Source mip level and layers.
    Offset source_offset{};                           ///< First source texel.
    ImageSubresourceLayers destination_subresource{}; ///< Destination mip level and layers.
    Offset destination_offset{};                      ///< First destination texel.
    Extent extent{};                                  ///< Texels copied.
};

/// @brief A vertex buffer bound to a binding.
struct VertexBufferBinding
{
    BufferHandle buffer{};    ///< Buffer.
    std::uint64_t offset = 0; ///< First byte.
};

/// @brief Arguments of a non-indexed draw; also the record layout of indirect draws (written by GPU
///        culling passes, AZDO style).
struct DrawArguments
{
    std::uint32_t vertex_count = 0;   ///< Vertices per instance.
    std::uint32_t instance_count = 1; ///< Instances.
    std::uint32_t first_vertex = 0;   ///< First vertex.
    std::uint32_t first_instance = 0; ///< First instance.
};

/// @brief Arguments of an indexed draw; also the record layout of indexed indirect draws.
struct DrawIndexedArguments
{
    std::uint32_t index_count = 0;    ///< Indices per instance.
    std::uint32_t instance_count = 1; ///< Instances.
    std::uint32_t first_index = 0;    ///< First index.
    std::int32_t vertex_offset = 0;   ///< Added to every index.
    std::uint32_t first_instance = 0; ///< First instance.
};

/// @brief Workgroups of a dispatch; also the record layout of indirect dispatches.
struct DispatchArguments
{
    std::uint32_t group_count_x = 1; ///< Workgroups along x.
    std::uint32_t group_count_y = 1; ///< Workgroups along y.
    std::uint32_t group_count_z = 1; ///< Workgroups along z.
};

/// @brief Task (or mesh) workgroups of a mesh draw; also the record layout of indirect mesh draws.
struct DrawMeshTasksArguments
{
    std::uint32_t group_count_x = 1; ///< Workgroups along x.
    std::uint32_t group_count_y = 1; ///< Workgroups along y.
    std::uint32_t group_count_z = 1; ///< Workgroups along z.
};

/// @brief The number of indirect draws, read from a buffer (GPU-driven rendering: a culling pass writes
///        both the records and their count).
struct IndirectCount
{
    BufferHandle buffer{};                ///< Buffer holding the count (INDIRECT usage).
    std::uint64_t offset = 0;             ///< Byte of the 32-bit count.
    std::uint32_t maximum_draw_count = 0; ///< Upper bound of the count.
};

/// @brief Draws whose arguments are read from a buffer.
struct IndirectDrawArguments
{
    BufferHandle buffer{};        ///< Buffer holding the argument records.
    std::uint64_t offset = 0;     ///< First record.
    std::uint32_t draw_count = 1; ///< Number of records.
    std::uint32_t stride = 0;     ///< Bytes between records (0 = tightly packed).
};

/// @brief A color attachment of a dynamic rendering pass.
struct ColorAttachment
{
    ImageViewHandle view{};                            ///< Rendered view.
    LoadOperation load = LoadOperation::CLEAR;         ///< Contents at the start.
    StoreOperation store = StoreOperation::STORE;      ///< Contents at the end.
    std::array<float, 4> clear_color{0.0F, 0.0F, 0.0F, 1.0F}; ///< Clear value (RGBA).
    ImageViewHandle resolve_view{};                    ///< Single-sampled view receiving the resolve.
    ResolveMode resolve_mode = ResolveMode::NONE;      ///< How samples are resolved.
};

/// @brief The depth/stencil attachment of a dynamic rendering pass.
struct DepthStencilAttachment
{
    ImageViewHandle view{};                                ///< Rendered view.
    LoadOperation depth_load = LoadOperation::CLEAR;       ///< Depth contents at the start.
    StoreOperation depth_store = StoreOperation::STORE;    ///< Depth contents at the end.
    LoadOperation stencil_load = LoadOperation::DONT_CARE; ///< Stencil contents at the start.
    StoreOperation stencil_store = StoreOperation::DONT_CARE; ///< Stencil contents at the end.
    float clear_depth = 1.0F;                              ///< Depth clear value.
    std::uint32_t clear_stencil = 0;                       ///< Stencil clear value.
    bool read_only = false;                                ///< Tested but never written.
};

/// @brief An image whose texels give the shading rate of screen regions.
struct ShadingRateAttachment
{
    ImageViewHandle view{}; ///< View of a SHADING_RATE_ATTACHMENT image (R8_UINT).
    Extent texel_size{};    ///< Pixels covered by one of its texels.
};

/// @brief Attachments and area of a dynamic rendering pass (no render pass or framebuffer objects).
struct RenderingDescription
{
    Rectangle area{};                                                   ///< Rendered area.
    std::uint32_t layer_count = 1;                                      ///< Layers rendered.
    std::span<const ColorAttachment> color_attachments{};               ///< Color attachments by location.
    std::optional<DepthStencilAttachment> depth_stencil_attachment{};   ///< Depth/stencil attachment.
    std::uint32_t view_mask = 0;                                        ///< Views rendered (DeviceFeature::MULTIVIEW; 0 = one).
    std::optional<ShadingRateAttachment> shading_rate_attachment{};     ///< Per-region shading rates (VARIABLE_RATE_SHADING).
    std::string_view debug_name{};                                      ///< Label shown by debugging tools.
};

/// @brief What a secondary command buffer recording part of a parallel rendering pass needs to know
///        about the pass. Obtained from ParallelRenderingScope::inheritance() and copied to the threads
///        that record the parts (RenderingPartRecorder).
struct RenderingInheritance
{
    CommandBufferHandle primary{};                                  ///< Command buffer that began the pass.
    std::uint32_t pass = 0;                                         ///< Which parallel pass of that command buffer
                                                                    ///< (counted from 0 since recording began).
    std::array<Format, MAXIMUM_COLOR_ATTACHMENTS> color_formats{};  ///< Formats of the color attachments
                                                                    ///< (first color_count used).
    std::uint32_t color_count = 0;                                  ///< Number of color attachments.
    Format depth_format = Format::UNDEFINED;                        ///< Depth attachment format (UNDEFINED = none).
    Format stencil_format = Format::UNDEFINED;                      ///< Stencil attachment format (UNDEFINED = none).
    SampleCount samples = SampleCount::ONE;                         ///< Samples per pixel.
    std::uint32_t view_mask = 0;                                    ///< Views rendered (multiview).

    /// @brief Formats of the color attachments, by location.
    [[nodiscard]] constexpr auto color_attachment_formats() const noexcept -> std::span<const Format>
    {
        return {color_formats.data(), color_count};
    }

    constexpr bool operator==(const RenderingInheritance&) const = default;
};

/// @brief What was recorded into a command buffer (CommandBuffer::statistics()): the numbers AZDO
///        tuning watches. Counted by the recorders; reset when recording begins.
struct CommandStatistics
{
    std::uint32_t draws = 0;                 ///< Direct draws (including mesh task draws).
    std::uint32_t indirect_draws = 0;        ///< Indirect draw commands (each may draw many times).
    std::uint32_t dispatches = 0;            ///< Direct and indirect dispatches.
    std::uint32_t ray_traces = 0;            ///< trace_rays commands.
    std::uint32_t pipeline_binds = 0;        ///< Pipelines bound.
    std::uint32_t descriptor_set_binds = 0;  ///< bind_descriptor_sets commands (bindless needs none).
    std::uint32_t push_constant_writes = 0;  ///< push_constants commands.
    std::uint32_t vertex_buffer_binds = 0;   ///< bind_vertex_buffers and bind_index_buffer commands.
    std::uint32_t barriers = 0;              ///< barrier commands.
    std::uint32_t transfers = 0;             ///< Copies, fills, updates, blits, resolves and clears.
    std::uint32_t rendering_passes = 0;      ///< Rendering passes begun (parallel ones included).

    /// @brief Adds the counts of @p other (the command buffers of a frame, for instance).
    constexpr auto operator+=(const CommandStatistics& other) noexcept -> CommandStatistics&
    {
        draws += other.draws;
        indirect_draws += other.indirect_draws;
        dispatches += other.dispatches;
        ray_traces += other.ray_traces;
        pipeline_binds += other.pipeline_binds;
        descriptor_set_binds += other.descriptor_set_binds;
        push_constant_writes += other.push_constant_writes;
        vertex_buffer_binds += other.vertex_buffer_binds;
        barriers += other.barriers;
        transfers += other.transfers;
        rendering_passes += other.rendering_passes;
        return *this;
    }

    constexpr bool operator==(const CommandStatistics&) const = default;
};

} // namespace gfx

// Argument structs double as the records of indirect commands: their layout must match the native ones.
static_assert(sizeof(gfx::DrawArguments) == 16 && std::is_standard_layout_v<gfx::DrawArguments>);
static_assert(sizeof(gfx::DrawIndexedArguments) == 20 && std::is_standard_layout_v<gfx::DrawIndexedArguments>);
static_assert(sizeof(gfx::DispatchArguments) == 12 && std::is_standard_layout_v<gfx::DispatchArguments>);
static_assert(sizeof(gfx::DrawMeshTasksArguments) == 12 && std::is_standard_layout_v<gfx::DrawMeshTasksArguments>);
