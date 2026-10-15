#include "vulkan_test.hpp"

import gfx.tests.vulkan_shaders;

namespace
{

using namespace gfx;
using gfx::tests::rgba;
using gfx::tests::VulkanDeviceTest;
namespace shaders = gfx::tests::vulkan_shaders;

constexpr std::uint32_t FRAMES_IN_FLIGHT = 2;
constexpr std::uint32_t WORD_COUNT = 256;

/// @brief Push constants of accumulate.comp.
struct AccumulatePush
{
    std::uint32_t target = 0;
    std::uint32_t source = 0;
    std::uint32_t source_word = 0;
    std::uint32_t count = 0;
};

/// @brief The library's frame helpers (FrameTimeline, FrameCommandPools, TransientBufferAllocator,
///        Uploader, GpuProfiler) driving the Vulkan backend.
class VulkanFrameTest : public VulkanDeviceTest
{
protected:
    /// @brief Submits @p command_buffer to the graphics queue, signaling the end of the current frame.
    void submit_frame(const FrameTimeline& timeline,
                      const CommandBuffer& command_buffer)
    {
        const std::array command_buffers{command_buffer.handle()};
        const std::array signals{timeline.frame_signal()};
        const std::array batches{SubmitDescription{.command_buffers = command_buffers, .timeline_signals = signals}};
        m_device->queue(QueueType::GRAPHICS).submit(batches);
    }
};

TEST_F(VulkanFrameTest, FramesUploadAccumulateTransientDataAndProfileTheirWork)
{
    FrameTimeline timeline{*m_device, FRAMES_IN_FLIGHT};
    FrameCommandPools pools{*m_device, {.queue_type = QueueType::GRAPHICS, .debug_name = "frame pools"}, timeline.timeline(), FRAMES_IN_FLIGHT};
    Uploader uploader{*m_device, 64 * 1024, FRAMES_IN_FLIGHT};
    TransientBufferAllocator transient{*m_device, 64 * 1024, FRAMES_IN_FLIGHT};
    GpuProfiler profiler{*m_device, FRAMES_IN_FLIGHT};
    std::unique_ptr<ShaderModule> shader = m_device->create_shader_module(shaders::accumulate_comp);
    std::unique_ptr<ComputePipeline> pipeline = m_device->create_compute_pipeline({.shader = shader->handle(), .debug_name = "accumulate"});
    std::unique_ptr<Buffer> totals = m_device->create_buffer({.size = WORD_COUNT * sizeof(std::uint32_t),
                                                              .usage = BufferUsage::STORAGE | BufferUsage::TRANSFER_SOURCE |
                                                                       BufferUsage::TRANSFER_DESTINATION,
                                                              .debug_name = "totals"});

    constexpr std::uint64_t FRAME_COUNT = 5;
    for (std::uint64_t index = 0; index < FRAME_COUNT; ++index)
    {
        // The frame loop: pace, then reuse this frame's pools and memory.
        const std::uint64_t frame = timeline.begin_frame();
        pools.begin_frame(frame);
        uploader.begin_frame(frame);
        transient.begin_frame(frame);
        CommandBuffer& command_buffer = pools.command_buffer();
        {
            GraphicsRecorder recorder{command_buffer};
            profiler.begin_frame(frame, recorder);

            // The first frame uploads the starting totals through the staging ring.
            if (frame == 1)
            {
                GpuProfilerScope upload = profiler.scope(recorder, "upload");
                std::vector<std::uint32_t> start(WORD_COUNT);
                std::ranges::iota(start, 1000U);
                uploader.upload_buffer(recorder, totals->handle(), 0, start, ResourceState::UNDEFINED,
                                       ResourceState::COMPUTE_SHADER_STORAGE_READ_WRITE);
            }
            else
            {
                // The previous frame's dispatch wrote the totals.
                const std::array previous{buffer_barrier(totals->handle(), ResourceState::COMPUTE_SHADER_STORAGE_READ_WRITE,
                                                         ResourceState::COMPUTE_SHADER_STORAGE_READ_WRITE)};
                recorder.barrier({.buffer_barriers = previous});
            }

            // This frame's values live in the transient ring, after another allocation (a non-zero offset).
            (void)transient.write(std::uint32_t{0xDEADBEEF});
            const std::vector<std::uint32_t> values(WORD_COUNT, static_cast<std::uint32_t>(frame));
            const TransientAllocation allocation = transient.write(values);
            ASSERT_NE(allocation.offset % transient.bytes_per_frame(), 0U);

            // The dispatch adds them to the totals.
            GpuProfilerScope accumulate = profiler.scope(recorder, "accumulate");
            recorder.bind_pipeline(pipeline->handle());
            recorder.push_constants(AccumulatePush{.target = totals->bindless_index().value(),
                                                   .source = allocation.buffer_index.value(),
                                                   .source_word = static_cast<std::uint32_t>(allocation.offset / sizeof(std::uint32_t)),
                                                   .count = WORD_COUNT});
            recorder.dispatch(WORD_COUNT / 64);
        }
        submit_frame(timeline, command_buffer);
    }
    timeline.wait_idle();

    // 1000 + index, plus 1 + 2 + 3 + 4 + 5.
    const std::vector<std::uint32_t> result = read_buffer<std::uint32_t>(*totals, WORD_COUNT, ResourceState::COMPUTE_SHADER_STORAGE_READ_WRITE);
    EXPECT_EQ(result[0], 1015U);
    EXPECT_EQ(result[WORD_COUNT - 1], 1000U + WORD_COUNT - 1 + 15U);

    // Frame 5 reused frame 3's queries, reading its timings without waiting.
    EXPECT_EQ(profiler.timed_frame(), FRAME_COUNT - FRAMES_IN_FLIGHT);
    ASSERT_EQ(profiler.timings().size(), 1U);
    EXPECT_EQ(profiler.timings()[0].name, "accumulate");
    EXPECT_GE(profiler.timings()[0].milliseconds, 0.0);
}

TEST_F(VulkanFrameTest, ProfilerReadsEveryScopeOfAFrame)
{
    FrameTimeline timeline{*m_device, 1};
    FrameCommandPools pools{*m_device, {.queue_type = QueueType::GRAPHICS}, timeline.timeline(), 1};
    GpuProfiler profiler{*m_device, 1};
    std::unique_ptr<Buffer> scratch = m_device->create_buffer({.size = 1 << 20, .usage = BufferUsage::TRANSFER_DESTINATION});
    for (int index = 0; index < 2; ++index)
    {
        const std::uint64_t frame = timeline.begin_frame();
        pools.begin_frame(frame);
        CommandBuffer& command_buffer = pools.command_buffer();
        {
            GraphicsRecorder recorder{command_buffer};
            profiler.begin_frame(frame, recorder);
            GpuProfilerScope outer = profiler.scope(recorder, "outer");
            recorder.fill_buffer(scratch->handle(), 0, WHOLE_SIZE, 1);
            {
                GpuProfilerScope inner = profiler.scope(recorder, "inner");
                const std::array again{buffer_barrier(scratch->handle(), ResourceState::TRANSFER_DESTINATION, ResourceState::TRANSFER_DESTINATION)};
                recorder.barrier({.buffer_barriers = again});
                recorder.fill_buffer(scratch->handle(), 0, WHOLE_SIZE, 2);
            }
        }
        submit_frame(timeline, command_buffer);
    }
    timeline.wait_idle();

    // The second frame read the first one's scopes, ordered by start time.
    EXPECT_EQ(profiler.timed_frame(), 1U);
    ASSERT_EQ(profiler.timings().size(), 2U);
    EXPECT_EQ(profiler.timings()[0].name, "outer");
    EXPECT_EQ(profiler.timings()[1].name, "inner");
    EXPECT_GE(profiler.timings()[0].milliseconds, profiler.timings()[1].milliseconds);
}

TEST_F(VulkanFrameTest, UploaderFillsImagesAndRegionsOfThem)
{
    constexpr Extent EXTENT{.width = 16, .height = 16, .depth = 1};
    std::unique_ptr<Image> image = m_device->create_image({.format = Format::R8G8B8A8_UNORM,
                                                           .extent = EXTENT,
                                                           .usage = ImageUsage::SAMPLED | ImageUsage::TRANSFER_DESTINATION |
                                                                    ImageUsage::TRANSFER_SOURCE,
                                                           .debug_name = "uploaded"});
    Uploader uploader{*m_device, 4096, 1};
    uploader.begin_frame(1);

    // The whole level in one color, then a 4x4 block at (8, 4) in another.
    const std::vector<std::uint32_t> blue(std::size_t{EXTENT.width} * EXTENT.height, rgba(0, 0, 255, 255));
    const std::vector<std::uint32_t> white(16, rgba(255, 255, 255, 255));
    run([&](GraphicsRecorder& recorder)
    {
        uploader.upload_image(recorder, *image, std::as_bytes(std::span{blue}), ResourceState::UNDEFINED, ResourceState::SHADER_SAMPLED_READ);
        uploader.upload_image(recorder, *image, std::as_bytes(std::span{white}), ResourceState::SHADER_SAMPLED_READ,
                              ResourceState::SHADER_SAMPLED_READ, {}, {.x = 8, .y = 4}, Extent{.width = 4, .height = 4, .depth = 1});
    });

    const std::vector<std::uint32_t> texels = read_image(*image, ResourceState::SHADER_SAMPLED_READ);
    const auto texel = [&](std::uint32_t x, std::uint32_t y)
    {
        return texels[std::size_t{y} * EXTENT.width + x];
    };
    EXPECT_EQ(texel(0, 0), rgba(0, 0, 255, 255));
    EXPECT_EQ(texel(8, 4), rgba(255, 255, 255, 255));
    EXPECT_EQ(texel(11, 7), rgba(255, 255, 255, 255));
    EXPECT_EQ(texel(12, 7), rgba(0, 0, 255, 255));
    EXPECT_EQ(texel(15, 15), rgba(0, 0, 255, 255));
}

TEST_F(VulkanFrameTest, TransientRegionsAreFullAtTheirFrameSize)
{
    // Frame N uses region N % frames in flight: frame 1 the second one.
    TransientBufferAllocator transient{*m_device, 256, 2};
    transient.begin_frame(1);
    EXPECT_EQ(transient.allocate(transient.bytes_per_frame()).offset, transient.bytes_per_frame());
    EXPECT_THROW((void)transient.allocate(1), OutOfMemoryError);

    // The next frame has a region of its own.
    transient.begin_frame(2);
    EXPECT_EQ(transient.allocate(16).offset, 0U);
}

} // namespace
