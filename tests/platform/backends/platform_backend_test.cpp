// The contract every platform backend honors, run on each backend built and available here (SDL3,
// GLFW): the factory, windows (hidden unless a test needs them shown), monitors, cursors, icons, waits,
// and the documented exception for each feature a backend lacks.
#include <gtest/gtest.h>

import std;
import gfx.platform;
import gfx.tests.backend_support;

namespace
{

using namespace gfx::platform;
using namespace std::chrono_literals;
using gfx::tests::pump_until_type;
using gfx::tests::ReceivedEvent;

/// Issues handles that name nothing, as a stale handle would.
class StaleHandles : public gfx::HandleIssuer
{
public:
    [[nodiscard]] static auto monitor() -> MonitorHandle
    {
        return MonitorHandle::create(handle_key(), 4000, 7);
    }

    [[nodiscard]] static auto gamepad() -> GamepadHandle
    {
        return GamepadHandle::create(handle_key(), 4000, 7);
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
hidden_window(std::string_view title = "gfx backend test") -> WindowDescription
{
    WindowDescription description;
    description.title = title;
    description.size = gfx::Extent{.width = 640, .height = 360, .depth = 1};
    description.flags = WindowFlag::RESIZABLE | WindowFlag::HIDDEN | WindowFlag::HIGH_PIXEL_DENSITY;
    return description;
}

/// Whether @p function throws BackendCapabilityError.
template<class Function>
auto
throws_capability_error(Function function) -> bool
{
    try
    {
        function();
    }
    catch (const gfx::BackendCapabilityError&)
    {
        return true;
    }
    catch (...)
    {
        return false;
    }
    return false;
}

/// A platform of the backend under test, per test (backends allow one platform at a time).
class PlatformBackendTest : public ::testing::TestWithParam<PlatformBackendKind>
{
protected:
    std::unique_ptr<Platform> m_platform = create_platform(GetParam());
};

TEST_P(PlatformBackendTest, TheFactoryCreatesOnePlatformAtATime)
{
    // Built, available, and refused a second time while the first exists (also for another backend).
    const PlatformBackendAvailability availability = platform_backend_availability(GetParam());
    EXPECT_TRUE(availability.built);
    EXPECT_TRUE(availability.available) << availability.reason;
    EXPECT_EQ(m_platform->backend(), GetParam());
    EXPECT_THROW((void)create_platform(GetParam()), gfx::InvalidUsageError);
    for (const PlatformBackendKind other : available_platform_backends())
    {
        if (other == GetParam())
        {
            continue;
        }
        try
        {
            (void)create_platform(other);
            FAIL() << "a " << to_string(other) << " platform was created next to a " << to_string(GetParam()) << " platform";
        }
        catch (const gfx::InvalidUsageError& error)
        {
            EXPECT_NE(std::string_view{error.what()}.find("one backend is active at a time"), std::string_view::npos) << error.what();
        }
    }

    // A new platform may follow a destroyed one.
    m_platform.reset();
    m_platform = create_platform(GetParam(), {.application_name = "gfx tests"});
    EXPECT_EQ(m_platform->application_name(), "gfx tests");
}

TEST_P(PlatformBackendTest, UnsupportedFlagsAndCursorsAreNamed)
{
    // A flag the backend does not honor is named in the error; so is a cursor shape it lacks.
    for (const auto& entry : gfx::EnumerationTraits<WindowFlag>::NAMES)
    {
        if (has_any(m_platform->supported_window_flags(), entry.value))
        {
            continue;
        }
        WindowDescription description = hidden_window();
        description.flags |= entry.value;
        try
        {
            (void)m_platform->create_window(description);
            FAIL() << "a window with " << entry.name << " was created";
        }
        catch (const gfx::BackendCapabilityError& error)
        {
            EXPECT_NE(std::string_view{error.what()}.find(entry.name), std::string_view::npos) << error.what();
        }
    }
    for (const auto& entry : gfx::EnumerationTraits<CursorShape>::NAMES)
    {
        if (m_platform->supports(entry.value))
        {
            EXPECT_EQ(m_platform->create_cursor(entry.value)->shape(), entry.value) << entry.name;
        }
        else
        {
            EXPECT_TRUE(throws_capability_error([&] { (void)m_platform->create_cursor(entry.value); })) << entry.name;
        }
    }
}

TEST_P(PlatformBackendTest, MissingFeaturesThrowTheirDocumentedError)
{
    // Each feature the backend reports missing throws BackendCapabilityError where it is used.
    const std::unique_ptr<Window> window = m_platform->create_window(hidden_window());
    InputDevices& devices = m_platform->input_devices();
    if (!m_platform->supports(PlatformFeature::WINDOW_OCCLUSION))
    {
        EXPECT_TRUE(throws_capability_error([&] { (void)window->is_occluded(); }));
    }
    if (!m_platform->supports(PlatformFeature::CLIPBOARD_IMAGE))
    {
        EXPECT_TRUE(throws_capability_error([&] { (void)m_platform->clipboard_image(); }));
        EXPECT_TRUE(throws_capability_error([&] { m_platform->set_clipboard_image(ImageData{}); }));
    }
    if (!m_platform->supports(PlatformFeature::TEXT_COMPOSITION))
    {
        EXPECT_TRUE(throws_capability_error([&] { window->start_text_input(TextInputDescription{.area = {}, .cursor = 0, .composition_events = true}); }));
    }
    if (!m_platform->supports(PlatformFeature::TOUCH))
    {
        EXPECT_TRUE(throws_capability_error([&] { (void)devices.touch_devices(); }));
    }
    if (!m_platform->supports(PlatformFeature::GAMEPAD_RUMBLE))
    {
        EXPECT_TRUE(throws_capability_error([&] { devices.set_gamepad_rumble(StaleHandles::gamepad(), 1.0F, 1.0F, 10ms); }));
    }
    if (!m_platform->supports(PlatformFeature::GAMEPAD_LED))
    {
        EXPECT_TRUE(throws_capability_error([&] { devices.set_gamepad_led(StaleHandles::gamepad(), LedColor{}); }));
    }
    if (!m_platform->supports(PlatformFeature::WINDOW_ICON))
    {
        const std::vector<std::byte> pixels = rgba_pixels(16, 16);
        const std::array icons{ImageData::from_rgba(gfx::Extent{.width = 16, .height = 16, .depth = 1}, pixels)};
        EXPECT_TRUE(throws_capability_error([&] { window->set_icon(icons); }));
    }

    // A handle of no gamepad is refused where the feature exists.
    if (m_platform->supports(PlatformFeature::GAMEPAD_RUMBLE))
    {
        EXPECT_THROW(devices.set_gamepad_rumble(StaleHandles::gamepad(), 1.0F, 1.0F, 10ms), gfx::InvalidHandleError);
    }
    EXPECT_THROW((void)devices.gamepad_properties(StaleHandles::gamepad()), gfx::InvalidHandleError);
}

TEST_P(PlatformBackendTest, WindowsReportTheirStateAndNativeHandle)
{
    // A hidden window with a UTF-8 title, sized in window coordinates and pixels.
    const std::unique_ptr<Window> window = m_platform->create_window(hidden_window("gfx \xC3\xA9t\xC3\xA9 \xE2\x9C\x93"));
    EXPECT_EQ(window->backend(), GetParam());
    EXPECT_TRUE(window->handle().is_valid());
    EXPECT_EQ(window->title(), "gfx \xC3\xA9t\xC3\xA9 \xE2\x9C\x93");
    EXPECT_EQ(window->size(), (gfx::Extent{.width = 640, .height = 360, .depth = 1}));
    EXPECT_GE(window->framebuffer_size().width, 640U);
    EXPECT_GT(window->content_scale(), 0.0F);
    EXPECT_FALSE(window->is_visible());
    EXPECT_FALSE(window->is_minimized());

    // The native handle is the Win32 window the backend created.
    const gfx::NativeWindowHandle native = window->native_handle();
    ASSERT_EQ(native.kind(), gfx::NativeWindowKind::WINDOWS);
    EXPECT_NE(native.get_if<gfx::Win32WindowHandle>()->window, 0U);
    EXPECT_NE(native.get_if<gfx::Win32WindowHandle>()->instance, 0U);

    // The window is on a known monitor; titles, modes and limits change.
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

TEST_P(PlatformBackendTest, ResizingIsReportedWithTheNewSizes)
{
    // The backend reports the change; the thread-safe sizes follow at once.
    const std::unique_ptr<Window> window = m_platform->create_window(hidden_window());
    static_cast<void>(m_platform->poll_events());
    window->set_size(gfx::Extent{.width = 800, .height = 500, .depth = 1});
    EXPECT_EQ(window->size(), (gfx::Extent{.width = 800, .height = 500, .depth = 1}));
    const std::optional<ReceivedEvent> resized = pump_until_type(*m_platform, EventType::WINDOW_RESIZED);
    ASSERT_TRUE(resized.has_value());
    EXPECT_EQ(resized->event.window(), window->handle());
    EXPECT_EQ(resized->event.as<WindowEvent>().size, (gfx::Extent{.width = 800, .height = 500, .depth = 1}));
    EXPECT_THROW(window->set_size(gfx::Extent{.width = 0, .height = 10, .depth = 1}), gfx::InvalidUsageError);
}

TEST_P(PlatformBackendTest, OnlySizesThatChangeAreReported)
{
    // A window shown at its size reports no resize (SDL reports the unchanged sizes of a window first shown).
    const std::unique_ptr<Window> window = m_platform->create_window(hidden_window());
    window->show();
    std::vector<ReceivedEvent> received;
    ASSERT_TRUE(gfx::tests::pump_until(*m_platform, [](const Event& event) { return event.type() == EventType::WINDOW_SHOWN; }, received));
    (void)gfx::tests::pump_until(*m_platform, [](const Event&) { return false; }, received, 300ms);
    EXPECT_EQ(gfx::tests::first_of(received, EventType::WINDOW_RESIZED), nullptr);
    EXPECT_EQ(gfx::tests::first_of(received, EventType::WINDOW_FRAMEBUFFER_RESIZED), nullptr);
}

TEST_P(PlatformBackendTest, WindowsMoveBetweenStates)
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

TEST_P(PlatformBackendTest, WindowsMayOutliveTheirPlatform)
{
    // The backend shuts down with the platform; the window then refuses to work, and its destruction is harmless.
    std::unique_ptr<Window> window = m_platform->create_window(hidden_window());
    std::unique_ptr<Cursor> cursor = m_platform->create_cursor(CursorShape::POINTER);
    m_platform.reset();
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

TEST_P(PlatformBackendTest, WindowsBelongToThePlatformThread)
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

TEST_P(PlatformBackendTest, MonitorsDescribeThemselves)
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

TEST_P(PlatformBackendTest, CursorsAreStandardOrCustom)
{
    // Standard and custom cursors, shown by a window; a destroyed cursor is refused.
    const std::unique_ptr<Window> window = m_platform->create_window(hidden_window());
    const std::unique_ptr<Cursor> pointer = m_platform->create_cursor(CursorShape::POINTER);
    window->set_cursor(pointer->handle());
    const std::vector<std::byte> pixels = rgba_pixels(16, 16);
    const gfx::Extent size{.width = 16, .height = 16, .depth = 1};
    std::unique_ptr<Cursor> custom =
        m_platform->create_cursor(CursorImageDescription{.image = ImageData::from_rgba(size, pixels), .hot_spot = {.x = 8, .y = 8, .z = 0}});
    EXPECT_FALSE(custom->shape().has_value());
    window->set_cursor(custom->handle());
    window->set_cursor(CursorHandle{});
    EXPECT_THROW((void)m_platform->create_cursor(
                     CursorImageDescription{.image = ImageData::from_rgba(size, pixels), .hot_spot = {.x = 16, .y = 0, .z = 0}}),
                 gfx::InvalidUsageError);
    const CursorHandle destroyed = custom->handle();
    custom.reset();
    EXPECT_THROW(window->set_cursor(destroyed), gfx::InvalidHandleError);
    static_cast<void>(m_platform->poll_events());
}

TEST_P(PlatformBackendTest, IconsCursorModesAndTextInputAreApplied)
{
    // Icons in two resolutions where the backend has icons; empty and undecodable icons are refused.
    const std::unique_ptr<Window> window = m_platform->create_window(hidden_window());
    if (m_platform->supports(PlatformFeature::WINDOW_ICON))
    {
        const std::vector<std::byte> small = rgba_pixels(16, 16);
        const std::vector<std::byte> large = rgba_pixels(32, 32);
        const std::array icons{ImageData::from_rgba(gfx::Extent{.width = 16, .height = 16, .depth = 1}, small),
                               ImageData::from_rgba(gfx::Extent{.width = 32, .height = 32, .depth = 1}, large)};
        window->set_icon(icons);
        EXPECT_THROW(window->set_icon({}), gfx::InvalidUsageError);
        const std::array<std::byte, 4> not_png{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
        const std::array broken{ImageData::from_png(not_png)};
        EXPECT_THROW(window->set_icon(broken), gfx::InvalidUsageError);
    }

    // Cursor modes and text input are remembered.
    for (const CursorMode mode : {CursorMode::HIDDEN, CursorMode::CONFINED, CursorMode::NORMAL})
    {
        window->set_cursor_mode(mode);
        EXPECT_EQ(window->cursor_mode(), mode);
    }
    window->start_text_input(TextInputDescription{.area = {.x = 10, .y = 10, .width = 200, .height = 20}, .cursor = 4, .composition_events = false});
    EXPECT_TRUE(window->is_text_input_active());
    window->stop_text_input();
    EXPECT_FALSE(window->is_text_input_active());
}

TEST_P(PlatformBackendTest, WaitsEndOnTimeoutOnWakeAndOnPushedEvents)
{
    // Nothing happens: the timeout ends the wait.
    static_cast<void>(m_platform->poll_events());
    const auto start = std::chrono::steady_clock::now();
    static_cast<void>(m_platform->wait_events_timeout(30ms));
    EXPECT_GE(std::chrono::steady_clock::now() - start, 25ms);

    // Another thread wakes an endless wait; the wake-up itself is not an event.
    std::jthread waker{[this]
    {
        std::this_thread::sleep_for(50ms);
        m_platform->wake();
    }};
    const EventSpan woken = m_platform->wait_events();
    EXPECT_TRUE(woken.empty());
    waker.join();

    // An event pushed by another thread ends the wait and is delivered.
    std::jthread pusher{[this]
    {
        std::this_thread::sleep_for(50ms);
        m_platform->push_event(Event::make<EventType::CLIPBOARD_UPDATED>(0ns, ApplicationEvent{}));
    }};
    const EventSpan pushed = m_platform->wait_events();
    pusher.join();
    ASSERT_EQ(pushed.size(), 1U);
    EXPECT_EQ(pushed[0].type(), EventType::CLIPBOARD_UPDATED);
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         PlatformBackendTest,
                         ::testing::ValuesIn(available_platform_backends()),
                         [](const ::testing::TestParamInfo<PlatformBackendKind>& parameter)
                         {
                             return std::string{to_string(parameter.param)};
                         });

} // namespace
