/// @file profiler_graphics.cppm
/// @brief `import gfx.profiler.graphics;` — GPU timings for the profiler through the graphics library's QueryPool
///        (no vendor API). A GpuTimer writes a timestamp before and after each GPU scope recorded into a command
///        buffer, from a per-frame arena of queries (one range per frame in flight, indices taken in recording order:
///        the same workload uses the same queries), reads them back once the GPU wrote them, and places them on the
///        CPU's steady_clock timeline: by calibrated timestamps when the device has them (recalibrated every second),
///        else by an estimate made at construction from a submission that writes a timestamp.
/// @code
/// gfx::profiler::GpuTimer timer{device, {.queue = gfx::QueueType::COMPUTE}};
/// profiler.add_gpu_source(timer);                           // the profiler collects at each end_frame()
/// gfx::ComputeRecorder recorder{command_buffer};
/// timer.begin_frame(recorder);                              // reads the slot's last use, resets its queries
/// {
///     PROFILE_GPU_SCOPE("broad phase", recorder, timer);    // timestamps around what is recorded in the block
///     PROFILE_DISPATCH("morton codes", recorder, timer, groups, 1, 1);
/// }
/// @endcode
///        Scopes are written only while a profiler records the GPU category with the timer added (or always, with
///        GpuTimerDescription::always: then latest() gives the last frame read back). One recording thread at a time;
///        collect() and latest() may run on other threads. Beyond scopes_per_frame a frame's scopes are DROPPED;
///        a queue without timestamps gives UNAVAILABLE scopes; timestamps not written within wait_limit, INCOMPLETE.
export module gfx.profiler.graphics;

import std;
export import gfx;
export import gfx.profiler;

export namespace gfx::profiler
{

/// @brief How to create a GpuTimer.
struct GpuTimerDescription
{
    QueueType queue = QueueType::GRAPHICS;                ///< The queue type of the command buffers it times.
    std::uint32_t frames_in_flight = 3;                   ///< Frames whose queries may be in use at once (at least 1).
    std::uint32_t scopes_per_frame = 1024;                ///< Timed scopes per frame (two queries each).
    bool always = false;                                  ///< Time scopes whether or not a profiler records them.
    std::chrono::milliseconds wait_limit{2000};           ///< How long a wait for timestamps lasts before INCOMPLETE.
    std::chrono::milliseconds recalibration{1000};        ///< How often calibrated timestamps are taken again.
    bool calibrated = true;                               ///< Use calibrated timestamps when the device has them (false:
                                                          ///< always the estimate).
    std::string name{};                                   ///< Its track's name (default: the device and the queue).
};

class GpuTimer;

/// @brief Closes a GPU scope (its end timestamp) when destroyed.
template<QueueType Type>
class GpuTimerScope
{
public:
    ~GpuTimerScope();

    GpuTimerScope(const GpuTimerScope&) = delete;
    GpuTimerScope(GpuTimerScope&&) = delete;
    auto operator=(const GpuTimerScope&) -> GpuTimerScope& = delete;
    auto operator=(GpuTimerScope&&) -> GpuTimerScope& = delete;

private:
    friend class GpuTimer;

    GpuTimerScope(GpuTimer* timer,
                  Recorder<Type>& recorder,
                  std::uint32_t scope,
                  std::uint32_t query) noexcept
        : m_timer{timer}
        , m_recorder{recorder}
        , m_scope{scope}
        , m_query{query}
    {
    }

private:
    GpuTimer* m_timer;
    Recorder<Type>& m_recorder;
    std::uint32_t m_scope;
    std::uint32_t m_query;
};

/// @brief GPU timestamps around scopes of commands, read back and placed on the CPU's timeline (a GpuTimingSource).
class GpuTimer final : public GpuTimingSource
{
public:
    /// @brief Nothing to time (a scope not recorded, or no query).
    static constexpr std::uint32_t NO_INDEX = 0xFFFF'FFFFU;

    /// @param device      The device (it outlives the timer).
    /// @param description Its queue, arena and limits.
    /// @throws ProfilerError if @p description has no frame in flight or no scope per frame.
    GpuTimer(Device& device,
             const GpuTimerDescription& description = {});
    ~GpuTimer() override;

    /// @brief Starts a frame of commands recorded into @p recorder (before any scope of the frame): the next slot of
    ///        queries is read if its last frame was not (waiting for it within wait_limit), then reset in @p recorder.
    template<QueueType Type>
    void begin_frame(Recorder<Type>& recorder)
    {
        const std::uint32_t first = start_frame();
        if (first != NO_INDEX)
        {
            recorder.reset_query_pool(query_pool(), first, queries_per_frame());
        }
    }

    /// @brief Opens a GPU scope of @p site in @p recorder's commands, closed when the returned object is destroyed.
    template<QueueType Type>
    [[nodiscard]] auto scope(Recorder<Type>& recorder,
                             const ProfileSite& site) -> GpuTimerScope<Type>
    {
        const auto [scope_index, query] = open_scope(site);
        if (query != NO_INDEX)
        {
            recorder.write_timestamp(query_pool(), PipelineStage::NONE, query);
        }
        return GpuTimerScope<Type>{this, recorder, scope_index, query};
    }

    /// @brief The scopes of the last frame read back, in query order (for direct use, with GpuTimerDescription::always).
    [[nodiscard]] auto latest() const -> std::vector<GpuScopeRecord>;

    /// @brief How it was created.
    [[nodiscard]] auto description() const noexcept -> const GpuTimerDescription&;

    // GpuTimingSource
    [[nodiscard]] auto track_name() const -> std::string override;
    [[nodiscard]] auto alignment() const noexcept -> GpuClockAlignment override;
    [[nodiscard]] auto alignment_deviation() const noexcept -> std::chrono::nanoseconds override;
    void collect(std::vector<GpuScopeRecord>& scopes,
                 bool wait) override;

    /// @brief The state behind it (defined in the library).
    struct Implementation;

private:
    template<QueueType Type>
    friend class GpuTimerScope;

private:
    [[nodiscard]] auto query_pool() const noexcept -> QueryPoolHandle;
    [[nodiscard]] auto queries_per_frame() const noexcept -> std::uint32_t;
    [[nodiscard]] auto start_frame() -> std::uint32_t;
    [[nodiscard]] auto open_scope(const ProfileSite& site) -> std::pair<std::uint32_t, std::uint32_t>;
    void close_scope(std::uint32_t scope) noexcept;

    std::unique_ptr<Implementation> m_implementation;
};

template<QueueType Type>
GpuTimerScope<Type>::~GpuTimerScope()
{
    if (m_query != GpuTimer::NO_INDEX)
    {
        m_recorder.write_timestamp(m_timer->query_pool(), PipelineStage::ALL_COMMANDS, m_query + 1);
    }
    if (m_scope != GpuTimer::NO_INDEX)
    {
        m_timer->close_scope(m_scope);
    }
}

} // namespace gfx::profiler
