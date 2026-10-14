/// @file command_pool.cppm
/// @brief CommandPool: allocates command buffers for one queue type, used by one thread at a time.
export module gfx.interface:command_pool;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :command_buffer;

export namespace gfx
{

/// @brief How to create a CommandPool.
struct CommandPoolDescription
{
    QueueType queue_type = QueueType::GRAPHICS;          ///< Queue type its command buffers are submitted to.
    CommandPoolOption options = CommandPoolOption::NONE; ///< Transient buffers, individual resets.
    std::string_view debug_name{};                       ///< Name shown by debugging tools.
};

/// @brief Allocates command buffers. Not thread-safe: give each recording thread its own pool per
///        queue type, so threads record in parallel without locks.
class CommandPool : public BackendObject
{
public:
    /// @brief Handle naming this pool.
    [[nodiscard]] virtual auto handle() const noexcept -> CommandPoolHandle = 0;

    /// @brief Queue type of its command buffers.
    [[nodiscard]] virtual auto queue_type() const noexcept -> QueueType = 0;

    /// @brief Allocates a command buffer.
    [[nodiscard]] virtual auto allocate(CommandBufferLevel level = CommandBufferLevel::PRIMARY,
                                        std::string_view debug_name = {}) -> std::unique_ptr<CommandBuffer> = 0;

    /// @brief Discards the commands of every command buffer of the pool (none may be pending on a queue).
    virtual void reset() = 0;
};

auto
CommandPool::allocate(CommandBufferLevel /*level*/,
                      std::string_view /*debug_name*/) -> std::unique_ptr<CommandBuffer>
{
    throw_not_implemented("CommandPool::allocate");
}

void
CommandPool::reset()
{
    throw_not_implemented("CommandPool::reset");
}

} // namespace gfx
