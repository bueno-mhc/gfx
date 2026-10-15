/// @file vulkan_instance.cppm
/// @brief Internal partition: VulkanInstance (VkInstance, validation layers, debug messenger) and
///        VulkanPhysicalDevice (properties, limits, features, formats and queue families, queried once).
module;

#include <volk.h>

module gfx.backends.vulkan:instance;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import gfx.interface;
import :common;

namespace gfx::vulkan
{

class VulkanInstance;

/// @brief Everything a physical device reports, queried once when the instance is created. The
///        structures' pNext pointers are cleared after the query (device creation builds its own chains).
struct PhysicalDeviceCapabilities
{
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceVulkan11Properties properties11{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES};
    VkPhysicalDeviceVulkan12Properties properties12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
    VkPhysicalDeviceVulkan13Properties properties13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR acceleration_structure_properties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_properties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    VkPhysicalDeviceMeshShaderPropertiesEXT mesh_shader_properties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT};
    VkPhysicalDeviceFragmentShadingRatePropertiesKHR shading_rate_properties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR};

    VkPhysicalDeviceFeatures features{};
    VkPhysicalDeviceVulkan11Features features11{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features features12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features features13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader_features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR shading_rate_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
    VkPhysicalDeviceFaultFeaturesEXT fault_features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};

    VkPhysicalDeviceMemoryProperties memory_properties{};
    std::vector<VkQueueFamilyProperties> queue_families;
    std::vector<std::string> extensions; ///< Sorted.

    /// @brief Whether the device offers extension @p name.
    [[nodiscard]] auto has_extension(std::string_view name) const noexcept -> bool
    {
        return std::ranges::binary_search(extensions, name);
    }

    /// @brief Whether the device supports at least Vulkan 1.3 (the backend's baseline).
    [[nodiscard]] auto has_vulkan_1_3() const noexcept -> bool
    {
        return properties.apiVersion >= VK_API_VERSION_1_3;
    }
};

/// @brief A Vulkan physical device.
class VulkanPhysicalDevice final : public PhysicalDevice
{
public:
    VulkanPhysicalDevice(VulkanInstance& instance, VkPhysicalDevice physical_device);

    [[nodiscard]] auto backend() const noexcept -> BackendKind override;
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override;
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto properties() const noexcept -> const PhysicalDeviceProperties& override;
    [[nodiscard]] auto queue_families() const noexcept -> std::span<const QueueFamilyProperties> override;
    [[nodiscard]] auto supports(DeviceFeature feature) const noexcept -> bool override;
    [[nodiscard]] auto format_support(Format format) const -> FormatSupport override;
    [[nodiscard]] auto surface_support(const NativeWindow& window) const -> SurfaceSupport override;
    [[nodiscard]] auto create_device(const DeviceDescription& description) -> std::unique_ptr<Device> override;

    /// @brief The native physical device.
    [[nodiscard]] auto native() const noexcept -> VkPhysicalDevice
    {
        return m_physical_device;
    }

    /// @brief The instance that owns this device.
    [[nodiscard]] auto instance() const noexcept -> VulkanInstance&
    {
        return m_instance;
    }

    /// @brief What the device reports.
    [[nodiscard]] auto capabilities() const noexcept -> const PhysicalDeviceCapabilities&
    {
        return m_capabilities;
    }

private:
    void query_capabilities();
    void fill_properties();

private:
    VulkanInstance& m_instance;
    VkPhysicalDevice m_physical_device;
    PhysicalDeviceCapabilities m_capabilities;
    PhysicalDeviceProperties m_properties;
    std::vector<QueueFamilyProperties> m_queue_families;
    std::string m_debug_name;
};

/// @brief A Vulkan instance (VkInstance), with the validation layers and the debug messenger that
///        forwards their messages to InstanceDescription::debug_callback.
class VulkanInstance final : public Instance
{
public:
    explicit VulkanInstance(const InstanceDescription& description);
    ~VulkanInstance() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override;
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override;
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto physical_device_count() const noexcept -> std::size_t override;
    [[nodiscard]] auto physical_device(std::size_t index) -> PhysicalDevice& override;

    /// @brief The native instance.
    [[nodiscard]] auto native() const noexcept -> VkInstance
    {
        return m_instance;
    }

    /// @brief Whether VK_EXT_debug_utils is enabled (object names, labels, messenger).
    [[nodiscard]] auto has_debug_utils() const noexcept -> bool
    {
        return m_debug_utils;
    }

    /// @brief Whether the validation layers are enabled.
    [[nodiscard]] auto has_validation_layers() const noexcept -> bool
    {
        return m_validation_layers;
    }

    /// @brief Forwards a message to the application's debug callback (if its severity is wanted).
    void report(DebugMessageSeverity severity, DebugMessageType type, std::string_view text) const;

private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    bool m_debug_utils = false;
    bool m_validation_layers = false;
    DebugMessageSeverity m_reported_severities = DebugMessageSeverity::NONE;
    std::function<void(const DebugMessage&)> m_debug_callback;
    std::vector<std::unique_ptr<VulkanPhysicalDevice>> m_physical_devices;
    std::string m_debug_name;
};

} // namespace gfx::vulkan
