// Reports: nearest-rank percentiles, per-frame and per-call stage statistics (recursion counted once), skipped frames,
// counters, Markdown — on traces built by hand, so the expected numbers are exact.
#include <gtest/gtest.h>

import std;
import gfx.profiler;

namespace
{

using namespace gfx::profiler;

/// Two frames: "step" with two "inner" scopes and a recursive "step" inside it, then a longer "step"; a counter in each.
auto
hand_built_trace() -> Trace
{
    std::vector<TraceSite> sites{
        {.identifier = 1, .name_identifier = 11, .name = "step", .category = ProfilerCategory::PHYSICS, .file = "step.cpp", .line = 10},
        {.identifier = 2, .name_identifier = 12, .name = "inner", .category = ProfilerCategory::PHYSICS, .file = "step.cpp", .line = 20},
        {.identifier = 3, .name_identifier = 13, .name = "bodies", .category = ProfilerCategory::GENERAL, .file = "step.cpp", .line = 30},
        {.identifier = 4, .name_identifier = 14, .name = "rebuilt", .category = ProfilerCategory::GENERAL, .file = "step.cpp", .line = 40},
        {.identifier = 5, .name_identifier = 15, .name = "dispatch", .category = ProfilerCategory::GPU, .file = "step.cpp", .line = 50},
    };
    std::vector<TraceFrame> frames(2);
    frames[0] = {.index = 0,
                 .begin = 0,
                 .end = 120,
                 .scopes = {{.thread = 0, .site = 1, .begin = 10, .end = 30, .depth = 1},
                            {.thread = 0, .site = 1, .begin = 40, .end = 50, .depth = 1},
                            {.thread = 0, .site = 0, .begin = 60, .end = 90, .depth = 1},
                            {.thread = 0, .site = 0, .begin = 0, .end = 100, .depth = 0}},
                 .samples = {{.thread = 0, .site = 2, .time = 5, .kind = RecordKind::COUNTER, .value = 5},
                             {.thread = 0, .site = 3, .time = 6, .kind = RecordKind::MARKER, .value = 0}},
                 .gpu_scopes = {{.track = 0, .site = 4, .begin = 50, .end = 110, .depth = 0, .query = 0, .status = GpuScopeStatus::TIMED},
                                {.track = 0, .site = 4, .begin = 60, .end = 80, .depth = 1, .query = 2, .status = GpuScopeStatus::TIMED},
                                {.track = 0, .site = 4, .begin = 90, .end = 90, .depth = 1, .query = 4, .status = GpuScopeStatus::DROPPED}},
                 .dropped_records = 1};
    frames[1] = {.index = 1,
                 .begin = 120,
                 .end = 400,
                 .scopes = {{.thread = 0, .site = 0, .begin = 130, .end = 330, .depth = 0}},
                 .samples = {{.thread = 0, .site = 2, .time = 135, .kind = RecordKind::COUNTER, .value = 7}},
                 .unmatched_records = 2};
    return Trace{"hand built", std::move(sites), {{.name = "main"}}, std::move(frames),
                 {{.name = "GPU", .alignment = GpuClockAlignment::CALIBRATED, .deviation = 0}}};
}

TEST(ProfilerReportTest, PercentilesAreNearestRank)
{
    std::vector<double> values(100);
    std::iota(values.begin(), values.end(), 1.0);
    std::ranges::shuffle(values, std::mt19937{7});
    const DurationStatistics statistics = statistics_of(values);
    EXPECT_EQ(statistics.samples, 100U);
    EXPECT_EQ(statistics.mean, 50.5);
    EXPECT_EQ(statistics.p50, 50.0);
    EXPECT_EQ(statistics.p95, 95.0);
    EXPECT_EQ(statistics.p99, 99.0);
    EXPECT_EQ(statistics.minimum, 1.0);
    EXPECT_EQ(statistics.maximum, 100.0);

    // Few values: the smallest value at least the fraction do not exceed.
    const std::array three{30.0, 10.0, 20.0};
    const DurationStatistics small = statistics_of(three);
    EXPECT_EQ(small.p50, 20.0);
    EXPECT_EQ(small.p95, 30.0);
    EXPECT_EQ(statistics_of({}), DurationStatistics{});
}

TEST(ProfilerReportTest, StagesCountTheirOutermostScopesPerFrameAndPerCall)
{
    const Report report = make_report(hand_built_trace());
    EXPECT_EQ(report.name, "hand built");
    EXPECT_EQ(report.frames, 2U);
    EXPECT_EQ(report.frame_time.mean, 200.0);
    EXPECT_EQ(report.dropped_records, 1U);
    EXPECT_EQ(report.unmatched_records, 2U);
    EXPECT_EQ(report.markers, 1U);

    // "step": 100 then 200 (the recursive scope inside the first adds nothing); "inner": 30 then 0.
    ASSERT_EQ(report.stages.size(), 2U);
    const StageReport& step = report.stages[0];
    EXPECT_EQ(step.name, "step");
    EXPECT_EQ(step.category, ProfilerCategory::PHYSICS);
    EXPECT_EQ(step.calls, 2U);
    EXPECT_EQ(step.total, 300.0);
    EXPECT_EQ(step.per_frame.mean, 150.0);
    EXPECT_EQ(step.per_frame.p50, 100.0);
    EXPECT_EQ(step.per_frame.p99, 200.0);
    EXPECT_EQ(step.per_call.mean, 150.0);
    const StageReport& inner = report.stages[1];
    EXPECT_EQ(inner.name, "inner");
    EXPECT_EQ(inner.calls, 2U);
    EXPECT_EQ(inner.total, 30.0);
    EXPECT_EQ(inner.per_frame.mean, 15.0);
    EXPECT_EQ(inner.per_frame.minimum, 0.0);
    EXPECT_EQ(inner.per_call.mean, 15.0);
    EXPECT_EQ(inner.per_call.maximum, 20.0);

    ASSERT_EQ(report.counters.size(), 1U);
    EXPECT_EQ(report.counters[0].name, "bodies");
    EXPECT_EQ(report.counters[0].samples, 2U);
    EXPECT_EQ(report.counters[0].mean, 6.0);
    EXPECT_EQ(report.counters[0].minimum, 5.0);
    EXPECT_EQ(report.counters[0].maximum, 7.0);
    EXPECT_EQ(report.counters[0].last, 7.0);
}

TEST(ProfilerReportTest, GpuScopesGiveGpuStagesBusyTimesOverlapAndIdleShares)
{
    // Frame 0 (120 ns): CPU busy [0, 100], GPU busy [50, 110]: overlap 50, GPU idle 60. Frame 1 (280 ns): no GPU work.
    const Report report = make_report(hand_built_trace());
    EXPECT_EQ(report.cpu_time.minimum, 100.0);
    EXPECT_EQ(report.cpu_time.maximum, 200.0);
    EXPECT_EQ(report.gpu_time.maximum, 60.0);
    EXPECT_EQ(report.gpu_time.minimum, 0.0);
    EXPECT_DOUBLE_EQ(report.overlap_ratio, (50.0 / 120.0 + 0.0) / 2.0);
    EXPECT_DOUBLE_EQ(report.idle_ratio, (60.0 / 120.0 + 1.0) / 2.0);
    EXPECT_EQ(report.untimed_gpu_scopes, 1U);

    // "dispatch": its outermost scope (60 ns) once; the nested one adds nothing, the dropped one is not timed.
    ASSERT_EQ(report.gpu_stages.size(), 1U);
    EXPECT_EQ(report.gpu_stages[0].name, "dispatch");
    EXPECT_EQ(report.gpu_stages[0].calls, 1U);
    EXPECT_EQ(report.gpu_stages[0].total, 60.0);
    EXPECT_EQ(report.gpu_stages[0].per_frame.maximum, 60.0);

    const std::string markdown = to_markdown(report);
    EXPECT_NE(markdown.find("| GPU stage |"), std::string::npos) << markdown;
    EXPECT_NE(markdown.find("overlap 20.8 %, GPU idle 75.0 %; 1 GPU scopes untimed"), std::string::npos) << markdown;
}

TEST(ProfilerReportTest, SkippedFramesAreLeftOut)
{
    const Report report = make_report(hand_built_trace(), {.skipped_frames = 1});
    EXPECT_EQ(report.frames, 1U);
    EXPECT_EQ(report.frame_time.mean, 280.0);
    ASSERT_EQ(report.stages.size(), 1U);
    EXPECT_EQ(report.stages[0].name, "step");
    EXPECT_EQ(report.stages[0].total, 200.0);
    EXPECT_EQ(report.dropped_records, 0U);
    EXPECT_EQ(make_report(hand_built_trace(), {.skipped_frames = 5}).frames, 0U);
}

TEST(ProfilerReportTest, MarkdownListsStagesAndCounters)
{
    const std::string markdown = to_markdown(make_report(hand_built_trace()));
    EXPECT_TRUE(markdown.starts_with("## hand built")) << markdown;
    EXPECT_NE(markdown.find("| step | PHYSICS | 2 | 0.000 |"), std::string::npos) << markdown;
    EXPECT_NE(markdown.find("| bodies | COUNTER | 2 | 6.000 | 5.000 | 7.000 | 7.000 |"), std::string::npos) << markdown;
    EXPECT_NE(markdown.find("1 records dropped, 2 unmatched"), std::string::npos) << markdown;
}

} // namespace
