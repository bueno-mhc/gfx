// SDL events translated by the SDL3 backend: injected with SDL_PushEvent (SDL's own event injection),
// as the operating system's input would arrive through SDL.
#include <gtest/gtest.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

import std;
import gfx.platform;
import gfx.tests.sdl3_support;

namespace
{

using namespace gfx::platform;
using namespace std::chrono_literals;
using gfx::tests::pump_until_type;
using gfx::tests::ReceivedEvent;

/// A platform and a hidden window that injected events are addressed to.
class Sdl3EventsTest : public ::testing::Test
{
protected:
    Sdl3EventsTest()
    {
        WindowDescription description;
        description.title = "gfx SDL3 events";
        description.size = gfx::Extent{.width = 320, .height = 200, .depth = 1};
        description.flags = WindowFlag::HIDDEN;
        m_window = m_platform->create_window(description);
        m_identifier = gfx::tests::sdl_window_identifier(*m_window);
        static_cast<void>(m_platform->poll_events());
    }

    /// Pushes @p event into SDL's queue, as SDL does with the system's input.
    static void push(SDL_Event event)
    {
        ASSERT_TRUE(SDL_PushEvent(&event)) << SDL_GetError();
    }

    /// The events of one pump, kept with their text.
    auto pump() -> std::vector<ReceivedEvent>
    {
        std::vector<ReceivedEvent> received;
        const EventSpan events = m_platform->poll_events();
        for (const Event& event : events)
        {
            received.push_back(ReceivedEvent{.event = event, .text = std::string{events.text(event)}});
        }
        return received;
    }

    /// The first event of type @p type in @p events.
    static auto first_of(const std::vector<ReceivedEvent>& events,
                         EventType type) -> const ReceivedEvent*
    {
        const auto found = std::ranges::find_if(events, [type](const ReceivedEvent& event) { return event.event.type() == type; });
        return found != events.end() ? &*found : nullptr;
    }

    std::unique_ptr<Platform> m_platform = create_platform(PlatformBackendKind::SDL3);
    std::unique_ptr<Window> m_window;
    SDL_WindowID m_identifier = 0;
};

TEST_F(Sdl3EventsTest, KeysCarryThePhysicalKeyTheSymbolAndTheModifiers)
{
    // W with left Shift and Caps Lock, then F1 released.
    SDL_Event down{};
    down.key.type = SDL_EVENT_KEY_DOWN;
    down.key.windowID = m_identifier;
    down.key.scancode = SDL_SCANCODE_W;
    down.key.key = SDLK_W;
    down.key.mod = SDL_KMOD_LSHIFT | SDL_KMOD_CAPS;
    down.key.raw = 17;
    down.key.down = true;
    push(down);
    SDL_Event up{};
    up.key.type = SDL_EVENT_KEY_UP;
    up.key.windowID = m_identifier;
    up.key.scancode = SDL_SCANCODE_F1;
    up.key.key = SDLK_F1;
    push(up);
    const std::vector<ReceivedEvent> events = pump();

    const ReceivedEvent* const pressed = first_of(events, EventType::KEY_DOWN);
    ASSERT_NE(pressed, nullptr);
    EXPECT_EQ(pressed->event.window(), m_window->handle());
    const KeyboardEvent& key = pressed->event.as<KeyboardEvent>();
    EXPECT_EQ(key.key, Key::W);
    EXPECT_EQ(key.key_code, KeyCode::W);
    EXPECT_EQ(key.modifiers, KeyModifier::LEFT_SHIFT | KeyModifier::CAPS_LOCK);
    EXPECT_EQ(key.native_scancode, 17U);
    EXPECT_FALSE(key.repeat);

    // Timestamps are on the steady clock.
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
    EXPECT_LT(std::chrono::abs(now - pressed->event.timestamp()), 10s);

    const ReceivedEvent* const released = first_of(events, EventType::KEY_UP);
    ASSERT_NE(released, nullptr);
    EXPECT_EQ(released->event.as<KeyboardEvent>().key, Key::F1);
    EXPECT_EQ(released->event.as<KeyboardEvent>().key_code, KeyCode::F1);
}

TEST_F(Sdl3EventsTest, TextAndCompositionsCarryTheirText)
{
    SDL_Event text{};
    text.text.type = SDL_EVENT_TEXT_INPUT;
    text.text.windowID = m_identifier;
    text.text.text = "h\xC3\xA9llo \xE2\x82\xAC";
    push(text);
    SDL_Event editing{};
    editing.edit.type = SDL_EVENT_TEXT_EDITING;
    editing.edit.windowID = m_identifier;
    editing.edit.text = "\xE3\x81\x8B";
    editing.edit.start = 1;
    editing.edit.length = 0;
    push(editing);
    const std::vector<ReceivedEvent> events = pump();

    const ReceivedEvent* const typed = first_of(events, EventType::TEXT_INPUT);
    ASSERT_NE(typed, nullptr);
    EXPECT_EQ(typed->text, "h\xC3\xA9llo \xE2\x82\xAC");
    const ReceivedEvent* const composed = first_of(events, EventType::TEXT_COMPOSITION);
    ASSERT_NE(composed, nullptr);
    EXPECT_EQ(composed->text, "\xE3\x81\x8B");
    EXPECT_EQ(composed->event.as<TextCompositionEvent>().cursor, 1);
}

TEST_F(Sdl3EventsTest, MouseEventsCarryPositionsButtonsAndWheelSteps)
{
    SDL_Event motion{};
    motion.motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = m_identifier;
    motion.motion.x = 10.5F;
    motion.motion.y = 20.0F;
    motion.motion.xrel = 1.0F;
    motion.motion.yrel = -2.0F;
    motion.motion.state = SDL_BUTTON_LMASK | SDL_BUTTON_RMASK;
    push(motion);
    SDL_Event button{};
    button.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.windowID = m_identifier;
    button.button.button = SDL_BUTTON_X1;
    button.button.down = true;
    button.button.clicks = 2;
    button.button.x = 3.0F;
    button.button.y = 4.0F;
    push(button);
    SDL_Event wheel{};
    wheel.wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.windowID = m_identifier;
    wheel.wheel.y = 1.5F;
    wheel.wheel.integer_y = 1;
    wheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    push(wheel);
    const std::vector<ReceivedEvent> events = pump();

    const ReceivedEvent* const moved = first_of(events, EventType::MOUSE_MOTION);
    ASSERT_NE(moved, nullptr);
    EXPECT_FLOAT_EQ(moved->event.as<MouseMotionEvent>().position.x, 10.5F);
    EXPECT_FLOAT_EQ(moved->event.as<MouseMotionEvent>().delta.y, -2.0F);
    EXPECT_EQ(moved->event.as<MouseMotionEvent>().buttons, MouseButtonMask::LEFT | MouseButtonMask::RIGHT);
    const ReceivedEvent* const clicked = first_of(events, EventType::MOUSE_BUTTON_DOWN);
    ASSERT_NE(clicked, nullptr);
    EXPECT_EQ(clicked->event.as<MouseButtonEvent>().button, MouseButton::BACK);
    EXPECT_EQ(clicked->event.as<MouseButtonEvent>().click_count, 2);
    const ReceivedEvent* const scrolled = first_of(events, EventType::MOUSE_WHEEL);
    ASSERT_NE(scrolled, nullptr);
    EXPECT_FLOAT_EQ(scrolled->event.as<MouseWheelEvent>().delta.y, 1.5F);
    EXPECT_EQ(scrolled->event.as<MouseWheelEvent>().steps_y, 1);
    EXPECT_TRUE(scrolled->event.as<MouseWheelEvent>().flipped);
}

TEST_F(Sdl3EventsTest, WindowApplicationDropAndTouchEventsAreTranslated)
{
    SDL_Event close{};
    close.window.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    close.window.windowID = m_identifier;
    push(close);
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    push(quit);
    SDL_Event drop{};
    drop.drop.type = SDL_EVENT_DROP_FILE;
    drop.drop.windowID = m_identifier;
    drop.drop.data = "C:/assets/texture.png";
    drop.drop.x = 7.0F;
    push(drop);
    SDL_Event finger{};
    finger.tfinger.type = SDL_EVENT_FINGER_DOWN;
    finger.tfinger.windowID = m_identifier;
    finger.tfinger.touchID = 77;
    finger.tfinger.fingerID = 5;
    finger.tfinger.x = 0.25F;
    finger.tfinger.pressure = 1.0F;
    push(finger);
    const std::vector<ReceivedEvent> events = pump();

    const ReceivedEvent* const closing = first_of(events, EventType::WINDOW_CLOSE_REQUESTED);
    ASSERT_NE(closing, nullptr);
    EXPECT_EQ(closing->event.window(), m_window->handle());
    EXPECT_EQ(closing->event.as<WindowEvent>().size, m_window->size());
    EXPECT_NE(first_of(events, EventType::QUIT_REQUESTED), nullptr);
    const ReceivedEvent* const dropped = first_of(events, EventType::DROP_FILE);
    ASSERT_NE(dropped, nullptr);
    EXPECT_EQ(dropped->text, "C:/assets/texture.png");
    EXPECT_FLOAT_EQ(dropped->event.as<DropEvent>().position.x, 7.0F);
    const ReceivedEvent* const touched = first_of(events, EventType::TOUCH_DOWN);
    ASSERT_NE(touched, nullptr);
    EXPECT_EQ(touched->event.as<TouchEvent>().finger, 5U);
    EXPECT_FLOAT_EQ(touched->event.as<TouchEvent>().position.x, 0.25F);
    const std::vector<TouchDeviceHandle> devices = m_platform->input_devices().touch_devices();
    EXPECT_NE(std::ranges::find(devices, touched->event.as<TouchEvent>().device), devices.end());
}

TEST_F(Sdl3EventsTest, EventsOfDestroyedOrUnknownWindowsAreDropped)
{
    // An event for an unknown window, and one queued for a window destroyed before the pump.
    SDL_Event unknown{};
    unknown.key.type = SDL_EVENT_KEY_DOWN;
    unknown.key.windowID = 999999;
    unknown.key.scancode = SDL_SCANCODE_A;
    push(unknown);
    SDL_Event pending{};
    pending.key.type = SDL_EVENT_KEY_DOWN;
    pending.key.windowID = m_identifier;
    pending.key.scancode = SDL_SCANCODE_B;
    push(pending);
    m_window.reset();
    const std::vector<ReceivedEvent> events = pump();
    EXPECT_EQ(first_of(events, EventType::KEY_DOWN), nullptr);
}

TEST_F(Sdl3EventsTest, EventsSdlRaisesDuringThePumpReachWatchersAtOnceAndOnlyOnce)
{
    // A watcher reacting to A makes SDL raise B on the platform thread during the pump: SDL's event watch
    // translates B at once (watchers see it during the dispatch of A), and its queued copy is skipped.
    std::vector<Key> watched;
    EventWatcher watcher = m_platform->add_event_watcher([this, &watched](const Event& event, std::string_view)
    {
        if (event.type() != EventType::KEY_DOWN)
        {
            return;
        }
        watched.push_back(event.as<KeyboardEvent>().key);
        if (event.as<KeyboardEvent>().key == Key::A)
        {
            SDL_Event follow{};
            follow.key.type = SDL_EVENT_KEY_DOWN;
            follow.key.windowID = m_identifier;
            follow.key.scancode = SDL_SCANCODE_B;
            SDL_PushEvent(&follow);
            watched.push_back(Key::UNKNOWN);
        }
    });
    SDL_Event first{};
    first.key.type = SDL_EVENT_KEY_DOWN;
    first.key.windowID = m_identifier;
    first.key.scancode = SDL_SCANCODE_A;
    push(first);
    const std::vector<ReceivedEvent> events = pump();

    // B was watched inside A's dispatch (before the marker), and delivered once.
    const std::vector<Key> expected{Key::A, Key::B, Key::UNKNOWN};
    EXPECT_EQ(watched, expected);
    const auto keys_b = std::ranges::count_if(events, [](const ReceivedEvent& event)
    {
        return event.event.type() == EventType::KEY_DOWN && event.event.as<KeyboardEvent>().key == Key::B;
    });
    EXPECT_EQ(keys_b, 1);
    EXPECT_TRUE(pump().empty());
}

} // namespace
