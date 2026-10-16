// Window surfaces through the platform surface extensions: a plain translation unit, because their
// headers need <windows.h> (or X11, XCB and Wayland headers), which cannot share a translation unit with
// modules. Functions are loaded through vkGetInstanceProcAddr (volk loads only portable functions).
#if defined(_WIN32)
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#else
#if __has_include(<X11/Xlib.h>)
#include <X11/Xlib.h>
#define VK_USE_PLATFORM_XLIB_KHR
#endif
#if __has_include(<xcb/xcb.h>)
#include <xcb/xcb.h>
#define VK_USE_PLATFORM_XCB_KHR
#endif
#if __has_include(<wayland-client.h>)
#include <wayland-client.h>
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#endif
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "vulkan_surface.hpp"

namespace gfx::vulkan::surface
{

namespace
{

/// The instance function @p name, as @p Function (null when the instance lacks it).
template<class Function>
Function
load(VkInstance instance,
     PFN_vkGetInstanceProcAddr get_instance_procedure,
     const char* name) noexcept
{
    return reinterpret_cast<Function>(get_instance_procedure(instance, name));
}

} // namespace

VkResult
create_win32([[maybe_unused]] VkInstance instance,
             [[maybe_unused]] PFN_vkGetInstanceProcAddr get_instance_procedure,
             [[maybe_unused]] unsigned long long hinstance,
             [[maybe_unused]] unsigned long long hwnd,
             [[maybe_unused]] VkSurfaceKHR* surface) noexcept
{
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    const auto create = load<PFN_vkCreateWin32SurfaceKHR>(instance, get_instance_procedure, "vkCreateWin32SurfaceKHR");
    if (create == nullptr)
    {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    const VkWin32SurfaceCreateInfoKHR information{
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .hinstance = reinterpret_cast<HINSTANCE>(hinstance),
        .hwnd = reinterpret_cast<HWND>(hwnd),
    };
    return create(instance, &information, nullptr, surface);
#else
    return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
}

VkResult
create_xlib([[maybe_unused]] VkInstance instance,
            [[maybe_unused]] PFN_vkGetInstanceProcAddr get_instance_procedure,
            [[maybe_unused]] unsigned long long display,
            [[maybe_unused]] unsigned long long window,
            [[maybe_unused]] VkSurfaceKHR* surface) noexcept
{
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    const auto create = load<PFN_vkCreateXlibSurfaceKHR>(instance, get_instance_procedure, "vkCreateXlibSurfaceKHR");
    if (create == nullptr)
    {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    const VkXlibSurfaceCreateInfoKHR information{
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .dpy = reinterpret_cast<Display*>(display),
        .window = static_cast<Window>(window),
    };
    return create(instance, &information, nullptr, surface);
#else
    return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
}

VkResult
create_xcb([[maybe_unused]] VkInstance instance,
           [[maybe_unused]] PFN_vkGetInstanceProcAddr get_instance_procedure,
           [[maybe_unused]] unsigned long long connection,
           [[maybe_unused]] unsigned int window,
           [[maybe_unused]] VkSurfaceKHR* surface) noexcept
{
#if defined(VK_USE_PLATFORM_XCB_KHR)
    const auto create = load<PFN_vkCreateXcbSurfaceKHR>(instance, get_instance_procedure, "vkCreateXcbSurfaceKHR");
    if (create == nullptr)
    {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    const VkXcbSurfaceCreateInfoKHR information{
        .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .connection = reinterpret_cast<xcb_connection_t*>(connection),
        .window = static_cast<xcb_window_t>(window),
    };
    return create(instance, &information, nullptr, surface);
#else
    return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
}

VkResult
create_wayland([[maybe_unused]] VkInstance instance,
               [[maybe_unused]] PFN_vkGetInstanceProcAddr get_instance_procedure,
               [[maybe_unused]] unsigned long long display,
               [[maybe_unused]] unsigned long long wayland_surface,
               [[maybe_unused]] VkSurfaceKHR* surface) noexcept
{
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    const auto create = load<PFN_vkCreateWaylandSurfaceKHR>(instance, get_instance_procedure, "vkCreateWaylandSurfaceKHR");
    if (create == nullptr)
    {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    const VkWaylandSurfaceCreateInfoKHR information{
        .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .display = reinterpret_cast<wl_display*>(display),
        .surface = reinterpret_cast<wl_surface*>(wayland_surface),
    };
    return create(instance, &information, nullptr, surface);
#else
    return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
}

VkResult
create_metal([[maybe_unused]] VkInstance instance,
             [[maybe_unused]] PFN_vkGetInstanceProcAddr get_instance_procedure,
             [[maybe_unused]] unsigned long long layer,
             [[maybe_unused]] VkSurfaceKHR* surface) noexcept
{
#if defined(VK_USE_PLATFORM_METAL_EXT)
    const auto create = load<PFN_vkCreateMetalSurfaceEXT>(instance, get_instance_procedure, "vkCreateMetalSurfaceEXT");
    if (create == nullptr)
    {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    const VkMetalSurfaceCreateInfoEXT information{
        .sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .pLayer = reinterpret_cast<const CAMetalLayer*>(layer),
    };
    return create(instance, &information, nullptr, surface);
#else
    return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
}

} // namespace gfx::vulkan::surface
