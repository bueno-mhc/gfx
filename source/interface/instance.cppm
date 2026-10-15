/// @file instance.cppm
/// @brief Instance: the entry object of a backend; enumerates physical devices.
export module gfx.interface:instance;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :common_types;
import :physical_device;

export namespace gfx
{

/// @brief A message from the backend's debug facilities (e.g. the Vulkan validation layers).
struct DebugMessage
{
    DebugMessageSeverity severity = DebugMessageSeverity::NONE; ///< Severity.
    DebugMessageType type = DebugMessageType::NONE;             ///< Category.
    std::string_view text{};                                    ///< Message text.
};

/// @brief How to create an Instance (passed to create_backend()).
struct InstanceDescription
{
    std::string_view application_name = "gfx application";        ///< Reported to drivers and tools.
    Version application_version{};                                ///< Reported to drivers and tools.
    ValidationLevel validation_level = DEFAULT_VALIDATION_LEVEL;  ///< Checks the library performs (every object
                                                                  ///< of the instance inherits it).
    bool enable_validation = false;                               ///< Also enable the native API's validation
                                                                  ///< (Vulkan validation layers, D3D12 debug layer).
    DebugMessageSeverity debug_message_severities =
        DebugMessageSeverity::WARNING | DebugMessageSeverity::CRITICAL; ///< Severities forwarded to the callback.
    std::function<void(const DebugMessage&)> debug_callback{};    ///< Receives debug messages (may run on any thread).
};

/// @brief Entry object of a backend. Created by create_backend(); owns the physical devices.
class Instance : public BackendObject
{
public:
    /// @brief Number of physical devices.
    [[nodiscard]] virtual auto physical_device_count() const noexcept -> std::size_t = 0;

    /// @brief Physical device @p index.
    /// @throws InvalidUsageError when @p index >= physical_device_count().
    [[nodiscard]] virtual auto physical_device(std::size_t index) -> PhysicalDevice& = 0;

    /// @brief The physical device that best matches @p preference among those with every CORE_FEATURES.
    /// @throws BackendCapabilityError when there is no such physical device (the message lists what each
    ///         device lacks).
    [[nodiscard]] auto select_physical_device(PhysicalDevicePreference preference = PhysicalDevicePreference::HIGH_PERFORMANCE)
        -> PhysicalDevice&
    {
        if (physical_device_count() == 0)
        {
            throw_unsupported("Instance::select_physical_device", "physical device",
                              std::format("no physical device supports the {} backend", backend()),
                              "install a driver that supports it, or use another backend");
        }

        // Only devices with every core feature can run gfx.
        std::vector<std::size_t> capable;
        std::string lacking;
        for (std::size_t index = 0; index < physical_device_count(); ++index)
        {
            const std::vector<DeviceFeature> missing = physical_device(index).missing_core_features();
            if (missing.empty())
            {
                capable.push_back(index);
                continue;
            }
            std::string features;
            for (const DeviceFeature feature : missing)
            {
                features += std::format("{}{}", features.empty() ? "" : ", ", feature);
            }
            lacking += std::format("{}'{}' lacks {}", lacking.empty() ? "" : "; ", physical_device(index).properties().name, features);
        }
        if (capable.empty())
        {
            throw_unsupported("Instance::select_physical_device", "core features",
                              std::format("no physical device has every core feature: {}", lacking),
                              "update the graphics driver, or use a newer GPU");
        }

        // Rank device types: lower is better.
        const auto rank = [preference](PhysicalDeviceType type)
        {
            constexpr std::array<int, ENUMERATOR_COUNT<PhysicalDeviceType>> high_performance{4, 1, 0, 2, 3};
            constexpr std::array<int, ENUMERATOR_COUNT<PhysicalDeviceType>> low_power{4, 0, 1, 2, 3};
            const auto index = static_cast<std::size_t>(std::to_underlying(type));
            return preference == PhysicalDevicePreference::HIGH_PERFORMANCE ? high_performance[index] : low_power[index];
        };

        // Keep the first capable device of the best rank.
        std::size_t best = capable.front();
        for (const std::size_t index : capable)
        {
            if (rank(physical_device(index).properties().type) < rank(physical_device(best).properties().type))
            {
                best = index;
            }
        }
        return physical_device(best);
    }
};

auto
Instance::physical_device(std::size_t /*index*/) -> PhysicalDevice&
{
    throw_not_implemented("Instance::physical_device");
}

} // namespace gfx
