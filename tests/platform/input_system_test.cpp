#include <gtest/gtest.h>

import std;
import gfx.platform;
import gfx.tests.input_support;

namespace
{

using namespace gfx::platform;
using namespace gfx::platform::literals;
using namespace std::chrono_literals;
using gfx::tests::Events;
using gfx::tests::GamepadIssuer;

constexpr auto SCHEMA = make_input_schema(
    actions(action("jump"),
            action("move", ActionValueType::AXIS_2D),
            action("zoom", ActionValueType::AXIS_1D),
            action("save"),
            action("shoot"),
            action("charge"),
            action("dodge"),
            action("pause"),
            action("accelerate", ActionValueType::AXIS_1D),
            action("send_message")),
    context("gameplay",
            bind("jump"_action, Key::SPACE),
            bind("jump"_action, GamepadButton::SOUTH),
            bind("move"_action, composite_2d(Key::W, Key::S, Key::A, Key::D)),
            bind("move"_action, stick(GamepadAxis::LEFT_X, GamepadAxis::LEFT_Y).with_deadzone(DeadzoneKind::SCALED_RADIAL, 0.2F)),
            bind("zoom"_action, MouseAxis::WHEEL_Y),
            bind("save"_action, chord(Key::S, KeyModifier::CONTROL)),
            bind("shoot"_action, MouseButton::LEFT),
            bind("shoot"_action, GamepadAxis::RIGHT_TRIGGER),
            bind("charge"_action, single(Key::E).held_for(500ms)),
            bind("dodge"_action, single(Key::F).tapped_within(200ms)),
            bind("accelerate"_action, single(GamepadAxis::RIGHT_TRIGGER).with_curve(ResponseCurve::QUADRATIC))),
    context("vehicle", {.priority = 5, .consumes_input = true, .blocks_lower_contexts = false, .reads_keyboard_during_text_input = false},
            bind("accelerate"_action, Key::W)),
    context("menu", {.priority = 10, .consumes_input = true, .blocks_lower_contexts = true, .reads_keyboard_during_text_input = false},
            bind("pause"_action, Key::ESCAPE)),
    context("chat", {.priority = 20, .consumes_input = true, .blocks_lower_contexts = false, .reads_keyboard_during_text_input = true},
            bind("send_message"_action, Key::RETURN)));

/// An input system with the gameplay context active, and a clock advancing one 60 Hz frame per update.
class InputSystemTest : public ::testing::Test
{
protected:
    auto step(const Events& events = Events{}) -> const InputFrame&
    {
        m_now += 16ms;
        return m_input.update(events.span(), m_now);
    }

    auto step_at(std::chrono::milliseconds time,
                 const Events& events = Events{}) -> const InputFrame&
    {
        m_now = time;
        return m_input.update(events.span(), m_now);
    }

    InputSystem m_input{SCHEMA};
    ContextActivation m_gameplay = m_input.activate_context("gameplay"_context);
    std::chrono::nanoseconds m_now{};
};

TEST_F(InputSystemTest, ButtonsReportEdgesAndDuration)
{
    // Before the first update every action is released.
    EXPECT_EQ(m_input.frame().number(), 0U);
    EXPECT_FALSE(m_input.frame().is_action_pressed("jump"_action));

    // Pressed: just pressed, value 1.
    const InputFrame& pressed = step(Events{}.key_down(Key::SPACE));
    EXPECT_EQ(pressed.number(), 1U);
    EXPECT_TRUE(pressed.is_action_pressed("jump"_action));
    EXPECT_TRUE(pressed.was_action_just_pressed("jump"_action));
    EXPECT_FLOAT_EQ(pressed.get_action_value("jump"_action), 1.0F);
    EXPECT_TRUE(pressed.is_key_down(Key::SPACE));

    // Held: no edge, the duration grows; key repeat changes nothing.
    const InputFrame& held = step(Events{}.key_down(Key::SPACE, KeyCode::SPACE, true));
    EXPECT_TRUE(held.is_action_pressed("jump"_action));
    EXPECT_FALSE(held.was_action_just_pressed("jump"_action));
    EXPECT_EQ(held.action_state("jump"_action).pressed_duration, 16ms);

    // Released: just released.
    const InputFrame& released = step(Events{}.key_up(Key::SPACE));
    EXPECT_FALSE(released.is_action_pressed("jump"_action));
    EXPECT_TRUE(released.was_action_just_released("jump"_action));
    EXPECT_FALSE(step().was_action_just_released("jump"_action));
}

TEST_F(InputSystemTest, PressAndReleaseWithinOneFrameStillCount)
{
    // A tap shorter than a frame presses the action for that frame and releases it the next.
    const InputFrame& tapped = step(Events{}.key_down(Key::SPACE).key_up(Key::SPACE));
    EXPECT_TRUE(tapped.was_action_just_pressed("jump"_action));
    EXPECT_TRUE(step().was_action_just_released("jump"_action));
}

TEST_F(InputSystemTest, CompositesCombineKeysAndNormalizeDiagonals)
{
    const gfx::Vector2 forward = step(Events{}.key_down(Key::W)).get_action_value_2d("move"_action);
    EXPECT_FLOAT_EQ(forward.x, 0.0F);
    EXPECT_FLOAT_EQ(forward.y, 1.0F);

    // W + D: a diagonal of length 1.
    const gfx::Vector2 diagonal = step(Events{}.key_down(Key::D)).get_action_value_2d("move"_action);
    EXPECT_NEAR(diagonal.x, std::sqrt(0.5F), 1e-6F);
    EXPECT_NEAR(diagonal.y, std::sqrt(0.5F), 1e-6F);

    // Opposite keys cancel.
    const gfx::Vector2 cancel = step(Events{}.key_down(Key::A)).get_action_value_2d("move"_action);
    EXPECT_FLOAT_EQ(cancel.x, 0.0F);
    EXPECT_TRUE(step(Events{}.key_up(Key::W).key_up(Key::A).key_up(Key::D)).was_action_just_released("move"_action));
}

TEST_F(InputSystemTest, SticksApplyTheirDeadZoneWithUpPositive)
{
    // The gamepad goes to player 0 when it connects.
    const GamepadHandle gamepad = GamepadIssuer::gamepad(0);
    step(Events{}.gamepad_connected(gamepad));
    EXPECT_EQ(m_input.gamepad_of(PlayerIdentifier{0}), gamepad);

    // Inside the dead zone: nothing.
    EXPECT_FALSE(step(Events{}.gamepad_axis(gamepad, GamepadAxis::LEFT_X, 0.1F)).is_action_pressed("move"_action));

    // Scaled radial: 0.6 maps to (0.6 - 0.2) / (1 - 0.2) = 0.5; stick y down is positive, the action's y up is.
    const gfx::Vector2 right = step(Events{}.gamepad_axis(gamepad, GamepadAxis::LEFT_X, 0.6F)).get_action_value_2d("move"_action);
    EXPECT_NEAR(right.x, 0.5F, 1e-6F);
    const InputFrame& up = step(Events{}.gamepad_axis(gamepad, GamepadAxis::LEFT_X, 0.0F).gamepad_axis(gamepad, GamepadAxis::LEFT_Y, -1.0F));
    EXPECT_NEAR(up.get_action_value_2d("move"_action).y, 1.0F, 1e-6F);
    EXPECT_FLOAT_EQ(up.gamepad_axis(GamepadAxis::LEFT_Y), -1.0F);

    // The strongest binding wins: a key (1) over the stick at half way.
    step(Events{}.gamepad_axis(gamepad, GamepadAxis::LEFT_Y, 0.0F).gamepad_axis(gamepad, GamepadAxis::LEFT_X, 0.6F));
    const gfx::Vector2 strongest = step(Events{}.key_down(Key::A)).get_action_value_2d("move"_action);
    EXPECT_FLOAT_EQ(strongest.x, -1.0F);
}

TEST_F(InputSystemTest, AnalogSourcesPressButtonsFromTheThreshold)
{
    // A trigger presses "shoot" from 0.5 on, with its strength as value; the curve squares "accelerate".
    const GamepadHandle gamepad = GamepadIssuer::gamepad(0);
    const InputFrame& light = step(Events{}.gamepad_connected(gamepad).gamepad_axis(gamepad, GamepadAxis::RIGHT_TRIGGER, 0.4F));
    EXPECT_FALSE(light.is_action_pressed("shoot"_action));
    EXPECT_NEAR(light.get_action_value("accelerate"_action), 0.16F, 1e-6F);
    const InputFrame& firm = step(Events{}.gamepad_axis(gamepad, GamepadAxis::RIGHT_TRIGGER, 0.8F));
    EXPECT_TRUE(firm.was_action_just_pressed("shoot"_action));
    EXPECT_NEAR(firm.get_action_value("shoot"_action), 0.8F, 1e-6F);
    EXPECT_NEAR(firm.get_action_value("accelerate"_action), 0.64F, 1e-6F);
}

TEST_F(InputSystemTest, MouseAxesAccumulateOverTheFrame)
{
    // Wheel events of a frame add up, then start over.
    const InputFrame& scrolled = step(Events{}.wheel({.x = 0.0F, .y = 1.0F}).wheel({.x = 0.0F, .y = 0.5F}).mouse_motion({.x = 3.0F, .y = 4.0F}));
    EXPECT_FLOAT_EQ(scrolled.get_action_value("zoom"_action), 1.5F);
    EXPECT_FLOAT_EQ(scrolled.mouse_motion().x, 3.0F);
    EXPECT_FLOAT_EQ(scrolled.mouse_wheel().y, 1.5F);
    EXPECT_TRUE(step().was_action_just_released("zoom"_action));

    // Mouse buttons.
    const InputFrame& clicked = step(Events{}.mouse_down(MouseButton::LEFT));
    EXPECT_TRUE(clicked.was_action_just_pressed("shoot"_action));
    EXPECT_TRUE(clicked.is_mouse_button_down(MouseButton::LEFT));
}

TEST_F(InputSystemTest, ChordsMatchModifiersAndHideTheirKey)
{
    // Ctrl+S saves and does not also move back.
    const InputFrame& saved = step(Events{}.key_down(Key::LEFT_CONTROL).key_down(Key::S));
    EXPECT_TRUE(saved.was_action_just_pressed("save"_action));
    EXPECT_FALSE(saved.is_action_pressed("move"_action));
    EXPECT_TRUE(has_any(saved.modifiers(), KeyModifier::LEFT_CONTROL));

    // S alone moves; Ctrl+Shift+S is not the exact chord.
    const InputFrame& back = step(Events{}.key_up(Key::LEFT_CONTROL));
    EXPECT_FALSE(back.is_action_pressed("save"_action));
    EXPECT_FLOAT_EQ(back.get_action_value_2d("move"_action).y, -1.0F);
    EXPECT_FALSE(step(Events{}.key_down(Key::RIGHT_CONTROL).key_down(Key::LEFT_SHIFT)).is_action_pressed("save"_action));
}

TEST_F(InputSystemTest, HoldPressesAfterItsTime)
{
    // E must be held 500 ms.
    EXPECT_FALSE(step_at(0ms, Events{}.key_down(Key::E)).is_action_pressed("charge"_action));
    EXPECT_FALSE(step_at(400ms).is_action_pressed("charge"_action));
    const InputFrame& charged = step_at(500ms);
    EXPECT_TRUE(charged.was_action_just_pressed("charge"_action));
    EXPECT_TRUE(step_at(600ms, Events{}.key_up(Key::E)).was_action_just_released("charge"_action));

    // Released early: never pressed.
    step_at(1000ms, Events{}.key_down(Key::E));
    EXPECT_FALSE(step_at(1300ms, Events{}.key_up(Key::E)).is_action_pressed("charge"_action));
    EXPECT_FALSE(step_at(1600ms).is_action_pressed("charge"_action));
}

TEST_F(InputSystemTest, TapPressesOnceOnAShortRelease)
{
    // Released within 200 ms: pressed for the frame of the release.
    EXPECT_FALSE(step_at(0ms, Events{}.key_down(Key::F)).is_action_pressed("dodge"_action));
    EXPECT_TRUE(step_at(100ms, Events{}.key_up(Key::F)).was_action_just_pressed("dodge"_action));
    EXPECT_TRUE(step_at(116ms).was_action_just_released("dodge"_action));

    // Held too long: no tap.
    step_at(1000ms, Events{}.key_down(Key::F));
    EXPECT_FALSE(step_at(1300ms, Events{}.key_up(Key::F)).is_action_pressed("dodge"_action));
}

TEST_F(InputSystemTest, HigherContextsConsumeTheirSourcesAndBlockingContextsHideEverything)
{
    // The vehicle takes W from the gameplay composite; S still moves.
    ContextActivation vehicle = m_input.activate_context("vehicle"_context);
    const InputFrame& driving = step(Events{}.key_down(Key::W));
    EXPECT_FLOAT_EQ(driving.get_action_value("accelerate"_action), 1.0F);
    EXPECT_FALSE(driving.is_action_pressed("move"_action));
    EXPECT_FLOAT_EQ(step(Events{}.key_up(Key::W).key_down(Key::S)).get_action_value_2d("move"_action).y, -1.0F);

    // A modal menu: lower contexts read nothing, so "move" is released.
    {
        ContextActivation menu = m_input.activate_context("menu"_context);
        const std::vector<BindingContextIdentifier> order{"menu"_context, "vehicle"_context, "gameplay"_context};
        EXPECT_EQ(m_input.active_contexts(), order);
        const InputFrame& paused = step(Events{}.key_down(Key::ESCAPE).key_down(Key::SPACE));
        EXPECT_TRUE(paused.was_action_just_pressed("pause"_action));
        EXPECT_FALSE(paused.is_action_pressed("jump"_action));
        EXPECT_TRUE(paused.was_action_just_released("move"_action));
    }

    // The menu popped: Space, pressed while it blocked gameplay, counts for gameplay once pressed again
    // (ContextDescription::ignore_held_inputs).
    EXPECT_FALSE(m_input.is_context_active("menu"_context));
    const InputFrame& resumed = step();
    EXPECT_TRUE(resumed.was_action_just_released("pause"_action));
    EXPECT_FALSE(resumed.is_action_pressed("jump"_action));
    EXPECT_TRUE(step(Events{}.key_up(Key::SPACE).key_down(Key::SPACE)).was_action_just_pressed("jump"_action));
}

TEST_F(InputSystemTest, ActivationsAreRaiiLayers)
{
    // Deactivating releases what the context pressed.
    step(Events{}.key_down(Key::SPACE));
    m_gameplay.reset();
    EXPECT_FALSE(m_gameplay.is_active());
    EXPECT_TRUE(step().was_action_just_released("jump"_action));

    // Activations move; one may outlive its input system harmlessly.
    ContextActivation outliving;
    {
        InputSystem temporary{SCHEMA};
        ContextActivation activation = temporary.activate_context("menu"_context);
        outliving = std::move(activation);
        EXPECT_FALSE(activation.is_active());
        EXPECT_TRUE(outliving.is_active());
        EXPECT_EQ(outliving.context(), "menu"_context);
    }
    EXPECT_FALSE(outliving.is_active());
    outliving.reset();

    // Unknown contexts are refused.
    EXPECT_THROW((void)m_input.activate_context("missing"_context), gfx::InvalidUsageError);
}

TEST_F(InputSystemTest, TextInputSilencesKeyboardBindingsOutsideTextContexts)
{
    // Typing in chat: gameplay keys do nothing, the chat context reads Return, the mouse still works.
    ContextActivation chat = m_input.activate_context("chat"_context);
    m_input.set_text_input_active(true);
    const InputFrame& typing = step(Events{}.key_down(Key::SPACE).key_down(Key::RETURN).mouse_down(MouseButton::LEFT));
    EXPECT_TRUE(typing.is_text_input_active());
    EXPECT_FALSE(typing.is_action_pressed("jump"_action));
    EXPECT_TRUE(typing.is_action_pressed("send_message"_action));
    EXPECT_TRUE(typing.is_action_pressed("shoot"_action));

    // Text input over: the key held while typing counts for gameplay once pressed again.
    m_input.set_text_input_active(false);
    EXPECT_FALSE(step().is_action_pressed("jump"_action));
    EXPECT_TRUE(step(Events{}.key_up(Key::SPACE).key_down(Key::SPACE)).was_action_just_pressed("jump"_action));
}

TEST_F(InputSystemTest, ContextsIgnoreInputsHeldWhenTheyStartReadingThem)
{
    // Escape, held when the menu opens, presses the menu's "pause" only once pressed again.
    step(Events{}.key_down(Key::ESCAPE));
    {
        ContextActivation menu = m_input.activate_context("menu"_context);
        EXPECT_FALSE(step().is_action_pressed("pause"_action));
        step(Events{}.key_up(Key::ESCAPE));
        EXPECT_TRUE(step(Events{}.key_down(Key::ESCAPE)).was_action_just_pressed("pause"_action));

        // A key and a mouse button pressed while the menu blocks gameplay...
        step(Events{}.key_down(Key::SPACE).mouse_down(MouseButton::LEFT));
    }

    // ...count for gameplay once pressed again, after the menu is gone.
    const InputFrame& resumed = step();
    EXPECT_FALSE(resumed.is_action_pressed("jump"_action));
    EXPECT_FALSE(resumed.is_action_pressed("shoot"_action));
    const InputFrame& again = step(Events{}.key_up(Key::SPACE).key_down(Key::SPACE).mouse_up(MouseButton::LEFT).mouse_down(MouseButton::LEFT));
    EXPECT_TRUE(again.was_action_just_pressed("jump"_action));
    EXPECT_TRUE(again.was_action_just_pressed("shoot"_action));
}

TEST_F(InputSystemTest, APressInTheFrameAContextStartsReadingCounts)
{
    // Activated before the update that brings the press: the press is new.
    ContextActivation menu = m_input.activate_context("menu"_context);
    EXPECT_TRUE(step(Events{}.key_down(Key::ESCAPE)).was_action_just_pressed("pause"_action));
}

TEST_F(InputSystemTest, OnlyTheHeldSourcesOfABindingAreIgnored)
{
    // W, held for the vehicle (which consumes it), is ignored by the gameplay composite once the vehicle
    // is left; D, pressed then, moves.
    {
        ContextActivation vehicle = m_input.activate_context("vehicle"_context);
        EXPECT_FLOAT_EQ(step(Events{}.key_down(Key::W)).get_action_value("accelerate"_action), 1.0F);
    }
    const gfx::Vector2 move = step(Events{}.key_down(Key::D)).get_action_value_2d("move"_action);
    EXPECT_FLOAT_EQ(move.x, 1.0F);
    EXPECT_FLOAT_EQ(move.y, 0.0F);
}

TEST_F(InputSystemTest, HeldGamepadButtonsAreIgnoredButNotAxes)
{
    // Pressed while the menu blocks gameplay: the button waits for a new press, the trigger counts at once.
    const GamepadHandle gamepad = GamepadIssuer::gamepad(0);
    {
        ContextActivation menu = m_input.activate_context("menu"_context);
        const InputFrame& paused = step(Events{}
                                            .gamepad_connected(gamepad)
                                            .gamepad_down(gamepad, GamepadButton::SOUTH)
                                            .gamepad_axis(gamepad, GamepadAxis::RIGHT_TRIGGER, 1.0F));
        EXPECT_FALSE(paused.is_action_pressed("jump"_action));
        EXPECT_FALSE(paused.is_action_pressed("accelerate"_action));
    }
    const InputFrame& resumed = step();
    EXPECT_FALSE(resumed.is_action_pressed("jump"_action));
    EXPECT_FLOAT_EQ(resumed.get_action_value("accelerate"_action), 1.0F);
    step(Events{}.gamepad_up(gamepad, GamepadButton::SOUTH));
    EXPECT_TRUE(step(Events{}.gamepad_down(gamepad, GamepadButton::SOUTH)).was_action_just_pressed("jump"_action));
}

/// Contexts that read held inputs at once (ignore_held_inputs = false).
constexpr auto IMMEDIATE_SCHEMA = make_input_schema(
    actions(action("jump"),
            action("pause")),
    context("gameplay", {.ignore_held_inputs = false},
            bind("jump"_action, Key::SPACE)),
    context("menu", {.priority = 10, .blocks_lower_contexts = true, .ignore_held_inputs = false},
            bind("pause"_action, Key::ESCAPE)));

TEST(InputSystemHeldInputsTest, ContextsMayReadHeldInputsAtOnce)
{
    // Keys held before the menu opens press its actions at once, and gameplay's again when it closes.
    InputSystem input{IMMEDIATE_SCHEMA};
    const ContextActivation gameplay = input.activate_context("gameplay"_context);
    EXPECT_TRUE(input.update(Events{}.key_down(Key::ESCAPE).key_down(Key::SPACE).span(), 16ms).is_action_pressed("jump"_action));
    std::optional<ContextActivation> menu = input.activate_context("menu"_context);
    const InputFrame& paused = input.update(Events{}.span(), 32ms);
    EXPECT_TRUE(paused.was_action_just_pressed("pause"_action));
    EXPECT_FALSE(paused.is_action_pressed("jump"_action));
    menu.reset();
    EXPECT_TRUE(input.update(Events{}.span(), 48ms).was_action_just_pressed("jump"_action));
}

TEST_F(InputSystemTest, LosingFocusReleasesKeysAndButtons)
{
    step(Events{}.key_down(Key::W).mouse_down(MouseButton::LEFT));
    const InputFrame& unfocused = step(Events{}.focus_lost());
    EXPECT_TRUE(unfocused.was_action_just_released("move"_action));
    EXPECT_TRUE(unfocused.was_action_just_released("shoot"_action));
    EXPECT_FALSE(unfocused.is_key_down(Key::W));
}

TEST_F(InputSystemTest, LogicalKeysFollowTheLayout)
{
    // Bound to the key labeled Z: on a German layout, the physical Y key produces it.
    const BindingSlot slot = m_input.add_binding("gameplay"_context, "jump"_action, single(KeyCode::Z));
    EXPECT_EQ(slot.slot, 2U);
    const InputFrame& german = step(Events{}.key_down(Key::Y, KeyCode::Z));
    EXPECT_TRUE(german.is_action_pressed("jump"_action));
    EXPECT_TRUE(german.is_key_code_down(KeyCode::Z));
    EXPECT_FALSE(german.is_key_code_down(KeyCode::Y));
}

TEST_F(InputSystemTest, ReadingUndeclaredActionsOrMissingPlayersThrows)
{
    const InputFrame& frame = step();
    try
    {
        (void)frame.is_action_pressed("missing"_action);
        FAIL() << "an undeclared action was read";
    }
    catch (const gfx::InvalidUsageError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("is not declared by the input schema"), std::string_view::npos) << error.what();
    }
    EXPECT_THROW((void)frame.is_action_pressed("jump"_action, PlayerIdentifier{1}), gfx::InvalidUsageError);
    EXPECT_THROW((void)frame.gamepad(PlayerIdentifier{3}), gfx::InvalidUsageError);
    EXPECT_THROW((InputSystem{SCHEMA, {.players = 0, .gamepad_assignment = GamepadAssignment::AUTOMATIC, .actuation_threshold = 0.5F}}),
                 gfx::InvalidUsageError);
    EXPECT_THROW((InputSystem{SCHEMA, {.players = 1, .gamepad_assignment = GamepadAssignment::AUTOMATIC, .actuation_threshold = 0.0F}}),
                 gfx::InvalidUsageError);
}

TEST(InputSystemPlayersTest, DevicesAreRoutedToTheirPlayers)
{
    // Two players: gamepads are assigned in connection order; the keyboard is player 0's.
    InputSystem input{SCHEMA, {.players = 2, .gamepad_assignment = GamepadAssignment::AUTOMATIC, .actuation_threshold = 0.5F}};
    ContextActivation first = input.activate_context("gameplay"_context, PlayerIdentifier{0});
    ContextActivation second = input.activate_context("gameplay"_context, PlayerIdentifier{1});
    const GamepadHandle pad_a = GamepadIssuer::gamepad(0);
    const GamepadHandle pad_b = GamepadIssuer::gamepad(1);
    const InputFrame& connected = input.update(Events{}.gamepad_connected(pad_a).gamepad_connected(pad_b).gamepad_down(pad_b, GamepadButton::SOUTH).span(), 16ms);
    EXPECT_EQ(connected.gamepad(PlayerIdentifier{0}).gamepad, pad_a);
    EXPECT_EQ(connected.gamepad(PlayerIdentifier{1}).gamepad, pad_b);
    EXPECT_FALSE(connected.is_action_pressed("jump"_action, PlayerIdentifier{0}));
    EXPECT_TRUE(connected.is_action_pressed("jump"_action, PlayerIdentifier{1}));
    EXPECT_TRUE(connected.is_gamepad_button_down(GamepadButton::SOUTH, PlayerIdentifier{1}));

    // The keyboard moves to player 1 on request.
    input.assign_keyboard_and_mouse(PlayerIdentifier{1});
    const InputFrame& typed = input.update(Events{}.key_down(Key::W).span(), 32ms);
    EXPECT_EQ(input.keyboard_and_mouse_player(), PlayerIdentifier{1});
    EXPECT_FLOAT_EQ(typed.get_action_value_2d("move"_action, PlayerIdentifier{1}).y, 1.0F);
    EXPECT_FALSE(typed.is_action_pressed("move"_action, PlayerIdentifier{0}));

    // A disconnected gamepad frees its player, and the next gamepad takes the slot.
    const GamepadHandle pad_c = GamepadIssuer::gamepad(2);
    input.update(Events{}.gamepad_disconnected(pad_a).span(), 48ms);
    EXPECT_FALSE(input.gamepad_of(PlayerIdentifier{0}).is_valid());
    input.update(Events{}.gamepad_connected(pad_c).span(), 64ms);
    EXPECT_EQ(input.gamepad_of(PlayerIdentifier{0}), pad_c);
}

TEST(InputSystemPlayersTest, ManualAssignmentWaitsForTheGame)
{
    InputSystem input{SCHEMA, {.players = 2, .gamepad_assignment = GamepadAssignment::MANUAL, .actuation_threshold = 0.5F}};
    ContextActivation second = input.activate_context("gameplay"_context, PlayerIdentifier{1});
    const GamepadHandle pad = GamepadIssuer::gamepad(4);
    input.update(Events{}.gamepad_connected(pad).span(), 16ms);
    EXPECT_FALSE(input.gamepad_of(PlayerIdentifier{0}).is_valid());

    // Assigned ("press A to join"): its buttons reach player 1.
    input.assign_gamepad(pad, PlayerIdentifier{1});
    EXPECT_TRUE(input.update(Events{}.gamepad_down(pad, GamepadButton::SOUTH).span(), 32ms).is_action_pressed("jump"_action, PlayerIdentifier{1}));
    input.unassign_gamepad(pad);
    EXPECT_FALSE(input.update(Events{}.span(), 48ms).is_action_pressed("jump"_action, PlayerIdentifier{1}));
    EXPECT_THROW(input.assign_gamepad(GamepadHandle{}, PlayerIdentifier{0}), gfx::InvalidUsageError);
}

TEST_F(InputSystemTest, RebindingChangesWhatActionsRead)
{
    // Jump moves from Space to J.
    m_input.rebind({.context = "gameplay"_context, .action = "jump"_action, .slot = 0}, single(Key::J));
    EXPECT_EQ(m_input.bindings("gameplay"_context, "jump"_action).front(), single(Key::J));
    EXPECT_FALSE(step(Events{}.key_down(Key::SPACE)).is_action_pressed("jump"_action));
    EXPECT_TRUE(step(Events{}.key_down(Key::J)).is_action_pressed("jump"_action));

    // Conflicts: every binding reading a source.
    const std::vector<BindingSlot> using_w = m_input.find_bindings_using(InputSource::of(Key::W));
    ASSERT_EQ(using_w.size(), 2U);
    EXPECT_EQ(using_w[0], (BindingSlot{.context = "gameplay"_context, .action = "move"_action, .slot = 0}));
    EXPECT_EQ(using_w[1], (BindingSlot{.context = "vehicle"_context, .action = "accelerate"_action, .slot = 0}));

    // Remove and reset.
    m_input.remove_binding({.context = "gameplay"_context, .action = "jump"_action, .slot = 0});
    EXPECT_EQ(m_input.bindings("gameplay"_context, "jump"_action).size(), 1U);
    m_input.reset_bindings("gameplay"_context, "jump"_action);
    EXPECT_EQ(m_input.bindings("gameplay"_context, "jump"_action).front(), single(Key::SPACE));
    m_input.add_binding("gameplay"_context, "pause"_action, single(Key::P));
    EXPECT_EQ(m_input.bindings("gameplay"_context, "pause"_action).size(), 1U);
    m_input.reset_bindings();
    EXPECT_TRUE(m_input.bindings("gameplay"_context, "pause"_action).empty());

    // Invalid rebinding names the action and the problem; missing slots are refused.
    try
    {
        m_input.rebind({.context = "gameplay"_context, .action = "jump"_action, .slot = 0}, stick(GamepadAxis::LEFT_X, GamepadAxis::LEFT_Y));
        FAIL() << "a stick was bound to a button";
    }
    catch (const gfx::InvalidUsageError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("action 'jump' (BUTTON)"), std::string_view::npos) << error.what();
        EXPECT_NE(std::string_view{error.what()}.find("INCOMPATIBLE_BINDING"), std::string_view::npos) << error.what();
    }
    EXPECT_THROW(m_input.rebind({.context = "gameplay"_context, .action = "jump"_action, .slot = 7}, single(Key::K)), gfx::InvalidUsageError);
    EXPECT_THROW(m_input.remove_binding({.context = "menu"_context, .action = "jump"_action, .slot = 0}), gfx::InvalidUsageError);
}

TEST_F(InputSystemTest, BindingCaptureReportsTheNextPressOnce)
{
    // The captured press does not reach the actions.
    m_input.capture_next_binding();
    EXPECT_TRUE(m_input.is_capturing_binding());
    const InputFrame& captured = step(Events{}.key_down(Key::SPACE));
    ASSERT_TRUE(captured.captured_binding().has_value());
    EXPECT_EQ(captured.captured_binding()->binding, single(Key::SPACE));
    EXPECT_FALSE(captured.captured_binding()->canceled);
    EXPECT_FALSE(captured.is_action_pressed("jump"_action));
    EXPECT_FALSE(m_input.is_capturing_binding());
    EXPECT_FALSE(step().captured_binding().has_value());

    // Escape cancels; repeats are ignored.
    m_input.capture_next_binding();
    const InputFrame& canceled = step(Events{}.key_down(Key::SPACE, KeyCode::SPACE, true).key_down(Key::ESCAPE));
    ASSERT_TRUE(canceled.captured_binding().has_value());
    EXPECT_TRUE(canceled.captured_binding()->canceled);

    // Gamepad axes are captured past the actuation threshold.
    const GamepadHandle gamepad = GamepadIssuer::gamepad(0);
    step(Events{}.gamepad_connected(gamepad));
    m_input.capture_next_binding(PlayerIdentifier{0}, BindingCaptureSource::GAMEPAD_AXIS);
    EXPECT_FALSE(step(Events{}.gamepad_axis(gamepad, GamepadAxis::RIGHT_X, 0.2F)).captured_binding().has_value());
    const InputFrame& axis = step(Events{}.key_down(Key::K).gamepad_axis(gamepad, GamepadAxis::RIGHT_X, 0.9F));
    ASSERT_TRUE(axis.captured_binding().has_value());
    EXPECT_EQ(axis.captured_binding()->binding, single(GamepadAxis::RIGHT_X));
    m_input.capture_next_binding();
    m_input.cancel_binding_capture();
    EXPECT_FALSE(m_input.is_capturing_binding());
}

TEST_F(InputSystemTest, ContextsCanBeAddedAtRunTime)
{
    // A mod adds its own context with declared actions.
    const std::array bindings{bind("jump"_action, Key::U)};
    m_input.add_context(ContextDefinition{.name = "mod", .description = {}}, bindings);
    ContextActivation mod = m_input.activate_context("mod"_context);
    EXPECT_TRUE(step(Events{}.key_down(Key::U)).is_action_pressed("jump"_action));
    EXPECT_EQ(m_input.context_name("mod"_context), "mod");
    EXPECT_EQ(m_input.action_name("jump"_action), "jump");
    EXPECT_TRUE(m_input.action_name("missing"_action).empty());

    // Taken names and invalid bindings are refused.
    EXPECT_THROW(m_input.add_context(ContextDefinition{.name = "mod", .description = {}}, bindings), gfx::InvalidUsageError);
    const std::array invalid{bind("jump"_action, stick(GamepadAxis::LEFT_X, GamepadAxis::LEFT_Y))};
    EXPECT_THROW(m_input.add_context(ContextDefinition{.name = "other", .description = {}}, invalid), gfx::InvalidUsageError);
}

TEST_F(InputSystemTest, FramesStayUnchangedUntilTheSecondUpdate)
{
    // Frame N is still intact (and readable from jobs) while frame N + 1 is computed.
    const InputFrame& first = step(Events{}.key_down(Key::SPACE));
    const InputFrame copy = first;
    std::atomic<bool> stop{false};
    std::atomic<int> inconsistent{0};
    std::vector<std::jthread> jobs;
    for (int job = 0; job < 4; ++job)
    {
        jobs.emplace_back([&first, &stop, &inconsistent]
        {
            while (!stop.load())
            {
                if (!first.is_action_pressed("jump"_action) || first.number() != 1U)
                {
                    ++inconsistent;
                }
            }
        });
    }
    const InputFrame& second = step(Events{}.key_up(Key::SPACE));
    stop.store(true);
    jobs.clear();
    EXPECT_EQ(inconsistent.load(), 0);
    EXPECT_NE(&first, &second);
    EXPECT_TRUE(first.is_action_pressed("jump"_action));
    EXPECT_FALSE(second.is_action_pressed("jump"_action));

    // Frames are values: a copy keeps its state.
    EXPECT_TRUE(copy.was_action_just_pressed("jump"_action));
    EXPECT_EQ(copy.number(), 1U);
}

} // namespace
