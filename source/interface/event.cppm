/// @file event.cppm
/// @brief Event: split barrier, set in one place of a command stream and waited on later.
export module gfx.interface:event;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;

export namespace gfx
{

/// @brief How to create an Event.
struct EventDescription
{
    std::string_view debug_name{}; ///< Name shown by debugging tools.
};

/// @brief Split barrier (DeviceFeature::EVENTS): Recorder::set_event() records the source side,
///        Recorder::wait_events() the destination side, letting unrelated work run in between.
class Event : public BackendObject
{
public:
    /// @brief Handle naming this event in commands.
    [[nodiscard]] virtual auto handle() const noexcept -> EventHandle = 0;

    /// @brief Whether the event is set.
    [[nodiscard]] virtual auto is_set() const -> bool = 0;

    /// @brief Sets the event from the CPU.
    virtual void set() = 0;

    /// @brief Resets the event from the CPU.
    virtual void reset() = 0;
};

auto
Event::is_set() const -> bool
{
    throw_not_implemented("Event::is_set");
}

void
Event::set()
{
    throw_not_implemented("Event::set");
}

void
Event::reset()
{
    throw_not_implemented("Event::reset");
}

} // namespace gfx
