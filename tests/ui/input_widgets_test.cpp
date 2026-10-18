// The input widgets of UiFrame driven by synthetic input: text fields (editing, motions, selection, the clipboard,
// undo, Return/Escape/Tab, filters, passwords, read-only fields, capacities, multi-line fields, the IME
// composition and the text input area), numbers (typed values, formats, step buttons, sliders, drags, typed input,
// components), colors (conversions, editors, pickers, swatches) and lists (combo boxes, list boxes, popups).
#include <gtest/gtest.h>

import std;
import gfx.ui;
import gfx.tests.ui_context_support;

namespace
{

using namespace gfx::ui;
using gfx::Key;
using gfx::KeyModifier;
using gfx::Vector2;
using gfx::tests::center;
using gfx::tests::has_issue;
using gfx::tests::UiHarness;

/// A window at a fixed place and size.
void
place(UiFrame& frame,
      Vector2 position = {.x = 20, .y = 20},
      Vector2 size = {.x = 420, .y = 400})
{
    frame.set_next_window_position(position, UiCondition::FIRST_USE_EVER);
    frame.set_next_window_size(size, UiCondition::FIRST_USE_EVER);
}

/// Runs @p body inside a placed window named "Widgets".
template<class Body>
auto
in_window(Body&& body)
{
    return [body = std::forward<Body>(body)](UiFrame& frame) mutable
    {
        place(frame);
        if (frame.begin_window("Widgets"))
        {
            body(frame);
        }
        frame.end_window();
    };
}

/// Clicks at the right end of @p field (the cursor goes after its text) and lets the release frame run; a second
/// apart from any earlier click, so it is never the second click of a double click.
template<class Body>
void
click_end(UiHarness& ui,
          const UiRectangle& field,
          Body& body)
{
    ui.wait(1.0);
    ui.click(field.right - 2, center(field).y, body);
}

// ===================================================================================================== text fields

TEST(UiTextFieldTest, AClickEditsTheFieldAndTypedTextGoesAtTheCursor)
{
    UiHarness ui;
    std::string name = "abc";
    UiRectangle field{};
    bool changed = false;
    auto body = in_window([&](UiFrame& frame)
    {
        changed = frame.input_text("Name", name);
        field = frame.item_rectangle();
    });
    ui.frames(2, body);
    EXPECT_FALSE(ui.context().wants_text_input());

    // A click edits it: text input starts at its cursor.
    click_end(ui, field, body);
    EXPECT_TRUE(ui.context().wants_text_input());
    EXPECT_TRUE(ui.services().is_text_input_active());
    const UiRectangle area = ui.services().text_input_area();
    EXPECT_GT(area.left, field.left);
    EXPECT_LT(area.left, field.right);
    EXPECT_GE(area.top, field.top);
    EXPECT_LE(area.bottom, field.bottom);

    // Typing inserts at the cursor; the cursor (and the text input area) moves on.
    ui.type("de");
    ui.frame(body);
    EXPECT_TRUE(changed);
    EXPECT_EQ(name, "abcde");
    ui.frame(body);
    EXPECT_GT(ui.services().text_input_area().left, area.left);
    ui.tap(Key::LEFT);
    ui.tap(Key::LEFT);
    ui.type("X");
    ui.frame(body);
    EXPECT_EQ(name, "abcXde");
    ui.tap(Key::HOME);
    ui.type("<");
    ui.frame(body);
    EXPECT_EQ(name, "<abcXde");
    ui.tap(Key::END);
    ui.tap(Key::BACKSPACE);
    ui.frame(body);
    EXPECT_EQ(name, "<abcXd");
    ui.tap(Key::HOME);
    ui.tap(Key::DELETE_FORWARD);
    ui.frame(body);
    EXPECT_EQ(name, "abcXd");
    ui.frame(body);
    EXPECT_FALSE(changed);
}

TEST(UiTextFieldTest, WordsSelectionTheClipboardAndUndoFollowTheShortcuts)
{
    UiHarness ui;
    std::string text = "hello world";
    UiRectangle field{};
    auto body = in_window([&](UiFrame& frame)
    {
        frame.input_text("Text", text);
        field = frame.item_rectangle();
    });
    ui.frames(2, body);
    click_end(ui, field, body);

    // Control+A, Control+C: everything copied.
    ui.modifiers(KeyModifier::CONTROL);
    ui.shortcut('a');
    ui.shortcut('c');
    ui.frame(body);
    EXPECT_EQ(ui.services().clipboard_text(), "hello world");

    // Control+Left to the last word, Shift+End selects it, Control+X cuts it (a frame's keys have no order:
    // one motion per frame).
    ui.tap(Key::END);
    ui.frame(body);
    ui.tap(Key::LEFT);
    ui.frame(body);
    ui.modifiers(KeyModifier::SHIFT);
    ui.tap(Key::END);
    ui.frame(body);
    ui.modifiers(KeyModifier::CONTROL);
    ui.shortcut('x');
    ui.frame(body);
    EXPECT_EQ(ui.services().clipboard_text(), "world");
    EXPECT_EQ(text, "hello ");

    // Two pastes, each an undo step; Control+Z undoes them and the cut; Control+Y redoes.
    ui.shortcut('v');
    ui.frame(body);
    ui.shortcut('v');
    ui.frame(body);
    EXPECT_EQ(text, "hello worldworld");
    ui.shortcut('z');
    ui.frame(body);
    EXPECT_EQ(text, "hello world");
    ui.shortcut('z');
    ui.frame(body);
    EXPECT_EQ(text, "hello ");
    ui.shortcut('z');
    ui.frame(body);
    EXPECT_EQ(text, "hello world");
    ui.shortcut('y');
    ui.frame(body);
    EXPECT_EQ(text, "hello ");

    // Control+Backspace erases the word before the cursor.
    ui.tap(Key::END);
    ui.tap(Key::BACKSPACE);
    ui.frame(body);
    EXPECT_EQ(text, "");
}

TEST(UiTextFieldTest, ADoubleClickSelectsAWordAndADragSelects)
{
    UiHarness ui;
    std::string text = "alpha beta gamma";
    UiRectangle field{};
    auto body = in_window([&](UiFrame& frame)
    {
        frame.input_text("Words", text);
        field = frame.item_rectangle();
    });
    ui.frames(2, body);

    // A double click near the start selects "alpha".
    const float x = field.left + 4 + 8;
    ui.click(x, center(field).y, body);
    ui.press();
    ui.frame(body);
    ui.release();
    ui.frame(body);
    ui.modifiers(KeyModifier::CONTROL);
    ui.shortcut('c');
    ui.frame(body);
    EXPECT_EQ(ui.services().clipboard_text(), "alpha");

    // A drag from the start to the end selects everything.
    ui.modifiers(KeyModifier::NONE);
    ui.wait(1.0);
    ui.move(field.left + 1, center(field).y);
    ui.frame(body);
    ui.press();
    ui.frame(body);
    ui.move(field.right - 1, center(field).y);
    ui.frame(body);
    ui.release();
    ui.frame(body);
    ui.modifiers(KeyModifier::CONTROL);
    ui.shortcut('c');
    ui.frame(body);
    EXPECT_EQ(ui.services().clipboard_text(), "alpha beta gamma");
}

TEST(UiTextFieldTest, ReturnConfirmsEscapeRevertsAndAClickElsewhereEnds)
{
    UiHarness ui;
    std::string text = "start";
    UiRectangle field{};
    bool entered = false;
    auto body = in_window([&](UiFrame& frame)
    {
        entered = frame.input_text("Confirm", text, {.flags = InputTextFlag::ENTER_RETURNS_TRUE});
        field = frame.item_rectangle();
    });
    ui.frames(2, body);

    // Return: true, editing ends, text input stops.
    click_end(ui, field, body);
    ui.type("ed");
    ui.frame(body);
    EXPECT_FALSE(entered);
    EXPECT_EQ(text, "started");
    ui.tap(Key::RETURN);
    ui.frame(body);
    EXPECT_TRUE(entered);
    EXPECT_FALSE(ui.context().wants_text_input());
    EXPECT_FALSE(ui.services().is_text_input_active());

    // Escape: the text it had when editing started.
    click_end(ui, field, body);
    ui.type("!!!");
    ui.frame(body);
    EXPECT_EQ(text, "started!!!");
    ui.tap(Key::ESCAPE);
    ui.frame(body);
    EXPECT_EQ(text, "started");
    EXPECT_FALSE(ui.context().wants_text_input());

    // A click outside the field ends editing and keeps the text.
    click_end(ui, field, body);
    ui.type("?");
    ui.frame(body);
    ui.click(field.left, field.bottom + 100, body);
    EXPECT_FALSE(ui.context().wants_text_input());
    EXPECT_EQ(text, "started?");
}

TEST(UiTextFieldTest, TabMovesToTheNextFieldWithItsTextSelected)
{
    UiHarness ui;
    std::string first = "one";
    std::string second = "two";
    UiRectangle field{};
    auto body = in_window([&](UiFrame& frame)
    {
        frame.input_text("First", first);
        field = frame.item_rectangle();
        frame.input_text("Second", second);
    });
    ui.frames(2, body);
    click_end(ui, field, body);
    ui.tap(Key::TAB);
    ui.frame(body);
    ui.frame(body);
    EXPECT_TRUE(ui.context().wants_text_input());
    ui.type("2");
    ui.frame(body);
    EXPECT_EQ(first, "one");
    EXPECT_EQ(second, "2");

    // Shift+Tab goes back: the first field's text is replaced.
    ui.modifiers(KeyModifier::SHIFT);
    ui.tap(Key::TAB);
    ui.frame(body);
    ui.modifiers(KeyModifier::NONE);
    ui.frame(body);
    ui.type("1");
    ui.frame(body);
    EXPECT_EQ(first, "1");
}

TEST(UiTextFieldTest, FiltersPasswordsReadOnlyFieldsAndBuffersLimitWhatChanges)
{
    UiHarness ui;
    std::string decimal;
    std::string upper;
    std::string secret = "hunter2";
    std::string fixed = "read me";
    std::array<char, 6> buffer{'a', 'b', '\0'};
    std::array<UiRectangle, 5> fields{};
    auto body = in_window([&](UiFrame& frame)
    {
        frame.input_text("Decimal", decimal, {.flags = InputTextFlag::CHARACTERS_DECIMAL});
        fields[0] = frame.item_rectangle();
        frame.input_text("Upper", upper, {.flags = InputTextFlag::CHARACTERS_UPPERCASE | InputTextFlag::CHARACTERS_NO_BLANK});
        fields[1] = frame.item_rectangle();
        frame.input_text("Secret", secret, {.flags = InputTextFlag::PASSWORD});
        fields[2] = frame.item_rectangle();
        frame.input_text("Fixed", fixed, {.flags = InputTextFlag::READ_ONLY});
        fields[3] = frame.item_rectangle();
        frame.input_text("Buffer", buffer);
        fields[4] = frame.item_rectangle();
    });
    ui.frames(2, body);

    click_end(ui, fields[0], body);
    ui.type("1a2.5b-");
    ui.frame(body);
    EXPECT_EQ(decimal, "12.5-");

    click_end(ui, fields[1], body);
    ui.type("ab c");
    ui.frame(body);
    EXPECT_EQ(upper, "ABC");

    // A password field types but never copies.
    ui.services().set_clipboard_text("kept");
    click_end(ui, fields[2], body);
    ui.type("!");
    ui.frame(body);
    ui.modifiers(KeyModifier::CONTROL);
    ui.shortcut('a');
    ui.shortcut('c');
    ui.frame(body);
    EXPECT_EQ(secret, "hunter2!");
    EXPECT_EQ(ui.services().clipboard_text(), "kept");
    ui.modifiers(KeyModifier::NONE);

    // A read-only field copies but does not change.
    click_end(ui, fields[3], body);
    ui.type("x");
    ui.tap(Key::BACKSPACE);
    ui.modifiers(KeyModifier::CONTROL);
    ui.shortcut('a');
    ui.shortcut('c');
    ui.frame(body);
    EXPECT_EQ(fixed, "read me");
    EXPECT_EQ(ui.services().clipboard_text(), "read me");
    ui.modifiers(KeyModifier::NONE);

    // A buffer of 6 holds 5 bytes and its NUL.
    click_end(ui, fields[4], body);
    ui.type("cdefgh");
    ui.frame(body);
    EXPECT_EQ(std::string_view(buffer.data()), "abcde");
    EXPECT_EQ(buffer[5], '\0');
}

TEST(UiTextFieldTest, MultiLineFieldsTypeLineBreaksAndMoveBetweenLines)
{
    UiHarness ui;
    std::string text = "first\nsecond";
    std::string confirmed;
    UiRectangle field{};
    UiRectangle other{};
    bool entered = false;
    auto body = in_window([&](UiFrame& frame)
    {
        frame.input_text_multiline("Notes", text, {.size = {.x = 300, .y = 120}});
        field = frame.item_rectangle();
        entered = frame.input_text_multiline("Confirm", confirmed,
                                             {.flags = InputTextFlag::CONTROL_ENTER_FOR_NEW_LINE | InputTextFlag::ENTER_RETURNS_TRUE
                                                       | InputTextFlag::ALLOW_TAB_INPUT,
                                              .size = {.x = 300, .y = 60}});
        other = frame.item_rectangle();
    });
    ui.frames(3, body);

    // Return types a line break; Up goes to the line above at the same column.
    ui.click(field.left + 8, field.top + 8, body);
    ui.modifiers(KeyModifier::CONTROL);
    ui.tap(Key::END);
    ui.frame(body);
    ui.modifiers(KeyModifier::NONE);
    ui.tap(Key::RETURN);
    ui.frame(body);
    ui.type("third");
    ui.frame(body);
    EXPECT_EQ(text, "first\nsecond\nthird");
    ui.tap(Key::UP);
    ui.tap(Key::UP);
    ui.frame(body);
    ui.tap(Key::HOME);
    ui.type(">");
    ui.frame(body);
    EXPECT_EQ(text, ">first\nsecond\nthird");
    ui.tap(Key::DOWN);
    ui.frame(body);
    ui.tap(Key::END);
    ui.type("!");
    ui.frame(body);
    EXPECT_EQ(text, ">first\nsecond!\nthird");

    // With CONTROL_ENTER_FOR_NEW_LINE, Control+Return breaks lines and Return confirms; Tab types a tab.
    ui.click(other.left + 8, other.top + 8, body);
    ui.type("a");
    ui.tap(Key::TAB);
    ui.frame(body);
    ui.modifiers(KeyModifier::CONTROL);
    ui.tap(Key::RETURN);
    ui.frame(body);
    ui.modifiers(KeyModifier::NONE);
    ui.type("b");
    ui.frame(body);
    EXPECT_FALSE(entered);
    ui.tap(Key::RETURN);
    ui.frame(body);
    EXPECT_TRUE(entered);
    EXPECT_EQ(confirmed, "a\t\nb");
    EXPECT_FALSE(ui.context().wants_text_input());
}

TEST(UiTextFieldTest, TheImeCompositionShowsAtTheCursorUntilItIsTyped)
{
    UiHarness ui;
    std::string text = "ab";
    UiRectangle field{};
    auto body = in_window([&](UiFrame& frame)
    {
        frame.input_text("Ime", text);
        field = frame.item_rectangle();
    });
    ui.frames(2, body);
    click_end(ui, field, body);
    const float before = ui.services().text_input_area().left;

    // A composition in progress is drawn (more glyphs), not typed; its cursor moves the text input area.
    const auto vertices = [&ui] { return ui.context().draw_data().lists.empty() ? 0U : ui.context().draw_data().lists[0]->vertices().size(); };
    const std::size_t plain = vertices();
    ui.compose("xyz", 0);
    ui.frame(body);
    EXPECT_EQ(text, "ab");
    EXPECT_GT(vertices(), plain);
    EXPECT_NEAR(ui.services().text_input_area().left, before, 1.0F);
    ui.compose("xyz", 2);
    ui.frame(body);
    EXPECT_GT(ui.services().text_input_area().left, before + 5);

    // Committed: the composition ends and its text is typed.
    ui.compose("");
    ui.type("xyz");
    ui.frame(body);
    EXPECT_EQ(text, "abxyz");
}

TEST(UiTextFieldTest, HintsShowInEmptyFieldsAndOverwriteReplacesCharacters)
{
    UiHarness ui;
    std::string empty;
    std::string overwrite = "abcd";
    UiRectangle field{};
    std::size_t with_hint = 0;
    auto body = in_window([&](UiFrame& frame)
    {
        frame.input_text("Search", empty, {.hint = "type here"});
        frame.input_text("Over", overwrite, {.flags = InputTextFlag::ALWAYS_OVERWRITE});
        field = frame.item_rectangle();
    });
    ui.frames(2, body);
    with_hint = ui.context().draw_data().lists[0]->vertices().size();
    EXPECT_GT(with_hint, 0U);
    ui.click(field.left + 1, center(field).y, body);
    ui.type("XY");
    ui.frame(body);
    EXPECT_EQ(overwrite, "XYcd");
}

TEST(UiTextFieldTest, LongTextsScrollToTheCursorStayFixedOrShowTheirEnd)
{
    UiHarness ui;
    const std::string long_text(60, 'x');
    std::string plain = long_text;
    std::string fixed = long_text;
    std::string elided = long_text;
    std::array<UiRectangle, 3> fields{};
    auto body = in_window([&](UiFrame& frame)
    {
        frame.input_text("Plain", plain);
        fields[0] = frame.item_rectangle();
        frame.input_text("Fixed", fixed, {.flags = InputTextFlag::NO_HORIZONTAL_SCROLL});
        fields[1] = frame.item_rectangle();
        frame.input_text("Elided", elided, {.flags = InputTextFlag::ELIDE_LEFT});
        fields[2] = frame.item_rectangle();
    });
    ui.frames(2, body);

    // A text wider than its field shows its start: a click at the right edge lands inside the text; End scrolls the
    // text so the cursor stays in the field.
    click_end(ui, fields[0], body);
    ui.type("!");
    ui.frame(body);
    EXPECT_LT(plain.find('!'), 60U);
    ui.tap(Key::END);
    ui.frame(body);
    ui.frame(body);
    EXPECT_LE(ui.services().text_input_area().right, fields[0].right);

    // NO_HORIZONTAL_SCROLL: the cursor leaves the field.
    click_end(ui, fields[1], body);
    ui.tap(Key::END);
    ui.frame(body);
    ui.frame(body);
    EXPECT_GT(ui.services().text_input_area().left, fields[1].right);

    // ELIDE_LEFT: its end is shown (and stays where it was when editing starts): the click lands at the end.
    click_end(ui, fields[2], body);
    ui.type("!");
    ui.frame(body);
    EXPECT_EQ(elided, long_text + "!");
}

// ===================================================================================================== numbers

TEST(UiNumberTest, TypedValuesAreParsedClampedToTheTypeAndFormatted)
{
    UiHarness ui;
    std::int32_t integer = 7;
    std::uint8_t byte = 1;
    float real = 0.5F;
    double precise = 2.0;
    std::array<UiRectangle, 4> fields{};
    UiFrameReport report{};
    auto body = in_window([&](UiFrame& frame)
    {
        frame.input_number("Integer", integer);
        fields[0] = frame.item_rectangle();
        frame.input_number("Byte", byte);
        fields[1] = frame.item_rectangle();
        frame.input_number("Real", real, 0.0F, 0.0F, "{:.2f} m");
        fields[2] = frame.item_rectangle();
        frame.input_number("Precise", precise, 0.0, 0.0, "{:d}");
        fields[3] = frame.item_rectangle();
    });
    ui.frames(2, body);

    // Typed: applied on every change.
    const auto retype = [&](std::size_t field, std::string_view text)
    {
        click_end(ui, fields[field], body);
        ui.modifiers(KeyModifier::CONTROL);
        ui.shortcut('a');
        ui.frame(body);
        ui.modifiers(KeyModifier::NONE);
        ui.type(text);
        ui.frame(body);
    };
    retype(0, "-42");
    EXPECT_EQ(integer, -42);
    ui.tap(Key::RETURN);
    ui.frame(body);

    // Out of the type's range: clamped to it; signs the type cannot have give its lowest value.
    retype(1, "300");
    EXPECT_EQ(byte, 255);
    retype(1, "-5");
    EXPECT_EQ(byte, 0);
    ui.tap(Key::RETURN);
    ui.frame(body);

    // Editing shows the value without the format's other text; scientific notation is accepted.
    retype(2, "1.5e1");
    EXPECT_FLOAT_EQ(real, 15.0F);
    ui.tap(Key::ESCAPE);
    ui.frame(body);
    EXPECT_FLOAT_EQ(real, 0.5F);

    // A format the type cannot use is reported and the default one is used.
    report = ui.frame(body);
    EXPECT_TRUE(has_issue(report, UiFrameProblem::INVALID_ARGUMENT, "Precise"));
}

TEST(UiNumberTest, StepButtonsAddTheStepAndControlAddsTheFastStep)
{
    UiHarness ui;
    std::int32_t value = 0;
    UiRectangle field{};
    float button = 0;
    auto body = in_window([&](UiFrame& frame)
    {
        frame.input_number("Count", value, 1, 10);
        field = frame.item_rectangle();
        button = frame.frame_height();
    });
    ui.frames(2, body);
    const float spacing = 4;
    const Vector2 minus{.x = field.right + spacing + button * 0.5F, .y = center(field).y};
    const Vector2 plus{.x = minus.x + button + spacing, .y = center(field).y};
    ui.click(plus.x, plus.y, body);
    EXPECT_EQ(value, 1);
    ui.click(plus.x, plus.y, body);
    EXPECT_EQ(value, 2);
    ui.click(minus.x, minus.y, body);
    EXPECT_EQ(value, 1);
    ui.modifiers(KeyModifier::CONTROL);
    ui.click(plus.x, plus.y, body);
    EXPECT_EQ(value, 11);
}

TEST(UiNumberTest, SlidersFollowTheMouseAndRoundToTheFormat)
{
    UiHarness ui;
    float linear = 0.25F;
    std::int32_t steps = 0;
    float logarithmic = 1.0F;
    std::array<UiRectangle, 3> sliders{};
    bool changed = false;
    auto body = in_window([&](UiFrame& frame)
    {
        changed = frame.slider("Linear", linear, 0.0F, 1.0F) || changed;
        sliders[0] = frame.item_rectangle();
        frame.slider("Steps", steps, 0, 10);
        sliders[1] = frame.item_rectangle();
        frame.slider("Logarithmic", logarithmic, 1.0F, 1000.0F, "{:.1f}", SliderFlag::LOGARITHMIC);
        sliders[2] = frame.item_rectangle();
    });
    ui.frames(2, body);

    // Ends and middle of a float slider (the grab's half width in from each end).
    ui.click(sliders[0].left + 1, center(sliders[0]).y, body);
    EXPECT_FLOAT_EQ(linear, 0.0F);
    EXPECT_TRUE(changed);
    ui.click(sliders[0].right - 1, center(sliders[0]).y, body);
    EXPECT_FLOAT_EQ(linear, 1.0F);
    ui.click(center(sliders[0]).x, center(sliders[0]).y, body);
    EXPECT_NEAR(linear, 0.5F, 0.01F);
    EXPECT_FLOAT_EQ(linear, std::round(linear * 1000.0F) / 1000.0F);

    // Dragging: the value follows while the button is held.
    ui.move(sliders[0].left + 1, center(sliders[0]).y);
    ui.frame(body);
    ui.press();
    ui.frame(body);
    ui.move(sliders[0].left + sliders[0].width() * 0.75F, center(sliders[0]).y);
    ui.frame(body);
    EXPECT_NEAR(linear, 0.75F, 0.03F);
    ui.release();
    ui.frame(body);

    // An integer slider takes integers; a logarithmic one puts the geometric middle in the middle.
    ui.click(center(sliders[1]).x, center(sliders[1]).y, body);
    EXPECT_EQ(steps, 5);
    ui.click(center(sliders[2]).x, center(sliders[2]).y, body);
    EXPECT_NEAR(logarithmic, 31.6F, 1.0F);
    EXPECT_FLOAT_EQ(logarithmic, std::round(logarithmic * 10.0F) / 10.0F);
}

TEST(UiNumberTest, ControlClickTypesAValueAndClampOnInputKeepsItInRange)
{
    UiHarness ui;
    float free = 0.5F;
    float clamped = 0.5F;
    std::array<UiRectangle, 2> sliders{};
    auto body = in_window([&](UiFrame& frame)
    {
        frame.slider("Free", free, 0.0F, 1.0F);
        sliders[0] = frame.item_rectangle();
        frame.slider("Clamped", clamped, 0.0F, 1.0F, {}, SliderFlag::CLAMP_ON_INPUT);
        sliders[1] = frame.item_rectangle();
    });
    ui.frames(2, body);

    // Control+click: a text field with the value selected; Return applies what was typed.
    ui.modifiers(KeyModifier::CONTROL);
    ui.click(center(sliders[0]).x, center(sliders[0]).y, body);
    ui.modifiers(KeyModifier::NONE);
    EXPECT_TRUE(ui.context().wants_text_input());
    EXPECT_FLOAT_EQ(free, 0.5F);
    ui.type("5");
    ui.frame(body);
    EXPECT_FLOAT_EQ(free, 0.5F);
    ui.tap(Key::RETURN);
    ui.frame(body);
    EXPECT_FLOAT_EQ(free, 5.0F);
    EXPECT_FALSE(ui.context().wants_text_input());

    // CLAMP_ON_INPUT: typed values stay in the range; Escape changes nothing.
    ui.modifiers(KeyModifier::CONTROL);
    ui.click(center(sliders[1]).x, center(sliders[1]).y, body);
    ui.modifiers(KeyModifier::NONE);
    ui.type("5");
    ui.tap(Key::RETURN);
    ui.frame(body);
    EXPECT_FLOAT_EQ(clamped, 1.0F);
    ui.modifiers(KeyModifier::CONTROL);
    ui.click(center(sliders[1]).x, center(sliders[1]).y, body);
    ui.modifiers(KeyModifier::NONE);
    ui.type("0.25");
    ui.tap(Key::ESCAPE);
    ui.frame(body);
    EXPECT_FLOAT_EQ(clamped, 1.0F);

    // A double click types a value too.
    ui.click(center(sliders[0]).x, center(sliders[0]).y, body);
    ui.press();
    ui.frame(body);
    ui.release();
    ui.frame(body);
    EXPECT_TRUE(ui.context().wants_text_input());
    ui.type("0.125");
    ui.tap(Key::RETURN);
    ui.frame(body);
    EXPECT_FLOAT_EQ(free, 0.125F);
}

TEST(UiNumberTest, DragsMoveByTheSpeedFasterWithShiftAndStayInTheirRange)
{
    UiHarness ui;
    std::int32_t value = 0;
    std::int32_t bounded = 5;
    std::int32_t wrapped = 8;
    std::array<UiRectangle, 3> drags{};
    auto body = in_window([&](UiFrame& frame)
    {
        frame.drag("Value", value, 1.0F);
        drags[0] = frame.item_rectangle();
        frame.drag("Bounded", bounded, 1.0F, 0, 10);
        drags[1] = frame.item_rectangle();
        frame.drag("Wrapped", wrapped, 1.0F, 0, 9, {}, SliderFlag::WRAP_AROUND);
        drags[2] = frame.item_rectangle();
    });
    ui.frames(2, body);
    const auto drag = [&](const UiRectangle& item, std::initializer_list<float> offsets)
    {
        ui.wait(1.0);
        Vector2 position = center(item);
        ui.move(position.x, position.y);
        ui.frame(body);
        ui.press();
        ui.frame(body);
        for (const float offset : offsets)
        {
            position.x += offset;
            ui.move(position.x, position.y);
            ui.frame(body);
        }
        ui.release();
        ui.frame(body);
    };

    // Past the drag threshold, every unit moved is a step; Shift: ten.
    drag(drags[0], {20});
    EXPECT_EQ(value, 20);
    drag(drags[0], {-30});
    EXPECT_EQ(value, -10);
    ui.modifiers(KeyModifier::SHIFT);
    drag(drags[0], {10, 2});
    EXPECT_EQ(value, 110);
    ui.modifiers(KeyModifier::NONE);

    // Clamped to the range; wrapped around it.
    drag(drags[1], {40});
    EXPECT_EQ(bounded, 10);
    drag(drags[1], {-80});
    EXPECT_EQ(bounded, 0);
    drag(drags[2], {10});
    EXPECT_EQ(wrapped, 8);

    // A click without moving changes nothing.
    ui.click(center(drags[0]).x, center(drags[0]).y, body);
    EXPECT_EQ(value, 110);
}

TEST(UiNumberTest, TheKeyboardStepsFocusedSlidersAndDrags)
{
    UiHarness ui;
    std::int32_t slider = 3;
    float drag = 1.0F;
    auto body = in_window([&](UiFrame& frame)
    {
        frame.slider("Slider", slider, 0, 10);
        frame.drag("Drag", drag, 0.5F);
    });
    ui.frames(2, body);
    ui.focus(true);
    ui.move(5, 5);
    ui.frame(body);
    ui.click(100, 100, body);
    ui.tap(Key::TAB);
    ui.frame(body);
    ui.tap(Key::RIGHT);
    ui.tap(Key::RIGHT);
    ui.frame(body);
    EXPECT_EQ(slider, 5);
    ui.tap(Key::TAB);
    ui.frame(body);
    ui.tap(Key::LEFT);
    ui.frame(body);
    EXPECT_FLOAT_EQ(drag, 0.5F);
}

TEST(UiNumberTest, ComponentsShareTheItemWidthAndEditTheirOwnValue)
{
    UiHarness ui;
    std::array<float, 3> position{0, 0, 0};
    std::array<std::int32_t, 2> size{1, 2};
    float width = 0;
    UiRectangle row{};
    auto body = in_window([&](UiFrame& frame)
    {
        width = frame.calculate_item_width();
        frame.drag("Position", std::span{position}, 1.0F);
        row = frame.item_rectangle();
        frame.slider("Size", std::span{size}, 0, 10);
    });
    ui.frames(2, body);
    const float component = (width - 4 * 2) / 3;
    EXPECT_GT(row.width(), width);

    // The second component edits the second value only.
    const Vector2 second{.x = row.left + component * 1.5F + 4, .y = center(row).y};
    ui.move(second.x, second.y);
    ui.frame(body);
    ui.press();
    ui.frame(body);
    ui.move(second.x + 20, second.y);
    ui.frame(body);
    ui.release();
    ui.frame(body);
    EXPECT_EQ(position, (std::array<float, 3>{0, 20, 0}));
    EXPECT_EQ(size, (std::array<std::int32_t, 2>{1, 2}));
}

// ===================================================================================================== colors

TEST(UiColorTest, HsvAndHexadecimalConversionsWorkAtCompileTime)
{
    static_assert(to_hsv({.red = 1, .green = 0, .blue = 0, .alpha = 1}) == HsvColor{.hue = 0, .saturation = 1, .value = 1, .alpha = 1});
    static_assert(to_hsv({.red = 0.5F, .green = 0.5F, .blue = 0.5F, .alpha = 0.25F}) == HsvColor{.hue = 0, .saturation = 0, .value = 0.5F, .alpha = 0.25F});
    static_assert(from_hsv({.hue = 1.0F / 3.0F, .saturation = 1, .value = 1}).green == 1);
    static_assert(from_hsv({.hue = 2.0F / 3.0F, .saturation = 1, .value = 1}).blue == 1);
    static_assert(to_hexadecimal(Color::from_rgba8(0x12, 0x34, 0x56, 0x78)).view() == "#12345678");
    static_assert(to_hexadecimal(Color::from_rgba8(0xFF, 0x80, 0x00), false).view() == "#FF8000");
    static_assert(from_hexadecimal("#ff8000") == Color::from_rgba8(0xFF, 0x80, 0x00));
    static_assert(from_hexadecimal("12345678") == Color::from_rgba8(0x12, 0x34, 0x56, 0x78));
    static_assert(!from_hexadecimal("#12345").has_value());
    static_assert(!from_hexadecimal("#GG0000").has_value());

    // Every 8-bit gray, primary and secondary survives a round trip through HSV.
    for (std::uint32_t level = 0; level < 256; level += 15)
    {
        const auto byte = static_cast<std::uint8_t>(level);
        for (const Color color : {Color::from_rgba8(byte, byte, byte), Color::from_rgba8(byte, 0, 0), Color::from_rgba8(0, byte, byte)})
        {
            EXPECT_EQ(from_hsv(to_hsv(color)).packed(), color.packed());
        }
    }
}

TEST(UiColorTest, ChannelDragsAndTheHexadecimalFieldEditTheColor)
{
    UiHarness ui;
    Color color = Color::from_rgba8(100, 0, 0);
    Color hexadecimal = Color::from_rgba8(0, 0, 255);
    Vector2 start{};
    float width = 0;
    float frame_size = 0;
    UiRectangle hexadecimal_row{};
    auto body = in_window([&](UiFrame& frame)
    {
        start = frame.cursor_screen_position();
        width = frame.calculate_item_width();
        frame_size = frame.frame_height();
        frame.color_edit("Color", color);
        frame.color_edit("Hexadecimal", hexadecimal, ColorEditFlag::DISPLAY_HEXADECIMAL | ColorEditFlag::NO_ALPHA);
        hexadecimal_row = frame.item_rectangle();
    });
    ui.frames(2, body);

    // The red channel's drag (the first of four, left of the preview): 20 units right is 20 levels more.
    const float available = width - (frame_size + 4);
    const float component = (available - 4 * 3) / 4;
    const Vector2 red{.x = start.x + component * 0.5F, .y = start.y + frame_size * 0.5F};
    ui.move(red.x, red.y);
    ui.frame(body);
    ui.press();
    ui.frame(body);
    ui.move(red.x + 20, red.y);
    ui.frame(body);
    ui.release();
    ui.frame(body);
    EXPECT_EQ(color.packed(), Color::from_rgba8(120, 0, 0).packed());

    // The hexadecimal field: typed digits replace the color (its alpha kept).
    ui.click(hexadecimal_row.left + 10, center(hexadecimal_row).y, body);
    ui.modifiers(KeyModifier::CONTROL);
    ui.shortcut('a');
    ui.frame(body);
    ui.modifiers(KeyModifier::NONE);
    ui.type("#00ff00");
    ui.frame(body);
    EXPECT_EQ(hexadecimal.packed(), Color::from_rgba8(0, 255, 0).packed());
}

TEST(UiColorTest, TheSquareAndTheHueBarOfAPickerSetTheColor)
{
    UiHarness ui;
    Color color = Color::from_rgba8(255, 0, 0);
    Vector2 start{};
    float frame_size = 0;
    auto body = in_window([&](UiFrame& frame)
    {
        start = frame.cursor_screen_position();
        frame_size = frame.frame_height();
        frame.set_next_item_width(300);
        frame.color_picker("Picker", color, ColorEditFlag::NO_INPUTS | ColorEditFlag::NO_SIDE_PREVIEW | ColorEditFlag::NO_ALPHA);
    });
    ui.frames(2, body);
    const float square = std::floor(300 - (frame_size + 4));

    // The square: white at the top left, black at the bottom, the hue at the top right.
    ui.click(start.x + 1, start.y + 1, body);
    EXPECT_GT(color.red, 0.98F);
    EXPECT_GT(color.green, 0.98F);
    EXPECT_GT(color.blue, 0.98F);
    ui.click(start.x + square - 1, start.y + square - 1, body);
    EXPECT_LT(color.red + color.green + color.blue, 0.02F);
    ui.click(start.x + square - 1, start.y + 1, body);
    EXPECT_GT(color.red, 0.98F);
    EXPECT_LT(color.green + color.blue, 0.02F);

    // The hue bar: a third of the way down is green; the saturation and value stay.
    const float bar = start.x + square + 4 + frame_size * 0.5F;
    ui.click(bar, start.y + square / 3.0F, body);
    EXPECT_GT(color.green, 0.95F);
    EXPECT_LT(color.red + color.blue, 0.1F);
}

TEST(UiColorTest, InputHsvColorsHoldHueSaturationAndValue)
{
    UiHarness ui;
    Color hsv{.red = 0.5F, .green = 1, .blue = 1, .alpha = 1};
    Vector2 start{};
    auto body = in_window([&](UiFrame& frame)
    {
        start = frame.cursor_screen_position();
        frame.set_next_item_width(300);
        frame.color_picker("Hsv", hsv, ColorEditFlag::INPUT_HSV | ColorEditFlag::NO_INPUTS | ColorEditFlag::NO_SIDE_PREVIEW | ColorEditFlag::NO_ALPHA);
    });
    ui.frames(2, body);

    // The square's top left: no saturation, full value; the hue is kept although white has none.
    ui.click(start.x + 1, start.y + 1, body);
    EXPECT_NEAR(hsv.red, 0.5F, 0.001F);
    EXPECT_LT(hsv.green, 0.02F);
    EXPECT_GT(hsv.blue, 0.98F);
}

TEST(UiColorTest, TheHueWheelSetsTheHueAndItsTriangleTheSaturationAndValue)
{
    UiHarness ui;
    Color color = Color::from_rgba8(255, 0, 0);
    Vector2 start{};
    float frame_size = 0;
    auto body = in_window([&](UiFrame& frame)
    {
        start = frame.cursor_screen_position();
        frame_size = frame.frame_height();
        frame.set_next_item_width(300);
        frame.color_picker("Wheel", color, ColorEditFlag::PICKER_HUE_WHEEL | ColorEditFlag::NO_INPUTS | ColorEditFlag::NO_SIDE_PREVIEW | ColorEditFlag::NO_ALPHA);
    });
    ui.frames(2, body);
    const float picker = std::floor(300.0F);
    const float radius = picker * 0.5F;
    const Vector2 center_point{.x = start.x + radius, .y = start.y + radius};
    const float ring = radius - std::max(4.0F, frame_size * 0.75F) * 0.5F;

    // Below the center on the ring: a quarter turn, between green and cyan (hue 0.25).
    ui.click(center_point.x, center_point.y + ring, body);
    const HsvColor turned = to_hsv(color);
    EXPECT_NEAR(turned.hue, 0.25F, 0.02F);

    // The triangle's white corner (120 degrees after the hue's corner): white.
    const float triangle = radius - std::max(4.0F, frame_size * 0.75F) - 4.0F;
    const float angle = turned.hue * 2 * std::numbers::pi_v<float> + 2.0F * std::numbers::pi_v<float> / 3.0F;
    ui.click(center_point.x + std::cos(angle) * (triangle - 3), center_point.y + std::sin(angle) * (triangle - 3), body);
    EXPECT_GT(color.red + color.green + color.blue, 2.8F);
}

TEST(UiColorTest, ThePreviewOpensAPickerPopupThatAClickOutsideCloses)
{
    UiHarness ui;
    Color color = Color::from_rgba8(10, 20, 30);
    Vector2 start{};
    float width = 0;
    float frame_size = 0;
    bool swatch = false;
    auto body = in_window([&](UiFrame& frame)
    {
        start = frame.cursor_screen_position();
        width = frame.calculate_item_width();
        frame_size = frame.frame_height();
        frame.color_edit("Edit", color);
        swatch = frame.color_button("Swatch", color) || swatch;
    });
    ui.frames(2, body);
    const auto lists = [&ui] { return ui.context().draw_data().lists.size(); };
    EXPECT_EQ(lists(), 1U);

    // The preview square opens the popup (drawn from its second frame, over the window).
    const Vector2 preview{.x = start.x + width - frame_size * 0.5F, .y = start.y + frame_size * 0.5F};
    ui.click(preview.x, preview.y, body);
    ui.frame(body);
    EXPECT_EQ(lists(), 2U);

    // Clicking inside it keeps it; outside closes it.
    ui.click(preview.x, preview.y + frame_size + 30, body);
    EXPECT_EQ(lists(), 2U);
    ui.click(start.x + 600, start.y + 500, body);
    EXPECT_EQ(lists(), 1U);
    EXPECT_FALSE(swatch);

    // A color button is a button.
    ui.click(start.x + frame_size * 0.5F, start.y + frame_size * 1.5F + 4, body);
    EXPECT_TRUE(swatch);
}

// ===================================================================================================== lists

TEST(UiListTest, AComboListsItsItemsInAPopupAndAChoiceClosesIt)
{
    UiHarness ui;
    std::size_t selected = 0;
    const std::array<std::string_view, 4> items{"apple", "banana", "cherry", "date"};
    UiRectangle combo{};
    float line = 0;
    bool changed = false;
    auto body = in_window([&](UiFrame& frame)
    {
        changed = frame.combo("Fruit", selected, items) || changed;
        combo = frame.item_rectangle();
        line = frame.text_line_height();
    });
    ui.frames(2, body);
    const auto lists = [&ui] { return ui.context().draw_data().lists.size(); };

    // Opened on the press; drawn from its second frame.
    ui.click(center(combo).x, center(combo).y, body);
    EXPECT_EQ(lists(), 2U);
    EXPECT_FALSE(changed);

    // The third item: chosen, the popup closes.
    const float item_y = combo.bottom + 8 + (line + 4) * 2 + line * 0.5F;
    ui.click(combo.left + 20, item_y, body);
    EXPECT_TRUE(changed);
    EXPECT_EQ(selected, 2U);
    ui.frame(body);
    EXPECT_EQ(lists(), 1U);
}

TEST(UiListTest, TheComboItselfEscapeOrAClickOutsideCloseItsPopup)
{
    UiHarness ui;
    std::size_t selected = 1;
    const std::array<std::string_view, 3> items{"one", "two", "three"};
    UiRectangle combo{};
    auto body = in_window([&](UiFrame& frame)
    {
        frame.combo("Number", selected, items);
        combo = frame.item_rectangle();
    });
    ui.frames(2, body);
    const auto lists = [&ui] { return ui.context().draw_data().lists.size(); };
    ui.click(center(combo).x, center(combo).y, body);
    ASSERT_EQ(lists(), 2U);

    // The combo again: closed, not reopened.
    ui.click(center(combo).x, center(combo).y, body);
    EXPECT_EQ(lists(), 1U);

    // Escape.
    ui.click(center(combo).x, center(combo).y, body);
    ASSERT_EQ(lists(), 2U);
    ui.tap(Key::ESCAPE);
    ui.frame(body);
    EXPECT_EQ(lists(), 1U);

    // A click elsewhere.
    ui.click(center(combo).x, center(combo).y, body);
    ASSERT_EQ(lists(), 2U);
    ui.click(combo.right + 200, combo.bottom + 250, body);
    EXPECT_EQ(lists(), 1U);
    EXPECT_EQ(selected, 1U);
}

TEST(UiListTest, ListBoxesSelectAndScroll)
{
    UiHarness ui;
    std::size_t selected = 0;
    std::vector<std::string> items;
    for (std::uint32_t item = 0; item < 20; ++item)
    {
        items.push_back(std::format("Item {}", item));
    }
    UiRectangle box{};
    float line = 0;
    bool changed = false;
    auto body = in_window([&](UiFrame& frame)
    {
        changed = frame.list_box("Items", selected, items, 4) || changed;
        box = frame.item_rectangle();
        line = frame.text_line_height();
    });
    ui.frames(3, body);

    // The second item (the region has the frame padding).
    ui.click(box.left + 20, box.top + 3 + line + 4 + line * 0.5F, body);
    EXPECT_TRUE(changed);
    EXPECT_EQ(selected, 1U);

    // The wheel scrolls it: the first visible item moves down the list.
    ui.wheel(-3);
    ui.frame(body);
    ui.frame(body);
    changed = false;
    ui.click(box.left + 20, box.top + 3 + line * 0.5F, body);
    EXPECT_TRUE(changed);
    EXPECT_GT(selected, 1U);
}

} // namespace
