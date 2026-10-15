/// @file image.cpp
/// @brief decode_image(): validation of RGBA images and PNG decoding with stb_image (declarations only
///        here; the implementation is compiled in stb_image_implementation.cpp).
module;

// Memory decoding only: no <stdio.h> (it conflicts with `import std` in a global module fragment).
#define STBI_NO_STDIO
#include <stb_image.h>

module gfx.platform.interface;

import std;
import gfx.core;

namespace gfx::platform
{

namespace
{

/// The InvalidUsageError of a bad image.
[[noreturn]] void
reject(std::string_view operation,
       std::string problem,
       std::string suggestion)
{
    throw InvalidUsageError({
        .backend = std::nullopt,
        .operation = std::string{operation},
        .problem = std::move(problem),
        .suggestion = std::move(suggestion),
    });
}

} // namespace

auto
decode_image(const ImageData& image,
             std::string_view operation) -> DecodedImage
{
    // Exactly one kind of data.
    if (image.rgba.empty() == image.png.empty())
    {
        reject(operation, image.rgba.empty() ? "the image has no data" : "the image has both RGBA and PNG data",
               "use ImageData::from_rgba() or ImageData::from_png()");
    }

    // RGBA data: its size must match the size given.
    if (!image.rgba.empty())
    {
        const std::uint64_t expected = std::uint64_t{image.size.width} * image.size.height * 4U;
        if (image.size.width == 0 || image.size.height == 0 || image.rgba.size() != expected)
        {
            reject(operation,
                   std::format("{}x{} RGBA pixels take {} bytes, but {} were given", image.size.width, image.size.height,
                               expected, image.rgba.size()),
                   "pass tightly packed RGBA8 rows of exactly the image's size");
        }
        return DecodedImage{.size = image.size, .rgba = {image.rgba.begin(), image.rgba.end()}};
    }

    // PNG data: decoded to RGBA8.
    if (image.png.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        reject(operation, std::format("the PNG data is too large ({} bytes)", image.png.size()), "use a smaller image");
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(image.png.data()), static_cast<int>(image.png.size()),
                                            &width, &height, &channels, 4);
    if (pixels == nullptr)
    {
        reject(operation, std::format("the PNG data cannot be decoded: {}", stbi_failure_reason()),
               "pass a valid PNG file, or RGBA pixels with ImageData::from_rgba()");
    }

    // Copy the pixels out of stb's allocation.
    const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    const auto* first = reinterpret_cast<const std::byte*>(pixels);
    DecodedImage decoded{.size = {.width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height), .depth = 1},
                         .rgba = {first, first + bytes}};
    stbi_image_free(pixels);
    return decoded;
}

} // namespace gfx::platform
