#include <gtest/gtest.h>

import std;
import gfx.platform;
import gfx.tests.fake_platform;

namespace
{

using namespace gfx::platform;
using gfx::tests::FakePlatform;
using gfx::tests::FakeWindow;

auto
contains(std::string_view text,
         std::string_view part) -> bool
{
    return text.find(part) != std::string_view::npos;
}

/// The InvalidUsageError message validate_window_description() raises for @p description.
auto
invalid_usage_of(const WindowDescription& description) -> std::string
{
    try
    {
        validate_window_description(description, WindowFlag::RESIZABLE | WindowFlag::HIGH_PIXEL_DENSITY, PlatformBackendKind::SDL3);
    }
    catch (const gfx::InvalidUsageError& error)
    {
        return error.what();
    }
    return "<no error>";
}

TEST(WindowDescriptionTest, InconsistentDescriptionsAreRefused)
{
    EXPECT_TRUE(contains(invalid_usage_of({.size = {.width = 0, .height = 600, .depth = 1}}), "the size 0x600 is empty"));
    const std::optional<gfx::Extent> small = gfx::Extent{.width = 100, .height = 100, .depth = 1};
    const std::optional<gfx::Extent> large = gfx::Extent{.width = 800, .height = 600, .depth = 1};
    EXPECT_TRUE(contains(invalid_usage_of({.minimum_size = large, .maximum_size = small}), "exceeds the maximum size"));
    EXPECT_TRUE(contains(invalid_usage_of({.size = {.width = 50, .height = 50, .depth = 1}, .minimum_size = small}),
                         "is below the minimum size"));
    const std::optional<AspectRatio> zero = AspectRatio{.numerator = 16, .denominator = 0};
    EXPECT_TRUE(contains(invalid_usage_of({.aspect_ratio = zero}), "has a zero term"));
    const FullscreenDescription mode{.monitor = {}, .video_mode = VideoMode{}};
    EXPECT_TRUE(contains(invalid_usage_of({.fullscreen = mode}), "a video mode is given for a WINDOWED window"));
    EXPECT_TRUE(contains(invalid_usage_of({.title = "\xC3("}), "the title is not valid UTF-8"));
    EXPECT_EQ(invalid_usage_of({.title = "Fenêtre"}), "<no error>");
}

TEST(WindowDescriptionTest, FlagsTheBackendCannotHonorAreAllNamed)
{
    try
    {
        validate_window_description({.flags = WindowFlag::RESIZABLE | WindowFlag::ALWAYS_ON_TOP | WindowFlag::UTILITY},
                                    WindowFlag::RESIZABLE, PlatformBackendKind::GLFW);
        FAIL() << "validation must throw";
    }
    catch (const gfx::BackendCapabilityError& error)
    {
        EXPECT_EQ(error.backend(), PlatformBackendKind::GLFW);
        EXPECT_TRUE(contains(error.what(), "[gfx::platform::GLFW] Platform::create_window")) << error.what();
        EXPECT_TRUE(contains(error.what(), "ALWAYS_ON_TOP | UTILITY")) << error.what();
    }
}

TEST(WindowTest, WindowsKeepTheirStateAndIcons)
{
    FakePlatform platform;
    constexpr std::array<std::byte, 16> pixels{};
    const std::array icons{ImageData::from_rgba({.width = 2, .height = 2, .depth = 1}, pixels)};
    std::unique_ptr<Window> window = platform.create_window({.title = "demo", .icon = icons});
    EXPECT_EQ(window->title(), "demo");
    window->set_title("renamed");
    EXPECT_EQ(window->title(), "renamed");
    ASSERT_EQ(static_cast<FakeWindow&>(*window).icon_sizes().size(), 1U);
    EXPECT_EQ(window->native_handle().kind(), gfx::NativeWindowKind::WINDOWS);
    EXPECT_EQ(window->monitor(), platform.primary_monitor());
}

TEST(WindowTest, SizesAreReadableFromOtherThreads)
{
    // The render thread reads the framebuffer size while the platform thread records resizes.
    FakePlatform platform;
    std::unique_ptr<Window> window = platform.create_window({.size = {.width = 640, .height = 480, .depth = 1}});
    auto& fake = static_cast<FakeWindow&>(*window);
    EXPECT_EQ(window->framebuffer_size().width, 1280U);
    EXPECT_FLOAT_EQ(window->pixel_density(), 2.0F);

    std::atomic<bool> stop = false;
    std::jthread reader{[&]
    {
        while (!stop.load())
        {
            // A size and its framebuffer size are each read atomically: never a torn value.
            const gfx::Extent pixels = window->framebuffer_size();
            EXPECT_TRUE((pixels.width == 1280 && pixels.height == 960) || (pixels.width == 2000 && pixels.height == 1000));
        }
    }};
    for (int index = 0; index < 1000; ++index)
    {
        fake.simulate_resize({.width = 1000, .height = 500, .depth = 1}, {.width = 2000, .height = 1000, .depth = 1});
        fake.simulate_resize({.width = 640, .height = 480, .depth = 1}, {.width = 1280, .height = 960, .depth = 1});
    }
    stop = true;
}

TEST(WindowTest, UnsupportedOperationsThrowWithTheBackendsReason)
{
    FakePlatform platform;
    std::unique_ptr<Window> window = platform.create_window({});
    try
    {
        (void)window->is_occluded();
        FAIL() << "is_occluded must throw";
    }
    catch (const gfx::BackendCapabilityError& error)
    {
        EXPECT_EQ(error.capability(), "occlusion state");
        EXPECT_TRUE(contains(error.what(), "the fake backend cannot see other windows")) << error.what();
    }
    EXPECT_THROW((void)platform.create_cursor(CursorShape::WAIT), gfx::BackendCapabilityError);
    EXPECT_THROW((void)platform.clipboard_image(), gfx::BackendCapabilityError);
    EXPECT_THROW(platform.input_devices().set_gamepad_rumble(GamepadHandle{}, 1.0F, 1.0F, std::chrono::milliseconds{100}),
                 gfx::BackendCapabilityError);

    // An operation a backend forwards to the interface reports that it is not implemented.
    try
    {
        platform.input_devices().set_gamepad_led(GamepadHandle{}, LedColor{});
        FAIL() << "set_gamepad_led must throw";
    }
    catch (const gfx::BackendCapabilityError& error)
    {
        EXPECT_TRUE(contains(error.what(), "the SDL3 backend does not implement this operation yet")) << error.what();
    }
}

} // namespace
