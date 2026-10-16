/// @file binding.cppm
/// @brief Bindings: what input an action reads (keys, buttons, axes, chords, composites) and how its
///        value is shaped (dead zone, response curve, inversion, scale, swizzle, normalization) and
///        triggered (down, hold, tap). Bindings are constexpr values, so a game's input schema can be a
///        compile-time constant; they are also changed at run time (rebinding).
///
/// @code
/// constexpr Binding move_stick = stick(GamepadAxis::LEFT_X, GamepadAxis::LEFT_Y)
///                                    .with_deadzone(DeadzoneKind::SCALED_RADIAL, 0.15F)
///                                    .with_curve(ResponseCurve::QUADRATIC);
/// constexpr ContextBinding jump = bind("jump"_action, Key::SPACE);
/// @endcode
export module gfx.platform.input:binding;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;

export namespace gfx::platform
{

/// @brief One input a binding reads: its kind and the enumerator's value (a Key, KeyCode, MouseButton,
///        MouseAxis, GamepadButton or GamepadAxis).
struct InputSource
{
    InputSourceKind kind = InputSourceKind::NONE; ///< Kind of input.
    std::uint32_t code = 0;                       ///< Value of the enumerator naming it.

    /// @brief A physical key.
    [[nodiscard]] static constexpr auto of(Key key) noexcept -> InputSource
    {
        return {.kind = InputSourceKind::KEY, .code = std::to_underlying(key)};
    }

    /// @brief A logical key.
    [[nodiscard]] static constexpr auto of(KeyCode key_code) noexcept -> InputSource
    {
        return {.kind = InputSourceKind::KEY_CODE, .code = std::to_underlying(key_code)};
    }

    /// @brief A mouse button.
    [[nodiscard]] static constexpr auto of(MouseButton button) noexcept -> InputSource
    {
        return {.kind = InputSourceKind::MOUSE_BUTTON, .code = std::to_underlying(button)};
    }

    /// @brief A mouse axis.
    [[nodiscard]] static constexpr auto of(MouseAxis axis) noexcept -> InputSource
    {
        return {.kind = InputSourceKind::MOUSE_AXIS, .code = std::to_underlying(axis)};
    }

    /// @brief A gamepad button.
    [[nodiscard]] static constexpr auto of(GamepadButton button) noexcept -> InputSource
    {
        return {.kind = InputSourceKind::GAMEPAD_BUTTON, .code = std::to_underlying(button)};
    }

    /// @brief A gamepad axis.
    [[nodiscard]] static constexpr auto of(GamepadAxis axis) noexcept -> InputSource
    {
        return {.kind = InputSourceKind::GAMEPAD_AXIS, .code = std::to_underlying(axis)};
    }

    /// @brief Whether the source is pressed or released (keys, key codes, buttons) rather than analog.
    [[nodiscard]] constexpr auto is_digital() const noexcept -> bool
    {
        return kind == InputSourceKind::KEY || kind == InputSourceKind::KEY_CODE || kind == InputSourceKind::MOUSE_BUTTON
               || kind == InputSourceKind::GAMEPAD_BUTTON;
    }

    constexpr bool operator==(const InputSource&) const = default;
};

/// @brief Any input a single-source binding can read.
template<class Type>
concept InputSourceType = std::same_as<Type, Key> || std::same_as<Type, KeyCode> || std::same_as<Type, MouseButton>
                          || std::same_as<Type, MouseAxis> || std::same_as<Type, GamepadButton> || std::same_as<Type, GamepadAxis>;

/// @brief An input that is pressed or released: what composites and chords combine.
template<class Type>
concept DigitalInputSourceType = std::same_as<Type, Key> || std::same_as<Type, KeyCode> || std::same_as<Type, MouseButton>
                                 || std::same_as<Type, GamepadButton>;

/// @brief What an action reads and how its value is shaped and triggered. Values flow through the
///        modifiers in this order: dead zone, response curve, inversion, scale, swizzle, normalization.
struct Binding
{
    BindingKind kind = BindingKind::NONE;                                ///< How the sources combine.
    std::array<InputSource, MAXIMUM_BINDING_SOURCES> sources{};          ///< Sources, as the kind uses them (see the builders).
    KeyModifier modifiers = KeyModifier::NONE;                           ///< CHORD: modifiers that must be held (SHIFT = either side).
    bool exact_modifiers = false;                                        ///< CHORD: no other Shift, Control, Alt or Super may be held.
    DeadzoneKind deadzone = DeadzoneKind::NONE;                          ///< Dead zone.
    float deadzone_lower = 0.0F;                                         ///< Magnitude below which the value is 0.
    float deadzone_upper = 1.0F;                                         ///< Magnitude from which the value is 1 (AXIAL, SCALED_RADIAL).
    ResponseCurve curve = ResponseCurve::LINEAR;                         ///< Response curve.
    float curve_exponent = 1.0F;                                         ///< ResponseCurve::POWER exponent.
    bool invert_x = false;                                               ///< Negates x.
    bool invert_y = false;                                               ///< Negates y.
    Vector2 scale{.x = 1.0F, .y = 1.0F};                                 ///< Multiplies the value.
    bool swizzle = false;                                                ///< Swaps x and y (a 1D source drives y).
    bool normalize = false;                                              ///< Shortens 2D values longer than 1 (diagonals).
    TriggerKind trigger = TriggerKind::DOWN;                             ///< When the binding presses its action.
    std::chrono::milliseconds trigger_time{};                            ///< HOLD: time to hold; TAP: longest tap.

    /// @brief Number of sources in use.
    [[nodiscard]] constexpr auto source_count() const noexcept -> std::size_t
    {
        std::size_t count = 0;
        while (count < sources.size() && sources[count].kind != InputSourceKind::NONE)
        {
            ++count;
        }
        return count;
    }

    /// @brief This binding with a dead zone: magnitudes below @p lower become 0 (and, for AXIAL and
    ///        SCALED_RADIAL, magnitudes from @p lower to @p upper are rescaled to [0, 1]).
    [[nodiscard]] constexpr auto with_deadzone(DeadzoneKind kind_of_deadzone,
                                               float lower,
                                               float upper = 1.0F) const noexcept -> Binding
    {
        Binding result = *this;
        result.deadzone = kind_of_deadzone;
        result.deadzone_lower = lower;
        result.deadzone_upper = upper;
        return result;
    }

    /// @brief This binding with a response curve (@p exponent for ResponseCurve::POWER).
    [[nodiscard]] constexpr auto with_curve(ResponseCurve response,
                                            float exponent = 1.0F) const noexcept -> Binding
    {
        Binding result = *this;
        result.curve = response;
        result.curve_exponent = exponent;
        return result;
    }

    /// @brief This binding with x and/or y negated.
    [[nodiscard]] constexpr auto inverted(bool x,
                                          bool y) const noexcept -> Binding
    {
        Binding result = *this;
        result.invert_x = x;
        result.invert_y = y;
        return result;
    }

    /// @brief This binding with its value multiplied by @p factor.
    [[nodiscard]] constexpr auto scaled(Vector2 factor) const noexcept -> Binding
    {
        Binding result = *this;
        result.scale = factor;
        return result;
    }

    /// @brief This binding with its value multiplied by @p factor on both axes.
    [[nodiscard]] constexpr auto scaled(float factor) const noexcept -> Binding
    {
        return scaled(Vector2{.x = factor, .y = factor});
    }

    /// @brief This binding with x and y swapped (a one-dimensional source drives y).
    [[nodiscard]] constexpr auto swizzled() const noexcept -> Binding
    {
        Binding result = *this;
        result.swizzle = true;
        return result;
    }

    /// @brief This binding with two-dimensional values no longer than 1.
    [[nodiscard]] constexpr auto normalized() const noexcept -> Binding
    {
        Binding result = *this;
        result.normalize = true;
        return result;
    }

    /// @brief This binding pressing its action only once held for @p time.
    [[nodiscard]] constexpr auto held_for(std::chrono::milliseconds time) const noexcept -> Binding
    {
        Binding result = *this;
        result.trigger = TriggerKind::HOLD;
        result.trigger_time = time;
        return result;
    }

    /// @brief This binding pressing its action for one frame when released within @p time.
    [[nodiscard]] constexpr auto tapped_within(std::chrono::milliseconds time) const noexcept -> Binding
    {
        Binding result = *this;
        result.trigger = TriggerKind::TAP;
        result.trigger_time = time;
        return result;
    }

    constexpr bool operator==(const Binding&) const = default;
};

// ================================================================================================= Builders

/// @brief A binding reading one input: a key, key code, button, or an axis (a gamepad trigger, or a
///        mouse axis accumulated during the frame).
template<InputSourceType Source>
[[nodiscard]] constexpr auto
single(Source source) noexcept -> Binding
{
    Binding binding;
    binding.kind = std::same_as<Source, MouseAxis> ? BindingKind::MOUSE_AXIS : BindingKind::BUTTON;
    binding.sources[0] = InputSource::of(source);
    return binding;
}

/// @brief A key (physical or logical) with modifiers, e.g. `chord(KeyCode::Z, KeyModifier::CONTROL)` for
///        undo on any layout. KeyModifier::SHIFT, CONTROL, ALT and SUPER accept either side.
/// @param exact No other Shift, Control, Alt or Super may be held (Ctrl+S, not Ctrl+Shift+S).
template<class KeySource>
    requires std::same_as<KeySource, Key> || std::same_as<KeySource, KeyCode>
[[nodiscard]] constexpr auto
chord(KeySource key,
      KeyModifier modifiers,
      bool exact = true) noexcept -> Binding
{
    Binding binding;
    binding.kind = BindingKind::CHORD;
    binding.sources[0] = InputSource::of(key);
    binding.modifiers = modifiers;
    binding.exact_modifiers = exact;
    return binding;
}

/// @brief Two to four gamepad buttons held together.
template<class... Buttons>
    requires(sizeof...(Buttons) >= 2 && sizeof...(Buttons) <= MAXIMUM_BINDING_SOURCES && (std::same_as<Buttons, GamepadButton> && ...))
[[nodiscard]] constexpr auto
gamepad_chord(Buttons... buttons) noexcept -> Binding
{
    Binding binding;
    binding.kind = BindingKind::GAMEPAD_CHORD;
    std::size_t index = 0;
    ((binding.sources[index++] = InputSource::of(buttons)), ...);
    return binding;
}

/// @brief A one-dimensional axis from two digital inputs: @p positive minus @p negative.
template<DigitalInputSourceType Negative, DigitalInputSourceType Positive>
[[nodiscard]] constexpr auto
composite_axis(Negative negative,
               Positive positive) noexcept -> Binding
{
    Binding binding;
    binding.kind = BindingKind::COMPOSITE_AXIS;
    binding.sources[0] = InputSource::of(negative);
    binding.sources[1] = InputSource::of(positive);
    return binding;
}

/// @brief A two-dimensional value from four digital inputs (WASD, the d-pad): x = right - left,
///        y = up - down; diagonals are normalized to length 1.
template<DigitalInputSourceType Up, DigitalInputSourceType Down, DigitalInputSourceType Left, DigitalInputSourceType Right>
[[nodiscard]] constexpr auto
composite_2d(Up up,
             Down down,
             Left left,
             Right right) noexcept -> Binding
{
    Binding binding;
    binding.kind = BindingKind::COMPOSITE_2D;
    binding.sources[0] = InputSource::of(up);
    binding.sources[1] = InputSource::of(down);
    binding.sources[2] = InputSource::of(left);
    binding.sources[3] = InputSource::of(right);
    binding.normalize = true;
    return binding;
}

/// @brief A two-dimensional value from two gamepad axes, y flipped so that up is positive (as in composites).
[[nodiscard]] constexpr auto
stick(GamepadAxis x,
      GamepadAxis y) noexcept -> Binding
{
    Binding binding;
    binding.kind = BindingKind::STICK;
    binding.sources[0] = InputSource::of(x);
    binding.sources[1] = InputSource::of(y);
    return binding;
}

/// @brief A binding of a binding context: the action and how it is bound.
struct ContextBinding
{
    ActionIdentifier action{}; ///< The action.
    Binding binding{};         ///< What it reads.

    constexpr bool operator==(const ContextBinding&) const = default;
};

/// @brief Binds @p action to @p binding.
[[nodiscard]] constexpr auto
bind(ActionIdentifier action,
     const Binding& binding) noexcept -> ContextBinding
{
    return ContextBinding{.action = action, .binding = binding};
}

/// @brief Binds @p action to one input, e.g. `bind("jump"_action, Key::SPACE)`.
template<InputSourceType Source>
[[nodiscard]] constexpr auto
bind(ActionIdentifier action,
     Source source) noexcept -> ContextBinding
{
    return ContextBinding{.action = action, .binding = single(source)};
}

} // namespace gfx::platform
