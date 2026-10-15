/// @file vulkan_backend.cpp
/// @brief Entry points of the Vulkan backend.
module;

#include <volk.h>

module gfx.backends.vulkan;

import std;
import gfx.interface;
import :common;
import :instance;

namespace gfx::vulkan
{

auto
unavailable_reason() -> std::string
{
    // The backend runs wherever the Vulkan loader can be loaded.
    const VkResult result = load_vulkan();
    if (result == VK_SUCCESS)
    {
        return {};
    }
    return std::format("the Vulkan loader (vulkan-1.dll on Windows, libvulkan.so.1 on Linux) could not be loaded ({}); "
                       "install a graphics driver with Vulkan support",
                       result_name(result));
}

auto
create_instance(const InstanceDescription& description) -> std::unique_ptr<Instance>
{
    return std::make_unique<VulkanInstance>(description);
}

} // namespace gfx::vulkan
