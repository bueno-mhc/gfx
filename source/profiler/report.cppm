/// @file report.cppm
/// @brief A Report: what a trace says per stage and per counter, over its frames — the mean, median, 95th and 99th
///        percentiles, minimum and maximum of each stage's time per frame and per call, the frames' times, the
///        counters' means and extremes — for regression checks, overlays and the benchmark's Markdown.
///        A stage is a site whose scopes ended in the frames; recursion counts once (a scope inside a scope of the
///        same site adds nothing). Percentiles are nearest-rank: the smallest value at least that fraction of the
///        values do not exceed. With GPU scopes: GPU stages (timed scopes only), and per frame the CPU's busy time (the
///        union of every thread's outermost scopes), the GPU's (the union of every track's outermost timed scopes),
///        the share of the frame both were busy (overlap) and the share the GPU was idle.
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
    DurationStatistics cpu_time{};         ///< Nanoseconds each frame some thread was in an outermost scope.
    DurationStatistics gpu_time{};         ///< Nanoseconds each frame some GPU track was in an outermost timed scope.
    double overlap_ratio = 0.0;            ///< Mean share of a frame the CPU and the GPU were both busy.
    double idle_ratio = 0.0;               ///< Mean share of a frame the GPU was idle (1 without GPU scopes).
    std::vector<StageReport> stages{};     ///< Longest total first (ties: by name).
    std::vector<StageReport> gpu_stages{}; ///< GPU sites, longest total first.
    std::uint64_t untimed_gpu_scopes = 0;  ///< GPU scopes DROPPED, UNAVAILABLE or INCOMPLETE.
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

/// @brief A stage of a workload and how its traces show it; a workload's list of them says what a trace of it should
///        cover (missing_stages()).
struct StageCoverage
{
    std::string_view stage{};                             ///< The stage, as a design or a request names it.
    StageCoverageKind kind = StageCoverageKind::RECORDED; ///< How it shows.
    std::string_view site{};                              ///< RECORDED: the site of its scope (CPU or GPU); FUSED_IN: the
                                                          ///< site whose scope holds it; ABSENT: none.
    std::string_view reason{};                            ///< FUSED_IN and ABSENT: why it has no scope of its own.
};

/// @brief What @p trace lacks of @p stages, a line each: a RECORDED or FUSED_IN stage whose site has no scope (CPU or
///        GPU, timed or not) in any frame, a FUSED_IN or ABSENT stage without a reason. Empty: the trace covers them.
[[nodiscard]] auto missing_stages(const Trace& trace,
                                  std::span<const StageCoverage> stages) -> std::vector<std::string>;

} // namespace gfx::profiler
