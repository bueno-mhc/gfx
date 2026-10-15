/// @file frame_command_pools.cppm
/// @brief FrameCommandPools: the command pools one recording thread uses for the frames in flight.
export module gfx.interface:frame_command_pools;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import :command_buffer;
import :command_pool;
import :timeline_semaphore;
import :device;

export namespace gfx
{

/// @brief The command pools of one recording thread for one queue type: one pool per frame in flight,
///        reused once the GPU has finished the frame that last recorded into it.
///
/// Frames are numbered from 1, and the last submission of frame N signals value N on the frame
/// timeline. begin_frame(N) picks pool N % frames_in_flight, waits until the timeline reaches the frame
/// that last used that pool, resets the pool, and command_buffer() then hands out its command buffers
/// again instead of allocating new ones. Not thread-safe: give each recording thread its own instance,
/// which is what lets threads record without locks.
class FrameCommandPools
{
public:
    /// @brief Creates one command pool per frame in flight.
    /// @param device           Device creating the pools.
    /// @param description      Description of every pool (queue type, options, debug name).
    /// @param frame_timeline   Timeline whose value N means "the GPU finished frame N".
    /// @param frames_in_flight Frames the CPU may record ahead of the GPU (at least 1).
    /// @throws InvalidUsageError when @p frames_in_flight is 0.
    FrameCommandPools(Device& device,
                      const CommandPoolDescription& description,
                      const TimelineSemaphore& frame_timeline,
                      std::uint32_t frames_in_flight)
        : m_frame_timeline{frame_timeline}
        , m_debug_name{description.debug_name}
    {
        // At least one frame, or there is nowhere to record.
        if (frames_in_flight == 0)
        {
            throw InvalidUsageError({
                .backend = device.backend(),
                .operation = "FrameCommandPools::FrameCommandPools",
                .problem = "frames_in_flight is 0",
                .suggestion = "use 2 or 3 frames in flight (1 makes the CPU wait for the GPU every frame)",
            });
        }

        // One pool per frame in flight.
        m_frames.resize(frames_in_flight);
        for (FramePool& frame : m_frames)
        {
            frame.pool = device.create_command_pool(description);
        }
    }

    /// @brief Starts recording frame @p frame: waits until the GPU finished the frame that last used
    ///        this frame's pool, then resets the pool (its command buffers are handed out again).
    /// @throws InvalidUsageError when @p frame is 0 or not greater than the previous frame.
    void begin_frame(std::uint64_t frame)
    {
        // Frames count up from 1.
        if (frame == 0 || frame <= m_current_frame)
        {
            throw InvalidUsageError({
                .backend = m_frames.front().pool->backend(),
                .operation = "FrameCommandPools::begin_frame",
                .problem = std::format("frame {} follows frame {}; frames are numbered from 1 and increase", frame,
                                       m_current_frame),
                .suggestion = "pass the timeline value the frame's last submission signals",
            });
        }
        m_current_frame = frame;
        FramePool& current = m_frames[frame % m_frames.size()];

        // The GPU must be done with the commands recorded in this pool before they are discarded. An
        // indefinite wait only returns once the value is reached (device loss throws DeviceLostError).
        if (current.last_frame != 0)
        {
            (void)m_frame_timeline.wait(current.last_frame);
            current.pool->reset();
        }
        current.last_frame = frame;
        current.used_primaries = 0;
        current.used_secondaries = 0;
    }

    /// @brief A command buffer of the current frame's pool, ready to record (reused from an earlier
    ///        frame when possible). Valid until the pool is reused, frames_in_flight() frames later.
    /// @throws InvalidUsageError when no frame was begun.
    [[nodiscard]] auto command_buffer(CommandBufferLevel level = CommandBufferLevel::PRIMARY) -> CommandBuffer&
    {
        // Reuse the command buffers of earlier frames of this pool, allocate beyond them.
        FramePool& current = current_frame_pool("FrameCommandPools::command_buffer");
        const bool primary = level == CommandBufferLevel::PRIMARY;
        std::vector<std::unique_ptr<CommandBuffer>>& buffers = primary ? current.primaries : current.secondaries;
        std::size_t& used = primary ? current.used_primaries : current.used_secondaries;
        if (used == buffers.size())
        {
            buffers.push_back(current.pool->allocate(level, m_debug_name));
        }
        return *buffers[used++];
    }

    /// @brief The current frame's pool.
    /// @throws InvalidUsageError when no frame was begun.
    [[nodiscard]] auto pool() -> CommandPool&
    {
        return *current_frame_pool("FrameCommandPools::pool").pool;
    }

    /// @brief Frame passed to the last begin_frame() (0 before the first).
    [[nodiscard]] auto current_frame() const noexcept -> std::uint64_t
    {
        return m_current_frame;
    }

    /// @brief Number of pools, i.e. frames the CPU may record ahead of the GPU.
    [[nodiscard]] auto frames_in_flight() const noexcept -> std::uint32_t
    {
        return static_cast<std::uint32_t>(m_frames.size());
    }

private:
    struct FramePool
    {
        std::unique_ptr<CommandPool> pool{};
        std::vector<std::unique_ptr<CommandBuffer>> primaries{};
        std::vector<std::unique_ptr<CommandBuffer>> secondaries{};
        std::size_t used_primaries = 0;
        std::size_t used_secondaries = 0;
        std::uint64_t last_frame = 0;
    };

    auto current_frame_pool(std::string_view operation) -> FramePool&
    {
        if (m_current_frame == 0)
        {
            throw InvalidUsageError({
                .backend = m_frames.front().pool->backend(),
                .operation = std::string{operation},
                .problem = "no frame was begun",
                .suggestion = "call begin_frame() at the start of every frame",
            });
        }
        return m_frames[m_current_frame % m_frames.size()];
    }

private:
    const TimelineSemaphore& m_frame_timeline;
    std::string m_debug_name;
    std::vector<FramePool> m_frames;
    std::uint64_t m_current_frame = 0;
};

} // namespace gfx
