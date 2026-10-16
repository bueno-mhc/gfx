// FrameMetrics and DebugOverlay: frame rate and frame times over the averaging period, the commands of a
// frame's command buffers, the text they make, and the rectangles the overlay clears for its panel and
// its glyphs. Debug builds only: in other builds both collect and draw nothing, which is tested too.
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

/// An overlay drawn in one rendering pass of a fake command buffer.
class DebugOverlayTest : public ::testing::Test
{
protected:
    /// The clears @p overlay records for @p lines in @p area.
    auto draw(DebugOverlay& overlay,
              std::vector<std::string> lines,
              Rectangle area = {.x = 0, .y = 0, .width = 640, .height = 480}) -> std::vector<FakeCommandBuffer::RecordedClear>
    {
        FakeCommandBuffer command_buffer{QueueType::GRAPHICS};
        {
            const std::array<ColorAttachment, 1> attachments{ColorAttachment{}};
            GraphicsRecorder recorder{command_buffer};
            RenderingScope<QueueType::GRAPHICS> scope = recorder.begin_rendering({.area = area, .color_attachments = attachments});
            overlay.draw(scope, area, lines);
        }
        return command_buffer.clears();
    }
};

TEST_F(DebugOverlayTest, OverlaysDrawInDebugBuildsOnlyAndWhenShown)
{
    // Hidden by default; toggling shows it (in Debug builds).
    DebugOverlay overlay;
    EXPECT_FALSE(overlay.is_visible());
    EXPECT_TRUE(draw(overlay, {"hidden"}).empty());
    overlay.toggle();
    EXPECT_TRUE(overlay.is_visible());
    EXPECT_EQ(DebugOverlay::enabled(), DEBUG_BUILD);
    EXPECT_EQ(draw(overlay, {"shown"}).size(), DEBUG_BUILD ? 2U : 0U);
    overlay.set_visible(false);
    EXPECT_TRUE(draw(overlay, {"hidden again"}).empty());
}

TEST_F(DebugOverlayTest, GlyphsAreClearedRunsOverAPanel)
{
    if constexpr (!DEBUG_BUILD)
    {
        GTEST_SKIP() << "overlays draw in Debug builds only";
    }

    // A panel of 11 x 13 pixels at scale 1, then the runs of '1': "..#..", ".##..", four "..#..", ".###.".
    DebugOverlay overlay{{.visible = true, .scale = 1, .position = {.x = 10, .y = 20, .z = 0}}};
    const std::vector<FakeCommandBuffer::RecordedClear> clears = draw(overlay, {"1"});
    ASSERT_EQ(clears.size(), 2U);
    EXPECT_EQ(clears[0].attachments.front().color, (std::array{0.0F, 0.0F, 0.0F, 1.0F}));
    ASSERT_EQ(clears[0].rectangles.size(), 1U);
    EXPECT_EQ(clears[0].rectangles.front().area, (Rectangle{.x = 10, .y = 20, .width = 11, .height = 13}));
    EXPECT_EQ(clears[1].attachments.front().color, (std::array{1.0F, 1.0F, 1.0F, 1.0F}));
    const std::vector<Rectangle> expected{
        {.x = 15, .y = 23, .width = 1, .height = 1}, {.x = 14, .y = 24, .width = 2, .height = 1}, {.x = 15, .y = 25, .width = 1, .height = 1},
        {.x = 15, .y = 26, .width = 1, .height = 1}, {.x = 15, .y = 27, .width = 1, .height = 1}, {.x = 15, .y = 28, .width = 1, .height = 1},
        {.x = 14, .y = 29, .width = 3, .height = 1},
    };
    std::vector<Rectangle> runs;
    for (const ClearRectangle& rectangle : clears[1].rectangles)
    {
        runs.push_back(rectangle.area);
    }
    EXPECT_EQ(runs, expected);
}

TEST_F(DebugOverlayTest, TheScaleEnlargesEveryPixel)
{
    if constexpr (!DEBUG_BUILD)
    {
        GTEST_SKIP() << "overlays draw in Debug builds only";
    }

    // Two lines, the longer of 4 characters, at scale 2: (4 x 6 - 1 + 2 x 3) x 2 by (2 x 9 - 2 + 2 x 3) x 2.
    DebugOverlay overlay{{.visible = true, .scale = 2}};
    const std::vector<std::string> lines{"ab", "abcd"};
    EXPECT_EQ(overlay.measure(lines), (Extent{.width = 58, .height = 44, .depth = 1}));
    const std::vector<FakeCommandBuffer::RecordedClear> clears = draw(overlay, lines);
    ASSERT_EQ(clears.size(), 2U);
    for (const ClearRectangle& rectangle : clears[1].rectangles)
    {
        EXPECT_EQ(rectangle.area.height, 2U);
        EXPECT_EQ(rectangle.area.width % 2, 0U);
    }
}

TEST_F(DebugOverlayTest, EverythingStaysInsideTheArea)
{
    if constexpr (!DEBUG_BUILD)
    {
        GTEST_SKIP() << "overlays draw in Debug builds only";
    }

    // A long line in a small area: the panel and every run are clipped to it.
    DebugOverlay overlay{{.visible = true, .scale = 3}};
    const Rectangle area{.x = 100, .y = 50, .width = 40, .height = 30};
    const std::vector<FakeCommandBuffer::RecordedClear> clears = draw(overlay, {"a line far wider than the area", "and a second one"}, area);
    ASSERT_EQ(clears.size(), 2U);
    for (const FakeCommandBuffer::RecordedClear& clear : clears)
    {
        for (const ClearRectangle& rectangle : clear.rectangles)
        {
            EXPECT_GE(rectangle.area.x, area.x);
            EXPECT_GE(rectangle.area.y, area.y);
            EXPECT_GT(rectangle.area.width, 0U);
            EXPECT_GT(rectangle.area.height, 0U);
            EXPECT_LE(rectangle.area.x + static_cast<std::int32_t>(rectangle.area.width), area.x + static_cast<std::int32_t>(area.width));
            EXPECT_LE(rectangle.area.y + static_cast<std::int32_t>(rectangle.area.height), area.y + static_cast<std::int32_t>(area.height));
        }
    }

    // An area the panel does not reach: nothing is drawn.
    EXPECT_TRUE(draw(overlay, {"x"}, Rectangle{.x = 0, .y = 0, .width = 4, .height = 4}).empty());
}

TEST_F(DebugOverlayTest, UnprintableCharactersShowAsQuestionMarks)
{
    if constexpr (!DEBUG_BUILD)
    {
        GTEST_SKIP() << "overlays draw in Debug builds only";
    }
    DebugOverlay overlay{{.visible = true, .scale = 1}};
    const auto rectangles = [&](std::string line)
    {
        std::vector<Rectangle> areas;
        for (const ClearRectangle& rectangle : draw(overlay, {std::move(line)}).back().rectangles)
        {
            areas.push_back(rectangle.area);
        }
        return areas;
    };
    EXPECT_EQ(rectangles("\x01\xE9"), rectangles("??"));
    EXPECT_NE(rectangles("A"), rectangles("?"));
}

} // namespace
