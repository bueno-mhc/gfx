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

constexpr auto SCHEMA = make_input_schema(
    actions(action("jump"), action("move", ActionValueType::AXIS_2D), action("undo"), action("look", ActionValueType::AXIS_2D)),
    context("gameplay",
            bind("jump"_action, Key::SPACE),
            bind("move"_action, composite_2d(Key::W, Key::S, Key::A, Key::D)),
            bind("undo"_action, chord(KeyCode::Z, KeyModifier::CONTROL)),
            bind("look"_action, stick(GamepadAxis::RIGHT_X, GamepadAxis::RIGHT_Y))),
    context("menu", {.priority = 10, .consumes_input = true, .blocks_lower_contexts = true, .reads_keyboard_during_text_input = false},
            bind("jump"_action, Key::RETURN)));

/// The message of the InvalidUsageError @p function throws (empty when it does not throw).
template<class Function>
auto
usage_error_of(Function function) -> std::string
{
    try
    {
        function();
    }
    catch (const gfx::InvalidUsageError& error)
    {
        return error.what();
    }
    return {};
}

TEST(BindingStorageTest, NothingIsSavedWhileBindingsAreTheDefaults)
{
    const InputSystem input{SCHEMA};
    const std::string saved = input.save_bindings();
    EXPECT_NE(saved.find("\"format\": \"gfx.input.bindings\""), std::string::npos) << saved;
    EXPECT_NE(saved.find("\"version\": 1"), std::string::npos) << saved;
    EXPECT_NE(saved.find("\"players\": []"), std::string::npos) << saved;
}

TEST(BindingStorageTest, SavedBindingsRoundTripByName)
{
    // Rebind with every kind of field: modifiers, dead zone, curve, inversion, scale, swizzle, triggers,
    // an unnamed key code, a removed list, and a second player.
    InputSystem input{SCHEMA, {.players = 2, .gamepad_assignment = GamepadAssignment::AUTOMATIC, .actuation_threshold = 0.5F}};
    const Binding hold = single(GamepadButton::NORTH).held_for(750ms);
    const Binding euro = chord(static_cast<KeyCode>(0x20AC), KeyModifier::RIGHT_ALT, false);
    const Binding look = stick(GamepadAxis::LEFT_X, GamepadAxis::LEFT_Y)
                             .with_deadzone(DeadzoneKind::AXIAL, 0.125F, 0.875F)
                             .with_curve(ResponseCurve::POWER, 1.75F)
                             .inverted(true, false)
                             .scaled(gfx::Vector2{.x = 2.5F, .y = 0.5F})
                             .swizzled()
                             .normalized();
    input.rebind({.context = "gameplay"_context, .action = "jump"_action, .slot = 0}, hold);
    input.add_binding("gameplay"_context, "undo"_action, euro);
    input.rebind({.context = "gameplay"_context, .action = "look"_action, .slot = 0}, look);
    input.remove_binding({.context = "menu"_context, .action = "jump"_action, .slot = 0});
    input.add_binding("gameplay"_context, "jump"_action, single(MouseButton::BACK).tapped_within(150ms), PlayerIdentifier{1});
    const std::string saved = input.save_bindings();
    EXPECT_NE(saved.find("\"KEY_CODE\": \"U+20AC\""), std::string::npos) << saved;
    EXPECT_NE(saved.find("\"modifiers\": \"RIGHT_ALT\""), std::string::npos) << saved;
    EXPECT_EQ(saved.find("\"action\": \"move\""), std::string::npos) << "unchanged lists are not saved\n" << saved;

    // A fresh system loads them: every binding comes back equal, the rest stays default.
    InputSystem restored{SCHEMA, {.players = 2, .gamepad_assignment = GamepadAssignment::AUTOMATIC, .actuation_threshold = 0.5F}};
    restored.load_bindings(saved);
    EXPECT_EQ(restored.bindings("gameplay"_context, "jump"_action), std::vector<Binding>{hold});
    EXPECT_EQ(restored.bindings("gameplay"_context, "undo"_action), (std::vector<Binding>{chord(KeyCode::Z, KeyModifier::CONTROL), euro}));
    EXPECT_EQ(restored.bindings("gameplay"_context, "look"_action), std::vector<Binding>{look});
    EXPECT_TRUE(restored.bindings("menu"_context, "jump"_action).empty());
    EXPECT_EQ(restored.bindings("gameplay"_context, "move"_action), input.bindings("gameplay"_context, "move"_action));
    EXPECT_EQ(restored.bindings("gameplay"_context, "jump"_action, PlayerIdentifier{1}).size(), 2U);
    EXPECT_EQ(restored.save_bindings(), saved);

    // Loading replaces everything: the defaults come back for lists the document does not hold.
    restored.load_bindings(InputSystem{SCHEMA}.save_bindings());
    EXPECT_EQ(restored.bindings("gameplay"_context, "jump"_action), std::vector<Binding>{single(Key::SPACE)});
}

/// Binds "jump" to every enumerator of @p Enumeration (but @p skipped) with @p make.
template<class Enumeration, class Make>
void
bind_every_enumerator(InputSystem& input,
                      std::optional<Enumeration> skipped,
                      Make make)
{
    for (const auto& entry : gfx::EnumerationTraits<Enumeration>::NAMES)
    {
        if (entry.value != skipped)
        {
            input.add_binding("gameplay"_context, "jump"_action, make(entry.value));
        }
    }
}

TEST(BindingStorageTest, EveryInputNameRoundTrips)
{
    // Every key, key code, mouse button and axis, gamepad button and axis, saved by name and read back.
    InputSystem input{SCHEMA};
    const auto single_binding = [](auto source) { return single(source); };
    bind_every_enumerator<Key>(input, Key::UNKNOWN, single_binding);
    bind_every_enumerator<KeyCode>(input, KeyCode::UNKNOWN, single_binding);
    bind_every_enumerator<MouseButton>(input, std::nullopt, single_binding);
    bind_every_enumerator<MouseAxis>(input, std::nullopt, single_binding);
    bind_every_enumerator<GamepadButton>(input, std::nullopt, single_binding);
    bind_every_enumerator<GamepadAxis>(input, std::nullopt, single_binding);
    constexpr KeyModifier LOCKS = KeyModifier::CAPS_LOCK | KeyModifier::NUM_LOCK | KeyModifier::SCROLL_LOCK;
    for (const auto& entry : gfx::EnumerationTraits<KeyModifier>::NAMES)
    {
        if (entry.value != KeyModifier::NONE && is_empty(entry.value & LOCKS))
        {
            input.add_binding("gameplay"_context, "jump"_action, chord(Key::S, entry.value));
        }
    }
    const std::vector<Binding> bound = input.bindings("gameplay"_context, "jump"_action);
    ASSERT_GT(bound.size(), 300U);

    InputSystem restored{SCHEMA};
    restored.load_bindings(input.save_bindings());
    EXPECT_EQ(restored.bindings("gameplay"_context, "jump"_action), bound);
}

TEST(BindingStorageTest, LoadingIsAllOrNothingAndListsEveryProblem)
{
    // Several problems at once: unknown context, unknown action, unknown key, a stick on a button,
    // an unknown field, and a player out of range.
    InputSystem input{SCHEMA};
    input.rebind({.context = "gameplay"_context, .action = "jump"_action, .slot = 0}, single(Key::J));
    constexpr std::string_view DOCUMENT = R"({
        "format": "gfx.input.bindings",
        "version": 1,
        "players": [
            {"player": 0, "bindings": [
                {"context": "cutscene", "action": "jump", "bindings": []},
                {"context": "gameplay", "action": "fly", "bindings": []},
                {"context": "gameplay", "action": "jump", "bindings": [{"kind": "BUTTON", "sources": [{"KEY": "SPCE"}]}]},
                {"context": "gameplay", "action": "undo", "bindings": [{"kind": "STICK", "sources": [{"GAMEPAD_AXIS": "LEFT_X"}, {"GAMEPAD_AXIS": "LEFT_Y"}]}]},
                {"context": "gameplay", "action": "move", "bindings": [{"kind": "COMPOSITE_2D", "sources": [], "deadzon": 1}]}
            ]},
            {"player": 3, "bindings": [{"context": "gameplay", "action": "jump", "bindings": []}]}
        ]
    })";
    const std::string message = usage_error_of([&] { input.load_bindings(DOCUMENT); });
    EXPECT_NE(message.find("6 problem(s)"), std::string::npos) << message;
    EXPECT_NE(message.find("no binding context 'cutscene'"), std::string::npos) << message;
    EXPECT_NE(message.find("no action 'fly'"), std::string::npos) << message;
    EXPECT_NE(message.find("players[0].bindings[2].bindings[0].sources[0] names no KEY input (\"SPCE\")"), std::string::npos) << message;
    EXPECT_NE(message.find("INCOMPATIBLE_BINDING"), std::string::npos) << message;
    EXPECT_NE(message.find("unknown field \"deadzon\""), std::string::npos) << message;
    EXPECT_NE(message.find("Player(3)"), std::string::npos) << message;

    // Nothing changed.
    EXPECT_EQ(input.bindings("gameplay"_context, "jump"_action), std::vector<Binding>{single(Key::J)});
}

TEST(BindingStorageTest, MalformedDocumentsAreRefused)
{
    InputSystem input{SCHEMA};
    EXPECT_NE(usage_error_of([&] { input.load_bindings("{\"format\": "); }).find("malformed JSON at byte"), std::string::npos);
    EXPECT_NE(usage_error_of([&] { input.load_bindings("[]"); }).find("the document is not an object"), std::string::npos);
    EXPECT_NE(usage_error_of([&] { input.load_bindings(R"({"format": "other", "version": 1, "players": []})"); }).find("format is \"other\""),
              std::string::npos);
    EXPECT_NE(usage_error_of([&] { input.load_bindings(R"({"format": "gfx.input.bindings", "version": 2, "players": []})"); })
                  .find("this build reads version 1"),
              std::string::npos);
    EXPECT_NE(usage_error_of([&] { input.load_bindings(R"({"format": "gfx.input.bindings", "version": 1.5, "players": []})"); })
                  .find("not an integer"),
              std::string::npos);
}

TEST(BindingStorageTest, LoadedBindingsDriveActions)
{
    // The saved jump binding (J) works after loading into a new system.
    InputSystem original{SCHEMA};
    original.rebind({.context = "gameplay"_context, .action = "jump"_action, .slot = 0}, single(Key::J));
    InputSystem input{SCHEMA};
    input.load_bindings(original.save_bindings());
    ContextActivation gameplay = input.activate_context("gameplay"_context);
    EXPECT_TRUE(input.update(Events{}.key_down(Key::J).span(), 16ms).is_action_pressed("jump"_action));
}

} // namespace
