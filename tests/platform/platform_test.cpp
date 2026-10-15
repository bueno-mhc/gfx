#include <gtest/gtest.h>

import std;
import gfx.platform;
import gfx.tests.fake_platform;

namespace
{

using namespace gfx::platform;
using namespace std::chrono_literals;
using gfx::tests::FakePlatform;

auto
contains(std::string_view text,
         std::string_view part) -> bool
{
    return text.find(part) != std::string_view::npos;
}

/// A key press of @p window.
auto
key_down(WindowHandle window,
         Key key) -> Event
{
    return Event::make<EventType::KEY_DOWN>(window, 1ms, KeyboardEvent{.key = key});
}

TEST(PlatformTest, PumpsReturnTheEventsOfTheSystem)
{
    FakePlatform platform;
    std::unique_ptr<Window> window = platform.create_window({});
    platform.queue_native_event(key_down(window->handle(), Key::A));
    platform.queue_native_event(Event::make<EventType::TEXT_INPUT>(window->handle(), 2ms, TextInputEvent{}), "a");

    const EventSpan events = platform.poll_events();
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].as<KeyboardEvent>().key, Key::A);
    EXPECT_EQ(events.text(events[1]), "a");
    EXPECT_EQ(platform.last_wait_mode(), EventWaitMode::POLL);

    // The next pump starts afresh.
    EXPECT_TRUE(platform.poll_events().empty());
}

TEST(PlatformTest, EventsPushedFromOtherThreadsArriveAtTheNextPump)
{
    FakePlatform platform;
    std::jthread producer{[&]
    {
        platform.push_event(Event::make<EventType::DROP_TEXT>(WindowHandle{}, 3ms, DropEvent{}), "dropped text");
    }};
    producer.join();

    // Pushed events are already waiting: the pump does not block.
    const EventSpan events = platform.wait_events();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events.text(events[0]), "dropped text");
    EXPECT_EQ(platform.last_wait_mode(), EventWaitMode::POLL);
}

TEST(PlatformTest, WaitsEndWhenAnotherThreadPushesOrWakes)
{
    FakePlatform platform;

    // A push during the wait wakes it and is delivered by the same pump.
    std::jthread pusher{[&]
    {
        std::this_thread::sleep_for(20ms);
        platform.push_event(Event::make<EventType::QUIT_REQUESTED>(1ms, ApplicationEvent{}));
    }};
    const EventSpan events = platform.wait_events();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].type(), EventType::QUIT_REQUESTED);
    pusher.join();

    // wake() ends a wait without an event; a timeout ends one by itself.
    std::jthread waker{[&]
    {
        std::this_thread::sleep_for(20ms);
        platform.wake();
    }};
    EXPECT_TRUE(platform.wait_events().empty());
    waker.join();
    const auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(platform.wait_events_timeout(10ms).empty());
    EXPECT_GE(std::chrono::steady_clock::now() - start, 9ms);
    EXPECT_EQ(platform.last_wait_mode(), EventWaitMode::WAIT_WITH_TIMEOUT);
}

TEST(PlatformTest, OnlyThePlatformThreadPumpsAndCreatesWindows)
{
    FakePlatform platform;
    EXPECT_TRUE(platform.is_platform_thread());
    std::string message;
    std::jthread other{[&]
    {
        try
        {
            (void)platform.poll_events();
        }
        catch (const gfx::InvalidUsageError& error)
        {
            message = error.what();
        }
        EXPECT_THROW((void)platform.create_window({}), gfx::InvalidUsageError);
        EXPECT_FALSE(platform.is_platform_thread());
    }};
    other.join();
    EXPECT_TRUE(contains(message, "called from another thread than the platform thread")) << message;

    // Without validation the check is skipped (the caller takes responsibility).
    FakePlatform unchecked{{.validation_level = gfx::ValidationLevel::NONE}};
    std::jthread unchecked_thread{[&]
    {
        EXPECT_NO_THROW((void)unchecked.poll_events());
    }};
}

TEST(PlatformTest, WatchersSeeEventsWhileTheyArePumped)
{
    FakePlatform platform;
    std::vector<EventType> seen;
    std::string text;
    EventWatcher watcher = platform.add_event_watcher([&](const Event& event, std::string_view event_text)
    {
        seen.push_back(event.type());
        text += event_text;
    });
    platform.queue_native_event(key_down(WindowHandle{}, Key::B));
    platform.queue_native_event(Event::make<EventType::TEXT_INPUT>(WindowHandle{}, 1ms, TextInputEvent{}), "b");
    (void)platform.poll_events();
    EXPECT_EQ(seen, (std::vector{EventType::KEY_DOWN, EventType::TEXT_INPUT}));
    EXPECT_EQ(text, "b");
    EXPECT_TRUE(watcher.is_active());

    // Destroying the registration stops the calls.
    watcher.reset();
    platform.queue_native_event(key_down(WindowHandle{}, Key::C));
    (void)platform.poll_events();
    EXPECT_EQ(seen.size(), 2U);
}

TEST(PlatformTest, WatchersMayUnregisterThemselvesButNotPump)
{
    FakePlatform platform;
    int calls = 0;
    EventWatcher watcher;
    watcher = platform.add_event_watcher([&](const Event&, std::string_view)
    {
        ++calls;
        watcher.reset();                                                      // removing itself is safe
        EXPECT_THROW((void)platform.poll_events(), gfx::InvalidUsageError); // pumping from a watcher is not
    });
    platform.queue_native_event(key_down(WindowHandle{}, Key::A));
    platform.queue_native_event(key_down(WindowHandle{}, Key::B));
    (void)platform.poll_events();
    EXPECT_EQ(calls, 1);
}

TEST(PlatformTest, WatchersOutlivingThePlatformDetach)
{
    EventWatcher watcher;
    {
        FakePlatform platform;
        watcher = platform.add_event_watcher([](const Event&, std::string_view) {});
        EXPECT_TRUE(watcher.is_active());
    }
    EXPECT_FALSE(watcher.is_active());
    watcher.reset(); // nothing to unregister from
}

TEST(PlatformTest, DestroyedWindowsLoseTheirEventsAndReleaseLater)
{
    FakePlatform platform;
    std::unique_ptr<Window> kept = platform.create_window({});
    std::unique_ptr<Window> closed = platform.create_window({});
    const WindowHandle closed_handle = closed->handle();

    // A window destroyed by the application: its pending events are dropped, its native window is
    // released at the next pump.
    platform.queue_native_event(key_down(closed_handle, Key::A));
    platform.queue_native_event(key_down(kept->handle(), Key::B));
    closed.reset();
    EXPECT_EQ(platform.released_windows(), 0);
    EventSpan events = platform.poll_events();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].window(), kept->handle());
    EXPECT_EQ(platform.released_windows(), 1);

    // A window destroyed during a pump (by a watcher): events it already got are dropped too.
    std::unique_ptr<Window> temporary = platform.create_window({});
    platform.queue_native_event(key_down(temporary->handle(), Key::C));
    platform.during_next_pump([&]
    {
        temporary.reset();
    });
    events = platform.poll_events();
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(platform.released_windows(), 1) << "released at the next pump, not inside this one";
    (void)platform.poll_events();
    EXPECT_EQ(platform.released_windows(), 2);
}

TEST(PlatformTest, CursorsMonitorsAndClipboard)
{
    FakePlatform platform;
    std::unique_ptr<Cursor> pointer = platform.create_cursor(CursorShape::POINTER);
    EXPECT_EQ(pointer->shape(), CursorShape::POINTER);
    constexpr std::array<std::byte, 64> pixels{};
    std::unique_ptr<Cursor> custom = platform.create_cursor({.image = ImageData::from_rgba({.width = 4, .height = 4, .depth = 1}, pixels),
                                                             .hot_spot = {.x = 1, .y = 1, .z = 0}});
    EXPECT_FALSE(custom->shape().has_value());
    EXPECT_NE(pointer->handle(), custom->handle());

    std::unique_ptr<Window> window = platform.create_window({});
    window->set_cursor(custom->handle());
    EXPECT_EQ(static_cast<gfx::tests::FakeWindow&>(*window).cursor(), custom->handle());

    ASSERT_EQ(platform.monitors().size(), 1U);
    const MonitorProperties monitor = platform.monitor_properties(platform.primary_monitor());
    EXPECT_TRUE(monitor.primary);
    EXPECT_EQ(monitor.current_mode.refresh_rate, 60.0F);

    platform.set_clipboard_text("copied");
    EXPECT_EQ(platform.clipboard_text(), "copied");
}

TEST(PlatformBackendFactoryTest, MissingBackendsSayWhy)
{
    for (const auto& entry : gfx::EnumerationTraits<PlatformBackendKind>::NAMES)
    {
        const PlatformBackendAvailability availability = platform_backend_availability(entry.value);
        EXPECT_EQ(availability.backend, entry.value);
        EXPECT_EQ(availability.available, availability.reason.empty()) << entry.name;
    }
    EXPECT_TRUE(contains(platform_backend_availability(PlatformBackendKind::COCOA).reason, "the supported backends are SDL3 and GLFW"));
    try
    {
        (void)create_platform(PlatformBackendKind::WEB);
        FAIL() << "create_platform must throw";
    }
    catch (const gfx::BackendCapabilityError& error)
    {
        EXPECT_EQ(error.backend(), PlatformBackendKind::WEB);
        EXPECT_TRUE(contains(error.what(), "[gfx::platform::WEB] create_platform")) << error.what();
    }
}

} // namespace
