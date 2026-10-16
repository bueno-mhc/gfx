/// @file vulkan_instance.cppm
/// @brief Internal partition: VulkanInstance (VkInstance, validation layers, debug messenger, window
///        surfaces) and VulkanPhysicalDevice (properties, limits, features, formats and queue families,
///        queried once).
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
    VkPhysicalDevicePresentIdFeaturesKHR present_id_features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR present_wait_features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};

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
    [[nodiscard]] auto surface_support(const NativeWindowHandle& window) const -> SurfaceSupport override;
    [[nodiscard]] auto create_device(const DeviceDescription& description) -> std::unique_ptr<Device> override;

    /// @brief What a swapchain for @p surface can use.
    [[nodiscard]] auto surface_support(VkSurfaceKHR surface,
                                       std::string_view operation) const -> SurfaceSupport;

    /// @brief Whether queue family @p family can present to @p surface.
    [[nodiscard]] auto can_present(std::uint32_t family,
                                   VkSurfaceKHR surface,
                                   std::string_view operation) const -> bool;

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

    /// @brief Whether VK_KHR_surface and the surface extension of window system @p kind are enabled.
    [[nodiscard]] auto supports_window_system(NativeWindowKind kind) const noexcept -> bool
    {
        return m_window_systems[static_cast<std::size_t>(std::to_underlying(kind))];
    }

    /// @brief Whether any window system can be presented to (VK_KHR_surface is enabled).
    [[nodiscard]] auto has_surfaces() const noexcept -> bool
    {
        return m_surfaces;
    }

    /// @brief Whether VK_EXT_swapchain_colorspace is enabled (HDR and wide-gamut color spaces).
    [[nodiscard]] auto has_extended_color_spaces() const noexcept -> bool
    {
        return m_extended_color_spaces;
    }

    /// @brief Creates a surface for @p window (the caller destroys it with vkDestroySurfaceKHR).
    /// @throws InvalidUsageError when @p window names no window, BackendCapabilityError when its window
    ///         system cannot be presented to.
    [[nodiscard]] auto create_surface(const NativeWindowHandle& window,
                                      std::string_view operation) const -> VkSurfaceKHR;

    /// @brief Forwards a message to the application's debug callback (if its severity is wanted).
    void report(DebugMessageSeverity severity, DebugMessageType type, std::string_view text) const;

private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    bool m_debug_utils = false;
    bool m_validation_layers = false;
    bool m_surfaces = false;
    bool m_extended_color_spaces = false;
    std::array<bool, ENUMERATOR_COUNT<NativeWindowKind>> m_window_systems{};
    DebugMessageSeverity m_reported_severities = DebugMessageSeverity::NONE;
    std::function<void(const DebugMessage&)> m_debug_callback;
    std::vector<std::unique_ptr<VulkanPhysicalDevice>> m_physical_devices;
    std::string m_debug_name;
};

/// @brief A window surface (VkSurfaceKHR), destroyed with the object.
class WindowSurface
{
public:
    WindowSurface(const VulkanInstance& instance,
                  const NativeWindowHandle& window,
                  std::string_view operation);
    ~WindowSurface();
    WindowSurface(const WindowSurface&) = delete;
    auto operator=(const WindowSurface&) -> WindowSurface& = delete;

    /// @brief The native surface.
    [[nodiscard]] auto native() const noexcept -> VkSurfaceKHR
    {
        return m_surface;
    }

private:
    VkInstance m_instance{VK_NULL_HANDLE};
    VkSurfaceKHR m_surface{VK_NULL_HANDLE};
};

} // namespace gfx::vulkan
