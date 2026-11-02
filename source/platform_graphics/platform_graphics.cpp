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
    , m_released_semaphores{std::make_shared<std::atomic<std::uint64_t>>(0)}
    , m_swapchain{device.create_swapchain(swapchain_description_for(window, base))}
{
    // Room for every acquisition semaphore there may be: returning them never allocates.
    m_acquire_semaphores.reserve(MAXIMUM_ACQUISITION_SEMAPHORES);
    m_free_acquire_semaphores.reserve(MAXIMUM_ACQUISITION_SEMAPHORES);
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
        const std::size_t semaphore = free_acquisition_semaphore();
        const std::expected<AcquiredImage, SwapchainStatus> acquired =
            m_swapchain->acquire_next_image(m_acquire_semaphores[semaphore]->handle(), {}, timeout);
        if (acquired)
        {
            // The frame holds the semaphore until present() hands it to the deleter.
            m_free_acquire_semaphores.pop_back();
            m_image_acquire_semaphore[acquired->index] = semaphore;
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
    // The acquisition semaphore is free again once every submission made so far, among them the rendering
    // that waits on it, has completed (the image coming back later says nothing about that rendering).
    // Handed over before presenting: a presentation that fails changes nothing about the rendering.
    if (frame.image.index < m_image_acquire_semaphore.size())
    {
        std::size_t& held = m_image_acquire_semaphore[frame.image.index];
        if (held != NO_SEMAPHORE && m_acquire_semaphores[held]->handle() == frame.acquired)
        {
            m_device.deleter().enqueue([released = m_released_semaphores, bit = std::uint64_t{1} << held]() noexcept
                                       { released->fetch_or(bit, std::memory_order_release); });
            ++m_pending_semaphores;
            held = NO_SEMAPHORE;
        }
    }

    // The image, once its rendering semaphore is signaled.
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

    // The device is idle (recreation waited for it): the rendering semaphores are unsignaled; acquisition
    // semaphores still pending come back through the deleter as before.
    prepare_semaphores();
}

void
WindowSwapchain::prepare_semaphores()
{
    // One acquisition semaphore per image plus one (the next acquisition's) to start with, one rendering
    // semaphore per image.
    const std::size_t image_count = m_swapchain->image_count();
    while (m_acquire_semaphores.size() < std::min(image_count + 1, MAXIMUM_ACQUISITION_SEMAPHORES))
    {
        add_acquisition_semaphore();
    }
    while (m_rendered_semaphores.size() < image_count)
    {
        m_rendered_semaphores.push_back(
            m_device.create_semaphore({.debug_name = std::format("swapchain rendering {}", m_rendered_semaphores.size())}));
    }
    m_image_acquire_semaphore.assign(image_count, NO_SEMAPHORE);
}

auto
WindowSwapchain::free_acquisition_semaphore() -> std::size_t
{
    // Those the deleter returned; when none is free, those whose frames the GPU finished since the deleter
    // was last collected.
    reclaim_released_semaphores();
    if (m_free_acquire_semaphores.empty() && m_pending_semaphores > 0)
    {
        (void)m_device.deleter().collect();
        reclaim_released_semaphores();
    }

    // The GPU is further behind than ever before: one more semaphore (the pool never shrinks, so this
    // allocates only then).
    if (m_free_acquire_semaphores.empty() && m_acquire_semaphores.size() < MAXIMUM_ACQUISITION_SEMAPHORES)
    {
        add_acquisition_semaphore();
    }

    // Every semaphore is pending: the device finishes its work. A deletion another thread took from the
    // deleter before the flush may still be running: until it has set its bit, flush again.
    while (m_free_acquire_semaphores.empty() && m_pending_semaphores > 0)
    {
        m_device.deleter().flush();
        reclaim_released_semaphores();
    }
    if (m_free_acquire_semaphores.empty())
    {
        throw InvalidUsageError({
            .backend = m_device.backend(),
            .operation = "WindowSwapchain::acquire",
            .problem = std::format("each of the {} acquisition semaphores belongs to a frame acquired and never presented",
                                   m_acquire_semaphores.size()),
            .suggestion = "present every frame acquire() returns with WindowSwapchain::present()",
        });
    }
    return m_free_acquire_semaphores.back();
}

void
WindowSwapchain::reclaim_released_semaphores() noexcept
{
    // One bit per semaphore a deletion returned (on whichever thread collected the deleter).
    std::uint64_t released = m_released_semaphores->exchange(0, std::memory_order_acquire);
    while (released != 0)
    {
        m_free_acquire_semaphores.push_back(static_cast<std::size_t>(std::countr_zero(released)));
        released &= released - 1;
        --m_pending_semaphores;
    }
}

void
WindowSwapchain::add_acquisition_semaphore()
{
    std::unique_ptr<Semaphore> semaphore =
        m_device.create_semaphore({.debug_name = std::format("swapchain acquisition {}", m_acquire_semaphores.size())});
    m_acquire_semaphores.push_back(std::move(semaphore));
    m_free_acquire_semaphores.push_back(m_acquire_semaphores.size() - 1);
}

} // namespace gfx::platform
