// Input through the GLFW backend: GLFW has no way to inject events, so the tests post the Win32
// messages the system would send to the window (WM_KEYDOWN, WM_CHAR, WM_MOUSEMOVE, ...). GLFW's own
// window procedure handles them as real input, calls its callbacks, and the backend translates them.
// Only the test's hidden window receives them.
#include <gfx/windows_headers.hpp>

#include <gtest/gtest.h>

import std;
import gfx.platform;
import gfx.tests.backend_support;

namespace
{

using namespace gfx::platform;
using namespace std::chrono_literals;
using gfx::tests::first_of;
using gfx::tests::ReceivedEvent;

/// The lParam of a key message: one repeat, the scan code, the extended-key bit, and for a release the
/// previous-state and transition bits.
auto
key_lparam(unsigned int scancode,
           bool extended,
           bool release) -> LPARAM
{
    unsigned long long value = 1U | (static_cast<unsigned long long>(scancode & 0xFFU) << 16U);
    value |= extended ? (1ULL << 24U) : 0U;
    value |= release ? ((1ULL << 30U) | (1ULL << 31U)) : 0U;
    return static_cast<LPARAM>(value);
}

/// A platform and a hidden window that the test's messages are posted to.
class GlfwEventsTest : public ::testing::Test
{
protected:
    GlfwEventsTest()
    {
        WindowDescription description;
        description.title = "gfx GLFW events";
        description.size = gfx::Extent{.width = 320, .height = 200, .depth = 1};
        description.flags = WindowFlag::HIDDEN;
        m_window = m_platform->create_window(description);
        m_hwnd = reinterpret_cast<HWND>(m_window->native_handle().get_if<gfx::Win32WindowHandle>()->window);
        static_cast<void>(m_platform->poll_events());
    }

    /// Posts @p message to the window, as the system does with input.
    void post(UINT message,
              WPARAM wparam,
              LPARAM lparam) const
    {
        ASSERT_NE(PostMessageW(m_hwnd, message, wparam, lparam), 0);
    }

    /// The events of one pump, kept with their text.
    auto pump() -> std::vector<ReceivedEvent>
    {
        return gfx::tests::pump(*m_platform);
    }

    std::unique_ptr<Platform> m_platform = create_platform(PlatformBackendKind::GLFW);
    std::unique_ptr<Window> m_window;
    HWND m_hwnd = nullptr;
};

TEST_F(GlfwEventsTest, KeysCarryThePhysicalKeyTheSymbolAndTheModifiers)
{
    // W pressed, held (key repeat) and released; then F1 and the right arrow (an extended key).
    post(WM_KEYDOWN, 'W', key_lparam(0x11, false, false));
    post(WM_KEYDOWN, 'W', key_lparam(0x11, false, false));
    post(WM_KEYUP, 'W', key_lparam(0x11, false, true));
    post(WM_KEYDOWN, VK_F1, key_lparam(0x3B, false, false));
    post(WM_KEYDOWN, VK_RIGHT, key_lparam(0x4D, true, false));
    const std::vector<ReceivedEvent> events = pump();

    std::vector<KeyboardEvent> presses;
    for (const ReceivedEvent& event : events)
    {
        if (event.event.type() == EventType::KEY_DOWN)
        {
            EXPECT_EQ(event.event.window(), m_window->handle());
            presses.push_back(event.event.as<KeyboardEvent>());
        }
    }
    ASSERT_EQ(presses.size(), 4U);
    EXPECT_EQ(presses[0].key, Key::W);
    EXPECT_EQ(presses[0].key_code, KeyCode::W);
    EXPECT_EQ(presses[0].native_scancode, 0x11U);
    EXPECT_FALSE(presses[0].repeat);
    EXPECT_TRUE(presses[1].repeat);
    EXPECT_EQ(presses[2].key, Key::F1);
    EXPECT_EQ(presses[2].key_code, KeyCode::F1);
    EXPECT_EQ(presses[3].key, Key::RIGHT);
    EXPECT_EQ(presses[3].key_code, KeyCode::RIGHT);

    // No Shift, Control, Alt or Super is held (the locks follow the real keyboard).
    constexpr KeyModifier SIDES = KeyModifier::SHIFT | KeyModifier::CONTROL | KeyModifier::ALT | KeyModifier::SUPER;
    EXPECT_TRUE(is_empty(presses[0].modifiers & SIDES));
    const ReceivedEvent* const released = first_of(events, EventType::KEY_UP);
    ASSERT_NE(released, nullptr);
    EXPECT_EQ(released->event.as<KeyboardEvent>().key, Key::W);
}

TEST_F(GlfwEventsTest, CharactersAreReportedWhileTextInputIsActive)
{
    // Ignored before start_text_input(); then é and an emoji (a surrogate pair) as UTF-8 text.
    post(WM_CHAR, L'a', 0);
    EXPECT_EQ(first_of(pump(), EventType::TEXT_INPUT), nullptr);
    m_window->start_text_input({});
    post(WM_CHAR, 0xE9, 0);
    post(WM_CHAR, 0xD83D, 0);
    post(WM_CHAR, 0xDE00, 0);
    const std::vector<ReceivedEvent> events = pump();
    std::string typed;
    for (const ReceivedEvent& event : events)
    {
        if (event.event.type() == EventType::TEXT_INPUT)
        {
            typed += event.text;
        }
    }
    EXPECT_EQ(typed, "\xC3\xA9\xF0\x9F\x98\x80");
}

TEST_F(GlfwEventsTest, MouseEventsCarryPositionsDeltasButtonsAndWheelSteps)
{
    // Two moves (the second one's delta is from the first), a left press, the first side button, two
    // half notches of the wheel.
    post(WM_MOUSEMOVE, 0, MAKELPARAM(10, 20));
    post(WM_MOUSEMOVE, 0, MAKELPARAM(15, 18));
    post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(15, 18));
    post(WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON1), MAKELPARAM(15, 18));
    post(WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA / 2), MAKELPARAM(15, 18));
    post(WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA / 2), MAKELPARAM(15, 18));
    const std::vector<ReceivedEvent> events = pump();

    std::vector<MouseMotionEvent> motions;
    std::vector<MouseButtonEvent> buttons;
    std::vector<MouseWheelEvent> wheels;
    for (const ReceivedEvent& event : events)
    {
        if (const auto* motion = event.event.get_if<MouseMotionEvent>())
        {
            motions.push_back(*motion);
        }
        if (const auto* button = event.event.get_if<MouseButtonEvent>(); button != nullptr && event.event.type() == EventType::MOUSE_BUTTON_DOWN)
        {
            buttons.push_back(*button);
        }
        if (const auto* wheel = event.event.get_if<MouseWheelEvent>())
        {
            wheels.push_back(*wheel);
        }
    }
    EXPECT_NE(first_of(events, EventType::WINDOW_MOUSE_ENTERED), nullptr);
    ASSERT_EQ(motions.size(), 2U);
    EXPECT_FLOAT_EQ(motions[0].position.x, 10.0F);
    EXPECT_FLOAT_EQ(motions[1].delta.x, 5.0F);
    EXPECT_FLOAT_EQ(motions[1].delta.y, -2.0F);
    ASSERT_EQ(buttons.size(), 2U);
    EXPECT_EQ(buttons[0].button, MouseButton::LEFT);
    EXPECT_EQ(buttons[1].button, MouseButton::BACK);
    ASSERT_EQ(wheels.size(), 2U);
    EXPECT_FLOAT_EQ(wheels[0].delta.y, 0.5F);
    EXPECT_EQ(wheels[0].steps_y, 0);
    EXPECT_EQ(wheels[1].steps_y, 1);
}

TEST_F(GlfwEventsTest, CloseRequestsLeaveTheWindowOpen)
{
    // WM_CLOSE is a request: the window stays until the application destroys it.
    post(WM_CLOSE, 0, 0);
    const std::vector<ReceivedEvent> events = pump();
    const ReceivedEvent* const closing = first_of(events, EventType::WINDOW_CLOSE_REQUESTED);
    ASSERT_NE(closing, nullptr);
    EXPECT_EQ(closing->event.window(), m_window->handle());
    EXPECT_EQ(closing->event.as<WindowEvent>().size, m_window->size());
    EXPECT_NE(IsWindow(m_hwnd), 0);
}

TEST_F(GlfwEventsTest, WatchersSeeEventsDuringThePump)
{
    // GLFW's callbacks run inside the pump: watchers see each event as GLFW reports it.
    std::vector<EventType> watched;
    const EventWatcher watcher = m_platform->add_event_watcher([&watched](const Event& event, std::string_view)
    {
        watched.push_back(event.type());
    });
    post(WM_KEYDOWN, 'A', key_lparam(0x1E, false, false));
    const std::vector<ReceivedEvent> events = pump();
    ASSERT_EQ(watched.size(), events.size());
    EXPECT_NE(std::ranges::find(watched, EventType::KEY_DOWN), watched.end());
}

TEST_F(GlfwEventsTest, EventsOfDestroyedWindowsAreDropped)
{
    // Messages queued for a window destroyed before the pump never become events.
    post(WM_KEYDOWN, 'B', key_lparam(0x30, false, false));
    m_window.reset();
    EXPECT_EQ(first_of(pump(), EventType::KEY_DOWN), nullptr);
}

} // namespace
