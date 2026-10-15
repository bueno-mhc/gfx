/// @file image.cppm
/// @brief Image: texels in a GPU-optimized arrangement (textures, render targets, storage images).
export module gfx.interface:image;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :common_types;

export namespace gfx
{

/// @brief How to create an Image.
struct ImageDescription
{
    ImageDimension dimension = ImageDimension::TWO_DIMENSIONAL; ///< 1D, 2D or 3D.
    Format format = Format::UNDEFINED;                          ///< Texel format.
    Extent extent{};                                            ///< Size of mip level 0.
    std::uint32_t mip_level_count = 1;                          ///< Mip levels.
    std::uint32_t array_layer_count = 1;                        ///< Array layers (6 per cube).
    SampleCount samples = SampleCount::ONE;                     ///< Samples per texel.
    ImageUsage usage = ImageUsage::NONE;                        ///< Every way the image will be used.
    ImageTiling tiling = ImageTiling::OPTIMAL;                  ///< Texel arrangement.
    ImageCreationOption options = ImageCreationOption::NONE;    ///< Cube compatibility, aliasing, ...
    MemoryLocation location = MemoryLocation::DEVICE_LOCAL;     ///< Where its memory lives (when not placed).
    SharingMode sharing = SharingMode::EXCLUSIVE;               ///< Ownership across queue types.
    std::optional<MemoryPlacement> placement{};                 ///< Place it in an existing DeviceMemory instead.
    std::string_view debug_name{};                              ///< Name shown by debugging tools.
};

/// @brief Texels in a GPU-optimized arrangement. Created in ImageLayout::UNDEFINED; barriers move it
///        between layouts. Destroying the object releases it once the GPU no longer uses it.
class Image : public BackendObject
{
public:
    /// @brief Handle naming this image in commands, views and barriers.
    [[nodiscard]] virtual auto handle() const noexcept -> ImageHandle = 0;

    /// @brief 1D, 2D or 3D.
    [[nodiscard]] virtual auto dimension() const noexcept -> ImageDimension = 0;

    /// @brief Texel format.
    [[nodiscard]] virtual auto format() const noexcept -> Format = 0;

    /// @brief Size of mip level 0.
    [[nodiscard]] virtual auto extent() const noexcept -> Extent = 0;

    /// @brief Mip levels.
    [[nodiscard]] virtual auto mip_level_count() const noexcept -> std::uint32_t = 0;

    /// @brief Array layers.
    [[nodiscard]] virtual auto array_layer_count() const noexcept -> std::uint32_t = 0;

    /// @brief Samples per texel.
    [[nodiscard]] virtual auto samples() const noexcept -> SampleCount = 0;

    /// @brief Ways the image may be used.
    [[nodiscard]] virtual auto usage() const noexcept -> ImageUsage = 0;
};

} // namespace gfx
