// Tables and the visualization widgets of UiFrame driven by synthetic input: table layout (rows, cells, sizing
// policies, widths and weights), resizing by the column borders, reordering by the headers, hiding from the context
// menu, sort specifications, frozen rows and columns while scrolling, the list clipper (in a table and alone), the
// table state kept between frames, spanning selectables, indentation, misuse; splitters, resizable child regions,
// plots, progress bars and images.
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

/// The style the tests' contexts use.
const Style STYLE = builtin_theme(ThemeKind::DARK).style;

/// Runs @p body inside a window "Tables" at (20, 20), 420 by 400.
template<class Body>
auto
in_window(Body&& body)
{
    return [body = std::forward<Body>(body)](UiFrame& frame)
    {
        frame.set_next_window_position({.x = 20, .y = 20}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size({.x = 420, .y = 400}, UiCondition::FIRST_USE_EVER);
        if (frame.begin_window("Tables"))
        {
            body(frame);
        }
        frame.end_window();
    };
}

/// Where a cell's content starts and how wide it is.
struct Cell
{
    Vector2 start{};
    float width = 0;
};

/// The cells of a table of @p columns columns and @p rows rows (each showing "r<row>c<column>" unless @p text gives
/// it), its rectangle and its top-left corner.
struct TableProbe
{
    std::vector<Cell> cells{};
    UiRectangle rectangle{};
    Vector2 top{};
    std::uint32_t columns = 0;

    [[nodiscard]] auto at(std::uint32_t row,
                          std::uint32_t column) const -> const Cell&
    {
        return cells.at(std::size_t{row} * columns + column);
    }
};

/// A frame body with a table "probe" of @p columns columns described by @p setup (columns, freezing), a header row when
/// @p headers, then @p rows rows of text cells recorded in @p probe.
template<class Setup>
auto
table_body(TableProbe& probe,
           std::uint32_t columns,
           std::uint32_t rows,
           TableFlag flags,
           Setup setup,
           bool headers = false,
           Vector2 outer_size = {})
{
    probe.columns = columns;
    return in_window([&probe, columns, rows, flags, setup, headers, outer_size](UiFrame& frame)
    {
        probe.top = frame.cursor_screen_position();
        probe.cells.assign(std::size_t{columns} * rows, Cell{});
        if (frame.begin_table("probe", columns, flags, outer_size))
        {
            setup(frame);
            if (headers)
            {
                frame.table_headers_row();
            }
            for (std::uint32_t row = 0; row < rows; ++row)
            {
                frame.table_next_row();
                for (std::uint32_t column = 0; column < columns; ++column)
                {
                    if (frame.table_set_column_index(column))
                    {
                        probe.cells[std::size_t{row} * columns + column] = {.start = frame.cursor_screen_position(), .width = frame.content_region_available().x};
                        frame.text("r{}c{}", row, column);
                    }
                }
            }
            frame.end_table();
            probe.rectangle = frame.item_rectangle();
        }
    });
}

/// No set-up: every column gets the defaults.
constexpr auto NO_SETUP = [](UiFrame&) {};

/// The center of the header of column @p column of @p probe (cells padded by the style's cell padding).
auto
header_center(const TableProbe& probe,
              std::uint32_t column) -> Vector2
{
    const Cell& cell = probe.at(0, column);
    return {.x = cell.start.x + cell.width * 0.5F, .y = probe.top.y + 4.0F};
}

/// Drags the mouse from @p from to @p to in @p steps frames, the left button held, running @p body each frame.
template<class Body>
void
drag(UiHarness& ui,
     Vector2 from,
     Vector2 to,
     std::uint32_t steps,
     Body&& body)
{
    ui.move(from.x, from.y);
    ui.frame(body);
    ui.press();
    ui.frame(body);
    for (std::uint32_t step = 1; step <= steps; ++step)
    {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        ui.move(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
        ui.frame(body);
    }
    ui.release();
    ui.frame(body);
    ui.wait(1.0);
}

/// Right-clicks at @p at (running @p body each frame).
template<class Body>
void
right_click(UiHarness& ui,
            Vector2 at,
            Body&& body)
{
    ui.move(at.x, at.y);
    ui.frame(body);
    ui.press(MouseButton::RIGHT);
    ui.frame(body);
    ui.release(MouseButton::RIGHT);
    ui.frame(body);
}

/// Chooses the @p item-th focusable item (from 1) of the focused popup with the keyboard.
template<class Body>
void
choose_with_keyboard(UiHarness& ui,
                     std::uint32_t item,
                     Body&& body)
{
    for (std::uint32_t step = 0; step < item; ++step)
    {
        ui.tap(Key::DOWN);
        ui.frame(body);
    }
    ui.tap(Key::RETURN);
    ui.frame(body);
    ui.frame(body);
}

// ===================================================================================================== layout

TEST(UiTableTest, CellsAreLaidOutInRowsAndStretchedColumnsShareTheWidth)
{
    UiHarness ui;
    TableProbe probe{};
    std::array<std::int32_t, 6> rows{};
    std::array<std::int32_t, 6> columns{};
    float after = 0;
    float line = 0;
    const auto body = in_window([&](UiFrame& frame)
    {
        line = frame.text_line_height();
        probe.top = frame.cursor_screen_position();
        probe.cells.assign(6, Cell{});
        probe.columns = 3;
        if (frame.begin_table("cells", 3, TableFlag::BORDERS))
        {
            for (std::size_t cell = 0; cell < 6; ++cell)
            {
                EXPECT_TRUE(frame.table_next_column());
                probe.cells[cell] = {.start = frame.cursor_screen_position(), .width = frame.content_region_available().x};
                rows[cell] = frame.table_row_index();
                columns[cell] = frame.table_column_index();
                frame.text("Cell {}", cell);
            }
            EXPECT_EQ(frame.table_column_count(), 3U);
            frame.end_table();
            probe.rectangle = frame.item_rectangle();
        }
        after = frame.cursor_screen_position().y;
    });
    const UiFrameReport report = ui.frames(3, body);
    EXPECT_TRUE(report.ok()) << (report.issues.empty() ? "" : report.issues.front().detail);

    // Rows of three cells; equal widths filling the table (outer borders pad its edges).
    EXPECT_EQ(rows, (std::array{0, 0, 0, 1, 1, 1}));
    EXPECT_EQ(columns, (std::array{0, 1, 2, 0, 1, 2}));
    const float padding = STYLE.cell_padding.x;
    EXPECT_NEAR(probe.at(0, 1).width, probe.at(0, 0).width, 1.0F);
    EXPECT_NEAR(probe.at(0, 2).width, probe.at(0, 0).width, 1.0F);
    EXPECT_NEAR(probe.at(0, 0).width + probe.at(0, 1).width + probe.at(0, 2).width + padding * 6, probe.rectangle.width(), 0.01F);
    EXPECT_FLOAT_EQ(probe.at(0, 0).start.x, probe.rectangle.left + padding);
    EXPECT_FLOAT_EQ(probe.at(0, 1).start.x, probe.at(0, 0).start.x + probe.at(0, 0).width + padding * 2);
    EXPECT_FLOAT_EQ(probe.at(0, 0).start.y, probe.top.y + STYLE.cell_padding.y);

    // A row is a line of text and its cell padding tall; the table is an item of the window.
    EXPECT_FLOAT_EQ(probe.at(1, 0).start.y - probe.at(0, 0).start.y, line + STYLE.cell_padding.y * 2);
    EXPECT_FLOAT_EQ(probe.rectangle.bottom, probe.top.y + (line + STYLE.cell_padding.y * 2) * 2);
    EXPECT_FLOAT_EQ(after, probe.rectangle.bottom + STYLE.item_spacing.y);
}

TEST(UiTableTest, FixedColumnsFitTheirContentAndKeepTheirAskedWidths)
{
    UiHarness ui;
    std::array<float, 3> widths{};
    std::array<float, 3> texts{};
    const auto body = in_window([&](UiFrame& frame)
    {
        constexpr std::array LABELS{std::string_view{"A"}, std::string_view{"A much longer cell"}, std::string_view{"Middle"}};
        if (frame.begin_table("fit", 3, TableFlag::SIZING_FIXED_FIT))
        {
            for (std::size_t column = 0; column < 3; ++column)
            {
                frame.table_next_column();
                widths[column] = frame.content_region_available().x;
                texts[column] = frame.calculate_text_size(LABELS[column]).x;
                frame.text_unformatted(LABELS[column]);
            }
            frame.end_table();
        }
    });
    ui.frames(3, body);
    for (std::size_t column = 0; column < 3; ++column)
    {
        EXPECT_NEAR(widths[column], std::max(texts[column], MINIMUM_TABLE_COLUMN_WIDTH), 0.01F) << column;
    }

    // Asked widths and weights: a fixed column of 100, stretched columns sharing the rest 1 to 3.
    UiHarness weighted;
    TableProbe probe{};
    weighted.frames(3, table_body(probe, 3, 1, TableFlag::NONE, [](UiFrame& frame)
    {
        frame.table_setup_column("Fixed", TableColumnFlag::WIDTH_FIXED, 100.0F);
        frame.table_setup_column("One", TableColumnFlag::WIDTH_STRETCH, 1.0F);
        frame.table_setup_column("Three", TableColumnFlag::WIDTH_STRETCH, 3.0F);
    }));
    EXPECT_FLOAT_EQ(probe.at(0, 0).width, 100.0F);
    EXPECT_NEAR(probe.at(0, 2).width, probe.at(0, 1).width * 3.0F, 3.0F);
    EXPECT_NEAR(probe.at(0, 2).start.x + probe.at(0, 2).width, probe.rectangle.right, 0.01F) << "no outer padding without outer borders";
}

TEST(UiTableTest, TablesWithoutHostExtensionAreAsWideAsTheirColumns)
{
    UiHarness ui;
    TableProbe probe{};
    ui.frames(3, table_body(probe, 2, 1, TableFlag::SIZING_FIXED_FIT | TableFlag::NO_HOST_EXTEND_X, [](UiFrame& frame)
    {
        frame.table_setup_column("A", TableColumnFlag::NONE, 50.0F);
        frame.table_setup_column("B", TableColumnFlag::NONE, 60.0F);
    }));
    EXPECT_NEAR(probe.rectangle.width(), 50.0F + 60.0F + STYLE.cell_padding.x * 2, 0.01F);

    // A least height: the table is at least that tall.
    UiHarness tall;
    TableProbe high{};
    tall.frames(3, table_body(high, 2, 1, TableFlag::NONE, NO_SETUP, false, {.x = 0, .y = 120}));
    EXPECT_FLOAT_EQ(high.rectangle.height(), 120.0F);
}

// ===================================================================================================== resizing

TEST(UiTableTest, DraggingABorderResizesAFixedColumnAndADoubleClickFitsIt)
{
    UiHarness ui;
    TableProbe probe{};
    const auto body = table_body(probe, 3, 4, TableFlag::RESIZABLE | TableFlag::SIZING_FIXED_FIT, [](UiFrame& frame)
    {
        frame.table_setup_column("Name", TableColumnFlag::NONE, 80.0F);
        frame.table_setup_column("Size", TableColumnFlag::NONE, 60.0F);
        frame.table_setup_column("Kind", TableColumnFlag::NONE, 60.0F);
    });
    ui.frames(3, body);
    ASSERT_FLOAT_EQ(probe.at(0, 0).width, 80.0F);

    // The border right of the first column (its right edge, before the second column's padding).
    const float border = probe.at(0, 1).start.x - STYLE.cell_padding.x;
    const float y = probe.at(1, 0).start.y + 2;
    drag(ui, {.x = border, .y = y}, {.x = border + 30, .y = y}, 3, body);
    ui.frames(2, body);
    EXPECT_FLOAT_EQ(probe.at(0, 0).width, 110.0F);
    EXPECT_FLOAT_EQ(probe.at(0, 1).width, 60.0F) << "a fixed column changes alone";

    // A double click on it fits the column to its content ("r3c0" is the widest).
    const float moved = probe.at(0, 1).start.x - STYLE.cell_padding.x;
    float content = 0;
    ui.frame([&](UiFrame& frame) { content = frame.calculate_text_size("r0c0").x; });
    ui.move(moved, y);
    ui.frame(body);
    for (int click = 0; click < 2; ++click)
    {
        ui.press();
        ui.frame(body);
        ui.release();
        ui.frame(body);
    }
    ui.frames(2, body);
    EXPECT_NEAR(probe.at(0, 0).width, content, 0.5F);
}

TEST(UiTableTest, ResizingAStretchedColumnMovesWidthFromTheNextOne)
{
    UiHarness ui;
    TableProbe probe{};
    const auto body = table_body(probe, 3, 2, TableFlag::RESIZABLE, NO_SETUP);
    ui.frames(3, body);
    const std::array before{probe.at(0, 0).width, probe.at(0, 1).width, probe.at(0, 2).width};
    const float border = probe.at(0, 1).start.x - STYLE.cell_padding.x;
    const float y = probe.at(0, 0).start.y + 2;
    drag(ui, {.x = border, .y = y}, {.x = border + 40, .y = y}, 4, body);
    ui.frames(2, body);
    EXPECT_NEAR(probe.at(0, 0).width, before[0] + 40, 1.0F);
    EXPECT_NEAR(probe.at(0, 1).width, before[1] - 40, 1.0F);
    EXPECT_NEAR(probe.at(0, 2).width, before[2], 1.0F);

    // Kept at the minimum width.
    const float second = probe.at(0, 2).start.x - STYLE.cell_padding.x;
    drag(ui, {.x = second, .y = y}, {.x = second - 400, .y = y}, 4, body);
    ui.frames(2, body);
    EXPECT_NEAR(probe.at(0, 1).width, MINIMUM_TABLE_COLUMN_WIDTH, 1.0F);
}

// ===================================================================================================== reordering, hiding

TEST(UiTableTest, DraggingAHeaderPastAnotherReordersTheColumnsUntilTheOrderIsReset)
{
    UiHarness ui;
    TableProbe probe{};
    const auto body = table_body(probe, 3, 1, TableFlag::REORDERABLE | TableFlag::RESIZABLE, [](UiFrame& frame)
    {
        frame.table_setup_column("A");
        frame.table_setup_column("B");
        frame.table_setup_column("C");
    }, true);
    ui.frames(3, body);
    const float a = probe.at(0, 0).start.x;
    const float b = probe.at(0, 1).start.x;
    const Vector2 from = header_center(probe, 0);
    const Vector2 past_b{.x = probe.at(0, 1).start.x + probe.at(0, 1).width * 0.5F, .y = from.y};
    drag(ui, from, past_b, 8, body);
    ui.frames(2, body);
    EXPECT_FLOAT_EQ(probe.at(0, 1).start.x, a) << "B moved to the first place";
    EXPECT_FLOAT_EQ(probe.at(0, 0).start.x, a + (b - a)) << "A after it (equal widths)";

    // The context menu's "Reset order" (after "Size column to fit" and "Size all columns to default").
    right_click(ui, header_center(probe, 2), body);
    choose_with_keyboard(ui, 3, body);
    EXPECT_FLOAT_EQ(probe.at(0, 0).start.x, a);
    EXPECT_FLOAT_EQ(probe.at(0, 1).start.x, b);
}

TEST(UiTableTest, TheContextMenuHidesAColumnAndTheStateIsKeptWhileTheTableIsNotSubmitted)
{
    UiHarness ui;
    TableProbe probe{};
    std::array<TableColumnStatus, 3> status{};
    bool submit = true;
    bool show_again = false;
    const auto table = table_body(probe, 3, 1, TableFlag::HIDEABLE, [&](UiFrame& frame)
    {
        frame.table_setup_column("A");
        frame.table_setup_column("B");
        frame.table_setup_column("C");
        if (show_again)
        {
            frame.table_set_column_enabled(1, true);
        }
    }, true);
    const auto body = [&](UiFrame& frame)
    {
        if (submit)
        {
            table(frame);
        }
        else
        {
            in_window([](UiFrame& inner) { inner.text_unformatted("No table"); })(frame);
        }
    };
    ui.frames(3, body);
    const float c = probe.at(0, 2).start.x;

    // Its menu lists the columns: the second item is "B".
    right_click(ui, header_center(probe, 1), body);
    choose_with_keyboard(ui, 2, body);
    ui.frames(2, body);
    EXPECT_EQ(probe.at(0, 1).width, 0.0F) << "B's cells are not submitted (table_set_column_index() returned false)";
    EXPECT_LT(probe.at(0, 2).start.x, c) << "C moved left";

    // Kept while the table is not submitted.
    submit = false;
    ui.frames(5, body);
    submit = true;
    ui.frames(2, body);
    EXPECT_EQ(probe.at(0, 1).width, 0.0F);
    ui.frame(in_window([&](UiFrame& frame)
    {
        if (frame.begin_table("probe", 3, TableFlag::HIDEABLE))
        {
            frame.table_next_row();
            for (std::int32_t column = 0; column < 3; ++column)
            {
                status[static_cast<std::size_t>(column)] = frame.table_column_status(column);
            }
            frame.end_table();
        }
    }));
    EXPECT_TRUE(has_any(status[0], TableColumnStatus::ENABLED | TableColumnStatus::VISIBLE));
    EXPECT_FALSE(has_any(status[1], TableColumnStatus::ENABLED));

    // Shown again by code.
    show_again = true;
    ui.frames(3, body);
    EXPECT_GT(probe.at(0, 1).width, 0.0F);
}

// ===================================================================================================== sorting

TEST(UiTableTest, HeaderClicksSortByOneColumnAndTheSpecificationsReportChanges)
{
    UiHarness ui;
    TableProbe probe{};
    std::vector<TableColumnSortSpecification> sort{};
    bool changed = false;
    bool changed_again = true;
    const auto body = in_window([&](UiFrame& frame)
    {
        probe.top = frame.cursor_screen_position();
        probe.columns = 4;
        probe.cells.assign(4, Cell{});
        if (frame.begin_table("sorted", 4, TableFlag::SORTABLE))
        {
            frame.table_setup_column("Name", TableColumnFlag::DEFAULT_SORT);
            frame.table_setup_column("Size");
            frame.table_setup_column("Kind", TableColumnFlag::NO_SORT);
            frame.table_setup_column("Date", TableColumnFlag::PREFER_SORT_DESCENDING);
            frame.table_headers_row();
            const TableSortSpecifications specifications = frame.table_sort_specifications();
            sort.assign(specifications.columns.begin(), specifications.columns.end());
            changed = specifications.changed;
            changed_again = frame.table_sort_specifications().changed;
            frame.table_next_row();
            for (std::uint32_t column = 0; column < 4; ++column)
            {
                frame.table_set_column_index(column);
                probe.cells[column] = {.start = frame.cursor_screen_position(), .width = frame.content_region_available().x};
            }
            frame.end_table();
        }
    });
    ui.frame(body);
    ASSERT_EQ(sort.size(), 1U);
    EXPECT_EQ(sort[0].column, 0U);
    EXPECT_EQ(sort[0].direction, SortDirection::ASCENDING);
    EXPECT_TRUE(changed) << "the first sort is a change";
    EXPECT_FALSE(changed_again) << "read once, it is no longer changed";
    ui.frames(2, body);
    EXPECT_FALSE(changed);

    // A click on "Size": sorted by it alone, ascending; again: descending; again: ascending (no tristate).
    const auto click = [&](std::uint32_t column, SortDirection expected)
    {
        const Vector2 at = header_center(probe, column);
        ui.click(at.x, at.y, body);
        ui.wait(1.0);
        ASSERT_EQ(sort.size(), 1U) << column;
        EXPECT_EQ(sort[0].column, column);
        EXPECT_EQ(sort[0].direction, expected);
    };
    click(1, SortDirection::ASCENDING);
    click(1, SortDirection::DESCENDING);
    click(1, SortDirection::ASCENDING);
    const Vector2 kind = header_center(probe, 2);
    ui.click(kind.x, kind.y, body);
    ui.wait(1.0);
    ASSERT_EQ(sort.size(), 1U);
    EXPECT_EQ(sort[0].column, 1U) << "Kind cannot be sorted by";
    click(3, SortDirection::DESCENDING);
}

TEST(UiTableTest, ShiftClicksSortBySeveralColumnsAndTristateHeadersLeaveTheSort)
{
    UiHarness ui;
    TableProbe probe{};
    std::vector<TableColumnSortSpecification> sort{};
    const auto body = in_window([&](UiFrame& frame)
    {
        probe.top = frame.cursor_screen_position();
        probe.columns = 3;
        probe.cells.assign(3, Cell{});
        if (frame.begin_table("multiple", 3, TableFlag::SORTABLE | TableFlag::SORT_MULTIPLE | TableFlag::SORT_TRISTATE))
        {
            frame.table_setup_column("Name");
            frame.table_setup_column("Size");
            frame.table_setup_column("Kind");
            frame.table_headers_row();
            const TableSortSpecifications specifications = frame.table_sort_specifications();
            sort.assign(specifications.columns.begin(), specifications.columns.end());
            frame.table_next_row();
            for (std::uint32_t column = 0; column < 3; ++column)
            {
                frame.table_set_column_index(column);
                probe.cells[column] = {.start = frame.cursor_screen_position(), .width = frame.content_region_available().x};
            }
            frame.end_table();
        }
    });
    ui.frames(2, body);
    EXPECT_TRUE(sort.empty()) << "tristate tables may be unsorted";
    const auto click = [&](std::uint32_t column, bool shift)
    {
        const Vector2 at = header_center(probe, column);
        ui.modifiers(shift ? KeyModifier::SHIFT : KeyModifier::NONE);
        ui.click(at.x, at.y, body);
        ui.modifiers(KeyModifier::NONE);
        ui.wait(1.0);
        ui.frame(body);
    };
    click(0, false);
    click(1, true);
    ASSERT_EQ(sort.size(), 2U);
    EXPECT_EQ(sort[0].column, 0U);
    EXPECT_EQ(sort[1].column, 1U);
    EXPECT_EQ(sort[1].order, 1U);
    EXPECT_EQ(sort[1].direction, SortDirection::ASCENDING);
    click(1, true);
    ASSERT_EQ(sort.size(), 2U);
    EXPECT_EQ(sort[1].direction, SortDirection::DESCENDING);
    click(1, true);
    ASSERT_EQ(sort.size(), 1U) << "a third click removes it";
    EXPECT_EQ(sort[0].column, 0U);
    click(2, false);
    ASSERT_EQ(sort.size(), 1U) << "a click without Shift sorts by that column alone";
    EXPECT_EQ(sort[0].column, 2U);
}

// ===================================================================================================== scrolling

TEST(UiTableTest, FrozenRowsAndColumnsStayInViewWhileTheTableScrolls)
{
    UiHarness ui;
    TableProbe probe{};
    Vector2 scroll{};
    Vector2 maximum{};
    const auto body = table_body(probe, 6, 20, TableFlag::SCROLL_X | TableFlag::SCROLL_Y | TableFlag::SIZING_FIXED_FIT, [&](UiFrame& frame)
    {
        for (int column = 0; column < 6; ++column)
        {
            frame.table_setup_column("Column", TableColumnFlag::NONE, 100.0F);
        }
        frame.table_setup_scroll_freeze(1, 1);
        if (scroll.x > 0 || scroll.y > 0)
        {
            frame.set_scroll_x(scroll.x);
            frame.set_scroll_y(scroll.y);
        }
        maximum = {.x = frame.scroll_maximum_x(), .y = frame.scroll_maximum_y()};
    }, false, {.x = 300, .y = 150});
    ui.frames(3, body);
    EXPECT_GT(maximum.x, 300.0F);
    EXPECT_GT(maximum.y, 200.0F);
    const Cell frozen_corner = probe.at(0, 0);
    const Cell frozen_row = probe.at(0, 2);
    const Cell frozen_column = probe.at(5, 0);
    const Cell body_cell = probe.at(5, 2);
    ASSERT_GT(frozen_row.width, 0.0F);

    // Scrolled: the first row and the first column do not move; the other cells move by the scroll.
    scroll = {.x = 80, .y = 50};
    ui.frames(3, body);
    EXPECT_EQ(probe.at(0, 0).start.x, frozen_corner.start.x);
    EXPECT_EQ(probe.at(0, 0).start.y, frozen_corner.start.y);
    EXPECT_FLOAT_EQ(probe.at(0, 2).start.y, frozen_row.start.y) << "the frozen row stays";
    EXPECT_FLOAT_EQ(probe.at(0, 2).start.x, frozen_row.start.x - 80) << "but scrolls sideways";
    EXPECT_FLOAT_EQ(probe.at(5, 0).start.x, frozen_column.start.x) << "the frozen column stays";
    EXPECT_FLOAT_EQ(probe.at(5, 0).start.y, frozen_column.start.y - 50) << "but scrolls up";
    EXPECT_FLOAT_EQ(probe.at(5, 2).start.x, body_cell.start.x - 80);
    EXPECT_FLOAT_EQ(probe.at(5, 2).start.y, body_cell.start.y - 50);
    EXPECT_FLOAT_EQ(probe.rectangle.height(), 150.0F);
}

TEST(UiTableTest, TheListClipperSubmitsOnlyTheVisibleRowsOfALargeTable)
{
    UiHarness ui;
    constexpr std::uint32_t ROWS = 10000;
    float scroll = 0;
    float maximum = 0;
    float row_height = 0;
    std::uint32_t submitted = 0;
    std::uint32_t first = 0;
    bool indices_match = true;
    const auto body = in_window([&](UiFrame& frame)
    {
        submitted = 0;
        first = ROWS;
        bool measured = false;
        if (frame.begin_table("large", 3, TableFlag::SCROLL_Y | TableFlag::ROW_BACKGROUND, {.x = 0, .y = 200}))
        {
            frame.table_setup_column("Index");
            frame.table_setup_column("Name");
            frame.table_setup_column("Value");
            frame.table_setup_scroll_freeze(0, 1);
            frame.table_headers_row();
            if (scroll > 0)
            {
                frame.set_scroll_y(scroll);
            }
            UiListClipper clipper;
            clipper.begin(frame, ROWS);
            while (clipper.step())
            {
                for (std::uint32_t row = clipper.display_start(); row < clipper.display_end(); ++row)
                {
                    frame.table_next_row();
                    frame.table_next_column();
                    indices_match = indices_match && frame.table_row_index() == static_cast<std::int32_t>(row) + 1;
                    frame.text("{}", row);
                    frame.table_next_column();
                    frame.text_unformatted("name");
                    frame.table_next_column();
                    frame.text("{:.2f}", static_cast<double>(row) * 0.5);
                    ++submitted;
                    first = measured ? std::min(first, row) : first;
                }
                measured = true;
            }
            row_height = clipper.item_height();
            maximum = frame.scroll_maximum_y();
            frame.end_table();
        }
    });
    ui.frames(3, body);
    EXPECT_TRUE(indices_match);
    EXPECT_GT(row_height, 0.0F);
    EXPECT_LE(submitted, static_cast<std::uint32_t>(200.0F / row_height) + 3) << "only what fits in 200 units";
    EXPECT_GE(submitted, static_cast<std::uint32_t>(200.0F / row_height) - 2);
    EXPECT_EQ(first, 1U) << "row 0 measured, then the visible rows after it";
    EXPECT_NEAR(maximum, row_height * ROWS + row_height - 200.0F, row_height) << "the content is every row's height";

    // Scrolled to the middle: the rows there.
    scroll = row_height * 5000;
    ui.frames(3, body);
    EXPECT_TRUE(indices_match);
    EXPECT_NEAR(static_cast<double>(first), 5000.0, 2.0);
    EXPECT_LE(submitted, static_cast<std::uint32_t>(200.0F / row_height) + 3);
}

TEST(UiTableTest, TheListClipperMeasuresItemsAndKeepsIncludedOnesInAWindow)
{
    UiHarness ui;
    constexpr std::uint32_t LINES = 1000;
    float scroll = 0;
    float height = 0;
    float maximum = 0;
    std::vector<std::uint32_t> lines{};
    const auto body = in_window([&](UiFrame& frame)
    {
        lines.clear();
        if (frame.begin_child("list", {.x = 0, .y = 200}))
        {
            if (scroll > 0)
            {
                frame.set_scroll_y(scroll);
            }
            UiListClipper clipper;
            clipper.begin(frame, LINES);
            clipper.include_item(10);
            while (clipper.step())
            {
                for (std::uint32_t line = clipper.display_start(); line < clipper.display_end(); ++line)
                {
                    frame.text("Line {}", line);
                    lines.push_back(line);
                }
            }
            height = clipper.item_height();
            maximum = frame.scroll_maximum_y();
        }
        frame.end_child();
    });
    ui.frames(3, body);
    float spaced = 0;
    ui.frame([&](UiFrame& frame) { spaced = frame.text_line_height_with_spacing(); });
    EXPECT_FLOAT_EQ(height, spaced) << "measured from the first line";
    EXPECT_NEAR(maximum, spaced * LINES - STYLE.item_spacing.y - 200.0F, 1.0F);
    EXPECT_LE(lines.size(), static_cast<std::size_t>(200.0F / spaced) + 3);
    scroll = spaced * 500;
    ui.frames(3, body);
    ASSERT_GE(lines.size(), 3U);
    EXPECT_EQ(lines[0], 0U) << "measured every frame";
    EXPECT_EQ(lines[1], 10U) << "the included line";
    EXPECT_NEAR(static_cast<double>(lines[2]), 500.0, 1.0);
}

// ===================================================================================================== cells

TEST(UiTableTest, SpanningSelectablesCoverTheRowAndIndentationCarriesIntoIndentedColumns)
{
    UiHarness ui;
    TableProbe probe{};
    bool hovered = false;
    UiRectangle selectable{};
    std::array<float, 3> first_column{};
    std::array<float, 3> second_column{};
    const auto body = in_window([&](UiFrame& frame)
    {
        if (frame.begin_table("rows", 3))
        {
            frame.table_next_row();
            frame.table_next_column();
            static_cast<void>(frame.selectable("Row", false, SelectableFlag::SPAN_ALL_COLUMNS));
            hovered = frame.is_item_hovered();
            selectable = frame.item_rectangle();
            frame.table_next_column();
            frame.text_unformatted("middle");
            frame.table_next_column();
            frame.text_unformatted("right");

            // A tree node indents the next rows' first column only (INDENT_ENABLE by default there).
            frame.table_next_row();
            frame.table_next_column();
            first_column[0] = frame.cursor_screen_position().x;
            const bool open = frame.tree_node("Parent", TreeNodeFlag::DEFAULT_OPEN);
            frame.table_next_column();
            second_column[0] = frame.cursor_screen_position().x;
            frame.table_next_row();
            frame.table_next_column();
            first_column[1] = frame.cursor_screen_position().x;
            frame.text_unformatted("child");
            frame.table_next_column();
            second_column[1] = frame.cursor_screen_position().x;
            if (open)
            {
                frame.tree_pop();
            }
            frame.table_next_row();
            frame.table_next_column();
            first_column[2] = frame.cursor_screen_position().x;
            frame.end_table();
            probe.rectangle = frame.item_rectangle();
        }
    });
    ui.frames(3, body);
    EXPECT_FLOAT_EQ(selectable.left, probe.rectangle.left);
    EXPECT_FLOAT_EQ(selectable.right, probe.rectangle.right);
    ui.move(probe.rectangle.right - 10, center(selectable).y);
    ui.frames(2, body);
    EXPECT_TRUE(hovered) << "hovered over the last column";
    EXPECT_FLOAT_EQ(first_column[1], first_column[0] + STYLE.indent_spacing);
    EXPECT_FLOAT_EQ(second_column[1], second_column[0]);
    EXPECT_FLOAT_EQ(first_column[2], first_column[0]) << "tree_pop() in another column unindents the next rows";
}

TEST(UiTableTest, BackgroundsBordersAndCellsMergeIntoFewDrawCommands)
{
    UiHarness ui;
    TableProbe probe{};
    const auto body = table_body(probe, 4, 30, TableFlag::BORDERS | TableFlag::ROW_BACKGROUND, NO_SETUP, true);
    ui.frames(3, body);
    std::size_t commands = 0;
    for (const DrawList* list : ui.context().draw_data().lists)
    {
        commands += list->commands().size();
    }
    EXPECT_LE(commands, 16U) << "a channel per column: one command each, not one per cell";
}

TEST(UiTableTest, CellColorsAndStatusesAreKeptPerRow)
{
    UiHarness ui;
    TableColumnStatus status{};
    std::int32_t hovered = -2;
    UiRectangle table{};
    const auto body = in_window([&](UiFrame& frame)
    {
        if (frame.begin_table("colors", 2, TableFlag::SORTABLE))
        {
            frame.table_next_row();
            frame.table_set_background_color(TableBackgroundTarget::ROW, {.red = 1, .alpha = 1});
            frame.table_next_column();
            frame.table_set_background_color(TableBackgroundTarget::CELL, {.green = 1, .alpha = 1});
            frame.text_unformatted("a");
            status = frame.table_column_status();
            hovered = frame.table_hovered_column();
            frame.table_next_column();
            frame.text_unformatted("b");
            frame.end_table();
            table = frame.item_rectangle();
        }
    });
    const UiFrameReport report = ui.frames(3, body);
    EXPECT_TRUE(report.ok());
    EXPECT_TRUE(has_all(status, TableColumnStatus::ENABLED | TableColumnStatus::VISIBLE | TableColumnStatus::SORTED)) << "sorted by its first column";
    EXPECT_EQ(hovered, -1);
    ui.move(table.left + 5, center(table).y);
    ui.frames(2, body);
    EXPECT_EQ(hovered, 0);
    EXPECT_TRUE(has_any(status, TableColumnStatus::HOVERED));
}

TEST(UiTableTest, MisuseIsReported)
{
    UiHarness ui;
    const UiFrameReport outside = ui.frame(in_window([](UiFrame& frame)
    {
        frame.table_next_row();
        frame.end_table();
        EXPECT_FALSE(frame.begin_table("none", 0));
        EXPECT_FALSE(frame.table_next_column());
    }));
    EXPECT_TRUE(has_issue(outside, UiFrameProblem::INVALID_ARGUMENT));
    EXPECT_TRUE(has_issue(outside, UiFrameProblem::END_WITHOUT_BEGIN));

    const UiFrameReport late = ui.frame(in_window([](UiFrame& frame)
    {
        if (frame.begin_table("late", 2))
        {
            frame.table_setup_column("A");
            frame.table_setup_column("B");
            frame.table_setup_column("C");
            frame.table_next_row();
            frame.table_setup_column("D");
            EXPECT_FALSE(frame.table_set_column_index(5));
            frame.end_table();
        }
    }));
    EXPECT_EQ(std::ranges::count_if(late.issues, [](const UiFrameIssue& issue) { return issue.problem == UiFrameProblem::INVALID_ARGUMENT; }), 3);

    // Left open: closed with its window, and reported.
    const UiFrameReport open = ui.frame(in_window([](UiFrame& frame)
    {
        if (frame.begin_table("open", 2, TableFlag::SCROLL_Y, {.x = 0, .y = 100}))
        {
            frame.table_next_column();
            frame.text_unformatted("left open");
        }
    }));
    EXPECT_TRUE(has_issue(open, UiFrameProblem::UNBALANCED_STACK));
    const UiFrameReport after = ui.frame(in_window([](UiFrame& frame) { frame.text_unformatted("fine"); }));
    EXPECT_TRUE(after.ok());
}

// ===================================================================================================== splitters, child regions

TEST(UiVisualTest, SplittersMoveSizeBetweenTheirAreasWithinTheirMinimums)
{
    UiHarness ui;
    float left = 100;
    float right = 200;
    UiRectangle bar{};
    bool dragged = false;
    const auto body = in_window([&](UiFrame& frame)
    {
        dragged = frame.splitter("split", true, 8.0F, left, right, 50.0F, 50.0F) || dragged;
        bar = frame.item_rectangle();
        frame.begin_child("first", {.x = left, .y = 100}, ChildFlag::BORDERS);
        frame.end_child();
        frame.same_line(0, 8.0F);
        frame.begin_child("second", {.x = right, .y = 100}, ChildFlag::BORDERS);
        frame.end_child();
    });
    ui.frames(2, body);
    EXPECT_FLOAT_EQ(bar.width(), 8.0F);
    const Vector2 at = center(bar);
    drag(ui, at, {.x = at.x + 40, .y = at.y}, 4, body);
    EXPECT_TRUE(dragged);
    EXPECT_FLOAT_EQ(left, 140.0F);
    EXPECT_FLOAT_EQ(right, 160.0F);
    ui.frame(body);
    const Vector2 moved = center(bar);
    drag(ui, moved, {.x = moved.x + 400, .y = moved.y}, 4, body);
    EXPECT_FLOAT_EQ(right, 50.0F) << "the second area keeps its minimum";
    EXPECT_FLOAT_EQ(left, 250.0F);
}

TEST(UiVisualTest, ResizableChildRegionsKeepTheSizeTheirEdgesAreDraggedTo)
{
    UiHarness ui;
    Vector2 position{};
    Vector2 size{};
    const auto body = in_window([&](UiFrame& frame)
    {
        if (frame.begin_child("panel", {.x = 150, .y = 100}, ChildFlag::BORDERS | ChildFlag::RESIZE_X | ChildFlag::RESIZE_Y))
        {
            position = frame.window_position();
            size = frame.window_size();
            frame.text_unformatted("Panel");
        }
        frame.end_child();
    });
    ui.frames(2, body);
    ASSERT_FLOAT_EQ(size.x, 150.0F);
    drag(ui, {.x = position.x + 150, .y = position.y + 50}, {.x = position.x + 200, .y = position.y + 50}, 3, body);
    ui.frames(3, body);
    EXPECT_FLOAT_EQ(size.x, 200.0F);
    drag(ui, {.x = position.x + 80, .y = position.y + 100}, {.x = position.x + 80, .y = position.y + 130}, 3, body);
    ui.frame(body);
    EXPECT_FLOAT_EQ(size.y, 130.0F);
    EXPECT_FLOAT_EQ(size.x, 200.0F);
}

// ===================================================================================================== plots, progress bars, images

/// The commands of every list of @p ui's draw data.
auto
all_commands(UiHarness& ui) -> std::vector<DrawCommand>
{
    std::vector<DrawCommand> commands{};
    for (const DrawList* list : ui.context().draw_data().lists)
    {
        commands.insert(commands.end(), list->commands().begin(), list->commands().end());
    }
    return commands;
}

TEST(UiVisualTest, PlotsShowTheValuesUnderTheMouseInATooltip)
{
    UiHarness ui;
    UiRectangle lines{};
    UiRectangle bars{};
    std::array<float, 64> values{};
    for (std::size_t value = 0; value < values.size(); ++value)
    {
        values[value] = std::sin(static_cast<float>(value) * 0.2F);
    }
    const auto body = in_window([&](UiFrame& frame)
    {
        frame.plot_lines("Wave", values, {.overlay = "sine", .size = {.x = 200, .y = 60}});
        lines = frame.item_rectangle();
        frame.plot_histogram("Bars", std::span{values}.first(16), {.minimum = -1.0F, .maximum = 1.0F, .size = {.x = 200, .y = 60}});
        bars = frame.item_rectangle();
        frame.plot_lines("One", std::span{values}.first(1));
    });
    const UiFrameReport report = ui.frames(2, body);
    EXPECT_TRUE(report.ok());
    EXPECT_FLOAT_EQ(lines.height(), 60.0F);
    EXPECT_GT(lines.width(), 200.0F) << "the label after the graph";
    const std::size_t lists = ui.context().draw_data().lists.size();
    ui.move(lines.left + 100, center(lines).y);
    ui.frames(2, body);
    EXPECT_EQ(ui.context().draw_data().lists.size(), lists + 1) << "a tooltip window";
    ui.move(bars.left + 50, center(bars).y);
    ui.frames(2, body);
    EXPECT_EQ(ui.context().draw_data().lists.size(), lists + 1);
}

TEST(UiVisualTest, ProgressBarsFillTheirFractionAndIndeterminateOnesSweep)
{
    UiHarness ui;
    UiRectangle bar{};
    float fraction = 0.25F;
    float line = 0;
    const auto body = in_window([&](UiFrame& frame)
    {
        line = frame.text_line_height();
        frame.progress_bar(fraction, {.x = 200, .y = 0});
        bar = frame.item_rectangle();
    });
    ui.frames(2, body);
    EXPECT_FLOAT_EQ(bar.width(), 200.0F);
    EXPECT_FLOAT_EQ(bar.height(), line + STYLE.frame_padding.y * 2);
    const auto filled = [&](float width)
    {
        return std::ranges::any_of(all_commands(ui), [&](const DrawCommand& command)
        {
            return std::abs(command.clip.left - bar.left) < 0.01F && std::abs(command.clip.width() - width) < 0.01F;
        });
    };
    EXPECT_TRUE(filled(50.0F)) << "a quarter of the bar";
    fraction = -1.0F;
    ui.frames(2, body);
    EXPECT_FALSE(filled(50.0F));
}

TEST(UiVisualTest, ImagesAndImageButtonsDrawTheirBindlessImage)
{
    UiHarness ui;
    UiRectangle image{};
    UiRectangle rounded{};
    UiRectangle button{};
    bool clicked = false;
    const auto body = in_window([&](UiFrame& frame)
    {
        frame.image(7, {.x = 32, .y = 24});
        image = frame.item_rectangle();
        frame.image_rounded(7, {.x = 32, .y = 32}, 8.0F);
        rounded = frame.item_rectangle();
        clicked = frame.image_button("open", 9, {.x = 16, .y = 16}) || clicked;
        button = frame.item_rectangle();
    });
    ui.frames(2, body);
    EXPECT_FLOAT_EQ(image.width(), 32.0F);
    EXPECT_FLOAT_EQ(image.height(), 24.0F);
    EXPECT_FLOAT_EQ(rounded.height(), 32.0F);
    EXPECT_FLOAT_EQ(button.width(), 16.0F + STYLE.frame_padding.x * 2);
    EXPECT_FLOAT_EQ(button.height(), 16.0F + STYLE.frame_padding.y * 2);
    const auto images = all_commands(ui);
    EXPECT_TRUE(std::ranges::any_of(images, [](const DrawCommand& command) { return command.texture == DrawTexture::from_image(7); }));
    EXPECT_TRUE(std::ranges::any_of(images, [](const DrawCommand& command) { return command.texture == DrawTexture::from_image(9); }));
    ui.click(center(button).x, center(button).y, body);
    EXPECT_TRUE(clicked);
}

} // namespace
