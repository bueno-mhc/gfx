// Reports: per stage and per counter over a trace's frames, with nearest-rank percentiles.
module gfx.profiler;

import std;
import gfx.profiler.enums;

namespace gfx::profiler
{

namespace
{

/// The nearest-rank @p fraction percentile of sorted @p values (not empty).
auto
percentile(std::span<const double> values,
           double fraction) -> double
{
    const auto rank = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size())));
    return values[std::clamp<std::size_t>(rank, 1U, values.size()) - 1U];
}

/// What a site gathers over the frames.
struct StageTotals
{
    std::uint64_t calls = 0;
    std::vector<double> per_frame{};
    std::vector<double> per_call{};
};

/// What a counter site gathers.
struct CounterTotals
{
    RecordKind kind = RecordKind::COUNTER;
    std::vector<double> values{};
};

/// The scopes of @p frame that are the outermost of their site on their thread (a scope inside another scope of the
/// same site adds nothing), as indices into frame.scopes.
auto
outermost_scopes(const TraceFrame& frame) -> std::vector<std::size_t>
{
    std::vector<std::size_t> order(frame.scopes.size());
    std::ranges::iota(order, std::size_t{0});
    std::ranges::sort(order, [&frame](std::size_t first, std::size_t second)
    {
        const TraceScope& a = frame.scopes[first];
        const TraceScope& b = frame.scopes[second];
        return std::tie(a.thread, a.begin, a.depth) < std::tie(b.thread, b.begin, b.depth);
    });
    std::vector<std::size_t> kept;
    kept.reserve(order.size());
    std::vector<std::size_t> enclosing;
    std::uint32_t thread = 0;
    for (const std::size_t index : order)
    {
        const TraceScope& scope = frame.scopes[index];
        if (scope.thread != thread)
        {
            enclosing.clear();
            thread = scope.thread;
        }
        // The scopes still enclosing this one; it counts unless one of them has its site.
        while (!enclosing.empty() && frame.scopes[enclosing.back()].end < scope.end)
        {
            enclosing.pop_back();
        }
        const bool nested_in_same_site = std::ranges::any_of(enclosing, [&](std::size_t outer) { return frame.scopes[outer].site == scope.site; });
        if (!nested_in_same_site)
        {
            kept.push_back(index);
        }
        enclosing.push_back(index);
    }
    return kept;
}

/// Intervals [begin, end), merged into disjoint sorted ones.
using Interval = std::pair<std::int64_t, std::int64_t>;

auto
merged(std::vector<Interval>& intervals) -> std::vector<Interval>&
{
    std::ranges::sort(intervals);
    std::size_t kept = 0;
    for (const Interval& interval : intervals)
    {
        if (interval.second <= interval.first)
        {
            continue;
        }
        if (kept != 0 && interval.first <= intervals[kept - 1].second)
        {
            intervals[kept - 1].second = std::max(intervals[kept - 1].second, interval.second);
        }
        else
        {
            intervals[kept++] = interval;
        }
    }
    intervals.resize(kept);
    return intervals;
}

/// Total length of disjoint sorted @p intervals.
auto
length(std::span<const Interval> intervals) -> std::int64_t
{
    return std::ranges::fold_left(intervals, std::int64_t{0}, [](std::int64_t sum, const Interval& interval) { return sum + interval.second - interval.first; });
}

/// Length of the intersection of two sets of disjoint sorted intervals.
auto
intersection(std::span<const Interval> first,
             std::span<const Interval> second) -> std::int64_t
{
    std::int64_t total = 0;
    std::size_t a = 0;
    std::size_t b = 0;
    while (a < first.size() && b < second.size())
    {
        total += std::max<std::int64_t>(0, std::min(first[a].second, second[b].second) - std::max(first[a].first, second[b].first));
        if (first[a].second < second[b].second)
        {
            ++a;
        }
        else
        {
            ++b;
        }
    }
    return total;
}

/// Each GPU site's outermost timed scopes per track, as indices into frame.gpu_scopes (as outermost_scopes()).
auto
outermost_gpu_scopes(const TraceFrame& frame) -> std::vector<std::size_t>
{
    std::vector<std::size_t> order;
    for (std::size_t index = 0; index < frame.gpu_scopes.size(); ++index)
    {
        if (frame.gpu_scopes[index].status == GpuScopeStatus::TIMED)
        {
            order.push_back(index);
        }
    }
    std::ranges::sort(order, [&frame](std::size_t first, std::size_t second)
    {
        const TraceGpuScope& a = frame.gpu_scopes[first];
        const TraceGpuScope& b = frame.gpu_scopes[second];
        return std::tie(a.track, a.begin, a.depth) < std::tie(b.track, b.begin, b.depth);
    });
    std::vector<std::size_t> kept;
    std::vector<std::size_t> enclosing;
    std::uint32_t track = 0;
    for (const std::size_t index : order)
    {
        const TraceGpuScope& scope = frame.gpu_scopes[index];
        if (scope.track != track)
        {
            enclosing.clear();
            track = scope.track;
        }
        while (!enclosing.empty() && frame.gpu_scopes[enclosing.back()].end < scope.end)
        {
            enclosing.pop_back();
        }
        if (std::ranges::none_of(enclosing, [&](std::size_t outer) { return frame.gpu_scopes[outer].site == scope.site; }))
        {
            kept.push_back(index);
        }
        enclosing.push_back(index);
    }
    return kept;
}

/// The stage reports of @p totals (sites with calls), longest total first.
auto
stage_reports(std::span<const TraceSite> sites,
              std::span<const StageTotals> totals) -> std::vector<StageReport>
{
    std::vector<StageReport> stages;
    for (std::size_t site = 0; site < sites.size(); ++site)
    {
        if (totals[site].calls == 0U)
        {
            continue;
        }
        const DurationStatistics per_frame = statistics_of(totals[site].per_frame);
        stages.push_back({.name = sites[site].name,
                          .category = sites[site].category,
                          .file = sites[site].file,
                          .line = sites[site].line,
                          .calls = totals[site].calls,
                          .total = per_frame.mean * static_cast<double>(per_frame.samples),
                          .per_frame = per_frame,
                          .per_call = statistics_of(totals[site].per_call)});
    }
    std::ranges::sort(stages, [](const StageReport& first, const StageReport& second)
    {
        return first.total != second.total ? first.total > second.total : first.name < second.name;
    });
    return stages;
}

/// Nanoseconds as milliseconds with three decimals.
auto
milliseconds(double nanoseconds) -> std::string
{
    return std::format("{:.3f}", nanoseconds / 1.0e6);
}

} // namespace

auto
statistics_of(std::span<const double> values) -> DurationStatistics
{
    if (values.empty())
    {
        return {};
    }
    std::vector<double> sorted{values.begin(), values.end()};
    std::ranges::sort(sorted);
    const double sum = std::ranges::fold_left(sorted, 0.0, std::plus{});
    return {.samples = sorted.size(),
            .mean = sum / static_cast<double>(sorted.size()),
            .p50 = percentile(sorted, 0.50),
            .p95 = percentile(sorted, 0.95),
            .p99 = percentile(sorted, 0.99),
            .minimum = sorted.front(),
            .maximum = sorted.back()};
}

auto
make_report(const Trace& trace,
            const ReportDescription& description) -> Report
{
    const std::span<const TraceFrame> all_frames = trace.frames();
    const std::span<const TraceFrame> frames = all_frames.subspan(std::min<std::size_t>(description.skipped_frames, all_frames.size()));
    const std::span<const TraceSite> sites = trace.sites();
    Report report{.name = std::string{trace.name()}, .frames = frames.size()};

    // Per frame: each site's outermost scopes (CPU and GPU), its samples, the CPU's and the GPU's busy time.
    std::vector<StageTotals> stages(sites.size());
    std::vector<StageTotals> gpu_stages(sites.size());
    std::vector<CounterTotals> counters(sites.size());
    std::vector<double> frame_times;
    frame_times.reserve(frames.size());
    std::vector<double> cpu_times;
    std::vector<double> gpu_times;
    double overlap = 0.0;
    double idle = 0.0;
    std::size_t timed_frames = 0;
    std::vector<double> frame_total(sites.size());
    std::vector<double> gpu_frame_total(sites.size());
    std::vector<Interval> cpu_busy;
    std::vector<Interval> gpu_busy;
    for (const TraceFrame& frame : frames)
    {
        frame_times.push_back(static_cast<double>(frame.end - frame.begin));
        std::ranges::fill(frame_total, 0.0);
        std::ranges::fill(gpu_frame_total, 0.0);
        cpu_busy.clear();
        gpu_busy.clear();
        for (const std::size_t index : outermost_scopes(frame))
        {
            const TraceScope& scope = frame.scopes[index];
            const auto duration = static_cast<double>(scope.duration());
            frame_total[scope.site] += duration;
            stages[scope.site].per_call.push_back(duration);
            ++stages[scope.site].calls;
        }
        for (const std::size_t index : outermost_gpu_scopes(frame))
        {
            const TraceGpuScope& scope = frame.gpu_scopes[index];
            const auto duration = static_cast<double>(scope.duration());
            gpu_frame_total[scope.site] += duration;
            gpu_stages[scope.site].per_call.push_back(duration);
            ++gpu_stages[scope.site].calls;
        }
        for (std::size_t site = 0; site < sites.size(); ++site)
        {
            stages[site].per_frame.push_back(frame_total[site]);
            gpu_stages[site].per_frame.push_back(gpu_frame_total[site]);
        }

        // Busy time inside the frame's window: outermost CPU scopes of every thread, outermost timed GPU scopes.
        for (const TraceScope& scope : frame.scopes)
        {
            if (scope.depth == 0U)
            {
                cpu_busy.emplace_back(std::max(scope.begin, frame.begin), std::min(scope.end, frame.end));
            }
        }
        for (const TraceGpuScope& scope : frame.gpu_scopes)
        {
            if (scope.depth == 0U && scope.status == GpuScopeStatus::TIMED)
            {
                gpu_busy.emplace_back(std::max(scope.begin, frame.begin), std::min(scope.end, frame.end));
            }
            report.untimed_gpu_scopes += scope.status == GpuScopeStatus::TIMED ? 0U : 1U;
        }
        const std::int64_t cpu = length(merged(cpu_busy));
        const std::int64_t gpu = length(merged(gpu_busy));
        cpu_times.push_back(static_cast<double>(cpu));
        gpu_times.push_back(static_cast<double>(gpu));
        if (frame.end > frame.begin)
        {
            const auto duration = static_cast<double>(frame.end - frame.begin);
            overlap += static_cast<double>(intersection(cpu_busy, gpu_busy)) / duration;
            idle += (duration - static_cast<double>(gpu)) / duration;
            ++timed_frames;
        }
        for (const TraceSample& sample : frame.samples)
        {
            if (sample.kind == RecordKind::MARKER)
            {
                ++report.markers;
                continue;
            }
            counters[sample.site].kind = sample.kind;
            counters[sample.site].values.push_back(sample.number());
        }
        report.dropped_records += frame.dropped_records;
        report.unmatched_records += frame.unmatched_records;
    }
    report.frame_time = statistics_of(frame_times);
    report.cpu_time = statistics_of(cpu_times);
    report.gpu_time = statistics_of(gpu_times);
    report.overlap_ratio = timed_frames == 0U ? 0.0 : overlap / static_cast<double>(timed_frames);
    report.idle_ratio = timed_frames == 0U ? 0.0 : idle / static_cast<double>(timed_frames);
    report.stages = stage_reports(sites, stages);
    report.gpu_stages = stage_reports(sites, gpu_stages);

    for (std::size_t site = 0; site < sites.size(); ++site)
    {
        if (!counters[site].values.empty())
        {
            const std::vector<double>& values = counters[site].values;
            report.counters.push_back({.name = sites[site].name,
                                       .kind = counters[site].kind,
                                       .samples = values.size(),
                                       .mean = std::ranges::fold_left(values, 0.0, std::plus{}) / static_cast<double>(values.size()),
                                       .minimum = std::ranges::min(values),
                                       .maximum = std::ranges::max(values),
                                       .last = values.back()});
        }
    }
    std::ranges::sort(report.counters, {}, &CounterReport::name);
    return report;
}

auto
to_markdown(const Report& report) -> std::string
{
    std::string text = std::format("## {}\n\n{} frames; frame time (ms): mean {}, p50 {}, p95 {}, p99 {}, max {}", report.name, report.frames,
                                   milliseconds(report.frame_time.mean), milliseconds(report.frame_time.p50), milliseconds(report.frame_time.p95),
                                   milliseconds(report.frame_time.p99), milliseconds(report.frame_time.maximum));
    if (report.dropped_records != 0U || report.unmatched_records != 0U)
    {
        text += std::format("; {} records dropped, {} unmatched", report.dropped_records, report.unmatched_records);
    }
    if (!report.gpu_stages.empty() || report.untimed_gpu_scopes != 0U)
    {
        text += std::format("\n\nCPU busy (ms): mean {}, p95 {}; GPU busy (ms): mean {}, p95 {}; overlap {:.1f} %, GPU idle {:.1f} %",
                            milliseconds(report.cpu_time.mean), milliseconds(report.cpu_time.p95), milliseconds(report.gpu_time.mean),
                            milliseconds(report.gpu_time.p95), 100.0 * report.overlap_ratio, 100.0 * report.idle_ratio);
        if (report.untimed_gpu_scopes != 0U)
        {
            text += std::format("; {} GPU scopes untimed", report.untimed_gpu_scopes);
        }
    }
    const auto stage_table = [&text](std::string_view title, std::span<const StageReport> stages)
    {
        text += std::format("\n\n| {} | Category | Calls | Total (ms) | Per frame: mean | p50 | p95 | p99 | max | Per call: mean |\n"
                            "|---|---|---|---|---|---|---|---|---|---|\n",
                            title);
        for (const StageReport& stage : stages)
        {
            text += std::format("| {} | {} | {} | {} | {} | {} | {} | {} | {} | {} |\n", stage.name, stage.category, stage.calls,
                                milliseconds(stage.total), milliseconds(stage.per_frame.mean), milliseconds(stage.per_frame.p50),
                                milliseconds(stage.per_frame.p95), milliseconds(stage.per_frame.p99), milliseconds(stage.per_frame.maximum),
                                milliseconds(stage.per_call.mean));
        }
    };
    stage_table("Stage", report.stages);
    if (!report.gpu_stages.empty())
    {
        stage_table("GPU stage", report.gpu_stages);
    }
    if (!report.counters.empty())
    {
        text += "\n| Counter | Kind | Samples | Mean | Min | Max | Last |\n|---|---|---|---|---|---|---|\n";
        for (const CounterReport& counter : report.counters)
        {
            text += std::format("| {} | {} | {} | {:.3f} | {:.3f} | {:.3f} | {:.3f} |\n", counter.name, counter.kind, counter.samples, counter.mean,
                                counter.minimum, counter.maximum, counter.last);
        }
    }
    return text;
}

auto
missing_stages(const Trace& trace,
               std::span<const StageCoverage> stages) -> std::vector<std::string>
{
    // The names of the sites some scope of the trace has (CPU or GPU).
    const std::span<const TraceSite> sites = trace.sites();
    std::vector<bool> scoped(sites.size(), false);
    for (const TraceFrame& frame : trace.frames())
    {
        for (const TraceScope& scope : frame.scopes)
        {
            scoped[scope.site] = true;
        }
        for (const TraceGpuScope& scope : frame.gpu_scopes)
        {
            scoped[scope.site] = true;
        }
    }
    std::set<std::string_view> names;
    for (std::size_t site = 0; site < sites.size(); ++site)
    {
        if (scoped[site])
        {
            names.insert(sites[site].name);
        }
    }

    // Each stage against them.
    std::vector<std::string> missing;
    for (const StageCoverage& stage : stages)
    {
        if (stage.kind != StageCoverageKind::RECORDED && stage.reason.empty())
        {
            missing.push_back(std::format("'{}' is {} without a reason", stage.stage, stage.kind));
        }
        if (stage.kind != StageCoverageKind::ABSENT && !names.contains(stage.site))
        {
            missing.push_back(std::format("'{}' ({} '{}'): no scope of that site in the trace", stage.stage, stage.kind, stage.site));
        }
    }
    return missing;
}

} // namespace gfx::profiler
