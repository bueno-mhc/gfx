/// @file physical_device.cppm
/// @brief PhysicalDevice: a GPU (or software device) the backend can use, its properties and features.
export module gfx.interface:physical_device;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :common_types;
import :device;

export namespace gfx
{

/// @brief Limits of a physical device that applications commonly check.
struct DeviceLimits
{
    std::uint32_t maximum_image_dimension = 0;                        ///< Largest width/height of a 2D image.
    std::uint32_t maximum_image_array_layers = 0;                     ///< Largest array layer count.
    std::uint32_t maximum_push_constant_size = 0;                     ///< Push-constant bytes.
    std::uint32_t maximum_bound_descriptor_sets = 0;                  ///< Sets bound at once.
    std::uint32_t maximum_color_attachments = 0;                      ///< Color attachments per pass.
    std::array<std::uint32_t, 3> maximum_compute_workgroup_count{};   ///< Workgroups per dispatch, per axis.
    std::array<std::uint32_t, 3> maximum_compute_workgroup_size{};    ///< Invocations per workgroup, per axis.
    std::uint32_t maximum_compute_workgroup_invocations = 0;          ///< Invocations per workgroup.
    float maximum_sampler_anisotropy = 1.0F;                          ///< Largest SamplerDescription::maximum_anisotropy.
    std::uint64_t minimum_uniform_buffer_offset_alignment = 1;        ///< Alignment of uniform buffer offsets.
    std::uint64_t minimum_storage_buffer_offset_alignment = 1;        ///< Alignment of storage buffer offsets.
    double timestamp_period_nanoseconds = 1.0;                        ///< Nanoseconds per timestamp tick.
    SampleCount color_sample_counts = SampleCount::ONE;               ///< Supported color sample counts.
    SampleCount depth_sample_counts = SampleCount::ONE;               ///< Supported depth sample counts.
};

/// @brief Identity, versions and limits of a physical device.
struct PhysicalDeviceProperties
{
    std::string name{};                                ///< Device name.
    PhysicalDeviceType type = PhysicalDeviceType::OTHER; ///< Discrete, integrated, ...
    std::uint32_t vendor_identifier = 0;               ///< PCI vendor identifier.
    std::uint32_t device_identifier = 0;               ///< PCI device identifier.
    Version api_version{};                             ///< Graphics API version supported.
    Version driver_version{};                          ///< Driver version.
    DeviceLimits limits{};                             ///< Limits.
};

/// @brief A family of identical queues.
struct QueueFamilyProperties
{
    QueueCapability capabilities = QueueCapability::NONE; ///< Work its queues run.
    std::uint32_t queue_count = 0;                        ///< Queues in the family.
    std::uint32_t timestamp_valid_bits = 0;               ///< Meaningful timestamp bits (0 = no timestamps).
};

/// @brief What a format can be used for on a physical device.
struct FormatSupport
{
    ImageUsage optimal_tiling_usage = ImageUsage::NONE; ///< Image usages with ImageTiling::OPTIMAL.
    ImageUsage linear_tiling_usage = ImageUsage::NONE;  ///< Image usages with ImageTiling::LINEAR.
    BufferUsage buffer_usage = BufferUsage::NONE;       ///< Texel buffer and vertex buffer usages.
};

/// @brief Queues requested from the device.
struct QueueRequest
{
    QueueType type = QueueType::GRAPHICS; ///< Queue type; a dedicated family is preferred.
    std::uint32_t count = 1;              ///< Number of queues.
    float priority = 1.0F;                ///< Scheduling priority in [0, 1].
};

/// @brief How to create a Device.
struct DeviceDescription
{
    std::span<const QueueRequest> queues{};             ///< Queues to create.
    std::span<const DeviceFeature> required_features{}; ///< Features that must be enabled.
    std::string_view debug_name{};                      ///< Name shown by debugging tools.
};

/// @brief A physical device. Owned by its Instance.
class PhysicalDevice : public BackendObject
{
public:
    /// @brief Identity, versions and limits.
    [[nodiscard]] virtual auto properties() const noexcept -> const PhysicalDeviceProperties& = 0;

    /// @brief Queue families.
    [[nodiscard]] virtual auto queue_families() const noexcept -> std::span<const QueueFamilyProperties> = 0;

    /// @brief Whether a feature is supported (and can be required by DeviceDescription).
    [[nodiscard]] virtual auto supports(DeviceFeature feature) const noexcept -> bool = 0;

    /// @brief What @p format can be used for.
    [[nodiscard]] virtual auto format_support(Format format) const -> FormatSupport = 0;

    /// @brief Creates a logical device.
    /// @throws BackendCapabilityError listing every required feature that is not supported.
    [[nodiscard]] virtual auto create_device(const DeviceDescription& description) -> std::unique_ptr<Device> = 0;

    /// @brief The features of @p features this device does not support.
    [[nodiscard]] auto missing_features(std::span<const DeviceFeature> features) const -> std::vector<DeviceFeature>
    {
        std::vector<DeviceFeature> missing;
        for (const DeviceFeature feature : features)
        {
            if (!supports(feature))
            {
                missing.push_back(feature);
            }
        }
        return missing;
    }
};

auto
PhysicalDevice::format_support(Format /*format*/) const -> FormatSupport
{
    throw_not_implemented("PhysicalDevice::format_support");
}

auto
PhysicalDevice::create_device(const DeviceDescription& /*description*/) -> std::unique_ptr<Device>
{
    throw_not_implemented("PhysicalDevice::create_device");
}

} // namespace gfx
