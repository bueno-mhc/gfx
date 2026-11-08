// The Profiler: sessions, frames, and the collection of records into frames (scope begins paired with their ends, spans'
// begins with their ends, samples kept in order), the live sinks.
module gfx.profiler;

import std;
import gfx.core.handles;
import gfx.profiler.enums;
import gfx.profiler.handles;
import gfx.profiler.errors;
import :state;

namespace gfx::profiler
{

namespace
{

/// The index of @p site in @p data's sites, added on first sight (sites alike in name, file and line are one).
auto
site_index(SessionData& data,
           const ProfileSite& site) -> std::uint32_t
{
    const auto [entry, added] = data.site_indices.try_emplace(site.identifier(), static_cast<std::uint32_t>(data.sites.size()));
    if (added)
    {
        data.sites.push_back({.identifier = site.identifier(),
                              .name_identifier = site.name_identifier(),
                              .name = std::string{site.name()},
                              .category = site.category(),
                              .file = std::string{site.file()},
                              .line = site.line()});
    }
    return entry->second;
}

/// The trace thread of @p context in @p data, added on first sight.
auto
thread_index(SessionData& data,
             ThreadSessionState& state,
             const ThreadContext& context) -> std::uint32_t
{
    if (state.trace_thread == ThreadSessionState::NO_THREAD)
    {
        state.trace_thread = static_cast<std::uint32_t>(data.threads.size());
        data.threads.push_back({.name = context.name});
    }
    return state.trace_thread;
}

/// Files @p record (from @p context) into @p frame or into @p data's open scopes and spans.
void
collect(SessionData& data,
        TraceFrame& frame,
        ThreadSessionState& state,
        const ThreadContext& context,
        const ProfileRecord& record)
{
    const std::int64_t time = record.time - data.start;
    const ProfileSite& site = *record.site;
    switch (record.kind)
    {
        case RecordKind::SCOPE_BEGIN:
            state.open.push_back({.site = record.site, .begin = time});
            break;
        case RecordKind::SCOPE_END:
        {
            // The last unmatched begin of the site; scopes begun after it lost their ends (a full ring).
            const auto found = std::ranges::find_if(state.open.rbegin(), state.open.rend(),
                                                    [&site](const OpenScope& open) { return open.site->identifier() == site.identifier(); });
            if (found == state.open.rend())
            {
                ++frame.unmatched_records;
                break;
            }
            const auto depth = static_cast<std::size_t>(state.open.rend() - found) - 1U;
            frame.unmatched_records += state.open.size() - depth - 1U;
            frame.scopes.push_back({.thread = thread_index(data, state, context),
                                    .site = site_index(data, site),
                                    .begin = state.open[depth].begin,
                                    .end = time,
                                    .depth = static_cast<std::uint32_t>(depth)});
            state.open.resize(depth);
            break;
        }
        case RecordKind::COUNTER:
        case RecordKind::COUNTER_FLOAT:
        case RecordKind::MEMORY:
        case RecordKind::MARKER:
            frame.samples.push_back(
                {.thread = thread_index(data, state, context), .site = site_index(data, site), .time = time, .kind = record.kind, .value = record.value});
            break;
        case RecordKind::ASYNC_BEGIN:
        {
            const OpenAsync open{.site = site_index(data, site), .thread = thread_index(data, state, context), .begin = time};
            const auto [entry, added] = data.open_async.insert_or_assign({site.name_identifier(), record.value}, open);
            frame.unmatched_records += added ? 0U : 1U;
            break;
        }
        case RecordKind::ASYNC_END:
        {
            const auto found = data.open_async.find({site.name_identifier(), record.value});
            if (found == data.open_async.end())
            {
                ++frame.unmatched_records;
                break;
            }
            frame.async_spans.push_back({.site = found->second.site,
                                         .token = record.value,
                                         .begin_thread = found->second.thread,
                                         .end_thread = thread_index(data, state, context),
                                         .begin = found->second.begin,
                                         .end = time});
            data.open_async.erase(found);
            break;
        }
    }
}

/// Throws ProfilerError: @p handle is not one of the profiler's.
[[noreturn]] void
throw_unknown(std::string_view operation,
              std::string_view what)
{
    throw ProfilerError({.subsystem = ProfilerSubsystem::SESSIONS,
                         .operation = std::string{operation},
                         .problem = std::format("{} is not one of this profiler's", what),
                         .suggestion = "use the handles the same Profiler returned"});
}

} // namespace

// ===================================================================================================== Implementation

Profiler::Implementation::Implementation(const ProfilerDescription& profiler_description)
    : id{[]
         {
             static std::atomic<std::uint64_t> next{1};
             return next.fetch_add(1, std::memory_order_relaxed);
         }()}
    , description{profiler_description}
{
}

auto
Profiler::Implementation::session(SessionHandle session,
                                  std::string_view operation) -> SessionData&
{
    const std::uint32_t index = index_of(session);
    if (!session.is_valid() || index >= sessions.size() || sessions[index].handle != session)
    {
        throw_unknown(operation, std::format("{}", session));
    }
    return sessions[index];
}

auto
Profiler::Implementation::session_handle(std::uint32_t index) -> SessionHandle
{
    return SessionHandle::create(handle_key(), index, 1);
}

auto
Profiler::Implementation::sink_handle(std::uint32_t index,
                                      std::uint32_t generation) -> SinkHandle
{
    return SinkHandle::create(handle_key(), index, generation);
}

auto
Profiler::Implementation::gpu_source_handle(std::uint32_t index) -> GpuSourceHandle
{
    return GpuSourceHandle::create(handle_key(), index, 1);
}

auto
Profiler::Implementation::index_of(SessionHandle handle) noexcept -> std::uint32_t
{
    return handle.is_valid() ? handle.index(handle_key()) : 0U;
}

auto
Profiler::Implementation::index_of(SinkHandle handle) noexcept -> std::uint32_t
{
    return handle.is_valid() ? handle.index(handle_key()) : 0U;
}

void
Profiler::Implementation::end_frame(SessionData& data)
{
    const std::int64_t end = now_nanoseconds();
    TraceFrame frame{.index = data.frames.size(), .begin = data.frame_begin - data.start, .end = end - data.start};
    {
        // Every thread's ring, in registration order (registrations wait meanwhile). Threads registered since the
        // session began have dropped nothing before it.
        const std::scoped_lock lock{mutex};
        data.per_context.resize(contexts.size());
        for (const std::unique_ptr<ThreadContext>& context : contexts)
        {
            ThreadSessionState& state = data.per_context[context->index];
            context->ring.drain([&](const ProfileRecord& record) { collect(data, frame, state, *context, record); });
            const std::uint64_t dropped = context->ring.dropped();
            frame.dropped_records += dropped - state.dropped_seen;
            state.dropped_seen = dropped;
            if (state.trace_thread != ThreadSessionState::NO_THREAD)
            {
                data.threads[state.trace_thread].name = context->name;
            }
        }
    }
    data.frames.push_back(std::move(frame));
    data.frame_begin = end;
    data.frame_open = false;

    // GPU scopes resolved since the last frame, filed into the frames they began in.
    collect_gpu(data, false, false);

    // The live sinks see the frame (on this thread).
    for (SinkEntry& sink : sinks)
    {
        sink.function(data.frames.back(), data.sites, data.threads);
    }
}

void
Profiler::Implementation::collect_gpu(SessionData& data,
                                      bool wait,
                                      bool file_all)
{
    // Each source's resolved scopes, its track made on the first ones, the categories the session records only.
    for (const GpuSourceEntry& entry : gpu_sources)
    {
        collected_gpu.clear();
        entry.source->collect(collected_gpu, wait);
        if (collected_gpu.empty())
        {
            continue;
        }
        const auto [track, added] = data.gpu_track_of.try_emplace(entry.source, static_cast<std::uint32_t>(data.gpu_tracks.size()));
        if (added)
        {
            data.gpu_tracks.push_back({.name = entry.source->track_name(),
                                       .alignment = entry.source->alignment(),
                                       .deviation = entry.source->alignment_deviation().count()});
        }
        for (const GpuScopeRecord& record : collected_gpu)
        {
            if (record.site == nullptr || !has_any(record.site->category(), data.description.categories))
            {
                continue;
            }
            data.pending_gpu.push_back({.track = track->second,
                                        .site = site_index(data, *record.site),
                                        .begin = record.begin - data.start,
                                        .end = record.end - data.start,
                                        .depth = record.depth,
                                        .query = record.query,
                                        .status = record.status});
        }
    }

    // Each into the first frame that ended after it began; later ones wait for their frame (or go into the last).
    if (data.frames.empty())
    {
        if (file_all)
        {
            data.pending_gpu.clear();
        }
        return;
    }
    auto kept = data.pending_gpu.begin();
    for (const TraceGpuScope& scope : data.pending_gpu)
    {
        auto frame = std::ranges::upper_bound(data.frames, scope.begin, {}, &TraceFrame::end);
        if (frame == data.frames.end())
        {
            if (!file_all)
            {
                *kept++ = scope;
                continue;
            }
            frame = std::prev(data.frames.end());
        }
        frame->gpu_scopes.push_back(scope);
    }
    data.pending_gpu.erase(kept, data.pending_gpu.end());
}

void
Profiler::Implementation::discard_records()
{
    const std::scoped_lock lock{mutex};
    for (const std::unique_ptr<ThreadContext>& context : contexts)
    {
        context->ring.drain([](const ProfileRecord&) {});
    }
}

// ===================================================================================================== Profiler

Profiler::Profiler(const ProfilerDescription& description)
{
    const std::uint32_t capacity = description.ring_capacity;
    if (capacity < 2U || capacity > MAXIMUM_PROFILER_RING_CAPACITY || !std::has_single_bit(capacity))
    {
        throw ProfilerError({.subsystem = ProfilerSubsystem::RECORDING,
                             .operation = "Profiler::Profiler",
                             .problem = std::format("a ring capacity of {} records is not a power of two in [2, {}]", capacity,
                                                    MAXIMUM_PROFILER_RING_CAPACITY),
                             .suggestion = std::format("use a power of two such as the default, {}", DEFAULT_PROFILER_RING_CAPACITY)});
    }
    m_implementation = std::make_unique<Implementation>(description);
}

Profiler::~Profiler()
{
    for (const GpuSourceEntry& entry : m_implementation->gpu_sources)
    {
        entry.source->m_attachments.fetch_sub(1, std::memory_order_acq_rel);
    }
    forget_current(*m_implementation);
}

auto
Profiler::description() const noexcept -> const ProfilerDescription&
{
    return m_implementation->description;
}

auto
Profiler::create_session(const SessionDescription& description) -> SessionHandle
{
    Implementation& implementation = *m_implementation;
    for (const SessionData& data : implementation.sessions)
    {
        if (data.state != SessionState::FINALIZED)
        {
            throw ProfilerError({.subsystem = ProfilerSubsystem::SESSIONS,
                                 .operation = "Profiler::create_session",
                                 .problem = std::format("{} ('{}') still {}", data.handle, data.description.name,
                                                        data.state == SessionState::RECORDING ? "records" : "waits to be finalized"),
                                 .suggestion = "finalize it first (one session records at a time)"});
        }
    }
    if (is_empty(description.categories))
    {
        throw ProfilerError({.subsystem = ProfilerSubsystem::SESSIONS,
                             .operation = "Profiler::create_session",
                             .problem = std::format("session '{}' records no category", description.name),
                             .suggestion = "give it categories (ALL_PROFILER_CATEGORIES by default)"});
    }

    // Records made before it are not its own, nor are the records the threads' rings dropped before.
    implementation.discard_records();
    const auto index = static_cast<std::uint32_t>(implementation.sessions.size());
    const std::int64_t start = now_nanoseconds();
    SessionData data{};
    data.handle = Implementation::session_handle(index);
    data.description = description;
    data.start = start;
    data.frame_begin = start;
    {
        const std::scoped_lock lock{implementation.mutex};
        data.per_context.resize(implementation.contexts.size());
        for (const std::unique_ptr<ThreadContext>& context : implementation.contexts)
        {
            data.per_context[context->index].dropped_seen = context->ring.dropped();
        }
    }
    implementation.sessions.push_back(std::move(data));
    implementation.recording_categories.store(std::to_underlying(description.categories), std::memory_order_relaxed);
    publish_recording(implementation);
    return implementation.sessions.back().handle;
}

auto
Profiler::session_state(SessionHandle session) const -> SessionState
{
    return m_implementation->session(session, "Profiler::session_state").state;
}

void
Profiler::begin_frame(SessionHandle session)
{
    SessionData& data = m_implementation->session(session, "Profiler::begin_frame");
    if (data.state == SessionState::RECORDING)
    {
        data.frame_begin = now_nanoseconds();
        data.frame_open = true;
    }
}

auto
Profiler::end_frame(SessionHandle session) -> bool
{
    Implementation& implementation = *m_implementation;
    SessionData& data = implementation.session(session, "Profiler::end_frame");
    if (data.state != SessionState::RECORDING)
    {
        return false;
    }
    implementation.end_frame(data);
    if (data.description.frames != 0U && data.frames.size() >= data.description.frames)
    {
        data.state = SessionState::STOPPED;
        implementation.recording_categories.store(0, std::memory_order_relaxed);
        publish_recording(implementation);
        return false;
    }
    return true;
}

auto
Profiler::finalize_session(SessionHandle session) -> Trace
{
    Implementation& implementation = *m_implementation;
    SessionData& data = implementation.session(session, "Profiler::finalize_session");
    if (data.state == SessionState::FINALIZED)
    {
        throw ProfilerError({.subsystem = ProfilerSubsystem::SESSIONS,
                             .operation = "Profiler::finalize_session",
                             .problem = std::format("{} ('{}') is finalized already", data.handle, data.description.name),
                             .suggestion = "keep the Trace the first finalize_session() returned"});
    }

    // A frame begun and not ended ends now; nothing more is recorded, and what the rings still hold is not the session's.
    if (data.state == SessionState::RECORDING && data.frame_open)
    {
        implementation.end_frame(data);
    }
    implementation.recording_categories.store(0, std::memory_order_relaxed);
    publish_recording(implementation);
    implementation.discard_records();

    // The GPU scopes not collected yet, waited for.
    implementation.collect_gpu(data, true, true);

    // Scopes and spans left open count as unmatched in the last frame.
    std::uint64_t open = data.open_async.size();
    for (const ThreadSessionState& state : data.per_context)
    {
        open += state.open.size();
    }
    if (!data.frames.empty())
    {
        data.frames.back().unmatched_records += open;
    }

    data.state = SessionState::FINALIZED;
    Trace trace{std::move(data.description.name), std::move(data.sites), std::move(data.threads), std::move(data.frames), std::move(data.gpu_tracks)};
    data.site_indices.clear();
    data.per_context.clear();
    data.open_async.clear();
    data.gpu_track_of.clear();
    data.pending_gpu.clear();
    return trace;
}

auto
Profiler::add_frame_sink(FrameSinkFunction function) -> SinkHandle
{
    Implementation& implementation = *m_implementation;
    const SinkHandle handle = Implementation::sink_handle(++implementation.sink_generation, 1);
    implementation.sinks.push_back({.handle = handle, .function = std::move(function)});
    return handle;
}

void
Profiler::remove_sink(SinkHandle sink)
{
    Implementation& implementation = *m_implementation;
    const auto found = std::ranges::find(implementation.sinks, sink, &SinkEntry::handle);
    if (found == implementation.sinks.end())
    {
        throw_unknown("Profiler::remove_sink", std::format("{}", sink));
    }
    implementation.sinks.erase(found);
}

auto
Profiler::add_gpu_source(GpuTimingSource& source) -> GpuSourceHandle
{
    Implementation& implementation = *m_implementation;
    if (std::ranges::contains(implementation.gpu_sources, &source, &GpuSourceEntry::source))
    {
        throw ProfilerError({.subsystem = ProfilerSubsystem::SESSIONS,
                             .operation = "Profiler::add_gpu_source",
                             .problem = std::format("the source '{}' is added already", source.track_name()),
                             .suggestion = "add each source once"});
    }
    const GpuSourceHandle handle = Implementation::gpu_source_handle(++implementation.gpu_source_generation);
    implementation.gpu_sources.push_back({.handle = handle, .source = &source});
    source.m_attachments.fetch_add(1, std::memory_order_acq_rel);
    return handle;
}

void
Profiler::remove_gpu_source(GpuSourceHandle source)
{
    Implementation& implementation = *m_implementation;
    const auto found = std::ranges::find(implementation.gpu_sources, source, &GpuSourceEntry::handle);
    if (found == implementation.gpu_sources.end())
    {
        throw_unknown("Profiler::remove_gpu_source", std::format("{}", source));
    }
    found->source->m_attachments.fetch_sub(1, std::memory_order_acq_rel);
    implementation.gpu_sources.erase(found);
}

auto
create_profiler(const ProfilerDescription& description) -> std::unique_ptr<Profiler>
{
    return std::make_unique<Profiler>(description);
}

} // namespace gfx::profiler
