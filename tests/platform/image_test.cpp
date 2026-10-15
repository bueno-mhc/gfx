#include <gtest/gtest.h>

import std;
import gfx.platform;

namespace
{

using namespace gfx::platform;

/// A 2x2 RGBA PNG: red, green (top row), blue, white (bottom row).
constexpr std::array<std::uint8_t, 76> PNG_2X2{
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D,
    0x24, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0,
    0x1F, 0x0C, 0x19, 0x18, 0xFE, 0x83, 0x01, 0x00, 0x49, 0xC8, 0x09, 0xF7, 0x14, 0x63, 0x32, 0x9D,
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

auto
contains(std::string_view text,
         std::string_view part) -> bool
{
    return text.find(part) != std::string_view::npos;
}

TEST(ImageTest, PngImagesAreDecodedToRgba)
{
    const DecodedImage image = decode_image(ImageData::from_png(std::as_bytes(std::span{PNG_2X2})), "test");
    EXPECT_EQ(image.size.width, 2U);
    EXPECT_EQ(image.size.height, 2U);
    ASSERT_EQ(image.rgba.size(), 16U);
    const auto pixel = [&](std::size_t index)
    {
        return std::array{std::to_integer<int>(image.rgba[index * 4]), std::to_integer<int>(image.rgba[index * 4 + 1]),
                          std::to_integer<int>(image.rgba[index * 4 + 2]), std::to_integer<int>(image.rgba[index * 4 + 3])};
    };
    EXPECT_EQ(pixel(0), (std::array{255, 0, 0, 255}));
    EXPECT_EQ(pixel(1), (std::array{0, 255, 0, 255}));
    EXPECT_EQ(pixel(2), (std::array{0, 0, 255, 255}));
    EXPECT_EQ(pixel(3), (std::array{255, 255, 255, 255}));
}

TEST(ImageTest, RgbaImagesMustMatchTheirSize)
{
    const std::vector<std::byte> pixels(2 * 2 * 4);
    const ImageData exact = ImageData::from_rgba({.width = 2, .height = 2, .depth = 1}, pixels);
    EXPECT_EQ(decode_image(exact, "test").rgba.size(), 16U);

    const ImageData wrong = ImageData::from_rgba({.width = 3, .height = 2, .depth = 1}, pixels);
    try
    {
        (void)decode_image(wrong, "Window::set_icon");
        FAIL() << "decode_image must throw";
    }
    catch (const gfx::InvalidUsageError& error)
    {
        EXPECT_TRUE(contains(error.what(), "3x2 RGBA pixels take 24 bytes, but 16 were given")) << error.what();
        EXPECT_EQ(error.operation(), "Window::set_icon");
    }
}

TEST(ImageTest, InvalidPngDataSaysWhy)
{
    const std::array<std::byte, 8> garbage{};
    try
    {
        (void)decode_image(ImageData::from_png(garbage), "test");
        FAIL() << "decode_image must throw";
    }
    catch (const gfx::InvalidUsageError& error)
    {
        EXPECT_TRUE(contains(error.what(), "cannot be decoded")) << error.what();
    }
    EXPECT_THROW((void)decode_image(ImageData{}, "test"), gfx::InvalidUsageError) << "an image needs data";
}

} // namespace
