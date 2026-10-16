// InputSystem: device state, context stacks, binding evaluation (shaping, triggers, consumption),
// rebinding, binding capture and the context activations.
module gfx.platform.input;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;
import gfx.platform.interface;

namespace gfx::platform
{

namespace
{

/// Throws InvalidUsageError.
[[noreturn]] void
throw_usage(std::string_view operation,
            std::string problem,
            std::string_view suggestion,
            std::source_location location)
{
    throw InvalidUsageError(
        {
            .backend = std::nullopt,
            .operation = std::string{operation},
            .problem = std::move(problem),
            .suggestion = std::string{suggestion},
        },
        location);
}

/// The modifier groups a chord tests: either side, or the side the chord names.
constexpr std::array MODIFIER_GROUPS{KeyModifier::SHIFT, KeyModifier::CONTROL, KeyModifier::ALT, KeyModifier::SUPER};

/// The lock modifiers, reported by keyboard events rather than derived from keys.
constexpr KeyModifier LOCK_MODIFIERS = KeyModifier::CAPS_LOCK | KeyModifier::NUM_LOCK | KeyModifier::SCROLL_LOCK;

/// Each modifier key and its bit.
constexpr std::array<std::pair<Key, KeyModifier>, 8> MODIFIER_KEYS{{
    {Key::LEFT_SHIFT, KeyModifier::LEFT_SHIFT},
    {Key::RIGHT_SHIFT, KeyModifier::RIGHT_SHIFT},
    {Key::LEFT_CONTROL, KeyModifier::LEFT_CONTROL},
    {Key::RIGHT_CONTROL, KeyModifier::RIGHT_CONTROL},
    {Key::LEFT_ALT, KeyModifier::LEFT_ALT},
    {Key::RIGHT_ALT, KeyModifier::RIGHT_ALT},
    {Key::LEFT_SUPER, KeyModifier::LEFT_SUPER},
    {Key::RIGHT_SUPER, KeyModifier::RIGHT_SUPER},
}};

/// Length of @p value.
[[nodiscard]] auto
length(Vector2 value) noexcept -> float
{
    return std::hypot(value.x, value.y);
}

/// @p value scaled by @p factor.
[[nodiscard]] auto
times(Vector2 value,
      float factor) noexcept -> Vector2
{
    return Vector2{.x = value.x * factor, .y = value.y * factor};
}

/// A component through an axial dead zone: 0 below @p lower, rescaled from @p lower to @p upper.
[[nodiscard]] auto
axial_deadzone(float component,
               float lower,
               float upper) noexcept -> float
{
    const float magnitude = std::abs(component);
    if (magnitude < lower)
    {
        return 0.0F;
    }
    return std::copysign(std::min(1.0F, (magnitude - lower) / (upper - lower)), component);
}

/// @p value through the modifiers of @p binding: dead zone, response curve, inversion, scale, swizzle,
/// normalization.
[[nodiscard]] auto
shape(Vector2 value,
      const Binding& binding) noexcept -> Vector2
{
    // Dead zone: per axis, or on the vector's length.
    switch (binding.deadzone)
    {
        case DeadzoneKind::NONE:
            break;
        case DeadzoneKind::AXIAL:
            value.x = axial_deadzone(value.x, binding.deadzone_lower, binding.deadzone_upper);
            value.y = axial_deadzone(value.y, binding.deadzone_lower, binding.deadzone_upper);
            break;
        case DeadzoneKind::RADIAL:
            if (length(value) < binding.deadzone_lower)
            {
                value = Vector2{};
            }
            break;
        case DeadzoneKind::SCALED_RADIAL:
        {
            const float magnitude = length(value);
            value = magnitude < binding.deadzone_lower
                ? Vector2{}
                : times(value, std::min(1.0F, (magnitude - binding.deadzone_lower) / (binding.deadzone_upper - binding.deadzone_lower))
                                   / magnitude);
            break;
        }
    }

    // Response curve on the length, keeping the direction.
    if (const float magnitude = length(value); magnitude > 0.0F && binding.curve != ResponseCurve::LINEAR)
    {
        float exponent = binding.curve_exponent;
        if (binding.curve == ResponseCurve::QUADRATIC)
        {
            exponent = 2.0F;
        }
        else if (binding.curve == ResponseCurve::CUBIC)
        {
            exponent = 3.0F;
        }
        value = times(value, std::pow(magnitude, exponent) / magnitude);
    }

    // Inversion, scale, swizzle, then normalization of long vectors.
    value.x = binding.invert_x ? -value.x : value.x;
    value.y = binding.invert_y ? -value.y : value.y;
    value = Vector2{.x = value.x * binding.scale.x, .y = value.y * binding.scale.y};
    if (binding.swizzle)
    {
        value = Vector2{.x = value.y, .y = value.x};
    }
    if (const float magnitude = length(value); binding.normalize && magnitude > 1.0F)
    {
        value = times(value, 1.0F / magnitude);
    }
    return value;
}

/// Whether @p held satisfies the modifiers @p required of a chord.
[[nodiscard]] auto
modifiers_match(KeyModifier required,
                KeyModifier held,
                bool exact) noexcept -> bool
{
    // Each group: either side when the chord names both, that side when it names one, none when exact.
    for (const KeyModifier group : MODIFIER_GROUPS)
    {
        const KeyModifier wanted = required & group;
        const KeyModifier present = held & group;
        if (is_empty(wanted))
        {
            if (exact && !is_empty(present))
            {
                return false;
            }
        }
        else if (is_empty(present & wanted))
        {
            return false;
        }
    }
    return true;
}

/// Bit of @p button in a MouseButtonMask.
[[nodiscard]] constexpr auto
mouse_bit(MouseButton button) noexcept -> MouseButtonMask
{
    return static_cast<MouseButtonMask>(1U << std::to_underlying(button));
}

/// Bit of @p button in GamepadState::buttons.
[[nodiscard]] constexpr auto
gamepad_bit(GamepadButton button) noexcept -> std::uint32_t
{
    return 1U << std::to_underlying(button);
}

/// Current steady-clock time.
[[nodiscard]] auto
steady_now() noexcept -> std::chrono::nanoseconds
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

} // namespace

// ================================================================================================= State

/// A default binding of a context.
struct DefaultBinding
{
    std::uint32_t action = 0;
    Binding binding{};
};

/// A binding context.
struct ContextRecord
{
    BindingContextIdentifier identifier{};
    std::string name{};
    ContextDescription description{};
    std::vector<DefaultBinding> defaults{};
};

/// A binding of a player, with its trigger state and what it ignores (ContextDescription::ignore_held_inputs).
struct PlayerBinding
{
    std::uint32_t action = 0;
    Binding binding{};
    bool actuated = false;
    std::chrono::nanoseconds actuation_start{};
    std::uint8_t readable_sources = 0; ///< Digital sources (bits by index) it could read in frame readable_frame.
    std::uint8_t ignored_sources = 0;  ///< Digital sources held when it began to read them: released until released.
    std::uint64_t readable_frame = 0;  ///< Frame readable_sources describes.
};

/// An activation of a context for a player.
struct Activation
{
    std::uint64_t identifier = 0;
    std::uint32_t context = 0;
};

/// What a player's action was the previous frame.
struct ActionMemory
{
    bool pressed = false;
    std::chrono::nanoseconds press_start{};
};

/// A player: bindings per context, active contexts, action memory, gamepad.
struct PlayerRecord
{
    std::vector<std::vector<PlayerBinding>> bindings{};
    std::vector<Activation> activations{};
    std::vector<std::uint32_t> order{};
    std::vector<ActionMemory> actions{};
    GamepadHandle gamepad{};
};

/// A gamepad seen in events.
struct GamepadRecord
{
    GamepadHandle gamepad{};
    std::uint32_t buttons = 0;
    std::uint32_t pressed = 0;
    std::array<float, ENUMERATOR_COUNT<GamepadAxis>> axes{};
};

/// The strongest binding of an action so far.
struct Candidate
{
    Vector2 value{};
    float magnitude = 0.0F;
    bool pressed = false;
};

/// A binding capture in progress.
struct Capture
{
    PlayerIdentifier player{};
    BindingCaptureSource sources = BindingCaptureSource::NONE;
    Key cancel_key = Key::ESCAPE;
};

/// What the bindings of one context of one player read.
struct DeviceView
{
    bool keyboard = false;
    bool mouse = false;
    const GamepadRecord* gamepad = nullptr;
    KeyModifier modifiers = KeyModifier::NONE;
    std::span<const InputSource> consumed{};
};

struct InputSystemState
{
    InputSystemDescription description{};
    std::shared_ptr<ActionTable> actions{};
    std::vector<ContextRecord> contexts{};
    std::vector<PlayerRecord> players{};
    std::uint32_t keyboard_player = 0;
    bool text_input_active = false;

    // Devices: what is down, and what was pressed during the frame (a press and release within one
    // frame still counts for that frame).
    std::bitset<InputFrame::KEY_SLOTS> keys_down{};
    std::bitset<InputFrame::KEY_SLOTS> keys_pressed{};
    std::array<KeyCode, InputFrame::KEY_SLOTS> key_codes{};
    KeyModifier lock_modifiers = KeyModifier::NONE;
    MouseButtonMask mouse_down = MouseButtonMask::NONE;
    MouseButtonMask mouse_pressed = MouseButtonMask::NONE;
    Vector2 mouse_position{};
    Vector2 mouse_motion{};
    Vector2 mouse_wheel{};
    std::vector<GamepadRecord> gamepads{};

    std::optional<Capture> capture{};
    std::optional<CapturedBinding> captured{};

    std::array<InputFrame, 2> frames{};
    std::size_t latest = 0;
    std::uint64_t frame_number = 0;
    std::uint64_t next_activation = 1;

    // Scratch memory of the evaluation, kept between frames.
    std::vector<Candidate> candidates{};
    std::vector<InputSource> consumed{};
    std::bitset<InputFrame::KEY_SLOTS> chord_keys{};

    // ------------------------------------------------------------------------------------------- lookups

    [[nodiscard]] auto find_context(BindingContextIdentifier context) const noexcept -> std::optional<std::uint32_t>
    {
        for (std::uint32_t index = 0; index < contexts.size(); ++index)
        {
            if (contexts[index].identifier == context)
            {
                return index;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto require_context(BindingContextIdentifier context,
                                       std::string_view operation,
                                       std::source_location location) const -> std::uint32_t
    {
        if (const std::optional<std::uint32_t> index = find_context(context))
        {
            return *index;
        }
        throw_usage(operation, std::format("{} is not declared by the input schema", context),
                    "declare the context in the schema, or add it with InputSystem::add_context()", location);
    }

    [[nodiscard]] auto require_action(ActionIdentifier action,
                                      std::string_view operation,
                                      std::source_location location) const -> std::uint32_t
    {
        if (const std::optional<std::uint32_t> index = actions->find(action))
        {
            return *index;
        }
        throw_usage(operation, std::format("{} is not declared by the input schema", action),
                    "declare the action in the schema (action())", location);
    }

    [[nodiscard]] auto require_player(PlayerIdentifier player,
                                      std::string_view operation,
                                      std::source_location location) -> PlayerRecord&
    {
        if (player.index() < players.size())
        {
            return players[player.index()];
        }
        throw_usage(operation, std::format("{} does not exist: the input system has {} player(s)", player, players.size()),
                    "raise InputSystemDescription::players", location);
    }

    [[nodiscard]] auto require_player(PlayerIdentifier player,
                                      std::string_view operation,
                                      std::source_location location) const -> const PlayerRecord&
    {
        return const_cast<InputSystemState*>(this)->require_player(player, operation, location);
    }

    /// Position in @p list of the binding in @p slot of @p action.
    [[nodiscard]] static auto find_slot(const std::vector<PlayerBinding>& list,
                                        std::uint32_t action,
                                        std::size_t slot) noexcept -> std::optional<std::size_t>
    {
        std::size_t seen = 0;
        for (std::size_t position = 0; position < list.size(); ++position)
        {
            if (list[position].action == action && seen++ == slot)
            {
                return position;
            }
        }
        return std::nullopt;
    }

    /// Checks @p binding for @p action, naming both in the error.
    void require_valid_binding(const Binding& binding,
                               std::uint32_t action,
                               std::uint32_t context,
                               std::string_view operation,
                               std::source_location location) const
    {
        const ActionRecord& record = actions->actions[action];
        if (const InputSchemaProblem problem = check_binding(binding, record.type); problem != InputSchemaProblem::NONE)
        {
            throw_usage(operation,
                        std::format("the binding of action '{}' ({}) in binding context '{}' is invalid: {} ({})", record.name,
                                    record.type, contexts[context].name, describe_input_schema_problem(problem), problem),
                        "build bindings with the binding builders, matching the action's value type", location);
        }
    }

    // ------------------------------------------------------------------------------------------- contexts

    /// Recomputes the evaluation order of @p player: decreasing priority, the latest activation first.
    void order_contexts(PlayerRecord& player) const
    {
        // The latest activation of each active context.
        std::vector<std::pair<std::uint32_t, std::uint64_t>> latest_activation;
        for (const Activation& activation : player.activations)
        {
            const auto found = std::ranges::find(latest_activation, activation.context, &std::pair<std::uint32_t, std::uint64_t>::first);
            if (found == latest_activation.end())
            {
                latest_activation.emplace_back(activation.context, activation.identifier);
            }
            else
            {
                found->second = std::max(found->second, activation.identifier);
            }
        }

        // Sort and keep the contexts.
        std::ranges::sort(latest_activation, [this](const auto& left, const auto& right)
        {
            const std::int32_t left_priority = contexts[left.first].description.priority;
            const std::int32_t right_priority = contexts[right.first].description.priority;
            return left_priority != right_priority ? left_priority > right_priority : left.second > right.second;
        });
        player.order.clear();
        for (const auto& [context, identifier] : latest_activation)
        {
            player.order.push_back(context);
        }
    }

    /// Forgets the trigger state of @p player's bindings in @p context (it stopped being evaluated).
    static void release_bindings(PlayerRecord& player,
                                 std::uint32_t context) noexcept
    {
        for (PlayerBinding& binding : player.bindings[context])
        {
            binding.actuated = false;
        }
    }

    // ------------------------------------------------------------------------------------------- devices

    /// The record of @p gamepad, created (and assigned automatically) when first seen.
    auto gamepad_record(GamepadHandle gamepad) -> GamepadRecord&
    {
        const auto found = std::ranges::find(gamepads, gamepad, &GamepadRecord::gamepad);
        if (found != gamepads.end())
        {
            return *found;
        }
        gamepads.push_back(GamepadRecord{.gamepad = gamepad});

        // Automatic assignment: the first player without a gamepad, unless one already has this one.
        if (description.gamepad_assignment == GamepadAssignment::AUTOMATIC
            && std::ranges::find(players, gamepad, &PlayerRecord::gamepad) == players.end())
        {
            const auto free = std::ranges::find_if(players, [](const PlayerRecord& player) { return !player.gamepad.is_valid(); });
            if (free != players.end())
            {
                free->gamepad = gamepad;
            }
        }
        return gamepads.back();
    }

    [[nodiscard]] auto find_gamepad(GamepadHandle gamepad) const noexcept -> const GamepadRecord*
    {
        if (!gamepad.is_valid())
        {
            return nullptr;
        }
        const auto found = std::ranges::find(gamepads, gamepad, &GamepadRecord::gamepad);
        return found != gamepads.end() ? &*found : nullptr;
    }

    /// Whether a capture for @p sources is waiting for an input of @p player_index.
    [[nodiscard]] auto capturing(BindingCaptureSource sources,
                                 std::uint32_t player_index) const noexcept -> bool
    {
        return capture && has_any(capture->sources, sources) && capture->player.index() == player_index;
    }

    /// Player @p gamepad is routed to, if any.
    [[nodiscard]] auto player_of(GamepadHandle gamepad) const noexcept -> std::optional<std::uint32_t>
    {
        for (std::uint32_t index = 0; index < players.size(); ++index)
        {
            if (players[index].gamepad == gamepad)
            {
                return index;
            }
        }
        return std::nullopt;
    }

    /// Ends the capture with @p binding (or its cancellation).
    void finish_capture(const Binding& binding,
                        bool canceled) noexcept
    {
        captured = CapturedBinding{.player = capture->player, .binding = binding, .canceled = canceled};
        capture.reset();
    }

    /// Applies one event to the device state (or completes a capture with it).
    void apply(const Event& event)
    {
        switch (event.type())
        {
            case EventType::KEY_DOWN:
            {
                const KeyboardEvent& key = *event.get_if<KeyboardEvent>();
                lock_modifiers = key.modifiers & LOCK_MODIFIERS;
                const auto slot = static_cast<std::size_t>(std::to_underlying(key.key));
                if (key.repeat || key.key == Key::UNKNOWN || slot >= InputFrame::KEY_SLOTS)
                {
                    break;
                }
                if (capturing(BindingCaptureSource::KEYBOARD, keyboard_player))
                {
                    finish_capture(key.key == capture->cancel_key ? Binding{} : single(key.key), key.key == capture->cancel_key);
                    break;
                }
                keys_down.set(slot);
                keys_pressed.set(slot);
                key_codes[slot] = key.key_code;
                break;
            }
            case EventType::KEY_UP:
            {
                const KeyboardEvent& key = *event.get_if<KeyboardEvent>();
                lock_modifiers = key.modifiers & LOCK_MODIFIERS;
                if (const auto slot = static_cast<std::size_t>(std::to_underlying(key.key)); slot < InputFrame::KEY_SLOTS)
                {
                    keys_down.reset(slot);
                }
                break;
            }
            case EventType::MOUSE_MOTION:
            {
                const MouseMotionEvent& motion = *event.get_if<MouseMotionEvent>();
                mouse_position = motion.position;
                mouse_motion = Vector2{.x = mouse_motion.x + motion.delta.x, .y = mouse_motion.y + motion.delta.y};
                break;
            }
            case EventType::MOUSE_BUTTON_DOWN:
            {
                const MouseButtonEvent& button = *event.get_if<MouseButtonEvent>();
                mouse_position = button.position;
                if (capturing(BindingCaptureSource::MOUSE_BUTTON, keyboard_player))
                {
                    finish_capture(single(button.button), false);
                    break;
                }
                mouse_down |= mouse_bit(button.button);
                mouse_pressed |= mouse_bit(button.button);
                break;
            }
            case EventType::MOUSE_BUTTON_UP:
            {
                const MouseButtonEvent& button = *event.get_if<MouseButtonEvent>();
                mouse_position = button.position;
                mouse_down &= ~mouse_bit(button.button);
                break;
            }
            case EventType::MOUSE_WHEEL:
            {
                const MouseWheelEvent& wheel = *event.get_if<MouseWheelEvent>();
                mouse_wheel = Vector2{.x = mouse_wheel.x + wheel.delta.x, .y = mouse_wheel.y + wheel.delta.y};
                break;
            }
            case EventType::WINDOW_FOCUS_LOST:
                // Keys and buttons released elsewhere never report it: release them now.
                keys_down.reset();
                mouse_down = MouseButtonMask::NONE;
                break;
            case EventType::GAMEPAD_CONNECTED:
                static_cast<void>(gamepad_record(event.get_if<GamepadDeviceEvent>()->gamepad));
                break;
            case EventType::GAMEPAD_DISCONNECTED:
            {
                const GamepadHandle gamepad = event.get_if<GamepadDeviceEvent>()->gamepad;
                std::erase_if(gamepads, [gamepad](const GamepadRecord& record) { return record.gamepad == gamepad; });
                for (PlayerRecord& player : players)
                {
                    if (player.gamepad == gamepad)
                    {
                        player.gamepad = GamepadHandle{};
                    }
                }
                break;
            }
            case EventType::GAMEPAD_BUTTON_DOWN:
            {
                const GamepadButtonEvent& button = *event.get_if<GamepadButtonEvent>();
                GamepadRecord& record = gamepad_record(button.gamepad);
                const std::optional<std::uint32_t> player = player_of(button.gamepad);
                if (player && capturing(BindingCaptureSource::GAMEPAD_BUTTON, *player))
                {
                    finish_capture(single(button.button), false);
                    break;
                }
                record.buttons |= gamepad_bit(button.button);
                record.pressed |= gamepad_bit(button.button);
                break;
            }
            case EventType::GAMEPAD_BUTTON_UP:
            {
                const GamepadButtonEvent& button = *event.get_if<GamepadButtonEvent>();
                gamepad_record(button.gamepad).buttons &= ~gamepad_bit(button.button);
                break;
            }
            case EventType::GAMEPAD_AXIS_MOTION:
            {
                const GamepadAxisEvent& axis = *event.get_if<GamepadAxisEvent>();
                gamepad_record(axis.gamepad).axes[std::to_underlying(axis.axis)] = axis.value;
                const std::optional<std::uint32_t> player = player_of(axis.gamepad);
                if (player && capturing(BindingCaptureSource::GAMEPAD_AXIS, *player)
                    && std::abs(axis.value) >= description.actuation_threshold)
                {
                    finish_capture(single(axis.axis), false);
                }
                break;
            }
            default:
                break;
        }
    }

    // ------------------------------------------------------------------------------------------- evaluation

    /// Shift, Control, Alt and Super keys down this frame, with the lock state of the last key event.
    [[nodiscard]] auto held_modifiers() const noexcept -> KeyModifier
    {
        const std::bitset<InputFrame::KEY_SLOTS> keys = keys_down | keys_pressed;
        KeyModifier held = lock_modifiers;
        for (const auto& [key, modifier] : MODIFIER_KEYS)
        {
            if (keys.test(std::to_underlying(key)))
            {
                held |= modifier;
            }
        }
        return held;
    }

    /// Whether @p source is consumed by a higher-priority context.
    [[nodiscard]] static auto is_consumed(InputSource source,
                                          const DeviceView& view) noexcept -> bool
    {
        return std::ranges::find(view.consumed, source) != view.consumed.end();
    }

    /// Whether digital source @p source is held since an earlier frame (down, and not pressed during this one).
    [[nodiscard]] auto held_since_earlier_frame(InputSource source,
                                                const DeviceView& view) const noexcept -> bool
    {
        const std::bitset<InputFrame::KEY_SLOTS> held_keys = keys_down & ~keys_pressed;
        switch (source.kind)
        {
            case InputSourceKind::KEY:
                return source.code < InputFrame::KEY_SLOTS && held_keys.test(source.code);
            case InputSourceKind::KEY_CODE:
                for (std::size_t slot = 0; slot < InputFrame::KEY_SLOTS; ++slot)
                {
                    if (held_keys.test(slot) && std::to_underlying(key_codes[slot]) == source.code)
                    {
                        return true;
                    }
                }
                return false;
            case InputSourceKind::MOUSE_BUTTON:
            {
                const MouseButtonMask bit = mouse_bit(static_cast<MouseButton>(source.code));
                return has_any(mouse_down, bit) && !has_any(mouse_pressed, bit);
            }
            case InputSourceKind::GAMEPAD_BUTTON:
            {
                const std::uint32_t bit = gamepad_bit(static_cast<GamepadButton>(source.code));
                return view.gamepad != nullptr && (view.gamepad->buttons & bit) != 0 && (view.gamepad->pressed & bit) == 0;
            }
            case InputSourceKind::NONE:
            case InputSourceKind::MOUSE_AXIS:
            case InputSourceKind::GAMEPAD_AXIS:
                return false;
        }
        return false;
    }

    /// Whether a binding seeing @p view can read digital source @p source (analog sources: never ignored).
    [[nodiscard]] static auto can_read_digital(InputSource source,
                                               const DeviceView& view) noexcept -> bool
    {
        if (is_consumed(source, view))
        {
            return false;
        }
        switch (source.kind)
        {
            case InputSourceKind::KEY:
            case InputSourceKind::KEY_CODE:
                return view.keyboard;
            case InputSourceKind::MOUSE_BUTTON:
                return view.mouse;
            case InputSourceKind::GAMEPAD_BUTTON:
                return view.gamepad != nullptr;
            case InputSourceKind::NONE:
            case InputSourceKind::MOUSE_AXIS:
            case InputSourceKind::GAMEPAD_AXIS:
                return false;
        }
        return false;
    }

    /// The sources of @p entry to read as released (ContextDescription::ignore_held_inputs): those it starts
    /// to read this frame while they are held since an earlier frame, until they are released.
    auto update_ignored_sources(PlayerBinding& entry,
                                const DeviceView& view,
                                bool blocked) const noexcept -> std::uint8_t
    {
        // What it could read the previous frame (nothing when its context was not evaluated then).
        const std::uint8_t previously_readable = entry.readable_frame + 1 == frame_number ? entry.readable_sources : std::uint8_t{0};
        std::uint8_t readable = 0;
        std::uint8_t held = 0;
        for (std::size_t index = 0; index < entry.binding.source_count(); ++index)
        {
            const InputSource source = entry.binding.sources[index];
            const auto bit = static_cast<std::uint8_t>(1U << index);
            if (!blocked && can_read_digital(source, view))
            {
                readable |= bit;
            }
            if (held_since_earlier_frame(source, view))
            {
                held |= bit;
            }
        }

        // Newly readable held sources join the ignored ones; a release ends the ignoring.
        entry.ignored_sources = static_cast<std::uint8_t>((entry.ignored_sources | (readable & ~previously_readable)) & held);
        entry.readable_sources = readable;
        entry.readable_frame = frame_number;
        return entry.ignored_sources;
    }

    /// Whether key @p slot (or one producing key code @p source) is down for @p view.
    [[nodiscard]] auto key_down(InputSource source,
                                const DeviceView& view,
                                bool include_chord_keys) const noexcept -> bool
    {
        const std::bitset<InputFrame::KEY_SLOTS> keys = (keys_down | keys_pressed) & (include_chord_keys ? ~std::bitset<InputFrame::KEY_SLOTS>{} : ~chord_keys);
        if (!view.keyboard)
        {
            return false;
        }
        if (source.kind == InputSourceKind::KEY)
        {
            return source.code < InputFrame::KEY_SLOTS && keys.test(source.code);
        }
        for (std::size_t slot = 0; slot < InputFrame::KEY_SLOTS; ++slot)
        {
            if (keys.test(slot) && std::to_underlying(key_codes[slot]) == source.code)
            {
                return true;
            }
        }
        return false;
    }

    /// Value of one source for @p view: 0 or 1 for digital sources, the axis value for axes.
    [[nodiscard]] auto read_source(InputSource source,
                                   const DeviceView& view) const noexcept -> float
    {
        if (is_consumed(source, view))
        {
            return 0.0F;
        }
        switch (source.kind)
        {
            case InputSourceKind::NONE:
                return 0.0F;
            case InputSourceKind::KEY:
            case InputSourceKind::KEY_CODE:
                return key_down(source, view, false) ? 1.0F : 0.0F;
            case InputSourceKind::MOUSE_BUTTON:
                return view.mouse && has_any(mouse_down | mouse_pressed, mouse_bit(static_cast<MouseButton>(source.code))) ? 1.0F : 0.0F;
            case InputSourceKind::MOUSE_AXIS:
                if (!view.mouse)
                {
                    return 0.0F;
                }
                switch (static_cast<MouseAxis>(source.code))
                {
                    case MouseAxis::MOTION_X:
                        return mouse_motion.x;
                    case MouseAxis::MOTION_Y:
                        return mouse_motion.y;
                    case MouseAxis::WHEEL_X:
                        return mouse_wheel.x;
                    case MouseAxis::WHEEL_Y:
                        return mouse_wheel.y;
                }
                return 0.0F;
            case InputSourceKind::GAMEPAD_BUTTON:
                return view.gamepad != nullptr
                           && ((view.gamepad->buttons | view.gamepad->pressed) & gamepad_bit(static_cast<GamepadButton>(source.code))) != 0
                    ? 1.0F
                    : 0.0F;
            case InputSourceKind::GAMEPAD_AXIS:
                return view.gamepad != nullptr ? view.gamepad->axes[source.code] : 0.0F;
        }
        return 0.0F;
    }

    /// Whether the chord @p binding is held (its key and modifiers), ignoring consumption by chords.
    [[nodiscard]] auto chord_held(const Binding& binding,
                                  const DeviceView& view) const noexcept -> bool
    {
        return !is_consumed(binding.sources[0], view) && key_down(binding.sources[0], view, true)
               && modifiers_match(binding.modifiers, view.modifiers, binding.exact_modifiers);
    }

    /// Raw value of @p binding for @p view, before the modifiers; the sources in @p ignored (bits by index)
    ///        read as released.
    [[nodiscard]] auto read_binding(const Binding& binding,
                                    const DeviceView& view,
                                    std::uint8_t ignored = 0) const noexcept -> Vector2
    {
        const auto source = [&](std::size_t index)
        {
            return ((ignored >> index) & 1U) != 0 ? 0.0F : read_source(binding.sources[index], view);
        };
        switch (binding.kind)
        {
            case BindingKind::NONE:
                return {};
            case BindingKind::BUTTON:
            case BindingKind::MOUSE_AXIS:
                return Vector2{.x = source(0)};
            case BindingKind::CHORD:
                return Vector2{.x = (ignored & 1U) == 0 && chord_held(binding, view) ? 1.0F : 0.0F};
            case BindingKind::GAMEPAD_CHORD:
            {
                bool held = true;
                for (std::size_t index = 0; index < binding.source_count(); ++index)
                {
                    held = held && source(index) > 0.0F;
                }
                return Vector2{.x = held ? 1.0F : 0.0F};
            }
            case BindingKind::COMPOSITE_AXIS:
                return Vector2{.x = source(1) - source(0)};
            case BindingKind::COMPOSITE_2D:
                return Vector2{.x = source(3) - source(2), .y = source(0) - source(1)};
            case BindingKind::STICK:
                return Vector2{.x = source(0), .y = -source(1)};
        }
        return {};
    }

    /// What @p context of player @p player_index reads.
    [[nodiscard]] auto device_view(std::uint32_t player_index,
                                   const ContextRecord& context) const noexcept -> DeviceView
    {
        const bool owns_keyboard = player_index == keyboard_player;
        return DeviceView{
            .keyboard = owns_keyboard && (!text_input_active || context.description.reads_keyboard_during_text_input),
            .mouse = owns_keyboard,
            .gamepad = find_gamepad(players[player_index].gamepad),
            .modifiers = held_modifiers(),
            .consumed = consumed,
        };
    }

    /// Evaluates every action of player @p player_index at time @p now into @p states.
    void evaluate(std::uint32_t player_index,
                  std::chrono::nanoseconds now,
                  std::span<ActionState> states)
    {
        PlayerRecord& player = players[player_index];

        // Keys of held chords do not press single-key bindings (Ctrl+S does not also press S).
        consumed.clear();
        chord_keys.reset();
        for (const std::uint32_t context : player.order)
        {
            const DeviceView view = device_view(player_index, contexts[context]);
            for (const PlayerBinding& entry : player.bindings[context])
            {
                if (entry.binding.kind != BindingKind::CHORD || !chord_held(entry.binding, view))
                {
                    continue;
                }
                for (std::size_t slot = 0; slot < InputFrame::KEY_SLOTS; ++slot)
                {
                    const InputSource key = entry.binding.sources[0];
                    const bool same_key = key.kind == InputSourceKind::KEY ? key.code == slot : std::to_underlying(key_codes[slot]) == key.code;
                    if (same_key && (keys_down | keys_pressed).test(slot))
                    {
                        chord_keys.set(slot);
                    }
                }
            }
            if (contexts[context].description.blocks_lower_contexts)
            {
                break;
            }
        }

        // Contexts by priority: each binding offers a value; consumed sources and blocked contexts read nothing.
        candidates.assign(actions->actions.size(), Candidate{});
        bool blocked = false;
        for (const std::uint32_t context : player.order)
        {
            const ContextRecord& record = contexts[context];
            const DeviceView view = device_view(player_index, record);
            for (PlayerBinding& entry : player.bindings[context])
            {
                const std::uint8_t ignored = record.description.ignore_held_inputs ? update_ignored_sources(entry, view, blocked) : std::uint8_t{0};
                const Vector2 value = blocked ? Vector2{} : shape(read_binding(entry.binding, view, ignored), entry.binding);
                offer(entry, value, now);
            }

            // Sources bound here are hidden from lower contexts; a blocking context hides everything.
            if (record.description.consumes_input)
            {
                for (const PlayerBinding& entry : player.bindings[context])
                {
                    for (std::size_t index = 0; index < entry.binding.source_count(); ++index)
                    {
                        consumed.push_back(entry.binding.sources[index]);
                    }
                }
            }
            blocked = blocked || record.description.blocks_lower_contexts;
        }

        // Actions: pressed, values and edges against the previous frame.
        for (std::size_t index = 0; index < candidates.size(); ++index)
        {
            const ActionValueType type = actions->actions[index].type;
            const Candidate& candidate = candidates[index];
            ActionMemory& memory = player.actions[index];
            ActionState& state = states[index];
            const bool pressed = type == ActionValueType::BUTTON ? candidate.pressed : candidate.magnitude > 0.0F;
            state.value = type == ActionValueType::BUTTON ? Vector2{.x = candidate.value.x} : candidate.value;
            state.pressed = pressed;
            state.just_pressed = pressed && !memory.pressed;
            state.just_released = !pressed && memory.pressed;
            if (state.just_pressed)
            {
                memory.press_start = now;
            }
            state.pressed_duration = pressed ? now - memory.press_start : std::chrono::nanoseconds::zero();
            memory.pressed = pressed;
        }
    }

    /// Runs the trigger of @p entry on @p value and offers the result to its action.
    void offer(PlayerBinding& entry,
               Vector2 value,
               std::chrono::nanoseconds now) noexcept
    {
        // Actuation: buttons from the threshold, axes from any value.
        const ActionValueType type = actions->actions[entry.action].type;
        const float magnitude = length(value);
        const bool actuated = type == ActionValueType::BUTTON ? magnitude >= description.actuation_threshold : magnitude > 0.0F;
        if (actuated && !entry.actuated)
        {
            entry.actuation_start = now;
        }

        // Triggers: while actuated, after a hold, or once on a short release.
        bool pressed = false;
        switch (entry.binding.trigger)
        {
            case TriggerKind::DOWN:
                pressed = actuated;
                break;
            case TriggerKind::HOLD:
                pressed = actuated && now - entry.actuation_start >= entry.binding.trigger_time;
                break;
            case TriggerKind::TAP:
                pressed = !actuated && entry.actuated && now - entry.actuation_start <= entry.binding.trigger_time;
                break;
        }
        entry.actuated = actuated;

        // The strongest binding wins.
        Candidate& candidate = candidates[entry.action];
        if (type == ActionValueType::BUTTON)
        {
            if (pressed)
            {
                candidate.pressed = true;
                candidate.value.x = std::max(candidate.value.x, entry.binding.trigger == TriggerKind::TAP ? 1.0F : magnitude);
            }
        }
        else if (magnitude > candidate.magnitude)
        {
            candidate.value = value;
            candidate.magnitude = magnitude;
        }
    }

    /// Evaluates every player into the older frame, with the device state, and makes it the latest.
    auto publish(std::chrono::nanoseconds now) -> const InputFrame&
    {
        // Device state of the frame.
        InputFrame& frame = frames[1 - latest];
        frame.m_actions = actions;
        frame.m_player_count = static_cast<std::uint32_t>(players.size());
        frame.m_number = frame_number;
        frame.m_time = now;
        frame.m_keys = keys_down | keys_pressed;
        frame.m_key_codes = key_codes;
        frame.m_modifiers = held_modifiers();
        frame.m_mouse_buttons = mouse_down | mouse_pressed;
        frame.m_mouse_position = mouse_position;
        frame.m_mouse_motion = mouse_motion;
        frame.m_mouse_wheel = mouse_wheel;
        frame.m_text_input_active = text_input_active;
        frame.m_captured_binding = captured;

        // Actions and gamepads of each player.
        const std::size_t action_count = actions->actions.size();
        frame.m_states.resize(players.size() * action_count);
        for (std::uint32_t index = 0; index < players.size(); ++index)
        {
            evaluate(index, now, std::span{frame.m_states}.subspan(index * action_count, action_count));
            GamepadState gamepad{.gamepad = players[index].gamepad};
            if (const GamepadRecord* record = find_gamepad(players[index].gamepad))
            {
                gamepad.buttons = record->buttons | record->pressed;
                gamepad.axes = record->axes;
            }
            frame.m_gamepads[index] = gamepad;
        }
        latest = 1 - latest;
        return frames[latest];
    }

    /// Restores the default bindings of every context for @p player.
    void reset_player(PlayerRecord& player) const
    {
        player.bindings.resize(contexts.size());
        for (std::size_t context = 0; context < contexts.size(); ++context)
        {
            player.bindings[context].clear();
            for (const DefaultBinding& binding : contexts[context].defaults)
            {
                player.bindings[context].push_back(PlayerBinding{.action = binding.action, .binding = binding.binding});
            }
        }
    }
};

// ================================================================================================= ContextActivation

ContextActivation::ContextActivation(std::weak_ptr<InputSystem* const> system,
                                     std::uint64_t identifier,
                                     BindingContextIdentifier context,
                                     PlayerIdentifier player) noexcept
    : m_system{std::move(system)}
    , m_identifier{identifier}
    , m_context{context}
    , m_player{player}
{
}

ContextActivation::~ContextActivation()
{
    reset();
}

ContextActivation::ContextActivation(ContextActivation&& other) noexcept
    : m_system{std::move(other.m_system)}
    , m_identifier{std::exchange(other.m_identifier, 0)}
    , m_context{other.m_context}
    , m_player{other.m_player}
{
}

auto
ContextActivation::operator=(ContextActivation&& other) noexcept -> ContextActivation&
{
    if (this != &other)
    {
        reset();
        m_system = std::move(other.m_system);
        m_identifier = std::exchange(other.m_identifier, 0);
        m_context = other.m_context;
        m_player = other.m_player;
    }
    return *this;
}

auto
ContextActivation::is_active() const noexcept -> bool
{
    return m_identifier != 0 && !m_system.expired();
}

void
ContextActivation::reset() noexcept
{
    if (m_identifier == 0)
    {
        return;
    }
    if (const std::shared_ptr<InputSystem* const> system = m_system.lock())
    {
        (*system)->deactivate(m_identifier);
    }
    m_identifier = 0;
    m_system.reset();
}

// ================================================================================================= InputSystem

InputSystem::InputSystem(InputSchemaView schema,
                         const InputSystemDescription& description,
                         std::source_location location)
    : m_state{std::make_unique<InputSystemState>()}
    , m_lifetime{std::make_shared<InputSystem* const>(this)}
{
    // The schema and the description are valid.
    validate_input_schema(schema, location);
    if (description.players == 0 || description.players > MAXIMUM_PLAYERS)
    {
        throw_usage("InputSystem::InputSystem", std::format("{} players requested, the input system supports 1 to {}", description.players, MAXIMUM_PLAYERS),
                    "set InputSystemDescription::players within that range", location);
    }
    if (!(description.actuation_threshold > 0.0F && description.actuation_threshold <= 1.0F))
    {
        throw_usage("InputSystem::InputSystem", std::format("the actuation threshold {} is outside (0, 1]", description.actuation_threshold),
                    "keep DEFAULT_ACTUATION_THRESHOLD or pick a value such as 0.3", location);
    }
    InputSystemState& state = *m_state;
    state.description = description;

    // Actions, with an index sorted by identifier.
    auto table = std::make_shared<ActionTable>();
    for (const ActionDefinition& action : schema.actions)
    {
        table->by_identifier.emplace_back(action.identifier(), static_cast<std::uint32_t>(table->actions.size()));
        table->actions.push_back(ActionRecord{.identifier = action.identifier(), .name = std::string{action.name}, .type = action.type});
    }
    std::ranges::sort(table->by_identifier, {}, &std::pair<ActionIdentifier, std::uint32_t>::first);
    state.actions = table;

    // Contexts and their default bindings.
    for (const ContextDefinition& context : schema.contexts)
    {
        state.contexts.push_back(ContextRecord{.identifier = context.identifier(), .name = std::string{context.name}, .description = context.description});
    }
    for (const SchemaBinding& binding : schema.bindings)
    {
        const std::uint32_t context = *state.find_context(binding.context);
        state.contexts[context].defaults.push_back(DefaultBinding{.action = *table->find(binding.action), .binding = binding.binding});
    }

    // Players start with the defaults; frames start with every action released.
    state.players.resize(description.players);
    for (PlayerRecord& player : state.players)
    {
        state.reset_player(player);
        player.actions.resize(table->actions.size());
    }
    for (InputFrame& frame : state.frames)
    {
        frame.m_actions = table;
        frame.m_player_count = description.players;
        frame.m_states.resize(static_cast<std::size_t>(description.players) * table->actions.size());
    }
}

InputSystem::~InputSystem() = default;

auto
InputSystem::update(const EventSpan& events) -> const InputFrame&
{
    return update(events, steady_now());
}

auto
InputSystem::update(const EventSpan& events,
                    std::chrono::nanoseconds now) -> const InputFrame&
{
    // Per-frame accumulators start over.
    InputSystemState& state = *m_state;
    ++state.frame_number;
    state.keys_pressed.reset();
    state.mouse_pressed = MouseButtonMask::NONE;
    state.mouse_motion = Vector2{};
    state.mouse_wheel = Vector2{};
    state.captured.reset();
    for (GamepadRecord& gamepad : state.gamepads)
    {
        gamepad.pressed = 0;
    }

    // Events in order, then every player's actions.
    for (const Event& event : events)
    {
        state.apply(event);
    }
    return state.publish(now);
}

auto
InputSystem::frame() const noexcept -> const InputFrame&
{
    return m_state->frames[m_state->latest];
}

auto
InputSystem::activate_context(BindingContextIdentifier context,
                              PlayerIdentifier player,
                              std::source_location location) -> ContextActivation
{
    // Push the context on the player's stack.
    InputSystemState& state = *m_state;
    const std::uint32_t index = state.require_context(context, "InputSystem::activate_context", location);
    PlayerRecord& record = state.require_player(player, "InputSystem::activate_context", location);
    const std::uint64_t identifier = state.next_activation++;
    record.activations.push_back(Activation{.identifier = identifier, .context = index});
    state.order_contexts(record);
    return ContextActivation{m_lifetime, identifier, context, player};
}

void
InputSystem::deactivate(std::uint64_t identifier) noexcept
{
    // Pop the activation; the context stops being evaluated when it was its last one.
    InputSystemState& state = *m_state;
    for (PlayerRecord& player : state.players)
    {
        const auto found = std::ranges::find(player.activations, identifier, &Activation::identifier);
        if (found == player.activations.end())
        {
            continue;
        }
        const std::uint32_t context = found->context;
        player.activations.erase(found);
        if (std::ranges::find(player.activations, context, &Activation::context) == player.activations.end())
        {
            InputSystemState::release_bindings(player, context);
        }
        try
        {
            state.order_contexts(player);
        }
        catch (...)
        {
            // Ordering allocates; without memory the order keeps the context out by rebuilding naively.
            std::erase(player.order, context);
        }
        return;
    }
}

auto
InputSystem::is_context_active(BindingContextIdentifier context,
                               PlayerIdentifier player) const noexcept -> bool
{
    const std::optional<std::uint32_t> index = m_state->find_context(context);
    return index && player.index() < m_state->players.size()
           && std::ranges::find(m_state->players[player.index()].order, *index) != m_state->players[player.index()].order.end();
}

auto
InputSystem::active_contexts(PlayerIdentifier player,
                             std::source_location location) const -> std::vector<BindingContextIdentifier>
{
    const PlayerRecord& record = m_state->require_player(player, "InputSystem::active_contexts", location);
    std::vector<BindingContextIdentifier> contexts;
    for (const std::uint32_t context : record.order)
    {
        contexts.push_back(m_state->contexts[context].identifier);
    }
    return contexts;
}

void
InputSystem::add_context(const ContextDefinition& context,
                         std::span<const ContextBinding> bindings,
                         std::source_location location)
{
    // The name is free and every binding suits its action.
    InputSystemState& state = *m_state;
    constexpr std::string_view OPERATION = "InputSystem::add_context";
    if (context.name.empty())
    {
        throw_usage(OPERATION, "the binding context has an empty name", "name the context", location);
    }
    if (state.find_context(context.identifier()))
    {
        throw_usage(OPERATION, std::format("a binding context named '{}' (or with the same identifier) already exists", context.name),
                    "pick another name", location);
    }
    ContextRecord record{.identifier = context.identifier(), .name = std::string{context.name}, .description = context.description};
    for (const ContextBinding& binding : bindings)
    {
        const std::uint32_t action = state.require_action(binding.action, OPERATION, location);
        const InputSchemaProblem problem = check_binding(binding.binding, state.actions->actions[action].type);
        if (problem != InputSchemaProblem::NONE)
        {
            throw_usage(OPERATION,
                        std::format("the binding of action '{}' in binding context '{}' is invalid: {} ({})", state.actions->actions[action].name,
                                    context.name, describe_input_schema_problem(problem), problem),
                        "build bindings with the binding builders, matching the action's value type", location);
        }
        record.defaults.push_back(DefaultBinding{.action = action, .binding = binding.binding});
    }

    // Every player starts with the defaults.
    state.contexts.push_back(std::move(record));
    for (PlayerRecord& player : state.players)
    {
        std::vector<PlayerBinding>& list = player.bindings.emplace_back();
        for (const DefaultBinding& binding : state.contexts.back().defaults)
        {
            list.push_back(PlayerBinding{.action = binding.action, .binding = binding.binding});
        }
    }
}

auto
InputSystem::player_count() const noexcept -> std::uint32_t
{
    return static_cast<std::uint32_t>(m_state->players.size());
}

void
InputSystem::assign_keyboard_and_mouse(PlayerIdentifier player,
                                       std::source_location location)
{
    static_cast<void>(m_state->require_player(player, "InputSystem::assign_keyboard_and_mouse", location));
    m_state->keyboard_player = player.index();
}

auto
InputSystem::keyboard_and_mouse_player() const noexcept -> PlayerIdentifier
{
    return PlayerIdentifier{m_state->keyboard_player};
}

void
InputSystem::assign_gamepad(GamepadHandle gamepad,
                            PlayerIdentifier player,
                            std::source_location location)
{
    // One gamepad per player, one player per gamepad.
    if (!gamepad.is_valid())
    {
        throw_usage("InputSystem::assign_gamepad", "the gamepad handle is null", "use unassign_gamepad() to remove a gamepad", location);
    }
    PlayerRecord& record = m_state->require_player(player, "InputSystem::assign_gamepad", location);
    unassign_gamepad(gamepad);
    record.gamepad = gamepad;
}

void
InputSystem::unassign_gamepad(GamepadHandle gamepad) noexcept
{
    for (PlayerRecord& player : m_state->players)
    {
        if (player.gamepad == gamepad)
        {
            player.gamepad = GamepadHandle{};
        }
    }
}

auto
InputSystem::gamepad_of(PlayerIdentifier player,
                        std::source_location location) const -> GamepadHandle
{
    return m_state->require_player(player, "InputSystem::gamepad_of", location).gamepad;
}

void
InputSystem::set_text_input_active(bool active) noexcept
{
    m_state->text_input_active = active;
}

auto
InputSystem::is_text_input_active() const noexcept -> bool
{
    return m_state->text_input_active;
}

auto
InputSystem::bindings(BindingContextIdentifier context,
                      ActionIdentifier action,
                      PlayerIdentifier player,
                      std::source_location location) const -> std::vector<Binding>
{
    constexpr std::string_view OPERATION = "InputSystem::bindings";
    const std::uint32_t context_index = m_state->require_context(context, OPERATION, location);
    const std::uint32_t action_index = m_state->require_action(action, OPERATION, location);
    const PlayerRecord& record = m_state->require_player(player, OPERATION, location);
    std::vector<Binding> result;
    for (const PlayerBinding& entry : record.bindings[context_index])
    {
        if (entry.action == action_index)
        {
            result.push_back(entry.binding);
        }
    }
    return result;
}

void
InputSystem::rebind(const BindingSlot& slot,
                    const Binding& binding,
                    PlayerIdentifier player,
                    std::source_location location)
{
    // Find the slot, check the binding, replace it (its trigger starts over).
    constexpr std::string_view OPERATION = "InputSystem::rebind";
    InputSystemState& state = *m_state;
    const std::uint32_t context = state.require_context(slot.context, OPERATION, location);
    const std::uint32_t action = state.require_action(slot.action, OPERATION, location);
    PlayerRecord& record = state.require_player(player, OPERATION, location);
    const std::optional<std::size_t> position = InputSystemState::find_slot(record.bindings[context], action, slot.slot);
    if (!position)
    {
        throw_usage(OPERATION,
                    std::format("action '{}' has no binding {} in binding context '{}' for {}", state.actions->actions[action].name,
                                slot.slot, state.contexts[context].name, player),
                    "use add_binding() to add a binding; bindings() lists the existing ones", location);
    }
    state.require_valid_binding(binding, action, context, OPERATION, location);
    record.bindings[context][*position] = PlayerBinding{.action = action, .binding = binding};
}

auto
InputSystem::add_binding(BindingContextIdentifier context,
                         ActionIdentifier action,
                         const Binding& binding,
                         PlayerIdentifier player,
                         std::source_location location) -> BindingSlot
{
    constexpr std::string_view OPERATION = "InputSystem::add_binding";
    InputSystemState& state = *m_state;
    const std::uint32_t context_index = state.require_context(context, OPERATION, location);
    const std::uint32_t action_index = state.require_action(action, OPERATION, location);
    PlayerRecord& record = state.require_player(player, OPERATION, location);
    state.require_valid_binding(binding, action_index, context_index, OPERATION, location);
    std::vector<PlayerBinding>& list = record.bindings[context_index];
    const auto slot = static_cast<std::size_t>(std::ranges::count(list, action_index, &PlayerBinding::action));
    list.push_back(PlayerBinding{.action = action_index, .binding = binding});
    return BindingSlot{.context = context, .action = action, .slot = slot};
}

void
InputSystem::remove_binding(const BindingSlot& slot,
                            PlayerIdentifier player,
                            std::source_location location)
{
    constexpr std::string_view OPERATION = "InputSystem::remove_binding";
    InputSystemState& state = *m_state;
    const std::uint32_t context = state.require_context(slot.context, OPERATION, location);
    const std::uint32_t action = state.require_action(slot.action, OPERATION, location);
    PlayerRecord& record = state.require_player(player, OPERATION, location);
    const std::optional<std::size_t> position = InputSystemState::find_slot(record.bindings[context], action, slot.slot);
    if (!position)
    {
        throw_usage(OPERATION,
                    std::format("action '{}' has no binding {} in binding context '{}' for {}", state.actions->actions[action].name,
                                slot.slot, state.contexts[context].name, player),
                    "bindings() lists the existing bindings", location);
    }
    record.bindings[context].erase(record.bindings[context].begin() + static_cast<std::ptrdiff_t>(*position));
}

void
InputSystem::reset_bindings(PlayerIdentifier player,
                            std::source_location location)
{
    InputSystemState& state = *m_state;
    state.reset_player(state.require_player(player, "InputSystem::reset_bindings", location));
}

void
InputSystem::reset_bindings(BindingContextIdentifier context,
                            ActionIdentifier action,
                            PlayerIdentifier player,
                            std::source_location location)
{
    // Remove the action's bindings, then append its defaults.
    constexpr std::string_view OPERATION = "InputSystem::reset_bindings";
    InputSystemState& state = *m_state;
    const std::uint32_t context_index = state.require_context(context, OPERATION, location);
    const std::uint32_t action_index = state.require_action(action, OPERATION, location);
    std::vector<PlayerBinding>& list = state.require_player(player, OPERATION, location).bindings[context_index];
    std::erase_if(list, [action_index](const PlayerBinding& entry) { return entry.action == action_index; });
    for (const DefaultBinding& binding : state.contexts[context_index].defaults)
    {
        if (binding.action == action_index)
        {
            list.push_back(PlayerBinding{.action = action_index, .binding = binding.binding});
        }
    }
}

auto
InputSystem::find_bindings_using(InputSource source,
                                 PlayerIdentifier player,
                                 std::source_location location) const -> std::vector<BindingSlot>
{
    // Every binding reading the source, with its slot among its action's bindings in its context.
    const InputSystemState& state = *m_state;
    const PlayerRecord& record = state.require_player(player, "InputSystem::find_bindings_using", location);
    std::vector<BindingSlot> slots;
    for (std::size_t context = 0; context < record.bindings.size(); ++context)
    {
        std::vector<std::size_t> seen(state.actions->actions.size(), 0);
        for (const PlayerBinding& entry : record.bindings[context])
        {
            const std::size_t slot = seen[entry.action]++;
            if (std::ranges::find(entry.binding.sources, source) != entry.binding.sources.end())
            {
                slots.push_back(BindingSlot{.context = state.contexts[context].identifier,
                                            .action = state.actions->actions[entry.action].identifier,
                                            .slot = slot});
            }
        }
    }
    return slots;
}

void
InputSystem::capture_next_binding(PlayerIdentifier player,
                                  BindingCaptureSource sources,
                                  Key cancel_key,
                                  std::source_location location)
{
    static_cast<void>(m_state->require_player(player, "InputSystem::capture_next_binding", location));
    if (is_empty(sources))
    {
        throw_usage("InputSystem::capture_next_binding", "no kind of input to capture", "pass at least one BindingCaptureSource", location);
    }
    m_state->capture = Capture{.player = player, .sources = sources, .cancel_key = cancel_key};
}

void
InputSystem::cancel_binding_capture() noexcept
{
    m_state->capture.reset();
}

auto
InputSystem::is_capturing_binding() const noexcept -> bool
{
    return m_state->capture.has_value();
}

auto
InputSystem::action_name(ActionIdentifier action) const noexcept -> std::string_view
{
    const std::optional<std::uint32_t> index = m_state->actions->find(action);
    return index ? std::string_view{m_state->actions->actions[*index].name} : std::string_view{};
}

auto
InputSystem::context_name(BindingContextIdentifier context) const noexcept -> std::string_view
{
    const std::optional<std::uint32_t> index = m_state->find_context(context);
    return index ? std::string_view{m_state->contexts[*index].name} : std::string_view{};
}

// ================================================================================================= Storage

auto
InputSystem::save_bindings() const -> std::string
{
    // Lists that differ from the defaults, by name.
    const InputSystemState& state = *m_state;
    std::vector<StoredBindingList> lists;
    for (std::uint32_t player = 0; player < state.players.size(); ++player)
    {
        for (std::size_t context = 0; context < state.contexts.size(); ++context)
        {
            for (std::uint32_t action = 0; action < state.actions->actions.size(); ++action)
            {
                std::vector<Binding> current;
                for (const PlayerBinding& entry : state.players[player].bindings[context])
                {
                    if (entry.action == action)
                    {
                        current.push_back(entry.binding);
                    }
                }
                std::vector<Binding> defaults;
                for (const DefaultBinding& entry : state.contexts[context].defaults)
                {
                    if (entry.action == action)
                    {
                        defaults.push_back(entry.binding);
                    }
                }
                if (current != defaults)
                {
                    lists.push_back(StoredBindingList{.player = player,
                                                      .context = state.contexts[context].name,
                                                      .action = state.actions->actions[action].name,
                                                      .bindings = std::move(current)});
                }
            }
        }
    }
    return write_binding_storage(lists);
}

void
InputSystem::load_bindings(std::string_view json,
                           std::source_location location)
{
    // Parse, then check every list against the schema; report every problem at once.
    InputSystemState& state = *m_state;
    std::vector<std::string> problems;
    const std::vector<StoredBindingList> lists = read_binding_storage(json, problems);
    struct Resolved
    {
        std::uint32_t player = 0;
        std::uint32_t context = 0;
        std::uint32_t action = 0;
        const std::vector<Binding>* bindings = nullptr;
    };
    std::vector<Resolved> resolved;
    for (const StoredBindingList& list : lists)
    {
        const std::optional<std::uint32_t> context = state.find_context(BindingContextIdentifier::from_name(list.context));
        const std::optional<std::uint32_t> action = state.actions->find(ActionIdentifier::from_name(list.action));
        const std::string where = std::format("action '{}' in binding context '{}' for {}", list.action, list.context, PlayerIdentifier{list.player});
        if (list.player >= state.players.size())
        {
            problems.push_back(std::format("{}: the input system has {} player(s)", where, state.players.size()));
        }
        if (!context)
        {
            problems.push_back(std::format("{}: the schema declares no binding context '{}'", where, list.context));
        }
        if (!action)
        {
            problems.push_back(std::format("{}: the schema declares no action '{}'", where, list.action));
        }
        if (!context || !action || list.player >= state.players.size())
        {
            continue;
        }
        for (std::size_t index = 0; index < list.bindings.size(); ++index)
        {
            const ActionValueType type = state.actions->actions[*action].type;
            if (const InputSchemaProblem problem = check_binding(list.bindings[index], type); problem != InputSchemaProblem::NONE)
            {
                problems.push_back(std::format("{}, binding {}: {} ({})", where, index, describe_input_schema_problem(problem), problem));
            }
        }
        resolved.push_back(Resolved{.player = list.player, .context = *context, .action = *action, .bindings = &list.bindings});
    }
    if (!problems.empty())
    {
        std::string text = std::format("the saved bindings were not loaded ({} problem(s)):", problems.size());
        for (const std::string& problem : problems)
        {
            text += "\n    ";
            text += problem;
        }
        throw_usage("InputSystem::load_bindings", std::move(text), "fix or delete the saved bindings (save_bindings() writes valid ones)", location);
    }

    // Defaults everywhere, then the saved lists replace theirs.
    for (PlayerRecord& player : state.players)
    {
        state.reset_player(player);
    }
    for (const Resolved& list : resolved)
    {
        std::vector<PlayerBinding>& bindings = state.players[list.player].bindings[list.context];
        std::erase_if(bindings, [&list](const PlayerBinding& entry) { return entry.action == list.action; });
        for (const Binding& binding : *list.bindings)
        {
            bindings.push_back(PlayerBinding{.action = list.action, .binding = binding});
        }
    }
}

} // namespace gfx::platform
