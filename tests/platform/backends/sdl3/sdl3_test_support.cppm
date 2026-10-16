// Test support for the SDL3 backend: SDL's identifier of a gfx window (tests inject SDL events for it
// with SDL_PushEvent) and SDL's window count, plus the helpers every backend test uses.
module;

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

export module gfx.tests.sdl3_support;

import std;
import gfx.platform;
export import gfx.tests.backend_support;

export namespace gfx::tests
{

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
