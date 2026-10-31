/// @file vulkan_instance.cpp
/// @brief VulkanInstance, VulkanPhysicalDevice and window surfaces.
module;

#include <volk.h>

#include "vulkan_surface.hpp"

module gfx.backends.vulkan;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import gfx.interface;
import :common;
import :translation;
import :instance;

namespace gfx::vulkan
{

namespace
{

constexpr std::string_view VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";

/// @brief The surface extension of each window system Vulkan presents to (NUL-terminated names).
constexpr std::array<std::pair<NativeWindowKind, std::string_view>, 5> SURFACE_EXTENSIONS{{
    {NativeWindowKind::WINDOWS, "VK_KHR_win32_surface"},
    {NativeWindowKind::XLIB, "VK_KHR_xlib_surface"},
    {NativeWindowKind::XCB, "VK_KHR_xcb_surface"},
    {NativeWindowKind::WAYLAND, "VK_KHR_wayland_surface"},
    {NativeWindowKind::COCOA, "VK_EXT_metal_surface"},
}};

/// @brief The surface extension of window system @p kind (empty when Vulkan cannot present to it).
auto
surface_extension(NativeWindowKind kind) noexcept -> std::string_view
{
    for (const auto& [system, extension] : SURFACE_EXTENSIONS)
    {
        if (system == kind)
        {
            return extension;
        }
    }
    return {};
}

/// @brief Appends @p structure to a pNext chain whose last pNext is @p tail, and returns the new tail.
template<class Structure>
auto
append(void** tail,
       Structure& structure) noexcept -> void**
{
    *tail = &structure;
    return &structure.pNext;
}

/// @brief Version of a driver, decoded with the vendor's own convention.
auto
driver_version(std::uint32_t vendor,
               std::uint32_t version) noexcept -> Version
{
    // NVIDIA: 10.8.8.6 bits.
    if (vendor == 0x10DE)
    {
        return {.major = version >> 22U, .minor = (version >> 14U) & 0xFFU, .patch = (version >> 6U) & 0xFFU};
    }

    // Intel on Windows: 18.14 bits.
    if (vendor == 0x8086)
    {
        return {.major = version >> 14U, .minor = version & 0x3FFFU, .patch = 0};
    }
    return {.major = VK_API_VERSION_MAJOR(version), .minor = VK_API_VERSION_MINOR(version), .patch = VK_API_VERSION_PATCH(version)};
}

/// @brief The application's debug callback, called by the validation layers (on any thread).
VKAPI_ATTR VkBool32 VKAPI_CALL
debug_messenger_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                         VkDebugUtilsMessageTypeFlagsEXT types,
                         const VkDebugUtilsMessengerCallbackDataEXT* data,
                         void* user_data)
{
    // Severity and category in gfx terms.
    DebugMessageSeverity gfx_severity = DebugMessageSeverity::VERBOSE;
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
    {
        gfx_severity = DebugMessageSeverity::CRITICAL;
    }
    else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
    {
        gfx_severity = DebugMessageSeverity::WARNING;
    }
    else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0)
    {
        gfx_severity = DebugMessageSeverity::INFORMATION;
    }
    DebugMessageType type = DebugMessageType::GENERAL;
    if ((types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0)
    {
        type = DebugMessageType::VALIDATION;
    }
    else if ((types & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) != 0)
    {
        type = DebugMessageType::PERFORMANCE;
    }

    // The callback must not throw into the driver.
    try
    {
        static_cast<const VulkanInstance*>(user_data)->report(gfx_severity, type,
                                                              data->pMessage != nullptr ? data->pMessage : "");
    }
    catch (...)
    {
    }
    return VK_FALSE;
}

} // namespace

// ---------------------------------------------------------------------------------------------------
// VulkanPhysicalDevice
// ---------------------------------------------------------------------------------------------------

VulkanPhysicalDevice::VulkanPhysicalDevice(VulkanInstance& instance,
                                           VkPhysicalDevice physical_device)
    : PhysicalDevice{}
    , m_instance{instance}
    , m_physical_device{physical_device}
{
    set_validation_level(instance.validation_level());
    query_capabilities();
    fill_properties();
    m_debug_name = m_properties.name;
}

void
VulkanPhysicalDevice::query_capabilities()
{
    PhysicalDeviceCapabilities& capabilities = m_capabilities;

    // Extensions, sorted for lookups.
    std::uint32_t count = 0;
    check(vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &count, nullptr), "create_instance",
          "vkEnumerateDeviceExtensionProperties");
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &count, extensions.data()), "create_instance",
          "vkEnumerateDeviceExtensionProperties");
    for (const VkExtensionProperties& extension : extensions)
    {
        capabilities.extensions.emplace_back(extension.extensionName);
    }
    std::ranges::sort(capabilities.extensions);

    // Queue families and memory.
    vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &count, nullptr);
    capabilities.queue_families.resize(count);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &count, capabilities.queue_families.data());
    vkGetPhysicalDeviceMemoryProperties(m_physical_device, &capabilities.memory_properties);

    // Core properties; devices older than Vulkan 1.3 cannot run the backend and report nothing more.
    vkGetPhysicalDeviceProperties(m_physical_device, &capabilities.properties);
    if (!capabilities.has_vulkan_1_3())
    {
        vkGetPhysicalDeviceFeatures(m_physical_device, &capabilities.features);
        return;
    }

    // Properties of Vulkan 1.1 to 1.3 and of the optional extensions the device offers.
    VkPhysicalDeviceProperties2 properties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = nullptr, .properties = {}};
    void** tail = append(&properties.pNext, capabilities.properties11);
    tail = append(tail, capabilities.properties12);
    tail = append(tail, capabilities.properties13);
    if (capabilities.has_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME))
    {
        tail = append(tail, capabilities.acceleration_structure_properties);
    }
    if (capabilities.has_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME))
    {
        tail = append(tail, capabilities.ray_tracing_properties);
    }
    if (capabilities.has_extension(VK_EXT_MESH_SHADER_EXTENSION_NAME))
    {
        tail = append(tail, capabilities.mesh_shader_properties);
    }
    if (capabilities.has_extension(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME))
    {
        tail = append(tail, capabilities.shading_rate_properties);
    }
    vkGetPhysicalDeviceProperties2(m_physical_device, &properties);
    capabilities.properties = properties.properties;

    // Features, likewise.
    VkPhysicalDeviceFeatures2 features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = nullptr, .features = {}};
    tail = append(&features.pNext, capabilities.features11);
    tail = append(tail, capabilities.features12);
    tail = append(tail, capabilities.features13);
    if (capabilities.has_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME))
    {
        tail = append(tail, capabilities.acceleration_structure_features);
    }
    if (capabilities.has_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME))
    {
        tail = append(tail, capabilities.ray_tracing_features);
    }
    if (capabilities.has_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME))
    {
        tail = append(tail, capabilities.ray_query_features);
    }
    if (capabilities.has_extension(VK_EXT_MESH_SHADER_EXTENSION_NAME))
    {
        tail = append(tail, capabilities.mesh_shader_features);
    }
    if (capabilities.has_extension(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME))
    {
        tail = append(tail, capabilities.shading_rate_features);
    }
    if (capabilities.has_extension(VK_EXT_DEVICE_FAULT_EXTENSION_NAME))
    {
        tail = append(tail, capabilities.fault_features);
    }
    if (capabilities.has_extension(VK_KHR_PRESENT_ID_EXTENSION_NAME))
    {
        tail = append(tail, capabilities.present_id_features);
    }
    if (capabilities.has_extension(VK_KHR_PRESENT_WAIT_EXTENSION_NAME))
    {
        tail = append(tail, capabilities.present_wait_features);
    }
    vkGetPhysicalDeviceFeatures2(m_physical_device, &features);
    capabilities.features = features.features;

    // The chains served the query only.
    capabilities.properties11.pNext = nullptr;
    capabilities.properties12.pNext = nullptr;
    capabilities.properties13.pNext = nullptr;
    capabilities.acceleration_structure_properties.pNext = nullptr;
    capabilities.ray_tracing_properties.pNext = nullptr;
    capabilities.mesh_shader_properties.pNext = nullptr;
    capabilities.shading_rate_properties.pNext = nullptr;
    capabilities.features11.pNext = nullptr;
    capabilities.features12.pNext = nullptr;
    capabilities.features13.pNext = nullptr;
    capabilities.acceleration_structure_features.pNext = nullptr;
    capabilities.ray_tracing_features.pNext = nullptr;
    capabilities.ray_query_features.pNext = nullptr;
    capabilities.mesh_shader_features.pNext = nullptr;
    capabilities.shading_rate_features.pNext = nullptr;
    capabilities.fault_features.pNext = nullptr;
    capabilities.present_id_features.pNext = nullptr;
    capabilities.present_wait_features.pNext = nullptr;
}

void
VulkanPhysicalDevice::fill_properties()
{
    const PhysicalDeviceCapabilities& capabilities = m_capabilities;
    const VkPhysicalDeviceProperties& native = capabilities.properties;
    const VkPhysicalDeviceLimits& limits = native.limits;

    // Identity and versions.
    m_properties.name = native.deviceName;
    m_properties.type = from_vulkan(native.deviceType);
    m_properties.vendor_identifier = native.vendorID;
    m_properties.device_identifier = native.deviceID;
    m_properties.api_version = {.major = VK_API_VERSION_MAJOR(native.apiVersion),
                                .minor = VK_API_VERSION_MINOR(native.apiVersion),
                                .patch = VK_API_VERSION_PATCH(native.apiVersion)};
    m_properties.driver_version = driver_version(native.vendorID, native.driverVersion);

    // Limits of the core API.
    DeviceLimits& gfx_limits = m_properties.limits;
    gfx_limits.maximum_image_dimension = limits.maxImageDimension2D;
    gfx_limits.maximum_image_array_layers = limits.maxImageArrayLayers;
    gfx_limits.maximum_push_constant_size = limits.maxPushConstantsSize;
    gfx_limits.maximum_bound_descriptor_sets = limits.maxBoundDescriptorSets;
    gfx_limits.maximum_color_attachments = limits.maxColorAttachments;
    std::ranges::copy(limits.maxComputeWorkGroupCount, gfx_limits.maximum_compute_workgroup_count.begin());
    std::ranges::copy(limits.maxComputeWorkGroupSize, gfx_limits.maximum_compute_workgroup_size.begin());
    gfx_limits.maximum_compute_workgroup_invocations = limits.maxComputeWorkGroupInvocations;
    gfx_limits.maximum_sampler_anisotropy = limits.maxSamplerAnisotropy;
    gfx_limits.minimum_uniform_buffer_offset_alignment = limits.minUniformBufferOffsetAlignment;
    gfx_limits.minimum_storage_buffer_offset_alignment = limits.minStorageBufferOffsetAlignment;
    gfx_limits.timestamp_period_nanoseconds = limits.timestampPeriod;
    gfx_limits.color_sample_counts = from_vulkan(limits.framebufferColorSampleCounts);
    gfx_limits.depth_sample_counts = from_vulkan(limits.framebufferDepthSampleCounts);
    gfx_limits.minimum_texel_buffer_offset_alignment = limits.minTexelBufferOffsetAlignment;
    gfx_limits.non_coherent_atom_size = limits.nonCoherentAtomSize;
    gfx_limits.maximum_draw_indirect_count = limits.maxDrawIndirectCount;
    gfx_limits.maximum_storage_buffer_range = limits.maxStorageBufferRange;
    gfx_limits.maximum_compute_shared_memory_size = limits.maxComputeSharedMemorySize;

    // Limits of Vulkan 1.1 to 1.3 (devices without them cannot run the backend anyway).
    if (capabilities.has_vulkan_1_3())
    {
        gfx_limits.maximum_memory_allocation_size = capabilities.properties11.maxMemoryAllocationSize;
        gfx_limits.maximum_multiview_view_count = capabilities.properties11.maxMultiviewViewCount;
        gfx_limits.minimum_subgroup_size = capabilities.properties13.minSubgroupSize;
        gfx_limits.maximum_subgroup_size = capabilities.properties13.maxSubgroupSize;
        if ((capabilities.properties11.subgroupSupportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0)
        {
            gfx_limits.subgroup_operations = from_vulkan_subgroup_operations(capabilities.properties11.subgroupSupportedOperations);
        }

        // Bindless arrays are visible to every stage: the per-stage limits apply too.
        const VkPhysicalDeviceVulkan12Properties& properties12 = capabilities.properties12;
        const auto bindless = [&gfx_limits](BindlessResourceKind kind, std::uint32_t set_limit, std::uint32_t stage_limit)
        {
            gfx_limits.maximum_bindless_resources[static_cast<std::size_t>(std::to_underlying(kind))] = std::min(set_limit, stage_limit);
        };
        bindless(BindlessResourceKind::SAMPLED_IMAGE, properties12.maxDescriptorSetUpdateAfterBindSampledImages,
                 properties12.maxPerStageDescriptorUpdateAfterBindSampledImages);
        bindless(BindlessResourceKind::STORAGE_IMAGE, properties12.maxDescriptorSetUpdateAfterBindStorageImages,
                 properties12.maxPerStageDescriptorUpdateAfterBindStorageImages);
        bindless(BindlessResourceKind::SAMPLER, properties12.maxDescriptorSetUpdateAfterBindSamplers,
                 properties12.maxPerStageDescriptorUpdateAfterBindSamplers);
        bindless(BindlessResourceKind::STORAGE_BUFFER, properties12.maxDescriptorSetUpdateAfterBindStorageBuffers,
                 properties12.maxPerStageDescriptorUpdateAfterBindStorageBuffers);
        if (capabilities.has_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME))
        {
            const VkPhysicalDeviceAccelerationStructurePropertiesKHR& structures = capabilities.acceleration_structure_properties;
            bindless(BindlessResourceKind::ACCELERATION_STRUCTURE, structures.maxDescriptorSetUpdateAfterBindAccelerationStructures,
                     structures.maxPerStageDescriptorUpdateAfterBindAccelerationStructures);
        }
    }

    // Ray tracing and mesh shading.
    gfx_limits.shader_group_handle_size = capabilities.ray_tracing_properties.shaderGroupHandleSize;
    gfx_limits.shader_group_handle_alignment = capabilities.ray_tracing_properties.shaderGroupHandleAlignment;
    gfx_limits.shader_group_base_alignment = capabilities.ray_tracing_properties.shaderGroupBaseAlignment;
    gfx_limits.maximum_ray_recursion_depth = capabilities.ray_tracing_properties.maxRayRecursionDepth;
    std::ranges::copy(capabilities.mesh_shader_properties.maxTaskWorkGroupSize, gfx_limits.maximum_task_workgroup_size.begin());
    std::ranges::copy(capabilities.mesh_shader_properties.maxMeshWorkGroupSize, gfx_limits.maximum_mesh_workgroup_size.begin());
    gfx_limits.maximum_mesh_output_vertices = capabilities.mesh_shader_properties.maxMeshOutputVertices;
    gfx_limits.maximum_mesh_output_primitives = capabilities.mesh_shader_properties.maxMeshOutputPrimitives;

    // Queue families.
    for (const VkQueueFamilyProperties& family : capabilities.queue_families)
    {
        m_queue_families.push_back({
            .capabilities = from_vulkan_queue_flags(family.queueFlags),
            .queue_count = family.queueCount,
            .timestamp_valid_bits = family.timestampValidBits,
        });
    }
}

auto
VulkanPhysicalDevice::backend() const noexcept -> BackendKind
{
    return BackendKind::VULKAN;
}

auto
VulkanPhysicalDevice::debug_name() const noexcept -> std::string_view
{
    return m_debug_name;
}

void
VulkanPhysicalDevice::set_debug_name(std::string_view name)
{
    m_debug_name = name;
}

auto
VulkanPhysicalDevice::properties() const noexcept -> const PhysicalDeviceProperties&
{
    return m_properties;
}

auto
VulkanPhysicalDevice::queue_families() const noexcept -> std::span<const QueueFamilyProperties>
{
    return m_queue_families;
}

auto
VulkanPhysicalDevice::supports(DeviceFeature feature) const noexcept -> bool
{
    const PhysicalDeviceCapabilities& capabilities = m_capabilities;
    const VkPhysicalDeviceFeatures& features = capabilities.features;
    const VkPhysicalDeviceVulkan11Features& features11 = capabilities.features11;
    const VkPhysicalDeviceVulkan12Features& features12 = capabilities.features12;
    const VkPhysicalDeviceVulkan13Features& features13 = capabilities.features13;

    // Every feature builds on the Vulkan 1.3 baseline.
    if (!capabilities.has_vulkan_1_3())
    {
        return false;
    }
    const auto family_with = [&capabilities](VkQueueFlags required, VkQueueFlags excluded)
    {
        return std::ranges::any_of(capabilities.queue_families, [&](const VkQueueFamilyProperties& family)
        {
            return (family.queueFlags & required) == required && (family.queueFlags & excluded) == 0 && family.queueCount > 0;
        });
    };
    const bool acceleration_structures = capabilities.has_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
                                         capabilities.has_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) &&
                                         capabilities.acceleration_structure_features.accelerationStructure == VK_TRUE &&
                                         features12.bufferDeviceAddress == VK_TRUE;

    switch (feature)
    {
        case DeviceFeature::TIMELINE_SEMAPHORE: 
            return features12.timelineSemaphore == VK_TRUE;
        case DeviceFeature::EVENTS: 
            return true;
        case DeviceFeature::DYNAMIC_RENDERING: 
            return features13.dynamicRendering == VK_TRUE && features13.synchronization2 == VK_TRUE;
        case DeviceFeature::DESCRIPTOR_INDEXING:
            return features12.descriptorIndexing == VK_TRUE && features12.runtimeDescriptorArray == VK_TRUE &&
                features12.descriptorBindingPartiallyBound == VK_TRUE && features12.descriptorBindingVariableDescriptorCount == VK_TRUE &&
                features12.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE &&
                features12.descriptorBindingStorageImageUpdateAfterBind == VK_TRUE &&
                features12.descriptorBindingStorageBufferUpdateAfterBind == VK_TRUE &&
                features12.descriptorBindingUpdateUnusedWhilePending == VK_TRUE &&
                features12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE &&
                features12.shaderStorageImageArrayNonUniformIndexing == VK_TRUE &&
                features12.shaderStorageBufferArrayNonUniformIndexing == VK_TRUE;
        case DeviceFeature::BUFFER_DEVICE_ADDRESS: 
            return features12.bufferDeviceAddress == VK_TRUE;
        case DeviceFeature::ASYNCHRONOUS_COMPUTE_QUEUE: 
            return family_with(VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT);
        case DeviceFeature::DEDICATED_TRANSFER_QUEUE:
            return family_with(VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
        case DeviceFeature::SPARSE_BINDING:
            return features.sparseBinding == VK_TRUE && features.sparseResidencyBuffer == VK_TRUE &&
                features.sparseResidencyImage2D == VK_TRUE && family_with(VK_QUEUE_SPARSE_BINDING_BIT, 0);
        case DeviceFeature::SAMPLER_ANISOTROPY: 
            return features.samplerAnisotropy == VK_TRUE;
        case DeviceFeature::MULTI_DRAW_INDIRECT: 
            return features.multiDrawIndirect == VK_TRUE;
        case DeviceFeature::DRAW_INDIRECT_COUNT: 
            return features12.drawIndirectCount == VK_TRUE;
        case DeviceFeature::GEOMETRY_SHADER: 
            return features.geometryShader == VK_TRUE;
        case DeviceFeature::TESSELLATION_SHADER: 
            return features.tessellationShader == VK_TRUE;
        case DeviceFeature::MESH_SHADER:
            return capabilities.has_extension(VK_EXT_MESH_SHADER_EXTENSION_NAME) &&
                capabilities.mesh_shader_features.meshShader == VK_TRUE && capabilities.mesh_shader_features.taskShader == VK_TRUE;
        case DeviceFeature::RAY_TRACING:
            return acceleration_structures && capabilities.has_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
                capabilities.ray_tracing_features.rayTracingPipeline == VK_TRUE;
        case DeviceFeature::SHADER_FLOAT16: 
            return features12.shaderFloat16 == VK_TRUE;
        case DeviceFeature::SHADER_FLOAT64: 
            return features.shaderFloat64 == VK_TRUE;
        case DeviceFeature::SHADER_INTEGER16: 
            return features.shaderInt16 == VK_TRUE;
        case DeviceFeature::SHADER_INTEGER64: 
            return features.shaderInt64 == VK_TRUE;
        case DeviceFeature::FILL_MODE_NON_SOLID: 
            return features.fillModeNonSolid == VK_TRUE;
        case DeviceFeature::WIDE_LINES: 
            return features.wideLines == VK_TRUE;
        case DeviceFeature::DEPTH_CLAMP: 
            return features.depthClamp == VK_TRUE;
        case DeviceFeature::INDEPENDENT_BLEND: 
            return features.independentBlend == VK_TRUE;
        case DeviceFeature::TIMESTAMP_QUERY: 
            return capabilities.properties.limits.timestampComputeAndGraphics == VK_TRUE;
        case DeviceFeature::PIPELINE_STATISTICS_QUERY: 
            return features.pipelineStatisticsQuery == VK_TRUE;
        case DeviceFeature::TEXTURE_COMPRESSION_BC: 
            return features.textureCompressionBC == VK_TRUE;
        case DeviceFeature::TEXTURE_COMPRESSION_ETC2: 
            return features.textureCompressionETC2 == VK_TRUE;
        case DeviceFeature::TEXTURE_COMPRESSION_ASTC: 
            return features.textureCompressionASTC_LDR == VK_TRUE;
        case DeviceFeature::PRESENTATION:
            return m_instance.has_surfaces() && capabilities.has_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        case DeviceFeature::EXTENDED_DYNAMIC_STATE: 
            return true;
        case DeviceFeature::DEPTH_BOUNDS: 
            return features.depthBounds == VK_TRUE;
        case DeviceFeature::MULTIVIEW: 
            return features11.multiview == VK_TRUE;
        case DeviceFeature::VARIABLE_RATE_SHADING:
            return capabilities.has_extension(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME) &&
                capabilities.shading_rate_features.pipelineFragmentShadingRate == VK_TRUE &&
                capabilities.shading_rate_features.attachmentFragmentShadingRate == VK_TRUE;
        case DeviceFeature::CONSERVATIVE_RASTERIZATION:
            return capabilities.has_extension(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);
        case DeviceFeature::RAY_QUERY:
            return acceleration_structures && capabilities.has_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
                capabilities.ray_query_features.rayQuery == VK_TRUE;
        case DeviceFeature::SAMPLER_FILTER_MINMAX: 
            return features12.samplerFilterMinmax == VK_TRUE;
        case DeviceFeature::MEMORY_BUDGET: 
            return capabilities.has_extension(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

        // Breadcrumbs use NVIDIA's diagnostic checkpoints (AMD's buffer markers are not implemented yet).
        case DeviceFeature::GPU_CRASH_MARKERS:
            return capabilities.has_extension(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);

        // Waiting for presentation needs identifiers on presents (VK_KHR_present_id) and waits on them.
        case DeviceFeature::FRAME_LATENCY_WAIT:
            return supports(DeviceFeature::PRESENTATION) && capabilities.has_extension(VK_KHR_PRESENT_ID_EXTENSION_NAME) &&
                capabilities.has_extension(VK_KHR_PRESENT_WAIT_EXTENSION_NAME) &&
                capabilities.present_id_features.presentId == VK_TRUE && capabilities.present_wait_features.presentWait == VK_TRUE;
        case DeviceFeature::SHADER_INTEGER64_ATOMICS:
            return features12.shaderBufferInt64Atomics == VK_TRUE && features12.shaderSharedInt64Atomics == VK_TRUE;
        case DeviceFeature::SHADER_MEMORY_MODEL:
            return features12.vulkanMemoryModel == VK_TRUE && features12.vulkanMemoryModelDeviceScope == VK_TRUE;
    }
    return false;
}

auto
VulkanPhysicalDevice::format_support(Format format) const -> FormatSupport
{
    // Format features of both tilings and of buffers (VkFormatProperties3: 64-bit feature flags).
    VkFormatProperties3 properties3{.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3,
                                    .pNext = nullptr,
                                    .linearTilingFeatures = 0,
                                    .optimalTilingFeatures = 0,
                                    .bufferFeatures = 0};
    VkFormatProperties2 properties{.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &properties3, .formatProperties = {}};
    if (format != Format::UNDEFINED)
    {
        vkGetPhysicalDeviceFormatProperties2(m_physical_device, to_vulkan(format), &properties);
    }
    return {
        .optimal_tiling_usage = image_usage_of(properties3.optimalTilingFeatures),
        .linear_tiling_usage = image_usage_of(properties3.linearTilingFeatures),
        .buffer_usage = buffer_usage_of(properties3.bufferFeatures),
    };
}

auto
VulkanPhysicalDevice::surface_support(const NativeWindowHandle& window) const -> SurfaceSupport
{
    constexpr std::string_view OPERATION = "PhysicalDevice::surface_support";

    // Presentation needs VK_KHR_swapchain on the device.
    if (!supports(DeviceFeature::PRESENTATION))
    {
        throw_unsupported(OPERATION, "presentation",
                          std::format("'{}' cannot present: {}", m_properties.name,
                                      m_instance.has_surfaces() ? "the device lacks VK_KHR_swapchain"
                                                                : "the Vulkan loader offers no VK_KHR_surface"),
                          "render offscreen, or choose a physical device whose supports(DeviceFeature::PRESENTATION) is true");
    }

    // A temporary surface for the window, and a queue family that presents to it.
    const WindowSurface surface{m_instance, window, OPERATION};
    bool presentable = false;
    for (std::uint32_t family = 0; family < m_capabilities.queue_families.size() && !presentable; ++family)
    {
        presentable = can_present(family, surface.native(), OPERATION);
    }
    if (!presentable)
    {
        throw_unsupported(OPERATION, "presentation to this window",
                          std::format("no queue family of '{}' can present to the {} window", m_properties.name, window.kind()),
                          "choose the physical device that drives the window's display");
    }
    return surface_support(surface.native(), OPERATION);
}

auto
VulkanPhysicalDevice::surface_support(VkSurfaceKHR surface,
                                      std::string_view operation) const -> SurfaceSupport
{
    SurfaceSupport support;

    // Image counts, size (none when the swapchain chooses it, as on Wayland) and usages.
    VkSurfaceCapabilitiesKHR capabilities{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device, surface, &capabilities), operation,
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    support.minimum_image_count = capabilities.minImageCount;
    support.maximum_image_count = capabilities.maxImageCount;
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
    {
        support.current_extent = {.width = capabilities.currentExtent.width, .height = capabilities.currentExtent.height};
    }
    else
    {
        support.current_extent = {.width = 0, .height = 0};
    }
    support.supported_usage = from_vulkan_image_usage(capabilities.supportedUsageFlags);

    // Formats and color spaces gfx names.
    std::uint32_t count = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, surface, &count, nullptr), operation,
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
    std::vector<VkSurfaceFormatKHR> formats(count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, surface, &count, formats.data()), operation,
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
    for (const VkSurfaceFormatKHR& native : std::span{formats}.first(count))
    {
        const Format format = from_vulkan(native.format);
        const std::optional<ColorSpace> color_space = from_vulkan(native.colorSpace);
        if (format != Format::UNDEFINED && color_space)
        {
            support.formats.push_back({.format = format, .color_space = *color_space});
        }
    }

    // Presentation modes gfx names.
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical_device, surface, &count, nullptr), operation,
          "vkGetPhysicalDeviceSurfacePresentModesKHR");
    std::vector<VkPresentModeKHR> modes(count);
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical_device, surface, &count, modes.data()), operation,
          "vkGetPhysicalDeviceSurfacePresentModesKHR");
    for (const VkPresentModeKHR native : std::span{modes}.first(count))
    {
        if (const std::optional<PresentMode> mode = from_vulkan(native))
        {
            support.present_modes.push_back(*mode);
        }
    }
    return support;
}

auto
VulkanPhysicalDevice::can_present(std::uint32_t family,
                                  VkSurfaceKHR surface,
                                  std::string_view operation) const -> bool
{
    VkBool32 supported = VK_FALSE;
    check(vkGetPhysicalDeviceSurfaceSupportKHR(m_physical_device, family, surface, &supported), operation,
          "vkGetPhysicalDeviceSurfaceSupportKHR");
    return supported == VK_TRUE;
}

// ---------------------------------------------------------------------------------------------------
// VulkanInstance
// ---------------------------------------------------------------------------------------------------

VulkanInstance::VulkanInstance(const InstanceDescription& description)
    : Instance{}
    , m_reported_severities{description.debug_message_severities}
    , m_debug_callback{description.debug_callback}
    , m_debug_name{"Vulkan instance"}
{
    set_validation_level(description.validation_level);

    // The loader, and a Vulkan 1.3 implementation behind it.
    check(load_vulkan(), "create_instance", "volkInitialize");
    std::uint32_t loader_version = VK_API_VERSION_1_0;
    check(vkEnumerateInstanceVersion(&loader_version), "create_instance", "vkEnumerateInstanceVersion");
    if (loader_version < VK_API_VERSION_1_3)
    {
        throw_unsupported("create_instance", "Vulkan 1.3",
                          std::format("the Vulkan loader implements Vulkan {}.{}, the backend needs 1.3",
                                      VK_API_VERSION_MAJOR(loader_version), VK_API_VERSION_MINOR(loader_version)),
                          "update the graphics driver (or the Vulkan runtime)");
    }

    // Layers: the validation layers when requested.
    std::uint32_t count = 0;
    check(vkEnumerateInstanceLayerProperties(&count, nullptr), "create_instance", "vkEnumerateInstanceLayerProperties");
    std::vector<VkLayerProperties> layers(count);
    check(vkEnumerateInstanceLayerProperties(&count, layers.data()), "create_instance", "vkEnumerateInstanceLayerProperties");
    std::vector<const char*> enabled_layers;
    if (description.enable_validation)
    {
        const bool available = std::ranges::any_of(layers, [](const VkLayerProperties& layer)
        {
            return VALIDATION_LAYER == layer.layerName;
        });
        if (!available)
        {
            throw_unsupported("create_instance", "Vulkan validation layers",
                              std::format("{} is not installed (InstanceDescription::enable_validation is set)", VALIDATION_LAYER),
                              "install the Vulkan SDK (or the MSYS2 package vulkan-validation-layers and point VK_ADD_LAYER_PATH "
                              "at its directory), or create the instance without enable_validation");
        }
        enabled_layers.push_back(VALIDATION_LAYER.data());
        m_validation_layers = true;
    }

    // Extensions: debug utils (names, labels, messages) and portability enumeration when available.
    check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr), "create_instance",
          "vkEnumerateInstanceExtensionProperties");
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()), "create_instance",
          "vkEnumerateInstanceExtensionProperties");
    const auto has_extension = [&extensions](std::string_view name)
    {
        return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension)
        {
            return name == extension.extensionName;
        });
    };
    std::vector<const char*> enabled_extensions;
    VkInstanceCreateFlags flags = 0;
    if (has_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
    {
        enabled_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        m_debug_utils = true;
    }
    if (has_extension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
    {
        enabled_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    // Window surfaces: VK_KHR_surface with the extension of every window system the loader offers, and
    // the HDR and wide-gamut color spaces.
    if (has_extension(VK_KHR_SURFACE_EXTENSION_NAME))
    {
        enabled_extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        m_surfaces = true;
        for (const auto& [kind, extension] : SURFACE_EXTENSIONS)
        {
            if (has_extension(extension))
            {
                enabled_extensions.push_back(extension.data());
                m_window_systems[static_cast<std::size_t>(std::to_underlying(kind))] = true;
            }
        }
        if (has_extension(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME))
        {
            enabled_extensions.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
            m_extended_color_spaces = true;
        }
    }

    // The messenger forwards validation messages to the application (also while the instance is created).
    VkDebugUtilsMessageSeverityFlagsEXT severities = 0;
    if (has_any(m_reported_severities, DebugMessageSeverity::VERBOSE))
    {
        severities |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    }
    if (has_any(m_reported_severities, DebugMessageSeverity::INFORMATION))
    {
        severities |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
    }
    if (has_any(m_reported_severities, DebugMessageSeverity::WARNING))
    {
        severities |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    }
    if (has_any(m_reported_severities, DebugMessageSeverity::CRITICAL))
    {
        severities |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    }
    const VkDebugUtilsMessengerCreateInfoEXT messenger{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity = severities,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = &debug_messenger_callback,
        .pUserData = this,
    };
    const bool use_messenger = m_debug_utils && m_debug_callback && severities != 0;

    // The instance.
    const std::string application_name{description.application_name};
    const VkApplicationInfo application{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = application_name.c_str(),
        .applicationVersion = VK_MAKE_API_VERSION(0, description.application_version.major, description.application_version.minor,
                                                  description.application_version.patch),
        .pEngineName = "gfx",
        .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };
    const VkInstanceCreateInfo create_information{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = use_messenger ? &messenger : nullptr,
        .flags = flags,
        .pApplicationInfo = &application,
        .enabledLayerCount = static_cast<std::uint32_t>(enabled_layers.size()),
        .ppEnabledLayerNames = enabled_layers.data(),
        .enabledExtensionCount = static_cast<std::uint32_t>(enabled_extensions.size()),
        .ppEnabledExtensionNames = enabled_extensions.data(),
    };
    check(vkCreateInstance(&create_information, nullptr, &m_instance), "create_instance", "vkCreateInstance");
    load_instance_functions(m_instance);

    // From here on the destructor does not run if construction fails: release what exists on errors.
    try
    {
        if (use_messenger)
        {
            check(vkCreateDebugUtilsMessengerEXT(m_instance, &messenger, nullptr, &m_messenger), "create_instance",
                  "vkCreateDebugUtilsMessengerEXT");
        }

        // Its physical devices.
        check(vkEnumeratePhysicalDevices(m_instance, &count, nullptr), "create_instance", "vkEnumeratePhysicalDevices");
        std::vector<VkPhysicalDevice> physical_devices(count);
        check(vkEnumeratePhysicalDevices(m_instance, &count, physical_devices.data()), "create_instance",
              "vkEnumeratePhysicalDevices");
        for (const VkPhysicalDevice physical_device : physical_devices)
        {
            m_physical_devices.push_back(std::make_unique<VulkanPhysicalDevice>(*this, physical_device));
        }
    }
    catch (...)
    {
        m_physical_devices.clear();
        if (m_messenger != VK_NULL_HANDLE)
        {
            vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
        }
        vkDestroyInstance(m_instance, nullptr);
        throw;
    }
}

VulkanInstance::~VulkanInstance()
{
    m_physical_devices.clear();
    if (m_messenger != VK_NULL_HANDLE)
    {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
    }
    vkDestroyInstance(m_instance, nullptr);
}

void
VulkanInstance::report(DebugMessageSeverity severity,
                       DebugMessageType type,
                       std::string_view text) const
{
    if (m_debug_callback && has_any(m_reported_severities, severity))
    {
        m_debug_callback(DebugMessage{.severity = severity, .type = type, .text = text});
    }
}

auto
VulkanInstance::create_surface(const NativeWindowHandle& window,
                               std::string_view operation) const -> VkSurfaceKHR
{
    // A window, of a window system this instance presents to.
    const NativeWindowKind kind = window.kind();
    if (kind == NativeWindowKind::NONE)
    {
        throw InvalidUsageError({
            .backend = BackendKind::VULKAN,
            .operation = std::string{operation},
            .problem = "the window handle names no window (NativeWindowKind::NONE)",
            .suggestion = "pass the native handle of a platform window (gfx::platform::Window::native_handle())",
        });
    }
    const std::string_view extension = surface_extension(kind);
    if (extension.empty())
    {
        throw_unsupported(operation, std::format("presentation to a {} window", kind),
                          std::format("Vulkan has no surface for {} windows", kind),
                          "use a backend that presents to them (WebGPU for HTML canvases)");
    }
    if (!supports_window_system(kind))
    {
        throw_unsupported(operation, std::format("presentation to a {} window", kind),
                          std::format("the Vulkan loader offers no {}{}", m_surfaces ? extension : "VK_KHR_surface",
                                      m_surfaces ? "" : " (no window system can be presented to)"),
                          "install a Vulkan driver for this window system, or render offscreen");
    }

    // The surface, through the window system's own creation function.
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult result = VK_ERROR_EXTENSION_NOT_PRESENT;
    switch (kind)
    {
        case NativeWindowKind::WINDOWS:
        {
            const Win32WindowHandle& handle = *window.get_if<Win32WindowHandle>();
            result = surface::create_win32(m_instance, vkGetInstanceProcAddr, handle.instance, handle.window, &surface);
            break;
        }
        case NativeWindowKind::XLIB:
        {
            const XlibWindowHandle& handle = *window.get_if<XlibWindowHandle>();
            result = surface::create_xlib(m_instance, vkGetInstanceProcAddr, handle.display, handle.window, &surface);
            break;
        }
        case NativeWindowKind::XCB:
        {
            const XcbWindowHandle& handle = *window.get_if<XcbWindowHandle>();
            result = surface::create_xcb(m_instance, vkGetInstanceProcAddr, handle.connection, handle.window, &surface);
            break;
        }
        case NativeWindowKind::WAYLAND:
        {
            const WaylandWindowHandle& handle = *window.get_if<WaylandWindowHandle>();
            result = surface::create_wayland(m_instance, vkGetInstanceProcAddr, handle.display, handle.surface, &surface);
            break;
        }
        case NativeWindowKind::COCOA:
        {
            const CocoaWindowHandle& handle = *window.get_if<CocoaWindowHandle>();
            result = surface::create_metal(m_instance, vkGetInstanceProcAddr, handle.metal_layer, &surface);
            break;
        }
        case NativeWindowKind::NONE:
        case NativeWindowKind::WEB_CANVAS:
            break;
    }

    // The extension is enabled, so a missing function means this build lacks the window system's headers.
    if (result == VK_ERROR_EXTENSION_NOT_PRESENT)
    {
        throw_unsupported(operation, std::format("presentation to a {} window", kind),
                          std::format("this build of gfx has no {} support (the window system's headers were not found "
                                      "when it was built)",
                                      extension),
                          "install the window system's development headers and rebuild gfx");
    }
    check(result, operation, "vkCreate*SurfaceKHR");
    return surface;
}

auto
VulkanInstance::backend() const noexcept -> BackendKind
{
    return BackendKind::VULKAN;
}

auto
VulkanInstance::debug_name() const noexcept -> std::string_view
{
    return m_debug_name;
}

void
VulkanInstance::set_debug_name(std::string_view name)
{
    m_debug_name = name;
}

auto
VulkanInstance::physical_device_count() const noexcept -> std::size_t
{
    return m_physical_devices.size();
}

auto
VulkanInstance::physical_device(std::size_t index) -> PhysicalDevice&
{
    if (index >= m_physical_devices.size())
    {
        throw InvalidUsageError({
            .backend = BackendKind::VULKAN,
            .operation = "Instance::physical_device",
            .problem = std::format("physical device {} does not exist; the instance has {}", index, m_physical_devices.size()),
            .suggestion = "use an index below physical_device_count()",
        });
    }
    return *m_physical_devices[index];
}

// ---------------------------------------------------------------------------------------------------
// WindowSurface
// ---------------------------------------------------------------------------------------------------

WindowSurface::WindowSurface(const VulkanInstance& instance,
                             const NativeWindowHandle& window,
                             std::string_view operation)
    : m_instance{instance.native()}
    , m_surface{instance.create_surface(window, operation)}
{
}

WindowSurface::~WindowSurface()
{
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
}

} // namespace gfx::vulkan
