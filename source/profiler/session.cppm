/// @file session.cppm
/// @brief The Profiler: it owns the threads' rings and the sessions. A session records frames; each end_frame()
///        drains every thread's ring into the frame (pairing scope begins and ends, spans' begins and ends), gives the
///        frame to the live sinks, and finalize_session() turns the frames into a Trace.
/// @code
/// const auto profiler = gfx::profiler::create_profiler();
/// gfx::profiler::set_current_profiler(profiler.get());            // what the PROFILE_* macros record into
/// const auto session = profiler->create_session({.frames = 300});
/// for (int frame = 0; frame < 300; ++frame)
/// {
///     profiler->begin_frame(session);
///     run_frame();                                                 // PROFILE_SCOPE(...) anywhere, on any thread
///     profiler->end_frame(session);
/// }
/// const gfx::profiler::Trace trace = profiler->finalize_session(session);
/// gfx::profiler::set_current_profiler(nullptr);
/// @endcode
///        Threads: recording from any thread; create_session(), begin_frame(), end_frame(), finalize_session(),
///        add_sink() and remove_sink() from one thread at a time. Destroy a profiler only when no thread records into
///        it (make another current, or none, and let threads leave their scopes).
export module gfx.profiler:session;

import std;
import gfx.profiler.enums;
import gfx.profiler.handles;
import :trace;
import :gpu;

export namespace gfx::profiler
{

/// @brief How to create a Profiler.
struct ProfilerDescription
{
    std::uint32_t ring_capacity = DEFAULT_PROFILER_RING_CAPACITY; ///< Records each thread's ring holds (a power of two,
                                                                  ///< at most MAXIMUM_PROFILER_RING_CAPACITY).
};

/// @brief How to record a session.
struct SessionDescription
{
    std::string name{};                                          ///< Shown in traces and reports.
    std::uint32_t frames = 0;                                    ///< Frames to record, then it stops (0: until finalized).
    ProfilerCategory categories = ALL_PROFILER_CATEGORIES;       ///< What it records.
};

/// @brief A function given each completed frame of a session, with the sites and threads its records refer to (a
///        live sink: an overlay, a network sender). Called by end_frame(), on its thread.
using FrameSinkFunction = std::move_only_function<void(const TraceFrame& frame,
                                                       std::span<const TraceSite> sites,
                                                       std::span<const TraceThread> threads)>;

/// @brief What add_sink() accepts: a callable taking a frame, the sites and the threads.
template<class Function>
concept FrameSink = std::move_constructible<std::decay_t<Function>>
                    && std::invocable<std::decay_t<Function>&, const TraceFrame&, std::span<const TraceSite>, std::span<const TraceThread>>;

/// @brief Records sessions of frames from the threads that record into it.
class Profiler
{
public:
    /// @throws ProfilerError if @p description's ring capacity is not a power of two within bounds.
    explicit Profiler(const ProfilerDescription& description = {});

    /// @brief Stops being current if it is, and detaches its GPU sources.
    ~Profiler();

    Profiler(const Profiler&) = delete;
    Profiler(Profiler&&) = delete;
    auto operator=(const Profiler&) -> Profiler& = delete;
    auto operator=(Profiler&&) -> Profiler& = delete;

    /// @brief How it was created.
    [[nodiscard]] auto description() const noexcept -> const ProfilerDescription&;

    /// @brief Starts recording a session (its frames begin at once; begin_frame() marks where each one starts).
    /// @throws ProfilerError if a session of this profiler records or waits to be finalized, or @p description
    ///         records no category.
    [[nodiscard]] auto create_session(const SessionDescription& description = {}) -> SessionHandle;

    /// @brief Where @p session is.
    /// @throws ProfilerError if @p session is not one of this profiler's.
    [[nodiscard]] auto session_state(SessionHandle session) const -> SessionState;

    /// @brief Marks the beginning of a frame of @p session (ignored unless it records).
    /// @throws ProfilerError if @p session is not one of this profiler's.
    void begin_frame(SessionHandle session);

    /// @brief Ends a frame of @p session: drains every thread's ring into it and gives it to the sinks.
    /// @return Whether the session still records (false once its frame count is reached).
    /// @throws ProfilerError if @p session is not one of this profiler's.
    auto end_frame(SessionHandle session) -> bool;

    /// @brief Ends @p session (a frame begun and not ended is ended first; later records are discarded) and returns
    ///        what it recorded.
    /// @throws ProfilerError if @p session is not one of this profiler's, or is finalized already.
    [[nodiscard]] auto finalize_session(SessionHandle session) -> Trace;

    /// @brief Adds a live sink: @p function receives each frame the sessions complete, from now on.
    template<FrameSink Function>
    auto add_sink(Function&& function) -> SinkHandle
    {
        return add_frame_sink(FrameSinkFunction{std::forward<Function>(function)});
    }

    /// @brief Removes a sink.
    /// @throws ProfilerError if @p sink is not one of this profiler's.
    void remove_sink(SinkHandle sink);

    /// @brief Collects GPU timings from @p source at each end_frame() from now on (and waits for its last ones at
    ///        finalize_session()), until it is removed or either is destroyed (each removes itself from the other).
    ///        May be called from any thread.
    /// @throws ProfilerError if @p source is added already.
    auto add_gpu_source(GpuTimingSource& source) -> GpuSourceHandle;

    /// @brief Stops collecting from a source (what it resolved and was not collected is not). May be called from any
    ///        thread.
    /// @throws ProfilerError if @p source is not one of this profiler's.
    void remove_gpu_source(GpuSourceHandle source);

    /// @brief The state behind it (defined in the library; an incomplete type for applications).
    struct Implementation;

private:
    friend class ProfilerAccess;

private:
    auto add_frame_sink(FrameSinkFunction function) -> SinkHandle;

    std::unique_ptr<Implementation> m_implementation;
};

/// @brief A Profiler (the same as constructing one).
/// @throws ProfilerError as Profiler's constructor.
[[nodiscard]] auto create_profiler(const ProfilerDescription& description = {}) -> std::unique_ptr<Profiler>;

/// @brief Makes @p profiler the one every thread records into (nullptr: none), as the PROFILE_* macros do.
void set_current_profiler(Profiler* profiler) noexcept;

/// @brief The profiler every thread records into, or nullptr.
[[nodiscard]] auto current_profiler() noexcept -> Profiler*;

} // namespace gfx::profiler
