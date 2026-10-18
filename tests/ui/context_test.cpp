// The immediate-mode UI (UiContext, UiFrame) driven by synthetic input: windows (fitting, moving, resizing,
// collapsing, closing, z-order, scrolling, child regions), layout, widgets, identifiers, keyboard focus, item
// queries, styles and themes, the frame report, published draw data (scale, order, leases on another thread)
// and the declared widgets of later phases.
#include <gtest/gtest.h>

import std;
import gfx.ui;
import gfx.tests.ui_context_support;

namespace
{

using namespace gfx::ui;
using gfx::Key;
using gfx::KeyModifier;
using gfx::MouseButton;
using gfx::Vector2;
using gfx::tests::center;
using gfx::tests::has_issue;
using gfx::tests::UiHarness;

constexpr Color GREEN{.green = 1, .alpha = 1};
constexpr Color RED{.red = 1, .alpha = 1};

/// A window at a fixed place, first used.
void
place(UiFrame& frame,
      Vector2 position,
      Vector2 size = {})
{
    frame.set_next_window_position(position, UiCondition::FIRST_USE_EVER);
    if (size.x > 0)
    {
        frame.set_next_window_size(size, UiCondition::FIRST_USE_EVER);
    }
}

/// Whether a vertex of @p data has the color @p color.
auto
has_vertex_color(const UiDrawData& data,
                 Color color) -> bool
{
    return std::ranges::any_of(data.lists, [&](const DrawList* list)
    {
        return std::ranges::any_of(list->vertices(), [&](const UiVertex& vertex) { return vertex.color == color.packed(); });
    });
}

// ===================================================================================================== themes and styles

TEST(UiThemeTest, BuiltInThemesAreMadeAtCompileTime)
{
    constexpr Theme dark = builtin_theme(ThemeKind::DARK);
    static_assert(dark.color(StyleColor::TEXT_NORMAL) == Color{.red = 1, .green = 1, .blue = 1, .alpha = 1});
    static_assert(builtin_theme(ThemeKind::LIGHT).color(StyleColor::TEXT_NORMAL) == Color{.red = 0, .green = 0, .blue = 0, .alpha = 1});
    static_assert(builtin_theme(ThemeKind::HIGH_CONTRAST).color(StyleColor::NAVIGATION_HIGHLIGHT) == Color{.red = 1, .green = 1, .blue = 0, .alpha = 1});
    static_assert(builtin_theme(ThemeKind::HIGH_CONTRAST).style.frame_border_size > 0, "every frame is bordered");
    static_assert(dark.color(StyleColor::TAB_HOVERED) == dark.color(StyleColor::HEADER_HOVERED), "derived colors");
    EXPECT_EQ(dark.name, "Dark");
    for (const ThemeKind kind : {ThemeKind::DARK, ThemeKind::LIGHT, ThemeKind::CLASSIC, ThemeKind::HIGH_CONTRAST})
    {
        const Theme theme = builtin_theme(kind);
        EXPECT_FALSE(theme.name.empty());
        EXPECT_GT(theme.color(StyleColor::TEXT_NORMAL).alpha, 0.9F) << theme.name;
    }
}

TEST(UiStyleTest, ScalingMultipliesSizesAndKeepsAlignments)
{
    constexpr Style style{};
    constexpr Style doubled = style.scaled(2.0F);
    static_assert(doubled.window_padding == Vector2{.x = 16, .y = 16});
    static_assert(doubled.frame_padding == Vector2{.x = 8, .y = 6});
    static_assert(doubled.window_title_align == style.window_title_align);
    static_assert(doubled.alpha == style.alpha);
    static_assert(is_vector_variable(StyleVariable::ITEM_SPACING) && !is_vector_variable(StyleVariable::ALPHA));
}

// ===================================================================================================== windows

TEST(UiWindowTest, WindowsFitTheirContentAndAppearOnTheirSecondFrame)
{
    UiHarness ui;
    Vector2 size{};
    Vector2 button{};
    float title_height = 0;
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 10, .y = 20});
        if (frame.begin_window("Fit"))
        {
            frame.button("Hello");
            button = {.x = frame.item_rectangle().width(), .y = frame.item_rectangle().height()};
            size = frame.window_size();
            title_height = std::floor(frame.text_line_height() + frame.style().frame_padding.y * 2);
        }
        frame.end_window();
    };

    // The first frame measures the content without drawing; the second draws the window at the fitted size.
    const UiFrameReport first = ui.frame(body);
    EXPECT_TRUE(first.ok());
    EXPECT_TRUE(ui.context().draw_data().lists.empty());
    ui.frame(body);
    EXPECT_EQ(ui.context().draw_data().lists.size(), 1U);
    EXPECT_FLOAT_EQ(size.x, button.x + 16);
    EXPECT_FLOAT_EQ(size.y, button.y + 16 + title_height);
}

TEST(UiWindowTest, ButtonsArePressedWhenReleasedOverThem)
{
    UiHarness ui;
    UiRectangle rectangle{};
    std::uint32_t presses = 0;
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 100, .y = 100});
        if (frame.begin_window("Buttons"))
        {
            presses += frame.button("Press me") ? 1U : 0U;
            rectangle = frame.item_rectangle();
        }
        frame.end_window();
    };
    ui.frames(2, body);

    // A click on it.
    ui.click(center(rectangle).x, center(rectangle).y, body);
    EXPECT_EQ(presses, 1U);

    // Pressed on it, released elsewhere: not pressed.
    ui.move(center(rectangle).x, center(rectangle).y);
    ui.frame(body);
    ui.press();
    ui.frame(body);
    ui.move(rectangle.right + 50, rectangle.bottom + 50);
    ui.release();
    ui.frame(body);
    EXPECT_EQ(presses, 1U);

    // Pressed elsewhere, released on it: not pressed either.
    ui.move(rectangle.right + 50, rectangle.bottom + 50);
    ui.press();
    ui.frame(body);
    ui.move(center(rectangle).x, center(rectangle).y);
    ui.release();
    ui.frame(body);
    EXPECT_EQ(presses, 1U);
}

TEST(UiWindowTest, TheCollapseArrowAndADoubleClickOnTheTitleCollapse)
{
    UiHarness ui;
    bool expanded = true;
    Vector2 position{};
    Vector2 size{};
    float line = 0;
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 50, .y = 50}, {.x = 200, .y = 150});
        expanded = frame.begin_window("Collapsing");
        position = frame.window_position();
        size = frame.window_size();
        line = frame.text_line_height();
        frame.end_window();
    };
    ui.frames(2, body);
    ASSERT_TRUE(expanded);

    // The arrow, at the title bar's left.
    const float title = std::floor(line + 6);
    ui.click(position.x + 4 + line * 0.5F, position.y + title * 0.5F, body);
    EXPECT_FALSE(expanded);
    EXPECT_FLOAT_EQ(size.y, title);

    // A double click on the title expands it again.
    ui.move(position.x + 120, position.y + title * 0.5F);
    ui.frame(body);
    for (int click = 0; click < 2; ++click)
    {
        ui.press();
        ui.frame(body);
        ui.release();
        ui.frame(body);
    }
    EXPECT_TRUE(expanded);
    EXPECT_FLOAT_EQ(size.y, 150.0F);
}

TEST(UiWindowTest, TheCloseButtonClearsTheOpenFlag)
{
    UiHarness ui;
    bool open = true;
    Vector2 position{};
    Vector2 size{};
    float line = 0;
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 50, .y = 50}, {.x = 200, .y = 150});
        if (frame.begin_window("Closable", open))
        {
            position = frame.window_position();
            size = frame.window_size();
            line = frame.text_line_height();
        }
        frame.end_window();
    };
    ui.frames(2, body);
    const float title = std::floor(line + 6);
    ui.click(position.x + size.x - 4 - line * 0.5F, position.y + title * 0.5F, body);
    EXPECT_FALSE(open);
    ui.frame(body);
    EXPECT_TRUE(ui.context().draw_data().lists.empty()) << "a closed window is not drawn";
}

TEST(UiWindowTest, WindowsMoveByTheirTitleBarAndEmptySpaceUnlessTheyMayNot)
{
    UiHarness ui;
    Vector2 moving{};
    Vector2 fixed{};
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 50, .y = 50}, {.x = 200, .y = 150});
        frame.begin_window("Moving");
        moving = frame.window_position();
        frame.end_window();
        place(frame, {.x = 400, .y = 50}, {.x = 200, .y = 150});
        frame.begin_window("Fixed", UiWindowFlag::NO_MOVE);
        fixed = frame.window_position();
        frame.end_window();
    };
    ui.frames(2, body);

    // Dragged by the title bar: the window follows the mouse.
    const auto drag = [&](Vector2 from, Vector2 by)
    {
        ui.move(from.x, from.y);
        ui.frame(body);
        ui.press();
        ui.frame(body);
        ui.move(from.x + by.x, from.y + by.y);
        ui.frame(body);
        ui.release();
        ui.frame(body);
    };
    drag({.x = 150, .y = 58}, {.x = 40, .y = 30});
    EXPECT_EQ(moving, (Vector2{.x = 90, .y = 80}));

    // Dragged by empty space below its title.
    drag({.x = 190, .y = 180}, {.x = -20, .y = 10});
    EXPECT_EQ(moving, (Vector2{.x = 70, .y = 90}));

    // A window that may not move stays.
    drag({.x = 500, .y = 120}, {.x = 50, .y = 50});
    EXPECT_EQ(fixed, (Vector2{.x = 400, .y = 50}));
}

TEST(UiWindowTest, TheResizeGripResizesAndShowsTheResizeCursor)
{
    UiHarness ui;
    Vector2 size{};
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 50, .y = 50}, {.x = 200, .y = 150});
        frame.begin_window("Resizable");
        size = frame.window_size();
        frame.end_window();
    };
    ui.frames(2, body);

    // Over the grip: the cursor; dragging it moves the bottom-right corner.
    ui.move(247, 197);
    ui.frame(body);
    EXPECT_EQ(ui.services().cursor(), gfx::CursorShape::RESIZE_NORTHWEST_SOUTHEAST);
    ui.press();
    ui.frame(body);
    ui.move(287, 222);
    ui.frame(body);
    ui.release();
    ui.frame(body);
    EXPECT_EQ(size, (Vector2{.x = 240, .y = 175}));

    // Away from it: the arrow again; smaller than the minimum size: the minimum.
    ui.move(10, 10);
    ui.frame(body);
    EXPECT_EQ(ui.services().cursor(), gfx::CursorShape::ARROW);
    ui.move(287, 222);
    ui.frame(body);
    ui.press();
    ui.frame(body);
    ui.move(0, 0);
    ui.frame(body);
    ui.release();
    ui.frame(body);
    EXPECT_EQ(size, (Vector2{.x = 32, .y = 32}));
}

TEST(UiWindowTest, ClickingAWindowBringsItToTheFront)
{
    // Two overlapping windows; the second, made later, is in front. Each window's list starts with its background.
    UiHarness ui;
    bool front_hovered = false;
    const auto body = [&](UiFrame& frame)
    {
        frame.push_style_color(StyleColor::WINDOW_BACKGROUND, RED);
        place(frame, {.x = 50, .y = 50}, {.x = 200, .y = 150});
        frame.begin_window("Red");
        frame.end_window();
        frame.pop_style_color();
        frame.push_style_color(StyleColor::WINDOW_BACKGROUND, GREEN);
        place(frame, {.x = 150, .y = 100}, {.x = 200, .y = 150});
        frame.begin_window("Green");
        front_hovered = frame.is_window_hovered();
        frame.end_window();
        frame.pop_style_color();
    };
    ui.frames(2, body);
    const auto background_of_last = [&]
    {
        const UiDrawData& data = ui.context().draw_data();
        return data.lists.back()->vertices().front().color;
    };
    EXPECT_EQ(background_of_last(), GREEN.packed());

    // The overlap is the green window's.
    ui.move(200, 150);
    ui.frame(body);
    EXPECT_TRUE(front_hovered);

    // A click on the red window's visible part brings it in front.
    ui.click(80, 120, body);
    EXPECT_EQ(background_of_last(), RED.packed());
    ui.move(200, 150);
    ui.frame(body);
    EXPECT_FALSE(front_hovered);
}

TEST(UiWindowTest, ContentTallerThanTheWindowScrollsWithTheWheelAndTheScrollbar)
{
    UiHarness ui;
    float scroll = 0;
    float maximum = 0;
    float line_height = 0;
    Vector2 position{};
    Vector2 size{};
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 50, .y = 50}, {.x = 200, .y = 150});
        frame.begin_window("Scrolling");
        line_height = frame.text_line_height();
        for (int line = 0; line < 30; ++line)
        {
            frame.text("Line {}", line);
        }
        scroll = frame.scroll_y();
        maximum = frame.scroll_maximum_y();
        position = frame.window_position();
        size = frame.window_size();
        frame.end_window();
    };
    ui.frames(3, body);
    ASSERT_GT(maximum, 100.0F);
    EXPECT_EQ(scroll, 0.0F);

    // The wheel scrolls down (toward the user), by steps of 5 lines.
    ui.move(120, 120);
    ui.wheel(-1);
    ui.frame(body);
    EXPECT_GT(scroll, 40.0F);
    ui.wheel(-100);
    ui.frame(body);
    EXPECT_FLOAT_EQ(scroll, maximum) << "clamped to the content";

    // Dragging the scrollbar's grab to the top scrolls back (at the bottom of its track, which ends before the
    // resize grip, a line and a third high).
    const float bar_x = position.x + size.x - 7;
    const float grip = std::floor(line_height * 1.35F);
    ui.move(bar_x, position.y + size.y - grip - 4);
    ui.frame(body);
    ui.press();
    ui.frame(body);
    ui.move(bar_x, position.y - 100);
    ui.frame(body);
    ui.release();
    ui.frame(body);
    EXPECT_EQ(scroll, 0.0F);

    // Scrolled by code.
    bool set = false;
    ui.frame([&](UiFrame& frame)
    {
        body(frame);
        if (!set && frame.begin_window("Scrolling"))
        {
            frame.set_scroll_y(50);
            set = true;
        }
        frame.end_window();
    });
    ui.frame(body);
    EXPECT_FLOAT_EQ(scroll, 50.0F);
}

TEST(UiWindowTest, ChildRegionsClipAndScrollOnTheirOwn)
{
    UiHarness ui;
    float child_scroll = 0;
    float parent_scroll = 0;
    UiRectangle child{};
    bool last_visible = true;
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 50, .y = 50}, {.x = 300, .y = 300});
        frame.begin_window("Parent");
        frame.text_unformatted("Above");
        if (frame.begin_child("List", {.x = 150, .y = 80}, ChildFlag::BORDERS))
        {
            for (int line = 0; line < 20; ++line)
            {
                frame.text("Item {}", line);
            }
            last_visible = frame.is_item_visible();
            child_scroll = frame.scroll_y();
        }
        frame.end_child();
        child = frame.item_rectangle();
        parent_scroll = frame.scroll_y();
        frame.end_window();
    };
    const UiFrameReport report = ui.frames(3, body);
    EXPECT_TRUE(report.ok());
    EXPECT_FLOAT_EQ(child.width(), 150.0F);
    EXPECT_FLOAT_EQ(child.height(), 80.0F);
    EXPECT_FALSE(last_visible) << "clipped by the child";

    // The wheel over the child scrolls the child, not its parent.
    ui.move(center(child).x, center(child).y);
    ui.wheel(-1);
    ui.frame(body);
    EXPECT_GT(child_scroll, 0.0F);
    EXPECT_EQ(parent_scroll, 0.0F);
}

TEST(UiWindowTest, AlwaysAutoResizeFollowsTheContentAndAWindowBegunTwiceAppends)
{
    UiHarness ui;
    std::uint32_t lines = 2;
    float height = 0;
    float second_part_top = 0;
    float first_part_bottom = 0;
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 50, .y = 50});
        frame.begin_window("Auto", UiWindowFlag::ALWAYS_AUTO_RESIZE);
        for (std::uint32_t line = 0; line < lines; ++line)
        {
            frame.text("Line {}", line);
        }
        first_part_bottom = frame.item_rectangle().bottom;
        height = frame.window_size().y;
        frame.end_window();
        frame.begin_window("Auto");
        frame.text_unformatted("Appended");
        second_part_top = frame.item_rectangle().top;
        frame.end_window();
    };
    ui.frames(3, body);
    const float two_lines = height;
    lines = 4;
    ui.frames(2, body);
    EXPECT_GT(height, two_lines);
    EXPECT_GT(second_part_top, first_part_bottom) << "the second begin continues below the first part";
}

// ===================================================================================================== layout

TEST(UiLayoutTest, ItemsGoDownLinesAndSameLinePutsThemSideBySide)
{
    UiHarness ui;
    UiRectangle first{};
    UiRectangle second{};
    UiRectangle third{};
    UiRectangle indented{};
    UiRectangle after_dummy{};
    Vector2 cursor_start{};
    Vector2 available{};
    float spacing_x = 0;
    float spacing_y = 0;
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 0, .y = 0}, {.x = 400, .y = 400});
        frame.begin_window("Layout");
        cursor_start = frame.cursor_position();
        available = frame.content_region_available();
        frame.button("One");
        first = frame.item_rectangle();
        frame.same_line();
        frame.button("Two");
        second = frame.item_rectangle();
        frame.button("Three");
        third = frame.item_rectangle();
        frame.indent();
        frame.button("Indented");
        indented = frame.item_rectangle();
        frame.unindent();
        frame.dummy({.x = 10, .y = 40});
        frame.button("After");
        after_dummy = frame.item_rectangle();
        spacing_x = frame.style().item_spacing.x;
        spacing_y = frame.style().item_spacing.y;
        frame.end_window();
    };
    ui.frames(2, body);
    EXPECT_EQ(cursor_start, (Vector2{}));
    EXPECT_FLOAT_EQ(available.x, 400 - 16);
    EXPECT_FLOAT_EQ(second.left, first.right + spacing_x);
    EXPECT_FLOAT_EQ(second.top, first.top);
    EXPECT_FLOAT_EQ(third.left, first.left);
    EXPECT_FLOAT_EQ(third.top, first.bottom + spacing_y);
    EXPECT_FLOAT_EQ(indented.left, first.left + 21);
    EXPECT_FLOAT_EQ(after_dummy.top, indented.bottom + spacing_y + 40 + spacing_y);
    EXPECT_FLOAT_EQ(after_dummy.left, first.left);
}

TEST(UiLayoutTest, GroupsBecomeOneItem)
{
    UiHarness ui;
    UiRectangle group{};
    UiRectangle beside{};
    UiRectangle top{};
    UiRectangle bottom{};
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 0, .y = 0}, {.x = 400, .y = 400});
        frame.begin_window("Groups");
        frame.begin_group();
        frame.button("Top");
        top = frame.item_rectangle();
        frame.button("A longer bottom");
        bottom = frame.item_rectangle();
        frame.end_group();
        group = frame.item_rectangle();
        frame.same_line();
        frame.button("Beside");
        beside = frame.item_rectangle();
        frame.end_window();
    };
    ui.frames(2, body);
    EXPECT_FLOAT_EQ(group.left, top.left);
    EXPECT_FLOAT_EQ(group.top, top.top);
    EXPECT_FLOAT_EQ(group.right, std::max(top.right, bottom.right));
    EXPECT_FLOAT_EQ(group.bottom, bottom.bottom);
    EXPECT_FLOAT_EQ(beside.top, group.top);
    EXPECT_GT(beside.left, group.right);
}

TEST(UiLayoutTest, ColumnsSplitTheWidthAndRowsStartBelowTheTallestColumn)
{
    UiHarness ui;
    std::array<UiRectangle, 4> cells{};
    std::uint32_t count = 0;
    UiRectangle after{};
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 0, .y = 0}, {.x = 316, .y = 400});
        frame.begin_window("Columns");
        frame.columns(3, "grid");
        count = frame.column_count();
        frame.button("A");
        cells[0] = frame.item_rectangle();
        frame.next_column();
        frame.button("B");
        frame.button("B2");
        cells[1] = frame.item_rectangle();
        frame.next_column();
        frame.button("C");
        cells[2] = frame.item_rectangle();
        frame.next_column();
        frame.button("D");
        cells[3] = frame.item_rectangle();
        frame.columns(1);
        frame.button("After");
        after = frame.item_rectangle();
        frame.end_window();
    };
    ui.frames(2, body);
    EXPECT_EQ(count, 3U);
    EXPECT_FLOAT_EQ(cells[1].left, cells[0].left + 100 + 8) << "a third of 300, plus the spacing";
    EXPECT_FLOAT_EQ(cells[2].left, cells[0].left + 200 + 8);
    EXPECT_FLOAT_EQ(cells[2].top, cells[0].top);
    EXPECT_FLOAT_EQ(cells[3].left, cells[0].left);
    EXPECT_GT(cells[3].top, cells[1].bottom) << "below the tallest column";
    EXPECT_FLOAT_EQ(after.left, cells[0].left);
    EXPECT_GT(after.top, cells[3].bottom);
}

TEST(UiLayoutTest, TextWrapsAtTheContentRegionAndSizesMatchTheLayout)
{
    UiHarness ui;
    UiRectangle wrapped{};
    UiRectangle single{};
    Vector2 measured{};
    Vector2 measured_wrapped{};
    float line = 0;
    const std::string_view text = "The quick brown fox jumps over the lazy dog, again and again.";
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 0, .y = 0}, {.x = 216, .y = 400});
        frame.begin_window("Text");
        frame.text_unformatted(text);
        single = frame.item_rectangle();
        frame.text_wrapped("{}", text);
        wrapped = frame.item_rectangle();
        measured = frame.calculate_text_size(text);
        measured_wrapped = frame.calculate_text_size(text, false, 200);
        line = frame.text_line_height();
        frame.end_window();
    };
    ui.frames(2, body);
    EXPECT_FLOAT_EQ(single.width(), measured.x);
    EXPECT_FLOAT_EQ(single.height(), line);
    EXPECT_LE(wrapped.width(), 200.0F);
    EXPECT_GE(wrapped.height(), line * 2);
    EXPECT_FLOAT_EQ(wrapped.height(), measured_wrapped.y);
    EXPECT_EQ(ui.frame([](UiFrame& frame) { static_cast<void>(frame.calculate_text_size("Text##hidden", true)); }).ok(), true);
}

// ===================================================================================================== widgets

TEST(UiWidgetTest, CheckboxesRadioButtonsSwitchesAndSelectablesChangeTheirValues)
{
    UiHarness ui;
    bool checked = false;
    bool switched = false;
    int choice = 0;
    bool selected = false;
    gfx::MouseButtonMask flags = gfx::MouseButtonMask::NONE;
    std::array<UiRectangle, 5> rectangles{};
    bool edited = false;
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 0, .y = 0}, {.x = 400, .y = 400});
        frame.begin_window("Values");
        frame.checkbox("Checked", checked);
        rectangles[0] = frame.item_rectangle();
        edited = edited || frame.is_item_edited();
        frame.radio_button("First", choice, 1);
        frame.radio_button("Second", choice, 2);
        rectangles[1] = frame.item_rectangle();
        frame.toggle("Switch", switched);
        rectangles[2] = frame.item_rectangle();
        frame.selectable("Selectable", &selected);
        rectangles[3] = frame.item_rectangle();
        frame.checkbox_flags("Middle", flags, gfx::MouseButtonMask::MIDDLE);
        rectangles[4] = frame.item_rectangle();
        frame.end_window();
    };
    ui.frames(2, body);
    for (const UiRectangle& rectangle : rectangles)
    {
        ui.click(center(rectangle).x, center(rectangle).y, body);
    }
    EXPECT_TRUE(checked);
    EXPECT_TRUE(edited);
    EXPECT_EQ(choice, 2);
    EXPECT_TRUE(switched);
    EXPECT_TRUE(selected);
    EXPECT_EQ(flags, gfx::MouseButtonMask::MIDDLE);
    ui.click(center(rectangles[0]).x, center(rectangles[0]).y, body);
    ui.click(center(rectangles[4]).x, center(rectangles[4]).y, body);
    EXPECT_FALSE(checked);
    EXPECT_EQ(flags, gfx::MouseButtonMask::NONE);
}

TEST(UiWidgetTest, InvisibleSmallAndArrowButtonsAreClickable)
{
    UiHarness ui;
    std::array<bool, 3> pressed{};
    std::array<UiRectangle, 3> rectangles{};
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 0, .y = 0}, {.x = 400, .y = 400});
        frame.begin_window("Buttons");
        pressed[0] = frame.invisible_button("area", {.x = 60, .y = 30}) || pressed[0];
        rectangles[0] = frame.item_rectangle();
        pressed[1] = frame.small_button("small") || pressed[1];
        rectangles[1] = frame.item_rectangle();
        pressed[2] = frame.arrow_button("arrow", ArrowDirection::RIGHT) || pressed[2];
        rectangles[2] = frame.item_rectangle();
        frame.end_window();
    };
    ui.frames(2, body);
    EXPECT_EQ(rectangles[0].width(), 60.0F);
    EXPECT_FLOAT_EQ(rectangles[2].width(), rectangles[2].height());
    for (const UiRectangle& rectangle : rectangles)
    {
        ui.click(center(rectangle).x, center(rectangle).y, body);
    }
    EXPECT_EQ(pressed, (std::array{true, true, true}));

    // An invisible button needs a size.
    const UiFrameReport report = ui.frame([](UiFrame& frame)
    {
        frame.begin_window("Buttons");
        static_cast<void>(frame.invisible_button("empty", {}));
        frame.end_window();
    });
    EXPECT_TRUE(has_issue(report, UiFrameProblem::INVALID_ARGUMENT, "empty"));
}

TEST(UiWidgetTest, DisabledItemsIgnoreInput)
{
    UiHarness ui;
    bool pressed = false;
    bool checked = false;
    UiRectangle button{};
    UiRectangle box{};
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 0, .y = 0}, {.x = 400, .y = 400});
        frame.begin_window("Disabled");
        frame.begin_disabled();
        pressed = frame.button("Off") || pressed;
        button = frame.item_rectangle();
        frame.checkbox("Off too", checked);
        box = frame.item_rectangle();
        frame.end_disabled();
        frame.end_window();
    };
    ui.frames(2, body);
    ui.click(center(button).x, center(button).y, body);
    ui.click(center(box).x, center(box).y, body);
    EXPECT_FALSE(pressed);
    EXPECT_FALSE(checked);
}

TEST(UiWidgetTest, LabelsBulletsSeparatorsAndTextVariantsLayOut)
{
    UiHarness ui;
    UiRectangle label{};
    UiRectangle bullet{};
    UiRectangle separator{};
    UiRectangle separator_text{};
    float frame_height = 0;
    const UiFrameReport report = ui.frames(2, [&](UiFrame& frame)
    {
        place(frame, {.x = 0, .y = 0}, {.x = 400, .y = 400});
        frame.begin_window("Text");
        frame.label("Speed", "{:.1f} m/s", 12.5);
        label = frame.item_rectangle();
        frame.bullet_text("{} items", 3);
        bullet = frame.item_rectangle();
        frame.separator();
        separator = frame.item_rectangle();
        frame.separator_text("Section");
        separator_text = frame.item_rectangle();
        frame.text_colored(RED, "Red {}", 1);
        frame.text_disabled("Disabled {}", 2);
        frame.bullet();
        frame.text_unformatted("after a bullet");
        frame.align_text_to_frame_padding();
        frame.text_unformatted("aligned");
        frame_height = frame.frame_height();
        frame.end_window();
    });
    EXPECT_TRUE(report.ok());
    EXPECT_FLOAT_EQ(label.height(), frame_height);
    EXPECT_GT(label.width(), 0.0F);
    EXPECT_GT(bullet.width(), 0.0F);
    EXPECT_FLOAT_EQ(separator.height(), 1.0F);
    EXPECT_FLOAT_EQ(separator.width(), 400.0F) << "across the window";
    EXPECT_GT(separator_text.height(), 0.0F);
    EXPECT_TRUE(has_vertex_color(ui.context().draw_data(), RED)) << "the colored text";
}

// ===================================================================================================== identifiers

TEST(UiIdentifierTest, LabelsIdentifyWidgetsUnderTheIdentifierStack)
{
    UiHarness ui;
    WidgetIdentifier first{};
    WidgetIdentifier second{};
    WidgetIdentifier hidden_a{};
    WidgetIdentifier hidden_b{};
    WidgetIdentifier restarted_a{};
    WidgetIdentifier restarted_b{};
    WidgetIdentifier pushed{};
    ui.frame([&](UiFrame& frame)
    {
        frame.begin_window("First");
        first = frame.identifier("Save");
        hidden_a = frame.identifier("Save##toolbar");
        hidden_b = frame.identifier("Save##menu");
        restarted_a = frame.identifier("Saved 1 file###status");
        restarted_b = frame.identifier("Saved 2 files###status");
        frame.push_id(7);
        pushed = frame.identifier("Save");
        frame.pop_id();
        frame.end_window();
        frame.begin_window("Second");
        second = frame.identifier("Save");
        frame.end_window();
    });
    EXPECT_NE(first, second) << "windows seed their widgets";
    EXPECT_NE(hidden_a, hidden_b);
    EXPECT_EQ(restarted_a, restarted_b);
    EXPECT_NE(pushed, first);
    EXPECT_TRUE(first.is_valid());
}

TEST(UiIdentifierTest, TwoWidgetsWithOneIdentifierAreReported)
{
    UiHarness ui;
    const auto conflicting = [](UiFrame& frame)
    {
        frame.begin_window("Conflicts");
        frame.button("OK");
        frame.button("OK");
        frame.end_window();
    };
    const UiFrameReport report = ui.frame(conflicting);
    EXPECT_TRUE(has_issue(report, UiFrameProblem::IDENTIFIER_CONFLICT, "OK"));

    // Told apart by push_id() or a suffix.
    const UiFrameReport fixed = ui.frame([](UiFrame& frame)
    {
        frame.begin_window("Conflicts");
        for (std::int32_t row = 0; row < 3; ++row)
        {
            frame.push_id(row);
            frame.button("OK");
            frame.pop_id();
        }
        frame.button("OK##last");
        frame.end_window();
    });
    EXPECT_TRUE(fixed.ok());
}

// ===================================================================================================== keyboard

TEST(UiKeyboardTest, TabMovesTheFocusAndSpaceActivatesTheFocusedItem)
{
    UiHarness ui;
    std::array<bool, 3> focused{};
    std::array<std::uint32_t, 3> presses{};
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 0, .y = 0}, {.x = 300, .y = 300});
        frame.begin_window("Focus");
        for (std::size_t button = 0; button < 3; ++button)
        {
            const std::array labels{"First", "Second", "Third"};
            presses[button] += frame.button(labels[button]) ? 1U : 0U;
            focused[button] = frame.is_item_focused();
        }
        frame.end_window();
    };
    ui.frames(2, body);
    EXPECT_EQ(focused, (std::array{false, false, false}));

    // Tab: the first item; Tab: the second; Shift+Tab: back; Shift+Tab: around to the last.
    ui.tap(Key::TAB);
    ui.frame(body);
    ui.frame(body);
    EXPECT_EQ(focused, (std::array{true, false, false}));
    EXPECT_TRUE(ui.context().wants_keyboard());
    ui.tap(Key::TAB);
    ui.frame(body);
    ui.frame(body);
    EXPECT_EQ(focused, (std::array{false, true, false}));
    ui.modifiers(KeyModifier::LEFT_SHIFT);
    ui.tap(Key::TAB);
    ui.frame(body);
    ui.tap(Key::TAB);
    ui.frame(body);
    ui.frame(body);
    ui.modifiers(KeyModifier::NONE);
    EXPECT_EQ(focused, (std::array{false, false, true}));

    // Space presses the focused item; Escape drops the focus.
    ui.tap(Key::SPACE);
    ui.frame(body);
    EXPECT_EQ(presses, (std::array<std::uint32_t, 3>{0, 0, 1}));
    ui.tap(Key::ESCAPE);
    ui.frame(body);
    ui.frame(body);
    EXPECT_EQ(focused, (std::array{false, false, false}));
    EXPECT_FALSE(ui.context().wants_keyboard());
}

TEST(UiKeyboardTest, KeysAreReportedPressedRepeatedAndReleased)
{
    UiHarness ui;
    std::array<bool, 4> state{};
    const auto body = [&](UiFrame& frame)
    {
        state = {frame.is_key_down(Key::A), frame.is_key_pressed(Key::A), frame.is_key_pressed(Key::A, false), frame.is_key_released(Key::A)};
    };
    ui.key(Key::A, true);
    ui.frame(body);
    EXPECT_EQ(state, (std::array{true, true, true, false}));
    ui.key(Key::A, true, true);
    ui.frame(body);
    EXPECT_EQ(state, (std::array{true, true, false, false})) << "a repeat";
    ui.frame(body);
    EXPECT_EQ(state, (std::array{true, false, false, false}));

    // Losing the focus releases what is held.
    ui.focus(false);
    ui.frame(body);
    EXPECT_EQ(state, (std::array{false, false, false, true}));
}

// ===================================================================================================== mouse and items

TEST(UiItemTest, ItemQueriesFollowTheMouse)
{
    UiHarness ui;
    struct Flags
    {
        bool hovered = false;
        bool active = false;
        bool activated = false;
        bool deactivated = false;
        bool clicked = false;
        bool text_hovered = false;
    };
    Flags flags{};
    UiRectangle button{};
    UiRectangle text{};
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 0, .y = 0}, {.x = 300, .y = 300});
        frame.begin_window("Items");
        frame.button("Button");
        flags = {.hovered = frame.is_item_hovered(),
                 .active = frame.is_item_active(),
                 .activated = frame.is_item_activated(),
                 .deactivated = frame.is_item_deactivated(),
                 .clicked = frame.is_item_clicked()};
        button = frame.item_rectangle();
        frame.text_unformatted("Some text");
        text = frame.item_rectangle();
        flags.text_hovered = frame.is_item_hovered();
        frame.end_window();
    };
    ui.frames(2, body);
    ui.move(center(button).x, center(button).y);
    ui.frame(body);
    EXPECT_TRUE(flags.hovered);
    EXPECT_FALSE(flags.active);
    ui.press();
    ui.frame(body);
    EXPECT_TRUE(flags.active);
    EXPECT_TRUE(flags.activated);
    EXPECT_TRUE(flags.clicked);
    ui.frame(body);
    EXPECT_TRUE(flags.active);
    EXPECT_FALSE(flags.activated);
    ui.release();
    ui.frame(body);
    EXPECT_FALSE(flags.active);
    EXPECT_TRUE(flags.deactivated);
    ui.move(center(text).x, center(text).y);
    ui.frame(body);
    EXPECT_TRUE(flags.text_hovered) << "items without identifier report hovering too";
    EXPECT_TRUE(ui.context().wants_mouse());
    ui.move(700, 500);
    ui.frame(body);
    EXPECT_FALSE(ui.context().wants_mouse());
}

TEST(UiItemTest, DoubleClicksAndDragsAreRecognized)
{
    UiHarness ui;
    bool double_clicked = false;
    bool dragging = false;
    Vector2 delta{};
    const auto body = [&](UiFrame& frame)
    {
        double_clicked = double_clicked || frame.is_mouse_double_clicked(MouseButton::LEFT);
        dragging = frame.is_mouse_dragging(MouseButton::LEFT);
        delta = frame.mouse_drag_delta();
    };
    ui.move(100, 100);
    ui.press();
    ui.release();
    ui.frame(body);
    ui.press();
    ui.release();
    ui.frame(body);
    EXPECT_TRUE(double_clicked);

    // Too late for a double click.
    double_clicked = false;
    ui.press();
    ui.release();
    ui.frame(body);
    ui.wait(1.0);
    ui.press();
    ui.release();
    ui.frame(body);
    EXPECT_FALSE(double_clicked);

    // Held and moved past the threshold: a drag.
    ui.press();
    ui.frame(body);
    ui.move(103, 100);
    ui.frame(body);
    EXPECT_FALSE(dragging);
    ui.move(120, 110);
    ui.frame(body);
    EXPECT_TRUE(dragging);
    EXPECT_EQ(delta, (Vector2{.x = 20, .y = 10}));
    ui.release();
    ui.frame(body);
    EXPECT_FALSE(dragging);
}

// ===================================================================================================== style stacks

TEST(UiStyleTest, PushedColorsAndVariablesApplyUntilPopped)
{
    UiHarness ui;
    UiRectangle normal{};
    UiRectangle padded{};
    const auto body = [&](UiFrame& frame)
    {
        place(frame, {.x = 0, .y = 0}, {.x = 300, .y = 300});
        frame.begin_window("Style");
        frame.button("Same");
        normal = frame.item_rectangle();
        frame.push_style_color(StyleColor::BUTTON, GREEN);
        frame.push_style_variable(StyleVariable::FRAME_PADDING, Vector2{.x = 20, .y = 10});
        frame.button("Same##padded");
        padded = frame.item_rectangle();
        frame.pop_style_variable();
        frame.pop_style_color();
        frame.end_window();
    };
    const UiFrameReport report = ui.frames(2, body);
    EXPECT_TRUE(report.ok());
    EXPECT_FLOAT_EQ(padded.width(), normal.width() + 32);
    EXPECT_FLOAT_EQ(padded.height(), normal.height() + 14);
    EXPECT_TRUE(has_vertex_color(ui.context().draw_data(), GREEN));
}

TEST(UiStyleTest, UnbalancedAndMismatchedStacksAreReportedAndRestored)
{
    UiHarness ui;
    const UiFrameReport unbalanced = ui.frame([](UiFrame& frame)
    {
        frame.push_style_color(StyleColor::BUTTON, GREEN);
        frame.push_style_variable(StyleVariable::FRAME_PADDING, 2.0F);
        frame.pop_style_variable();
        frame.pop_id();
        frame.push_font(gfx::ui::FontHandle{});
    });
    EXPECT_TRUE(has_any(unbalanced.problems, UiFrameProblem::UNBALANCED_STACK));
    EXPECT_TRUE(has_any(unbalanced.problems, UiFrameProblem::INVALID_ARGUMENT));
    EXPECT_TRUE(has_any(unbalanced.problems, UiFrameProblem::NO_FONT));
    Color button{};
    const UiFrameReport next = ui.frame([&](UiFrame& frame) { button = frame.style_color(StyleColor::BUTTON); });
    EXPECT_TRUE(next.ok());
    EXPECT_EQ(button, builtin_theme(ThemeKind::DARK).color(StyleColor::BUTTON)) << "restored from the theme";
}

TEST(UiStyleTest, ThemesChangeTheColorsDrawn)
{
    UiHarness ui;
    const auto body = [](UiFrame& frame)
    {
        place(frame, {.x = 0, .y = 0}, {.x = 300, .y = 300});
        frame.begin_window("Themed");
        frame.end_window();
    };
    ui.frames(2, body);
    const Theme light = builtin_theme(ThemeKind::LIGHT);
    EXPECT_FALSE(has_vertex_color(ui.context().draw_data(), light.color(StyleColor::WINDOW_BACKGROUND)));
    ui.context().set_theme(light);
    ui.frame(body);
    EXPECT_TRUE(has_vertex_color(ui.context().draw_data(), light.color(StyleColor::WINDOW_BACKGROUND)));
    EXPECT_EQ(ui.context().theme().name, "Light");
}

// ===================================================================================================== the report

TEST(UiReportTest, MisuseIsReportedWithoutThrowing)
{
    UiHarness ui;
    const UiFrameReport report = ui.frame([](UiFrame& frame)
    {
        frame.button("Nowhere");
        frame.end_window();
        frame.begin_window("Left open");
    });
    EXPECT_TRUE(has_issue(report, UiFrameProblem::WIDGET_OUTSIDE_WINDOW));
    EXPECT_TRUE(has_issue(report, UiFrameProblem::END_WITHOUT_BEGIN));
    EXPECT_TRUE(has_issue(report, UiFrameProblem::UNBALANCED_STACK, "Left open"));
    EXPECT_FALSE(report.ok());

    // A frame used after end_frame() reports into the next frame.
    UiFrame* stale = nullptr;
    ui.frame([&](UiFrame& frame) { stale = &frame; });
    static_cast<void>(stale->button("Late"));
    const UiFrameReport late = ui.frame([](UiFrame&) {});
    EXPECT_TRUE(has_issue(late, UiFrameProblem::FRAME_NOT_ACTIVE));
}

TEST(UiReportTest, ContextsWithoutFontsReportItOncePerFrame)
{
    UiHarness ui{false};
    const UiFrameReport report = ui.frame([](UiFrame& frame)
    {
        frame.begin_window("No font");
        frame.text_unformatted("one");
        frame.text_unformatted("two");
        frame.end_window();
    });
    EXPECT_EQ(std::ranges::count_if(report.issues, [](const UiFrameIssue& issue) { return issue.problem == UiFrameProblem::NO_FONT; }), 1);
}

TEST(UiReportTest, TooManyIssuesAreCountedAsDropped)
{
    UiHarness ui;
    const UiFrameReport report = ui.frame([](UiFrame& frame)
    {
        frame.begin_window("Flood");
        for (std::size_t button = 0; button < MAXIMUM_FRAME_ISSUES + 10; ++button)
        {
            frame.button("Same");
        }
        frame.end_window();
    });
    EXPECT_EQ(report.issues.size(), MAXIMUM_FRAME_ISSUES);
    EXPECT_EQ(report.dropped_issues, 9U);
    EXPECT_TRUE(has_any(report.problems, UiFrameProblem::ISSUES_DROPPED));
}

TEST(UiReportTest, WidgetsOfLaterPhasesReportOnceAndDoNothing)
{
    UiHarness ui;
    std::array<bool, 3> results{true, true, true};
    const UiFrameReport report = ui.frame([&](UiFrame& frame)
    {
        frame.begin_window("Later");
        results[0] = frame.begin_table("table", 3);
        results[1] = frame.begin_table("table##again", 3);
        results[2] = frame.table_next_column();
        frame.end_window();
    });
    EXPECT_EQ(results, (std::array{false, false, false}));
    EXPECT_EQ(std::ranges::count_if(report.issues, [](const UiFrameIssue& issue) { return issue.operation == "begin_table"; }), 1);
    EXPECT_TRUE(std::ranges::any_of(report.issues, [](const UiFrameIssue& issue) { return issue.operation == "table_next_column"; }));
    EXPECT_THROW(static_cast<void>(ui.context().dock_builder(DockSpaceIdentifier::from_name("editor"))), gfx::BackendCapabilityError);
    EXPECT_THROW(static_cast<void>(ui.context().save_layout()), gfx::BackendCapabilityError);
}

TEST(UiReportTest, TheDiagnosticsWindowShowsThePreviousReport)
{
    UiHarness ui;
    bool open = true;
    ui.frame([](UiFrame& frame) { frame.button("Outside"); });
    const UiFrameReport report = ui.frames(2, [&](UiFrame& frame) { frame.show_diagnostics_window(open); });
    EXPECT_TRUE(report.ok());
    EXPECT_FALSE(ui.context().draw_data().lists.empty());
}

TEST(UiContextTest, FontsAreCheckedWhenLoaded)
{
    UiContext context;
    EXPECT_FALSE(context.default_font().is_valid());
    EXPECT_THROW(static_cast<void>(context.load_font(nullptr, 16)), gfx::InvalidUsageError);
    EXPECT_THROW(context.set_default_font(FontHandle{}), gfx::InvalidUsageError);
}

// ===================================================================================================== draw data

TEST(UiDrawDataTest, DrawDataIsInPixelsWithTheUnitsScale)
{
    UiHarness ui;
    ui.scale(2.0F);
    const auto body = [](UiFrame& frame)
    {
        frame.background_draw_list().add_rectangle_filled({.left = 0, .top = 0, .right = 10, .bottom = 10}, RED);
        place(frame, {.x = 0, .y = 0}, {.x = 100, .y = 100});
        frame.begin_window("Scaled");
        frame.end_window();
        frame.foreground_draw_list().add_rectangle_filled({.left = 0, .top = 0, .right = 10, .bottom = 10}, GREEN);
    };
    ui.frames(2, body);
    const UiDrawData& data = ui.context().draw_data();
    EXPECT_EQ(data.width, UiHarness::DISPLAY.x * 2);
    EXPECT_EQ(data.height, UiHarness::DISPLAY.y * 2);
    EXPECT_EQ(data.scale, 2.0F);
    ASSERT_EQ(data.lists.size(), 3U);
    EXPECT_EQ(data.lists.front()->vertices().front().color, RED.packed()) << "the background first";
    EXPECT_EQ(data.lists.back()->vertices().front().color, GREEN.packed()) << "the foreground last";
}

TEST(UiDrawDataTest, ARenderThreadReadsPublishedDataWhileFramesAreBuilt)
{
    UiHarness ui;
    std::atomic<bool> done{false};
    std::atomic<std::uint64_t> leases{0};
    std::atomic<std::uint64_t> inconsistent{0};
    std::jthread render{[&]
    {
        // Each lease's lists are complete: every index names a vertex.
        while (!done.load())
        {
            const UiDrawDataLease lease = ui.context().acquire_draw_data();
            for (const DrawList* list : lease.data().lists)
            {
                const std::size_t vertices = list->vertices().size();
                inconsistent += std::ranges::any_of(list->indices(), [vertices](std::uint32_t index) { return index >= vertices; }) ? 1U : 0U;
            }
            ++leases;
        }
    }};
    for (std::uint32_t frame = 0; frame < 300; ++frame)
    {
        ui.frame([frame](UiFrame& ui_frame)
        {
            place(ui_frame, {.x = 10, .y = 10}, {.x = 300, .y = 300});
            ui_frame.begin_window("Busy");
            for (std::uint32_t line = 0; line < 1 + frame % 20; ++line)
            {
                ui_frame.text("Frame {} line {}", frame, line);
            }
            ui_frame.end_window();
        });
    }
    done = true;
    render.join();
    EXPECT_GT(leases.load(), 0U);
    EXPECT_EQ(inconsistent.load(), 0U);
}

} // namespace
