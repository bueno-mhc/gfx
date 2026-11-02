/// @file platform_graphics.cppm
/// @brief gfx.platform.graphics: the bridge between the platform library's windows and the graphics
///        library's swapchains - the SwapchainDescription of a window, the rules that say when a swapchain
///        must be recreated, and WindowSwapchain, which applies them and manages the presentation
///        semaphores. Built when both libraries are; neither library depends on it.
export module gfx.platform.graphics;

import std;
export import gfx;
export import gfx.platform;

export namespace gfx::platform
{

/// @brief The SwapchainDescription of @p window: its native handle and framebuffer size (pixels), the
///        other members from @p base. Create the window with WindowDescription::surface_api set to the
///        graphics API (SurfaceApi::VULKAN) so that the backend prepares it.
///
/// @code
/// std::unique_ptr<gfx::Swapchain> swapchain = device->create_swapchain(gfx::platform::swapchain_description_for(*window));
/// @endcode
[[nodiscard]] auto swapchain_description_for(const Window& window,
                                             SwapchainDescription base = {}) noexcept -> SwapchainDescription;

/// @brief Whether @p event requires recreating the swapchain that presents to @p window: its framebuffer
///        was resized (minimizing included).
[[nodiscard]] constexpr auto
requires_swapchain_recreation(const Event& event,
                              WindowHandle window) noexcept -> bool
{
    return event.window() == window && event.type() == EventType::WINDOW_FRAMEBUFFER_RESIZED;
}

/// @brief Whether a swapchain that reported @p status (by Swapchain::acquire_next_image() or
///        Queue::present()) must be recreated before its next image: SUBOPTIMAL or OUT_OF_DATE.
///        SURFACE_LOST requires a new swapchain (the window's surface is gone).
[[nodiscard]] constexpr auto
requires_swapchain_recreation(SwapchainStatus status) noexcept -> bool
{
    return status == SwapchainStatus::SUBOPTIMAL || status == SwapchainStatus::OUT_OF_DATE;
}

/// @brief The image of one frame of a WindowSwapchain, with the semaphores that order its rendering
///        between the acquisition and the presentation.
struct SwapchainFrame
{
    AcquiredImage image{};      ///< Index, image and color view (ResourceState::UNDEFINED when acquired).
    SemaphoreHandle acquired{}; ///< Signaled when the image may be written: the rendering submission waits on it.
    SemaphoreHandle rendered{}; ///< The rendering submission signals it; WindowSwapchain::present() waits on it.
};

/// @brief A swapchain that follows its window: recreated when the framebuffer is resized or the
///        swapchain reports itself out of date (or suboptimal), skipped while the window is minimized,
///        and replaced when the window's surface is lost. It owns the binary semaphores of presentation:
///        acquisition semaphores (one per image plus one to start with), each free again once the GPU has
///        finished the frame that waited on it, and one rendering semaphore per image.
///
/// Every frame acquire() returns must be rendered by a submission that waits on SwapchainFrame::acquired
/// and signals SwapchainFrame::rendered, then presented with present(). Recreation waits for the device
/// to be idle. Use it on one thread (the swapchain is externally synchronized).
///
/// An acquisition semaphore may be given to the presentation engine again only once the submission that
/// waited on it has completed, and an image coming back from the presentation engine does not tell that:
/// an image can come back the frame after it was presented, before the GPU ran the rendering of that frame
/// (seen with FIFO on a laptop's discrete GPU while other work kept it busy). So present() hands the frame's
/// acquisition semaphore to the device's Deleter, which returns it once every submission made so far has
/// completed; acquire() collects the deleter when no semaphore is free, creates another one when the GPU
/// is further behind than ever before, and waits for the device beyond 64.
///
/// @code
/// gfx::platform::WindowSwapchain presentation{*device, *window};
/// for (const gfx::platform::Event& event : platform->poll_events())
/// {
///     presentation.handle_event(event);
/// }
/// if (const std::optional<gfx::platform::SwapchainFrame> frame = presentation.acquire())
/// {
///     // record: image_barrier(frame->image.image, UNDEFINED, COLOR_ATTACHMENT_WRITE), render into
///     // frame->image.view, image_barrier(frame->image.image, COLOR_ATTACHMENT_WRITE, PRESENT)
///     const std::array waits{gfx::SemaphoreWait{.semaphore = frame->acquired}}; // every stage waits
///     const std::array signals{gfx::SemaphoreSignal{.semaphore = frame->rendered}};
///     queue.submit(std::array{gfx::SubmitDescription{.command_buffers = commands, .waits = waits, .signals = signals}});
///     presentation.present(queue, *frame);
/// }
/// @endcode
class WindowSwapchain
{
public:
    /// @brief Creates the swapchain of @p window (swapchain_description_for(window, base)).
    /// @throws BackendCapabilityError when the device cannot present to the window or lacks a requested
    ///         format, color space or presentation mode (PhysicalDevice::surface_support() lists them).
    WindowSwapchain(Device& device,
                    const Window& window,
                    SwapchainDescription base = {});
    ~WindowSwapchain();
    WindowSwapchain(const WindowSwapchain&) = delete;
    auto operator=(const WindowSwapchain&) -> WindowSwapchain& = delete;

    /// @brief Notes the events that require recreating the swapchain (a framebuffer size that differs
    ///        from the images'); pass every event of the pump.
    void handle_event(const Event& event) noexcept;

    /// @brief Recreates the swapchain before the next acquisition.
    void request_recreation() noexcept;

    /// @brief The next image, after recreating the swapchain when needed.
    /// @param timeout Maximum wait for an image.
    /// @return The frame, or std::nullopt while the window is minimized or has no area, or when no
    ///         image became available before @p timeout.
    [[nodiscard]] auto acquire(std::chrono::nanoseconds timeout = WAIT_INDEFINITELY) -> std::optional<SwapchainFrame>;

    /// @brief Presents @p frame on @p queue once its rendering semaphore is signaled, and notes when the
    ///        swapchain must be recreated. The frame's acquisition semaphore is free again once the GPU has
    ///        finished everything submitted before this call (its rendering included).
    /// @return The status Queue::present() reported.
    auto present(Queue& queue,
                 const SwapchainFrame& frame) -> SwapchainStatus;

    /// @brief The swapchain (replaced when the window's surface is lost).
    [[nodiscard]] auto swapchain() noexcept -> Swapchain&
    {
        return *m_swapchain;
    }

    /// @brief Size of the images (0 x 0 while the window has no area).
    [[nodiscard]] auto extent() const noexcept -> Extent
    {
        return m_swapchain->extent();
    }

    /// @brief Format of the images.
    [[nodiscard]] auto format() const noexcept -> Format
    {
        return m_swapchain->format();
    }

    /// @brief How many times the swapchain was recreated (resizes and replacements).
    [[nodiscard]] auto recreation_count() const noexcept -> std::uint32_t
    {
        return m_recreation_count;
    }

private:
    /// @brief Recreates the swapchain for the window's framebuffer size (a new one when the surface was
    ///        lost) and prepares the semaphores of its images.
    void recreate();

    /// @brief Makes enough semaphores for the swapchain's images: one acquisition semaphore per image plus
    ///        one at least (those that exist keep their state), one rendering semaphore per image.
    void prepare_semaphores();

    /// @brief Makes sure an acquisition semaphore is free: one the deleter returned, else one more while
    ///        fewer than MAXIMUM_ACQUISITION_SEMAPHORES exist, else one the device returns once idle.
    /// @return The free semaphore (the last of m_free_acquire_semaphores).
    /// @throws InvalidUsageError when every semaphore belongs to a frame acquired and never presented.
    [[nodiscard]] auto free_acquisition_semaphore() -> std::size_t;

    /// @brief Moves the acquisition semaphores the deleter returned to the free ones.
    void reclaim_released_semaphores() noexcept;

    /// @brief Creates a free acquisition semaphore.
    void add_acquisition_semaphore();

private:
    static constexpr std::size_t NO_SEMAPHORE = std::numeric_limits<std::size_t>::max();
    /// @brief Acquisition semaphores at most: one bit each in the released mask.
    static constexpr std::size_t MAXIMUM_ACQUISITION_SEMAPHORES = 64;

    Device& m_device;
    const Window& m_window;
    SwapchainDescription m_description;
    std::vector<std::unique_ptr<Semaphore>> m_acquire_semaphores;
    std::vector<std::unique_ptr<Semaphore>> m_rendered_semaphores;
    std::vector<std::size_t> m_free_acquire_semaphores; ///< Acquisition semaphores no frame holds (capacity for all of them).
    std::vector<std::size_t> m_image_acquire_semaphore; ///< Per image: the acquisition semaphore of its frame not presented yet.
    /// @brief One bit per acquisition semaphore the deleter returned: set by deletions on whichever thread
    ///        collects the deleter, shared with them so that they outlive this object.
    std::shared_ptr<std::atomic<std::uint64_t>> m_released_semaphores;
    std::size_t m_pending_semaphores = 0; ///< Acquisition semaphores presented, not returned yet.
    bool m_recreation_requested = false;
    bool m_surface_lost = false;
    std::uint32_t m_recreation_count = 0;
    std::unique_ptr<Swapchain> m_swapchain; ///< Destroyed first: it waits for the device to be idle.
};

} // namespace gfx::platform
