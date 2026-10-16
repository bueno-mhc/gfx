/// @file vulkan_queue.cpp
/// @brief VulkanQueue (submission with binary and timeline semaphores, progress tracking) and
///        VulkanDeleter (deferred destruction keyed on every queue's progress).
module;

#include <volk.h>

module gfx.backends.vulkan;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import gfx.interface;
import gfx.core.handle_table;
import :common;
import :translation;
import :instance;
import :device;

namespace gfx::vulkan
{

// ---------------------------------------------------------------------------------------------------
// VulkanQueue
// ---------------------------------------------------------------------------------------------------

VulkanQueue::VulkanQueue(VulkanDevice& device,
                         NativeQueue& native,
                         QueueType type,
                         std::uint32_t index)
    : Queue{}
    , m_device{device}
    , m_native{native}
    , m_type{type}
    , m_debug_name{std::format("{} queue {}", type, index)}
{
    set_validation_level(device.validation_level());
    m_handle = device.handles().queues.insert({.queue = this});
    device.name_object(VK_OBJECT_TYPE_QUEUE, native.queue, m_debug_name);
}

VulkanQueue::~VulkanQueue()
{
    (void)m_device.handles().queues.remove(m_handle);
}

auto
VulkanQueue::backend() const noexcept -> BackendKind
{
    return BackendKind::VULKAN;
}

auto
VulkanQueue::debug_name() const noexcept -> std::string_view
{
    return m_debug_name;
}

void
VulkanQueue::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    m_device.name_object(VK_OBJECT_TYPE_QUEUE, m_native.queue, m_debug_name);
}

auto
VulkanQueue::handle() const noexcept -> QueueHandle
{
    return m_handle;
}

auto
VulkanQueue::type() const noexcept -> QueueType
{
    return m_type;
}

auto
VulkanQueue::family_index() const noexcept -> std::uint32_t
{
    return m_native.family;
}

auto
VulkanQueue::capabilities() const noexcept -> QueueCapability
{
    return m_native.capabilities;
}

void
VulkanQueue::submit(std::span<const SubmitDescription> batches,
                    FenceHandle fence)
{
    static constexpr std::string_view OPERATION = "Queue::submit";
    HandleTables& handles = m_device.handles();
    const VkPipelineStageFlags2 supported_stages = m_device.supported_stages();
    const VkFence native_fence = fence.is_valid() ? handles.fences.get(fence, OPERATION).fence : VK_NULL_HANDLE;

    {
        const std::scoped_lock lock{m_native.mutex};

        // Refuse an order of timeline waits and signals that would deadlock the hardware queue.
        const bool ordered = validates(ValidationLevel::CREATION);
        std::vector<UnresolvedWait> unresolved;
        if (ordered)
        {
            m_timeline_batches.clear();
            for (const SubmitDescription& batch : batches)
            {
                m_timeline_batches.push_back({.waits = batch.timeline_waits, .signals = batch.timeline_signals});
            }
            unresolved = order_timeline_batches(m_timeline_batches, OPERATION);
        }

        // Sizes first: the arrays must not reallocate while submit informations point into them.
        std::size_t command_buffer_count = 0;
        std::size_t wait_count = 0;
        std::size_t signal_count = 1;
        for (const SubmitDescription& batch : batches)
        {
            command_buffer_count += batch.command_buffers.size();
            wait_count += batch.waits.size() + batch.timeline_waits.size();
            signal_count += batch.signals.size() + batch.timeline_signals.size();
        }
        m_submits.clear();
        m_command_buffers.clear();
        m_waits.clear();
        m_signals.clear();
        m_submits.reserve(std::max<std::size_t>(batches.size(), 1));
        m_command_buffers.reserve(command_buffer_count);
        m_waits.reserve(wait_count);
        m_signals.reserve(signal_count);

        // Every batch: its command buffers (recorded, primary, for a queue that can run them) and semaphores.
        const std::uint64_t progress = m_native.submitted.load(std::memory_order_relaxed) + 1;
        for (std::size_t index = 0; index < batches.size(); ++index)
        {
            const SubmitDescription& batch = batches[index];
            const std::size_t first_command_buffer = m_command_buffers.size();
            const std::size_t first_wait = m_waits.size();
            const std::size_t first_signal = m_signals.size();
            for (const CommandBufferHandle handle : batch.command_buffers)
            {
                const CommandBufferRecord& record = handles.command_buffers.get(handle, OPERATION);
                if (validates(ValidationLevel::CREATION))
                {
                    if (record.level != CommandBufferLevel::PRIMARY)
                    {
                        throw InvalidUsageError({
                            .backend = BackendKind::VULKAN,
                            .operation = std::string{OPERATION},
                            .problem = std::format("{} is a secondary command buffer", handle),
                            .suggestion = "submit primary command buffers; secondary ones run through "
                                          "ParallelRenderingScope::execute()",
                        });
                    }
                    if (!has_all(m_native.capabilities, queue_type_capabilities(record.queue_type)))
                    {
                        throw InvalidUsageError({
                            .backend = BackendKind::VULKAN,
                            .operation = std::string{OPERATION},
                            .problem = std::format("{} was recorded for a {} queue, which runs work '{}' cannot", handle,
                                                   record.queue_type, m_debug_name),
                            .suggestion = "submit it to a queue of its type",
                        });
                    }
                }
                if (!record.status->executable.load(std::memory_order_acquire))
                {
                    throw InvalidUsageError({
                        .backend = BackendKind::VULKAN,
                        .operation = std::string{OPERATION},
                        .problem = record.status->failure.empty()
                                       ? std::format("{} is not recorded (its Recorder has not been destroyed yet, or it "
                                                     "was reset)",
                                                     handle)
                                       : std::format("the recording of {} failed: {}", handle, record.status->failure),
                        .suggestion = "submit command buffers whose Recorder went out of scope",
                    });
                }
                m_command_buffers.push_back({.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                             .commandBuffer = record.command_buffer});
            }
            for (const SemaphoreWait& wait : batch.waits)
            {
                m_waits.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                   .semaphore = handles.semaphores.get(wait.semaphore, OPERATION).semaphore,
                                   .stageMask = to_vulkan(wait.stages) & supported_stages});
            }
            for (const TimelineSemaphoreWait& wait : batch.timeline_waits)
            {
                m_waits.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                   .semaphore = handles.timeline_semaphores.get(wait.semaphore, OPERATION).semaphore,
                                   .value = wait.value,
                                   .stageMask = to_vulkan(wait.stages) & supported_stages});
            }
            for (const SemaphoreSignal& signal : batch.signals)
            {
                m_signals.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                     .semaphore = handles.semaphores.get(signal.semaphore, OPERATION).semaphore,
                                     .stageMask = to_vulkan(signal.stages) & supported_stages});
            }
            for (const TimelineSemaphoreSignal& signal : batch.timeline_signals)
            {
                m_signals.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                     .semaphore = handles.timeline_semaphores.get(signal.semaphore, OPERATION).semaphore,
                                     .value = signal.value,
                                     .stageMask = to_vulkan(signal.stages) & supported_stages});
            }

            // The last batch also advances the queue's progress timeline.
            if (index + 1 == batches.size())
            {
                m_signals.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                     .semaphore = m_native.progress,
                                     .value = progress,
                                     .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT});
            }
            m_submits.push_back({
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                .waitSemaphoreInfoCount = static_cast<std::uint32_t>(m_waits.size() - first_wait),
                .pWaitSemaphoreInfos = m_waits.data() + first_wait,
                .commandBufferInfoCount = static_cast<std::uint32_t>(m_command_buffers.size() - first_command_buffer),
                .pCommandBufferInfos = m_command_buffers.data() + first_command_buffer,
                .signalSemaphoreInfoCount = static_cast<std::uint32_t>(m_signals.size() - first_signal),
                .pSignalSemaphoreInfos = m_signals.data() + first_signal,
            });
        }

        // No batch: an empty submission still signals the fence and the progress timeline.
        if (batches.empty())
        {
            m_signals.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                 .semaphore = m_native.progress,
                                 .value = progress,
                                 .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT});
            m_submits.push_back({.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                                 .signalSemaphoreInfoCount = 1,
                                 .pSignalSemaphoreInfos = m_signals.data()});
        }

        m_device.check_result(m_device.functions().vkQueueSubmit2(m_native.queue, static_cast<std::uint32_t>(m_submits.size()),
                                                                  m_submits.data(), native_fence),
                              OPERATION, "vkQueueSubmit2");
        m_native.submitted.store(progress, std::memory_order_release);

        // What the submission promised, and the waits the hardware queue may now be stalled on.
        if (ordered)
        {
            record_timeline_batches(m_timeline_batches, std::move(unresolved));
        }
    }

    // Deletions whose submissions completed meanwhile.
    m_device.vulkan_deleter().collect();
}

auto
VulkanQueue::order_timeline_batches(std::span<const TimelineBatch> batches,
                                    std::string_view operation) const -> std::vector<UnresolvedWait>
{
    HandleTables& handles = m_device.handles();

    // Whether a submission promised the value, or the GPU or the CPU already reached it.
    const auto reached = [&](const SemaphoreRecord& record,
                             std::uint64_t value)
    {
        if (record.timeline->submitted.load(std::memory_order_acquire) >= value)
        {
            return true;
        }
        std::uint64_t current = 0;
        m_device.check_result(m_device.functions().vkGetSemaphoreCounterValue(m_device.native(), record.semaphore, &current),
                              operation, "vkGetSemaphoreCounterValue");
        return current >= value;
    };

    // The waits the hardware queue may still be stalled on: those nothing satisfied or promised since.
    std::vector<UnresolvedWait> unresolved;
    for (const UnresolvedWait& wait : m_native.unresolved_waits)
    {
        const SemaphoreRecord* record = handles.timeline_semaphores.find(wait.semaphore);
        if (record != nullptr && !reached(*record, wait.value))
        {
            unresolved.push_back(wait);
        }
    }

    // The batches in submission order; in a batch, the waits come before the signals.
    std::vector<TimelineSemaphoreSignal> promised;
    for (const TimelineBatch& batch : batches)
    {
        // A wait that neither an earlier batch nor anything before satisfies stalls the hardware queue.
        for (const TimelineSemaphoreWait& wait : batch.waits)
        {
            const SemaphoreRecord& record = handles.timeline_semaphores.get(wait.semaphore, operation);
            const bool earlier_batch = std::ranges::any_of(promised, [&](const TimelineSemaphoreSignal& signal)
            {
                return signal.semaphore == wait.semaphore && signal.value >= wait.value;
            });
            if (!earlier_batch && !reached(record, wait.value))
            {
                unresolved.push_back({.semaphore = wait.semaphore, .value = wait.value, .queue = m_handle});
            }
        }

        // A signal of exactly the value such a wait waits for is the only thing that can release it (a
        // value reached elsewhere first would make this signal decrease the semaphore), yet it runs behind
        // the wait. A greater value is accepted: a smaller one from elsewhere may release the wait first.
        for (const TimelineSemaphoreSignal& signal : batch.signals)
        {
            const auto blocked = std::ranges::find_if(unresolved, [&](const UnresolvedWait& wait)
            {
                return wait.semaphore == signal.semaphore && wait.value == signal.value;
            });
            if (blocked != unresolved.end())
            {
                reject_deadlock(*blocked, signal, operation);
            }
            promised.push_back(signal);
        }
    }
    return unresolved;
}

void
VulkanQueue::reject_deadlock(const UnresolvedWait& wait,
                             const TimelineSemaphoreSignal& signal,
                             std::string_view operation) const
{
    // Name the semaphore, the value and the queues in the error.
    HandleTables& handles = m_device.handles();
    const SemaphoreRecord& record = handles.timeline_semaphores.get(signal.semaphore, operation);
    const std::string semaphore_name{record.timeline->semaphore->debug_name()};
    const QueueRecord* waiting = handles.queues.find(wait.queue);
    const std::string waiting_name = waiting != nullptr ? std::string{waiting->queue->debug_name()} : std::string{"a destroyed queue"};
    const std::string stall = std::format("'{}' waits, in an earlier submission, for timeline semaphore '{}' to reach {}, which "
                                          "nothing has signaled or promised to signal",
                                          waiting_name, semaphore_name, wait.value);

    // The same queue: the application ordered its own submissions wrongly.
    if (wait.queue == m_handle)
    {
        throw InvalidUsageError({
            .backend = BackendKind::VULKAN,
            .operation = std::string{operation},
            .problem = std::format("{}; this submission to the same queue signals exactly {}, so only it can release the "
                                   "wait, but a queue runs its submissions in order and the signal would wait behind the wait "
                                   "forever (GPU deadlock)",
                                   stall, signal.value),
            .suggestion = "submit the signaling work first, or reach the value from the CPU (TimelineSemaphore::signal) or "
                          "from another queue before submitting this",
        });
    }

    // Two queues on one hardware queue: waiting before the signal is submitted needs separate hardware.
    const std::uint32_t queue_count = m_device.physical_device().queue_families()[m_native.family].queue_count;
    throw BackendCapabilityError({
        .backend = BackendKind::VULKAN,
        .operation = std::string{operation},
        .capability = "a wait submitted before its signal on another queue",
        .reason = std::format("{}; this submission from '{}' signals exactly {}, so only it can release the wait, but both "
                              "queues run on the same hardware queue (queue {} of family {}, which has {} queue(s)) and it runs "
                              "submissions in order, so the signal would wait behind the wait forever (GPU deadlock)",
                              stall, m_debug_name, signal.value, m_native.index, m_native.family, queue_count),
        .suggestion = "submit the signaling work before the work that waits for it (always safe), or reach the value from "
                      "the CPU first; queues of one family share a hardware queue when more queues were requested than the "
                      "family has",
    });
}

void
VulkanQueue::record_timeline_batches(std::span<const TimelineBatch> batches,
                                     std::vector<UnresolvedWait> unresolved)
{
    // Raise every signaled semaphore's promised value.
    HandleTables& handles = m_device.handles();
    for (const TimelineBatch& batch : batches)
    {
        for (const TimelineSemaphoreSignal& signal : batch.signals)
        {
            std::atomic<std::uint64_t>& submitted = handles.timeline_semaphores.get(signal.semaphore, "Queue::submit").timeline->submitted;
            std::uint64_t previous = submitted.load(std::memory_order_relaxed);
            while (previous < signal.value &&
                   !submitted.compare_exchange_weak(previous, signal.value, std::memory_order_release, std::memory_order_relaxed))
            {
            }
        }
    }

    // The waits the hardware queue may now be stalled on.
    m_native.unresolved_waits = std::move(unresolved);
}

auto
VulkanQueue::present(const PresentDescription& presentation) -> SwapchainStatus
{
    static constexpr std::string_view OPERATION = "Queue::present";
    HandleTables& handles = m_device.handles();
    SwapchainState& state = *handles.swapchains.get(presentation.swapchain, OPERATION).state;

    // A swapchain without images (the window has no area) stays out of date until it is resized.
    if (state.swapchain == VK_NULL_HANDLE)
    {
        return SwapchainStatus::OUT_OF_DATE;
    }

    // This queue presents to the window, an image that exists.
    if (validates(ValidationLevel::CREATION))
    {
        const std::string_view swapchain_name = state.swapchain_object->debug_name();
        if (!state.presenting_families[m_native.family])
        {
            throw InvalidUsageError({
                .backend = BackendKind::VULKAN,
                .operation = std::string{OPERATION},
                .problem = std::format("{} queue '{}' (family {}) cannot present to the window of swapchain '{}'", m_type, m_debug_name,
                                       m_native.family, swapchain_name),
                .suggestion = "present from the queue type named by SwapchainDescription::present_queue",
            });
        }
        if (presentation.image_index >= state.image_count)
        {
            throw InvalidUsageError({
                .backend = BackendKind::VULKAN,
                .operation = std::string{OPERATION},
                .problem = std::format("image {} of swapchain '{}' does not exist; it has {} images", presentation.image_index,
                                       swapchain_name, state.image_count),
                .suggestion = "present the index returned by Swapchain::acquire_next_image()",
            });
        }
    }

    // The identifier of the presentation, for waits on it (Swapchain::wait_for_frame_latency()).
    const std::uint64_t identifier = state.presented + 1;
    const VkPresentIdKHR present_identifier{
        .sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR,
        .swapchainCount = 1,
        .pPresentIds = &identifier,
    };
    VkResult result = VK_SUCCESS;
    {
        const std::scoped_lock lock{m_native.mutex};

        // Binary semaphores the rendering signals.
        m_present_waits.clear();
        for (const SemaphoreHandle wait : presentation.waits)
        {
            m_present_waits.push_back(handles.semaphores.get(wait, OPERATION).semaphore);
        }
        const VkPresentInfoKHR information{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = state.present_identifiers ? &present_identifier : nullptr,
            .waitSemaphoreCount = static_cast<std::uint32_t>(m_present_waits.size()),
            .pWaitSemaphores = m_present_waits.data(),
            .swapchainCount = 1,
            .pSwapchains = &state.swapchain,
            .pImageIndices = &presentation.image_index,
        };
        result = m_device.functions().vkQueuePresentKHR(m_native.queue, &information);
    }

    // The semaphore waits run even when the presentation is refused (OUT_OF_DATE): the identifier is used.
    state.presented = identifier;
    switch (result)
    {
        case VK_SUCCESS:
            return SwapchainStatus::OPTIMAL;
        case VK_SUBOPTIMAL_KHR:
            return SwapchainStatus::SUBOPTIMAL;
        case VK_ERROR_OUT_OF_DATE_KHR:
            return SwapchainStatus::OUT_OF_DATE;
        case VK_ERROR_SURFACE_LOST_KHR:
            return SwapchainStatus::SURFACE_LOST;
        default:
            m_device.check_result(result, OPERATION, "vkQueuePresentKHR");
            return SwapchainStatus::OPTIMAL;
    }
}

void
VulkanQueue::wait_idle()
{
    // Waiting on the progress timeline leaves the queue free for other threads' submissions.
    const std::uint64_t submitted = m_native.submitted.load(std::memory_order_acquire);
    const VkSemaphoreWaitInfo wait{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &m_native.progress,
        .pValues = &submitted,
    };
    m_device.check_result(m_device.functions().vkWaitSemaphores(m_device.native(), &wait, std::numeric_limits<std::uint64_t>::max()),
                          "Queue::wait_idle", "vkWaitSemaphores");
    m_device.vulkan_deleter().collect();
}

// ---------------------------------------------------------------------------------------------------
// VulkanDeleter
// ---------------------------------------------------------------------------------------------------

VulkanDeleter::VulkanDeleter(VulkanDevice& device)
    : Deleter{}
    , m_device{device}
{
    set_validation_level(device.validation_level());
}

auto
VulkanDeleter::backend() const noexcept -> BackendKind
{
    return BackendKind::VULKAN;
}

auto
VulkanDeleter::debug_name() const noexcept -> std::string_view
{
    return m_debug_name;
}

void
VulkanDeleter::set_debug_name(std::string_view name)
{
    m_debug_name = name;
}

void
VulkanDeleter::enqueue(std::move_only_function<void()> deletion)
{
    // Remember how far every queue had submitted: once each completed that much, nothing submitted
    // before can still use what the deletion releases.
    Pending pending{.deletion = std::move(deletion), .submitted = {}};
    const std::span<const std::unique_ptr<NativeQueue>> queues = m_device.native_queues();
    for (std::size_t index = 0; index < queues.size(); ++index)
    {
        pending.submitted[index] = queues[index]->submitted.load(std::memory_order_acquire);
    }
    const std::scoped_lock lock{m_mutex};
    m_pending.push_back(std::move(pending));
    m_pending_count.store(m_pending.size(), std::memory_order_relaxed);
}

auto
VulkanDeleter::collect() -> std::size_t
{
    // How far every queue has completed.
    const std::span<const std::unique_ptr<NativeQueue>> queues = m_device.native_queues();
    std::array<std::uint64_t, 16> completed{};
    for (std::size_t index = 0; index < queues.size(); ++index)
    {
        m_device.check_result(m_device.functions().vkGetSemaphoreCounterValue(m_device.native(), queues[index]->progress,
                                                                              &completed[index]),
                              "Deleter::collect", "vkGetSemaphoreCounterValue");
    }

    // Entries are in enqueue order and their submission values only grow: stop at the first not ready.
    std::vector<std::move_only_function<void()>> ready;
    {
        const std::scoped_lock lock{m_mutex};
        while (!m_pending.empty())
        {
            const Pending& front = m_pending.front();
            bool done = true;
            for (std::size_t index = 0; index < queues.size(); ++index)
            {
                done = done && completed[index] >= front.submitted[index];
            }
            if (!done)
            {
                break;
            }
            ready.push_back(std::move(m_pending.front().deletion));
            m_pending.pop_front();
        }
        m_pending_count.store(m_pending.size(), std::memory_order_relaxed);
    }

    // Run them outside the lock: a deletion may enqueue another one.
    for (std::move_only_function<void()>& deletion : ready)
    {
        deletion();
    }
    return ready.size();
}

void
VulkanDeleter::flush()
{
    m_device.wait_idle();
    run_all();
}

auto
VulkanDeleter::pending_count() const noexcept -> std::size_t
{
    return m_pending_count.load(std::memory_order_relaxed);
}

auto
VulkanDeleter::run_all() noexcept -> std::size_t
{
    // Deletions may enqueue more deletions: repeat until none is left.
    std::size_t count = 0;
    while (true)
    {
        std::deque<Pending> pending;
        {
            const std::scoped_lock lock{m_mutex};
            pending.swap(m_pending);
            m_pending_count.store(0, std::memory_order_relaxed);
        }
        if (pending.empty())
        {
            return count;
        }
        for (Pending& entry : pending)
        {
            try
            {
                entry.deletion();
            }
            catch (...)
            {
            }
            ++count;
        }
    }
}

} // namespace gfx::vulkan
