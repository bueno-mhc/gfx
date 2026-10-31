// What the GPU backend needs of a device, what it chooses to run there, and the errors naming what a device lacks.
module gfx.physics.gpu_xpbd;

import std;
import gfx.enums;
import gfx.interface;
import gfx.physics.enums;
import gfx.physics.errors;
import :settings;
import :probing;

namespace gfx::physics::gpu_xpbd
{

namespace
{

/// The subgroup operations the subgroup variant uses (shaders/workgroup.glsl).
constexpr SubgroupOperation SUBGROUP_VARIANT_OPERATIONS = SubgroupOperation::BASIC | SubgroupOperation::ARITHMETIC;

/// Adds @p name to @p missing when @p reported is under @p required.
template<class Value>
void
require_at_least(std::vector<MissingCapability>& missing,
                 std::string_view name,
                 Value required,
                 Value reported)
{
    if (reported < required)
    {
        missing.push_back({.name = std::string{name}, .required = std::format("{}", required), .reported = std::format("{}", reported)});
    }
}

/// Adds @p feature to @p missing when @p available is false.
void
require_feature(std::vector<MissingCapability>& missing,
                DeviceFeature feature,
                bool available,
                std::string_view what)
{
    if (!available)
    {
        missing.push_back({.name = std::format("DeviceFeature::{} ({})", feature, what), .required = "enabled", .reported = "missing"});
    }
}

/// The limits every device made from @p physical_device has.
void
check_limits(const PhysicalDevice& physical_device,
             std::vector<MissingCapability>& missing)
{
    const DeviceLimits& limits = physical_device.properties().limits;
    require_at_least(missing, "maximum_compute_workgroup_size (x)", GPU_XPBD_WORKGROUP_SIZE, limits.maximum_compute_workgroup_size[0]);
    require_at_least(missing, "maximum_compute_workgroup_invocations", GPU_XPBD_WORKGROUP_SIZE, limits.maximum_compute_workgroup_invocations);
    require_at_least(missing, "maximum_compute_workgroup_count (x)", 65535U, limits.maximum_compute_workgroup_count[0]);
    require_at_least(missing, "maximum_compute_shared_memory_size", GPU_XPBD_SHARED_MEMORY_SIZE, limits.maximum_compute_shared_memory_size);
    require_at_least(missing, "maximum_storage_buffer_range", GPU_XPBD_STORAGE_BUFFER_RANGE, limits.maximum_storage_buffer_range);
}

} // namespace

auto
missing_capabilities(const PhysicalDevice& physical_device) -> std::vector<MissingCapability>
{
    std::vector<MissingCapability> missing;
    const auto computes = [](const QueueFamilyProperties& family) { return has_all(family.capabilities, QueueCapability::COMPUTE) && family.queue_count > 0; };
    const auto families = physical_device.queue_families();
    if (!std::ranges::any_of(families, computes))
    {
        missing.push_back({.name = "a queue family with COMPUTE capability", .required = "1", .reported = "0"});
    }
    check_limits(physical_device, missing);
    for (const DeviceFeature feature : CORE_FEATURES)
    {
        require_feature(missing, feature, physical_device.supports(feature), "a core feature of the graphics library");
    }
    require_feature(missing, DeviceFeature::SHADER_MEMORY_MODEL, physical_device.supports(DeviceFeature::SHADER_MEMORY_MODEL),
                    "workgroups exchanging data within a dispatch");
    return missing;
}

auto
missing_capabilities(Device& device) -> std::vector<MissingCapability>
{
    // The physical device's limits; the device's own queues, features and bindless table.
    std::vector<MissingCapability> missing;
    if (!step_queue_type(device))
    {
        missing.push_back({.name = "a COMPUTE queue, or a GRAPHICS queue with COMPUTE capability",
                           .required = "1",
                           .reported = std::format("{} COMPUTE, {} GRAPHICS", device.queue_count(QueueType::COMPUTE), device.queue_count(QueueType::GRAPHICS))});
    }
    check_limits(device.physical_device(), missing);
    for (const DeviceFeature feature : CORE_FEATURES)
    {
        require_feature(missing, feature, device.enabled(feature), "a core feature of the graphics library");
    }
    require_feature(missing, DeviceFeature::SHADER_MEMORY_MODEL, device.enabled(DeviceFeature::SHADER_MEMORY_MODEL),
                    "workgroups exchanging data within a dispatch");
    const BindlessTableUsage bindless = device.bindless_usage();
    const std::uint32_t used = bindless.used_by(BindlessResourceKind::STORAGE_BUFFER);
    const std::uint32_t capacity = bindless.capacity[static_cast<std::size_t>(std::to_underlying(BindlessResourceKind::STORAGE_BUFFER))];
    require_at_least(missing, "free bindless storage buffers", GPU_XPBD_STORAGE_BUFFERS, capacity > used ? capacity - used : 0U);
    return missing;
}

auto
step_queue_type(Device& device) -> std::optional<QueueType>
{
    for (const QueueType type : {QueueType::COMPUTE, QueueType::GRAPHICS})
    {
        if (device.queue_count(type) > 0 && has_all(device.queue(type).capabilities(), QueueCapability::COMPUTE))
        {
            return type;
        }
    }
    return std::nullopt;
}

auto
missing_lines(std::span<const MissingCapability> missing) -> std::string
{
    std::string lines;
    for (const MissingCapability& capability : missing)
    {
        lines += std::format("\n  - {}: required {}, reported {}", capability.name, capability.required, capability.reported);
    }
    return lines;
}

void
throw_missing(std::string_view operation,
              std::string_view device_name,
              std::span<const MissingCapability> missing)
{
    throw_unsupported(PhysicsBackendKind::GPU_XPBD, operation, PhysicsCapability::WORLD_OBJECTS,
                      std::format("'{}' lacks what the GPU backend needs:{}", device_name, missing_lines(missing)),
                      "run the backend on another device (gfx::physics::gpu_xpbd::capabilities() checks one), or use another backend "
                      "(PhysicsBackendKind::REFERENCE or BOX3D)");
}

// ===================================================================================================== public

auto
capabilities(Device& device,
             const GpuXpbdVariants& variants) -> GpuXpbdCapabilities
{
    const std::vector<MissingCapability> missing = missing_capabilities(device);
    const PhysicalDevice& physical_device = device.physical_device();
    if (!missing.empty())
    {
        throw_missing("gpu_xpbd::capabilities", physical_device.properties().name, missing);
    }

    // What the device reports, and the variants chosen from it.
    const DeviceLimits& limits = physical_device.properties().limits;
    const QueueType queue_type = *step_queue_type(device);
    const std::uint32_t family = device.queue(queue_type).family_index();
    const BindlessTableUsage bindless = device.bindless_usage();
    const auto bindless_capacity = [&bindless](BindlessResourceKind kind) { return bindless.capacity[static_cast<std::size_t>(std::to_underlying(kind))]; };
    const std::span<const QueueFamilyProperties> families = physical_device.queue_families();
    return {
        .device_name = physical_device.properties().name,
        .queue_type = queue_type,
        .queue_family = family,
        .maximum_workgroup_invocations = limits.maximum_compute_workgroup_invocations,
        .maximum_compute_shared_memory_size = limits.maximum_compute_shared_memory_size,
        .maximum_storage_buffer_range = limits.maximum_storage_buffer_range,
        .minimum_subgroup_size = limits.minimum_subgroup_size,
        .maximum_subgroup_size = limits.maximum_subgroup_size,
        .subgroup_operations = limits.subgroup_operations,
        .free_storage_buffers = bindless_capacity(BindlessResourceKind::STORAGE_BUFFER) - bindless.used_by(BindlessResourceKind::STORAGE_BUFFER),
        .subgroups = variants.subgroups == GpuVariantChoice::AUTOMATIC && has_all(limits.subgroup_operations, SUBGROUP_VARIANT_OPERATIONS) &&
                     limits.minimum_subgroup_size >= GPU_XPBD_MINIMUM_SUBGROUP_SIZE,
        .integer64_atomics = variants.integer64_atomics == GpuVariantChoice::AUTOMATIC && device.enabled(DeviceFeature::SHADER_INTEGER64_ATOMICS),
        .timestamps = device.enabled(DeviceFeature::TIMESTAMP_QUERY) && family < families.size() && families[family].timestamp_valid_bits > 0,
        .float16 = device.enabled(DeviceFeature::SHADER_FLOAT16),
        .storage_images = bindless_capacity(BindlessResourceKind::STORAGE_IMAGE) > 0,
    };
}

auto
to_string(const GpuXpbdCapabilities& capabilities) -> std::string
{
    const auto yes = [](bool value) { return value ? "yes" : "no"; };
    return std::format("GPU physics on '{}' (queue {}, family {})\n"
                       "  workgroups: {} invocations at most, {} bytes of shared memory; storage buffers of {} bytes at most; {} bindless storage "
                       "buffers free\n"
                       "  subgroups: {} to {} invocations; operations: {}\n"
                       "  variants: {} for scans and sorts, {} for nearest hits\n"
                       "  timestamps: {}; 16-bit floats: {} (not used); storage images: {} (not used)",
                       capabilities.device_name, capabilities.queue_type, capabilities.queue_family, capabilities.maximum_workgroup_invocations,
                       capabilities.maximum_compute_shared_memory_size, capabilities.maximum_storage_buffer_range, capabilities.free_storage_buffers,
                       capabilities.minimum_subgroup_size, capabilities.maximum_subgroup_size, capabilities.subgroup_operations,
                       capabilities.subgroups ? "subgroup arithmetic" : "shared memory", capabilities.integer64_atomics ? "64-bit atomics" : "two 32-bit passes",
                       yes(capabilities.timestamps), yes(capabilities.float16), yes(capabilities.storage_images));
}

} // namespace gfx::physics::gpu_xpbd
