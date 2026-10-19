// The UI's navigation context of the input system (gfx.ui.platform): its actions and default bindings in a schema
// with the application's, navigation_input() turning the keyboard's and a gamepad's actions into commands (pressed,
// held) and the right stick into scrolling, the keys it binds hidden from gameplay while it is active, and the
// gamepad-only context leaving the keyboard to the UI's own keymap.
#include <gtest/gtest.h>

import std;
import gfx.platform;
import gfx.ui;
import gfx.ui.platform;

namespace
{

using namespace std::chrono_literals;
using gfx::Key;
using gfx::platform::Event;
using gfx::platform::EventType;
using gfx::platform::GamepadAxis;
using gfx::platform::GamepadButton;
using gfx::ui::UiNavigationCommand;

/// The handles of virtual gamepads.
class GamepadIssuer : public gfx::HandleIssuer
{
public:
    [[nodiscard]] static auto gamepad(std::uint32_t index) -> gfx::platform::GamepadHandle
    {
        return gfx::platform::GamepadHandle::create(handle_key(), index, 1);
    }
};

/// A game's schema with the UI's navigation (keyboard and gamepad).
constexpr auto SCHEMA = gfx::platform::make_input_schema(
    gfx::ui::with_ui_navigation_actions(gfx::platform::actions(gfx::platform::action("jump"))),
    gfx::platform::context("gameplay", gfx::platform::bind(gfx::platform::action("jump").identifier(), Key::SPACE)),
    gfx::ui::ui_navigation_context());
static_assert(SCHEMA.actions.size() == 1 + gfx::ui::UI_NAVIGATION_ACTION_COUNT);
static_assert(SCHEMA.contexts.size() == 2);

/// The same with the gamepad's navigation only.
constexpr auto GAMEPAD_SCHEMA = gfx::platform::make_input_schema(gfx::ui::with_ui_navigation_actions(gfx::platform::actions(gfx::platform::action("jump"))),
                                                                 gfx::ui::ui_gamepad_navigation_context());

/// The events of a frame.
class Events
{
public:
    auto key(Key key,
             bool down,
             gfx::KeyModifier modifiers = gfx::KeyModifier::NONE) -> Events&
    {
        gfx::platform::KeyboardEvent payload{};
        payload.key = key;
        payload.modifiers = modifiers;
        m_events.push_back(down ? Event::make<EventType::KEY_DOWN>(std::chrono::nanoseconds{}, payload)
                                : Event::make<EventType::KEY_UP>(std::chrono::nanoseconds{}, payload));
        return *this;
    }

    auto connected(gfx::platform::GamepadHandle gamepad) -> Events&
    {
        gfx::platform::GamepadDeviceEvent payload{};
        payload.gamepad = gamepad;
        m_events.push_back(Event::make<EventType::GAMEPAD_CONNECTED>(std::chrono::nanoseconds{}, payload));
        return *this;
    }

    auto button(gfx::platform::GamepadHandle gamepad,
                GamepadButton button,
                bool down) -> Events&
    {
        gfx::platform::GamepadButtonEvent payload{};
        payload.gamepad = gamepad;
        payload.button = button;
        m_events.push_back(down ? Event::make<EventType::GAMEPAD_BUTTON_DOWN>(std::chrono::nanoseconds{}, payload)
                                : Event::make<EventType::GAMEPAD_BUTTON_UP>(std::chrono::nanoseconds{}, payload));
        return *this;
    }

    auto axis(gfx::platform::GamepadHandle gamepad,
              GamepadAxis axis,
              float value) -> Events&
    {
        gfx::platform::GamepadAxisEvent payload{};
        payload.gamepad = gamepad;
        payload.axis = axis;
        payload.value = value;
        m_events.push_back(Event::make<EventType::GAMEPAD_AXIS_MOTION>(std::chrono::nanoseconds{}, payload));
        return *this;
    }

    [[nodiscard]] auto span() const noexcept -> gfx::platform::EventSpan
    {
        return gfx::platform::EventSpan{m_events, {}};
    }

private:
    std::vector<Event> m_events{};
};

TEST(UiNavigationInputTest, TheKeysAndTheGamepadOfTheContextBecomeNavigationCommands)
{
    gfx::platform::InputSystem input{SCHEMA};
    const gfx::platform::ContextActivation gameplay = input.activate_context(gfx::platform::BindingContextIdentifier::from_name("gameplay"));
    const gfx::platform::ContextActivation navigation =
        input.activate_context(gfx::platform::BindingContextIdentifier::from_name(gfx::ui::UI_NAVIGATION_CONTEXT));
    std::chrono::nanoseconds now = 1s;
    const auto update = [&](const Events& events)
    {
        now += 16ms;
        return gfx::ui::navigation_input(input.update(events.span(), now));
    };

    // Down: MOVE_DOWN pressed and held, then held only, then neither.
    gfx::ui::UiNavigationInput commands = update(Events{}.key(Key::DOWN, true));
    EXPECT_EQ(commands.pressed, UiNavigationCommand::MOVE_DOWN);
    EXPECT_EQ(commands.down, UiNavigationCommand::MOVE_DOWN);
    commands = update(Events{});
    EXPECT_EQ(commands.pressed, UiNavigationCommand::NONE);
    EXPECT_EQ(commands.down, UiNavigationCommand::MOVE_DOWN);
    commands = update(Events{}.key(Key::DOWN, false));
    EXPECT_EQ(commands.down, UiNavigationCommand::NONE);

    // Return: ACTIVATE and INPUT; Shift+Tab: PREVIOUS only.
    commands = update(Events{}.key(Key::RETURN, true).key(Key::RETURN, false));
    EXPECT_EQ(commands.pressed, UiNavigationCommand::ACTIVATE | UiNavigationCommand::INPUT);
    commands = update(Events{}.key(Key::LEFT_SHIFT, true, gfx::KeyModifier::LEFT_SHIFT).key(Key::TAB, true, gfx::KeyModifier::LEFT_SHIFT));
    EXPECT_EQ(commands.pressed, UiNavigationCommand::PREVIOUS);
    static_cast<void>(update(Events{}.key(Key::TAB, false).key(Key::LEFT_SHIFT, false)));

    // Space is the UI's while its context is active: gameplay does not jump.
    static_cast<void>(update(Events{}.key(Key::SPACE, true)));
    EXPECT_FALSE(input.frame().is_action_pressed(gfx::platform::ActionIdentifier::from_name("jump")));
    static_cast<void>(update(Events{}.key(Key::SPACE, false)));

    // A gamepad: the south button ACTIVATE, the d-pad and the left stick the moves, the right stick scrolling.
    const gfx::platform::GamepadHandle gamepad = GamepadIssuer::gamepad(0);
    static_cast<void>(update(Events{}.connected(gamepad)));
    commands = update(Events{}.button(gamepad, GamepadButton::SOUTH, true));
    EXPECT_EQ(commands.pressed, UiNavigationCommand::ACTIVATE);
    static_cast<void>(update(Events{}.button(gamepad, GamepadButton::SOUTH, false)));
    commands = update(Events{}.button(gamepad, GamepadButton::DPAD_RIGHT, true));
    EXPECT_EQ(commands.pressed, UiNavigationCommand::MOVE_RIGHT);
    static_cast<void>(update(Events{}.button(gamepad, GamepadButton::DPAD_RIGHT, false)));
    commands = update(Events{}.axis(gamepad, GamepadAxis::LEFT_Y, -0.9F));
    EXPECT_EQ(commands.down, UiNavigationCommand::MOVE_UP) << "y is down positive on a stick";
    commands = update(Events{}.axis(gamepad, GamepadAxis::LEFT_X, 0.95F));
    EXPECT_EQ(commands.down, UiNavigationCommand::MOVE_RIGHT) << "the larger axis";
    commands = update(Events{}.axis(gamepad, GamepadAxis::LEFT_X, 0.0F).axis(gamepad, GamepadAxis::LEFT_Y, 0.3F));
    EXPECT_EQ(commands.down, UiNavigationCommand::NONE) << "below the threshold";
    static_cast<void>(update(Events{}.axis(gamepad, GamepadAxis::LEFT_Y, 0.0F)));
    commands = update(Events{}.axis(gamepad, GamepadAxis::RIGHT_Y, 1.0F));
    EXPECT_GT(commands.scroll.y, 0.5F) << "scrolling: +y downwards";
    EXPECT_EQ(commands.scroll.x, 0.0F);
    commands = update(Events{}.button(gamepad, GamepadButton::LEFT_SHOULDER, true));
    EXPECT_EQ(commands.pressed, UiNavigationCommand::PREVIOUS_WINDOW);
}

TEST(UiNavigationInputTest, TheGamepadContextLeavesTheKeyboardToTheKeymap)
{
    gfx::platform::InputSystem input{GAMEPAD_SCHEMA};
    const gfx::platform::ContextActivation navigation =
        input.activate_context(gfx::platform::BindingContextIdentifier::from_name(gfx::ui::UI_NAVIGATION_CONTEXT));
    std::chrono::nanoseconds now = 1s;
    const auto update = [&](const Events& events)
    {
        now += 16ms;
        return gfx::ui::navigation_input(input.update(events.span(), now));
    };
    EXPECT_EQ(update(Events{}.key(Key::DOWN, true)).pressed, UiNavigationCommand::NONE);
    const gfx::platform::GamepadHandle gamepad = GamepadIssuer::gamepad(0);
    static_cast<void>(update(Events{}.connected(gamepad)));
    EXPECT_EQ(update(Events{}.button(gamepad, GamepadButton::DPAD_DOWN, true)).pressed, UiNavigationCommand::MOVE_DOWN);
    EXPECT_EQ(update(Events{}.button(gamepad, GamepadButton::EAST, true)).pressed, UiNavigationCommand::CANCEL);
}

TEST(UiNavigationInputTest, ASchemaWithoutTheNavigationActionsIsRefused)
{
    constexpr auto PLAIN = gfx::platform::make_input_schema(gfx::platform::actions(gfx::platform::action("jump")));
    gfx::platform::InputSystem input{PLAIN};
    EXPECT_THROW(static_cast<void>(gfx::ui::navigation_input(input.update({}))), gfx::InvalidUsageError);
}

} // namespace
