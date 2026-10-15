/// @file swapchain.cppm
/// @brief Swapchain: the images presented to a window (the window surface is part of it).
export module gfx.interface:swapchain;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :common_types;

export namespace gfx
{

/// @brief How to create a Swapchain.
struct SwapchainDescription
{
    NativeWindowHandle window{};                          ///< Window presented to (Window::native_handle() of the platform library).
    Extent extent{};                                      ///< Image size (usually the window's client size).
    Format format = Format::B8G8R8A8_SRGB;                ///< Image format.
    ColorSpace color_space = ColorSpace::SRGB_NONLINEAR;  ///< Color space.
    PresentMode present_mode = PresentMode::FIFO;         ///< Presentation mode.
    std::uint32_t minimum_image_count = 3;                ///< Images in flight.
    ImageUsage usage = ImageUsage::COLOR_ATTACHMENT;      ///< Usage of the images.
    QueueType present_queue = QueueType::GRAPHICS;        ///< Queue type that presents.
    std::uint32_t maximum_frame_latency = 2;              ///< Frames queued for presentation before
                                                          ///< wait_for_frame_latency() blocks (input latency).
    std::string_view debug_name{};                        ///< Name shown by debugging tools.
};

/// @brief Mastering display and content light levels of HDR output (HDR10 metadata).
struct HdrMetadata
{
    std::array<float, 2> display_primary_red{0.708F, 0.292F};   ///< CIE 1931 xy chromaticity.
    std::array<float, 2> display_primary_green{0.170F, 0.797F}; ///< CIE 1931 xy chromaticity.
    std::array<float, 2> display_primary_blue{0.131F, 0.046F};  ///< CIE 1931 xy chromaticity.
    std::array<float, 2> white_point{0.3127F, 0.3290F};          ///< CIE 1931 xy chromaticity.
    float maximum_luminance = 1000.0F;                           ///< Nits.
    float minimum_luminance = 0.001F;                            ///< Nits.
    float maximum_content_light_level = 1000.0F;                 ///< Nits.
    float maximum_frame_average_light_level = 400.0F;            ///< Nits.
};

/// @brief An image acquired from a swapchain, to render into and then present.
struct AcquiredImage
{
    std::uint32_t index = 0;   ///< Image index (passed to Queue::present()).
    ImageHandle image{};       ///< The image.
    ImageViewHandle view{};    ///< A color view of the image.
    bool suboptimal = false;   ///< Usable, but the swapchain should be recreated.
};

/// @brief Images presented to a window. Acquire an image, render into it (waiting on the acquisition
///        semaphore), then present it (waiting on the rendering semaphore).
class Swapchain : public BackendObject
{
public:
    /// @brief Handle naming this swapchain in presentations.
    [[nodiscard]] virtual auto handle() const noexcept -> SwapchainHandle = 0;

    /// @brief Size of the images.
    [[nodiscard]] virtual auto extent() const noexcept -> Extent = 0;

    /// @brief Format of the images.
    [[nodiscard]] virtual auto format() const noexcept -> Format = 0;

    /// @brief Number of images.
    [[nodiscard]] virtual auto image_count() const noexcept -> std::uint32_t = 0;

    /// @brief Acquires the next image to render into.
    /// @param signal  Semaphore signaled when the image may be written.
    /// @param fence   Fence signaled at the same time (null handle = none).
    /// @param timeout Maximum wait for an image.
    /// @return The image, or the status explaining why none was acquired (OUT_OF_DATE, SURFACE_LOST).
    [[nodiscard]] virtual auto acquire_next_image(SemaphoreHandle signal,
                                                  FenceHandle fence = {},
                                                  std::chrono::nanoseconds timeout = WAIT_INDEFINITELY)
        -> std::expected<AcquiredImage, SwapchainStatus> = 0;

    /// @brief Recreates the images with a new size (after the window was resized).
    virtual void resize(Extent extent) = 0;

    /// @brief Blocks until fewer than SwapchainDescription::maximum_frame_latency frames wait for
    ///        presentation: call it before sampling input, to keep latency low (DXGI frame latency
    ///        waitable object, VK_KHR_present_wait).
    [[nodiscard]] virtual auto wait_for_frame_latency(std::chrono::nanoseconds timeout = WAIT_INDEFINITELY) -> WaitStatus = 0;

    /// @brief Describes the HDR content presented (HDR color spaces).
    virtual void set_hdr_metadata(const HdrMetadata& metadata) = 0;
};

auto
Swapchain::acquire_next_image(SemaphoreHandle /*signal*/,
                              FenceHandle /*fence*/,
                              std::chrono::nanoseconds /*timeout*/) -> std::expected<AcquiredImage, SwapchainStatus>
{
    throw_not_implemented("Swapchain::acquire_next_image");
}

void
Swapchain::resize(Extent /*extent*/)
{
    throw_not_implemented("Swapchain::resize");
}

auto
Swapchain::wait_for_frame_latency(std::chrono::nanoseconds /*timeout*/) -> WaitStatus
{
    throw_not_implemented("Swapchain::wait_for_frame_latency");
}

void
Swapchain::set_hdr_metadata(const HdrMetadata& /*metadata*/)
{
    throw_not_implemented("Swapchain::set_hdr_metadata");
}

} // namespace gfx
