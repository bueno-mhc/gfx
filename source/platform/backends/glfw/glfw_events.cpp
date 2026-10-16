/// @file glfw_events.cpp
/// @brief GLFW callbacks translated into gfx events, the pump, and the polling of gamepads and
///        joysticks (GLFW reports their connections, not their input).
module;

#include "glfw_headers.hpp"

module gfx.platform.backends.glfw;

import std;
import gfx.core;
import gfx.core.handle_table;
import gfx.platform.enums;
import gfx.platform.handles;
import gfx.platform.interface;
import :internal;

namespace gfx::platform::glfw
{

namespace
{

/// The platform GLFW's process-wide callbacks (monitors, joysticks) report to.
GlfwPlatform* g_callback_platform = nullptr;

/// Runs @p handle with the gfx window of @p native; exceptions wait for the pump (C callbacks cannot throw).
template<class Handler>
void
with_window(GLFWwindow* native,
            Handler handle) noexcept
{
    auto* const window = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(native));
    if (window == nullptr)
    {
        return;
    }
    try
    {
        handle(*window);
    }
    catch (...)
    {
        window->platform().record_failure(std::current_exception());
    }
}

/// Reports a window event of type @p TYPE with the window's state.
template<EventType TYPE>
void
report_window_event(GlfwWindow& window)
{
    const WindowEvent state = window.refresh();
    window.platform().deliver(Event::make<TYPE>(window.handle(), GlfwPlatform::now(), state));
}

// ------------------------------------------------------------------------------------------------- Window callbacks

void
on_position(GLFWwindow* native,
            int /*x*/,
            int /*y*/)
{
    with_window(native, [](GlfwWindow& window) { report_window_event<EventType::WINDOW_MOVED>(window); });
}

void
on_size(GLFWwindow* native,
        int /*width*/,
        int /*height*/)
{
    with_window(native, [](GlfwWindow& window) { report_window_event<EventType::WINDOW_RESIZED>(window); });
}

void
on_framebuffer_size(GLFWwindow* native,
                    int /*width*/,
                    int /*height*/)
{
    with_window(native, [](GlfwWindow& window) { report_window_event<EventType::WINDOW_FRAMEBUFFER_RESIZED>(window); });
}

void
on_content_scale(GLFWwindow* native,
                 float /*x*/,
                 float /*y*/)
{
    with_window(native, [](GlfwWindow& window) { report_window_event<EventType::WINDOW_CONTENT_SCALE_CHANGED>(window); });
}

void
on_refresh(GLFWwindow* native)
{
    with_window(native, [](GlfwWindow& window) { report_window_event<EventType::WINDOW_EXPOSED>(window); });
}

void
on_focus(GLFWwindow* native,
         int focused)
{
    with_window(native, [focused](GlfwWindow& window)
    {
        focused == GLFW_TRUE ? report_window_event<EventType::WINDOW_FOCUS_GAINED>(window)
                             : report_window_event<EventType::WINDOW_FOCUS_LOST>(window);
    });
}

void
on_iconify(GLFWwindow* native,
           int iconified)
{
    with_window(native, [iconified](GlfwWindow& window)
    {
        iconified == GLFW_TRUE ? report_window_event<EventType::WINDOW_MINIMIZED>(window)
                               : report_window_event<EventType::WINDOW_RESTORED>(window);
    });
}

void
on_maximize(GLFWwindow* native,
            int maximized)
{
    with_window(native, [maximized](GlfwWindow& window)
    {
        maximized == GLFW_TRUE ? report_window_event<EventType::WINDOW_MAXIMIZED>(window)
                               : report_window_event<EventType::WINDOW_RESTORED>(window);
    });
}

void
on_close(GLFWwindow* native)
{
    // Only a request: the application destroys the window if it agrees.
    glfwSetWindowShouldClose(native, GLFW_FALSE);
    with_window(native, [](GlfwWindow& window) { report_window_event<EventType::WINDOW_CLOSE_REQUESTED>(window); });
}

void
on_cursor_enter(GLFWwindow* native,
                int entered)
{
    with_window(native, [entered](GlfwWindow& window)
    {
        entered == GLFW_TRUE ? report_window_event<EventType::WINDOW_MOUSE_ENTERED>(window)
                             : report_window_event<EventType::WINDOW_MOUSE_LEFT>(window);
    });
}

// ------------------------------------------------------------------------------------------------- Input callbacks

void
on_key(GLFWwindow* native,
       int key,
       int scancode,
       int action,
       int modifiers)
{
    with_window(native, [native, key, scancode, action, modifiers](GlfwWindow& window)
    {
        const KeyboardEvent payload{
            .key = to_key(key),
            .key_code = to_key_code(key, scancode),
            .modifiers = to_key_modifier(native, modifiers),
            .native_scancode = static_cast<std::uint32_t>(std::max(scancode, 0)),
            .repeat = action == GLFW_REPEAT,
        };
        window.platform().deliver(action == GLFW_RELEASE ? Event::make<EventType::KEY_UP>(window.handle(), GlfwPlatform::now(), payload)
                                                         : Event::make<EventType::KEY_DOWN>(window.handle(), GlfwPlatform::now(), payload));
    });
}

void
on_character(GLFWwindow* native,
             unsigned int code_point)
{
    // Characters reach the application while text input is active.
    with_window(native, [code_point](GlfwWindow& window)
    {
        if (window.reports_text())
        {
            window.platform().deliver(Event::make<EventType::TEXT_INPUT>(window.handle(), GlfwPlatform::now(), TextInputEvent{}),
                                      utf8(code_point));
        }
    });
}

void
on_cursor_position(GLFWwindow* native,
                   double x,
                   double y)
{
    // GLFW reports positions; the delta is from the previous one.
    with_window(native, [x, y](GlfwWindow& window)
    {
        const Vector2 position{.x = static_cast<float>(x), .y = static_cast<float>(y)};
        const Vector2 previous = window.last_cursor_position().value_or(position);
        window.set_last_cursor_position(position);
        const MouseMotionEvent motion{
            .position = position,
            .delta = {.x = position.x - previous.x, .y = position.y - previous.y},
            .buttons = window.held_buttons(),
        };
        window.platform().deliver(Event::make<EventType::MOUSE_MOTION>(window.handle(), GlfwPlatform::now(), motion));
    });
}

void
on_mouse_button(GLFWwindow* native,
                int button,
                int action,
                int /*modifiers*/)
{
    // GLFW does not count clicks: each press is a single click.
    with_window(native, [native, button, action](GlfwWindow& window)
    {
        const std::optional<MouseButton> mouse_button = to_mouse_button(button);
        if (!mouse_button)
        {
            return;
        }
        window.set_button(*mouse_button, action == GLFW_PRESS);
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(native, &x, &y);
        const MouseButtonEvent payload{.button = *mouse_button,
                                       .click_count = 1,
                                       .position = {.x = static_cast<float>(x), .y = static_cast<float>(y)}};
        window.platform().deliver(action == GLFW_PRESS ? Event::make<EventType::MOUSE_BUTTON_DOWN>(window.handle(), GlfwPlatform::now(), payload)
                                                       : Event::make<EventType::MOUSE_BUTTON_UP>(window.handle(), GlfwPlatform::now(), payload));
    });
}

void
on_scroll(GLFWwindow* native,
          double x,
          double y)
{
    // Whole notches accumulate from the precise offsets.
    with_window(native, [native, x, y](GlfwWindow& window)
    {
        const auto [steps_x, steps_y] = window.wheel_steps(x, y);
        double cursor_x = 0.0;
        double cursor_y = 0.0;
        glfwGetCursorPos(native, &cursor_x, &cursor_y);
        const MouseWheelEvent wheel{
            .delta = {.x = static_cast<float>(x), .y = static_cast<float>(y)},
            .steps_x = steps_x,
            .steps_y = steps_y,
            .position = {.x = static_cast<float>(cursor_x), .y = static_cast<float>(cursor_y)},
            .flipped = false,
        };
        window.platform().deliver(Event::make<EventType::MOUSE_WHEEL>(window.handle(), GlfwPlatform::now(), wheel));
    });
}

void
on_drop(GLFWwindow* native,
        int count,
        const char** paths)
{
    // One event per file, at the cursor (GLFW gives no drop position).
    with_window(native, [native, count, paths](GlfwWindow& window)
    {
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(native, &x, &y);
        for (int index = 0; index < count; ++index)
        {
            const DropEvent drop{.text = {}, .position = {.x = static_cast<float>(x), .y = static_cast<float>(y)}};
            window.platform().deliver(Event::make<EventType::DROP_FILE>(window.handle(), GlfwPlatform::now(), drop), paths[index]);
        }
    });
}

// ------------------------------------------------------------------------------------------------- Process-wide callbacks

void
on_monitor(GLFWmonitor* monitor,
           int event)
{
    GlfwPlatform* const platform = g_callback_platform;
    if (platform == nullptr)
    {
        return;
    }
    try
    {
        // A new monitor gets a handle; a removed one is reported with its handle, which goes stale.
        if (event == GLFW_CONNECTED)
        {
            const MonitorHandle handle = platform->register_monitor(monitor);
            platform->deliver(Event::make<EventType::MONITOR_CONNECTED>(GlfwPlatform::now(), MonitorEvent{.monitor = handle}));
        }
        else if (const MonitorHandle handle = platform->unregister_monitor(monitor); handle.is_valid())
        {
            platform->deliver(Event::make<EventType::MONITOR_DISCONNECTED>(GlfwPlatform::now(), MonitorEvent{.monitor = handle}));
        }
    }
    catch (...)
    {
        platform->record_failure(std::current_exception());
    }
}

void
on_joystick(int joystick,
            int event)
{
    GlfwPlatform* const platform = g_callback_platform;
    if (platform == nullptr)
    {
        return;
    }
    try
    {
        if (event == GLFW_CONNECTED)
        {
            platform->devices().connect(joystick, GlfwPlatform::now());
        }
        else
        {
            platform->devices().disconnect(joystick, GlfwPlatform::now());
        }
    }
    catch (...)
    {
        platform->record_failure(std::current_exception());
    }
}

} // namespace

void
install_window_callbacks(GLFWwindow* window) noexcept
{
    glfwSetWindowPosCallback(window, &on_position);
    glfwSetWindowSizeCallback(window, &on_size);
    glfwSetFramebufferSizeCallback(window, &on_framebuffer_size);
    glfwSetWindowContentScaleCallback(window, &on_content_scale);
    glfwSetWindowRefreshCallback(window, &on_refresh);
    glfwSetWindowFocusCallback(window, &on_focus);
    glfwSetWindowIconifyCallback(window, &on_iconify);
    glfwSetWindowMaximizeCallback(window, &on_maximize);
    glfwSetWindowCloseCallback(window, &on_close);
    glfwSetCursorEnterCallback(window, &on_cursor_enter);
    glfwSetKeyCallback(window, &on_key);
    glfwSetCharCallback(window, &on_character);
    glfwSetCursorPosCallback(window, &on_cursor_position);
    glfwSetMouseButtonCallback(window, &on_mouse_button);
    glfwSetScrollCallback(window, &on_scroll);
    glfwSetDropCallback(window, &on_drop);
}

void
install_platform_callbacks(GlfwPlatform* platform) noexcept
{
    g_callback_platform = platform;
    glfwSetMonitorCallback(platform != nullptr ? &on_monitor : nullptr);
    glfwSetJoystickCallback(platform != nullptr ? &on_joystick : nullptr);
}

// ================================================================================================= GlfwPlatform: events

void
GlfwPlatform::deliver(const Event& event,
                      std::string_view text)
{
    // During a pump at once (watchers run in real time); otherwise at the next pump.
    if (m_in_pump)
    {
        event.text() ? emit(event, text) : emit(event);
    }
    else
    {
        event.text() ? m_pending.push(event, text) : m_pending.push(event);
    }
}

void
GlfwPlatform::record_failure(std::exception_ptr failure) noexcept
{
    if (!m_callback_failure)
    {
        m_callback_failure = std::move(failure);
    }
}

void
GlfwPlatform::pump(EventWaitMode mode,
                   std::chrono::nanoseconds timeout)
{
    // Callbacks emit at once from here on.
    m_in_pump = true;
    struct PumpGuard
    {
        GlfwPlatform& platform;

        ~PumpGuard()
        {
            platform.m_in_pump = false;
        }
    } guard{*this};

    // What GLFW reported between pumps comes first (and means there is no need to wait).
    if (!m_pending.empty())
    {
        const EventSpan pending = m_pending.view();
        for (const Event& event : pending)
        {
            event.text() ? emit(event, pending.text(event)) : emit(event);
        }
        m_pending.clear();
        mode = EventWaitMode::POLL;
    }

    // GLFW processes the system's events, calling the callbacks; gamepads and joysticks are polled
    // (their input does not end a wait: GLFW reports none). Any system message ends GLFW's wait, also
    // one that becomes no event: waits go on until an event, a wake or the deadline.
    const std::size_t before = events().size();
    const std::chrono::nanoseconds deadline = now() + timeout;
    for (;;)
    {
        const std::chrono::nanoseconds remaining = deadline - now();
        if (mode == EventWaitMode::POLL || (mode == EventWaitMode::WAIT_WITH_TIMEOUT && remaining <= std::chrono::nanoseconds::zero()))
        {
            glfwPollEvents();
        }
        else if (mode == EventWaitMode::WAIT)
        {
            glfwWaitEvents();
        }
        else
        {
            glfwWaitEventsTimeout(std::chrono::duration<double>(remaining).count());
        }
        if (m_callback_failure)
        {
            std::rethrow_exception(std::exchange(m_callback_failure, nullptr));
        }
        m_devices->poll(now());
        clear_glfw_error();
        const bool woken = m_wake_requested.exchange(false);
        if (mode == EventWaitMode::POLL || woken || events().size() > before
            || (mode == EventWaitMode::WAIT_WITH_TIMEOUT && now() >= deadline))
        {
            return;
        }
    }
}

void
GlfwPlatform::wake_backend() noexcept
{
    // Thread-safe in GLFW: ends glfwWaitEvents; the flag tells the pump to stop waiting.
    m_wake_requested.store(true);
    glfwPostEmptyEvent();
}

} // namespace gfx::platform::glfw
