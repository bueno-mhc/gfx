// Traces: the value a session becomes, and its canonical form (what two runs of one workload share).
module gfx.profiler;

import std;
import gfx.profiler.enums;

namespace gfx::profiler
{

Trace::Trace(std::string name,
             std::vector<TraceSite> sites,
             std::vector<TraceThread> threads,
             std::vector<TraceFrame> frames,
             std::vector<TraceGpuTrack> gpu_tracks)
    : m_name{std::move(name)}
    , m_sites{std::move(sites)}
    , m_threads{std::move(threads)}
    , m_frames{std::move(frames)}
    , m_gpu_tracks{std::move(gpu_tracks)}
{
}

auto
Trace::gpu_tracks() const noexcept -> std::span<const TraceGpuTrack>
{
    return m_gpu_tracks;
}

auto
Trace::name() const noexcept -> std::string_view
{
    return m_name;
}

auto
Trace::sites() const noexcept -> std::span<const TraceSite>
{
    return m_sites;
}

auto
Trace::threads() const noexcept -> std::span<const TraceThread>
{
    return m_threads;
}

auto
Trace::frames() const noexcept -> std::span<const TraceFrame>
{
    return m_frames;
}

auto
Trace::find_site(std::string_view name) const noexcept -> std::optional<std::uint32_t>
{
    const auto found = std::ranges::find(m_sites, name, &TraceSite::name);
    return found == m_sites.end() ? std::nullopt : std::optional{static_cast<std::uint32_t>(found - m_sites.begin())};
}

auto
canonical_trace(const Trace& trace) -> std::string
{
    // Sites ranked by name, file and line; threads by name (then by their first appearance).
    const std::span<const TraceSite> sites = trace.sites();
    std::vector<std::uint32_t> site_order(sites.size());
    std::ranges::iota(site_order, 0U);
    std::ranges::sort(site_order, [&sites](std::uint32_t first, std::uint32_t second)
    {
        return std::tie(sites[first].name, sites[first].file, sites[first].line) < std::tie(sites[second].name, sites[second].file, sites[second].line);
    });
    std::vector<std::uint32_t> site_rank(sites.size());
    for (std::uint32_t position = 0; position < site_order.size(); ++position)
    {
        site_rank[site_order[position]] = position;
    }
    const std::span<const TraceThread> threads = trace.threads();
    std::vector<std::uint32_t> thread_order(threads.size());
    std::ranges::iota(thread_order, 0U);
    std::ranges::stable_sort(thread_order, {}, [&threads](std::uint32_t thread) -> const std::string& { return threads[thread].name; });

    std::string text = std::format("trace '{}'\n", trace.name());
    for (std::uint32_t position = 0; position < site_order.size(); ++position)
    {
        const TraceSite& site = sites[site_order[position]];
        text += std::format("site {} '{}' {} {}:{}\n", position, site.name, site.category, site.file, site.line);
    }
    for (std::uint32_t position = 0; position < thread_order.size(); ++position)
    {
        text += std::format("thread {} '{}'\n", position, threads[thread_order[position]].name);
    }

    // GPU tracks ranked by name, as threads.
    const std::span<const TraceGpuTrack> tracks = trace.gpu_tracks();
    std::vector<std::uint32_t> track_order(tracks.size());
    std::ranges::iota(track_order, 0U);
    std::ranges::stable_sort(track_order, {}, [&tracks](std::uint32_t track) -> const std::string& { return tracks[track].name; });
    std::vector<std::uint32_t> track_rank(tracks.size());
    for (std::uint32_t position = 0; position < track_order.size(); ++position)
    {
        track_rank[track_order[position]] = position;
        text += std::format("gpu track {} '{}' {}\n", position, tracks[track_order[position]].name, tracks[track_order[position]].alignment);
    }

    // Each frame, thread by thread: scopes in the order they began, samples in the order recorded; then the spans.
    std::vector<TraceScope> scopes;
    std::vector<TraceAsyncSpan> spans;
    std::vector<TraceGpuScope> gpu_scopes;
    std::vector<std::uint32_t> thread_rank(threads.size());
    for (std::uint32_t position = 0; position < thread_order.size(); ++position)
    {
        thread_rank[thread_order[position]] = position;
    }
    for (const TraceFrame& frame : trace.frames())
    {
        text += std::format("frame {} dropped {} unmatched {}\n", frame.index, frame.dropped_records, frame.unmatched_records);
        for (const std::uint32_t thread : thread_order)
        {
            scopes.clear();
            std::ranges::copy_if(frame.scopes, std::back_inserter(scopes), [thread](const TraceScope& scope) { return scope.thread == thread; });
            // Stable: sibling scopes that began in the same clock tick keep the order they ended in (the order they ran).
            std::ranges::stable_sort(scopes, [](const TraceScope& first, const TraceScope& second)
            {
                return std::tie(first.begin, first.depth) < std::tie(second.begin, second.depth);
            });
            for (const TraceScope& scope : scopes)
            {
                text += std::format("  scope thread {} site {} depth {}\n", thread_rank[thread], site_rank[scope.site], scope.depth);
            }
            for (const TraceSample& sample : frame.samples)
            {
                if (sample.thread == thread)
                {
                    text += std::format("  sample thread {} site {} {} {}\n", thread_rank[thread], site_rank[sample.site], sample.kind, sample.value);
                }
            }
        }
        spans.assign(frame.async_spans.begin(), frame.async_spans.end());
        std::ranges::sort(spans, [&site_rank](const TraceAsyncSpan& first, const TraceAsyncSpan& second)
        {
            return std::pair{site_rank[first.site], first.token} < std::pair{site_rank[second.site], second.token};
        });
        for (const TraceAsyncSpan& span : spans)
        {
            text += std::format("  span site {} token {} threads {} {}\n", site_rank[span.site], span.token, thread_rank[span.begin_thread],
                                thread_rank[span.end_thread]);
        }
        gpu_scopes.assign(frame.gpu_scopes.begin(), frame.gpu_scopes.end());
        std::ranges::sort(gpu_scopes, [&track_rank](const TraceGpuScope& first, const TraceGpuScope& second)
        {
            return std::pair{track_rank[first.track], first.query} < std::pair{track_rank[second.track], second.query};
        });
        for (const TraceGpuScope& scope : gpu_scopes)
        {
            text += std::format("  gpu track {} site {} depth {} query {} {}\n", track_rank[scope.track], site_rank[scope.site], scope.depth, scope.query,
                                scope.status);
        }
    }
    return text;
}

} // namespace gfx::profiler
