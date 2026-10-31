/// @file vulkan_device.cpp
/// @brief VulkanDevice: feature selection, queue assignment, device and allocator creation, teardown.
module;

#include <volk.h>
#include <vk_mem_alloc.h>

module gfx.backends.vulkan;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import gfx.interface;
import gfx.core.handle_table;
import :common;
import :translation;
import :instance;
import :device;
import :pipelines;

namespace gfx::vulkan
{

namespace
{

/// @brief Features enabled whenever the device supports them: they cost nothing at run time.
constexpr std::array AUTOMATIC_FEATURES{
    DeviceFeature::TIMELINE_SEMAPHORE,         DeviceFeature::EVENTS,
    DeviceFeature::DYNAMIC_RENDERING,          DeviceFeature::DESCRIPTOR_INDEXING,
    DeviceFeature::BUFFER_DEVICE_ADDRESS,      DeviceFeature::ASYNCHRONOUS_COMPUTE_QUEUE,
    DeviceFeature::DEDICATED_TRANSFER_QUEUE,   DeviceFeature::SAMPLER_ANISOTROPY,
    DeviceFeature::MULTI_DRAW_INDIRECT,        DeviceFeature::DRAW_INDIRECT_COUNT,
    DeviceFeature::GEOMETRY_SHADER,            DeviceFeature::TESSELLATION_SHADER,
    DeviceFeature::SHADER_FLOAT16,             DeviceFeature::SHADER_FLOAT64,
    DeviceFeature::SHADER_INTEGER16,           DeviceFeature::SHADER_INTEGER64,
    DeviceFeature::FILL_MODE_NON_SOLID,        DeviceFeature::WIDE_LINES,
    DeviceFeature::DEPTH_CLAMP,                DeviceFeature::INDEPENDENT_BLEND,
    DeviceFeature::TIMESTAMP_QUERY,            DeviceFeature::PIPELINE_STATISTICS_QUERY,
    DeviceFeature::TEXTURE_COMPRESSION_BC,     DeviceFeature::TEXTURE_COMPRESSION_ETC2,
    DeviceFeature::TEXTURE_COMPRESSION_ASTC,   DeviceFeature::EXTENDED_DYNAMIC_STATE,
    DeviceFeature::DEPTH_BOUNDS,               DeviceFeature::MULTIVIEW,
    DeviceFeature::SAMPLER_FILTER_MINMAX,      DeviceFeature::MEMORY_BUDGET,
    DeviceFeature::PRESENTATION,               DeviceFeature::FRAME_LATENCY_WAIT,
    DeviceFeature::SHADER_MEMORY_MODEL,
};

/// @brief Most native queues a device uses (the deleter records the progress of each).
constexpr std::size_t MAXIMUM_NATIVE_QUEUES = 16;

/// @brief A comma-separated list of features.
auto
feature_list(std::span<const DeviceFeature> features) -> std::string
{
    std::string list;
    for (const DeviceFeature feature : features)
    {
        list += std::format("{}{}", list.empty() ? "" : ", ", feature);
    }
    return list;
}

} // namespace

// ---------------------------------------------------------------------------------------------------
// Creation
// ---------------------------------------------------------------------------------------------------

auto
VulkanPhysicalDevice::create_device(const DeviceDescription& description) -> std::unique_ptr<Device>
{
    return std::make_unique<VulkanDevice>(*this, description);
}

VulkanDevice::VulkanDevice(VulkanPhysicalDevice& physical_device,
                           const DeviceDescription& description)
    : Device{}
    , m_physical_device{physical_device}
    , m_crash_diagnostics{description.crash_diagnostics}
    , m_task_scheduler{description.task_scheduler}
    , m_debug_name{description.debug_name.empty() ? std::string{"Vulkan device"} : std::string{description.debug_name}}
{
    set_validation_level(physical_device.validation_level());

    // Features first: nothing is created when one is missing.
    enable_features(description);
    create_device(description);

    // Everything after the native device: released again when a step fails (no destructor runs then).
    try
    {
        create_queues();
        create_allocator();
        const VkPipelineCacheCreateInfo cache{.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        check_result(m_functions.vkCreatePipelineCache(m_device, &cache, nullptr, &m_pipeline_cache), "PhysicalDevice::create_device",
                     "vkCreatePipelineCache");
        m_deleter = std::make_unique<VulkanDeleter>(*this);

        // The bindless table and the pipeline layout every pipeline uses by default.
        const bool acceleration_structures = enabled(DeviceFeature::RAY_TRACING) || enabled(DeviceFeature::RAY_QUERY);
        m_bindless = std::make_unique<BindlessTable>(*this, description.bindless, acceleration_structures);
        m_bindless_pipeline_layout = std::make_unique<VulkanPipelineLayout>(
            *this, m_bindless->set_layout(), bindless_pipeline_layout_definition(description.bindless, m_bindless->kinds()));
        m_bindless_pipeline_layout->set_debug_name("bindless pipeline layout");
        name_object(VK_OBJECT_TYPE_DEVICE, m_device, m_debug_name);
        m_capture_tool = detect_capture_tool();
    }
    catch (...)
    {
        m_bindless_pipeline_layout.reset();
        if (m_deleter)
        {
            m_deleter->run_all();
        }
        m_bindless.reset();
        if (m_deleter)
        {
            m_deleter->run_all();
        }
        m_queues.clear();
        for (const std::unique_ptr<NativeQueue>& native : m_native_queues)
        {
            m_functions.vkDestroySemaphore(m_device, native->progress, nullptr);
        }
        m_functions.vkDestroyPipelineCache(m_device, m_pipeline_cache, nullptr);
        if (m_allocator != nullptr)
        {
            vmaDestroyAllocator(m_allocator);
        }
        m_functions.vkDestroyDevice(m_device, nullptr);
        throw;
    }
}

void
VulkanDevice::enable_features(const DeviceDescription& description)
{
    // Core features and required features must all be supported; report every missing one at once.
    std::vector<DeviceFeature> requested(CORE_FEATURES.begin(), CORE_FEATURES.end());
    requested.insert(requested.end(), description.required_features.begin(), description.required_features.end());
    if (has_any(description.crash_diagnostics, CrashDiagnostic::BREADCRUMBS))
    {
        requested.push_back(DeviceFeature::GPU_CRASH_MARKERS);
    }
    std::vector<DeviceFeature> missing = m_physical_device.missing_features(requested);
    std::ranges::sort(missing);
    const auto [first, last] = std::ranges::unique(missing);
    missing.erase(first, last);
    if (!missing.empty())
    {
        throw_unsupported("PhysicalDevice::create_device", feature_list(missing),
                          std::format("'{}' does not support {}", m_physical_device.properties().name, feature_list(missing)),
                          "remove them from DeviceDescription::required_features (or crash_diagnostics), or choose a physical "
                          "device whose PhysicalDevice::supports() reports them");
    }

    // Automatic features when supported, plus everything requested.
    for (const DeviceFeature feature : AUTOMATIC_FEATURES)
    {
        m_enabled_features[static_cast<std::size_t>(std::to_underlying(feature))] = m_physical_device.supports(feature);
    }
    for (const DeviceFeature feature : requested)
    {
        m_enabled_features[static_cast<std::size_t>(std::to_underlying(feature))] = true;
    }

    // Vendor crash reports need VK_EXT_device_fault.
    if (has_any(description.crash_diagnostics, CrashDiagnostic::VENDOR_REPORT) &&
        (!m_physical_device.capabilities().has_extension(VK_EXT_DEVICE_FAULT_EXTENSION_NAME) ||
         m_physical_device.capabilities().fault_features.deviceFault != VK_TRUE))
    {
        throw_unsupported("PhysicalDevice::create_device", "CrashDiagnostic::VENDOR_REPORT",
                          std::format("'{}' does not support VK_EXT_device_fault", m_physical_device.properties().name),
                          "request CrashDiagnostic::BREADCRUMBS only, or use a driver with VK_EXT_device_fault");
    }

    // Queues: requested types mapped to families (dedicated families when they exist).
    const std::vector<VkQueueFamilyProperties>& families = m_physical_device.capabilities().queue_families;
    const auto find_family = [&families](VkQueueFlags required, VkQueueFlags excluded) -> std::optional<std::uint32_t>
    {
        for (std::uint32_t family = 0; family < families.size(); ++family)
        {
            if ((families[family].queueFlags & required) == required && (families[family].queueFlags & excluded) == 0 &&
                families[family].queueCount > 0)
            {
                return family;
            }
        }
        return std::nullopt;
    };
    const std::optional<std::uint32_t> graphics = find_family(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0);
    const std::optional<std::uint32_t> compute = find_family(VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT).or_else([&]
    {
        return graphics;
    });
    const std::optional<std::uint32_t> transfer =
        find_family(VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT).or_else([&]
    {
        return compute;
    });
    const std::optional<std::uint32_t> sparse =
        graphics && (families[*graphics].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) != 0 ? graphics
                                                                                         : find_family(VK_QUEUE_SPARSE_BINDING_BIT, 0);
    const std::array<std::optional<std::uint32_t>, ENUMERATOR_COUNT<QueueType>> family_of{graphics, compute, transfer, sparse};
    for (std::size_t type = 0; type < family_of.size(); ++type)
    {
        m_queue_families[type] = family_of[type].value_or(std::numeric_limits<std::uint32_t>::max());
    }

    // One native queue per request while the family has queues left; requests beyond share them.
    static constexpr std::array<QueueRequest, 1> DEFAULT_QUEUES{QueueRequest{.type = QueueType::GRAPHICS, .count = 1, .priority = 1.0F}};
    const std::span<const QueueRequest> requests = description.queues.empty() ? std::span<const QueueRequest>{DEFAULT_QUEUES}
                                                                              : description.queues;
    std::vector<std::uint32_t> used(families.size(), 0);
    for (const QueueRequest& request : requests)
    {
        const std::optional<std::uint32_t> family = family_of[static_cast<std::size_t>(std::to_underlying(request.type))];
        if (!family)
        {
            throw_unsupported("PhysicalDevice::create_device", std::format("{} queue", request.type),
                              std::format("'{}' has no queue family that runs {} work", m_physical_device.properties().name,
                                          request.type),
                              "request another queue type");
        }
        for (std::uint32_t index = 0; index < request.count; ++index)
        {
            m_queue_assignments.push_back({
                .type = request.type,
                .family = *family,
                .native_index = used[*family]++ % families[*family].queueCount,
                .priority = std::clamp(request.priority, 0.0F, 1.0F),
            });
        }
    }
}

void
VulkanDevice::create_device(const DeviceDescription& description)
{
    const PhysicalDeviceCapabilities& capabilities = m_physical_device.capabilities();

    // Core features: everything the device supports, except what costs performance (robustness),
    // needs protected queues, or was not requested (sparse residency).
    VkPhysicalDeviceFeatures2 features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .features = capabilities.features};
    features.features.robustBufferAccess = VK_FALSE;
    if (!enabled(DeviceFeature::SPARSE_BINDING))
    {
        features.features.sparseBinding = VK_FALSE;
        features.features.sparseResidencyBuffer = VK_FALSE;
        features.features.sparseResidencyImage2D = VK_FALSE;
        features.features.sparseResidencyImage3D = VK_FALSE;
        features.features.sparseResidency2Samples = VK_FALSE;
        features.features.sparseResidency4Samples = VK_FALSE;
        features.features.sparseResidency8Samples = VK_FALSE;
        features.features.sparseResidency16Samples = VK_FALSE;
        features.features.sparseResidencyAliased = VK_FALSE;
        features.features.shaderResourceResidency = VK_FALSE;
        features.features.shaderResourceMinLod = VK_FALSE;
    }
    VkPhysicalDeviceVulkan11Features features11 = capabilities.features11;
    features11.protectedMemory = VK_FALSE;
    VkPhysicalDeviceVulkan12Features features12 = capabilities.features12;
    features12.bufferDeviceAddressCaptureReplay = VK_FALSE;
    features12.bufferDeviceAddressMultiDevice = VK_FALSE;
    if (!enabled(DeviceFeature::SHADER_INTEGER64_ATOMICS))
    {
        // Required only: shaders using them without the request are then refused by the validation layers, as on
        // devices without them.
        features12.shaderBufferInt64Atomics = VK_FALSE;
        features12.shaderSharedInt64Atomics = VK_FALSE;
    }
    if (!enabled(DeviceFeature::SHADER_MEMORY_MODEL))
    {
        features12.vulkanMemoryModel = VK_FALSE;
        features12.vulkanMemoryModelDeviceScope = VK_FALSE;
        features12.vulkanMemoryModelAvailabilityVisibilityChains = VK_FALSE;
    }
    VkPhysicalDeviceVulkan13Features features13 = capabilities.features13;
    features13.robustImageAccess = VK_FALSE;
    features.pNext = &features11;
    features11.pNext = &features12;
    features12.pNext = &features13;
    features13.pNext = nullptr;
    void** tail = &features13.pNext;
    const auto append = [&tail](auto& structure)
    {
        structure.pNext = nullptr;
        *tail = &structure;
        tail = &structure.pNext;
    };

    // Optional extensions and their features.
    m_extensions.clear();
    if (enabled(DeviceFeature::MEMORY_BUDGET))
    {
        m_extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    }
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR ray_query{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR shading_rate{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
    VkPhysicalDeviceFaultFeaturesEXT fault{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
    VkPhysicalDevicePresentIdFeaturesKHR present_id{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR present_wait{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    if (enabled(DeviceFeature::RAY_TRACING) || enabled(DeviceFeature::RAY_QUERY))
    {
        m_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        m_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        acceleration_structure.accelerationStructure = VK_TRUE;
        acceleration_structure.descriptorBindingAccelerationStructureUpdateAfterBind =
            capabilities.acceleration_structure_features.descriptorBindingAccelerationStructureUpdateAfterBind;
        append(acceleration_structure);
    }
    if (enabled(DeviceFeature::RAY_TRACING))
    {
        m_extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        ray_tracing.rayTracingPipeline = VK_TRUE;
        ray_tracing.rayTracingPipelineTraceRaysIndirect = capabilities.ray_tracing_features.rayTracingPipelineTraceRaysIndirect;
        ray_tracing.rayTraversalPrimitiveCulling = capabilities.ray_tracing_features.rayTraversalPrimitiveCulling;
        append(ray_tracing);
    }
    if (enabled(DeviceFeature::RAY_QUERY))
    {
        m_extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        ray_query.rayQuery = VK_TRUE;
        append(ray_query);
    }
    if (enabled(DeviceFeature::MESH_SHADER))
    {
        m_extensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
        mesh_shader.taskShader = VK_TRUE;
        mesh_shader.meshShader = VK_TRUE;
        mesh_shader.multiviewMeshShader = enabled(DeviceFeature::MULTIVIEW) ? capabilities.mesh_shader_features.multiviewMeshShader
                                                                            : VK_FALSE;
        mesh_shader.primitiveFragmentShadingRateMeshShader =
            enabled(DeviceFeature::VARIABLE_RATE_SHADING) ? capabilities.mesh_shader_features.primitiveFragmentShadingRateMeshShader
                                                          : VK_FALSE;
        append(mesh_shader);
    }
    if (enabled(DeviceFeature::VARIABLE_RATE_SHADING))
    {
        m_extensions.push_back(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
        shading_rate.pipelineFragmentShadingRate = VK_TRUE;
        shading_rate.primitiveFragmentShadingRate = capabilities.shading_rate_features.primitiveFragmentShadingRate;
        shading_rate.attachmentFragmentShadingRate = VK_TRUE;
        append(shading_rate);
    }
    if (enabled(DeviceFeature::CONSERVATIVE_RASTERIZATION))
    {
        m_extensions.push_back(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);
    }
    if (enabled(DeviceFeature::GPU_CRASH_MARKERS))
    {
        m_extensions.push_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
    }
    if (has_any(description.crash_diagnostics, CrashDiagnostic::VENDOR_REPORT))
    {
        m_extensions.push_back(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
        fault.deviceFault = VK_TRUE;
        fault.deviceFaultVendorBinary = capabilities.fault_features.deviceFaultVendorBinary;
        append(fault);
    }

    // Presentation: swapchains, waits for presentations, and HDR metadata when the device offers it.
    if (enabled(DeviceFeature::PRESENTATION))
    {
        m_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        if (capabilities.has_extension(VK_EXT_HDR_METADATA_EXTENSION_NAME))
        {
            m_extensions.push_back(VK_EXT_HDR_METADATA_EXTENSION_NAME);
            m_hdr_metadata = true;
        }
    }
    if (enabled(DeviceFeature::FRAME_LATENCY_WAIT))
    {
        m_extensions.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
        m_extensions.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
        present_id.presentId = VK_TRUE;
        present_wait.presentWait = VK_TRUE;
        append(present_id);
        append(present_wait);
    }

    // Stages and accesses barriers may name with these features.
    m_supported_stages = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
                         VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                         VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                         VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT | VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
                         VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    m_supported_access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT |
                         VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
                         VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                         VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_READ_BIT |
                         VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    if (enabled(DeviceFeature::TESSELLATION_SHADER))
    {
        m_supported_stages |= VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT;
    }
    if (enabled(DeviceFeature::GEOMETRY_SHADER))
    {
        m_supported_stages |= VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT;
    }
    if (enabled(DeviceFeature::MESH_SHADER))
    {
        m_supported_stages |= VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
    }
    if (enabled(DeviceFeature::RAY_TRACING) || enabled(DeviceFeature::RAY_QUERY))
    {
        m_supported_stages |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        m_supported_access |= VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    }
    if (enabled(DeviceFeature::RAY_TRACING))
    {
        m_supported_stages |= VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    }
    if (enabled(DeviceFeature::VARIABLE_RATE_SHADING))
    {
        m_supported_stages |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
        m_supported_access |= VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;
    }

    // One queue create information per family, with the highest priority requested for each queue.
    std::map<std::uint32_t, std::vector<float>> priorities;
    for (const QueueAssignment& assignment : m_queue_assignments)
    {
        std::vector<float>& family = priorities[assignment.family];
        if (family.size() <= assignment.native_index)
        {
            family.resize(assignment.native_index + 1, 0.0F);
        }
        family[assignment.native_index] = std::max(family[assignment.native_index], assignment.priority);
    }
    std::vector<VkDeviceQueueCreateInfo> queues;
    std::size_t native_queue_count = 0;
    for (const auto& [family, family_priorities] : priorities)
    {
        queues.push_back({
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = family,
            .queueCount = static_cast<std::uint32_t>(family_priorities.size()),
            .pQueuePriorities = family_priorities.data(),
        });
        native_queue_count += family_priorities.size();
    }
    if (native_queue_count > MAXIMUM_NATIVE_QUEUES)
    {
        throw_unsupported("PhysicalDevice::create_device", "queues",
                          std::format("{} native queues were requested; the backend supports at most {}", native_queue_count,
                                      MAXIMUM_NATIVE_QUEUES),
                          "request fewer queues (queues of one type can share command pools and run in parallel)");
    }

    // The device, and its functions.
    const VkDeviceCreateInfo create_information{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features,
        .queueCreateInfoCount = static_cast<std::uint32_t>(queues.size()),
        .pQueueCreateInfos = queues.data(),
        .enabledExtensionCount = static_cast<std::uint32_t>(m_extensions.size()),
        .ppEnabledExtensionNames = m_extensions.data(),
    };
    check(vkCreateDevice(m_physical_device.native(), &create_information, nullptr, &m_device), "PhysicalDevice::create_device",
          "vkCreateDevice");
    volkLoadDeviceTable(&m_functions, m_device);
}

void
VulkanDevice::create_queues()
{
    const std::vector<VkQueueFamilyProperties>& families = m_physical_device.capabilities().queue_families;

    // One native queue per (family, index) in use, with its progress timeline.
    const auto native_queue = [this, &families](std::uint32_t family, std::uint32_t index) -> NativeQueue&
    {
        for (const std::unique_ptr<NativeQueue>& existing : m_native_queues)
        {
            if (existing->family == family && existing->index == index)
            {
                return *existing;
            }
        }
        auto native = std::make_unique<NativeQueue>();
        native->family = family;
        native->index = index;
        native->capabilities = from_vulkan_queue_flags(families[family].queueFlags);
        m_functions.vkGetDeviceQueue(m_device, family, index, &native->queue);
        const VkSemaphoreTypeCreateInfo timeline{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                                                 .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                                                 .initialValue = 0};
        const VkSemaphoreCreateInfo semaphore{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &timeline};
        check_result(m_functions.vkCreateSemaphore(m_device, &semaphore, nullptr, &native->progress), "PhysicalDevice::create_device",
                     "vkCreateSemaphore");
        name_object(VK_OBJECT_TYPE_SEMAPHORE, native->progress, std::format("queue family {} queue {} progress", family, index));
        m_native_queues.push_back(std::move(native));
        return *m_native_queues.back();
    };

    // One gfx queue per request.
    std::array<std::uint32_t, ENUMERATOR_COUNT<QueueType>> counts{};
    for (const QueueAssignment& assignment : m_queue_assignments)
    {
        std::uint32_t& count = counts[static_cast<std::size_t>(std::to_underlying(assignment.type))];
        m_queues.push_back(std::make_unique<VulkanQueue>(*this, native_queue(assignment.family, assignment.native_index),
                                                         assignment.type, count++));
    }
}

void
VulkanDevice::create_allocator()
{
    // VMA loads the functions it needs from the device itself.
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateFlags flags = VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT;
    if (enabled(DeviceFeature::BUFFER_DEVICE_ADDRESS))
    {
        flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    }
    if (enabled(DeviceFeature::MEMORY_BUDGET))
    {
        flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }
    const VmaAllocatorCreateInfo create_information{
        .flags = flags,
        .physicalDevice = m_physical_device.native(),
        .device = m_device,
        .pVulkanFunctions = &functions,
        .instance = m_physical_device.instance().native(),
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };
    check_result(vmaCreateAllocator(&create_information, &m_allocator), "PhysicalDevice::create_device", "vmaCreateAllocator");
}

// ---------------------------------------------------------------------------------------------------
// Destruction
// ---------------------------------------------------------------------------------------------------

VulkanDevice::~VulkanDevice()
{
    // The GPU finishes, then every deferred deletion runs.
    for (const std::unique_ptr<NativeQueue>& native : m_native_queues)
    {
        native->mutex.lock();
    }
    m_functions.vkDeviceWaitIdle(m_device);
    for (const std::unique_ptr<NativeQueue>& native : m_native_queues)
    {
        native->mutex.unlock();
    }
    m_bindless_pipeline_layout.reset();
    m_deleter->run_all();

    // Objects still alive are a defect of the application: report them (they leak).
    std::string leaks;
    std::size_t leaked = 0;
    const auto count = [&leaks, &leaked](std::string_view kind, std::size_t live)
    {
        if (live != 0)
        {
            leaks += std::format("{}{} {}", leaks.empty() ? "" : ", ", live, kind);
            leaked += live;
        }
    };
    count("Buffer", m_handles.buffers.size());
    count("Image", m_handles.images.size());
    count("ImageView", m_handles.image_views.size());
    count("Sampler", m_handles.samplers.size());
    count("BufferView", m_handles.buffer_views.size());
    count("DeviceMemory", m_handles.memories.size());
    count("ShaderModule", m_handles.shader_modules.size());
    count("DescriptorSetLayout", m_handles.descriptor_set_layouts.size());
    count("PipelineLayout", m_handles.pipeline_layouts.size());
    count("ComputePipeline", m_handles.compute_pipelines.size());
    count("GraphicsPipeline", m_handles.graphics_pipelines.size());
    count("RayTracingPipeline", m_handles.ray_tracing_pipelines.size());
    count("DescriptorPool", m_handles.descriptor_pools.size());
    count("DescriptorSet", m_handles.descriptor_sets.size());
    count("CommandPool", m_handles.command_pools.size());
    count("CommandBuffer", m_handles.command_buffers.size());
    count("Fence", m_handles.fences.size());
    count("Semaphore", m_handles.semaphores.size());
    count("TimelineSemaphore", m_handles.timeline_semaphores.size());
    count("Event", m_handles.events.size());
    count("QueryPool", m_handles.query_pools.size());
    count("AccelerationStructure", m_handles.acceleration_structures.size());
    count("PipelineCache", m_handles.pipeline_caches.size());
    if (leaked != 0)
    {
        m_physical_device.instance().report(DebugMessageSeverity::CRITICAL, DebugMessageType::VALIDATION,
                                            std::format("[gfx::VULKAN] Device '{}' was destroyed while objects created from it are "
                                                        "alive: {}. Destroy every object before its device.",
                                                        m_debug_name, leaks));
    }

    // The device's own objects.
    m_bindless.reset();
    m_deleter->run_all();
    m_queues.clear();
    for (const std::unique_ptr<NativeQueue>& native : m_native_queues)
    {
        m_functions.vkDestroySemaphore(m_device, native->progress, nullptr);
    }
    m_native_queues.clear();
    m_functions.vkDestroyPipelineCache(m_device, m_pipeline_cache, nullptr);

    // With leaked allocations, the allocator would assert: leak it with them.
    if (leaked == 0)
    {
        vmaDestroyAllocator(m_allocator);
    }
    m_functions.vkDestroyDevice(m_device, nullptr);
}

// ---------------------------------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::backend() const noexcept -> BackendKind
{
    return BackendKind::VULKAN;
}

auto
VulkanDevice::debug_name() const noexcept -> std::string_view
{
    return m_debug_name;
}

void
VulkanDevice::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    name_object(VK_OBJECT_TYPE_DEVICE, m_device, m_debug_name);
}

auto
VulkanDevice::physical_device() const noexcept -> const PhysicalDevice&
{
    return m_physical_device;
}

auto
VulkanDevice::queue(QueueType type,
                    std::uint32_t index) -> Queue&
{
    std::uint32_t seen = 0;
    for (const std::unique_ptr<VulkanQueue>& candidate : m_queues)
    {
        if (candidate->type() == type && seen++ == index)
        {
            return *candidate;
        }
    }
    throw InvalidUsageError({
        .backend = BackendKind::VULKAN,
        .operation = "Device::queue",
        .problem = std::format("queue {} of type {} was not requested; the device has {} {} queue(s)", index, type,
                               queue_count(type), type),
        .suggestion = "request it in DeviceDescription::queues",
    });
}

auto
VulkanDevice::queue_count(QueueType type) const noexcept -> std::uint32_t
{
    return static_cast<std::uint32_t>(std::ranges::count(m_queue_assignments, type, &QueueAssignment::type));
}

auto
VulkanDevice::deleter() noexcept -> Deleter&
{
    return *m_deleter;
}

void
VulkanDevice::wait_idle()
{
    // vkDeviceWaitIdle needs every queue: hold every queue's lock (in a fixed order).
    {
        std::vector<std::unique_lock<std::mutex>> locks;
        locks.reserve(m_native_queues.size());
        for (const std::unique_ptr<NativeQueue>& native : m_native_queues)
        {
            locks.emplace_back(native->mutex);
        }
        check_result(m_functions.vkDeviceWaitIdle(m_device), "Device::wait_idle", "vkDeviceWaitIdle");
    }
    m_deleter->collect();
}

auto
VulkanDevice::bindless_pipeline_layout() const noexcept -> const PipelineLayout&
{
    return *m_bindless_pipeline_layout;
}

auto
VulkanDevice::bindless_usage() const noexcept -> BindlessTableUsage
{
    try
    {
        return m_bindless->usage();
    }
    catch (...)
    {
        return {};
    }
}

void
VulkanDevice::require(DeviceFeature feature,
                      std::string_view operation) const
{
    if (!enabled(feature))
    {
        throw_unsupported(operation, std::format("{}", feature),
                          m_physical_device.supports(feature)
                              ? std::format("the device was created without {}", feature)
                              : std::format("'{}' does not support {}", m_physical_device.properties().name, feature),
                          m_physical_device.supports(feature)
                              ? std::format("add DeviceFeature::{} to DeviceDescription::required_features", feature)
                              : std::string{"use a physical device that supports it"});
    }
}

auto
VulkanDevice::queue_family(QueueType type) const noexcept -> std::uint32_t
{
    return m_queue_families[static_cast<std::size_t>(std::to_underlying(type))];
}

void
VulkanDevice::name_object(VkObjectType type,
                          std::uint64_t handle,
                          std::string_view name) const
{
    if (!m_physical_device.instance().has_debug_utils() || vkSetDebugUtilsObjectNameEXT == nullptr || name.empty())
    {
        return;
    }
    const std::string terminated{name};
    const VkDebugUtilsObjectNameInfoEXT information{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = handle,
        .pObjectName = terminated.c_str(),
    };
    vkSetDebugUtilsObjectNameEXT(m_device, &information);
}

void
VulkanDevice::check_result(VkResult result,
                           std::string_view operation,
                           std::string_view function,
                           std::source_location location) const
{
    if (result < VK_SUCCESS) [[unlikely]]
    {
        // A lost device reports where the GPU was; anything else is a plain native failure.
        if (result == VK_ERROR_DEVICE_LOST)
        {
            report_device_lost(operation, function, location);
        }
        throw_result(result, operation, function, location);
    }
}

} // namespace gfx::vulkan
