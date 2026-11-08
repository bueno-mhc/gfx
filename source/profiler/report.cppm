/// @file report.cppm
/// @brief A Report: what a trace says per stage and per counter, over its frames — the mean, median, 95th and 99th
///        percentiles, minimum and maximum of each stage's time per frame and per call, the frames' times, the
///        counters' means and extremes — for regression checks, overlays and the benchmark's Markdown.
///        A stage is a site whose scopes ended in the frames; recursion counts once (a scope inside a scope of the
///        same site adds nothing). Percentiles are nearest-rank: the smallest value at least that fraction of the
///        values do not exceed.
export module gfx.profiler:report;

import std;
import gfx.profiler.enums;
import :trace;

export namespace gfx::profiler
{

/// @brief How to make a report.
struct ReportDescription
{
    std::uint32_t skipped_frames = 0; ///< The first frames left out (warm-up).
};

/// @brief Statistics of durations, in nanoseconds.
struct DurationStatistics
{
    std::uint64_t samples = 0; ///< How many values.
    double mean = 0.0;         ///< Their mean.
    double p50 = 0.0;          ///< Median.
    double p95 = 0.0;          ///< 95th percentile.
    double p99 = 0.0;          ///< 99th percentile.
    double minimum = 0.0;      ///< Smallest.
    double maximum = 0.0;      ///< Largest.

    [[nodiscard]] auto operator==(const DurationStatistics&) const -> bool = default;
};

/// @brief One site's scopes over the report's frames.
struct StageReport
{
    std::string name{};                                     ///< The site's name.
    ProfilerCategory category = ProfilerCategory::GENERAL;  ///< Its category.
    std::string file{};                                     ///< Where it is.
    std::uint32_t line = 0;                                 ///< Its line.
    std::uint64_t calls = 0;                                ///< Scopes counted (outermost of their site).
    double total = 0.0;                                     ///< Nanoseconds over every frame.
    DurationStatistics per_frame{};                         ///< Its nanoseconds in each frame (0 in frames without it).
    DurationStatistics per_call{};                          ///< Each scope's nanoseconds.
};

/// @brief One counter site's samples over the report's frames.
struct CounterReport
{
    std::string name{};                     ///< The site's name.
    RecordKind kind = RecordKind::COUNTER;  ///< COUNTER, COUNTER_FLOAT or MEMORY.
    std::uint64_t samples = 0;              ///< How many.
    double mean = 0.0;                      ///< Their mean.
    double minimum = 0.0;                   ///< Smallest.
    double maximum = 0.0;                   ///< Largest.
    double last = 0.0;                      ///< The last one.
};

/// @brief What a trace says.
struct Report
{
    std::string name{};                    ///< The session's name.
    std::uint64_t frames = 0;              ///< Frames counted.
    DurationStatistics frame_time{};       ///< Nanoseconds from each frame's begin to its end.
    std::vector<StageReport> stages{};     ///< Longest total first (ties: by name).
    std::vector<CounterReport> counters{}; ///< By name.
    std::uint64_t markers = 0;             ///< Markers recorded.
    std::uint64_t dropped_records = 0;     ///< Records lost to full rings.
    std::uint64_t unmatched_records = 0;   ///< Ends without begins and begins left open.
};

/// @brief Statistics of @p values (nearest-rank percentiles); zeros when empty.
[[nodiscard]] auto statistics_of(std::span<const double> values) -> DurationStatistics;

/// @brief What @p trace says.
[[nodiscard]] auto make_report(const Trace& trace,
                               const ReportDescription& description = {}) -> Report;

/// @brief @p report as Markdown tables (milliseconds).
[[nodiscard]] auto to_markdown(const Report& report) -> std::string;

} // namespace gfx::profiler
