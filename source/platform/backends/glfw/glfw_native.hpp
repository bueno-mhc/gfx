/// @file glfw_native.hpp
/// @brief Bridge to GLFW's native access (glfw_native.cpp, a plain translation unit): glfw3native.h
///        includes <windows.h> on Windows, which cannot share a translation unit with module imports
///        (toolchain defect 2). No standard header here either, so module units may include it.
///        Each function returns whether GLFW runs on that windowing system, and then its handles.
#pragma once

struct GLFWwindow;

namespace gfx::platform::glfw
{

/// Win32: the HINSTANCE and HWND of @p window.
bool win32_handles(GLFWwindow* window,
                   unsigned long long& instance,
                   unsigned long long& hwnd) noexcept;

/// X11: the Display* and Window of @p window.
bool x11_handles(GLFWwindow* window,
                 unsigned long long& display,
                 unsigned long long& x11_window) noexcept;

/// Wayland: the wl_display* and wl_surface* of @p window.
bool wayland_handles(GLFWwindow* window,
                     unsigned long long& display,
                     unsigned long long& surface) noexcept;

/// Cocoa: the NSWindow* of @p window.
bool cocoa_handles(GLFWwindow* window,
                   unsigned long long& ns_window) noexcept;

} // namespace gfx::platform::glfw
