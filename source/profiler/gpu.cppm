/// @file gpu.cppm
/// @brief GPU timings in the profiler, without depending on graphics: a GpuTimingSource hands the profiler the GPU
///        scopes it resolved (their timestamps read and placed on the CPU's steady_clock timeline); the profiler
///        collects from every source it was given at each end_frame() (and waits for the last ones at
///        finalize_session()), and files each scope into the frame its beginning falls in. gfx.profiler.graphics
///        implements a source over the graphics library's QueryPool (GpuTimer); a GpuTimingProvider is what the
///        PROFILE_GPU_SCOPE and PROFILE_DISPATCH macros record through.
export module gfx.profiler:gpu;

import std;
import gfx.profiler.enums;
import :site;

export namespace gfx::profiler
{

/// @brief A GPU scope a source resolved.
struct GpuScopeRecord
{
    const ProfileSite* site = nullptr;              ///< Its site.
    std::int64_t begin = 0;                         ///< steady_clock nanoseconds when the GPU began it (when it was
                                                    ///< recorded, if not timed).
    std::int64_t end = 0;                           ///< steady_clock nanoseconds when the GPU ended it.
    std::uint32_t depth = 0;                        ///< GPU scopes open around it in its command stream.
    std::uint32_t query = 0;                        ///< Its first timestamp's index in its frame's queries.
    GpuScopeStatus status = GpuScopeStatus::TIMED;  ///< Whether it was timed.
};

class Profiler;

/// @brief What hands a Profiler GPU timings (Profiler::add_gpu_source()). Its collect() is called from the thread
///        that ends the profiler's frames, possibly while other threads record GPU scopes into it.
class GpuTimingSource
{
public:
    virtual ~GpuTimingSource() = default;

    GpuTimingSource(const GpuTimingSource&) = delete;
    GpuTimingSource(GpuTimingSource&&) = delete;
    auto operator=(const GpuTimingSource&) -> GpuTimingSource& = delete;
    auto operator=(GpuTimingSource&&) -> GpuTimingSource& = delete;

    /// @brief The track's name in traces (the device and the queue).
    [[nodiscard]] virtual auto track_name() const -> std::string = 0;

    /// @brief How its times are placed on the CPU's timeline.
    [[nodiscard]] virtual auto alignment() const noexcept -> GpuClockAlignment = 0;

    /// @brief How far its times may be off.
    [[nodiscard]] virtual auto alignment_deviation() const noexcept -> std::chrono::nanoseconds = 0;

    /// @brief Appends to @p scopes the scopes resolved since the last call; with @p wait, every scope recorded so far
    ///        (waiting for their timestamps, within the source's limit: those never written are INCOMPLETE).
    virtual void collect(std::vector<GpuScopeRecord>& scopes,
                         bool wait) = 0;

    /// @brief Whether a profiler collects from it (sources keep resolved scopes for collection only then).
    [[nodiscard]] auto attached() const noexcept -> bool
    {
        return m_attachments.load(std::memory_order_acquire) != 0U;
    }

protected:
    GpuTimingSource() = default;

private:
    friend class Profiler;

    std::atomic<std::uint32_t> m_attachments{0};
};

/// @brief What PROFILE_GPU_SCOPE(name, recorder, provider) records through: `provider.scope(recorder, site)` opens a
///        GPU scope in @p Recorder's commands, closed when the returned object is destroyed.
template<class Provider, class Recorder>
concept GpuTimingProvider = requires(Provider& provider, Recorder& recorder, const ProfileSite& site) {
    provider.scope(recorder, site);
};

} // namespace gfx::profiler
