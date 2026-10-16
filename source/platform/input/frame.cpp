// InputFrame queries.
module gfx.platform.input;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;

namespace gfx::platform
{

void
InputFrame::check_player(PlayerIdentifier player,
                         std::string_view operation,
                         std::source_location location) const
{
    if (player.index() < m_player_count)
    {
        return;
    }
    throw InvalidUsageError(
        {
            .backend = std::nullopt,
            .operation = std::string{operation},
            .problem = std::format("{} does not exist: the input system has {} player(s)", player, m_player_count),
            .suggestion = "raise InputSystemDescription::players",
        },
        location);
}

auto
InputFrame::state_of(ActionIdentifier action,
                     PlayerIdentifier player,
                     std::string_view operation,
                     std::source_location location) const -> const ActionState&
{
    // The action is declared and the player exists.
    const std::optional<std::uint32_t> index = m_actions ? m_actions->find(action) : std::nullopt;
    if (!index)
    {
        throw InvalidUsageError(
            {
                .backend = std::nullopt,
                .operation = std::string{operation},
                .problem = m_actions ? std::format("{} is not declared by the input schema", action)
                                     : std::format("{} was read from an empty frame (before InputSystem::update())", action),
                .suggestion = "declare the action in the schema (action()), and read frames InputSystem::update() returns",
            },
            location);
    }
    check_player(player, operation, location);
    return m_states[(static_cast<std::size_t>(player.index()) * m_actions->actions.size()) + *index];
}

auto
InputFrame::is_action_pressed(ActionIdentifier action,
                              PlayerIdentifier player,
                              std::source_location location) const -> bool
{
    return state_of(action, player, "InputFrame::is_action_pressed", location).pressed;
}

auto
InputFrame::was_action_just_pressed(ActionIdentifier action,
                                    PlayerIdentifier player,
                                    std::source_location location) const -> bool
{
    return state_of(action, player, "InputFrame::was_action_just_pressed", location).just_pressed;
}

auto
InputFrame::was_action_just_released(ActionIdentifier action,
                                     PlayerIdentifier player,
                                     std::source_location location) const -> bool
{
    return state_of(action, player, "InputFrame::was_action_just_released", location).just_released;
}

auto
InputFrame::get_action_value(ActionIdentifier action,
                             PlayerIdentifier player,
                             std::source_location location) const -> float
{
    return state_of(action, player, "InputFrame::get_action_value", location).value.x;
}

auto
InputFrame::get_action_value_2d(ActionIdentifier action,
                                PlayerIdentifier player,
                                std::source_location location) const -> Vector2
{
    return state_of(action, player, "InputFrame::get_action_value_2d", location).value;
}

auto
InputFrame::action_state(ActionIdentifier action,
                         PlayerIdentifier player,
                         std::source_location location) const -> const ActionState&
{
    return state_of(action, player, "InputFrame::action_state", location);
}

auto
InputFrame::is_key_down(Key key) const noexcept -> bool
{
    const auto slot = static_cast<std::size_t>(std::to_underlying(key));
    return slot < KEY_SLOTS && m_keys.test(slot);
}

auto
InputFrame::is_key_code_down(KeyCode key_code) const noexcept -> bool
{
    // Keys remember the key code they produced when pressed.
    for (std::size_t slot = 0; slot < KEY_SLOTS; ++slot)
    {
        if (m_keys.test(slot) && m_key_codes[slot] == key_code)
        {
            return true;
        }
    }
    return false;
}

auto
InputFrame::is_mouse_button_down(MouseButton button) const noexcept -> bool
{
    const auto bit = static_cast<MouseButtonMask>(1U << std::to_underlying(button));
    return has_any(m_mouse_buttons, bit);
}

auto
InputFrame::gamepad(PlayerIdentifier player,
                    std::source_location location) const -> const GamepadState&
{
    check_player(player, "InputFrame::gamepad", location);
    return m_gamepads[player.index()];
}

auto
InputFrame::is_gamepad_button_down(GamepadButton button,
                                   PlayerIdentifier player,
                                   std::source_location location) const -> bool
{
    check_player(player, "InputFrame::is_gamepad_button_down", location);
    return (m_gamepads[player.index()].buttons & (1U << std::to_underlying(button))) != 0;
}

auto
InputFrame::gamepad_axis(GamepadAxis axis,
                         PlayerIdentifier player,
                         std::source_location location) const -> float
{
    check_player(player, "InputFrame::gamepad_axis", location);
    return m_gamepads[player.index()].axes[std::to_underlying(axis)];
}

} // namespace gfx::platform
