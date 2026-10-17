// FrameMetrics: frame rate and frame times over the averaging period, the commands of a frame's command
// buffers, and the text they make. Debug builds only: in other builds it collects nothing, which is tested
// too. (gfx::ui::DebugOverlay shows the text.)
#include <gtest/gtest.h>

import std;
import gfx;
import gfx.tests.fake_backend;

namespace
{

using namespace gfx;
using namespace std::chrono_literals;
using gfx::tests::FakeCommandBuffer;
using gfx::tests::FakeDevice;
using gfx::tests::FakePhysicalDevice;
using Clock = std::chrono::steady_clock;

class FrameMetricsTest : public ::testing::Test
{
protected:
    FakePhysicalDevice m_physical_device{PhysicalDeviceType::DISCRETE_GPU};
    FakeDevice m_device{m_physical_device};
};

/// Records @p draws draws in one rendering pass of @p command_buffer.
void
record_draws(FakeCommandBuffer& command_buffer,
             std::uint32_t draws)
{
    const std::array<ColorAttachment, 1> attachments{ColorAttachment{}};
    GraphicsRecorder recorder{command_buffer};
    RenderingScope<QueueType::GRAPHICS> scope = recorder.begin_rendering({.color_attachments = attachments});
    for (std::uint32_t draw = 0; draw < draws; ++draw)
    {
        scope.draw({.vertex_count = 3});
    }
}

/// Whether @p lines has a line containing @p text.
auto
contains(const std::vector<std::string>& lines,
         std::string_view text) -> bool
{
    return std::ranges::any_of(lines, [text](const std::string& line) { return line.find(text) != std::string::npos; });
}

TEST_F(FrameMetricsTest, OnlyDebugBuildsCollect)
{
    // Other builds: nothing is measured, the text is empty.
    FrameMetrics metrics{m_device};
    metrics.begin_frame(Clock::time_point{});
    metrics.begin_frame(Clock::time_point{} + 1s);
    EXPECT_EQ(FrameMetrics::enabled(), DEBUG_BUILD);
    EXPECT_EQ(metrics.snapshot().frames, DEBUG_BUILD ? 1U : 0U);
    EXPECT_EQ(metrics.text().empty(), !DEBUG_BUILD);
}

TEST_F(FrameMetricsTest, FramesAreAveragedOverThePeriod)
{
    if constexpr (!DEBUG_BUILD)
    {
        GTEST_SKIP() << "frame metrics are collected in Debug builds only";
    }

    // Frames of 10 and 20 ms in turn; the 100 ms period completes at the eighth begin_frame().
    m_device.set_memory_statistics({.block_count = 2, .allocation_count = 5, .block_bytes = 64U << 20U, .allocation_bytes = 3U << 20U});
    FrameMetrics metrics{m_device, {.averaging_period = 100ms}};
    const Clock::time_point start{};
    std::chrono::milliseconds time{0};
    metrics.begin_frame(start);
    for (int frame = 0; frame < 6; ++frame)
    {
        time += frame % 2 == 0 ? 10ms : 20ms;
        metrics.begin_frame(start + time);
    }
    EXPECT_EQ(metrics.snapshot().frames, 6U);
    EXPECT_EQ(metrics.snapshot().frames_per_second, 0.0) << "no complete period yet";
    metrics.begin_frame(start + 100ms);

    // Seven frames in 100 ms.
    const FrameMetricsSnapshot& snapshot = metrics.snapshot();
    EXPECT_EQ(snapshot.frames, 7U);
    EXPECT_NEAR(snapshot.frames_per_second, 70.0, 1e-9);
    EXPECT_NEAR(snapshot.average_frame_milliseconds, 100.0 / 7.0, 1e-9);
    EXPECT_NEAR(snapshot.minimum_frame_milliseconds, 10.0, 1e-9);
    EXPECT_NEAR(snapshot.maximum_frame_milliseconds, 20.0, 1e-9);
    EXPECT_EQ(snapshot.memory.allocation_count, 5U);
}

TEST_F(FrameMetricsTest, TheCommandsOfAFrameAreReportedAtTheNextOne)
{
    if constexpr (!DEBUG_BUILD)
    {
        GTEST_SKIP() << "frame metrics are collected in Debug builds only";
    }

    // Two command buffers of one frame add up.
    FrameMetrics metrics{m_device};
    FakeCommandBuffer first{QueueType::GRAPHICS};
    FakeCommandBuffer second{QueueType::GRAPHICS};
    record_draws(first, 2);
    record_draws(second, 1);
    metrics.begin_frame(Clock::time_point{});
    metrics.count(first);
    metrics.count(second);
    metrics.begin_frame(Clock::time_point{} + 16ms);
    EXPECT_EQ(metrics.snapshot().command_buffers, 2U);
    EXPECT_EQ(metrics.snapshot().commands.draws, 3U);
    EXPECT_EQ(metrics.snapshot().commands.rendering_passes, 2U);

    // A frame that counts nothing reports nothing.
    metrics.begin_frame(Clock::time_point{} + 32ms);
    EXPECT_EQ(metrics.snapshot().command_buffers, 0U);
    EXPECT_EQ(metrics.snapshot().commands, CommandStatistics{});
}

TEST_F(FrameMetricsTest, TheTextNamesEveryMetric)
{
    if constexpr (!DEBUG_BUILD)
    {
        GTEST_SKIP() << "frame metrics are collected in Debug builds only";
    }
    m_device.set_memory_statistics({.block_count = 2, .allocation_count = 5, .block_bytes = 64U << 20U, .allocation_bytes = 3U << 20U});
    FrameMetrics metrics{m_device, {.averaging_period = 10ms}};
    FakeCommandBuffer commands{QueueType::GRAPHICS};
    record_draws(commands, 3);
    metrics.begin_frame(Clock::time_point{});
    metrics.count(commands);
    const std::array timings{GpuTiming{.name = "frame", .milliseconds = 1.5, .start = 0}};
    metrics.set_gpu_timings(timings);
    metrics.begin_frame(Clock::time_point{} + 20ms);
    const std::vector<std::string> lines = metrics.text();
    EXPECT_TRUE(contains(lines, "50.0 fps   20.00 ms")) << lines.front();
    EXPECT_TRUE(contains(lines, "gpu frame: 1.500 ms"));
    EXPECT_TRUE(contains(lines, "draws 3"));
    EXPECT_TRUE(contains(lines, "passes 1   command buffers 1"));
    EXPECT_TRUE(contains(lines, "memory 3.0 MiB in 5 allocations, 64.0 MiB in 2 blocks"));
}

} // namespace
