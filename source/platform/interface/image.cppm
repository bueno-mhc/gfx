/// @file image.cppm
/// @brief Images given to the platform (window icons, custom cursors, clipboard images): RGBA8 pixels
///        or an encoded PNG, decoded and validated by the library so every backend accepts both.
export module gfx.platform.interface:image;

import std;
import gfx.core;

export namespace gfx::platform
{

/// @brief An image given to the platform: tightly packed RGBA8 rows (top to bottom), or an encoded PNG.
///        The data is only borrowed for the call that receives it.
struct ImageData
{
    Extent size{};                     ///< Size in pixels of RGBA data (PNG data carries its own).
    std::span<const std::byte> rgba{}; ///< size.width * size.height * 4 bytes.
    std::span<const std::byte> png{};  ///< An encoded PNG.

    /// @brief An image of @p size pixels from RGBA8 data.
    [[nodiscard]] static constexpr auto from_rgba(Extent size,
                                                  std::span<const std::byte> rgba) noexcept -> ImageData
    {
        return ImageData{.size = size, .rgba = rgba, .png = {}};
    }

    /// @brief An image from an encoded PNG.
    [[nodiscard]] static constexpr auto from_png(std::span<const std::byte> png) noexcept -> ImageData
    {
        return ImageData{.size = {}, .rgba = {}, .png = png};
    }
};

/// @brief An image as backends receive it: RGBA8 pixels, rows top to bottom.
struct DecodedImage
{
    Extent size{};                 ///< Size in pixels.
    std::vector<std::byte> rgba{}; ///< size.width * size.height * 4 bytes.
};

/// @brief Validates @p image and returns its RGBA8 pixels (decoding PNG data).
/// @param operation Operation named in errors, e.g. "Window::set_icon".
/// @throws InvalidUsageError when the image has no data or both kinds of data, when RGBA data does not
///         match its size, or when PNG data cannot be decoded (with the decoder's reason).
[[nodiscard]] auto
decode_image(const ImageData& image,
             std::string_view operation) -> DecodedImage;

} // namespace gfx::platform
