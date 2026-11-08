/// @file trace.cppm
/// @brief What a session recorded, once finalized: a Trace — the sites recorded, the threads that recorded, and the
///        frames, each with its scopes (per thread, with their nesting depth), its samples (counters, sizes, markers)
///        and the asynchronous spans that ended in it. Times are nanoseconds since the session began (steady_clock).
///        A Trace is a value: immutable once made, comparable, written and read by the exporters.
export module gfx.profiler:trace;

import std;
import gfx.profiler.enums;

export namespace gfx::profiler
{

/// @brief A recorded site (a copy of its ProfileSite: a trace outlives the program that made it).
struct TraceSite
{
    std::uint64_t identifier = 0;                           ///< ProfileSite::identifier().
    std::uint64_t name_identifier = 0;                      ///< ProfileSite::name_identifier().
    std::string name{};                                     ///< What is measured or counted.
    ProfilerCategory category = ProfilerCategory::GENERAL;  ///< Its category.
    std::string file{};                                     ///< The source file it is in.
    std::uint32_t line = 0;                                 ///< The line it is at.

    [[nodiscard]] auto operator==(const TraceSite&) const -> bool = default;
};

/// @brief A thread that recorded (its index in the trace is its position).
struct TraceThread
{
    std::string name{}; ///< Its name (set_thread_name()), or empty.

    [[nodiscard]] auto operator==(const TraceThread&) const -> bool = default;
};

/// @brief A completed scope.
struct TraceScope
{
    std::uint32_t thread = 0; ///< The thread (index in Trace::threads()).
    std::uint32_t site = 0;   ///< The site (index in Trace::sites()).
    std::int64_t begin = 0;   ///< Nanoseconds since the session began.
    std::int64_t end = 0;     ///< Nanoseconds since the session began.
    std::uint32_t depth = 0;  ///< Scopes open around it on its thread when it began (0: outermost).

    /// @brief Nanoseconds from its begin to its end.
    [[nodiscard]] constexpr auto duration() const noexcept -> std::int64_t
    {
        return end - begin;
    }

    [[nodiscard]] auto operator==(const TraceScope&) const -> bool = default;
};

/// @brief A counter's value, a size or a marker.
struct TraceSample
{
    std::uint32_t thread = 0;              ///< The thread.
    std::uint32_t site = 0;                ///< The site.
    std::int64_t time = 0;                 ///< Nanoseconds since the session began.
    RecordKind kind = RecordKind::MARKER;  ///< COUNTER, COUNTER_FLOAT, MEMORY or MARKER.
    std::uint64_t value = 0;               ///< The integer, the double's bits, the bytes; 0 for a marker.

    /// @brief The value as a number (COUNTER_FLOAT: the double; others: the integer).
    [[nodiscard]] constexpr auto number() const noexcept -> double
    {
        switch (kind)
        {
            case RecordKind::COUNTER_FLOAT:
                return std::bit_cast<double>(value);
            case RecordKind::COUNTER:
                return static_cast<double>(std::bit_cast<std::int64_t>(value));
            default:
                return static_cast<double>(value);
        }
    }

    [[nodiscard]] auto operator==(const TraceSample&) const -> bool = default;
};

/// @brief A completed asynchronous span (it may begin and end on different threads and in different frames).
struct TraceAsyncSpan
{
    std::uint32_t site = 0;         ///< The site that began it.
    std::uint64_t token = 0;        ///< The token naming it.
    std::uint32_t begin_thread = 0; ///< The thread that began it.
    std::uint32_t end_thread = 0;   ///< The thread that ended it.
    std::int64_t begin = 0;         ///< Nanoseconds since the session began.
    std::int64_t end = 0;           ///< Nanoseconds since the session began.

    [[nodiscard]] auto operator==(const TraceAsyncSpan&) const -> bool = default;
};

/// @brief A GPU queue's timings (its index in the trace is its position): one per GpuTimingSource.
struct TraceGpuTrack
{
    std::string name{};                                       ///< The source's name (device, queue).
    GpuClockAlignment alignment = GpuClockAlignment::UNAVAILABLE; ///< How its times were placed on the CPU's timeline.
    std::int64_t deviation = 0;                               ///< Nanoseconds they may be off.

    [[nodiscard]] auto operator==(const TraceGpuTrack&) const -> bool = default;
};

/// @brief A scope of GPU commands (between two timestamps), on the CPU's timeline.
struct TraceGpuScope
{
    std::uint32_t track = 0;                         ///< The GPU track (index in Trace::gpu_tracks()).
    std::uint32_t site = 0;                          ///< The site (index in Trace::sites()).
    std::int64_t begin = 0;                          ///< Nanoseconds since the session began (the GPU's begin, or when an
                                                     ///< untimed scope was recorded).
    std::int64_t end = 0;                            ///< Nanoseconds since the session began.
    std::uint32_t depth = 0;                         ///< GPU scopes open around it in its command stream.
    std::uint32_t query = 0;                         ///< Its first timestamp's index in its frame's queries (recording order).
    GpuScopeStatus status = GpuScopeStatus::TIMED;   ///< Whether it was timed.

    /// @brief Nanoseconds from its begin to its end.
    [[nodiscard]] constexpr auto duration() const noexcept -> std::int64_t
    {
        return end - begin;
    }

    [[nodiscard]] auto operator==(const TraceGpuScope&) const -> bool = default;
};

/// @brief One frame of a session: what was recorded between two end_frame() calls. Scopes and spans belong to the
///        frame they end in; GPU scopes to the frame whose time their beginning falls in (they may arrive a few frames
///        later, when their timestamps are read).
struct TraceFrame
{
    std::uint64_t index = 0;                     ///< Its number in the session (from 0).
    std::int64_t begin = 0;                      ///< begin_frame() (or the previous frame's end), since the session began.
    std::int64_t end = 0;                        ///< end_frame(), since the session began.
    std::vector<TraceScope> scopes{};            ///< Scopes that ended in it, thread by thread.
    std::vector<TraceSample> samples{};          ///< Samples, thread by thread, in recording order.
    std::vector<TraceAsyncSpan> async_spans{};   ///< Spans that ended in it.
    std::vector<TraceGpuScope> gpu_scopes{};     ///< GPU scopes that began in it, track by track in recording order.
    std::uint64_t dropped_records = 0;           ///< Records lost because a thread's ring was full.
    std::uint64_t unmatched_records = 0;         ///< Scope or span ends without a begin, begins left open.

    [[nodiscard]] auto operator==(const TraceFrame&) const -> bool = default;
};

/// @brief A finalized session's recordings.
class Trace
{
public:
    /// @brief An empty trace.
    Trace() = default;

    /// @param name    The session's name.
    /// @param sites   The sites the frames refer to.
    /// @param threads The threads the frames refer to.
    /// @param frames  The frames, in order.
    /// @param gpu_tracks The GPU tracks the frames' GPU scopes refer to.
    Trace(std::string name,
          std::vector<TraceSite> sites,
          std::vector<TraceThread> threads,
          std::vector<TraceFrame> frames,
          std::vector<TraceGpuTrack> gpu_tracks = {});

    /// @brief The session's name.
    [[nodiscard]] auto name() const noexcept -> std::string_view;

    /// @brief The sites recorded.
    [[nodiscard]] auto sites() const noexcept -> std::span<const TraceSite>;

    /// @brief The threads that recorded.
    [[nodiscard]] auto threads() const noexcept -> std::span<const TraceThread>;

    /// @brief The frames, in order.
    [[nodiscard]] auto frames() const noexcept -> std::span<const TraceFrame>;

    /// @brief The GPU tracks.
    [[nodiscard]] auto gpu_tracks() const noexcept -> std::span<const TraceGpuTrack>;

    /// @brief The index of the first site named @p name, if any.
    [[nodiscard]] auto find_site(std::string_view name) const noexcept -> std::optional<std::uint32_t>;

    [[nodiscard]] auto operator==(const Trace&) const -> bool = default;

private:
    std::string m_name{};
    std::vector<TraceSite> m_sites{};
    std::vector<TraceThread> m_threads{};
    std::vector<TraceFrame> m_frames{};
    std::vector<TraceGpuTrack> m_gpu_tracks{};
};

/// @brief @p trace without its times, as text: the sites (sorted by name, file and line), then per frame and per
///        thread (threads sorted by name) its scopes in the order they began, its samples and its spans, with their
///        sites, depths, values and tokens, then its GPU scopes track by track in query order, with their depths,
///        queries and statuses. Two runs of the same workload give the same text (the determinism the
///        tests check).
[[nodiscard]] auto canonical_trace(const Trace& trace) -> std::string;

} // namespace gfx::profiler
