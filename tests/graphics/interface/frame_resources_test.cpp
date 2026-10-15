// Per-frame resources built on the interfaces: FrameTimeline pacing, TransientBufferAllocator (lock-free
// bump allocation in persistently mapped memory), Uploader (staged copies with barriers) and GpuProfiler.
#include <gtest/gtest.h>

import std;
import gfx;
import gfx.tests.fake_backend;

namespace
{

using namespace gfx;
using gfx::tests::FakeCommandBuffer;
using gfx::tests::FakeDevice;
using gfx::tests::FakePhysicalDevice;

class FrameResourcesTest : public ::testing::Test
{
protected:
    FakePhysicalDevice m_physical_device{PhysicalDeviceType::DISCRETE_GPU};
    FakeDevice m_device{m_physical_device};
};

TEST_F(FrameResourcesTest, TheTimelineKeepsTheCpuAtMostFramesInFlightAhead)
{
    FrameTimeline timeline{m_device, 2};
    EXPECT_EQ(timeline.begin_frame(), 1U);
    EXPECT_EQ(timeline.begin_frame(), 2U);
    EXPECT_TRUE(m_device.last_timeline().waited_values().empty()) << "the first frames in flight wait for nothing";
    EXPECT_EQ(timeline.frame_signal().value, 2U);
    EXPECT_EQ(timeline.frame_signal().semaphore, m_device.last_timeline().handle());

    // Frame 3 reuses frame 1's resources: it waits for frame 1, then runs deferred destructions.
    m_device.last_timeline().signal(1);
    EXPECT_EQ(timeline.begin_frame(), 3U);
    EXPECT_EQ(m_device.last_timeline().waited_values(), (std::vector<std::uint64_t>{1}));
    EXPECT_EQ(m_device.fake_deleter().collections(), 3U);
    EXPECT_EQ(timeline.completed_frame(), 1U);
    EXPECT_THROW(FrameTimeline(m_device, 0), InvalidUsageError);
}

TEST_F(FrameResourcesTest, TransientAllocationsAreAlignedAndPerFrame)
{
    TransientBufferAllocator transient{m_device, 1024, 2};
    transient.begin_frame(1);
    const TransientAllocation first = transient.allocate(10);
    const TransientAllocation second = transient.allocate(10);
    EXPECT_EQ(first.offset, 1024U) << "frame 1 uses region 1 % 2";
    EXPECT_EQ(second.offset, 1040U) << "aligned to the device's buffer offset alignment (at least 16)";
    EXPECT_EQ(first.buffer, transient.buffer().handle());
    EXPECT_TRUE(first.buffer_index.is_valid()) << "reachable by shaders through the bindless table";
    EXPECT_EQ(first.memory.size(), 10U);

    // Frame 2 uses region 0; frame 3 region 1 again.
    transient.begin_frame(2);
    EXPECT_EQ(transient.allocate(4).offset, 0U);
    transient.begin_frame(3);
    EXPECT_EQ(transient.allocate(4).offset, 1024U);

    // write() copies the data.
    const std::array<std::uint32_t, 2> data{0xAAAAAAAAU, 0x55555555U};
    const TransientAllocation written = transient.write(data);
    std::array<std::uint32_t, 2> read_back{};
    std::memcpy(read_back.data(), written.memory.data(), sizeof(read_back));
    EXPECT_EQ(read_back, data);
}

TEST_F(FrameResourcesTest, TransientAllocationIsLockFreeAcrossThreads)
{
    TransientBufferAllocator transient{m_device, 64 * 1024, 1};
    transient.begin_frame(1);
    std::vector<std::uint64_t> offsets(8 * 100);
    {
        std::vector<std::jthread> threads;
        for (std::size_t thread = 0; thread < 8; ++thread)
        {
            threads.emplace_back([&transient, &offsets, thread]
            {
                for (std::size_t allocation = 0; allocation < 100; ++allocation)
                {
                    offsets[thread * 100 + allocation] = transient.allocate(16).offset;
                }
            });
        }
    }

    // Every allocation got its own 16 bytes.
    std::ranges::sort(offsets);
    EXPECT_EQ(std::ranges::adjacent_find(offsets), offsets.end()) << "no two allocations overlap";
    EXPECT_EQ(offsets.back(), (800U - 1) * 16);
}

TEST_F(FrameResourcesTest, AFullFrameRegionReportsItsSize)
{
    TransientBufferAllocator transient{m_device, 64, 1};
    transient.begin_frame(1);
    (void)transient.allocate(48);
    try
    {
        (void)transient.allocate(32);
        FAIL() << "OutOfMemoryError was not thrown";
    }
    catch (const OutOfMemoryError& error)
    {
        EXPECT_EQ(error.requested_bytes(), 32U);
        EXPECT_NE(std::string_view{error.what()}.find("'transient buffer' has 16 of its 64 bytes per frame left; raise bytes_per_frame"),
                  std::string_view::npos)
            << error.what();
    }
}

TEST_F(FrameResourcesTest, UploadsAreStagedAndFencedByBarriers)
{
    Uploader uploader{m_device, 4096, 2};
    uploader.begin_frame(1);
    FakeCommandBuffer command_buffer{QueueType::TRANSFER};
    const std::array<std::uint32_t, 4> vertices{1, 2, 3, 4};
    {
        TransferRecorder recorder{command_buffer};
        uploader.upload_buffer(recorder, BufferHandle{}, 64, vertices, ResourceState::UNDEFINED, ResourceState::VERTEX_BUFFER);
    }
    EXPECT_EQ(command_buffer.log(), (std::vector<std::string>{"begin", "barrier 0 1 0", "copy_buffer 1", "barrier 0 1 0", "end"}));

    // The staging memory of frame 1 (region 1 % 2) holds the data.
    std::array<std::uint32_t, 4> staged{};
    std::memcpy(staged.data(), uploader.staging().buffer().mapped_memory().data() + 4096, sizeof(staged));
    EXPECT_EQ(staged, vertices);
}

TEST_F(FrameResourcesTest, ProfilerScopesAreTimedWhenTheGpuFinishedTheFrame)
{
    GpuProfiler profiler{m_device, 2, 4};
    FakeCommandBuffer command_buffer{QueueType::GRAPHICS};
    {
        GraphicsRecorder recorder{command_buffer};
        profiler.begin_frame(1, recorder);
        {
            auto shadows = profiler.scope(recorder, "shadows");
        }
    }
    // Frame 1 uses the second slot of queries: [8, 16).
    EXPECT_EQ(command_buffer.log(), (std::vector<std::string>{"begin", "reset_query_pool 8 8", "begin_debug_label shadows",
                                                              std::format("write_timestamp {} 8", PipelineStage::NONE),
                                                              std::format("write_timestamp {} 9", PipelineStage::ALL_COMMANDS),
                                                              "end_debug_label", "end"}));
    EXPECT_TRUE(profiler.timings().empty()) << "nothing finished yet";

    // Frame 3 reuses frame 1's queries: frame 1's results are read (timestamp period 1 ns).
    m_device.last_query_pool().set_result(8, 1'000'000);
    m_device.last_query_pool().set_result(9, 3'500'000);
    FakeCommandBuffer next{QueueType::GRAPHICS};
    {
        GraphicsRecorder recorder{next};
        profiler.begin_frame(3, recorder);
    }
    ASSERT_EQ(profiler.timings().size(), 1U);
    EXPECT_EQ(profiler.timings()[0].name, "shadows");
    EXPECT_DOUBLE_EQ(profiler.timings()[0].milliseconds, 2.5);
    EXPECT_EQ(profiler.timed_frame(), 1U);
}

} // namespace
