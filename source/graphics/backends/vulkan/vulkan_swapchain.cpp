/// @file vulkan_swapchain.cpp
/// @brief Swapchains: the window surface, the native swapchain recreated on resize (none while the
///        window has no area), its images registered in the handle tables, acquisition, waits for
///        presentation (VK_KHR_present_wait) and HDR metadata. Nothing outside this unit needs the class,
///        so it is declared here (no partition); presentation itself is VulkanQueue::present().
module;

#include <volk.h>

module gfx.backends.vulkan;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import gfx.interface;
import :common;
import :translation;
import :instance;
import :device;

namespace gfx::vulkan
{

namespace
{

/// @brief "FORMAT/COLOR_SPACE, ..." of the formats a surface supports.
auto
format_list(std::span<const SurfaceFormat> formats) -> std::string
{
    std::string list;
    for (const SurfaceFormat& format : formats)
    {
        list += std::format("{}{}/{}", list.empty() ? "" : ", ", format.format, format.color_space);
    }
    return list.empty() ? std::string{"none"} : list;
}

/// @brief "MODE, ..." of the presentation modes a surface supports.
auto
present_mode_list(std::span<const PresentMode> modes) -> std::string
{
    std::string list;
    for (const PresentMode mode : modes)
    {
        list += std::format("{}{}", list.empty() ? "" : ", ", mode);
    }
    return list.empty() ? std::string{"none"} : list;
}

/// @brief The composite alpha mode: opaque when the surface allows it, its first mode otherwise.
auto
composite_alpha(VkCompositeAlphaFlagsKHR supported) noexcept -> VkCompositeAlphaFlagBitsKHR
{
    for (const VkCompositeAlphaFlagBitsKHR mode : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                                   VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                                   VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
    {
        if ((supported & static_cast<VkCompositeAlphaFlagsKHR>(mode)) != 0)
        {
            return mode;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

} // namespace

/// @brief A swapchain (VkSwapchainKHR) with its window surface.
class VulkanSwapchain final : public Swapchain
{
public:
    VulkanSwapchain(VulkanDevice& device, const SwapchainDescription& description);
    ~VulkanSwapchain() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override;
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override;
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto handle() const noexcept -> SwapchainHandle override;
    [[nodiscard]] auto extent() const noexcept -> Extent override;
    [[nodiscard]] auto format() const noexcept -> Format override;
    [[nodiscard]] auto image_count() const noexcept -> std::uint32_t override;
    [[nodiscard]] auto acquire_next_image(SemaphoreHandle signal,
                                          FenceHandle fence,
                                          std::chrono::nanoseconds timeout) -> std::expected<AcquiredImage, SwapchainStatus> override;
    void resize(Extent extent) override;
    [[nodiscard]] auto wait_for_frame_latency(std::chrono::nanoseconds timeout) -> WaitStatus override;
    void set_hdr_metadata(const HdrMetadata& metadata) override;

private:
    /// @brief An image of the native swapchain, registered in the handle tables.
    struct SwapchainImage
    {
        ImageHandle image{};
        ImageViewHandle view{};
        VkImageView native_view{VK_NULL_HANDLE};
    };

    /// @brief Checks the description against what the surface and the device support.
    void validate(const SwapchainDescription& description) const;

    /// @brief (Re)creates the native swapchain for the window's current size (@p requested where the
    ///        window system lets the swapchain choose); none while the window has no area. The device
    ///        must be idle.
    void create(Extent requested, std::string_view operation);

    /// @brief Destroys the views and unregisters the images of the native swapchain.
    void release_images() noexcept;

    /// @brief Names the native swapchain and its images in debugging tools.
    void name_objects() const;

    /// @brief Hands the HDR metadata to the native swapchain.
    void apply_hdr_metadata() const;

private:
    VulkanDevice& m_device;
    WindowSurface m_surface;
    Format m_format{Format::UNDEFINED};
    ColorSpace m_color_space{ColorSpace::SRGB_NONLINEAR};
    PresentMode m_present_mode{PresentMode::FIFO};
    std::uint32_t m_minimum_image_count{0};
    ImageUsage m_usage{ImageUsage::NONE};
    std::uint32_t m_maximum_frame_latency{0};
    Extent m_extent{};
    SwapchainState m_state{};
    SwapchainHandle m_handle{};
    std::vector<SwapchainImage> m_images{};
    std::optional<HdrMetadata> m_hdr_metadata{};
    std::string m_debug_name{};
};

VulkanSwapchain::VulkanSwapchain(VulkanDevice& device,
                                 const SwapchainDescription& description)
    : Swapchain{}
    , m_device{device}
    , m_surface{device.vulkan_physical_device().instance(), description.window, "Device::create_swapchain"}
    , m_format{description.format}
    , m_color_space{description.color_space}
    , m_present_mode{description.present_mode}
    , m_minimum_image_count{description.minimum_image_count}
    , m_usage{description.usage}
    , m_maximum_frame_latency{description.maximum_frame_latency}
    , m_debug_name{description.debug_name}
{
    static constexpr std::string_view OPERATION = "Device::create_swapchain";
    set_validation_level(device.validation_level());

    // The queue families that present to the window, and what the surface supports.
    const VulkanPhysicalDevice& physical_device = device.vulkan_physical_device();
    const std::size_t family_count = physical_device.capabilities().queue_families.size();
    for (std::uint32_t family = 0; family < family_count; ++family)
    {
        m_state.presenting_families.push_back(physical_device.can_present(family, m_surface.native(), OPERATION));
    }
    validate(description);
    m_state.present_identifiers = device.enabled(DeviceFeature::FRAME_LATENCY_WAIT);
    m_state.swapchain_object = this;

    // The native swapchain and its images, then the handle naming the swapchain in presentations.
    try
    {
        create(description.extent, OPERATION);
        m_handle = device.handles().swapchains.insert({.state = &m_state});
    }
    catch (...)
    {
        release_images();
        if (m_state.swapchain != VK_NULL_HANDLE)
        {
            device.functions().vkDestroySwapchainKHR(device.native(), m_state.swapchain, nullptr);
        }
        throw;
    }
}

VulkanSwapchain::~VulkanSwapchain()
{
    // Presentations and rendering into the images must be over before they go away.
    (void)m_device.handles().swapchains.remove(m_handle);
    try
    {
        m_device.wait_idle();
    }
    catch (...)
    {
    }
    release_images();
    if (m_state.swapchain != VK_NULL_HANDLE)
    {
        m_device.functions().vkDestroySwapchainKHR(m_device.native(), m_state.swapchain, nullptr);
    }
}

void
VulkanSwapchain::validate(const SwapchainDescription& description) const
{
    static constexpr std::string_view OPERATION = "Device::create_swapchain";
    m_device.require(DeviceFeature::PRESENTATION, OPERATION);
    const VulkanPhysicalDevice& physical_device = m_device.vulkan_physical_device();
    const std::string_view device_name = physical_device.properties().name;

    // The presenting queue type exists and its family presents to the window.
    const std::uint32_t family = m_device.queue_family(description.present_queue);
    if (family == std::numeric_limits<std::uint32_t>::max())
    {
        throw InvalidUsageError({
            .backend = BackendKind::VULKAN,
            .operation = std::string{OPERATION},
            .problem = std::format("SwapchainDescription::present_queue is {}, but the device has no {} queue", description.present_queue,
                                   description.present_queue),
            .suggestion = "present from a queue type the device was created with (usually GRAPHICS)",
        });
    }
    if (!m_state.presenting_families[family])
    {
        std::string presenting;
        for (std::uint32_t candidate = 0; candidate < m_state.presenting_families.size(); ++candidate)
        {
            if (m_state.presenting_families[candidate])
            {
                presenting += std::format("{}{}", presenting.empty() ? "" : ", ", candidate);
            }
        }
        throw_unsupported(OPERATION, "presentation to this window",
                          std::format("the {} queue family ({}) of '{}' cannot present to the {} window; families that can: {}",
                                      description.present_queue, family, device_name, description.window.kind(),
                                      presenting.empty() ? std::string{"none"} : presenting),
                          presenting.empty() ? "choose the physical device that drives the window's display"
                                             : "present from another queue type (SwapchainDescription::present_queue)");
    }

    // Format, color space, presentation mode and usage the surface supports.
    const SurfaceSupport support = physical_device.surface_support(m_surface.native(), OPERATION);
    const SurfaceFormat requested{.format = description.format, .color_space = description.color_space};
    if (!std::ranges::contains(support.formats, requested))
    {
        throw_unsupported(OPERATION, std::format("swapchain format {}/{}", description.format, description.color_space),
                          std::format("the window surface of '{}' supports {}", device_name, format_list(support.formats)),
                          "choose one of them (PhysicalDevice::surface_support() lists them)");
    }
    if (!std::ranges::contains(support.present_modes, description.present_mode))
    {
        throw_unsupported(OPERATION, std::format("presentation mode {}", description.present_mode),
                          std::format("the window surface of '{}' supports {}", device_name, present_mode_list(support.present_modes)),
                          "choose one of them; FIFO is always supported");
    }
    if (!has_all(support.supported_usage, description.usage))
    {
        throw_unsupported(OPERATION, std::format("swapchain image usage {}", description.usage),
                          std::format("swapchain images of '{}' allow {}", device_name, support.supported_usage),
                          "render into another image and copy (or blit) it into the swapchain image");
    }

    // Counts that make sense.
    if (description.minimum_image_count == 0 || description.maximum_frame_latency == 0)
    {
        throw InvalidUsageError({
            .backend = BackendKind::VULKAN,
            .operation = std::string{OPERATION},
            .problem = std::format("SwapchainDescription::minimum_image_count ({}) and maximum_frame_latency ({}) must be at least 1",
                                   description.minimum_image_count, description.maximum_frame_latency),
            .suggestion = "keep the defaults (3 images, a latency of 2 frames) unless a measurement says otherwise",
        });
    }
}

void
VulkanSwapchain::create(Extent requested,
                        std::string_view operation)
{
    const VulkanPhysicalDevice& physical_device = m_device.vulkan_physical_device();
    const VolkDeviceTable& functions = m_device.functions();

    // The window's size, unless the window system lets the swapchain choose it (Wayland).
    VkSurfaceCapabilitiesKHR capabilities{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device.native(), m_surface.native(), &capabilities), operation,
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == std::numeric_limits<std::uint32_t>::max())
    {
        extent.width = std::clamp(requested.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(requested.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    // The previous images go; the previous swapchain is retired by the new one (or destroyed).
    release_images();
    const VkSwapchainKHR previous = m_state.swapchain;
    m_state.swapchain = VK_NULL_HANDLE;
    m_state.image_count = 0;
    m_state.presented = 0;
    m_extent = {.width = extent.width, .height = extent.height};

    // A window without area (minimized) has no swapchain: acquisitions report OUT_OF_DATE until resize().
    if (extent.width == 0 || extent.height == 0)
    {
        if (previous != VK_NULL_HANDLE)
        {
            functions.vkDestroySwapchainKHR(m_device.native(), previous, nullptr);
        }
        return;
    }

    // The native swapchain.
    std::uint32_t image_count = std::max(m_minimum_image_count, capabilities.minImageCount);
    if (capabilities.maxImageCount != 0)
    {
        image_count = std::min(image_count, capabilities.maxImageCount);
    }
    const VkSwapchainCreateInfoKHR information{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = m_surface.native(),
        .minImageCount = image_count,
        .imageFormat = to_vulkan(m_format),
        .imageColorSpace = to_vulkan(m_color_space),
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = to_vulkan(m_usage),
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = composite_alpha(capabilities.supportedCompositeAlpha),
        .presentMode = to_vulkan(m_present_mode),
        .clipped = VK_TRUE,
        .oldSwapchain = previous,
    };
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    const VkResult result = functions.vkCreateSwapchainKHR(m_device.native(), &information, nullptr, &swapchain);
    if (previous != VK_NULL_HANDLE)
    {
        functions.vkDestroySwapchainKHR(m_device.native(), previous, nullptr);
    }
    m_device.check_result(result, operation, "vkCreateSwapchainKHR");
    m_state.swapchain = swapchain;

    // Its images, with a color view each, registered like any image (not owned, not bindless).
    std::uint32_t count = 0;
    m_device.check_result(functions.vkGetSwapchainImagesKHR(m_device.native(), swapchain, &count, nullptr), operation,
                          "vkGetSwapchainImagesKHR");
    std::vector<VkImage> images(count);
    m_device.check_result(functions.vkGetSwapchainImagesKHR(m_device.native(), swapchain, &count, images.data()), operation,
                          "vkGetSwapchainImagesKHR");
    HandleTables& handles = m_device.handles();
    constexpr ImageSubresourceRange RANGE{.aspects = ImageAspect::COLOR, .mip_level_count = 1, .array_layer_count = 1};
    for (const VkImage image : std::span{images}.first(count))
    {
        const VkImageViewCreateInfo view_information{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = to_vulkan(m_format),
            .components = to_vulkan(ComponentMapping{}),
            .subresourceRange = to_vulkan(RANGE),
        };
        VkImageView view = VK_NULL_HANDLE;
        m_device.check_result(functions.vkCreateImageView(m_device.native(), &view_information, nullptr, &view), operation,
                              "vkCreateImageView");
        SwapchainImage& registered = m_images.emplace_back(SwapchainImage{.native_view = view});
        registered.image = handles.images.insert({
            .image = image,
            .format = m_format,
            .aspects = ImageAspect::COLOR,
            .extent = m_extent,
            .mip_level_count = 1,
            .array_layer_count = 1,
            .samples = SampleCount::ONE,
            .usage = m_usage,
        });
        registered.view = handles.image_views.insert({
            .view = view,
            .image = registered.image,
            .format = m_format,
            .range = RANGE,
            .samples = SampleCount::ONE,
        });
    }
    m_state.image_count = count;
    name_objects();
    apply_hdr_metadata();
}

void
VulkanSwapchain::release_images() noexcept
{
    HandleTables& handles = m_device.handles();
    for (const SwapchainImage& image : m_images)
    {
        (void)handles.image_views.remove(image.view);
        (void)handles.images.remove(image.image);
        m_device.functions().vkDestroyImageView(m_device.native(), image.native_view, nullptr);
    }
    m_images.clear();
}

void
VulkanSwapchain::name_objects() const
{
    if (m_debug_name.empty() || m_state.swapchain == VK_NULL_HANDLE)
    {
        return;
    }
    m_device.name_object(VK_OBJECT_TYPE_SWAPCHAIN_KHR, m_state.swapchain, m_debug_name);
    for (std::size_t index = 0; index < m_images.size(); ++index)
    {
        m_device.name_object(VK_OBJECT_TYPE_IMAGE_VIEW, m_images[index].native_view, std::format("{} image {}", m_debug_name, index));
    }
}

void
VulkanSwapchain::apply_hdr_metadata() const
{
    if (!m_hdr_metadata || m_state.swapchain == VK_NULL_HANDLE)
    {
        return;
    }
    const HdrMetadata& metadata = *m_hdr_metadata;
    const auto xy = [](const std::array<float, 2>& chromaticity)
    {
        return VkXYColorEXT{.x = chromaticity[0], .y = chromaticity[1]};
    };
    const VkHdrMetadataEXT native{
        .sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT,
        .displayPrimaryRed = xy(metadata.display_primary_red),
        .displayPrimaryGreen = xy(metadata.display_primary_green),
        .displayPrimaryBlue = xy(metadata.display_primary_blue),
        .whitePoint = xy(metadata.white_point),
        .maxLuminance = metadata.maximum_luminance,
        .minLuminance = metadata.minimum_luminance,
        .maxContentLightLevel = metadata.maximum_content_light_level,
        .maxFrameAverageLightLevel = metadata.maximum_frame_average_light_level,
    };
    m_device.functions().vkSetHdrMetadataEXT(m_device.native(), 1, &m_state.swapchain, &native);
}

auto
VulkanSwapchain::backend() const noexcept -> BackendKind
{
    return BackendKind::VULKAN;
}

auto
VulkanSwapchain::debug_name() const noexcept -> std::string_view
{
    return m_debug_name;
}

void
VulkanSwapchain::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    name_objects();
}

auto
VulkanSwapchain::handle() const noexcept -> SwapchainHandle
{
    return m_handle;
}

auto
VulkanSwapchain::extent() const noexcept -> Extent
{
    return m_extent;
}

auto
VulkanSwapchain::format() const noexcept -> Format
{
    return m_format;
}

auto
VulkanSwapchain::image_count() const noexcept -> std::uint32_t
{
    return m_state.image_count;
}

auto
VulkanSwapchain::acquire_next_image(SemaphoreHandle signal,
                                    FenceHandle fence,
                                    std::chrono::nanoseconds timeout) -> std::expected<AcquiredImage, SwapchainStatus>
{
    static constexpr std::string_view OPERATION = "Swapchain::acquire_next_image";

    // A window without area has no images until the swapchain is resized.
    if (m_state.swapchain == VK_NULL_HANDLE)
    {
        return std::unexpected(SwapchainStatus::OUT_OF_DATE);
    }

    // The semaphore and fence signaled when the image may be written: at least one of them.
    HandleTables& handles = m_device.handles();
    const VkSemaphore semaphore = signal.is_valid() ? handles.semaphores.get(signal, OPERATION).semaphore : VK_NULL_HANDLE;
    const VkFence native_fence = fence.is_valid() ? handles.fences.get(fence, OPERATION).fence : VK_NULL_HANDLE;
    if (semaphore == VK_NULL_HANDLE && native_fence == VK_NULL_HANDLE)
    {
        throw InvalidUsageError({
            .backend = BackendKind::VULKAN,
            .operation = std::string{OPERATION},
            .problem = std::format("swapchain '{}': neither a semaphore nor a fence would tell when the image may be written",
                                   m_debug_name),
            .suggestion = "pass the binary semaphore the rendering submission waits on",
        });
    }

    // The image, or why there is none.
    std::uint32_t index = 0;
    const VkResult result = m_device.functions().vkAcquireNextImageKHR(m_device.native(), m_state.swapchain, timeout_nanoseconds(timeout),
                                                                       semaphore, native_fence, &index);
    switch (result)
    {
        case VK_SUCCESS:
        case VK_SUBOPTIMAL_KHR:
            return AcquiredImage{
                .index = index,
                .image = m_images[index].image,
                .view = m_images[index].view,
                .suboptimal = result == VK_SUBOPTIMAL_KHR,
            };
        case VK_TIMEOUT:
        case VK_NOT_READY:
            return std::unexpected(SwapchainStatus::TIMEOUT);
        case VK_ERROR_OUT_OF_DATE_KHR:
            return std::unexpected(SwapchainStatus::OUT_OF_DATE);
        case VK_ERROR_SURFACE_LOST_KHR:
            return std::unexpected(SwapchainStatus::SURFACE_LOST);
        default:
            m_device.check_result(result, OPERATION, "vkAcquireNextImageKHR");
            return std::unexpected(SwapchainStatus::OUT_OF_DATE);
    }
}

void
VulkanSwapchain::resize(Extent extent)
{
    // The old images must no longer be in use.
    m_device.wait_idle();
    create(extent, "Swapchain::resize");
}

auto
VulkanSwapchain::wait_for_frame_latency(std::chrono::nanoseconds timeout) -> WaitStatus
{
    static constexpr std::string_view OPERATION = "Swapchain::wait_for_frame_latency";
    if (!m_device.enabled(DeviceFeature::FRAME_LATENCY_WAIT))
    {
        throw_unsupported(OPERATION, "FRAME_LATENCY_WAIT",
                          std::format("'{}' does not support VK_KHR_present_wait", m_device.vulkan_physical_device().properties().name),
                          "check PhysicalDevice::supports(DeviceFeature::FRAME_LATENCY_WAIT), and pace frames with a FrameTimeline "
                          "on devices without it");
    }

    // Fewer than maximum_frame_latency presentations pending: presentation maximum_frame_latency - 1
    // frames before the last one has completed.
    if (m_state.swapchain == VK_NULL_HANDLE || m_state.presented < m_maximum_frame_latency)
    {
        return WaitStatus::SIGNALED;
    }
    const std::uint64_t awaited = m_state.presented - m_maximum_frame_latency + 1;
    const VkResult result = m_device.functions().vkWaitForPresentKHR(m_device.native(), m_state.swapchain, awaited,
                                                                     timeout_nanoseconds(timeout));
    switch (result)
    {
        case VK_TIMEOUT:
            return WaitStatus::TIMEOUT;

        // A swapchain to recreate waits no longer: the next acquisition or presentation reports it.
        case VK_SUCCESS:
        case VK_SUBOPTIMAL_KHR:
        case VK_ERROR_OUT_OF_DATE_KHR:
        case VK_ERROR_SURFACE_LOST_KHR:
            return WaitStatus::SIGNALED;
        default:
            m_device.check_result(result, OPERATION, "vkWaitForPresentKHR");
            return WaitStatus::SIGNALED;
    }
}

void
VulkanSwapchain::set_hdr_metadata(const HdrMetadata& metadata)
{
    if (!m_device.has_hdr_metadata())
    {
        throw_unsupported("Swapchain::set_hdr_metadata", "HDR metadata",
                          std::format("'{}' does not support VK_EXT_hdr_metadata", m_device.vulkan_physical_device().properties().name),
                          "present HDR content without metadata (the display assumes its defaults)");
    }

    // Kept for the swapchains that resize() creates.
    m_hdr_metadata = metadata;
    apply_hdr_metadata();
}

// ---------------------------------------------------------------------------------------------------
// VulkanDevice
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::create_swapchain(const SwapchainDescription& description) -> std::unique_ptr<Swapchain>
{
    return std::make_unique<VulkanSwapchain>(*this, description);
}

} // namespace gfx::vulkan
