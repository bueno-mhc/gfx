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
///
/// **Order of waits and signals.** A queue runs its submissions in order: a submission waiting for a
/// timeline value holds back every later submission to that queue until the value is reached. A
/// submission may wait for a value nothing has signaled yet (the CPU or another queue signals it
/// later), but that value must not be left to a later submission to the same hardware queue: its
/// signal would wait behind the wait forever (GPU deadlock). Backends refuse the certain case, a later
/// submission signaling exactly the value waited for; a later signal of a greater value is accepted,
/// since a smaller value from elsewhere may release the wait first (if nothing does, the GPU stalls).
/// Several queues may run on one hardware queue when a device has fewer queues than were requested
/// (e.g. a GPU with a single queue family of one queue runs the graphics, compute and transfer queues
/// on it); the rule then spans them. Submitting the signaling work before the work that waits for it
/// is always safe.
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
    /// @throws InvalidUsageError (at ValidationLevel::CREATION and above) when a batch signals exactly the
    ///         timeline value an earlier submission to this queue waits for, which nothing else has
    ///         signaled or promised: the signal could never run (see "Order of waits and signals" above).
    /// @throws BackendCapabilityError (at ValidationLevel::CREATION and above) when that earlier wait
    ///         comes from another queue running on the same hardware queue.
    virtual void submit(std::span<const SubmitDescription> batches, FenceHandle fence = {}) = 0;

    /// @brief Presents a swapchain image once the semaphores in @p presentation are signaled.
    /// @return SwapchainStatus::OPTIMAL, or the reason the swapchain should (SUBOPTIMAL) or must
    ///         (OUT_OF_DATE, SURFACE_LOST) be recreated.
    [[nodiscard]] virtual auto present(const PresentDescription& presentation) -> SwapchainStatus = 0;

    /// @brief Blocks until every submission to this queue completes.
    virtual void wait_idle() = 0;

    /// @brief Binds or unbinds memory of sparse resources (queues with QueueCapability::SPARSE_BINDING).
    /// @throws InvalidUsageError, BackendCapabilityError as submit() for an order of timeline waits and
    ///         signals that would deadlock the hardware queue.
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
