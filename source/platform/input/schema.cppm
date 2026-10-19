/// @file schema.cppm
/// @brief The input schema: the actions a game declares, its binding contexts and their default
///        bindings. A schema is usually a compile-time constant built by make_input_schema(), which
///        rejects mistakes (duplicate names, unknown actions, a 2D binding on a button, ...) with a
///        compilation error naming the problem; schemas built at run time (data-driven games, tools)
///        are checked by validate_input_schema(), which InputSystem calls.
///
/// @code
/// using namespace gfx::platform::literals;
/// constexpr auto SCHEMA = make_input_schema(
///     actions(action("jump"), action("move", ActionValueType::AXIS_2D), action("pause")),
///     context("gameplay",
///             bind("jump"_action, Key::SPACE),
///             bind("jump"_action, GamepadButton::SOUTH),
///             bind("move"_action, composite_2d(Key::W, Key::S, Key::A, Key::D)),
///             bind("move"_action, stick(GamepadAxis::LEFT_X, GamepadAxis::LEFT_Y)
///                                     .with_deadzone(DeadzoneKind::SCALED_RADIAL, 0.15F))),
///     context("menu", {.priority = 10, .blocks_lower_contexts = true},
///             bind("pause"_action, Key::ESCAPE)));
/// @endcode
export module gfx.platform.input:schema;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;
import :binding;

export namespace gfx::platform
{

// ================================================================================================= Definitions

/// @brief An action: a name the game reads instead of devices ("jump", "move") and the kind of value
///        it produces.
struct ActionDefinition
{
    std::string_view name{};                        ///< Name, also used in saved bindings.
    ActionValueType type = ActionValueType::BUTTON; ///< Kind of value.

    /// @brief The identifier of the name ("jump"_action).
    [[nodiscard]] constexpr auto identifier() const noexcept -> ActionIdentifier
    {
        return ActionIdentifier::from_name(name);
    }

    constexpr bool operator==(const ActionDefinition&) const = default;
};

/// @brief How a binding context takes part in evaluation. Active contexts are evaluated by decreasing
///        priority (the most recently activated first among equal priorities).
struct ContextDescription
{
    std::int32_t priority = 0;                     ///< Higher priorities are evaluated first.
    bool consumes_input = true;                    ///< Sources bound here are hidden from lower-priority contexts.
    bool blocks_lower_contexts = false;            ///< Lower-priority contexts read nothing (modal menus).
    bool reads_keyboard_during_text_input = false; ///< Keyboard bindings keep working while text input is active.
    bool ignore_held_inputs = true;                ///< Inputs already held when the context starts reading them
                                                   ///< count only once released and pressed again (see below).

    // ignore_held_inputs: a context starts reading an input when it is activated, when a context above
    // stops blocking it or consuming that input, or when text input ends. A key, mouse button or gamepad
    // button held from an earlier frame at that moment reads as released in this context until it is
    // released, so the key that opens a menu does not also press the menu's actions, and the key that
    // closes it does not press gameplay's (Unreal's "ignore all pressed keys until release"). A press
    // that begins in the same frame counts. Analog inputs (sticks, triggers, mouse motion) are not ignored.

    constexpr bool operator==(const ContextDescription&) const = default;
};

/// @brief A binding context: a named set of bindings activated and deactivated together ("gameplay",
///        "vehicle", "menu"), as a layer.
struct ContextDefinition
{
    std::string_view name{};          ///< Name, also used in saved bindings.
    ContextDescription description{}; ///< Priority and blocking rules.

    /// @brief The identifier of the name ("gameplay"_context).
    [[nodiscard]] constexpr auto identifier() const noexcept -> BindingContextIdentifier
    {
        return BindingContextIdentifier::from_name(name);
    }

    constexpr bool operator==(const ContextDefinition&) const = default;
};

/// @brief A default binding of a schema: a context binds an action to an input.
struct SchemaBinding
{
    BindingContextIdentifier context{}; ///< Context holding the binding.
    ActionIdentifier action{};          ///< Action it drives.
    Binding binding{};                  ///< What it reads.

    constexpr bool operator==(const SchemaBinding&) const = default;
};

/// @brief A schema whatever its size: what InputSystem and validate_input_schema() read.
struct InputSchemaView
{
    std::span<const ActionDefinition> actions{};   ///< Actions.
    std::span<const ContextDefinition> contexts{}; ///< Binding contexts.
    std::span<const SchemaBinding> bindings{};     ///< Default bindings.
};

/// @brief A schema of fixed size, usually a compile-time constant (make_input_schema()).
template<std::size_t ACTION_COUNT, std::size_t CONTEXT_COUNT, std::size_t BINDING_COUNT>
struct InputSchema
{
    std::array<ActionDefinition, ACTION_COUNT> actions{};    ///< Actions.
    std::array<ContextDefinition, CONTEXT_COUNT> contexts{}; ///< Binding contexts.
    std::array<SchemaBinding, BINDING_COUNT> bindings{};     ///< Default bindings.

    /// @brief The schema as a view.
    [[nodiscard]] constexpr auto view() const noexcept -> InputSchemaView
    {
        return InputSchemaView{.actions = actions, .contexts = contexts, .bindings = bindings};
    }

    /// @brief The schema as a view (InputSystem accepts schemas directly).
    [[nodiscard]] constexpr operator InputSchemaView() const noexcept
    {
        return view();
    }
};

/// @brief A context and its bindings, as context() builds them for make_input_schema().
template<std::size_t BINDING_COUNT>
struct ContextBlock
{
    ContextDefinition definition{};                     ///< The context.
    std::array<ContextBinding, BINDING_COUNT> bindings{}; ///< Its default bindings.
};

// ================================================================================================= Builders

/// @brief Declares an action, e.g. `action("move", ActionValueType::AXIS_2D)`.
[[nodiscard]] consteval auto
action(std::string_view name,
       ActionValueType type = ActionValueType::BUTTON) -> ActionDefinition
{
    return ActionDefinition{.name = name, .type = type};
}

/// @brief The actions of a schema, e.g. `actions(action("jump"), action("move", ActionValueType::AXIS_2D))`.
template<class... Definitions>
    requires(std::same_as<Definitions, ActionDefinition> && ...)
[[nodiscard]] consteval auto
actions(const Definitions&... definitions) -> std::array<ActionDefinition, sizeof...(Definitions)>
{
    return {definitions...};
}

/// @brief A binding context with its default bindings.
template<class... Bindings>
    requires(std::same_as<Bindings, ContextBinding> && ...)
[[nodiscard]] consteval auto
context(std::string_view name,
        ContextDescription description,
        const Bindings&... bindings) -> ContextBlock<sizeof...(Bindings)>
{
    return ContextBlock<sizeof...(Bindings)>{
        .definition = ContextDefinition{.name = name, .description = description},
        .bindings = {bindings...},
    };
}

/// @brief A binding context with the default description (priority 0, consumes its input).
template<class... Bindings>
    requires(std::same_as<Bindings, ContextBinding> && ...)
[[nodiscard]] consteval auto
context(std::string_view name,
        const Bindings&... bindings) -> ContextBlock<sizeof...(Bindings)>
{
    return context(name, ContextDescription{}, bindings...);
}

// ================================================================================================= Checks

/// @brief The first problem check_input_schema() found, and where: indices into the schema's actions,
///        contexts and bindings (empty when they do not apply).
struct InputSchemaCheck
{
    InputSchemaProblem problem = InputSchemaProblem::NONE; ///< What is wrong.
    std::optional<std::size_t> action{};                   ///< Action concerned.
    std::optional<std::size_t> context{};                  ///< Context concerned.
    std::optional<std::size_t> binding{};                  ///< Binding concerned.
    std::optional<std::size_t> duplicate_of{};             ///< DUPLICATE_*: the earlier action or context of that name.

    /// @brief Whether the schema is valid.
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        return problem == InputSchemaProblem::NONE;
    }

    constexpr bool operator==(const InputSchemaCheck&) const = default;
};

/// @brief Whether @p source names an enumerator of its kind (any non-zero KeyCode is valid).
[[nodiscard]] constexpr auto
is_valid_source(InputSource source) noexcept -> bool
{
    // Sequential enumerations are valid below their enumerator count; Key lists its enumerators.
    switch (source.kind)
    {
        case InputSourceKind::NONE:
            return false;
        case InputSourceKind::KEY:
            return source.code != 0 && source.code <= std::numeric_limits<std::uint16_t>::max()
                   && to_string(static_cast<Key>(source.code)) != "<invalid>";
        case InputSourceKind::KEY_CODE:
            return source.code != 0;
        case InputSourceKind::MOUSE_BUTTON:
            return source.code < ENUMERATOR_COUNT<MouseButton>;
        case InputSourceKind::MOUSE_AXIS:
            return source.code < ENUMERATOR_COUNT<MouseAxis>;
        case InputSourceKind::GAMEPAD_BUTTON:
            return source.code < ENUMERATOR_COUNT<GamepadButton>;
        case InputSourceKind::GAMEPAD_AXIS:
            return source.code < ENUMERATOR_COUNT<GamepadAxis>;
    }
    return false;
}

/// @brief What is wrong with @p binding on an action of type @p type (InputSchemaProblem::NONE when
///        nothing): its sources, its shape, its trigger and its modifiers. InputSystem checks
///        rebinding with it.
[[nodiscard]] constexpr auto
check_binding(const Binding& binding,
              ActionValueType type) noexcept -> InputSchemaProblem
{
    // Sources are packed at the front and name enumerators of their kind.
    const std::size_t count = binding.source_count();
    for (std::size_t index = 0; index < binding.sources.size(); ++index)
    {
        const InputSource source = binding.sources[index];
        if (index < count ? !is_valid_source(source) : source.kind != InputSourceKind::NONE)
        {
            return InputSchemaProblem::MALFORMED_BINDING;
        }
    }

    // Each kind reads a fixed number of sources of given kinds.
    const auto first_sources_are = [&binding](std::size_t first_count, auto predicate)
    {
        return std::all_of(binding.sources.begin(), binding.sources.begin() + static_cast<std::ptrdiff_t>(first_count), predicate);
    };
    const auto digital = [](InputSource source) { return source.is_digital(); };
    const auto is_key = [](InputSource source)
    {
        return source.kind == InputSourceKind::KEY || source.kind == InputSourceKind::KEY_CODE;
    };
    const auto of_kind = [](InputSourceKind kind)
    {
        return [kind](InputSource source) { return source.kind == kind; };
    };
    bool well_formed = false;
    switch (binding.kind)
    {
        case BindingKind::NONE:
            well_formed = false;
            break;
        case BindingKind::BUTTON:
            well_formed = count == 1 && binding.sources[0].kind != InputSourceKind::MOUSE_AXIS;
            break;
        case BindingKind::CHORD:
            well_formed = count == 1 && first_sources_are(1, is_key);
            break;
        case BindingKind::GAMEPAD_CHORD:
            well_formed = count >= 2 && first_sources_are(count, of_kind(InputSourceKind::GAMEPAD_BUTTON));
            break;
        case BindingKind::MOUSE_AXIS:
            well_formed = count == 1 && first_sources_are(1, of_kind(InputSourceKind::MOUSE_AXIS));
            break;
        case BindingKind::COMPOSITE_AXIS:
            well_formed = count == 2 && first_sources_are(2, digital);
            break;
        case BindingKind::COMPOSITE_2D:
            well_formed = count == 4 && first_sources_are(4, digital);
            break;
        case BindingKind::STICK:
            well_formed = count == 2 && first_sources_are(2, of_kind(InputSourceKind::GAMEPAD_AXIS));
            break;
    }

    // Modifiers belong to chords, and only Shift, Control, Alt and Super take part (not the locks).
    constexpr KeyModifier CHORD_MODIFIERS = KeyModifier::SHIFT | KeyModifier::CONTROL | KeyModifier::ALT | KeyModifier::SUPER;
    if (binding.kind != BindingKind::CHORD ? !is_empty(binding.modifiers) : !is_empty(binding.modifiers & ~CHORD_MODIFIERS))
    {
        well_formed = false;
    }
    if (!well_formed)
    {
        return InputSchemaProblem::MALFORMED_BINDING;
    }

    // Two-dimensional bindings need a two-dimensional action; triggers need a button.
    if ((binding.kind == BindingKind::COMPOSITE_2D || binding.kind == BindingKind::STICK) && type != ActionValueType::AXIS_2D)
    {
        return InputSchemaProblem::INCOMPATIBLE_BINDING;
    }
    if (binding.trigger != TriggerKind::DOWN && type != ActionValueType::BUTTON)
    {
        return InputSchemaProblem::TRIGGER_ON_AXIS;
    }
    if (binding.trigger != TriggerKind::DOWN && binding.trigger_time <= std::chrono::milliseconds::zero())
    {
        return InputSchemaProblem::INVALID_TRIGGER_TIME;
    }

    // Dead zone thresholds are ordered within [0, 1] (NaN fails every comparison); exponents are positive.
    if (binding.deadzone != DeadzoneKind::NONE
        && !(binding.deadzone_lower >= 0.0F && binding.deadzone_lower < binding.deadzone_upper && binding.deadzone_upper <= 1.0F))
    {
        return InputSchemaProblem::INVALID_DEADZONE;
    }
    if (binding.curve == ResponseCurve::POWER && !(binding.curve_exponent > 0.0F))
    {
        return InputSchemaProblem::INVALID_RESPONSE_CURVE;
    }
    return InputSchemaProblem::NONE;
}

/// @brief The first problem of @p schema: names (present, unique), then each binding (its context and
///        action declared, then check_binding()).
[[nodiscard]] constexpr auto
check_input_schema(InputSchemaView schema) noexcept -> InputSchemaCheck
{
    // Names are present and unique (identifiers are hashes of the names).
    for (std::size_t index = 0; index < schema.actions.size(); ++index)
    {
        if (schema.actions[index].name.empty())
        {
            return {.problem = InputSchemaProblem::MISSING_NAME, .action = index};
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier)
        {
            if (schema.actions[earlier].identifier() == schema.actions[index].identifier())
            {
                return {.problem = InputSchemaProblem::DUPLICATE_ACTION, .action = index, .duplicate_of = earlier};
            }
        }
    }
    for (std::size_t index = 0; index < schema.contexts.size(); ++index)
    {
        if (schema.contexts[index].name.empty())
        {
            return {.problem = InputSchemaProblem::MISSING_NAME, .context = index};
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier)
        {
            if (schema.contexts[earlier].identifier() == schema.contexts[index].identifier())
            {
                return {.problem = InputSchemaProblem::DUPLICATE_CONTEXT, .context = index, .duplicate_of = earlier};
            }
        }
    }

    // Every binding names a declared context and action, and suits the action.
    for (std::size_t index = 0; index < schema.bindings.size(); ++index)
    {
        const SchemaBinding& entry = schema.bindings[index];
        const auto context = std::ranges::find(schema.contexts, entry.context, &ContextDefinition::identifier);
        if (context == schema.contexts.end())
        {
            return {.problem = InputSchemaProblem::UNKNOWN_CONTEXT, .binding = index};
        }
        const auto context_index = static_cast<std::size_t>(context - schema.contexts.begin());
        const auto action = std::ranges::find(schema.actions, entry.action, &ActionDefinition::identifier);
        if (action == schema.actions.end())
        {
            return {.problem = InputSchemaProblem::UNKNOWN_ACTION, .context = context_index, .binding = index};
        }
        const auto action_index = static_cast<std::size_t>(action - schema.actions.begin());
        if (const InputSchemaProblem problem = check_binding(entry.binding, action->type); problem != InputSchemaProblem::NONE)
        {
            return {.problem = problem, .action = action_index, .context = context_index, .binding = index};
        }
    }
    return {};
}

/// @brief A sentence describing @p problem (for messages, e.g. in a rebinding menu).
[[nodiscard]] constexpr auto
describe_input_schema_problem(InputSchemaProblem problem) noexcept -> std::string_view
{
    switch (problem)
    {
        case InputSchemaProblem::NONE:
            return "nothing is wrong";
        case InputSchemaProblem::MISSING_NAME:
            return "an action or a binding context has an empty name";
        case InputSchemaProblem::DUPLICATE_ACTION:
            return "two actions have the same name (or names whose identifier hashes collide)";
        case InputSchemaProblem::DUPLICATE_CONTEXT:
            return "two binding contexts have the same name (or names whose identifier hashes collide)";
        case InputSchemaProblem::UNKNOWN_ACTION:
            return "the binding names an action the schema does not declare";
        case InputSchemaProblem::UNKNOWN_CONTEXT:
            return "the binding names a binding context the schema does not declare";
        case InputSchemaProblem::MALFORMED_BINDING:
            return "the binding lacks the sources its kind needs (BUTTON: one source other than a mouse axis; CHORD: one "
                   "key; GAMEPAD_CHORD: two to four gamepad buttons; MOUSE_AXIS: one mouse axis; COMPOSITE_AXIS: two "
                   "digital sources; COMPOSITE_2D: four digital sources; STICK: two gamepad axes), names an input that "
                   "does not exist, or has modifiers other than Shift, Control, Alt and Super on a CHORD";
        case InputSchemaProblem::INCOMPATIBLE_BINDING:
            return "a two-dimensional binding (COMPOSITE_2D, STICK) needs an AXIS_2D action";
        case InputSchemaProblem::TRIGGER_ON_AXIS:
            return "HOLD and TAP triggers need a BUTTON action";
        case InputSchemaProblem::INVALID_TRIGGER_TIME:
            return "HOLD and TAP triggers need a positive time";
        case InputSchemaProblem::INVALID_DEADZONE:
            return "dead zone thresholds must satisfy 0 <= lower < upper <= 1";
        case InputSchemaProblem::INVALID_RESPONSE_CURVE:
            return "a POWER response curve needs a positive exponent";
    }
    return "unknown problem";
}

/// @brief A sentence describing @p check in @p schema, with the names concerned (for messages).
[[nodiscard]] auto
describe_input_schema_problem(InputSchemaView schema,
                              const InputSchemaCheck& check) -> std::string;

/// @brief Checks @p schema.
/// @throws InvalidUsageError describing the first problem, with the names concerned.
void
validate_input_schema(InputSchemaView schema,
                      std::source_location location = std::source_location::current());

// ================================================================================================= Compile-time schemas

// A constant expression cannot throw: make_input_schema() calls one of these functions, which are not
// constexpr, so that the compiler reports the problem by its name, below an "in 'constexpr' expansion
// of" line pointing at the schema.

/// @cond INTERNAL
inline void input_schema_error_an_action_or_a_context_has_an_empty_name() {}
inline void input_schema_error_two_actions_have_the_same_name() {}
inline void input_schema_error_two_contexts_have_the_same_name() {}
inline void input_schema_error_a_binding_names_an_undeclared_action() {}
inline void input_schema_error_a_binding_names_an_undeclared_context() {}
inline void input_schema_error_a_binding_lacks_the_sources_its_kind_needs() {}
inline void input_schema_error_a_two_dimensional_binding_needs_an_axis_2d_action() {}
inline void input_schema_error_hold_and_tap_triggers_need_a_button_action() {}
inline void input_schema_error_hold_and_tap_triggers_need_a_positive_time() {}
inline void input_schema_error_dead_zone_thresholds_must_satisfy_0_lower_upper_1() {}
inline void input_schema_error_a_power_curve_needs_a_positive_exponent() {}

/// Stops constant evaluation on @p problem.
constexpr void
reject_input_schema(InputSchemaProblem problem)
{
    switch (problem)
    {
        case InputSchemaProblem::NONE:
            break;
        case InputSchemaProblem::MISSING_NAME:
            input_schema_error_an_action_or_a_context_has_an_empty_name();
            break;
        case InputSchemaProblem::DUPLICATE_ACTION:
            input_schema_error_two_actions_have_the_same_name();
            break;
        case InputSchemaProblem::DUPLICATE_CONTEXT:
            input_schema_error_two_contexts_have_the_same_name();
            break;
        case InputSchemaProblem::UNKNOWN_ACTION:
            input_schema_error_a_binding_names_an_undeclared_action();
            break;
        case InputSchemaProblem::UNKNOWN_CONTEXT:
            input_schema_error_a_binding_names_an_undeclared_context();
            break;
        case InputSchemaProblem::MALFORMED_BINDING:
            input_schema_error_a_binding_lacks_the_sources_its_kind_needs();
            break;
        case InputSchemaProblem::INCOMPATIBLE_BINDING:
            input_schema_error_a_two_dimensional_binding_needs_an_axis_2d_action();
            break;
        case InputSchemaProblem::TRIGGER_ON_AXIS:
            input_schema_error_hold_and_tap_triggers_need_a_button_action();
            break;
        case InputSchemaProblem::INVALID_TRIGGER_TIME:
            input_schema_error_hold_and_tap_triggers_need_a_positive_time();
            break;
        case InputSchemaProblem::INVALID_DEADZONE:
            input_schema_error_dead_zone_thresholds_must_satisfy_0_lower_upper_1();
            break;
        case InputSchemaProblem::INVALID_RESPONSE_CURVE:
            input_schema_error_a_power_curve_needs_a_positive_exponent();
            break;
    }
}
/// @endcond

/// @brief A compile-time schema: @p actions (actions()) and the binding contexts (context()). A
///        problem is a compilation error naming it, e.g. "call to non-'constexpr' function 'void
///        gfx::platform::input_schema_error_two_actions_have_the_same_name()'".
template<std::size_t ACTION_COUNT, std::size_t... BINDING_COUNTS>
[[nodiscard]] consteval auto
make_input_schema(const std::array<ActionDefinition, ACTION_COUNT>& actions,
                  const ContextBlock<BINDING_COUNTS>&... contexts)
    -> InputSchema<ACTION_COUNT, sizeof...(BINDING_COUNTS), (BINDING_COUNTS + ... + 0)>
{
    // Gather the contexts and flatten their bindings.
    InputSchema<ACTION_COUNT, sizeof...(BINDING_COUNTS), (BINDING_COUNTS + ... + 0)> schema;
    schema.actions = actions;
    std::size_t context_index = 0;
    std::size_t binding_index = 0;
    [[maybe_unused]] const auto gather = [&]<std::size_t COUNT>(const ContextBlock<COUNT>& block)
    {
        schema.contexts[context_index++] = block.definition;
        for (const ContextBinding& binding : block.bindings)
        {
            schema.bindings[binding_index++] = SchemaBinding{
                .context = block.definition.identifier(),
                .action = binding.action,
                .binding = binding.binding,
            };
        }
    };
    (gather(contexts), ...);

    // Reject the first problem by name.
    reject_input_schema(check_input_schema(schema.view()).problem);
    return schema;
}

} // namespace gfx::platform
