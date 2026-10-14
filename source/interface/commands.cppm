/// @file commands.cppm
/// @brief Arguments of recorded commands: copy regions, draws, vertex buffers and dynamic rendering.
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

/// @brief Arguments of a non-indexed draw.
struct DrawArguments
{
    std::uint32_t vertex_count = 0;   ///< Vertices per instance.
    std::uint32_t instance_count = 1; ///< Instances.
    std::uint32_t first_vertex = 0;   ///< First vertex.
    std::uint32_t first_instance = 0; ///< First instance.
};

/// @brief Arguments of an indexed draw.
struct DrawIndexedArguments
{
    std::uint32_t index_count = 0;    ///< Indices per instance.
    std::uint32_t instance_count = 1; ///< Instances.
    std::uint32_t first_index = 0;    ///< First index.
    std::int32_t vertex_offset = 0;   ///< Added to every index.
    std::uint32_t first_instance = 0; ///< First instance.
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

/// @brief Attachments and area of a dynamic rendering pass (no render pass or framebuffer objects).
struct RenderingDescription
{
    Rectangle area{};                                                   ///< Rendered area.
    std::uint32_t layer_count = 1;                                      ///< Layers rendered.
    std::span<const ColorAttachment> color_attachments{};               ///< Color attachments by location.
    std::optional<DepthStencilAttachment> depth_stencil_attachment{};   ///< Depth/stencil attachment.
    std::string_view debug_name{};                                      ///< Label shown by debugging tools.
};

} // namespace gfx
