/// @file fence.cppm
/// @brief Fence: lets the CPU wait for a queue submission to complete.
export module gfx.interface:fence;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;

export namespace gfx
{

/// @brief How to create a Fence.
struct FenceDescription
{
    bool signaled = false;         ///< Create it already signaled.
    std::string_view debug_name{}; ///< Name shown by debugging tools.
};

/// @brief Signaled by the GPU when a submission completes; waited on by the CPU.
class Fence : public BackendObject
{
public:
    /// @brief Handle naming this fence in submissions.
    [[nodiscard]] virtual auto handle() const noexcept -> FenceHandle = 0;

    /// @brief Whether the fence is signaled, without waiting.
    [[nodiscard]] virtual auto is_signaled() const -> bool = 0;

    /// @brief Blocks the calling thread until the fence is signaled or @p timeout expires.
    [[nodiscard]] virtual auto wait(std::chrono::nanoseconds timeout = WAIT_INDEFINITELY) const -> WaitStatus = 0;

    /// @brief Returns the fence to the unsignaled state.
    virtual void reset() = 0;
};

auto
Fence::is_signaled() const -> bool
{
    throw_not_implemented("Fence::is_signaled");
}

auto
Fence::wait(std::chrono::nanoseconds /*timeout*/) const -> WaitStatus
{
    throw_not_implemented("Fence::wait");
}

void
Fence::reset()
{
    throw_not_implemented("Fence::reset");
}

} // namespace gfx
