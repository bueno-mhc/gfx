/// @file handles.cppm
/// @brief The handles and identifiers of the platform library: windows, monitors, cursors and input
///        devices (issued by the platform backend), actions and binding contexts (declared by the
///        application, hashed at compile time), and player slots. Handle<Tag> and Identifier<Tag>
///        come from gfx.core.handles, which this module re-exports.
export module gfx.platform.handles;

import std;
export import gfx.core.handles;
import gfx.platform.enums;

export namespace gfx::platform
{

// ================================================================================================= Tags

/// @brief Tag of WindowHandle.
struct WindowTag
{
    static constexpr std::string_view NAME = "Window";
};

/// @brief Tag of MonitorHandle.
struct MonitorTag
{
    static constexpr std::string_view NAME = "Monitor";
};

/// @brief Tag of CursorHandle.
struct CursorTag
{
    static constexpr std::string_view NAME = "Cursor";
};

/// @brief Tag of GamepadHandle.
struct GamepadTag
{
    static constexpr std::string_view NAME = "Gamepad";
};

/// @brief Tag of JoystickHandle.
struct JoystickTag
{
    static constexpr std::string_view NAME = "Joystick";
};

/// @brief Tag of TouchDeviceHandle.
struct TouchDeviceTag
{
    static constexpr std::string_view NAME = "TouchDevice";
};

/// @brief Tag of ActionIdentifier.
struct ActionTag
{
    static constexpr std::string_view NAME = "Action";
};

/// @brief Tag of BindingContextIdentifier.
struct BindingContextTag
{
    static constexpr std::string_view NAME = "BindingContext";
};

// ================================================================================================= Handles and identifiers

using WindowHandle = Handle<WindowTag>;           ///< Names a Window.
using MonitorHandle = Handle<MonitorTag>;         ///< Names a monitor (owned by the Platform).
using CursorHandle = Handle<CursorTag>;           ///< Names a Cursor.
using GamepadHandle = Handle<GamepadTag>;         ///< Names a connected gamepad.
using JoystickHandle = Handle<JoystickTag>;       ///< Names a connected joystick.
using TouchDeviceHandle = Handle<TouchDeviceTag>; ///< Names a touch device.

using ActionIdentifier = Identifier<ActionTag>;                 ///< Names an action ("jump"_action).
using BindingContextIdentifier = Identifier<BindingContextTag>; ///< Names a binding context ("gameplay"_context).

/// @brief A local player: a slot from 0 to MAXIMUM_PLAYERS - 1 that devices and binding contexts are
///        routed to (couch multiplayer). Player 0 owns the keyboard and the mouse by default.
class PlayerIdentifier
{
public:
    /// @brief Player 0.
    constexpr PlayerIdentifier() noexcept = default;

    /// @brief Player @p index (checked against MAXIMUM_PLAYERS where players are used).
    constexpr explicit PlayerIdentifier(std::uint32_t index) noexcept
        : m_index{index}
    {
    }

    /// @brief Slot of the player.
    [[nodiscard]] constexpr auto index() const noexcept -> std::uint32_t
    {
        return m_index;
    }

    /// @brief Players compare by slot.
    [[nodiscard]] constexpr auto operator<=>(const PlayerIdentifier&) const noexcept = default;

private:
    std::uint32_t m_index = 0;
};

// ================================================================================================= Concepts

/// @brief A handle the platform issues (windows, monitors, cursors, input devices).
template<class Type>
concept PlatformHandle = TaggedHandle<Type>
                         && (std::same_as<Type, WindowHandle> || std::same_as<Type, MonitorHandle>
                             || std::same_as<Type, CursorHandle> || std::same_as<Type, GamepadHandle>
                             || std::same_as<Type, JoystickHandle> || std::same_as<Type, TouchDeviceHandle>);

/// @brief A handle naming an input device.
template<class Type>
concept InputDeviceHandle = std::same_as<Type, GamepadHandle> || std::same_as<Type, JoystickHandle>
                            || std::same_as<Type, TouchDeviceHandle>;

/// @brief An identifier the application declares for the input system.
template<class Type>
concept InputIdentifier = std::same_as<Type, ActionIdentifier> || std::same_as<Type, BindingContextIdentifier>;

// ================================================================================================= Literals

/// @brief Compile-time identifiers: `using namespace gfx::platform::literals;` then `"jump"_action`.
namespace literals
{

/// @brief The action named @p name, e.g. `"jump"_action`.
consteval auto
operator""_action(const char* name,
                  std::size_t length) -> ActionIdentifier
{
    return ActionIdentifier{std::string_view{name, length}};
}

/// @brief The binding context named @p name, e.g. `"gameplay"_context`.
consteval auto
operator""_context(const char* name,
                   std::size_t length) -> BindingContextIdentifier
{
    return BindingContextIdentifier{std::string_view{name, length}};
}

} // namespace literals

} // namespace gfx::platform

/// @brief Formats a player as "Player(1)".
template<>
struct std::formatter<gfx::platform::PlayerIdentifier, char> : std::formatter<std::string_view, char>
{
    template<class FormatContext>
    auto format(const gfx::platform::PlayerIdentifier& player, FormatContext& context) const -> FormatContext::iterator
    {
        const std::string text = std::format("Player({})", player.index());
        return std::formatter<std::string_view, char>::format(text, context);
    }
};
