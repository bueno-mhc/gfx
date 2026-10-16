/// @file platform_graphics.cpp
/// @brief swapchain_description_for() and WindowSwapchain.
module gfx.platform.graphics;

import std;
import gfx;
import gfx.platform;

namespace gfx::platform
{

auto
swapchain_description_for(const Window& window,
                          SwapchainDescription base) noexcept -> SwapchainDescription
{
    base.window = window.native_handle();
    base.extent = window.framebuffer_size();
    return base;
}

WindowSwapchain::WindowSwapchain(Device& device,
                                 const Window& window,
                                 SwapchainDescription base)
    : m_device{device}
    , m_window{window}
    , m_description{base}
    , m_swapchain{device.create_swapchain(swapchain_description_for(window, base))}
{
    prepare_semaphores();
}

WindowSwapchain::~WindowSwapchain()
{
    // The swapchain goes first: its destruction waits until the device no longer uses the semaphores.
    m_swapchain.reset();
}

void
WindowSwapchain::handle_event(const Event& event) noexcept
{
    // A framebuffer that now differs from the images (resizing back to the images' size needs nothing).
    if (requires_swapchain_recreation(event, m_window.handle()))
    {
        const Extent size = event.get_if<WindowEvent>()->framebuffer_size;
        const Extent images = m_swapchain->extent();
        m_recreation_requested = m_recreation_requested || size.width != images.width || size.height != images.height;
    }
}

void
WindowSwapchain::request_recreation() noexcept
{
    m_recreation_requested = true;
}

auto
WindowSwapchain::acquire(std::chrono::nanoseconds timeout) -> std::optional<SwapchainFrame>
{
    // Nothing is presented while the window is minimized; the swapchain follows when it comes back.
    if (m_window.is_minimized())
    {
        return std::nullopt;
    }

    // At most two attempts: an out-of-date swapchain is recreated once per acquisition.
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        // A requested recreation, or a swapchain without images (the window had no area).
        if (m_recreation_requested || m_surface_lost || m_swapchain->image_count() == 0)
        {
            recreate();
        }
        if (m_swapchain->image_count() == 0)
        {
            return std::nullopt;
        }

        // The image, signaling a free acquisition semaphore.
        const std::size_t semaphore = m_free_acquire_semaphores.back();
        const std::expected<AcquiredImage, SwapchainStatus> acquired =
            m_swapchain->acquire_next_image(m_acquire_semaphores[semaphore]->handle(), {}, timeout);
        if (acquired)
        {
            // The semaphore of the image's previous acquisition is free again: the image came back, so
            // its presentation, and the rendering that waited on that semaphore, are over.
            m_free_acquire_semaphores.pop_back();
            std::size_t& held = m_image_acquire_semaphore[acquired->index];
            if (held != NO_SEMAPHORE)
            {
                m_free_acquire_semaphores.push_back(held);
            }
            held = semaphore;
            m_recreation_requested = m_recreation_requested || acquired->suboptimal;
            return SwapchainFrame{
                .image = *acquired,
                .acquired = m_acquire_semaphores[semaphore]->handle(),
                .rendered = m_rendered_semaphores[acquired->index]->handle(),
            };
        }

        // No image: the semaphore stays unsignaled and free.
        switch (acquired.error())
        {
            case SwapchainStatus::TIMEOUT:
                return std::nullopt;
            case SwapchainStatus::SURFACE_LOST:
                m_surface_lost = true;
                break;
            case SwapchainStatus::OPTIMAL:
            case SwapchainStatus::SUBOPTIMAL:
            case SwapchainStatus::OUT_OF_DATE:
                m_recreation_requested = true;
                break;
        }
    }
    return std::nullopt;
}

auto
WindowSwapchain::present(Queue& queue,
                         const SwapchainFrame& frame) -> SwapchainStatus
{
    const std::array waits{frame.rendered};
    const SwapchainStatus status = queue.present({
        .swapchain = m_swapchain->handle(),
        .image_index = frame.image.index,
        .waits = waits,
    });
    m_recreation_requested = m_recreation_requested || requires_swapchain_recreation(status);
    m_surface_lost = m_surface_lost || status == SwapchainStatus::SURFACE_LOST;
    return status;
}

void
WindowSwapchain::recreate()
{
    // A new swapchain when the surface is gone; otherwise the images follow the framebuffer's size.
    if (m_surface_lost)
    {
        m_swapchain.reset();
        m_swapchain = m_device.create_swapchain(swapchain_description_for(m_window, m_description));
        m_surface_lost = false;
    }
    else
    {
        m_swapchain->resize(m_window.framebuffer_size());
    }
    m_recreation_requested = false;
    ++m_recreation_count;

    // The device is idle (recreation waited for it): every semaphore is unsignaled and free.
    prepare_semaphores();
}

void
WindowSwapchain::prepare_semaphores()
{
    // One acquisition semaphore per image plus one (the next acquisition's), one rendering semaphore per image.
    const std::size_t image_count = m_swapchain->image_count();
    while (m_acquire_semaphores.size() < image_count + 1)
    {
        m_acquire_semaphores.push_back(
            m_device.create_semaphore({.debug_name = std::format("swapchain acquisition {}", m_acquire_semaphores.size())}));
    }
    while (m_rendered_semaphores.size() < image_count)
    {
        m_rendered_semaphores.push_back(
            m_device.create_semaphore({.debug_name = std::format("swapchain rendering {}", m_rendered_semaphores.size())}));
    }
    m_free_acquire_semaphores.resize(m_acquire_semaphores.size());
    std::ranges::iota(m_free_acquire_semaphores, std::size_t{0});
    m_image_acquire_semaphore.assign(image_count, NO_SEMAPHORE);
}

} // namespace gfx::platform
