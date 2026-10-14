// FrameCommandPools: one pool per frame in flight, reused once the GPU finished the frame that last
// recorded into it, with its command buffers handed out again.
#include <gtest/gtest.h>

import std;
import gfx;
import gfx.tests.fake_backend;

namespace
{

using namespace gfx;
using gfx::tests::FakeCommandPool;
using gfx::tests::FakeDevice;
using gfx::tests::FakePhysicalDevice;
using gfx::tests::FakeTimelineSemaphore;

class FrameCommandPoolsTest : public ::testing::Test
{
protected:
    FakePhysicalDevice m_physical_device{PhysicalDeviceType::DISCRETE_GPU};
    FakeDevice m_device{m_physical_device};
    FakeTimelineSemaphore m_frame_timeline;
};

TEST_F(FrameCommandPoolsTest, EachFrameInFlightRecordsIntoItsOwnPool)
{
    FrameCommandPools pools{m_device, {.queue_type = QueueType::GRAPHICS, .debug_name = "main thread"}, m_frame_timeline, 2};
    EXPECT_EQ(pools.frames_in_flight(), 2U);

    pools.begin_frame(1);
    CommandBuffer& first = pools.command_buffer();
    CommandPool& first_pool = pools.pool();
    pools.begin_frame(2);
    CommandBuffer& second = pools.command_buffer();
    EXPECT_NE(&first, &second);
    EXPECT_NE(&first_pool, &pools.pool());
    EXPECT_TRUE(m_frame_timeline.waited_values().empty()) << "fresh pools need no wait";
    EXPECT_EQ(first.queue_type(), QueueType::GRAPHICS);
}

TEST_F(FrameCommandPoolsTest, APoolIsReusedOnceTheGpuFinishedItsLastFrame)
{
    FrameCommandPools pools{m_device, {.queue_type = QueueType::COMPUTE}, m_frame_timeline, 2};
    pools.begin_frame(1);
    CommandBuffer& frame_one = pools.command_buffer();
    pools.begin_frame(2);
    (void)pools.command_buffer();

    // Frame 3 uses frame 1's pool: it waits for frame 1, resets the pool and hands its buffer out again.
    m_frame_timeline.signal(1);
    pools.begin_frame(3);
    EXPECT_EQ(m_frame_timeline.waited_values(), (std::vector<std::uint64_t>{1}));
    const auto& pool = dynamic_cast<const FakeCommandPool&>(pools.pool());
    EXPECT_EQ(pool.reset_count(), 1U);
    EXPECT_EQ(&pools.command_buffer(), &frame_one);
    EXPECT_EQ(pool.allocation_count(), 1U) << "the command buffer was reused, not allocated again";

    // A second buffer in the same frame is new.
    EXPECT_NE(&pools.command_buffer(), &frame_one);
    EXPECT_EQ(pool.allocation_count(), 2U);
}

TEST_F(FrameCommandPoolsTest, SecondaryBuffersAreKeptApartFromPrimaries)
{
    FrameCommandPools pools{m_device, {.queue_type = QueueType::GRAPHICS}, m_frame_timeline, 1};
    pools.begin_frame(1);
    CommandBuffer& primary = pools.command_buffer();
    CommandBuffer& secondary = pools.command_buffer(CommandBufferLevel::SECONDARY);
    EXPECT_EQ(primary.level(), CommandBufferLevel::PRIMARY);
    EXPECT_EQ(secondary.level(), CommandBufferLevel::SECONDARY);

    // One frame in flight: every frame waits for the previous one.
    pools.begin_frame(2);
    EXPECT_EQ(m_frame_timeline.waited_values(), (std::vector<std::uint64_t>{1}));
    EXPECT_EQ(&pools.command_buffer(CommandBufferLevel::SECONDARY), &secondary);
    EXPECT_EQ(&pools.command_buffer(), &primary);
}

TEST_F(FrameCommandPoolsTest, MisuseIsReported)
{
    EXPECT_THROW(FrameCommandPools(m_device, {}, m_frame_timeline, 0), InvalidUsageError);

    FrameCommandPools pools{m_device, {}, m_frame_timeline, 2};
    EXPECT_THROW((void)pools.command_buffer(), InvalidUsageError) << "no frame begun";
    EXPECT_THROW(pools.begin_frame(0), InvalidUsageError);
    pools.begin_frame(5);
    try
    {
        pools.begin_frame(5);
        FAIL() << "InvalidUsageError was not thrown";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_EQ(error.problem(), "frame 5 follows frame 5; frames are numbered from 1 and increase");
    }
    EXPECT_EQ(pools.current_frame(), 5U);
}

} // namespace
