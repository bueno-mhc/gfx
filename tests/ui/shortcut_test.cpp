// Keyboard shortcuts of UiFrame driven by synthetic input: chords by the layout's symbols with exact modifiers,
// routing (the focused window and the windows it is in, global routes after them, the active item's, always), a
// text field being edited keeping its chords, repeats, items activated by set_next_item_shortcut() (buttons, check
// boxes), the text of chords, and the routes' capacity.
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
using gfx::tests::has_issue;
using gfx::tests::UiHarness;

constexpr KeyChord CONTROL_S{.key = KeyCode::S, .modifiers = KeyModifier::CONTROL};
constexpr KeyChord CONTROL_Z{.key = KeyCode::Z, .modifiers = KeyModifier::CONTROL};

/// Two windows "Left" and "Right" side by side, "Left" focused from the second frame (a window appearing takes the
/// focus); @p left and @p right run inside them.
template<class Left, class Right>
auto
two_windows(Left&& left,
            Right&& right)
{
    return [left = std::forward<Left>(left), right = std::forward<Right>(right), frames = 0](UiFrame& frame) mutable
    {
        frame.set_next_window_position({.x = 20, .y = 20}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size({.x = 300, .y = 300}, UiCondition::FIRST_USE_EVER);
        if (++frames == 2)
        {
            frame.set_next_window_focus();
        }
        if (frame.begin_window("Left"))
        {
            left(frame);
        }
        frame.end_window();
        frame.set_next_window_position({.x = 400, .y = 20}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size({.x = 300, .y = 300}, UiCondition::FIRST_USE_EVER);
        if (frame.begin_window("Right"))
        {
            right(frame);
        }
        frame.end_window();
    };
}

/// Presses Control+@p letter (a US layout's key, whose symbol is the letter) in the next frame.
void
control(UiHarness& ui,
        char letter)
{
    ui.modifiers(KeyModifier::CONTROL);
    ui.shortcut(letter);
}

// ===================================================================================================== routing

TEST(UiShortcutTest, AChordGoesToTheFocusedWindowWithExactModifiers)
{
    UiHarness ui;
    std::uint32_t left = 0;
    std::uint32_t right = 0;
    auto body = two_windows([&](UiFrame& frame) { left += frame.shortcut(CONTROL_S) ? 1U : 0U; },
                            [&](UiFrame& frame) { right += frame.shortcut(CONTROL_S) ? 1U : 0U; });
    ui.frames(3, body);
    control(ui, 's');
    ui.frame(body);
    EXPECT_EQ(left, 1U);
    EXPECT_EQ(right, 0U);

    // Other modifiers, or none: not the chord.
    ui.modifiers(KeyModifier::CONTROL | KeyModifier::SHIFT);
    ui.shortcut('s');
    ui.frame(body);
    ui.modifiers(KeyModifier::NONE);
    ui.shortcut('s');
    ui.frame(body);
    EXPECT_EQ(left, 1U);

    // Either Control key; once per press (repeats need ShortcutFlag::REPEAT).
    ui.modifiers(KeyModifier::RIGHT_CONTROL);
    ui.shortcut('s');
    ui.frame(body);
    EXPECT_EQ(left, 2U);
    ui.key_code(Key::S, KeyCode::S, true, true);
    ui.frame(body);
    EXPECT_EQ(left, 2U);

    // The other window focused: its call has the chord (from the frame after its route was decided).
    ui.click(500, 200, body);
    ui.frame(body);
    control(ui, 's');
    ui.frame(body);
    EXPECT_EQ(left, 2U);
    EXPECT_EQ(right, 1U);
}

TEST(UiShortcutTest, TheWindowOfTheFocusedItemWinsOverTheWindowsItIsIn)
{
    UiHarness ui;
    std::uint32_t parent = 0;
    std::uint32_t child = 0;
    auto body = two_windows([&](UiFrame& frame)
    {
        parent += frame.shortcut(CONTROL_S) ? 1U : 0U;
        if (frame.begin_child("Panel", {.x = 200, .y = 100}, ChildFlag::BORDERS))
        {
            static_cast<void>(frame.button("Inside"));
            child += frame.shortcut(CONTROL_S) ? 1U : 0U;
        }
        frame.end_child();
    }, [](UiFrame&) {});
    ui.frames(3, body);

    // No item focused: the window itself.
    control(ui, 's');
    ui.frame(body);
    EXPECT_EQ(parent, 1U);
    EXPECT_EQ(child, 0U);

    // The child's button focused: the child (nearer the focus) has it.
    ui.modifiers(KeyModifier::NONE);
    ui.tap(Key::TAB);
    ui.frames(3, body);
    control(ui, 's');
    ui.frame(body);
    EXPECT_EQ(parent, 1U);
    EXPECT_EQ(child, 1U);
}

TEST(UiShortcutTest, GlobalRoutesComeAfterFocusedOnesAndAlwaysRoutesAlwaysFire)
{
    UiHarness ui;
    std::uint32_t focused = 0;
    std::uint32_t global = 0;
    std::uint32_t always = 0;
    bool route_focused = true;
    auto body = two_windows([&](UiFrame& frame)
    {
        if (route_focused)
        {
            focused += frame.shortcut(CONTROL_S) ? 1U : 0U;
        }
    }, [&](UiFrame& frame)
    {
        global += frame.shortcut(CONTROL_S, ShortcutFlag::ROUTE_GLOBAL) ? 1U : 0U;
        always += frame.shortcut(CONTROL_S, ShortcutFlag::ROUTE_ALWAYS) ? 1U : 0U;
    });
    ui.frames(3, body);
    control(ui, 's');
    ui.frame(body);
    EXPECT_EQ(focused, 1U);
    EXPECT_EQ(global, 0U);
    EXPECT_EQ(always, 1U);

    // Nothing routed from the focused window: the global route has it.
    route_focused = false;
    ui.modifiers(KeyModifier::NONE);
    ui.frames(2, body);
    control(ui, 's');
    ui.frame(body);
    EXPECT_EQ(global, 1U);
    EXPECT_EQ(always, 2U);
}

TEST(UiShortcutTest, AnActiveRouteFiresOnlyWhileItsItemIsActiveAndRepeatsWhenAsked)
{
    UiHarness ui;
    std::uint32_t active = 0;
    std::uint32_t repeated = 0;
    UiRectangle button{};
    auto body = two_windows([&](UiFrame& frame)
    {
        static_cast<void>(frame.button("Hold me"));
        button = frame.item_rectangle();
        active += frame.shortcut({.key = KeyCode::R}, ShortcutFlag::ROUTE_ACTIVE) ? 1U : 0U;
        repeated += frame.shortcut({.key = KeyCode::T}, ShortcutFlag::REPEAT) ? 1U : 0U;
    }, [](UiFrame&) {});
    ui.frames(3, body);
    ui.tap_code(Key::R, KeyCode::R);
    ui.frame(body);
    EXPECT_EQ(active, 0U);

    // Held with the mouse: the button is active.
    ui.move((button.left + button.right) * 0.5F, (button.top + button.bottom) * 0.5F);
    ui.frame(body);
    ui.press();
    ui.frames(2, body);
    ui.tap_code(Key::R, KeyCode::R);
    ui.frame(body);
    ui.release();
    ui.frame(body);
    EXPECT_EQ(active, 1U);

    // REPEAT: true on each repeat too.
    ui.key_code(Key::T, KeyCode::T, true);
    ui.frame(body);
    ui.key_code(Key::T, KeyCode::T, true, true);
    ui.frame(body);
    ui.key_code(Key::T, KeyCode::T, true, true);
    ui.key_code(Key::T, KeyCode::T, false);
    ui.frame(body);
    EXPECT_EQ(repeated, 3U);
}

TEST(UiShortcutTest, AFieldBeingEditedKeepsTheChordsItEditsWith)
{
    UiHarness ui;
    std::string text = "abc";
    std::uint32_t undo = 0;
    std::uint32_t save = 0;
    UiRectangle field{};
    auto body = two_windows([&](UiFrame& frame)
    {
        static_cast<void>(frame.input_text("Field", text));
        field = frame.item_rectangle();
        undo += frame.shortcut(CONTROL_Z) ? 1U : 0U;
        save += frame.shortcut(CONTROL_S) ? 1U : 0U;
    }, [](UiFrame&) {});
    ui.frames(3, body);
    control(ui, 'z');
    ui.frame(body);
    EXPECT_EQ(undo, 1U);

    // Editing: Control+Z undoes typing in the field, Control+S is still the window's.
    ui.modifiers(KeyModifier::NONE);
    ui.click(field.right - 4, (field.top + field.bottom) * 0.5F, body);
    ui.type("d");
    ui.frame(body);
    control(ui, 'z');
    ui.frame(body);
    EXPECT_EQ(undo, 1U);
    control(ui, 's');
    ui.frame(body);
    EXPECT_EQ(save, 1U);
}

// ===================================================================================================== items

TEST(UiShortcutTest, AnItemsShortcutActivatesIt)
{
    UiHarness ui;
    std::uint32_t saved = 0;
    std::uint32_t other = 0;
    bool enabled = false;
    auto body = two_windows([&](UiFrame& frame)
    {
        frame.set_next_item_shortcut(CONTROL_S);
        saved += frame.button("Save") ? 1U : 0U;
        frame.set_next_item_shortcut({.key = KeyCode::E, .modifiers = KeyModifier::ALT});
        static_cast<void>(frame.checkbox("Enabled", enabled));
    }, [&](UiFrame& frame)
    {
        // Not the focused window: its item's shortcut is not routed to it.
        frame.set_next_item_shortcut(CONTROL_S);
        other += frame.button("Save") ? 1U : 0U;
    });
    ui.frames(3, body);
    control(ui, 's');
    ui.frame(body);
    EXPECT_EQ(saved, 1U);
    EXPECT_EQ(other, 0U);
    ui.modifiers(KeyModifier::ALT);
    ui.shortcut('e');
    ui.frame(body);
    ui.modifiers(KeyModifier::NONE);
    EXPECT_TRUE(enabled);
    EXPECT_EQ(saved, 1U);
}

TEST(UiShortcutTest, ChordsAreWrittenAsMenusShowThem)
{
    static_assert(key_chord_text({.key = KeyCode::S, .modifiers = KeyModifier::CONTROL | KeyModifier::SHIFT}).view() == "Ctrl+Shift+S");
    static_assert(key_chord_text({.key = KeyCode::F5}).view() == "F5");
    static_assert(key_chord_text({.key = KeyCode::PAGE_DOWN, .modifiers = KeyModifier::ALT}).view() == "Alt+PageDown");
    static_assert(key_chord_text({.key = KeyCode::DELETE_FORWARD}).view() == "Delete");
    static_assert(key_chord_text({.key = KeyCode::SPACE, .modifiers = KeyModifier::SUPER}).view() == "Super+Space");
    static_assert(key_chord_text({.key = KeyCode::EQUALS, .modifiers = KeyModifier::LEFT_CONTROL}).view() == "Ctrl+=");
    static_assert(key_chord_text({.key = static_cast<KeyCode>(0xE9)}).view() == "é");
    static_assert(key_chord_text({}).view().empty());
}

TEST(UiShortcutTest, MoreChordsThanTheRoutesHoldAreReported)
{
    UiHarness ui{true, {.limits = {.shortcut_routes = 4}}};
    auto body = two_windows([](UiFrame& frame)
    {
        for (const KeyCode key : {KeyCode::A, KeyCode::B, KeyCode::C, KeyCode::D, KeyCode::E})
        {
            static_cast<void>(frame.shortcut({.key = key, .modifiers = KeyModifier::CONTROL}));
        }
    }, [](UiFrame&) {});
    const UiFrameReport report = ui.frame(body);
    EXPECT_TRUE(has_issue(report, UiFrameProblem::CAPACITY_EXCEEDED));
}

} // namespace
