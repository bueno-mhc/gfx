// The implementation of the Vulkan Memory Allocator, compiled once. A plain translation unit (no
// modules): VMA is a header library. Functions come from volk; VMA fetches them through
// vkGetInstanceProcAddr and vkGetDeviceProcAddr (VmaVulkanFunctions, see VulkanDevice).
#include <volk.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
