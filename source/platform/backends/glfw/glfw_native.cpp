// GLFW's native access (glfw3native.h): a plain translation unit, because the Win32 variant includes
// <windows.h>, which cannot share a translation unit with module imports (toolchain defect 2). Only
// the windowing systems of the build's operating system are compiled.
#include "glfw_native.hpp"

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#else
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace gfx::platform::glfw
{

bool
win32_handles([[maybe_unused]] GLFWwindow* window,
              [[maybe_unused]] unsigned long long& instance,
              [[maybe_unused]] unsigned long long& hwnd) noexcept
{
#if defined(_WIN32)
    if (glfwGetPlatform() == GLFW_PLATFORM_WIN32)
    {
        const HWND native = glfwGetWin32Window(window);
        instance = static_cast<unsigned long long>(GetWindowLongPtrW(native, GWLP_HINSTANCE));
        hwnd = reinterpret_cast<unsigned long long>(native);
        return native != nullptr;
    }
#endif
    return false;
}

bool
x11_handles([[maybe_unused]] GLFWwindow* window,
            [[maybe_unused]] unsigned long long& display,
            [[maybe_unused]] unsigned long long& x11_window) noexcept
{
#if !defined(_WIN32) && !defined(__APPLE__)
    if (glfwGetPlatform() == GLFW_PLATFORM_X11)
    {
        display = reinterpret_cast<unsigned long long>(glfwGetX11Display());
        x11_window = static_cast<unsigned long long>(glfwGetX11Window(window));
        return true;
    }
#endif
    return false;
}

bool
wayland_handles([[maybe_unused]] GLFWwindow* window,
                [[maybe_unused]] unsigned long long& display,
                [[maybe_unused]] unsigned long long& surface) noexcept
{
#if !defined(_WIN32) && !defined(__APPLE__)
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
    {
        display = reinterpret_cast<unsigned long long>(glfwGetWaylandDisplay());
        surface = reinterpret_cast<unsigned long long>(glfwGetWaylandWindow(window));
        return true;
    }
#endif
    return false;
}

bool
cocoa_handles([[maybe_unused]] GLFWwindow* window,
              [[maybe_unused]] unsigned long long& ns_window) noexcept
{
#if defined(__APPLE__)
    if (glfwGetPlatform() == GLFW_PLATFORM_COCOA)
    {
        ns_window = reinterpret_cast<unsigned long long>(glfwGetCocoaWindow(window));
        return true;
    }
#endif
    return false;
}

} // namespace gfx::platform::glfw
