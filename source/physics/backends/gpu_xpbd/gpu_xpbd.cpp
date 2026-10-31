// The GPU backend's entry points: whether the factory can run it here, worlds on a device of their own (the factory's)
// or on the application's device.
module gfx.physics.gpu_xpbd;

import std;
import gfx.enums;
import gfx.errors;
import gfx.interface;
import gfx.backend_factory;
import gfx.physics.enums;
import gfx.physics.interface;
import gfx.physics.world;
import :settings;
import :probing;
import :backend;

namespace gfx::physics::gpu_xpbd
{

namespace
{

/// The instance of a world made by the factory (no validation layers: the factory has no place to ask for them).
[[nodiscard]] auto
make_instance() -> std::unique_ptr<Instance>
{
    return create_instance(BackendKind::VULKAN, {.application_name = "gfx physics (GPU backend)"});
}

/// The physical devices of @p instance in the order the factory tries them: the one the graphics library selects first,
/// then the others.
[[nodiscard]] auto
candidates(Instance& instance) -> std::vector<PhysicalDevice*>
{
    std::vector<PhysicalDevice*> devices;
    try
    {
        devices.push_back(&instance.select_physical_device(PhysicalDevicePreference::HIGH_PERFORMANCE));
    }
    catch (const BackendCapabilityError&)
    {
        // No device with the core features: each is tried below and says what it lacks.
    }
    for (std::size_t index = 0; index < instance.physical_device_count(); ++index)
    {
        PhysicalDevice* device = &instance.physical_device(index);
        if (!std::ranges::contains(devices, device))
        {
            devices.push_back(device);
        }
    }
    return devices;
}

/// Why no physical device of @p devices can run the backend (empty when one can), and the first that can.
[[nodiscard]] auto
first_capable(std::span<PhysicalDevice* const> devices) -> std::pair<PhysicalDevice*, std::string>
{
    std::string lacking;
    for (PhysicalDevice* device : devices)
    {
        const std::vector<MissingCapability> missing = missing_capabilities(*device);
        if (missing.empty())
        {
            return {device, {}};
        }
        lacking += std::format("\n'{}' lacks:{}", device->properties().name, missing_lines(missing));
    }
    return {nullptr, devices.empty() ? std::string{"no physical device supports Vulkan"} : std::format("no physical device can run it:{}", lacking)};
}

/// An instance and a device on the most capable physical device that runs the backend.
[[nodiscard]] auto
make_owned_device(const GpuXpbdDescription& settings) -> OwnedDevice
{
    const BackendAvailability vulkan = backend_availability(BackendKind::VULKAN);
    if (!vulkan.available)
    {
        throw_unsupported(PhysicsBackendKind::GPU_XPBD, "create_physics_world", PhysicsCapability::WORLD_OBJECTS,
                          std::format("the graphics library's Vulkan backend is unavailable: {}", vulkan.reason),
                          "install a Vulkan driver, or use another backend (PhysicsBackendKind::REFERENCE or BOX3D)");
    }
    OwnedDevice owned{.instance = make_instance(), .device = {}};
    const std::vector<PhysicalDevice*> devices = candidates(*owned.instance);
    const auto [physical_device, reason] = first_capable(devices);
    if (physical_device == nullptr)
    {
        throw_unsupported(PhysicsBackendKind::GPU_XPBD, "create_physics_world", PhysicsCapability::WORLD_OBJECTS, reason,
                          "use another backend (PhysicsBackendKind::REFERENCE or BOX3D)");
    }

    // One compute queue; 64-bit atomics when the device has them and the settings allow them.
    static constexpr std::array QUEUES{QueueRequest{.type = QueueType::COMPUTE, .count = 1, .priority = 1.0F}};
    std::vector<DeviceFeature> features;
    if (settings.variants.integer64_atomics == GpuVariantChoice::AUTOMATIC && physical_device->supports(DeviceFeature::SHADER_INTEGER64_ATOMICS))
    {
        features.push_back(DeviceFeature::SHADER_INTEGER64_ATOMICS);
    }
    owned.device = physical_device->create_device({.queues = QUEUES, .required_features = features, .debug_name = "gfx physics GPU backend"});
    return owned;
}

} // namespace

auto
unavailable_reason() -> std::string
{
    const BackendAvailability vulkan = backend_availability(BackendKind::VULKAN);
    if (!vulkan.available)
    {
        return std::format("the graphics library's Vulkan backend is unavailable: {}", vulkan.reason);
    }
    try
    {
        const std::unique_ptr<Instance> instance = make_instance();
        const std::vector<PhysicalDevice*> devices = candidates(*instance);
        return first_capable(devices).second;
    }
    catch (const Error& error)
    {
        return error.what();
    }
}

auto
create_physics_world(const PhysicsWorldDescription& description,
                     const GpuXpbdDescription& settings) -> std::unique_ptr<PhysicsWorld>
{
    check_world_description(description, PhysicsBackendKind::GPU_XPBD);
    OwnedDevice owned = make_owned_device(settings);
    Device& device = *owned.device;
    return std::make_unique<SteppedWorld>(description, std::make_unique<GpuXpbdBackend>(std::move(owned), device, settings));
}

auto
create_physics_world(const PhysicsWorldDescription& description,
                     Device& device,
                     const GpuXpbdDescription& settings) -> std::unique_ptr<PhysicsWorld>
{
    check_world_description(description, PhysicsBackendKind::GPU_XPBD);
    return std::make_unique<SteppedWorld>(description, std::make_unique<GpuXpbdBackend>(OwnedDevice{}, device, settings));
}

} // namespace gfx::physics::gpu_xpbd
