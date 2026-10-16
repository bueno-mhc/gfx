// The SDL3 platform backend on this machine: the factory, windows (hidden unless a test needs them
// shown), monitors, cursors and icons.
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
using namespace std::chrono_literals;
using gfx::tests::pump_until_type;

/// Issues handles that name nothing, as a stale handle would.
class StaleHandles : public gfx::HandleIssuer
{
public:
    [[nodiscard]] static auto monitor() -> MonitorHandle
    {
        return MonitorHandle::create(handle_key(), 4000, 7);
    }
};

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
hidden_window(std::string_view title = "gfx SDL3 test") -> WindowDescription
{
    WindowDescription description;
    description.title = title;
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

TEST(Sdl3FactoryTest, TheFactoryBuildsTheSdl3Backend)
{
    // SDL3 is built and preferred; GLFW is not built yet and says how to build it.
    const PlatformBackendAvailability sdl3 = platform_backend_availability(PlatformBackendKind::SDL3);
    EXPECT_TRUE(sdl3.built);
    EXPECT_TRUE(sdl3.available) << sdl3.reason;
    ASSERT_FALSE(available_platform_backends().empty());
    EXPECT_EQ(available_platform_backends().front(), PlatformBackendKind::SDL3);
    const PlatformBackendAvailability glfw = platform_backend_availability(PlatformBackendKind::GLFW);
    EXPECT_FALSE(glfw.built);
    EXPECT_NE(glfw.reason.find("GFX_PLATFORM_BACKEND_GLFW=ON"), std::string::npos) << glfw.reason;

    // One SDL3 platform at a time: SDL's event queue is process-wide.
    const std::unique_ptr<Platform> platform = create_platform(PlatformBackendKind::SDL3, {.application_name = "gfx tests"});
    EXPECT_EQ(platform->backend(), PlatformBackendKind::SDL3);
    EXPECT_EQ(platform->application_name(), "gfx tests");
    try
    {
        (void)create_platform(PlatformBackendKind::SDL3);
        FAIL() << "a second SDL3 platform was created";
    }
    catch (const gfx::InvalidUsageError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("one event queue per process"), std::string_view::npos) << error.what();
    }
}

TEST(Sdl3FactoryTest, ANewPlatformMayFollowADestroyedOne)
{
    for (int round = 0; round < 2; ++round)
    {
        const std::unique_ptr<Platform> platform = create_platform(PlatformBackendKind::SDL3);
        EXPECT_FALSE(platform->monitors().empty());
    }
}

TEST_F(Sdl3PlatformTest, FeaturesAndFlagsAreReported)
{
    // Every standard cursor; no mouse pass-through window (SDL has none), and asking for one names it.
    for (const auto& entry : gfx::EnumerationTraits<CursorShape>::NAMES)
    {
        EXPECT_TRUE(m_platform->supports(entry.value)) << entry.name;
    }
    EXPECT_TRUE(m_platform->supports(PlatformFeature::TEXT_COMPOSITION));
    EXPECT_FALSE(m_platform->supports(PlatformFeature::GAMEPAD_SENSORS));
    EXPECT_FALSE(has_any(m_platform->supported_window_flags(), WindowFlag::MOUSE_PASSTHROUGH));
    WindowDescription description = hidden_window();
    description.flags |= WindowFlag::MOUSE_PASSTHROUGH;
    try
    {
        (void)m_platform->create_window(description);
        FAIL() << "a mouse pass-through window was created";
    }
    catch (const gfx::BackendCapabilityError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("MOUSE_PASSTHROUGH"), std::string_view::npos) << error.what();
    }
}

TEST_F(Sdl3PlatformTest, WindowsReportTheirStateAndNativeHandle)
{
    // A hidden window with a UTF-8 title, sized in window coordinates and pixels.
    const std::unique_ptr<Window> window = m_platform->create_window(hidden_window("gfx \xC3\xA9t\xC3\xA9 \xE2\x9C\x93"));
    EXPECT_EQ(window->backend(), PlatformBackendKind::SDL3);
    EXPECT_TRUE(window->handle().is_valid());
    EXPECT_EQ(window->title(), "gfx \xC3\xA9t\xC3\xA9 \xE2\x9C\x93");
    EXPECT_EQ(window->size(), (gfx::Extent{.width = 640, .height = 360, .depth = 1}));
    EXPECT_GE(window->framebuffer_size().width, 640U);
    EXPECT_GT(window->content_scale(), 0.0F);
    EXPECT_FALSE(window->is_visible());
    EXPECT_FALSE(window->is_minimized());

    // The native handle is the Win32 window SDL created.
    const gfx::NativeWindowHandle native = window->native_handle();
    ASSERT_EQ(native.kind(), gfx::NativeWindowKind::WINDOWS);
    EXPECT_NE(native.get_if<gfx::Win32WindowHandle>()->window, 0U);
    EXPECT_NE(native.get_if<gfx::Win32WindowHandle>()->instance, 0U);
    EXPECT_NE(gfx::tests::sdl_window_identifier(*window), 0U);

    // The window is on a known monitor; titles and modes change.
    const std::vector<MonitorHandle> monitors = m_platform->monitors();
    EXPECT_NE(std::ranges::find(monitors, window->monitor()), monitors.end());
    window->set_title("renamed");
    EXPECT_EQ(window->title(), "renamed");
    EXPECT_EQ(window->mode(), WindowMode::WINDOWED);
    window->set_mode(WindowMode::BORDERLESS, {});
    EXPECT_EQ(window->mode(), WindowMode::BORDERLESS);
    window->set_resizable(false);
    window->set_size_limits(gfx::Extent{.width = 200, .height = 100, .depth = 1}, std::nullopt);
    window->set_aspect_ratio(AspectRatio{.numerator = 16, .denominator = 9});
    window->set_aspect_ratio(std::nullopt);
    EXPECT_THROW(window->set_title("\xC3\x28"), gfx::InvalidUsageError);
}

TEST_F(Sdl3PlatformTest, ResizingIsReportedWithTheNewSizes)
{
    // SDL reports the change; the thread-safe sizes follow at once.
    const std::unique_ptr<Window> window = m_platform->create_window(hidden_window());
    static_cast<void>(m_platform->poll_events());
    window->set_size(gfx::Extent{.width = 800, .height = 500, .depth = 1});
    EXPECT_EQ(window->size(), (gfx::Extent{.width = 800, .height = 500, .depth = 1}));
    const std::optional<gfx::tests::ReceivedEvent> resized = pump_until_type(*m_platform, EventType::WINDOW_RESIZED);
    ASSERT_TRUE(resized.has_value());
    EXPECT_EQ(resized->event.window(), window->handle());
    EXPECT_EQ(resized->event.as<WindowEvent>().size, (gfx::Extent{.width = 800, .height = 500, .depth = 1}));
    EXPECT_THROW(window->set_size(gfx::Extent{.width = 0, .height = 10, .depth = 1}), gfx::InvalidUsageError);
}

TEST_F(Sdl3PlatformTest, WindowsMoveBetweenStates)
{
    // Shown (a window appears briefly), minimized, restored, hidden.
    const std::unique_ptr<Window> window = m_platform->create_window(hidden_window());
    window->set_position(gfx::Offset{.x = 120, .y = 140, .z = 0});
    window->show();
    ASSERT_TRUE(pump_until_type(*m_platform, EventType::WINDOW_SHOWN).has_value());
    EXPECT_TRUE(window->is_visible());
    EXPECT_EQ(window->position(), (gfx::Offset{.x = 120, .y = 140, .z = 0}));
    window->minimize();
    ASSERT_TRUE(pump_until_type(*m_platform, EventType::WINDOW_MINIMIZED).has_value());
    EXPECT_TRUE(window->is_minimized());
    window->restore();
    ASSERT_TRUE(pump_until_type(*m_platform, EventType::WINDOW_RESTORED).has_value());
    EXPECT_FALSE(window->is_minimized());
    window->hide();
    ASSERT_TRUE(pump_until_type(*m_platform, EventType::WINDOW_HIDDEN).has_value());
    EXPECT_FALSE(window->is_visible());
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

TEST(Sdl3WindowLifetimeTest, WindowsMayOutliveTheirPlatform)
{
    // SDL shuts down with the platform; the window then refuses to work, and its destruction is harmless.
    std::unique_ptr<Platform> platform = create_platform(PlatformBackendKind::SDL3);
    std::unique_ptr<Window> window = platform->create_window(hidden_window());
    std::unique_ptr<Cursor> cursor = platform->create_cursor(CursorShape::POINTER);
    platform.reset();
    try
    {
        (void)window->title();
        FAIL() << "a window of a destroyed platform worked";
    }
    catch (const gfx::InvalidUsageError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("platform was destroyed"), std::string_view::npos) << error.what();
    }
    window.reset();
    cursor.reset();
}

TEST_F(Sdl3PlatformTest, WindowsBelongToThePlatformThread)
{
    std::exception_ptr failure;
    std::jthread other{[this, &failure]
    {
        try
        {
            (void)m_platform->create_window(hidden_window());
        }
        catch (...)
        {
            failure = std::current_exception();
        }
    }};
    other.join();
    EXPECT_THROW(std::rethrow_exception(failure), gfx::InvalidUsageError);
}

TEST_F(Sdl3PlatformTest, MonitorsDescribeThemselves)
{
    // The primary monitor is listed, named, sized, scaled, with its video modes.
    const std::vector<MonitorHandle> monitors = m_platform->monitors();
    ASSERT_FALSE(monitors.empty());
    const MonitorHandle primary = m_platform->primary_monitor();
    EXPECT_NE(std::ranges::find(monitors, primary), monitors.end());
    const MonitorProperties properties = m_platform->monitor_properties(primary);
    EXPECT_EQ(properties.handle, primary);
    EXPECT_TRUE(properties.primary);
    EXPECT_FALSE(properties.name.empty());
    EXPECT_GT(properties.bounds.width, 0U);
    EXPECT_GT(properties.work_area.height, 0U);
    EXPECT_LE(properties.work_area.height, properties.bounds.height);
    EXPECT_GT(properties.content_scale, 0.0F);
    EXPECT_GT(properties.current_mode.size.width, 0U);
    EXPECT_FALSE(m_platform->video_modes(primary).empty());

    // A handle of no monitor is refused.
    EXPECT_THROW((void)m_platform->monitor_properties(StaleHandles::monitor()), gfx::InvalidHandleError);
}

TEST_F(Sdl3PlatformTest, CursorsAreStandardOrCustom)
{
    // Every standard shape, a custom cursor from RGBA, and windows showing them.
    const std::unique_ptr<Window> window = m_platform->create_window(hidden_window());
    for (const auto& entry : gfx::EnumerationTraits<CursorShape>::NAMES)
    {
        const std::unique_ptr<Cursor> cursor = m_platform->create_cursor(entry.value);
        EXPECT_EQ(cursor->shape(), entry.value);
        window->set_cursor(cursor->handle());
    }
    const std::vector<std::byte> pixels = rgba_pixels(16, 16);
    const gfx::Extent size{.width = 16, .height = 16, .depth = 1};
    std::unique_ptr<Cursor> custom =
        m_platform->create_cursor(CursorImageDescription{.image = ImageData::from_rgba(size, pixels), .hot_spot = {.x = 8, .y = 8, .z = 0}});
    EXPECT_FALSE(custom->shape().has_value());
    window->set_cursor(custom->handle());
    window->set_cursor(CursorHandle{});

    // A hot spot outside the image, and a destroyed cursor, are refused.
    EXPECT_THROW((void)m_platform->create_cursor(
                     CursorImageDescription{.image = ImageData::from_rgba(size, pixels), .hot_spot = {.x = 16, .y = 0, .z = 0}}),
                 gfx::InvalidUsageError);
    const CursorHandle destroyed = custom->handle();
    custom.reset();
    EXPECT_THROW(window->set_cursor(destroyed), gfx::InvalidHandleError);
    static_cast<void>(m_platform->poll_events());
}

TEST_F(Sdl3PlatformTest, IconsAndCursorModesAreApplied)
{
    // Icons in two resolutions; empty and undecodable icons are refused.
    const std::unique_ptr<Window> window = m_platform->create_window(hidden_window());
    const std::vector<std::byte> small = rgba_pixels(16, 16);
    const std::vector<std::byte> large = rgba_pixels(32, 32);
    const std::array icons{ImageData::from_rgba(gfx::Extent{.width = 16, .height = 16, .depth = 1}, small),
                           ImageData::from_rgba(gfx::Extent{.width = 32, .height = 32, .depth = 1}, large)};
    window->set_icon(icons);
    EXPECT_THROW(window->set_icon({}), gfx::InvalidUsageError);
    const std::array<std::byte, 4> not_png{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const std::array broken{ImageData::from_png(not_png)};
    EXPECT_THROW(window->set_icon(broken), gfx::InvalidUsageError);

    // Cursor modes and text input are remembered.
    for (const CursorMode mode : {CursorMode::HIDDEN, CursorMode::CONFINED, CursorMode::NORMAL})
    {
        window->set_cursor_mode(mode);
        EXPECT_EQ(window->cursor_mode(), mode);
    }
    window->start_text_input(TextInputDescription{.area = {.x = 10, .y = 10, .width = 200, .height = 20}, .cursor = 4, .composition_events = true});
    EXPECT_TRUE(window->is_text_input_active());
    window->stop_text_input();
    EXPECT_FALSE(window->is_text_input_active());
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
