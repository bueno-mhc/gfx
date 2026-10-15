/// @file queue.cppm
/// @brief Queue: executes submitted command buffers; graphics, compute, transfer and sparse binding
///        queues are first-class and run concurrently.
export module gfx.interface:queue;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :synchronization;
import :sparse_binding;

export namespace gfx
{

/// @brief One batch of a submission: command buffers with the semaphores they wait on and signal.
struct SubmitDescription
{
    std::span<const CommandBufferHandle> command_buffers{};          ///< Executed in order.
    std::span<const SemaphoreWait> waits{};                          ///< Binary semaphores waited on.
    std::span<const TimelineSemaphoreWait> timeline_waits{};         ///< Timeline values waited on.
    std::span<const SemaphoreSignal> signals{};                      ///< Binary semaphores signaled.
    std::span<const TimelineSemaphoreSignal> timeline_signals{};     ///< Timeline values signaled.
};

/// @brief Presentation of a swapchain image.
struct PresentDescription
{
    SwapchainHandle swapchain{};               ///< Swapchain.
    std::uint32_t image_index = 0;             ///< Image acquired with Swapchain::acquire_next_image().
    std::span<const SemaphoreHandle> waits{};  ///< Semaphores signaled by the rendering submission.
};

/// @brief A device queue. submit() never waits for the GPU: it returns as soon as the work is queued.
///        Submissions to one queue are serialized internally; submissions to different queues proceed
///        in parallel from different threads.
class Queue : public BackendObject
{
public:
    /// @brief Handle naming this queue.
    [[nodiscard]] virtual auto handle() const noexcept -> QueueHandle = 0;

    /// @brief Type it was requested as.
    [[nodiscard]] virtual auto type() const noexcept -> QueueType = 0;

    /// @brief Queue family index of the backend (queues of one family share command pools).
    [[nodiscard]] virtual auto family_index() const noexcept -> std::uint32_t = 0;

    /// @brief Work the queue runs (at least queue_type_capabilities(type())).
    [[nodiscard]] virtual auto capabilities() const noexcept -> QueueCapability = 0;

    /// @brief Queues command buffers for execution.
    /// @param batches Batches executed in order, each waiting on and signaling its semaphores.
    /// @param fence   Fence signaled when every batch completes (null handle = none).
    virtual void submit(std::span<const SubmitDescription> batches, FenceHandle fence = {}) = 0;

    /// @brief Presents a swapchain image once the semaphores in @p presentation are signaled.
    /// @return SwapchainStatus::OPTIMAL, or the reason the swapchain should (SUBOPTIMAL) or must
    ///         (OUT_OF_DATE, SURFACE_LOST) be recreated.
    [[nodiscard]] virtual auto present(const PresentDescription& presentation) -> SwapchainStatus = 0;

    /// @brief Blocks until every submission to this queue completes.
    virtual void wait_idle() = 0;

    /// @brief Binds or unbinds memory of sparse resources (queues with QueueCapability::SPARSE_BINDING).
    virtual void bind_sparse(const SparseBindDescription& description) = 0;
};

void
Queue::submit(std::span<const SubmitDescription> /*batches*/,
              FenceHandle /*fence*/)
{
    throw_not_implemented("Queue::submit");
}

auto
Queue::present(const PresentDescription& /*presentation*/) -> SwapchainStatus
{
    throw_not_implemented("Queue::present");
}

void
Queue::wait_idle()
{
    throw_not_implemented("Queue::wait_idle");
}

void
Queue::bind_sparse(const SparseBindDescription& /*description*/)
{
    throw_not_implemented("Queue::bind_sparse");
}

} // namespace gfx
