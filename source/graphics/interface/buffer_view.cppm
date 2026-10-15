/// @file buffer_view.cppm
/// @brief BufferView: a formatted view of a buffer range, read and written by shaders as a texel buffer.
export module gfx.interface:buffer_view;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;

export namespace gfx
{

/// @brief How to create a BufferView.
struct BufferViewDescription
{
    BufferHandle buffer{};             ///< Viewed buffer (UNIFORM_TEXEL and/or STORAGE_TEXEL usage).
    Format format = Format::UNDEFINED; ///< Format of its texels.
    std::uint64_t offset = 0;          ///< First byte (a multiple of DeviceLimits::minimum_texel_buffer_offset_alignment).
    std::uint64_t size = WHOLE_SIZE;   ///< Bytes viewed.
    std::string_view debug_name{};     ///< Name shown by debugging tools.
};

/// @brief A formatted view of a buffer range (GLSL samplerBuffer / imageBuffer, HLSL Buffer / RWBuffer),
///        written to UNIFORM_TEXEL_BUFFER and STORAGE_TEXEL_BUFFER descriptors.
class BufferView : public BackendObject
{
public:
    /// @brief Handle naming this view in descriptors.
    [[nodiscard]] virtual auto handle() const noexcept -> BufferViewHandle = 0;

    /// @brief Viewed buffer.
    [[nodiscard]] virtual auto buffer() const noexcept -> BufferHandle = 0;

    /// @brief Format of its texels.
    [[nodiscard]] virtual auto format() const noexcept -> Format = 0;
};

} // namespace gfx
