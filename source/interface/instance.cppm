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
    bool enable_validation = false;                               ///< Enable the backend's validation.
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

    /// @brief The physical device that best matches @p preference.
    /// @throws BackendCapabilityError when there is no physical device.
    [[nodiscard]] auto select_physical_device(PhysicalDevicePreference preference = PhysicalDevicePreference::HIGH_PERFORMANCE)
        -> PhysicalDevice&
    {
        if (physical_device_count() == 0)
        {
            throw_unsupported("Instance::select_physical_device", "physical device",
                              std::format("no physical device supports the {} backend", backend()),
                              "install a driver that supports it, or use another backend");
        }

        // Rank device types: lower is better.
        const auto rank = [preference](PhysicalDeviceType type)
        {
            constexpr std::array<int, ENUMERATOR_COUNT<PhysicalDeviceType>> high_performance{4, 1, 0, 2, 3};
            constexpr std::array<int, ENUMERATOR_COUNT<PhysicalDeviceType>> low_power{4, 0, 1, 2, 3};
            const auto index = static_cast<std::size_t>(std::to_underlying(type));
            return preference == PhysicalDevicePreference::HIGH_PERFORMANCE ? high_performance[index] : low_power[index];
        };

        // Keep the first device of the best rank.
        std::size_t best = 0;
        for (std::size_t index = 1; index < physical_device_count(); ++index)
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
