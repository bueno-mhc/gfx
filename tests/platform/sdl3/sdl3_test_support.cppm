// Test support for the SDL3 backend: pumping until an event arrives, and SDL's identifier of a gfx
// window (tests inject SDL events for it with SDL_PushEvent).
module;

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

export module gfx.tests.sdl3_support;

import std;
import gfx.platform;

export namespace gfx::tests
{

/// An event and its text, kept beyond the pump that delivered it.
struct ReceivedEvent
{
    gfx::platform::Event event{};
    std::string text{};
};

/// Pumps @p platform until an event satisfies @p wanted (or @p timeout passes); every event received
/// meanwhile is appended to @p received.
/// @return Whether a wanted event arrived.
template<class Predicate>
auto
pump_until(gfx::platform::Platform& platform,
           Predicate wanted,
           std::vector<ReceivedEvent>& received,
           std::chrono::milliseconds timeout = std::chrono::milliseconds{3000}) -> bool
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        bool found = false;
        const gfx::platform::EventSpan events = platform.wait_events_timeout(std::chrono::milliseconds{10});
        for (const gfx::platform::Event& event : events)
        {
            received.push_back(ReceivedEvent{.event = event, .text = std::string{events.text(event)}});
            found = found || wanted(event);
        }
        if (found)
        {
            return true;
        }
    }
    return false;
}

/// Pumps @p platform until an event of type @p type arrives; returns it (std::nullopt on timeout).
inline auto
pump_until_type(gfx::platform::Platform& platform,
                gfx::platform::EventType type,
                std::chrono::milliseconds timeout = std::chrono::milliseconds{3000}) -> std::optional<ReceivedEvent>
{
    std::vector<ReceivedEvent> received;
    if (!pump_until(platform, [type](const gfx::platform::Event& event) { return event.type() == type; }, received, timeout))
    {
        return std::nullopt;
    }
    for (const ReceivedEvent& event : received)
    {
        if (event.event.type() == type)
        {
            return event;
        }
    }
    return std::nullopt;
}

/// SDL's identifier of @p window (0 when SDL has no window with its native handle).
inline auto
sdl_window_identifier(const gfx::platform::Window& window) -> std::uint32_t
{
    // Match the operating-system window of every SDL window.
    const gfx::NativeWindowHandle native = window.native_handle();
    const gfx::Win32WindowHandle* const win32 = native.get_if<gfx::Win32WindowHandle>();
    int count = 0;
    SDL_Window** const windows = SDL_GetWindows(&count);
    std::uint32_t identifier = 0;
    for (int index = 0; windows != nullptr && index < count; ++index)
    {
        const void* const hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(windows[index]), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (win32 != nullptr && reinterpret_cast<std::uintptr_t>(hwnd) == win32->window)
        {
            identifier = SDL_GetWindowID(windows[index]);
        }
    }
    SDL_free(windows);
    return identifier;
}

/// Number of windows SDL holds (destroyed gfx windows stay until the next pump).
inline auto
sdl_window_count() -> int
{
    int count = 0;
    SDL_free(SDL_GetWindows(&count));
    return count;
}

} // namespace gfx::tests
