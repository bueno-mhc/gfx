/// @file frame_timeline.cppm
/// @brief FrameTimeline: paces the CPU against the GPU with one timeline semaphore, the frame loop every
///        per-frame resource (FrameCommandPools, TransientBufferAllocator, Uploader, GpuProfiler) relies on.
export module gfx.interface:frame_timeline;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import :synchronization;
import :timeline_semaphore;
import :deleter;
import :device;

export namespace gfx
{

/// @brief Frame numbering and pacing. Frames are numbered from 1; the last submission of frame N
///        signals value N on timeline() (frame_signal()). begin_frame() lets the CPU run at most
///        frames_in_flight() frames ahead of the GPU, and runs the device's deferred destructions that
///        the GPU has finished with.
///
/// @code
/// const std::uint64_t frame = timeline.begin_frame();   // waits for frame - frames_in_flight
/// pools.begin_frame(frame); transient.begin_frame(frame); uploader.begin_frame(frame);
/// ... record ...
/// queue.submit({{.command_buffers = buffers, .timeline_signals = std::array{timeline.frame_signal()}}});
/// @endcode
class FrameTimeline
{
public:
    /// @brief Creates the timeline semaphore.
    /// @throws InvalidUsageError when @p frames_in_flight is 0.
    FrameTimeline(Device& device, std::uint32_t frames_in_flight, std::string_view debug_name = "frame timeline")
        : m_device{device}
        , m_frames_in_flight{frames_in_flight}
    {
        // At least one frame in flight.
        if (frames_in_flight == 0)
        {
            throw InvalidUsageError({
                .backend = device.backend(),
                .operation = "FrameTimeline::FrameTimeline",
                .problem = "frames_in_flight is 0",
                .suggestion = "use 2 or 3 frames in flight (1 makes the CPU wait for the GPU every frame)",
            });
        }
        m_timeline = device.create_timeline_semaphore({.initial_value = 0, .debug_name = debug_name});
    }

    /// @brief Starts the next frame: waits until the GPU finished frame (N - frames_in_flight), then runs
    ///        the deferred destructions it allows.
    /// @return The new frame number N.
    auto begin_frame() -> std::uint64_t
    {
        // Wait for the frame whose resources this frame reuses.
        ++m_current_frame;
        if (m_current_frame > m_frames_in_flight)
        {
            (void)m_timeline->wait(m_current_frame - m_frames_in_flight);
        }

        // Objects destroyed while the GPU used them can now be released.
        (void)m_device.deleter().collect();
        return m_current_frame;
    }

    /// @brief The signal the last submission of the current frame carries.
    [[nodiscard]] auto frame_signal(PipelineStage stages = PipelineStage::ALL_COMMANDS) const noexcept -> TimelineSemaphoreSignal
    {
        return {.semaphore = m_timeline->handle(), .value = m_current_frame, .stages = stages};
    }

    /// @brief A wait for the GPU to finish frame @p frame (e.g. for work on another queue).
    [[nodiscard]] auto frame_wait(std::uint64_t frame, PipelineStage stages = PipelineStage::ALL_COMMANDS) const noexcept
        -> TimelineSemaphoreWait
    {
        return {.semaphore = m_timeline->handle(), .value = frame, .stages = stages};
    }

    /// @brief Number of the frame being recorded (0 before the first begin_frame()).
    [[nodiscard]] auto current_frame() const noexcept -> std::uint64_t
    {
        return m_current_frame;
    }

    /// @brief Last frame the GPU finished.
    [[nodiscard]] auto completed_frame() const -> std::uint64_t
    {
        return m_timeline->value();
    }

    /// @brief Frames the CPU may record ahead of the GPU.
    [[nodiscard]] auto frames_in_flight() const noexcept -> std::uint32_t
    {
        return m_frames_in_flight;
    }

    /// @brief The timeline semaphore (to wait for or signal frames from other queues).
    [[nodiscard]] auto timeline() const noexcept -> TimelineSemaphore&
    {
        return *m_timeline;
    }

    /// @brief Blocks until the GPU finished the current frame.
    void wait_idle() const
    {
        if (m_current_frame != 0)
        {
            (void)m_timeline->wait(m_current_frame);
        }
    }

private:
    Device& m_device;
    std::uint32_t m_frames_in_flight;
    std::unique_ptr<TimelineSemaphore> m_timeline;
    std::uint64_t m_current_frame = 0;
};

} // namespace gfx
