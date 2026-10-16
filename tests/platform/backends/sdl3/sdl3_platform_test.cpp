// What is particular to the SDL3 backend (the contract shared with the other backends is tested in
// ../platform_backend_test.cpp): its feature set, deferred window destruction as SDL sees it, and the
// clipboard (opt-in).
#include <gtest/gtest.h>

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

import std;
import gfx.platform;
import gfx.tests.sdl3_support;

namespace
{

using namespace gfx::platform;

/// RGBA pixels of @p width x @p height, all one color.
auto
rgba_pixels(std::uint32_t width,
            std::uint32_t height) -> std::vector<std::byte>
{
    std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t index = 0; index < pixels.size(); index += 4)
    {
        pixels[index] = std::byte{0x20};
        pixels[index + 1] = std::byte{0x80};
        pixels[index + 2] = std::byte{0xF0};
        pixels[index + 3] = std::byte{0xFF};
    }
    return pixels;
}

/// A hidden window description of 640 x 360.
auto
hidden_window() -> WindowDescription
{
    WindowDescription description;
    description.title = "gfx SDL3 test";
    description.size = gfx::Extent{.width = 640, .height = 360, .depth = 1};
    description.flags = WindowFlag::RESIZABLE | WindowFlag::HIDDEN | WindowFlag::HIGH_PIXEL_DENSITY;
    return description;
}

/// A platform per test (SDL allows one at a time).
class Sdl3PlatformTest : public ::testing::Test
{
protected:
    std::unique_ptr<Platform> m_platform = create_platform(PlatformBackendKind::SDL3);
};

TEST_F(Sdl3PlatformTest, FeaturesAreTheDocumentedOnes)
{
    // Everything but gamepad sensors (no gfx interface yet) and mouse pass-through windows.
    for (const auto& entry : gfx::EnumerationTraits<PlatformFeature>::NAMES)
    {
        EXPECT_EQ(m_platform->supports(entry.value), entry.value != PlatformFeature::GAMEPAD_SENSORS) << entry.name;
    }
    for (const auto& entry : gfx::EnumerationTraits<CursorShape>::NAMES)
    {
        EXPECT_TRUE(m_platform->supports(entry.value)) << entry.name;
    }
    EXPECT_EQ(m_platform->supported_window_flags(), ~WindowFlag::MOUSE_PASSTHROUGH);
}

TEST_F(Sdl3PlatformTest, DestroyedWindowsAreReleasedAtTheNextPump)
{
    // The handle dies at once; SDL destroys its window at the next pump.
    std::unique_ptr<Window> window = m_platform->create_window(hidden_window());
    const int with_window = gfx::tests::sdl_window_count();
    window.reset();
    EXPECT_EQ(gfx::tests::sdl_window_count(), with_window);
    static_cast<void>(m_platform->poll_events());
    EXPECT_EQ(gfx::tests::sdl_window_count(), with_window - 1);
}

TEST_F(Sdl3PlatformTest, TheClipboardHoldsTextAndImages)
{
    // The real clipboard: only when asked (GFX_TEST_CLIPBOARD=1), and only when it holds nothing but
    // plain text, which is put back.
    const char* const enabled = std::getenv("GFX_TEST_CLIPBOARD");
    if (enabled == nullptr || std::string_view{enabled} != "1")
    {
        GTEST_SKIP() << "uses the system clipboard: set GFX_TEST_CLIPBOARD=1 to run it";
    }
    std::size_t type_count = 0;
    char** const types = SDL_GetClipboardMimeTypes(&type_count);
    bool only_text = true;
    for (std::size_t index = 0; index < type_count; ++index)
    {
        only_text = only_text && std::string_view{types[index]}.starts_with("text/plain");
    }
    SDL_free(static_cast<void*>(types));
    if (!only_text)
    {
        GTEST_SKIP() << "the clipboard holds more than plain text, which this test would lose";
    }
    const std::string saved = m_platform->clipboard_text();

    // Text, then an image (PNG keeps every pixel).
    m_platform->set_clipboard_text("gfx clipboard \xE2\x9C\x93");
    EXPECT_EQ(m_platform->clipboard_text(), "gfx clipboard \xE2\x9C\x93");
    const std::vector<std::byte> pixels = rgba_pixels(3, 2);
    const gfx::Extent size{.width = 3, .height = 2, .depth = 1};
    m_platform->set_clipboard_image(ImageData::from_rgba(size, pixels));
    const DecodedImage image = m_platform->clipboard_image();
    EXPECT_EQ(image.size, size);
    EXPECT_EQ(image.rgba, pixels);

    // Put the text back.
    if (saved.empty())
    {
        SDL_ClearClipboardData();
    }
    else
    {
        m_platform->set_clipboard_text(saved);
    }
}

} // namespace
