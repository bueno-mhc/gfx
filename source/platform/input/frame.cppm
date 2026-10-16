/// @file frame.cppm
/// @brief InputFrame: the state of every action and device for one game frame, as InputSystem::update()
///        computed it. A frame never changes once published, so jobs of the frame read it from any
///        thread without locks; it is copyable (replays, network prediction).
///
/// @code
/// const InputFrame& input = input_system.update(platform.poll_events());
/// if (input.was_action_just_pressed("jump"_action))
/// {
///     ...
/// }
/// const Vector2 movement = input.get_action_value_2d("move"_action);
/// @endcode
export module gfx.platform.input:frame;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;
import :binding;

namespace gfx::platform
{

/// The working state of an InputSystem (input_system.cpp).
struct InputSystemState;

/// An action as InputSystem knows it (names are kept for messages and saved bindings).
struct ActionRecord
{
    ActionIdentifier identifier{};
    std::string name{};
    ActionValueType type = ActionValueType::BUTTON;
};

/// The actions of an InputSystem, shared by its frames: schema order, plus an index sorted by identifier.
struct ActionTable
{
    std::vector<ActionRecord> actions{};
    std::vector<std::pair<ActionIdentifier, std::uint32_t>> by_identifier{};

    /// Index of @p action, if it is declared.
    [[nodiscard]] auto find(ActionIdentifier action) const noexcept -> std::optional<std::uint32_t>
    {
        const auto found = std::ranges::lower_bound(by_identifier, action, {}, &std::pair<ActionIdentifier, std::uint32_t>::first);
        if (found == by_identifier.end() || found->first != action)
        {
            return std::nullopt;
        }
        return found->second;
    }
};

} // namespace gfx::platform

export namespace gfx::platform
{

/// @brief State of one action for one player during a frame.
struct ActionState
{
    Vector2 value{};                             ///< BUTTON: x = strength (1 for digital sources); AXIS_1D: x; AXIS_2D: x and y.
    bool pressed = false;                        ///< BUTTON: a binding presses it; axes: the value is not zero.
    bool just_pressed = false;                   ///< Pressed this frame, not the previous one.
    bool just_released = false;                  ///< Pressed the previous frame, not this one.
    std::chrono::nanoseconds pressed_duration{}; ///< Time since it was pressed (0 while released).

    constexpr bool operator==(const ActionState&) const = default;
};

/// @brief The binding InputSystem::capture_next_binding() captured (or its cancellation).
struct CapturedBinding
{
    PlayerIdentifier player{}; ///< Player the capture was for.
    Binding binding{};         ///< The input pressed, as a binding (empty when canceled).
    bool canceled = false;     ///< The cancel key was pressed.

    constexpr bool operator==(const CapturedBinding&) const = default;
};

/// @brief A gamepad's state during a frame.
struct GamepadState
{
    GamepadHandle gamepad{};                                         ///< The gamepad (null when the player has none).
    std::uint32_t buttons = 0;                                       ///< Bit n: GamepadButton n is down.
    std::array<float, ENUMERATOR_COUNT<GamepadAxis>> axes{};         ///< Axis values, no dead zone applied.

    constexpr bool operator==(const GamepadState&) const = default;
};

/// @brief Every action and device of one game frame (InputSystem::update()). Action queries take the
///        player (player 0 by default).
/// @throws InvalidUsageError (action queries) for an action the schema does not declare or a player
///         beyond InputSystemDescription::players.
class InputFrame
{
public:
    /// @brief An empty frame (no action, nothing pressed).
    InputFrame() = default;

    // ----------------------------------------------------------------------------------------- frame

    /// @brief Number of the frame: 1 for the first update(), then one more per update().
    [[nodiscard]] auto number() const noexcept -> std::uint64_t
    {
        return m_number;
    }

    /// @brief Time of the update() that computed the frame (steady clock).
    [[nodiscard]] auto time() const noexcept -> std::chrono::nanoseconds
    {
        return m_time;
    }

    /// @brief Number of players.
    [[nodiscard]] auto player_count() const noexcept -> std::uint32_t
    {
        return m_player_count;
    }

    // ----------------------------------------------------------------------------------------- actions

    /// @brief Whether @p action is pressed (BUTTON) or not zero (axes).
    [[nodiscard]] auto is_action_pressed(ActionIdentifier action,
                                         PlayerIdentifier player = {},
                                         std::source_location location = std::source_location::current()) const -> bool;

    /// @brief Whether @p action became pressed this frame.
    [[nodiscard]] auto was_action_just_pressed(ActionIdentifier action,
                                               PlayerIdentifier player = {},
                                               std::source_location location = std::source_location::current()) const -> bool;

    /// @brief Whether @p action stopped being pressed this frame.
    [[nodiscard]] auto was_action_just_released(ActionIdentifier action,
                                                PlayerIdentifier player = {},
                                                std::source_location location = std::source_location::current()) const -> bool;

    /// @brief Value of @p action: AXIS_1D value, BUTTON strength, or the x of an AXIS_2D action.
    [[nodiscard]] auto get_action_value(ActionIdentifier action,
                                        PlayerIdentifier player = {},
                                        std::source_location location = std::source_location::current()) const -> float;

    /// @brief Value of @p action as a vector: AXIS_2D value, or (value, 0) for the other types.
    [[nodiscard]] auto get_action_value_2d(ActionIdentifier action,
                                           PlayerIdentifier player = {},
                                           std::source_location location = std::source_location::current()) const -> Vector2;

    /// @brief Everything about @p action.
    [[nodiscard]] auto action_state(ActionIdentifier action,
                                    PlayerIdentifier player = {},
                                    std::source_location location = std::source_location::current()) const -> const ActionState&;

    // ----------------------------------------------------------------------------------------- devices

    /// @brief Whether physical key @p key is down (or was pressed during the frame).
    [[nodiscard]] auto is_key_down(Key key) const noexcept -> bool;

    /// @brief Whether a key the layout labels @p key_code is down (or was pressed during the frame).
    [[nodiscard]] auto is_key_code_down(KeyCode key_code) const noexcept -> bool;

    /// @brief Shift, Control, Alt and Super keys down (each side separately).
    [[nodiscard]] auto modifiers() const noexcept -> KeyModifier
    {
        return m_modifiers;
    }

    /// @brief Whether mouse button @p button is down (or was pressed during the frame).
    [[nodiscard]] auto is_mouse_button_down(MouseButton button) const noexcept -> bool;

    /// @brief Last cursor position, in window coordinates.
    [[nodiscard]] auto mouse_position() const noexcept -> Vector2
    {
        return m_mouse_position;
    }

    /// @brief Mouse motion during the frame (relative motion when the cursor is captured).
    [[nodiscard]] auto mouse_motion() const noexcept -> Vector2
    {
        return m_mouse_motion;
    }

    /// @brief Wheel motion during the frame (+y away from the user).
    [[nodiscard]] auto mouse_wheel() const noexcept -> Vector2
    {
        return m_mouse_wheel;
    }

    /// @brief The gamepad of @p player and its state (a null gamepad when the player has none).
    /// @throws InvalidUsageError for a player beyond InputSystemDescription::players.
    [[nodiscard]] auto gamepad(PlayerIdentifier player = {},
                               std::source_location location = std::source_location::current()) const -> const GamepadState&;

    /// @brief Whether @p button of @p player's gamepad is down (or was pressed during the frame).
    [[nodiscard]] auto is_gamepad_button_down(GamepadButton button,
                                              PlayerIdentifier player = {},
                                              std::source_location location = std::source_location::current()) const -> bool;

    /// @brief Value of @p axis of @p player's gamepad (0 without a gamepad; no dead zone applied).
    [[nodiscard]] auto gamepad_axis(GamepadAxis axis,
                                    PlayerIdentifier player = {},
                                    std::source_location location = std::source_location::current()) const -> float;

    /// @brief Whether text input was active (keyboard bindings of most contexts read nothing then).
    [[nodiscard]] auto is_text_input_active() const noexcept -> bool
    {
        return m_text_input_active;
    }

    /// @brief The binding InputSystem::capture_next_binding() captured during this frame, if any.
    [[nodiscard]] auto captured_binding() const noexcept -> const std::optional<CapturedBinding>&
    {
        return m_captured_binding;
    }

private:
    friend class InputSystem;
    friend struct InputSystemState;

    /// Slots of the key bitset: every Key value is below it.
    static constexpr std::size_t KEY_SLOTS = 256;

    [[nodiscard]] auto state_of(ActionIdentifier action,
                                PlayerIdentifier player,
                                std::string_view operation,
                                std::source_location location) const -> const ActionState&;
    void check_player(PlayerIdentifier player,
                      std::string_view operation,
                      std::source_location location) const;

private:
    std::shared_ptr<const ActionTable> m_actions{};
    std::vector<ActionState> m_states{};
    std::uint32_t m_player_count = 0;
    std::uint64_t m_number = 0;
    std::chrono::nanoseconds m_time{};
    std::bitset<KEY_SLOTS> m_keys{};
    std::array<KeyCode, KEY_SLOTS> m_key_codes{};
    KeyModifier m_modifiers = KeyModifier::NONE;
    MouseButtonMask m_mouse_buttons = MouseButtonMask::NONE;
    Vector2 m_mouse_position{};
    Vector2 m_mouse_motion{};
    Vector2 m_mouse_wheel{};
    std::array<GamepadState, MAXIMUM_PLAYERS> m_gamepads{};
    bool m_text_input_active = false;
    std::optional<CapturedBinding> m_captured_binding{};
};

} // namespace gfx::platform
