/// @file state.cppm
/// @brief Internal partition: what is behind the Profiler — the records, the threads' single-producer
///        single-consumer rings, the thread contexts, the sessions being recorded — and the recording functions'
///        global state (the current profiler and the categories it records).
module gfx.profiler:state;

import std;
import gfx.core.handles;
import gfx.profiler.enums;
import gfx.profiler.handles;
import :site;
import :trace;
import :session;

namespace gfx::profiler
{

/// One record in a thread's ring.
struct alignas(PROFILER_RECORD_SIZE) ProfileRecord
{
    std::int64_t time = 0;                 // steady_clock nanoseconds
    const ProfileSite* site = nullptr;     // the site (it outlives every recording)
    std::uint64_t value = 0;               // counter value, double bits, bytes, async token
    RecordKind kind = RecordKind::MARKER;
};
static_assert(sizeof(ProfileRecord) == PROFILER_RECORD_SIZE, "records are half a cache line");

/// A single-producer single-consumer ring of records: the recording thread pushes, the thread calling end_frame()
/// drains. No allocation after construction, no lock; the two indices live on cache lines of their own.
class RecordRing
{
public:
    /// @param capacity Records it holds (a power of two).
    explicit RecordRing(std::uint32_t capacity);

    /// Adds @p record (producer). False, and counted as dropped, when the ring is full.
    auto push(const ProfileRecord& record) noexcept -> bool
    {
        const std::uint64_t head = m_head.load(std::memory_order_relaxed);
        if (head - m_cached_tail >= m_capacity)
        {
            m_cached_tail = m_tail.load(std::memory_order_acquire);
            if (head - m_cached_tail >= m_capacity)
            {
                m_dropped.store(m_dropped.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                return false;
            }
        }
        m_records[head & m_mask] = record;
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    /// Calls @p visitor with every record pushed so far, oldest first, then frees their places (consumer).
    template<class Visitor>
    void drain(Visitor&& visitor)
    {
        const std::uint64_t head = m_head.load(std::memory_order_acquire);
        std::uint64_t tail = m_tail.load(std::memory_order_relaxed);
        for (; tail != head; ++tail)
        {
            visitor(m_records[tail & m_mask]);
        }
        m_tail.store(tail, std::memory_order_release);
    }

    /// Records dropped since the ring was made (consumer side: read with acquire).
    [[nodiscard]] auto dropped() const noexcept -> std::uint64_t
    {
        return m_dropped.load(std::memory_order_relaxed);
    }

    /// Records it holds.
    [[nodiscard]] auto capacity() const noexcept -> std::uint64_t
    {
        return m_capacity;
    }

private:
    // The producer's line: its index, its copy of the consumer's, the dropped count it alone writes.
    alignas(PROFILER_CACHE_LINE_SIZE) std::atomic<std::uint64_t> m_head{0};
    std::uint64_t m_cached_tail{0};
    std::atomic<std::uint64_t> m_dropped{0};
    // The consumer's line.
    alignas(PROFILER_CACHE_LINE_SIZE) std::atomic<std::uint64_t> m_tail{0};
    // Read-only after construction.
    alignas(PROFILER_CACHE_LINE_SIZE) std::unique_ptr<ProfileRecord[]> m_records{};
    std::uint64_t m_capacity{0};
    std::uint64_t m_mask{0};
};

/// A thread that recorded into a profiler: its ring and its name.
struct ThreadContext
{
    ThreadContext(std::uint32_t context_index,
                  std::uint32_t capacity,
                  std::string thread_name);

    std::uint32_t index{0}; // registration order in its profiler
    std::string name{};     // guarded by the profiler's mutex
    RecordRing ring;
};

/// A scope begun and not yet ended, on one thread.
struct OpenScope
{
    const ProfileSite* site{nullptr};
    std::int64_t begin{0};
};

/// A span begun and not yet ended.
struct OpenAsync
{
    std::uint32_t site{0};
    std::uint32_t thread{0};
    std::int64_t begin{0};
};

/// What a session keeps per thread context.
struct ThreadSessionState
{
    static constexpr std::uint32_t NO_THREAD = 0xFFFF'FFFFU;

    std::uint32_t trace_thread{NO_THREAD}; // index in the session's threads, once the thread recorded into it
    std::vector<OpenScope> open{};         // its scopes begun and not ended
    std::uint64_t dropped_seen{0};         // the ring's dropped count already reported
};

/// A session: its description, its frames so far, the sites and threads they refer to.
struct SessionData
{
    SessionHandle handle{};
    SessionDescription description{};
    SessionState state{SessionState::RECORDING};
    std::int64_t start{0};       // steady_clock nanoseconds
    std::int64_t frame_begin{0}; // steady_clock nanoseconds
    bool frame_open{false};
    std::vector<TraceFrame> frames{};
    std::vector<TraceSite> sites{};
    std::unordered_map<std::uint64_t, std::uint32_t> site_indices{}; // ProfileSite::identifier() → index in sites
    std::vector<TraceThread> threads{};
    std::vector<ThreadSessionState> per_context{};                    // by ThreadContext::index
    std::map<std::pair<std::uint64_t, std::uint64_t>, OpenAsync> open_async{}; // (name identifier, token)
};

/// A live sink.
struct SinkEntry
{
    SinkHandle handle{};
    FrameSinkFunction function{};
};

} // namespace gfx::profiler

namespace gfx::profiler
{

struct Profiler::Implementation : private HandleIssuer
{
    explicit Implementation(const ProfilerDescription& profiler_description);

    /// The session @p session names. Throws ProfilerError naming @p operation if it is none of this profiler's.
    [[nodiscard]] auto session(SessionHandle session,
                               std::string_view operation) -> SessionData&;

    /// The handle of a new session or sink of slot @p index.
    [[nodiscard]] static auto session_handle(std::uint32_t index) -> SessionHandle;
    [[nodiscard]] static auto sink_handle(std::uint32_t index,
                                          std::uint32_t generation) -> SinkHandle;
    [[nodiscard]] static auto index_of(SessionHandle handle) noexcept -> std::uint32_t;
    [[nodiscard]] static auto index_of(SinkHandle handle) noexcept -> std::uint32_t;

    /// Drains every thread's ring into @p data's current frame, then ends it (begin_frame() .. now).
    void end_frame(SessionData& data);

    /// Discards what the rings hold (records made after a session ended).
    void discard_records();

    std::uint64_t id;                                   // unique among the process's profilers
    ProfilerDescription description;
    std::mutex mutex{};                                 // guards contexts (and their names) and sinks
    std::vector<std::unique_ptr<ThreadContext>> contexts{};
    std::vector<SessionData> sessions{};                // by handle index
    std::atomic<std::uint32_t> recording_categories{0}; // what its recording session records (0: none)
    std::vector<SinkEntry> sinks{};
    std::uint32_t sink_generation{0};
    std::vector<ThreadContext*> drained{};              // reused by end_frame()
};

/// Reaches a Profiler's implementation (Profiler befriends it).
class ProfilerAccess
{
public:
    [[nodiscard]] static auto implementation(Profiler& profiler) noexcept -> Profiler::Implementation&;
};

/// Nanoseconds of steady_clock.
[[nodiscard]] auto now_nanoseconds() noexcept -> std::int64_t;

/// Publishes what @p profiler records now (its recording session's categories, or none) to the recording threads,
/// if it is current (recording.cpp).
void publish_recording(Profiler::Implementation& profiler) noexcept;

/// Stops @p profiler being current if it is (its destructor).
void forget_current(Profiler::Implementation& profiler) noexcept;

} // namespace gfx::profiler
