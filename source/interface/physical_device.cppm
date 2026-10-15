/// @file physical_device.cppm
/// @brief PhysicalDevice: a GPU (or software device) the backend can use, its properties and features.
export module gfx.interface:physical_device;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :common_types;
import :bindless;
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
    std::uint64_t minimum_texel_buffer_offset_alignment = 1;          ///< Alignment of BufferView offsets.
    std::uint64_t non_coherent_atom_size = 1;                         ///< Granularity of flushes of non-coherent memory.
    std::uint64_t maximum_memory_allocation_size = 0;                 ///< Largest single allocation.
    std::uint32_t maximum_draw_indirect_count = 1;                    ///< Largest draw count of one indirect draw.
    std::uint32_t minimum_subgroup_size = 1;                          ///< Smallest subgroup (wave) size.
    std::uint32_t maximum_subgroup_size = 1;                          ///< Largest subgroup (wave) size.
    std::array<std::uint32_t, ENUMERATOR_COUNT<BindlessResourceKind>> maximum_bindless_resources{}; ///< Bindless capacity
                                                                                                    ///< the device allows, by kind.
    std::uint32_t maximum_multiview_view_count = 1;                   ///< Views of a multiview pass.
    std::uint32_t shader_group_handle_size = 0;                       ///< Bytes of a ray tracing shader group handle.
    std::uint32_t shader_group_handle_alignment = 0;                  ///< Alignment of shader binding table records.
    std::uint32_t shader_group_base_alignment = 0;                    ///< Alignment of shader binding table regions.
    std::uint32_t maximum_ray_recursion_depth = 0;                    ///< Deepest trace recursion.
    std::array<std::uint32_t, 3> maximum_task_workgroup_size{};       ///< Task shader invocations per workgroup, per axis.
    std::array<std::uint32_t, 3> maximum_mesh_workgroup_size{};       ///< Mesh shader invocations per workgroup, per axis.
    std::uint32_t maximum_mesh_output_vertices = 0;                   ///< Vertices one mesh workgroup emits.
    std::uint32_t maximum_mesh_output_primitives = 0;                 ///< Primitives one mesh workgroup emits.
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

/// @brief A format and color space a surface can present.
struct SurfaceFormat
{
    Format format = Format::UNDEFINED;                   ///< Image format.
    ColorSpace color_space = ColorSpace::SRGB_NONLINEAR; ///< Color space.

    constexpr bool operator==(const SurfaceFormat&) const = default;
};

/// @brief What a window surface supports (PhysicalDevice::surface_support()), to fill a SwapchainDescription.
struct SurfaceSupport
{
    std::vector<SurfaceFormat> formats{};           ///< Presentable formats and color spaces (HDR ones included).
    std::vector<PresentMode> present_modes{};       ///< Presentation modes.
    std::uint32_t minimum_image_count = 0;          ///< Fewest swapchain images.
    std::uint32_t maximum_image_count = 0;          ///< Most swapchain images (0 = no limit).
    Extent current_extent{};                        ///< The window's size.
    ImageUsage supported_usage = ImageUsage::NONE;  ///< Usages swapchain images allow.
};

/// @brief Runs a background task (e.g. an asynchronous pipeline compilation) on the application's job
///        system. May be called from any thread; the library never creates threads of its own.
using TaskScheduler = std::function<void(std::move_only_function<void()>)>;

/// @brief How to create a Device.
struct DeviceDescription
{
    std::span<const QueueRequest> queues{};                            ///< Queues to create.
    std::span<const DeviceFeature> required_features{};                ///< Features that must be enabled (CORE_FEATURES always are).
    BindlessTableDescription bindless{};                               ///< Capacities of the bindless table.
    CrashDiagnostic crash_diagnostics = CrashDiagnostic::NONE;         ///< GPU crash diagnostics to collect.
    TaskScheduler task_scheduler{};                                    ///< Runs asynchronous pipeline compilations
                                                                       ///< (empty = they compile during creation).
    std::string_view debug_name{};                                     ///< Name shown by debugging tools.
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

    /// @brief What a swapchain for @p window can use (formats, color spaces, present modes, image counts).
    [[nodiscard]] virtual auto surface_support(const NativeWindow& window) const -> SurfaceSupport = 0;

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

    /// @brief The CORE_FEATURES this device lacks (it cannot run gfx when any is missing).
    [[nodiscard]] auto missing_core_features() const -> std::vector<DeviceFeature>
    {
        return missing_features(CORE_FEATURES);
    }
};

auto
PhysicalDevice::format_support(Format /*format*/) const -> FormatSupport
{
    throw_not_implemented("PhysicalDevice::format_support");
}

auto
PhysicalDevice::surface_support(const NativeWindow& /*window*/) const -> SurfaceSupport
{
    throw_not_implemented("PhysicalDevice::surface_support");
}

auto
PhysicalDevice::create_device(const DeviceDescription& /*description*/) -> std::unique_ptr<Device>
{
    throw_not_implemented("PhysicalDevice::create_device");
}

} // namespace gfx
