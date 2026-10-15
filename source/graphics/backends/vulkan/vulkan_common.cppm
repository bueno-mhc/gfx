/// @file vulkan_common.cppm
/// @brief Internal partition shared by every unit of the Vulkan backend: native result checks and the
///        process-wide loader (volk).
module;

#include <volk.h>

module gfx.backends.vulkan:common;

import std;
import gfx.enums;
import gfx.errors;

namespace gfx::vulkan
{

/// @brief Name of a VkResult, e.g. "VK_ERROR_DEVICE_LOST".
[[nodiscard]] auto result_name(VkResult result) noexcept -> std::string_view;

/// @brief Throws the gfx error describing the failed native call @p function:
///        OutOfMemoryError, DeviceLostError or BackendApiError.
[[noreturn]] void throw_result(VkResult result,
                               std::string_view operation,
                               std::string_view function,
                               std::source_location location);

/// @brief Throws the error describing @p result when it is a failure (negative); success codes pass.
/// @param result    Result of the native call.
/// @param operation Operation of the abstraction, e.g. "PhysicalDevice::create_device".
/// @param function  Native function, e.g. "vkCreateDevice".
/// @param location  Where the call is made (defaults to the caller).
inline void
check(VkResult result,
      std::string_view operation,
      std::string_view function,
      std::source_location location = std::source_location::current())
{
    if (result < VK_SUCCESS) [[unlikely]]
    {
        throw_result(result, operation, function, location);
    }
}

/// @brief Loads the Vulkan loader once per process (thread-safe).
/// @return The failure of the load, or VK_SUCCESS.
[[nodiscard]] auto load_vulkan() noexcept -> VkResult;

/// @brief Makes the global instance-level function pointers point at @p instance's functions.
///        The loader's trampolines dispatch on the object, so pointers loaded for one instance serve
///        every instance; loading is serialized because the pointers are process-wide.
void load_instance_functions(VkInstance instance);

} // namespace gfx::vulkan
