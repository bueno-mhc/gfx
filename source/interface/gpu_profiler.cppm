/// @file gpu_profiler.cppm
/// @brief GpuProfiler: named GPU timings per frame, from timestamp queries written around scopes of
///        commands, read back once the GPU finished the frame (no stall).
export module gfx.interface:gpu_profiler;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import :query_pool;
import :recorder;
import :physical_device;
import :device;

export namespace gfx
{

/// @brief GPU time of one profiled scope.
struct GpuTiming
{
    std::string name{};        ///< Scope name.
    double milliseconds = 0.0; ///< GPU time between its start and end timestamps.
    std::uint64_t start = 0;   ///< Start timestamp, in ticks (for ordering scopes).
};

/// @brief Writes a scope's end timestamp and closes its debug label when destroyed.
template<QueueType Type>
class GpuProfilerScope
{
public:
    ~GpuProfilerScope()
    {
        if (m_query != NO_QUERY)
        {
            m_recorder.write_timestamp(m_pool, PipelineStage::ALL_COMMANDS, m_query + 1);
        }
        m_recorder.end_debug_label();
    }

    GpuProfilerScope(const GpuProfilerScope&) = delete;
    GpuProfilerScope(GpuProfilerScope&&) = delete;
    auto operator=(const GpuProfilerScope&) -> GpuProfilerScope& = delete;
    auto operator=(GpuProfilerScope&&) -> GpuProfilerScope& = delete;

private:
    friend class GpuProfiler;

    static constexpr std::uint32_t NO_QUERY = std::numeric_limits<std::uint32_t>::max();

    GpuProfilerScope(Recorder<Type>& recorder, QueryPoolHandle pool, std::uint32_t query)
        : m_recorder{recorder}
        , m_pool{pool}
        , m_query{query}
    {}

private:
    Recorder<Type>& m_recorder;
    QueryPoolHandle m_pool;
    std::uint32_t m_query;
};

/// @brief Per-frame GPU timings of named scopes. Scopes may be opened from several threads (their query
///        slots are taken atomically); begin_frame() must not run concurrently with scope().
///
/// @code
/// profiler.begin_frame(frame, recorder);                 // reads frame - frames_in_flight, resets queries
/// { auto shadows = profiler.scope(recorder, "shadows"); /* commands */ }
/// for (const GpuTiming& timing : profiler.timings()) { ... }
/// @endcode
class GpuProfiler
{
public:
    /// @brief Creates the timestamp queries: two per scope, for each frame in flight.
    GpuProfiler(Device& device, std::uint32_t frames_in_flight, std::uint32_t maximum_scopes_per_frame = 256)
        : m_frames_in_flight{std::max(frames_in_flight, 1U)}
        , m_maximum_scopes{maximum_scopes_per_frame}
        , m_nanoseconds_per_tick{device.physical_device().properties().limits.timestamp_period_nanoseconds}
    {
        m_pool = device.create_query_pool({.type = QueryType::TIMESTAMP,
                                           .count = m_frames_in_flight * m_maximum_scopes * 2,
                                           .debug_name = "gpu profiler"});
        m_frames.resize(m_frames_in_flight);
        for (Frame& frame : m_frames)
        {
            frame.names.resize(m_maximum_scopes);
        }
    }

    /// @brief Starts frame @p frame: reads the timings of the frame that last used this frame's queries
    ///        (the GPU finished it: call after FrameTimeline::begin_frame()), then resets the queries in
    ///        @p recorder, which must run before any scope of the frame.
    template<QueueType Type>
    void begin_frame(std::uint64_t frame, Recorder<Type>& recorder)
    {
        // Read the previous user of this slot.
        m_current = static_cast<std::uint32_t>(frame % m_frames_in_flight);
        Frame& slot = m_frames[m_current];
        const std::uint32_t first = m_current * m_maximum_scopes * 2;
        const std::uint32_t used = std::min(slot.used.load(std::memory_order_relaxed), m_maximum_scopes);
        if (slot.frame != 0)
        {
            std::vector<std::uint64_t> ticks(std::size_t{used} * 2);
            if (used != 0 && m_pool->results(first, ticks, true))
            {
                m_timings.clear();
                for (std::uint32_t scope = 0; scope < used; ++scope)
                {
                    const std::uint64_t start = ticks[std::size_t{scope} * 2];
                    const std::uint64_t end = ticks[std::size_t{scope} * 2 + 1];
                    m_timings.push_back({.name = slot.names[scope],
                                         .milliseconds = static_cast<double>(end - start) * m_nanoseconds_per_tick / 1.0e6,
                                         .start = start});
                }
                std::ranges::sort(m_timings, {}, &GpuTiming::start);
            }
            m_completed_frame = slot.frame;
        }

        // Reuse the slot for this frame.
        recorder.reset_query_pool(m_pool->handle(), first, m_maximum_scopes * 2);
        slot.frame = frame;
        slot.used.store(0, std::memory_order_relaxed);
    }

    /// @brief Opens a timed scope (and a debug label of the same name) in @p recorder; it closes when
    ///        the returned object is destroyed. Beyond maximum_scopes_per_frame, scopes are labeled but
    ///        not timed.
    template<QueueType Type>
    [[nodiscard]] auto scope(Recorder<Type>& recorder, std::string_view name) -> GpuProfilerScope<Type>
    {
        recorder.begin_debug_label(name);
        Frame& slot = m_frames[m_current];
        const std::uint32_t index = slot.used.fetch_add(1, std::memory_order_relaxed);
        if (index >= m_maximum_scopes)
        {
            return GpuProfilerScope<Type>{recorder, m_pool->handle(), GpuProfilerScope<Type>::NO_QUERY};
        }
        slot.names[index] = name;
        const std::uint32_t query = (m_current * m_maximum_scopes + index) * 2;
        recorder.write_timestamp(m_pool->handle(), PipelineStage::NONE, query);
        return GpuProfilerScope<Type>{recorder, m_pool->handle(), query};
    }

    /// @brief Timings of the latest frame read back, ordered by start time.
    [[nodiscard]] auto timings() const noexcept -> std::span<const GpuTiming>
    {
        return m_timings;
    }

    /// @brief Frame the timings belong to (0 before any was read back).
    [[nodiscard]] auto timed_frame() const noexcept -> std::uint64_t
    {
        return m_completed_frame;
    }

private:
    struct Frame
    {
        std::uint64_t frame = 0;
        std::atomic<std::uint32_t> used{0};
        std::vector<std::string> names{};

        Frame() = default;
        Frame(Frame&& other) noexcept
            : frame{other.frame}
            , used{other.used.load()}
            , names{std::move(other.names)}
        {}
    };

private:
    std::uint32_t m_frames_in_flight;
    std::uint32_t m_maximum_scopes;
    double m_nanoseconds_per_tick;
    std::unique_ptr<QueryPool> m_pool;
    std::vector<Frame> m_frames;
    std::uint32_t m_current = 0;
    std::vector<GpuTiming> m_timings;
    std::uint64_t m_completed_frame = 0;
};

} // namespace gfx
