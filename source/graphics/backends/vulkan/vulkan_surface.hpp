#pragma once
/// @file vulkan_surface.hpp
/// @brief Window surfaces, behind plain functions (vulkan_surface.cpp, a plain translation unit): the
///        platform surface headers need <windows.h> on Windows (and X11, XCB or Wayland headers on Linux),
///        which cannot share a translation unit with modules. Include it after <volk.h>, which provides
///        the Vulkan types. Each function returns VK_ERROR_EXTENSION_NOT_PRESENT when the build or the
///        instance lacks that window system.

#include <vulkan/vulkan_core.h>

namespace gfx::vulkan::surface
{

/// @brief A surface for a Win32 window (VK_KHR_win32_surface).
VkResult create_win32(VkInstance instance,
                      PFN_vkGetInstanceProcAddr get_instance_procedure,
                      unsigned long long hinstance,
                      unsigned long long hwnd,
                      VkSurfaceKHR* surface) noexcept;

/// @brief A surface for an X11 window through Xlib (VK_KHR_xlib_surface).
VkResult create_xlib(VkInstance instance,
                     PFN_vkGetInstanceProcAddr get_instance_procedure,
                     unsigned long long display,
                     unsigned long long window,
                     VkSurfaceKHR* surface) noexcept;

/// @brief A surface for an X11 window through XCB (VK_KHR_xcb_surface).
VkResult create_xcb(VkInstance instance,
                    PFN_vkGetInstanceProcAddr get_instance_procedure,
                    unsigned long long connection,
                    unsigned int window,
                    VkSurfaceKHR* surface) noexcept;

/// @brief A surface for a Wayland surface (VK_KHR_wayland_surface).
VkResult create_wayland(VkInstance instance,
                        PFN_vkGetInstanceProcAddr get_instance_procedure,
                        unsigned long long display,
                        unsigned long long wayland_surface,
                        VkSurfaceKHR* surface) noexcept;

/// @brief A surface for a CAMetalLayer (VK_EXT_metal_surface).
VkResult create_metal(VkInstance instance,
                      PFN_vkGetInstanceProcAddr get_instance_procedure,
                      unsigned long long layer,
                      VkSurfaceKHR* surface) noexcept;

} // namespace gfx::vulkan::surface
