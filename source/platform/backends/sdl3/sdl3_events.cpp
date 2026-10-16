/// @file sdl3_events.cpp
/// @brief The SDL3 event pump: SDL events translated into gfx events.
///
/// Events reach the translation two ways. SDL's event watch sees every event as it is pushed: events SDL
/// generates on the platform thread during the pump are translated there and then, so event watchers run
/// in real time (Windows resizes and moves windows in a loop of its own, inside SDL's pump). Their copies
/// in SDL's queue are skipped when polled, in this pump or the next (SDL's poll stops at a sentinel, so
/// an event raised late in a pump waits for the next one). Events pushed by other threads (devices,
/// wake-ups, the application) are translated when polled.
module;

#include "sdl3_headers.hpp"

module gfx.platform.backends.sdl3;

import std;
import gfx.core;
import gfx.core.handle_table;
import gfx.platform.enums;
import gfx.platform.handles;
import gfx.platform.interface;
import :internal;

namespace gfx::platform::sdl3
{

namespace
{

/// A window event of type @p TYPE with the window's state.
template<EventType TYPE>
auto
window_event(WindowHandle window,
             std::chrono::nanoseconds time,
             const WindowEvent& state) -> Event
{
    return Event::make<TYPE>(window, time, state);
}

/// Whether @p first and @p second are the same SDL event (the watch's copy and the queue's).
auto
same_event(const SDL_Event& first,
           const SDL_Event& second) noexcept -> bool
{
    return first.type == second.type && first.common.timestamp == second.common.timestamp
           && std::memcmp(&first, &second, sizeof(SDL_Event)) == 0;
}

} // namespace

void
Sdl3Platform::watch_native_event(const SDL_Event& event) noexcept
{
    // Other threads' events, and events outside a pump, are translated when polled.
    if (!is_platform_thread() || !m_in_native_pump || event.type == m_wake_event)
    {
        return;
    }
    try
    {
        translate(event);
        m_translated.emplace_back(m_pump_number, event);
    }
    catch (...)
    {
        // A C callback cannot throw: the pump rethrows the first failure once SDL returns.
        if (!m_watch_failure)
        {
            m_watch_failure = std::current_exception();
        }
    }
}

void
Sdl3Platform::pump(EventWaitMode mode,
                   std::chrono::nanoseconds timeout)
{
    // The watch translates what SDL generates from here on; copies older than the last pump are gone.
    ++m_pump_number;
    std::erase_if(m_translated, [this](const auto& entry) { return entry.first + 1 < m_pump_number; });
    m_in_native_pump = true;
    struct PumpGuard
    {
        Sdl3Platform& platform;

        ~PumpGuard()
        {
            platform.m_in_native_pump = false;
            platform.m_watch_failure = nullptr;
        }
    } guard{*this};
    const auto rethrow_watch_failure = [this]
    {
        if (m_watch_failure)
        {
            std::rethrow_exception(std::exchange(m_watch_failure, nullptr));
        }
    };

    // The first event, as the mode says, then everything pending. An SDL event that becomes no gfx
    // event does not end a wait: waits go on until an event, a wake or the deadline.
    const std::size_t before = events().size();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;)
    {
        SDL_Event event{};
        bool received = false;
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        switch (mode)
        {
            case EventWaitMode::POLL:
                received = SDL_PollEvent(&event);
                break;
            case EventWaitMode::WAIT:
                received = SDL_WaitEvent(&event);
                break;
            case EventWaitMode::WAIT_WITH_TIMEOUT:
                // SDL waits in whole milliseconds: rounded up, so that a wait never ends early.
                received = SDL_WaitEventTimeout(&event, static_cast<Sint32>(std::clamp<std::chrono::milliseconds::rep>(
                                                            remaining.count(), 0, std::numeric_limits<Sint32>::max())));
                break;
        }
        bool woken = false;
        while (received)
        {
            rethrow_watch_failure();
            const auto translated = std::ranges::find_if(m_translated, [&event](const auto& entry) { return same_event(entry.second, event); });
            if (translated != m_translated.end())
            {
                m_translated.erase(translated);
            }
            else if (event.type == m_wake_event)
            {
                woken = true;
            }
            else
            {
                translate(event);
            }
            received = SDL_PollEvent(&event);
        }
        rethrow_watch_failure();
        if (mode == EventWaitMode::POLL || woken || events().size() > before
            || (mode == EventWaitMode::WAIT_WITH_TIMEOUT && std::chrono::steady_clock::now() >= deadline))
        {
            return;
        }
    }
}

void
Sdl3Platform::wake_backend() noexcept
{
    // An event of our own type ends SDL's wait (SDL_PushEvent is thread-safe).
    SDL_Event event{};
    event.type = m_wake_event;
    static_cast<void>(SDL_PushEvent(&event));
}

auto
Sdl3Platform::steady_time(Uint64 timestamp) const noexcept -> std::chrono::nanoseconds
{
    return m_clock_offset + std::chrono::nanoseconds{static_cast<std::int64_t>(timestamp)};
}

auto
Sdl3Platform::window_of(SDL_WindowID identifier) const noexcept -> std::optional<WindowHandle>
{
    // No window (device-level events) is a null handle; an unknown window drops the event.
    if (identifier == 0)
    {
        return WindowHandle{};
    }
    const Sdl3Window* const window = find_window(identifier);
    return window != nullptr ? std::optional<WindowHandle>{window->handle()} : std::nullopt;
}

void
Sdl3Platform::translate(const SDL_Event& event)
{
    const std::chrono::nanoseconds time = steady_time(event.common.timestamp);
    const auto type = static_cast<SDL_EventType>(event.type);
    if (type >= SDL_EVENT_WINDOW_FIRST && type <= SDL_EVENT_WINDOW_LAST)
    {
        translate_window_event(event);
        return;
    }
    switch (type)
    {
        case SDL_EVENT_QUIT:
            emit(Event::make<EventType::QUIT_REQUESTED>(time, ApplicationEvent{}));
            break;
        case SDL_EVENT_KEYMAP_CHANGED:
            emit(Event::make<EventType::KEYBOARD_LAYOUT_CHANGED>(time, ApplicationEvent{}));
            break;
        case SDL_EVENT_CLIPBOARD_UPDATE:
            emit(Event::make<EventType::CLIPBOARD_UPDATED>(time, ApplicationEvent{}));
            break;
        case SDL_EVENT_DISPLAY_ADDED:
        case SDL_EVENT_DISPLAY_REMOVED:
            translate_display_event(event);
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            const std::optional<WindowHandle> window = window_of(event.key.windowID);
            if (!window)
            {
                break;
            }
            const KeyboardEvent key{
                .key = to_key(event.key.scancode),
                .key_code = to_key_code(event.key.key),
                .modifiers = to_key_modifier(event.key.mod),
                .native_scancode = event.key.raw,
                .repeat = event.key.repeat,
            };
            emit(type == SDL_EVENT_KEY_DOWN ? Event::make<EventType::KEY_DOWN>(*window, time, key)
                                            : Event::make<EventType::KEY_UP>(*window, time, key));
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
        {
            const std::optional<WindowHandle> window = window_of(event.text.windowID);
            if (window)
            {
                emit(Event::make<EventType::TEXT_INPUT>(*window, time, TextInputEvent{}),
                     event.text.text != nullptr ? std::string_view{event.text.text} : std::string_view{});
            }
            break;
        }
        case SDL_EVENT_TEXT_EDITING:
        {
            const std::optional<WindowHandle> window = window_of(event.edit.windowID);
            if (window)
            {
                const TextCompositionEvent composition{.text = {}, .cursor = event.edit.start, .selection_length = event.edit.length};
                emit(Event::make<EventType::TEXT_COMPOSITION>(*window, time, composition),
                     event.edit.text != nullptr ? std::string_view{event.edit.text} : std::string_view{});
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
        {
            const std::optional<WindowHandle> window = window_of(event.motion.windowID);
            if (window)
            {
                const MouseMotionEvent motion{
                    .position = {.x = event.motion.x, .y = event.motion.y},
                    .delta = {.x = event.motion.xrel, .y = event.motion.yrel},
                    .buttons = static_cast<MouseButtonMask>(event.motion.state & 0x1FU),
                };
                emit(Event::make<EventType::MOUSE_MOTION>(*window, time, motion));
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            const std::optional<WindowHandle> window = window_of(event.button.windowID);
            const std::optional<MouseButton> button = to_mouse_button(event.button.button);
            if (window && button)
            {
                const MouseButtonEvent payload{.button = *button,
                                               .click_count = event.button.clicks,
                                               .position = {.x = event.button.x, .y = event.button.y}};
                emit(type == SDL_EVENT_MOUSE_BUTTON_DOWN ? Event::make<EventType::MOUSE_BUTTON_DOWN>(*window, time, payload)
                                                         : Event::make<EventType::MOUSE_BUTTON_UP>(*window, time, payload));
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
        {
            const std::optional<WindowHandle> window = window_of(event.wheel.windowID);
            if (window)
            {
                const MouseWheelEvent wheel{
                    .delta = {.x = event.wheel.x, .y = event.wheel.y},
                    .steps_x = event.wheel.integer_x,
                    .steps_y = event.wheel.integer_y,
                    .position = {.x = event.wheel.mouse_x, .y = event.wheel.mouse_y},
                    .flipped = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED,
                };
                emit(Event::make<EventType::MOUSE_WHEEL>(*window, time, wheel));
            }
            break;
        }
        case SDL_EVENT_DROP_FILE:
        case SDL_EVENT_DROP_TEXT:
        {
            const std::optional<WindowHandle> window = window_of(event.drop.windowID);
            if (window)
            {
                const DropEvent drop{.text = {}, .position = {.x = event.drop.x, .y = event.drop.y}};
                const std::string_view data = event.drop.data != nullptr ? std::string_view{event.drop.data} : std::string_view{};
                emit(type == SDL_EVENT_DROP_FILE ? Event::make<EventType::DROP_FILE>(*window, time, drop)
                                                 : Event::make<EventType::DROP_TEXT>(*window, time, drop),
                     data);
            }
            break;
        }
        default:
            translate_device_event(event);
            break;
    }
}

void
Sdl3Platform::translate_window_event(const SDL_Event& event)
{
    // Every window event refreshes the window's thread-safe state and carries it.
    Sdl3Window* const window = find_window(event.window.windowID);
    if (window == nullptr)
    {
        return;
    }
    const float previous_scale = window->content_scale();
    const WindowEvent state = window->refresh();
    const WindowHandle handle = window->handle();
    const std::chrono::nanoseconds time = steady_time(event.common.timestamp);
    switch (static_cast<SDL_EventType>(event.type))
    {
        case SDL_EVENT_WINDOW_SHOWN:
            emit(window_event<EventType::WINDOW_SHOWN>(handle, time, state));
            break;
        case SDL_EVENT_WINDOW_HIDDEN:
            emit(window_event<EventType::WINDOW_HIDDEN>(handle, time, state));
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
            emit(window_event<EventType::WINDOW_EXPOSED>(handle, time, state));
            break;
        case SDL_EVENT_WINDOW_MOVED:
            emit(window_event<EventType::WINDOW_MOVED>(handle, time, state));
            break;
        // Sizes are events only when they changed (SDL also reports them when a window is first shown).
        case SDL_EVENT_WINDOW_RESIZED:
            if (window->report_size(state.size))
            {
                emit(window_event<EventType::WINDOW_RESIZED>(handle, time, state));
            }
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (window->report_framebuffer_size(state.framebuffer_size))
            {
                emit(window_event<EventType::WINDOW_FRAMEBUFFER_RESIZED>(handle, time, state));
            }
            break;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            emit(window_event<EventType::WINDOW_CONTENT_SCALE_CHANGED>(handle, time, state));
            break;
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            // Moving to a monitor with another scale.
            if (state.content_scale != previous_scale)
            {
                emit(window_event<EventType::WINDOW_CONTENT_SCALE_CHANGED>(handle, time, state));
            }
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            emit(window_event<EventType::WINDOW_MINIMIZED>(handle, time, state));
            break;
        case SDL_EVENT_WINDOW_MAXIMIZED:
            emit(window_event<EventType::WINDOW_MAXIMIZED>(handle, time, state));
            break;
        case SDL_EVENT_WINDOW_RESTORED:
            emit(window_event<EventType::WINDOW_RESTORED>(handle, time, state));
            break;
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            window->mouse_entered();
            emit(window_event<EventType::WINDOW_MOUSE_ENTERED>(handle, time, state));
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            window->mouse_left();
            emit(window_event<EventType::WINDOW_MOUSE_LEFT>(handle, time, state));
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            emit(window_event<EventType::WINDOW_FOCUS_GAINED>(handle, time, state));
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            emit(window_event<EventType::WINDOW_FOCUS_LOST>(handle, time, state));
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            emit(window_event<EventType::WINDOW_CLOSE_REQUESTED>(handle, time, state));
            break;
        case SDL_EVENT_WINDOW_OCCLUDED:
            emit(window_event<EventType::WINDOW_OCCLUDED>(handle, time, state));
            break;
        default:
            break;
    }
}

void
Sdl3Platform::translate_display_event(const SDL_Event& event)
{
    // A new monitor gets a handle; a removed one is reported, then its handle goes stale.
    const std::chrono::nanoseconds time = steady_time(event.common.timestamp);
    const SDL_DisplayID display = event.display.displayID;
    if (event.type == SDL_EVENT_DISPLAY_ADDED)
    {
        const bool known = monitor_handle(display).is_valid();
        const MonitorHandle monitor = register_display(display);
        if (!known)
        {
            emit(Event::make<EventType::MONITOR_CONNECTED>(time, MonitorEvent{.monitor = monitor}));
        }
        return;
    }
    const MonitorHandle monitor = monitor_handle(display);
    if (!monitor.is_valid())
    {
        return;
    }
    emit(Event::make<EventType::MONITOR_DISCONNECTED>(time, MonitorEvent{.monitor = monitor}));
    static_cast<void>(m_monitors.remove(monitor));
    m_displays.erase(display);
}

void
Sdl3Platform::translate_device_event(const SDL_Event& event)
{
    const std::chrono::nanoseconds time = steady_time(event.common.timestamp);
    switch (static_cast<SDL_EventType>(event.type))
    {
        // ----------------------------------------------------------------------------------------- gamepads
        case SDL_EVENT_GAMEPAD_ADDED:
        {
            bool first_report = false;
            const GamepadHandle gamepad = m_devices->connect_gamepad(event.gdevice.which, first_report);
            if (gamepad.is_valid() && first_report)
            {
                emit(Event::make<EventType::GAMEPAD_CONNECTED>(time, GamepadDeviceEvent{.gamepad = gamepad}));
            }
            break;
        }
        case SDL_EVENT_GAMEPAD_REMOVED:
        {
            const GamepadHandle gamepad = m_devices->disconnect_gamepad(event.gdevice.which);
            if (gamepad.is_valid())
            {
                emit(Event::make<EventType::GAMEPAD_DISCONNECTED>(time, GamepadDeviceEvent{.gamepad = gamepad}));
            }
            break;
        }
        case SDL_EVENT_GAMEPAD_REMAPPED:
        {
            const GamepadHandle gamepad = m_devices->gamepad_handle(event.gdevice.which);
            if (gamepad.is_valid())
            {
                emit(Event::make<EventType::GAMEPAD_REMAPPED>(time, GamepadDeviceEvent{.gamepad = gamepad}));
            }
            break;
        }
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        {
            const GamepadHandle gamepad = m_devices->gamepad_handle(event.gbutton.which);
            const std::optional<GamepadButton> button = to_gamepad_button(event.gbutton.button);
            if (gamepad.is_valid() && button)
            {
                const GamepadButtonEvent payload{.gamepad = gamepad, .button = *button};
                emit(event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? Event::make<EventType::GAMEPAD_BUTTON_DOWN>(time, payload)
                                                                 : Event::make<EventType::GAMEPAD_BUTTON_UP>(time, payload));
            }
            break;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        {
            const GamepadHandle gamepad = m_devices->gamepad_handle(event.gaxis.which);
            const std::optional<GamepadAxis> axis = to_gamepad_axis(event.gaxis.axis);
            if (gamepad.is_valid() && axis)
            {
                emit(Event::make<EventType::GAMEPAD_AXIS_MOTION>(
                    time, GamepadAxisEvent{.gamepad = gamepad, .axis = *axis, .value = to_axis_value(event.gaxis.value)}));
            }
            break;
        }
        case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
        case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
        case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
        {
            const GamepadHandle gamepad = m_devices->gamepad_handle(event.gtouchpad.which);
            if (!gamepad.is_valid())
            {
                break;
            }
            const GamepadTouchpadEvent touch{
                .gamepad = gamepad,
                .touchpad = static_cast<std::uint32_t>(std::max(event.gtouchpad.touchpad, 0)),
                .finger = static_cast<std::uint32_t>(std::max(event.gtouchpad.finger, 0)),
                .position = {.x = event.gtouchpad.x, .y = event.gtouchpad.y},
                .pressure = event.gtouchpad.pressure,
            };
            if (event.type == SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN)
            {
                emit(Event::make<EventType::GAMEPAD_TOUCHPAD_DOWN>(time, touch));
            }
            else if (event.type == SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION)
            {
                emit(Event::make<EventType::GAMEPAD_TOUCHPAD_MOTION>(time, touch));
            }
            else
            {
                emit(Event::make<EventType::GAMEPAD_TOUCHPAD_UP>(time, touch));
            }
            break;
        }

        // ----------------------------------------------------------------------------------------- joysticks without mapping
        case SDL_EVENT_JOYSTICK_ADDED:
        {
            bool first_report = false;
            const JoystickHandle joystick = m_devices->connect_joystick(event.jdevice.which, first_report);
            if (joystick.is_valid() && first_report)
            {
                emit(Event::make<EventType::JOYSTICK_CONNECTED>(time, JoystickDeviceEvent{.joystick = joystick}));
            }
            break;
        }
        case SDL_EVENT_JOYSTICK_REMOVED:
        {
            const JoystickHandle joystick = m_devices->disconnect_joystick(event.jdevice.which);
            if (joystick.is_valid())
            {
                emit(Event::make<EventType::JOYSTICK_DISCONNECTED>(time, JoystickDeviceEvent{.joystick = joystick}));
            }
            break;
        }
        case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        {
            const JoystickHandle joystick = m_devices->joystick_handle(event.jaxis.which);
            if (joystick.is_valid())
            {
                emit(Event::make<EventType::JOYSTICK_AXIS_MOTION>(
                    time, JoystickAxisEvent{.joystick = joystick, .axis = event.jaxis.axis, .value = to_axis_value(event.jaxis.value)}));
            }
            break;
        }
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_UP:
        {
            const JoystickHandle joystick = m_devices->joystick_handle(event.jbutton.which);
            if (joystick.is_valid())
            {
                const JoystickButtonEvent payload{.joystick = joystick, .button = event.jbutton.button};
                emit(event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN ? Event::make<EventType::JOYSTICK_BUTTON_DOWN>(time, payload)
                                                                  : Event::make<EventType::JOYSTICK_BUTTON_UP>(time, payload));
            }
            break;
        }
        case SDL_EVENT_JOYSTICK_HAT_MOTION:
        {
            const JoystickHandle joystick = m_devices->joystick_handle(event.jhat.which);
            if (joystick.is_valid())
            {
                emit(Event::make<EventType::JOYSTICK_HAT_MOTION>(
                    time, JoystickHatEvent{.joystick = joystick, .hat = event.jhat.hat, .position = to_hat_position(event.jhat.value)}));
            }
            break;
        }

        // ----------------------------------------------------------------------------------------- touch
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
        {
            const std::optional<WindowHandle> window = window_of(event.tfinger.windowID);
            if (!window)
            {
                break;
            }
            const TouchEvent touch{
                .device = m_devices->touch_device_handle(event.tfinger.touchID),
                .finger = event.tfinger.fingerID,
                .position = {.x = event.tfinger.x, .y = event.tfinger.y},
                .delta = {.x = event.tfinger.dx, .y = event.tfinger.dy},
                .pressure = event.tfinger.pressure,
            };
            switch (static_cast<SDL_EventType>(event.type))
            {
                case SDL_EVENT_FINGER_DOWN:
                    emit(Event::make<EventType::TOUCH_DOWN>(*window, time, touch));
                    break;
                case SDL_EVENT_FINGER_MOTION:
                    emit(Event::make<EventType::TOUCH_MOTION>(*window, time, touch));
                    break;
                case SDL_EVENT_FINGER_UP:
                    emit(Event::make<EventType::TOUCH_UP>(*window, time, touch));
                    break;
                default:
                    emit(Event::make<EventType::TOUCH_CANCELED>(*window, time, touch));
                    break;
            }
            break;
        }
        default:
            break;
    }
}

} // namespace gfx::platform::sdl3
