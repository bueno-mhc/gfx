// Keyboard and gamepad navigation of UiFrame driven by synthetic input: the keymap's commands (the arrows move the
// focus to the nearest item in their direction, Tab steps through the focus order), the same commands from a
// gamepad (UiFrameInput::navigation, held moves repeating), the keymap turned off, scrolling to the item the focus
// moves to and with a stick, widgets that use the arrows themselves (sliders), the menu bar (Alt, Left and Right
// between its menus), window cycling (Control+Tab), context menus from the keyboard, and text fields (arrival by
// an arrow does not edit, INPUT does, a gamepad confirms and cancels).
#include <gtest/gtest.h>

import std;
import gfx.ui;
import gfx.tests.ui_context_support;

namespace
{

using namespace gfx::ui;
using gfx::Key;
using gfx::KeyCode;
using gfx::KeyModifier;
using gfx::Vector2;
using gfx::tests::center;
using gfx::tests::UiHarness;

/// Runs @p body inside a focused window "Navigation" at (20, 20), 420 by 400.
template<class Body>
auto
in_window(Body&& body,
          UiWindowFlag flags = UiWindowFlag::NONE,
          Vector2 size = {.x = 420, .y = 400})
{
    return [body = std::forward<Body>(body), flags, size, first = true](UiFrame& frame) mutable
    {
        frame.set_next_window_position({.x = 20, .y = 20}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size(size, UiCondition::FIRST_USE_EVER);
        if (std::exchange(first, false))
        {
            frame.set_next_window_focus();
        }
        if (frame.begin_window("Navigation", flags))
        {
            body(frame);
        }
        frame.end_window();
    };
}

/// Taps @p key, then runs the frame that moves the focus and the one that shows where it is.
template<class Body>
void
press(UiHarness& ui,
      Key key,
      Body& body)
{
    ui.tap(key);
    ui.frame(body);
    ui.frame(body);
}

/// Sends navigation command @p command as a gamepad would, then runs two frames.
template<class Body>
void
command(UiHarness& ui,
        UiNavigationCommand pressed,
        Body& body)
{
    ui.navigate(pressed);
    ui.frame(body);
    ui.frame(body);
}

/// Six buttons in two rows of three ("A" to "F"); the focused one's label in @p focused, the pressed ones in
/// @p pressed.
auto
grid(std::string& focused,
     std::string& pressed)
{
    return [&focused, &pressed](UiFrame& frame)
    {
        focused.clear();
        for (const std::string_view label : {"A", "B", "C", "D", "E", "F"})
        {
            if (label != "A" && label != "D")
            {
                frame.same_line();
            }
            if (frame.button(label, {.x = 60, .y = 30}))
            {
                pressed.append(label);
            }
            if (frame.is_item_focused())
            {
                focused = label;
            }
        }
    };
}

// ===================================================================================================== moves

TEST(UiNavigationTest, ArrowsMoveTheFocusToTheNearestItemInTheirDirection)
{
    UiHarness ui;
    std::string focused;
    std::string pressed;
    auto body = in_window(grid(focused, pressed));
    ui.frames(3, body);

    // Nothing focused: Down goes to the first item; then each arrow to the nearest item its way (none: it stays).
    press(ui, Key::DOWN, body);
    EXPECT_EQ(focused, "A");
    press(ui, Key::RIGHT, body);
    EXPECT_EQ(focused, "B");
    press(ui, Key::RIGHT, body);
    EXPECT_EQ(focused, "C");
    press(ui, Key::RIGHT, body);
    EXPECT_EQ(focused, "C") << "nothing on the right of C: no wrapping in a window";
    press(ui, Key::DOWN, body);
    EXPECT_EQ(focused, "F");
    press(ui, Key::LEFT, body);
    EXPECT_EQ(focused, "E");
    press(ui, Key::UP, body);
    EXPECT_EQ(focused, "B");
    press(ui, Key::DOWN, body);
    press(ui, Key::LEFT, body);
    EXPECT_EQ(focused, "D");

    // Space presses the focused item; Escape drops the focus.
    press(ui, Key::SPACE, body);
    EXPECT_EQ(pressed, "D");
    press(ui, Key::ESCAPE, body);
    EXPECT_EQ(focused, "");
    EXPECT_FALSE(ui.context().wants_keyboard());

    // Nothing focused: Up goes to the last item.
    press(ui, Key::UP, body);
    EXPECT_EQ(focused, "F");
}

TEST(UiNavigationTest, TabStepsThroughTheFocusOrderAroundTheWindow)
{
    UiHarness ui;
    std::string focused;
    std::string pressed;
    auto body = in_window(grid(focused, pressed));
    ui.frames(3, body);
    std::string order;
    for (std::int32_t step = 0; step < 7; ++step)
    {
        press(ui, Key::TAB, body);
        order.append(focused);
    }
    EXPECT_EQ(order, "ABCDEFA");
    ui.modifiers(KeyModifier::SHIFT);
    press(ui, Key::TAB, body);
    ui.modifiers(KeyModifier::NONE);
    EXPECT_EQ(focused, "F");
}

TEST(UiNavigationTest, AGamepadNavigatesAndItsHeldMovesRepeat)
{
    UiHarness ui;
    std::string focused;
    std::string pressed;
    auto body = in_window(grid(focused, pressed));
    ui.frames(3, body);

    // The commands of UiFrameInput::navigation, as the keymap's.
    command(ui, UiNavigationCommand::MOVE_RIGHT, body);
    EXPECT_EQ(focused, "A");
    command(ui, UiNavigationCommand::MOVE_DOWN, body);
    EXPECT_EQ(focused, "D");
    command(ui, UiNavigationCommand::ACTIVATE, body);
    EXPECT_EQ(pressed, "D");
    EXPECT_TRUE(ui.context().wants_keyboard());

    // Held (a stick: held without a press): once at first, then again after the repeat delay, once per interval.
    ui.hold_navigation(UiNavigationCommand::MOVE_RIGHT);
    ui.frame(body);
    ui.frame(body);
    EXPECT_EQ(focused, "E");
    ui.frames(3, body);
    EXPECT_EQ(focused, "E") << "not before the repeat delay";
    ui.wait(DEFAULT_REPEAT_DELAY_SECONDS);
    ui.frame(body);
    ui.frame(body);
    EXPECT_EQ(focused, "F");
    ui.hold_navigation(UiNavigationCommand::NONE);
    ui.frames(2, body);

    // CANCEL drops the focus.
    command(ui, UiNavigationCommand::CANCEL, body);
    EXPECT_EQ(focused, "");
}

TEST(UiNavigationTest, WithoutTheKeymapOnlyTheNavigationInputMovesTheFocus)
{
    UiHarness ui{true, {.input = {.keyboard_navigation = false}}};
    std::string focused;
    std::string pressed;
    auto body = in_window(grid(focused, pressed));
    ui.frames(3, body);
    press(ui, Key::TAB, body);
    press(ui, Key::DOWN, body);
    EXPECT_EQ(focused, "");
    command(ui, UiNavigationCommand::NEXT, body);
    EXPECT_EQ(focused, "A");
    press(ui, Key::SPACE, body);
    EXPECT_EQ(pressed, "");
    command(ui, UiNavigationCommand::ACTIVATE, body);
    EXPECT_EQ(pressed, "A");
}

TEST(UiNavigationTest, TheWindowScrollsToTheItemTheFocusMovesTo)
{
    UiHarness ui;
    std::int32_t focused = -1;
    float scroll = 0;
    UiRectangle item{};
    UiRectangle window{};
    auto body = in_window([&](UiFrame& frame)
    {
        focused = -1;
        for (std::int32_t index = 0; index < 40; ++index)
        {
            frame.push_id(index);
            static_cast<void>(frame.button("Row"));
            if (frame.is_item_focused())
            {
                focused = index;
                item = frame.item_rectangle();
            }
            frame.pop_id();
        }
        scroll = frame.scroll_y();
        window = {.left = frame.window_position().x, .top = frame.window_position().y, .right = frame.window_position().x + frame.window_size().x,
                  .bottom = frame.window_position().y + frame.window_size().y};
    }, UiWindowFlag::NONE, {.x = 300, .y = 200});
    ui.frames(3, body);
    for (std::int32_t step = 0; step < 20; ++step)
    {
        press(ui, Key::DOWN, body);
    }
    ui.frame(body);
    EXPECT_EQ(focused, 19);
    EXPECT_GT(scroll, 0.0F);
    EXPECT_GE(item.top, window.top);
    EXPECT_LE(item.bottom, window.bottom);

    // A stick scrolls the focused window.
    const float before = scroll;
    ui.scroll_navigation({.x = 0, .y = 1});
    ui.frames(10, body);
    ui.scroll_navigation({});
    ui.frame(body);
    EXPECT_GT(scroll, before);
}

// ===================================================================================================== widgets

TEST(UiNavigationTest, ASliderUsesLeftAndRightWhileUpLeavesIt)
{
    UiHarness ui;
    std::int32_t value = 5;
    bool button_focused = false;
    bool slider_focused = false;
    auto body = in_window([&](UiFrame& frame)
    {
        static_cast<void>(frame.button("Above"));
        button_focused = frame.is_item_focused();
        frame.slider("Value", value, 0, 10);
        slider_focused = frame.is_item_focused();
    });
    ui.frames(3, body);
    press(ui, Key::TAB, body);
    press(ui, Key::DOWN, body);
    ASSERT_TRUE(slider_focused);
    press(ui, Key::RIGHT, body);
    press(ui, Key::RIGHT, body);
    EXPECT_EQ(value, 7);
    EXPECT_TRUE(slider_focused) << "the slider used Right";
    command(ui, UiNavigationCommand::MOVE_LEFT, body);
    EXPECT_EQ(value, 6) << "a gamepad's moves step it too";
    press(ui, Key::UP, body);
    EXPECT_TRUE(button_focused);
    EXPECT_EQ(value, 6);
}

TEST(UiNavigationTest, ArrivingByAnArrowDoesNotEditAFieldInputAndAGamepadDo)
{
    UiHarness ui;
    std::string text = "abc";
    bool field_focused = false;
    auto body = in_window([&](UiFrame& frame)
    {
        static_cast<void>(frame.button("Above"));
        static_cast<void>(frame.input_text("Field", text));
        field_focused = frame.is_item_focused();
    });
    ui.frames(3, body);
    press(ui, Key::TAB, body);
    press(ui, Key::DOWN, body);
    ASSERT_TRUE(field_focused);
    EXPECT_FALSE(ui.context().wants_text_input());

    // Return (INPUT) edits it (all selected), the arrows are its own; a gamepad's ACTIVATE confirms.
    press(ui, Key::RETURN, body);
    ASSERT_TRUE(ui.context().wants_text_input());
    press(ui, Key::UP, body);
    press(ui, Key::LEFT, body);
    EXPECT_TRUE(field_focused) << "Up and Left did not move the focus";
    ui.type("d");
    ui.frame(body);
    command(ui, UiNavigationCommand::ACTIVATE, body);
    EXPECT_FALSE(ui.context().wants_text_input());
    EXPECT_EQ(text, "dabc") << "Left went to the start of the selection, then d was typed";

    // A gamepad's ACTIVATE edits it again; its CANCEL reverts.
    command(ui, UiNavigationCommand::ACTIVATE, body);
    ASSERT_TRUE(ui.context().wants_text_input());
    ui.type("zzz");
    ui.frame(body);
    command(ui, UiNavigationCommand::CANCEL, body);
    EXPECT_FALSE(ui.context().wants_text_input());
    EXPECT_EQ(text, "dabc");
}

TEST(UiNavigationTest, TheContextMenuKeyOpensTheFocusedItemsContextMenu)
{
    UiHarness ui;
    bool open = false;
    bool first_focused = false;
    auto body = in_window([&](UiFrame& frame)
    {
        static_cast<void>(frame.button("Item"));
        open = frame.begin_popup_context_item();
        if (open)
        {
            static_cast<void>(frame.menu_item("Rename"));
            first_focused = frame.is_item_focused();
            static_cast<void>(frame.menu_item("Delete"));
            frame.end_popup();
        }
    });
    ui.frames(3, body);
    press(ui, Key::TAB, body);
    press(ui, Key::APPLICATION, body);
    EXPECT_TRUE(open);
    EXPECT_TRUE(first_focused);
    press(ui, Key::ESCAPE, body);
    EXPECT_FALSE(open);
    ui.modifiers(KeyModifier::SHIFT);
    press(ui, Key::F10, body);
    ui.modifiers(KeyModifier::NONE);
    EXPECT_TRUE(open) << "Shift+F10 too";
}

// ===================================================================================================== menus and windows

TEST(UiNavigationTest, AltAloneMovesTheFocusToTheMenuBarAndBack)
{
    UiHarness ui;
    bool body_focused = false;
    bool file_focused = false;
    bool file_open = false;
    bool edit_open = false;
    bool new_focused = false;
    bool undo_focused = false;
    auto body = in_window([&](UiFrame& frame)
    {
        if (frame.begin_menu_bar())
        {
            file_open = frame.begin_menu("File");
            file_focused = frame.is_item_focused();
            if (file_open)
            {
                static_cast<void>(frame.menu_item("New"));
                new_focused = frame.is_item_focused();
                static_cast<void>(frame.menu_item("Open"));
                frame.end_menu();
            }
            edit_open = frame.begin_menu("Edit");
            if (edit_open)
            {
                static_cast<void>(frame.menu_item("Undo"));
                undo_focused = frame.is_item_focused();
                frame.end_menu();
            }
            frame.end_menu_bar();
        }
        static_cast<void>(frame.button("Body"));
        body_focused = frame.is_item_focused();
    }, UiWindowFlag::MENU_BAR);
    ui.frames(3, body);
    press(ui, Key::TAB, body);
    press(ui, Key::TAB, body);
    press(ui, Key::TAB, body);
    ASSERT_TRUE(body_focused);

    // Alt pressed and released alone: the first menu; again: back to the body.
    ui.key(Key::LEFT_ALT, true);
    ui.key(Key::LEFT_ALT, false);
    ui.frames(2, body);
    EXPECT_TRUE(file_focused);
    ui.key(Key::LEFT_ALT, true);
    ui.key(Key::LEFT_ALT, false);
    ui.frames(2, body);
    EXPECT_TRUE(body_focused);

    // Alt with another key is not alone.
    ui.key(Key::LEFT_ALT, true);
    ui.tap(Key::F);
    ui.key(Key::LEFT_ALT, false);
    ui.frames(2, body);
    EXPECT_TRUE(body_focused);

    // In the bar: Down opens File (New focused); Right goes to Edit's menu, Left back to File's.
    ui.key(Key::LEFT_ALT, true);
    ui.key(Key::LEFT_ALT, false);
    ui.frames(2, body);
    press(ui, Key::DOWN, body);
    EXPECT_TRUE(file_open);
    EXPECT_TRUE(new_focused);
    press(ui, Key::RIGHT, body);
    ui.frame(body);
    EXPECT_FALSE(file_open);
    EXPECT_TRUE(edit_open);
    EXPECT_TRUE(undo_focused);
    press(ui, Key::LEFT, body);
    ui.frame(body);
    EXPECT_TRUE(file_open);
    EXPECT_FALSE(edit_open);

    // Escape closes the menu, Escape again leaves the bar.
    press(ui, Key::ESCAPE, body);
    EXPECT_FALSE(file_open);
    EXPECT_TRUE(file_focused);
    press(ui, Key::ESCAPE, body);
    EXPECT_TRUE(body_focused);
}

TEST(UiNavigationTest, ControlTabCyclesTheWindowsAndGivesBackTheirFocusedItem)
{
    UiHarness ui;
    std::array<bool, 2> focused_windows{};
    std::array<bool, 2> focused_buttons{};
    std::int32_t frames = 0;
    const auto body = [&](UiFrame& frame)
    {
        ++frames;
        for (std::size_t index = 0; index < 2; ++index)
        {
            frame.set_next_window_position({.x = 20 + 300.0F * static_cast<float>(index), .y = 20}, UiCondition::FIRST_USE_EVER);
            frame.set_next_window_size({.x = 250, .y = 200}, UiCondition::FIRST_USE_EVER);
            if (index == 0 && frames == 2)
            {
                frame.set_next_window_focus();
            }
            if (frame.begin_window(index == 0 ? "One" : "Two"))
            {
                focused_windows[index] = frame.is_window_focused();
                static_cast<void>(frame.button("First"));
                static_cast<void>(frame.button("Second"));
                focused_buttons[index] = frame.is_item_focused();
            }
            frame.end_window();
        }
    };
    ui.frames(3, body);
    press(ui, Key::TAB, body);
    press(ui, Key::TAB, body);
    ASSERT_TRUE(focused_buttons[0]);
    ui.modifiers(KeyModifier::CONTROL);
    press(ui, Key::TAB, body);
    EXPECT_TRUE(focused_windows[1]);
    EXPECT_FALSE(focused_windows[0]);
    press(ui, Key::TAB, body);
    ui.modifiers(KeyModifier::NONE);
    EXPECT_TRUE(focused_windows[0]);
    EXPECT_TRUE(focused_buttons[0]) << "One's Second again";

    // A gamepad's shoulders do the same.
    command(ui, UiNavigationCommand::PREVIOUS_WINDOW, body);
    EXPECT_TRUE(focused_windows[1]);
}

} // namespace
