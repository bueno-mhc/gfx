/// @file frame_metrics.cppm
/// @brief FrameMetrics: what each frame costs - frame rate, CPU frame times, the commands of the frame's
///        command buffers, GPU timings and device memory - collected in Debug builds for a DebugOverlay
///        (or a log). In other builds it collects nothing.
export module gfx.interface:frame_metrics;

import std;
import gfx.enums;
import :commands;
import :device_memory;
import :command_buffer;
import :gpu_profiler;
import :device;

export namespace gfx
{

/// @brief How FrameMetrics averages.
struct FrameMetricsDescription
{
    std::chrono::milliseconds averaging_period{500}; ///< Frame rate and frame times are averaged over this
                                                     ///< period (and memory is read once per period).
};

/// @brief The metrics of the latest frames (FrameMetrics::snapshot()).
struct FrameMetricsSnapshot
{
    std::uint64_t frames = 0;                ///< Frames measured since FrameMetrics was created.
    double frames_per_second = 0.0;          ///< Over the last complete averaging period.
    double average_frame_milliseconds = 0.0; ///< CPU time from one begin_frame() to the next, averaged.
    double minimum_frame_milliseconds = 0.0; ///< Shortest frame of the period.
    double maximum_frame_milliseconds = 0.0; ///< Longest frame of the period.
    std::uint32_t command_buffers = 0;       ///< Command buffers counted in the previous frame.
    CommandStatistics commands{};            ///< Their commands (draws, dispatches, binds, barriers, ...).
    std::vector<GpuTiming> gpu_timings{};    ///< GPU scopes of the latest frame the GPU finished (GpuProfiler).
    MemoryStatistics memory{};               ///< Device memory, read at the end of the last period.
};

/// @brief Per-frame metrics, in Debug builds (enabled()); in other builds every member does nothing and
///        the snapshot stays empty, so code using it builds unchanged. Call begin_frame() once per frame,
///        count() for each command buffer the frame submits (after recording it), and
///        set_gpu_timings() with a GpuProfiler's timings; snapshot() and text() then describe the
///        previous frame and the last averaging period. Not thread-safe: use it on the submitting thread.
///
/// @code
/// metrics.begin_frame();
/// ... record the frame's command buffers ...
/// metrics.count(commands);
/// metrics.set_gpu_timings(profiler.timings());
/// overlay.draw(scope, area, metrics.text());
/// @endcode
class FrameMetrics
{
public:
    /// @brief Metrics of the frames of @p device.
    explicit FrameMetrics(const Device& device,
                          const FrameMetricsDescription& description = {})
        : m_device{device}
        , m_description{description}
    {
    }

    /// @brief Whether metrics are collected in this build (Debug builds).
    [[nodiscard]] static constexpr auto enabled() noexcept -> bool
    {
        return DEBUG_BUILD;
    }

    /// @brief Ends the previous frame (its counts become the snapshot's) and starts timing the next one.
    /// @param now Time the frame starts (the steady clock by default; tests pass their own).
    void begin_frame(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now())
    {
        if constexpr (!DEBUG_BUILD)
        {
            return;
        }

        // The previous frame: its CPU time joins the period, its counts become the snapshot's.
        if (m_frame_start)
        {
            const double milliseconds = std::chrono::duration<double, std::milli>(now - *m_frame_start).count();
            m_period_milliseconds += milliseconds;
            m_period_minimum = m_period_frames == 0 ? milliseconds : std::min(m_period_minimum, milliseconds);
            m_period_maximum = m_period_frames == 0 ? milliseconds : std::max(m_period_maximum, milliseconds);
            ++m_period_frames;
            ++m_snapshot.frames;
        }
        else
        {
            m_period_start = now;
        }
        m_snapshot.commands = std::exchange(m_commands, CommandStatistics{});
        m_snapshot.command_buffers = std::exchange(m_command_buffers, 0U);

        // A complete period: averages, extremes and memory.
        if (m_period_frames != 0 && now - m_period_start >= m_description.averaging_period)
        {
            const double seconds = std::chrono::duration<double>(now - m_period_start).count();
            m_snapshot.frames_per_second = static_cast<double>(m_period_frames) / seconds;
            m_snapshot.average_frame_milliseconds = m_period_milliseconds / static_cast<double>(m_period_frames);
            m_snapshot.minimum_frame_milliseconds = m_period_minimum;
            m_snapshot.maximum_frame_milliseconds = m_period_maximum;
            m_snapshot.memory = m_device.memory_statistics();
            m_period_start = now;
            m_period_frames = 0;
            m_period_milliseconds = 0.0;
        }
        m_frame_start = now;
    }

    /// @brief Adds the commands recorded so far in @p command_buffer to the current frame.
    void count(const CommandBuffer& command_buffer) noexcept
    {
        if constexpr (DEBUG_BUILD)
        {
            m_commands += command_buffer.statistics();
            ++m_command_buffers;
        }
    }

    /// @brief The GPU timings to show (GpuProfiler::timings(): the latest frame the GPU finished).
    void set_gpu_timings(std::span<const GpuTiming> timings)
    {
        if constexpr (DEBUG_BUILD)
        {
            m_snapshot.gpu_timings.assign(timings.begin(), timings.end());
        }
    }

    /// @brief The metrics of the previous frame and of the last averaging period.
    [[nodiscard]] auto snapshot() const noexcept -> const FrameMetricsSnapshot&
    {
        return m_snapshot;
    }

    /// @brief The snapshot as lines of text, for a DebugOverlay (empty in builds other than Debug).
    [[nodiscard]] auto text() const -> std::vector<std::string>
    {
        if constexpr (!DEBUG_BUILD)
        {
            return {};
        }
        const FrameMetricsSnapshot& metrics = m_snapshot;
        const CommandStatistics& commands = metrics.commands;
        std::vector<std::string> lines;
        lines.push_back(std::format("{:.1f} fps   {:.2f} ms   min {:.2f}   max {:.2f}", metrics.frames_per_second,
                                    metrics.average_frame_milliseconds, metrics.minimum_frame_milliseconds,
                                    metrics.maximum_frame_milliseconds));
        for (const GpuTiming& timing : metrics.gpu_timings)
        {
            lines.push_back(std::format("gpu {}: {:.3f} ms", timing.name, timing.milliseconds));
        }
        lines.push_back(std::format("draws {}   indirect draws {}   dispatches {}   ray traces {}", commands.draws,
                                    commands.indirect_draws, commands.dispatches, commands.ray_traces));
        lines.push_back(std::format("pipelines {}   descriptor sets {}   push constants {}   vertex buffers {}", commands.pipeline_binds,
                                    commands.descriptor_set_binds, commands.push_constant_writes, commands.vertex_buffer_binds));
        lines.push_back(std::format("barriers {}   transfers {}   passes {}   command buffers {}", commands.barriers, commands.transfers,
                                    commands.rendering_passes, metrics.command_buffers));
        const auto mebibytes = [](std::uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); };
        lines.push_back(std::format("memory {:.1f} MiB in {} allocations, {:.1f} MiB in {} blocks", mebibytes(metrics.memory.allocation_bytes),
                                    metrics.memory.allocation_count, mebibytes(metrics.memory.block_bytes), metrics.memory.block_count));
        return lines;
    }

private:
    const Device& m_device;
    FrameMetricsDescription m_description{};
    FrameMetricsSnapshot m_snapshot{};
    CommandStatistics m_commands{};
    std::uint32_t m_command_buffers = 0;
    std::optional<std::chrono::steady_clock::time_point> m_frame_start{};
    std::chrono::steady_clock::time_point m_period_start{};
    std::uint32_t m_period_frames = 0;
    double m_period_milliseconds = 0.0;
    double m_period_minimum = 0.0;
    double m_period_maximum = 0.0;
};

} // namespace gfx
