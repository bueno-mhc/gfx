#include <gtest/gtest.h>

import std;
import gfx.platform;

namespace
{

using namespace gfx::platform;
using namespace gfx::platform::literals;
using namespace std::chrono_literals;

// A compile-time schema: the checks run while compiling (a mistake does not compile).
constexpr auto SCHEMA = make_input_schema(
    actions(action("jump"), action("move", ActionValueType::AXIS_2D), action("throttle", ActionValueType::AXIS_1D), action("pause")),
    context("gameplay",
            bind("jump"_action, Key::SPACE),
            bind("jump"_action, GamepadButton::SOUTH),
            bind("move"_action, composite_2d(Key::W, Key::S, Key::A, Key::D)),
            bind("move"_action, stick(GamepadAxis::LEFT_X, GamepadAxis::LEFT_Y).with_deadzone(DeadzoneKind::SCALED_RADIAL, 0.15F)),
            bind("throttle"_action, composite_axis(Key::Q, Key::E))),
    context("menu", {.priority = 10, .consumes_input = true, .blocks_lower_contexts = true, .reads_keyboard_during_text_input = false},
            bind("pause"_action, Key::ESCAPE)));

TEST(InputBindingTest, BuildersPlaceTheirSources)
{
    // Single inputs: buttons and axes are BUTTON bindings, mouse axes MOUSE_AXIS.
    constexpr Binding jump = single(Key::SPACE);
    static_assert(jump.kind == BindingKind::BUTTON);
    static_assert(jump.sources[0] == InputSource::of(Key::SPACE));
    static_assert(jump.source_count() == 1);
    static_assert(single(MouseAxis::MOTION_X).kind == BindingKind::MOUSE_AXIS);
    static_assert(single(GamepadAxis::RIGHT_TRIGGER).kind == BindingKind::BUTTON);

    // Composites list negative then positive, or up, down, left, right; 2D composites normalize.
    constexpr Binding wasd = composite_2d(Key::W, Key::S, Key::A, Key::D);
    static_assert(wasd.source_count() == 4 && wasd.normalize);
    static_assert(wasd.sources[3] == InputSource::of(Key::D));
    constexpr Binding gamepad = gamepad_chord(GamepadButton::LEFT_SHOULDER, GamepadButton::RIGHT_SHOULDER);
    static_assert(gamepad.kind == BindingKind::GAMEPAD_CHORD && gamepad.source_count() == 2);

    // Chords are exact by default.
    constexpr Binding undo = chord(KeyCode::Z, KeyModifier::CONTROL);
    static_assert(undo.exact_modifiers && undo.modifiers == KeyModifier::CONTROL);
    static_assert(InputSource::of(KeyCode::Z).kind == InputSourceKind::KEY_CODE);
    static_assert(InputSource::of(Key::A).is_digital() && !InputSource::of(GamepadAxis::LEFT_X).is_digital());
}

TEST(InputBindingTest, ModifiersAreFluentValues)
{
    // Every modifier returns a new binding; the original is untouched.
    constexpr Binding base = stick(GamepadAxis::RIGHT_X, GamepadAxis::RIGHT_Y);
    constexpr Binding shaped = base.with_deadzone(DeadzoneKind::AXIAL, 0.2F, 0.9F)
                                   .with_curve(ResponseCurve::POWER, 1.5F)
                                   .inverted(false, true)
                                   .scaled(2.0F)
                                   .swizzled()
                                   .normalized();
    static_assert(base.deadzone == DeadzoneKind::NONE);
    static_assert(shaped.deadzone == DeadzoneKind::AXIAL && shaped.deadzone_lower == 0.2F && shaped.deadzone_upper == 0.9F);
    static_assert(shaped.curve == ResponseCurve::POWER && shaped.curve_exponent == 1.5F);
    static_assert(!shaped.invert_x && shaped.invert_y && shaped.swizzle && shaped.normalize);
    static_assert(shaped.scale == gfx::Vector2{.x = 2.0F, .y = 2.0F});
    static_assert(single(Key::E).held_for(500ms).trigger == TriggerKind::HOLD);
    static_assert(single(Key::E).tapped_within(200ms).trigger_time == 200ms);
}

TEST(InputBindingTest, CheckBindingNamesWhatIsWrong)
{
    // Well-formed bindings suit their action types.
    EXPECT_EQ(check_binding(single(Key::SPACE), ActionValueType::BUTTON), InputSchemaProblem::NONE);
    EXPECT_EQ(check_binding(composite_2d(Key::W, Key::S, Key::A, Key::D), ActionValueType::AXIS_2D), InputSchemaProblem::NONE);
    EXPECT_EQ(check_binding(single(GamepadAxis::LEFT_TRIGGER), ActionValueType::BUTTON), InputSchemaProblem::NONE);

    // Each problem is told apart.
    EXPECT_EQ(check_binding(Binding{}, ActionValueType::BUTTON), InputSchemaProblem::MALFORMED_BINDING);
    Binding hole = composite_axis(Key::Q, Key::E);
    hole.sources[0] = InputSource{};
    EXPECT_EQ(check_binding(hole, ActionValueType::AXIS_1D), InputSchemaProblem::MALFORMED_BINDING);
    Binding mouse_button = single(Key::SPACE);
    mouse_button.sources[0] = InputSource{.kind = InputSourceKind::MOUSE_BUTTON, .code = 200};
    EXPECT_EQ(check_binding(mouse_button, ActionValueType::BUTTON), InputSchemaProblem::MALFORMED_BINDING);
    Binding modifiers_on_button = single(Key::S);
    modifiers_on_button.modifiers = KeyModifier::CONTROL;
    EXPECT_EQ(check_binding(modifiers_on_button, ActionValueType::BUTTON), InputSchemaProblem::MALFORMED_BINDING);
    EXPECT_EQ(check_binding(chord(Key::S, KeyModifier::CAPS_LOCK), ActionValueType::BUTTON), InputSchemaProblem::MALFORMED_BINDING);
    EXPECT_EQ(check_binding(stick(GamepadAxis::LEFT_X, GamepadAxis::LEFT_Y), ActionValueType::BUTTON), InputSchemaProblem::INCOMPATIBLE_BINDING);
    EXPECT_EQ(check_binding(single(Key::E).held_for(1s), ActionValueType::AXIS_1D), InputSchemaProblem::TRIGGER_ON_AXIS);
    EXPECT_EQ(check_binding(single(Key::E).held_for(0ms), ActionValueType::BUTTON), InputSchemaProblem::INVALID_TRIGGER_TIME);
    EXPECT_EQ(check_binding(single(GamepadAxis::LEFT_X).with_deadzone(DeadzoneKind::AXIAL, 0.5F, 0.4F), ActionValueType::AXIS_1D),
              InputSchemaProblem::INVALID_DEADZONE);
    EXPECT_EQ(check_binding(single(GamepadAxis::LEFT_X).with_deadzone(DeadzoneKind::RADIAL, std::numeric_limits<float>::quiet_NaN()),
                            ActionValueType::AXIS_1D),
              InputSchemaProblem::INVALID_DEADZONE);
    EXPECT_EQ(check_binding(single(GamepadAxis::LEFT_X).with_curve(ResponseCurve::POWER, 0.0F), ActionValueType::AXIS_1D),
              InputSchemaProblem::INVALID_RESPONSE_CURVE);
    EXPECT_FALSE(describe_input_schema_problem(InputSchemaProblem::INCOMPATIBLE_BINDING).empty());
}

TEST(InputSchemaTest, CompileTimeSchemasFlattenTheirContexts)
{
    // Sizes are part of the type; bindings carry their context.
    static_assert(SCHEMA.actions.size() == 4);
    static_assert(SCHEMA.contexts.size() == 2);
    static_assert(SCHEMA.bindings.size() == 6);
    static_assert(SCHEMA.bindings[5].context == "menu"_context);
    static_assert(SCHEMA.bindings[5].action == "pause"_action);
    static_assert(SCHEMA.contexts[1].description.priority == 10);
    static_assert(SCHEMA.actions[1].identifier() == "move"_action);
    static_assert(check_input_schema(SCHEMA.view()).is_valid());

    // The schema converts to a view.
    const InputSchemaView view = SCHEMA;
    EXPECT_EQ(view.bindings.size(), 6U);
    EXPECT_NO_THROW(validate_input_schema(view));
}

TEST(InputSchemaTest, TheDesignDocumentSchemaCompiles)
{
    // docs/platform-design.md, C6: descriptions may name only the fields they change.
    constexpr auto GAME_INPUT = make_input_schema(
        actions(action("jump"), action("move", ActionValueType::AXIS_2D), action("charge"), action("confirm"), action("back")),
        context("gameplay",
                bind("jump"_action, Key::SPACE),
                bind("jump"_action, GamepadButton::SOUTH),
                bind("move"_action, composite_2d(Key::W, Key::S, Key::A, Key::D)),
                bind("move"_action, stick(GamepadAxis::LEFT_X, GamepadAxis::LEFT_Y)
                                        .with_deadzone(DeadzoneKind::SCALED_RADIAL, 0.15F)
                                        .with_curve(ResponseCurve::QUADRATIC)),
                bind("charge"_action, single(GamepadButton::WEST).held_for(500ms))),
        context("menu", {.priority = 10, .consumes_input = true, .blocks_lower_contexts = true},
                bind("confirm"_action, Key::RETURN),
                bind("confirm"_action, GamepadButton::SOUTH),
                bind("back"_action, Key::ESCAPE)));
    static_assert(GAME_INPUT.actions.size() == 5);
    static_assert(!GAME_INPUT.contexts[1].description.reads_keyboard_during_text_input);
    static_assert(GAME_INPUT.contexts[1].description.ignore_held_inputs);
    const InputSystem input{GAME_INPUT, {.players = 2}};
    EXPECT_EQ(input.player_count(), 2U);
}

TEST(InputSchemaTest, RunTimeSchemasReportTheirFirstProblemWithNames)
{
    // Data-driven schemas are spans over any storage.
    const std::array actions_of_schema{ActionDefinition{.name = "fire", .type = ActionValueType::BUTTON},
                                       ActionDefinition{.name = "fire", .type = ActionValueType::AXIS_1D}};
    const std::array contexts_of_schema{ContextDefinition{.name = "gameplay", .description = {}}};
    const InputSchemaView duplicate{.actions = actions_of_schema, .contexts = contexts_of_schema, .bindings = {}};
    const InputSchemaCheck check = check_input_schema(duplicate);
    EXPECT_EQ(check.problem, InputSchemaProblem::DUPLICATE_ACTION);
    EXPECT_EQ(check.action, 1U);
    EXPECT_EQ(check.duplicate_of, 0U);
    try
    {
        validate_input_schema(duplicate);
        FAIL() << "a duplicate action was accepted";
    }
    catch (const gfx::InvalidUsageError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("two actions are named 'fire'"), std::string_view::npos) << error.what();
        EXPECT_NE(std::string_view{error.what()}.find("DUPLICATE_ACTION"), std::string_view::npos) << error.what();
    }

    // Bindings that name undeclared actions or contexts, or do not suit their action.
    const std::array single_action{ActionDefinition{.name = "fire", .type = ActionValueType::BUTTON}};
    const std::array unknown_action{SchemaBinding{.context = "gameplay"_context, .action = "jump"_action, .binding = single(Key::SPACE)}};
    EXPECT_EQ(check_input_schema({.actions = single_action, .contexts = contexts_of_schema, .bindings = unknown_action}).problem,
              InputSchemaProblem::UNKNOWN_ACTION);
    const std::array unknown_context{SchemaBinding{.context = "menu"_context, .action = "fire"_action, .binding = single(Key::SPACE)}};
    EXPECT_EQ(check_input_schema({.actions = single_action, .contexts = contexts_of_schema, .bindings = unknown_context}).problem,
              InputSchemaProblem::UNKNOWN_CONTEXT);
    const std::array two_dimensional{
        SchemaBinding{.context = "gameplay"_context, .action = "fire"_action, .binding = stick(GamepadAxis::LEFT_X, GamepadAxis::LEFT_Y)}};
    const InputSchemaView incompatible{.actions = single_action, .contexts = contexts_of_schema, .bindings = two_dimensional};
    EXPECT_EQ(check_input_schema(incompatible).problem, InputSchemaProblem::INCOMPATIBLE_BINDING);
    try
    {
        validate_input_schema(incompatible);
        FAIL() << "a stick on a button was accepted";
    }
    catch (const gfx::InvalidUsageError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("action 'fire' in binding context 'gameplay'"), std::string_view::npos) << error.what();
        EXPECT_NE(std::string_view{error.what()}.find("AXIS_2D"), std::string_view::npos) << error.what();
    }

    // Empty names.
    const std::array unnamed{ActionDefinition{.name = "", .type = ActionValueType::BUTTON}};
    EXPECT_EQ(check_input_schema({.actions = unnamed, .contexts = {}, .bindings = {}}).problem, InputSchemaProblem::MISSING_NAME);
}

} // namespace
