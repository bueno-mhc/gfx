/// @file deleter.cppm
/// @brief Deleter: the device's deferred-destruction queue.
export module gfx.interface:deleter;

import std;
import gfx.enums;
import :backend_object;

export namespace gfx
{

/// @brief Defers work until the GPU has finished every submission made before it was enqueued.
///
/// Backends destroy native objects through it, which is why destroying a gfx object is always safe,
/// even while the GPU still uses it: the destructor returns immediately and the native object is
/// released later. Engines can enqueue their own work too (e.g. recycling staging buffers).
class Deleter : public BackendObject
{
public:
    /// @brief Runs @p deletion once every command submitted to any queue of the device before this
    ///        call has completed.
    virtual void enqueue(std::move_only_function<void()> deletion) = 0;

    /// @brief Runs the deletions whose submissions have completed (also done by every Queue::submit()).
    /// @return Number of deletions run.
    virtual auto collect() -> std::size_t = 0;

    /// @brief Waits until the device is idle, then runs every pending deletion.
    virtual void flush() = 0;

    /// @brief Number of deletions not run yet.
    [[nodiscard]] virtual auto pending_count() const noexcept -> std::size_t = 0;
};

void
Deleter::enqueue(std::move_only_function<void()> /*deletion*/)
{
    throw_not_implemented("Deleter::enqueue");
}

auto
Deleter::collect() -> std::size_t
{
    throw_not_implemented("Deleter::collect");
}

void
Deleter::flush()
{
    throw_not_implemented("Deleter::flush");
}

} // namespace gfx
