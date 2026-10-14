/// @file timeline_semaphore.cppm
/// @brief TimelineSemaphore: monotonically increasing counter that orders work across queues and
///        lets the CPU wait for (or signal) any point of a timeline.
export module gfx.interface:timeline_semaphore;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;

export namespace gfx
{

/// @brief How to create a TimelineSemaphore.
struct TimelineSemaphoreDescription
{
    std::uint64_t initial_value = 0; ///< Starting value.
    std::string_view debug_name{};   ///< Name shown by debugging tools.
};

/// @brief 64-bit counter signaled and waited on by queues (TimelineSemaphoreSignal/Wait in
///        submissions) and by the CPU. The primary tool to run compute and graphics work on separate
///        queues in parallel while ordering only where results are shared.
class TimelineSemaphore : public BackendObject
{
public:
    /// @brief Handle naming this semaphore in submissions.
    [[nodiscard]] virtual auto handle() const noexcept -> TimelineSemaphoreHandle = 0;

    /// @brief Current value.
    [[nodiscard]] virtual auto value() const -> std::uint64_t = 0;

    /// @brief Blocks the calling thread until the value reaches @p value or @p timeout expires.
    [[nodiscard]] virtual auto wait(std::uint64_t value,
                                    std::chrono::nanoseconds timeout = WAIT_INDEFINITELY) const -> WaitStatus = 0;

    /// @brief Sets the value from the CPU (it must be greater than the current value).
    virtual void signal(std::uint64_t value) = 0;
};

auto
TimelineSemaphore::value() const -> std::uint64_t
{
    throw_not_implemented("TimelineSemaphore::value");
}

auto
TimelineSemaphore::wait(std::uint64_t /*value*/,
                        std::chrono::nanoseconds /*timeout*/) const -> WaitStatus
{
    throw_not_implemented("TimelineSemaphore::wait");
}

void
TimelineSemaphore::signal(std::uint64_t /*value*/)
{
    throw_not_implemented("TimelineSemaphore::signal");
}

} // namespace gfx
