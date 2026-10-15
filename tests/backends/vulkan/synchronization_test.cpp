#include "vulkan_test.hpp"

namespace
{

using namespace gfx;
using gfx::tests::VulkanDeviceTest;
using gfx::tests::VulkanInstanceTest;
using namespace std::chrono_literals;

auto
contains(std::string_view text,
         std::string_view part) -> bool
{
    return text.find(part) != std::string_view::npos;
}

/// @brief A batch waiting for @p semaphore to reach @p value.
auto
wait_for(const TimelineSemaphore& semaphore,
         std::uint64_t value) -> std::array<TimelineSemaphoreWait, 1>
{
    return {TimelineSemaphoreWait{.semaphore = semaphore.handle(), .value = value}};
}

/// @brief A batch signaling @p value on @p semaphore.
auto
signal_of(const TimelineSemaphore& semaphore,
          std::uint64_t value) -> std::array<TimelineSemaphoreSignal, 1>
{
    return {TimelineSemaphoreSignal{.semaphore = semaphore.handle(), .value = value}};
}

TEST_F(VulkanDeviceTest, FencesAreSignaledBySubmissionsAndReset)
{
    std::unique_ptr<Fence> signaled = m_device->create_fence({.signaled = true, .debug_name = "signaled"});
    EXPECT_TRUE(signaled->is_signaled());
    EXPECT_EQ(signaled->wait(0ns), WaitStatus::SIGNALED);
    signaled->reset();
    EXPECT_FALSE(signaled->is_signaled());
    EXPECT_EQ(signaled->wait(0ns), WaitStatus::TIMEOUT);

    // A submission signals it when it completes.
    m_device->queue(QueueType::GRAPHICS).submit({}, signaled->handle());
    EXPECT_EQ(signaled->wait(), WaitStatus::SIGNALED);
    EXPECT_TRUE(signaled->is_signaled());
}

TEST_F(VulkanDeviceTest, TimelineSemaphoresAreSignaledByTheCpuAndByQueues)
{
    std::unique_ptr<TimelineSemaphore> timeline = m_device->create_timeline_semaphore({.initial_value = 5, .debug_name = "timeline"});
    EXPECT_EQ(timeline->value(), 5U);
    timeline->signal(6);
    EXPECT_EQ(timeline->value(), 6U);
    EXPECT_THROW(timeline->signal(6), InvalidUsageError);
    EXPECT_EQ(timeline->wait(6), WaitStatus::SIGNALED);
    EXPECT_EQ(timeline->wait(7, 1ms), WaitStatus::TIMEOUT);

    // A queue signals it.
    const std::array signals{TimelineSemaphoreSignal{.semaphore = timeline->handle(), .value = 10}};
    const std::array batches{SubmitDescription{.timeline_signals = signals}};
    m_device->queue(QueueType::GRAPHICS).submit(batches);
    EXPECT_EQ(timeline->wait(10, 5s), WaitStatus::SIGNALED);
}

TEST_F(VulkanDeviceTest, QueuesWaitOnTimelineValuesSignaledElsewhere)
{
    // The compute queue waits for value 20 (signaled by the CPU later), then signals 21.
    std::unique_ptr<TimelineSemaphore> timeline = m_device->create_timeline_semaphore({.debug_name = "cross queue"});
    const std::array waits{TimelineSemaphoreWait{.semaphore = timeline->handle(), .value = 20}};
    const std::array signals{TimelineSemaphoreSignal{.semaphore = timeline->handle(), .value = 21}};
    const std::array batches{SubmitDescription{.timeline_waits = waits, .timeline_signals = signals}};
    m_device->queue(QueueType::COMPUTE).submit(batches);
    EXPECT_EQ(timeline->wait(21, 10ms), WaitStatus::TIMEOUT);

    // The GPU proceeds once the value is reached.
    timeline->signal(20);
    EXPECT_EQ(timeline->wait(21, 5s), WaitStatus::SIGNALED);
}

TEST_F(VulkanDeviceTest, BinarySemaphoresChainSubmissionsAcrossQueues)
{
    std::unique_ptr<Semaphore> semaphore = m_device->create_semaphore({.debug_name = "graphics to compute"});
    std::unique_ptr<Fence> fence = m_device->create_fence({});
    const std::array signal{SemaphoreSignal{.semaphore = semaphore->handle()}};
    const std::array first{SubmitDescription{.signals = signal}};
    m_device->queue(QueueType::GRAPHICS).submit(first);
    const std::array wait{SemaphoreWait{.semaphore = semaphore->handle(), .stages = PipelineStage::COMPUTE_SHADER}};
    const std::array second{SubmitDescription{.waits = wait}};
    m_device->queue(QueueType::COMPUTE).submit(second, fence->handle());
    EXPECT_EQ(fence->wait(5s), WaitStatus::SIGNALED);
}

TEST_F(VulkanDeviceTest, DeletionsWaitForEveryQueue)
{
    // Needs two native queues (with one, the blocked submission would hold the graphics work back too).
    if (queues_share_hardware(QueueType::COMPUTE, QueueType::GRAPHICS))
    {
        GTEST_SKIP() << "the graphics and compute queues share one native queue on this device";
    }

    // The compute queue is held back by a timeline value the CPU has not signaled yet.
    std::unique_ptr<TimelineSemaphore> gate = m_device->create_timeline_semaphore({.debug_name = "gate"});
    const std::array waits{TimelineSemaphoreWait{.semaphore = gate->handle(), .value = 1}};
    const std::array batches{SubmitDescription{.timeline_waits = waits}};
    m_device->queue(QueueType::COMPUTE).submit(batches);
    m_device->queue(QueueType::GRAPHICS).submit({});

    // A deletion enqueued now waits for both queues.
    bool deleted = false;
    m_device->deleter().enqueue([&deleted] { deleted = true; });
    m_device->queue(QueueType::GRAPHICS).wait_idle();
    m_device->deleter().collect();
    EXPECT_FALSE(deleted) << "the compute queue has not completed its submission";
    EXPECT_EQ(m_device->deleter().pending_count(), 1U);

    gate->signal(1);
    m_device->queue(QueueType::COMPUTE).wait_idle();
    m_device->deleter().collect();
    EXPECT_TRUE(deleted);
}

TEST_F(VulkanDeviceTest, SignalsQueuedBehindTheirOwnWaitAreRefused)
{
    // The queue stalls on a value nothing has signaled yet.
    std::unique_ptr<TimelineSemaphore> gate = m_device->create_timeline_semaphore({.debug_name = "gate"});
    Queue& queue = m_device->queue(QueueType::GRAPHICS);
    const auto waits = wait_for(*gate, 1);
    queue.submit(std::array{SubmitDescription{.timeline_waits = waits}});

    // A signal submitted behind that wait could never run.
    const auto signals = signal_of(*gate, 1);
    try
    {
        queue.submit(std::array{SubmitDescription{.timeline_signals = signals}});
        FAIL() << "submit must throw";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_TRUE(contains(error.what(), "timeline semaphore 'gate' to reach 1")) << error.what();
        EXPECT_TRUE(contains(error.what(), "GPU deadlock")) << error.what();
    }

    // Once the CPU reached the value, the queue runs again and later signals are accepted.
    gate->signal(1);
    const auto later = signal_of(*gate, 2);
    queue.submit(std::array{SubmitDescription{.timeline_signals = later}});
    EXPECT_EQ(gate->wait(2, 5s), WaitStatus::SIGNALED);
}

TEST_F(VulkanDeviceTest, WaitsAndSignalsInOneSubmissionAreCheckedInBatchOrder)
{
    // A later batch waiting for an earlier batch's signal is fine; the reverse order is refused.
    std::unique_ptr<TimelineSemaphore> timeline = m_device->create_timeline_semaphore({.debug_name = "batches"});
    Queue& queue = m_device->queue(QueueType::GRAPHICS);
    const auto signal_one = signal_of(*timeline, 1);
    const auto wait_one = wait_for(*timeline, 1);
    queue.submit(std::array{SubmitDescription{.timeline_signals = signal_one}, SubmitDescription{.timeline_waits = wait_one}});
    EXPECT_EQ(timeline->wait(1, 5s), WaitStatus::SIGNALED);

    const auto wait_two = wait_for(*timeline, 2);
    const auto signal_two = signal_of(*timeline, 2);
    EXPECT_THROW(queue.submit(std::array{SubmitDescription{.timeline_waits = wait_two}, SubmitDescription{.timeline_signals = signal_two}}),
                 InvalidUsageError);

    // Nothing of the refused submission reached the GPU: the queue still runs.
    queue.submit(std::array{SubmitDescription{.timeline_signals = signal_two}});
    EXPECT_EQ(timeline->wait(2, 5s), WaitStatus::SIGNALED);
}

TEST_F(VulkanInstanceTest, QueuesSharingAHardwareQueueRefuseWaitsSubmittedBeforeTheirSignals)
{
    // One more graphics queue than the family has: the last one runs on the first one's hardware queue.
    PhysicalDevice& device = physical_device();
    const std::array queues{QueueRequest{.type = QueueType::GRAPHICS, .count = 40}};
    std::unique_ptr<Device> logical = device.create_device({.queues = queues});
    Queue& first = logical->queue(QueueType::GRAPHICS, 0);
    Queue& sharing = logical->queue(QueueType::GRAPHICS, device.queue_families()[first.family_index()].queue_count);
    std::unique_ptr<TimelineSemaphore> handoff = logical->create_timeline_semaphore({.debug_name = "handoff"});

    // The first queue waits for a value the other is to signal: refused, both run on one hardware queue.
    const auto wait_one = wait_for(*handoff, 1);
    first.submit(std::array{SubmitDescription{.timeline_waits = wait_one}});
    const auto signal_one = signal_of(*handoff, 1);
    try
    {
        sharing.submit(std::array{SubmitDescription{.timeline_signals = signal_one}});
        FAIL() << "submit must throw";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_TRUE(contains(error.what(), std::format("'{}' waits", first.debug_name()))) << error.what();
        EXPECT_TRUE(contains(error.what(), "same hardware queue")) << error.what();
    }

    // Signaling first is always safe.
    handoff->signal(1);
    const auto signal_two = signal_of(*handoff, 2);
    const auto wait_two = wait_for(*handoff, 2);
    sharing.submit(std::array{SubmitDescription{.timeline_signals = signal_two}});
    first.submit(std::array{SubmitDescription{.timeline_waits = wait_two}});
    first.wait_idle();
    EXPECT_EQ(handoff->value(), 2U);
}

TEST_F(VulkanDeviceTest, EventsAreSetAndResetByTheCpu)
{
    std::unique_ptr<Event> event = m_device->create_event({.debug_name = "event"});
    EXPECT_FALSE(event->is_set());
    event->set();
    EXPECT_TRUE(event->is_set());
    event->reset();
    EXPECT_FALSE(event->is_set());
}

TEST_F(VulkanDeviceTest, QueryPoolsReportUnwrittenQueriesAsUnavailable)
{
    std::unique_ptr<QueryPool> timestamps = m_device->create_query_pool({.type = QueryType::TIMESTAMP, .count = 4, .debug_name = "times"});
    EXPECT_EQ(timestamps->type(), QueryType::TIMESTAMP);
    EXPECT_EQ(timestamps->count(), 4U);
    std::array<std::uint64_t, 4> values{};
    EXPECT_FALSE(timestamps->results(0, values, false));
    std::array<std::uint64_t, 2> too_many{};
    EXPECT_THROW((void)timestamps->results(3, too_many, false), InvalidUsageError);

    if (m_device->physical_device().supports(DeviceFeature::PIPELINE_STATISTICS_QUERY))
    {
        std::unique_ptr<QueryPool> statistics = m_device->create_query_pool({
            .type = QueryType::PIPELINE_STATISTICS,
            .count = 2,
            .statistics = PipelineStatistic::VERTEX_SHADER_INVOCATIONS | PipelineStatistic::FRAGMENT_SHADER_INVOCATIONS,
        });
        std::array<std::uint64_t, 3> odd{};
        EXPECT_THROW((void)statistics->results(0, odd, false), InvalidUsageError);
        std::array<std::uint64_t, 4> two_queries{};
        EXPECT_FALSE(statistics->results(0, two_queries, false));
    }
    EXPECT_THROW((void)m_device->create_query_pool({.type = QueryType::OCCLUSION, .count = 0}), InvalidUsageError);
}

TEST_F(VulkanDeviceTest, SubmittingStaleHandlesIsAnError)
{
    std::unique_ptr<Fence> fence = m_device->create_fence({});
    const FenceHandle stale = fence->handle();
    fence.reset();
    EXPECT_THROW(m_device->queue(QueueType::GRAPHICS).submit({}, stale), InvalidHandleError);
}

} // namespace
