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

    // Per frame: each site's outermost scopes, its samples.
    std::vector<StageTotals> stages(sites.size());
    std::vector<CounterTotals> counters(sites.size());
    std::vector<double> frame_times;
    frame_times.reserve(frames.size());
    std::vector<double> frame_total(sites.size());
    for (const TraceFrame& frame : frames)
    {
        frame_times.push_back(static_cast<double>(frame.end - frame.begin));
        std::ranges::fill(frame_total, 0.0);
        for (const std::size_t index : outermost_scopes(frame))
        {
            const TraceScope& scope = frame.scopes[index];
            const auto duration = static_cast<double>(scope.duration());
            frame_total[scope.site] += duration;
            stages[scope.site].per_call.push_back(duration);
            ++stages[scope.site].calls;
        }
        for (std::size_t site = 0; site < sites.size(); ++site)
        {
            stages[site].per_frame.push_back(frame_total[site]);
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

    for (std::size_t site = 0; site < sites.size(); ++site)
    {
        if (stages[site].calls != 0U)
        {
            const DurationStatistics per_frame = statistics_of(stages[site].per_frame);
            report.stages.push_back({.name = sites[site].name,
                                     .category = sites[site].category,
                                     .file = sites[site].file,
                                     .line = sites[site].line,
                                     .calls = stages[site].calls,
                                     .total = per_frame.mean * static_cast<double>(per_frame.samples),
                                     .per_frame = per_frame,
                                     .per_call = statistics_of(stages[site].per_call)});
        }
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
    std::ranges::sort(report.stages, [](const StageReport& first, const StageReport& second)
    {
        return first.total != second.total ? first.total > second.total : first.name < second.name;
    });
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
    text += "\n\n| Stage | Category | Calls | Total (ms) | Per frame: mean | p50 | p95 | p99 | max | Per call: mean |\n"
            "|---|---|---|---|---|---|---|---|---|---|\n";
    for (const StageReport& stage : report.stages)
    {
        text += std::format("| {} | {} | {} | {} | {} | {} | {} | {} | {} | {} |\n", stage.name, stage.category, stage.calls, milliseconds(stage.total),
                            milliseconds(stage.per_frame.mean), milliseconds(stage.per_frame.p50), milliseconds(stage.per_frame.p95),
                            milliseconds(stage.per_frame.p99), milliseconds(stage.per_frame.maximum), milliseconds(stage.per_call.mean));
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

} // namespace gfx::profiler
