// The structure widgets of UiFrame driven by synthetic input: tree nodes and collapsing headers (open states,
// flags, the keyboard), tab bars (selection, closing, fitting, reordering, leading and trailing tabs, the tab
// list), menus (window and main menu bars, submenus, items, keyboard navigation), popups (opening, stacking,
// closing, context popups, modals), tooltips (at the mouse, item tooltips after a delay) and drag and drop
// (sources, payload types, targets, delivery, holding to open, colors).
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

/// Runs @p body inside a window "Structure" at (20, 20), 420 by 400.
template<class Body>
auto
in_window(Body&& body,
          UiWindowFlag flags = UiWindowFlag::NONE)
{
    return [body = std::forward<Body>(body), flags](UiFrame& frame) mutable
    {
        frame.set_next_window_position({.x = 20, .y = 20}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size({.x = 420, .y = 400}, UiCondition::FIRST_USE_EVER);
        if (frame.begin_window("Structure", flags))
        {
            body(frame);
        }
        frame.end_window();
    };
}

/// The draw lists published by the last frame.
auto
lists(UiHarness& ui) -> std::size_t
{
    return ui.context().draw_data().lists.size();
}

/// A click at @p point, a second after any earlier one (never a double click).
template<class Body>
void
click(UiHarness& ui,
      Vector2 point,
      Body& body,
      MouseButton button = MouseButton::LEFT)
{
    ui.wait(1.0);
    ui.move(point.x, point.y);
    ui.frame(body);
    ui.press(button);
    ui.frame(body);
    ui.release(button);
    ui.frame(body);
}

// ===================================================================================================== trees

TEST(UiTreeTest, AClickTogglesANodeAndItsChildrenAreIndentedUnderIt)
{
    UiHarness ui;
    bool open = false;
    UiRectangle node{};
    float child_x = 0;
    auto body = in_window([&](UiFrame& frame)
    {
        open = frame.tree_node("Node");
        node = frame.item_rectangle();
        if (open)
        {
            child_x = frame.cursor_screen_position().x;
            frame.text_unformatted("child");
            frame.tree_pop();
        }
    });
    ui.frames(2, body);
    EXPECT_FALSE(open);
    click(ui, center(node), body);
    EXPECT_TRUE(open);
    EXPECT_FLOAT_EQ(child_x, node.left + ui.context().theme().style.indent_spacing);
    ui.frame(body);
    EXPECT_TRUE(open) << "remembered";
    click(ui, center(node), body);
    EXPECT_FALSE(open);
    EXPECT_TRUE(ui.frame(body).ok());
}

TEST(UiTreeTest, ArrowsDoubleClicksLeavesAndDefaultsFollowTheFlags)
{
    UiHarness ui;
    std::array<bool, 4> open{};
    std::array<UiRectangle, 4> nodes{};
    auto body = in_window([&](UiFrame& frame)
    {
        open[0] = frame.tree_node("Arrow", TreeNodeFlag::OPEN_ON_ARROW);
        nodes[0] = frame.item_rectangle();
        if (open[0])
        {
            frame.tree_pop();
        }
        open[1] = frame.tree_node("Double", TreeNodeFlag::OPEN_ON_DOUBLE_CLICK);
        nodes[1] = frame.item_rectangle();
        if (open[1])
        {
            frame.tree_pop();
        }
        open[2] = frame.tree_node("Leaf", TreeNodeFlag::LEAF | TreeNodeFlag::NO_TREE_PUSH_ON_OPEN);
        nodes[2] = frame.item_rectangle();
        open[3] = frame.tree_node("Default", TreeNodeFlag::DEFAULT_OPEN);
        nodes[3] = frame.item_rectangle();
        if (open[3])
        {
            frame.tree_pop();
        }
    });
    ui.frames(2, body);
    EXPECT_EQ(open, (std::array{false, false, true, true}));

    // OPEN_ON_ARROW: the label does nothing, the arrow toggles.
    click(ui, {.x = nodes[0].right - 4, .y = center(nodes[0]).y}, body);
    EXPECT_FALSE(open[0]);
    click(ui, {.x = nodes[0].left + 6, .y = center(nodes[0]).y}, body);
    EXPECT_TRUE(open[0]);

    // OPEN_ON_DOUBLE_CLICK: one click does nothing, a double click toggles.
    click(ui, center(nodes[1]), body);
    EXPECT_FALSE(open[1]);
    ui.press();
    ui.frame(body);
    ui.release();
    ui.frame(body);
    EXPECT_TRUE(open[1]);

    // A leaf stays open; a default-open node closes on a click.
    click(ui, center(nodes[2]), body);
    EXPECT_TRUE(open[2]);
    click(ui, center(nodes[3]), body);
    EXPECT_FALSE(open[3]);
}

TEST(UiTreeTest, SetNextItemOpenAppliesByCondition)
{
    UiHarness ui;
    bool always = false;
    bool once = false;
    std::optional<bool> force_always;
    bool force_once = true;
    UiRectangle once_node{};
    auto body = in_window([&](UiFrame& frame)
    {
        if (force_always)
        {
            frame.set_next_item_open(*force_always);
        }
        always = frame.tree_node("Always");
        if (always)
        {
            frame.tree_pop();
        }
        frame.set_next_item_open(force_once, UiCondition::ONCE);
        once = frame.tree_node("Once");
        once_node = frame.item_rectangle();
        if (once)
        {
            frame.tree_pop();
        }
    });
    ui.frame(body);
    EXPECT_FALSE(always);
    EXPECT_TRUE(once);
    force_always = true;
    ui.frame(body);
    EXPECT_TRUE(always);
    force_always = false;
    ui.frame(body);
    EXPECT_FALSE(always);

    // ONCE: only before the node has a state; a click still toggles it.
    ui.frame(body);
    click(ui, center(once_node), body);
    EXPECT_FALSE(once);
    ui.frame(body);
    EXPECT_FALSE(once);
}

TEST(UiTreeTest, TheKeyboardOpensClosesAndTogglesFocusedNodes)
{
    UiHarness ui;
    bool open = false;
    auto body = in_window([&](UiFrame& frame)
    {
        open = frame.tree_node("Keyboard");
        if (open)
        {
            frame.tree_pop();
        }
    });
    ui.frames(2, body);
    click(ui, {.x = 300, .y = 300}, body);
    ui.tap(Key::TAB);
    ui.frame(body);
    ui.tap(Key::RIGHT);
    ui.frame(body);
    ui.frame(body);
    EXPECT_TRUE(open);
    ui.tap(Key::LEFT);
    ui.frame(body);
    ui.frame(body);
    EXPECT_FALSE(open);
    ui.tap(Key::SPACE);
    ui.frame(body);
    ui.frame(body);
    EXPECT_TRUE(open);
}

TEST(UiTreeTest, UnbalancedTreePopsAreReported)
{
    UiHarness ui;
    const UiFrameReport extra = ui.frame(in_window([](UiFrame& frame) { frame.tree_pop(); }));
    EXPECT_TRUE(has_issue(extra, UiFrameProblem::END_WITHOUT_BEGIN));
    const UiFrameReport missing = ui.frame(in_window([](UiFrame& frame) { frame.tree_push("left open"); }));
    EXPECT_TRUE(has_issue(missing, UiFrameProblem::UNBALANCED_STACK));
}

TEST(UiTreeTest, CollapsingHeadersSpanTheWidthAndTheirCloseButtonHidesThem)
{
    UiHarness ui;
    bool open = false;
    bool visible = true;
    bool closable_open = false;
    UiRectangle header{};
    UiRectangle closable{};
    float content_width = 0;
    auto body = in_window([&](UiFrame& frame)
    {
        content_width = frame.content_region_available().x;
        open = frame.collapsing_header("Header");
        header = frame.item_rectangle();
        closable_open = frame.collapsing_header("Closable", visible);
        closable = frame.item_rectangle();
    });
    ui.frames(2, body);
    EXPECT_GT(header.width(), content_width);
    click(ui, center(header), body);
    EXPECT_TRUE(open);

    // The close button at the right end of the closable header.
    const float line = ui.context().theme().style.frame_padding.x;
    click(ui, {.x = closable.right + line + 8, .y = center(closable).y}, body);
    EXPECT_FALSE(visible);
    EXPECT_FALSE(closable_open);
}

// ===================================================================================================== tabs

/// A tab bar of @p names (each closable when @p open has a flag for it); what each tab showed and where it was.
struct Tabs
{
    std::vector<std::string> names{};
    std::vector<bool> shown{};
    std::vector<UiRectangle> rectangles{};
    std::vector<bool> open{};
    TabBarFlag flags = TabBarFlag::NONE;
    std::vector<TabItemFlag> item_flags{};
    UiRectangle bar{};
    bool button_pressed = false;
    bool with_button = false;

    void operator()(UiFrame& frame)
    {
        shown.assign(names.size(), false);
        rectangles.resize(names.size());
        item_flags.resize(names.size(), TabItemFlag::NONE);
        const Vector2 start = frame.cursor_screen_position();
        if (frame.begin_tab_bar("Tabs", flags))
        {
            for (std::size_t tab = 0; tab < names.size(); ++tab)
            {
                if (tab < open.size())
                {
                    bool value = open[tab];
                    shown[tab] = frame.begin_tab_item(names[tab], value, item_flags[tab]);
                    open[tab] = value;
                }
                else
                {
                    shown[tab] = frame.begin_tab_item(names[tab], item_flags[tab]);
                }
                rectangles[tab] = frame.item_rectangle();
                if (shown[tab])
                {
                    frame.text("content {}", tab);
                    frame.end_tab_item();
                }
            }
            if (with_button)
            {
                button_pressed = frame.tab_item_button("+", TabItemFlag::TRAILING) || button_pressed;
            }
            frame.end_tab_bar();
        }
        bar = {.left = start.x, .top = start.y, .right = start.x + frame.content_region_available().x, .bottom = start.y + frame.frame_height()};
    }
};

TEST(UiTabTest, TheFirstTabIsSelectedAndAPressSelectsAnotherFromTheNextFrame)
{
    UiHarness ui;
    Tabs tabs{.names = {"One", "Two", "Three"}};
    auto body = in_window(std::ref(tabs));
    ui.frame(body);
    EXPECT_EQ(tabs.shown, (std::vector{true, false, false})) << "at once, on the bar's first frame";
    ui.frame(body);
    EXPECT_LT(tabs.rectangles[0].right, tabs.rectangles[1].left);
    EXPECT_LT(tabs.rectangles[1].right, tabs.rectangles[2].left);

    // Pressed: selected from the next frame.
    ui.wait(1.0);
    ui.move(center(tabs.rectangles[2]).x, center(tabs.rectangles[2]).y);
    ui.frame(body);
    ui.press();
    ui.frame(body);
    EXPECT_EQ(tabs.shown, (std::vector{true, false, false}));
    ui.release();
    ui.frame(body);
    EXPECT_EQ(tabs.shown, (std::vector{false, false, true}));
}

TEST(UiTabTest, ClosableTabsCloseByTheirButtonOrTheMiddleButtonAndTheSelectionMovesOn)
{
    UiHarness ui;
    Tabs tabs{.names = {"One", "Two", "Three"}, .open = {true, true, true}};
    auto body = in_window(std::ref(tabs));
    ui.frames(2, body);
    click(ui, center(tabs.rectangles[1]), body);
    ASSERT_EQ(tabs.shown, (std::vector{false, true, false}));

    // The selected tab's close button (at its right end): closed, the next tab selected.
    const Style& style = ui.context().theme().style;
    const UiRectangle second = tabs.rectangles[1];
    const float line = tabs.bar.height() - style.frame_padding.y * 2;
    click(ui, {.x = second.right - style.frame_padding.x - line * 0.5F, .y = center(second).y}, body);
    EXPECT_FALSE(tabs.open[1]);
    ui.frames(2, body);
    EXPECT_EQ(tabs.shown, (std::vector{false, false, true}));

    // The middle button closes a tab.
    click(ui, center(tabs.rectangles[0]), body, MouseButton::MIDDLE);
    EXPECT_FALSE(tabs.open[0]);
    EXPECT_TRUE(tabs.open[2]);
}

TEST(UiTabTest, SetSelectedAndAutoSelectNewTabsSelectFromTheNextFrame)
{
    UiHarness ui;
    Tabs tabs{.names = {"One", "Two"}, .flags = TabBarFlag::AUTO_SELECT_NEW_TABS};
    auto body = in_window(std::ref(tabs));
    ui.frames(2, body);
    tabs.names.push_back("New");
    ui.frame(body);
    EXPECT_EQ(tabs.shown, (std::vector{true, false, false}));
    ui.frame(body);
    EXPECT_EQ(tabs.shown, (std::vector{false, false, true}));

    tabs.item_flags = {TabItemFlag::NONE, TabItemFlag::SET_SELECTED, TabItemFlag::NONE};
    ui.frame(body);
    tabs.item_flags = {};
    ui.frame(body);
    EXPECT_EQ(tabs.shown, (std::vector{false, true, false}));
}

TEST(UiTabTest, TabsThatDoNotFitShrinkOrScroll)
{
    UiHarness ui;
    Tabs shrink{};
    for (int tab = 0; tab < 6; ++tab)
    {
        shrink.names.push_back(std::format("A long tab {}", tab));
    }
    auto body = in_window(std::ref(shrink));
    ui.frames(3, body);
    EXPECT_LE(shrink.rectangles.back().right, shrink.bar.right + 0.5F);
    EXPECT_GE(shrink.rectangles.back().width(), MINIMUM_TAB_WIDTH - 0.5F);

    // Scrolling: the tabs keep their widths; the right arrow scrolls them left.
    UiHarness scrolled_ui;
    Tabs scroll = shrink;
    scroll.flags = TabBarFlag::FITTING_POLICY_SCROLL;
    auto scroll_body = in_window(std::ref(scroll));
    scrolled_ui.frames(3, scroll_body);
    EXPECT_GT(scroll.rectangles[0].width(), shrink.rectangles[0].width());
    EXPECT_LT(scroll.rectangles.back().width(), scroll.rectangles[0].width()) << "the last one is cut";
    const float before = scroll.rectangles[0].right;
    click(scrolled_ui, {.x = scroll.bar.right - scroll.bar.height() * 0.5F, .y = center(scroll.bar).y}, scroll_body);
    scrolled_ui.frame(scroll_body);
    EXPECT_LT(scroll.rectangles[0].right, before);
}

TEST(UiTabTest, ReorderableTabsMoveWhenDraggedAndLeadingAndTrailingTabsStayAtTheEnds)
{
    UiHarness ui;
    Tabs tabs{.names = {"Start", "One", "Two", "Three"}, .flags = TabBarFlag::REORDERABLE, .with_button = true};
    tabs.item_flags = {TabItemFlag::LEADING, TabItemFlag::NONE, TabItemFlag::NONE, TabItemFlag::NONE};
    auto body = in_window(std::ref(tabs));
    ui.frames(3, body);
    EXPECT_LT(tabs.rectangles[0].left, tabs.rectangles[1].left);

    // "One" dragged past "Two": they swap; the leading tab stays first.
    const Vector2 from = center(tabs.rectangles[1]);
    const Vector2 to = center(tabs.rectangles[2]);
    ui.wait(1.0);
    ui.move(from.x, from.y);
    ui.frame(body);
    ui.press();
    ui.frame(body);
    ui.move(to.x + 4, to.y);
    ui.frame(body);
    ui.frame(body);
    ui.release();
    ui.frame(body);
    ui.frame(body);
    EXPECT_GT(tabs.rectangles[1].left, tabs.rectangles[2].left);
    EXPECT_LT(tabs.rectangles[0].left, tabs.rectangles[2].left);

    // The trailing button (at the bar's right end) is a button: pressed, never selected.
    click(ui, {.x = tabs.bar.right - 8, .y = center(tabs.bar).y}, body);
    EXPECT_TRUE(tabs.button_pressed);
    EXPECT_TRUE(std::ranges::count(tabs.shown, true) == 1);
}

TEST(UiTabTest, TheTabListButtonSelectsAnyTab)
{
    UiHarness ui;
    Tabs tabs{.names = {"One", "Two", "Three"}, .flags = TabBarFlag::TAB_LIST_POPUP_BUTTON};
    auto body = in_window(std::ref(tabs));
    ui.frames(2, body);
    EXPECT_GT(tabs.rectangles[0].left, tabs.bar.left + 8) << "after the list button";
    const float height = tabs.bar.height();
    click(ui, {.x = tabs.bar.left + height * 0.5F, .y = center(tabs.bar).y}, body);
    ASSERT_EQ(lists(ui), 2U);

    // The third entry of the list (the popup is under the button, its rows a line apart).
    const float line = height - ui.context().theme().style.frame_padding.y * 2;
    click(ui, {.x = tabs.bar.left + 20, .y = tabs.bar.bottom + 8 + (line + 4) * 2 + line * 0.5F}, body);
    ui.frame(body);
    EXPECT_EQ(tabs.shown, (std::vector{false, false, true}));
}

// ===================================================================================================== menus

/// A window with a menu bar: File (Save, Recent > First/Second, Quit) and Edit (Undo), its content after it.
struct MenuWindow
{
    std::uint32_t saved = 0;
    std::uint32_t recent = 0;
    std::uint32_t undone = 0;
    UiRectangle file{};
    UiRectangle edit{};
    bool file_focused = false;
    Vector2 file_menu{};
    Vector2 recent_menu{};
    float content_top = 0;
    bool checked = false;
    bool file_open = false;
    bool recent_open = false;
    bool edit_open = false;

    void operator()(UiFrame& frame)
    {
        if (frame.begin_menu_bar())
        {
            file_open = frame.begin_menu("File");
            if (file_open)
            {
                file_menu = frame.cursor_screen_position();
                saved += frame.menu_item("Save", "Ctrl+S") ? 1U : 0U;
                recent_open = frame.begin_menu("Recent");
                if (recent_open)
                {
                    recent_menu = frame.cursor_screen_position();
                    recent += frame.menu_item("First") ? 1U : 0U;
                    recent += frame.menu_item("Second") ? 2U : 0U;
                    frame.end_menu();
                }
                frame.menu_item("Checked", {}, &checked);
                frame.menu_item("Disabled", {}, false, false);
                frame.end_menu();
            }
            else
            {
                file = frame.item_rectangle();
                file_focused = frame.is_item_focused();
            }
            edit_open = frame.begin_menu("Edit");
            if (edit_open)
            {
                undone += frame.menu_item("Undo") ? 1U : 0U;
                frame.end_menu();
            }
            else
            {
                edit = frame.item_rectangle();
            }
            frame.end_menu_bar();
        }
        content_top = frame.cursor_screen_position().y;
        frame.text_unformatted("content");
    }

    /// The center of row @p row of a menu whose content starts at @p start.
    [[nodiscard]] static auto row(Vector2 start,
                                  std::uint32_t row,
                                  float line) -> Vector2
    {
        return {.x = start.x + 10, .y = start.y + static_cast<float>(row) * (line + 4) + line * 0.5F};
    }
};

TEST(UiMenuTest, AMenuBarMenuOpensOnAPressAndAnItemChosenClosesIt)
{
    UiHarness ui;
    MenuWindow menus;
    auto body = in_window(std::ref(menus), UiWindowFlag::MENU_BAR);
    ui.frames(2, body);
    const Style& style = ui.context().theme().style;
    EXPECT_GT(menus.content_top, menus.file.bottom) << "the content is under the menu bar";
    EXPECT_GT(menus.edit.left, menus.file.right - 1);

    // Pressed: open (a popup over the window); Save chosen: counted, closed.
    click(ui, center(menus.file), body);
    EXPECT_TRUE(menus.file_open);
    EXPECT_EQ(lists(ui), 2U);
    EXPECT_NEAR(menus.file_menu.y, menus.file.bottom + style.window_padding.y, 1.0F);
    const float line = menus.file.height() - style.frame_padding.y * 2;
    click(ui, MenuWindow::row(menus.file_menu, 0, line), body);
    EXPECT_EQ(menus.saved, 1U);
    ui.frame(body);
    EXPECT_FALSE(menus.file_open);
    EXPECT_EQ(lists(ui), 1U);

    // Open, then hovering Edit opens Edit; a press on Edit closes it.
    click(ui, center(menus.file), body);
    ui.move(center(menus.edit).x, center(menus.edit).y);
    ui.frames(2, body);
    EXPECT_FALSE(menus.file_open);
    EXPECT_TRUE(menus.edit_open);
    click(ui, center(menus.edit), body);
    EXPECT_FALSE(menus.edit_open);

    // A check mark toggles; a disabled item cannot be chosen.
    click(ui, center(menus.file), body);
    click(ui, MenuWindow::row(menus.file_menu, 2, line), body);
    EXPECT_TRUE(menus.checked);
    click(ui, center(menus.file), body);
    click(ui, MenuWindow::row(menus.file_menu, 3, line), body);
    EXPECT_TRUE(menus.file_open);
}

TEST(UiMenuTest, SubmenusOpenOnHoverAndAnItemInThemClosesEveryMenu)
{
    UiHarness ui;
    MenuWindow menus;
    auto body = in_window(std::ref(menus), UiWindowFlag::MENU_BAR);
    ui.frames(2, body);
    const float line = menus.file.height() - ui.context().theme().style.frame_padding.y * 2;
    click(ui, center(menus.file), body);

    // Hovering Recent opens it beside File; going down to Checked closes it.
    const Vector2 recent_row = MenuWindow::row(menus.file_menu, 1, line);
    ui.move(recent_row.x, recent_row.y);
    ui.frames(2, body);
    EXPECT_TRUE(menus.recent_open);
    EXPECT_EQ(lists(ui), 3U);
    EXPECT_GT(menus.recent_menu.x, menus.file_menu.x + 20);
    const Vector2 checked_row = MenuWindow::row(menus.file_menu, 2, line);
    ui.move(checked_row.x, checked_row.y);
    ui.frames(2, body);
    EXPECT_FALSE(menus.recent_open);
    EXPECT_TRUE(menus.file_open);

    // Back to Recent, into it, Second chosen: every menu closes.
    ui.move(recent_row.x, recent_row.y);
    ui.frames(2, body);
    click(ui, MenuWindow::row(menus.recent_menu, 1, line), body);
    EXPECT_EQ(menus.recent, 2U);
    ui.frame(body);
    EXPECT_FALSE(menus.file_open);
    EXPECT_EQ(lists(ui), 1U);
}

TEST(UiMenuTest, MenusAreNavigatedWithTheKeyboard)
{
    UiHarness ui;
    MenuWindow menus;
    auto body = in_window(std::ref(menus), UiWindowFlag::MENU_BAR);
    ui.frames(2, body);
    click(ui, {.x = 300, .y = 300}, body);

    // Tab focuses File; Return opens it, its first item focused; Down, Down: Checked; Return toggles it and closes.
    const auto key = [&](Key pressed)
    {
        ui.tap(pressed);
        ui.frame(body);
        ui.frame(body);
    };
    key(Key::TAB);
    EXPECT_TRUE(menus.file_focused);
    key(Key::RETURN);
    EXPECT_TRUE(menus.file_open);
    key(Key::DOWN);
    key(Key::DOWN);
    key(Key::RETURN);
    EXPECT_TRUE(menus.checked);
    EXPECT_FALSE(menus.file_open);

    // Again: Down to Recent, Right opens it (its first item focused), Left closes it, Up to Save, Return saves.
    key(Key::TAB);
    key(Key::RETURN);
    key(Key::DOWN);
    key(Key::RIGHT);
    EXPECT_TRUE(menus.recent_open);
    key(Key::LEFT);
    EXPECT_FALSE(menus.recent_open);
    EXPECT_TRUE(menus.file_open);
    key(Key::UP);
    key(Key::RETURN);
    EXPECT_EQ(menus.saved, 1U);

    // Escape closes the menu and gives the focus back to File.
    key(Key::TAB);
    key(Key::RETURN);
    ASSERT_TRUE(menus.file_open);
    key(Key::ESCAPE);
    EXPECT_FALSE(menus.file_open);
    EXPECT_TRUE(menus.file_focused);
}

TEST(UiMenuTest, TheMainMenuBarSpansTheTopOfTheDisplay)
{
    UiHarness ui;
    UiRectangle file{};
    bool open = false;
    std::uint32_t quit = 0;
    const auto body = [&](UiFrame& frame)
    {
        if (frame.begin_main_menu_bar())
        {
            open = frame.begin_menu("File");
            if (open)
            {
                quit += frame.menu_item("Quit") ? 1U : 0U;
                frame.end_menu();
            }
            else
            {
                file = frame.item_rectangle();
            }
            frame.end_main_menu_bar();
        }
    };
    const UiFrameReport report = ui.frames(2, body);
    EXPECT_TRUE(report.ok()) << (report.issues.empty() ? "" : report.issues.front().detail);
    EXPECT_FLOAT_EQ(file.top, 0.0F);
    EXPECT_GT(file.left, 0.0F);
    EXPECT_LT(file.left, 20.0F);
    click(ui, center(file), body);
    EXPECT_TRUE(open);
}

// ===================================================================================================== popups

TEST(UiPopupTest, OpenPopupShowsItAtTheMouseUntilAClickOutsideOrEscape)
{
    UiHarness ui;
    UiRectangle button{};
    bool shown = false;
    Vector2 popup{};
    std::uint32_t chosen = 0;
    auto body = in_window([&](UiFrame& frame)
    {
        if (frame.button("Open"))
        {
            frame.open_popup("popup");
        }
        button = frame.item_rectangle();
        shown = frame.begin_popup("popup");
        if (shown)
        {
            popup = frame.window_position();
            frame.text_unformatted("in the popup");
            chosen += frame.selectable("Choose") ? 1U : 0U;
            frame.end_popup();
        }
    });
    ui.frames(2, body);
    click(ui, center(button), body);
    ui.frame(body);
    EXPECT_TRUE(shown);
    EXPECT_FLOAT_EQ(popup.x, center(button).x);
    EXPECT_FLOAT_EQ(popup.y, center(button).y);
    EXPECT_EQ(lists(ui), 2U);

    // A click outside closes it; Escape closes it; a selectable in it closes it.
    click(ui, {.x = 400, .y = 380}, body);
    EXPECT_FALSE(shown);
    click(ui, center(button), body);
    ui.tap(Key::ESCAPE);
    ui.frame(body);
    ui.frame(body);
    EXPECT_FALSE(shown);
    click(ui, center(button), body);
    const float line = button.height() - ui.context().theme().style.frame_padding.y * 2;
    click(ui, {.x = popup.x + 20, .y = popup.y + 8 + line + 4 + line * 0.5F}, body);
    EXPECT_EQ(chosen, 1U);
    ui.frame(body);
    EXPECT_FALSE(shown);
}

TEST(UiPopupTest, PopupsOpenedFromPopupsStackAndCloseCurrentPopupClosesOne)
{
    UiHarness ui;
    bool outer = false;
    bool inner = false;
    bool any_level = false;
    bool outer_here = false;
    UiRectangle open_outer{};
    UiRectangle open_inner{};
    UiRectangle close_inner{};
    Vector2 outer_position{};
    auto body = in_window([&](UiFrame& frame)
    {
        if (frame.button("Open"))
        {
            frame.open_popup("outer");
        }
        open_outer = frame.item_rectangle();
        outer_here = frame.is_popup_open("outer");
        outer = frame.begin_popup("outer");
        if (outer)
        {
            // Popup identifiers follow the identifier stack: "inner" is asked for inside the outer popup.
            any_level = frame.is_popup_open("inner", PopupFlag::ANY_POPUP_LEVEL);
            outer_position = frame.window_position();
            if (frame.button("Inner"))
            {
                frame.open_popup("inner");
            }
            open_inner = frame.item_rectangle();
            inner = frame.begin_popup("inner");
            if (inner)
            {
                if (frame.button("Close"))
                {
                    frame.close_current_popup();
                }
                close_inner = frame.item_rectangle();
                frame.end_popup();
            }
            frame.end_popup();
        }
    });
    ui.frames(2, body);
    click(ui, center(open_outer), body);
    ui.frame(body);
    ASSERT_TRUE(outer);
    EXPECT_TRUE(outer_here);
    click(ui, center(open_inner), body);
    ui.frame(body);
    EXPECT_TRUE(inner);
    EXPECT_TRUE(any_level);
    EXPECT_EQ(lists(ui), 3U);

    // Closing the inner one keeps the outer one.
    click(ui, center(close_inner), body);
    ui.frame(body);
    EXPECT_FALSE(inner);
    EXPECT_TRUE(outer);

    // A click in the outer one (outside the inner one) closes the inner one only.
    click(ui, center(open_inner), body);
    ui.frame(body);
    ASSERT_TRUE(inner);
    click(ui, {.x = outer_position.x + 3, .y = outer_position.y + 3}, body);
    EXPECT_FALSE(inner);
    EXPECT_TRUE(outer);
}

TEST(UiPopupTest, ContextPopupsOpenWhenTheRightButtonIsReleased)
{
    UiHarness ui;
    UiRectangle button{};
    bool item_menu = false;
    bool window_menu = false;
    auto body = in_window([&](UiFrame& frame)
    {
        frame.button("Target");
        button = frame.item_rectangle();
        item_menu = frame.begin_popup_context_item();
        if (item_menu)
        {
            frame.text_unformatted("item");
            frame.end_popup();
        }
        window_menu = frame.begin_popup_context_window({}, PopupFlag::MOUSE_BUTTON_RIGHT | PopupFlag::NO_OPEN_OVER_ITEMS);
        if (window_menu)
        {
            frame.text_unformatted("window");
            frame.end_popup();
        }
    });
    ui.frames(2, body);
    click(ui, center(button), body, MouseButton::RIGHT);
    EXPECT_TRUE(item_menu);
    EXPECT_FALSE(window_menu);
    click(ui, {.x = 300, .y = 300}, body, MouseButton::RIGHT);
    EXPECT_FALSE(item_menu);
    EXPECT_TRUE(window_menu);
    click(ui, {.x = 100, .y = 380}, body);
    EXPECT_FALSE(window_menu);
}

TEST(UiPopupTest, ModalsBlockWhatIsBehindThemAndCloseOnlyByCodeOrTheirButton)
{
    UiHarness ui;
    UiRectangle open_button{};
    UiRectangle behind{};
    std::uint32_t behind_presses = 0;
    bool modal = false;
    bool closable = true;
    Vector2 position{};
    Vector2 size{};
    UiRectangle ok{};
    auto body = in_window([&](UiFrame& frame)
    {
        if (frame.button("Ask"))
        {
            frame.open_popup("Confirm");
            closable = true;
        }
        open_button = frame.item_rectangle();
        behind_presses += frame.button("Behind") ? 1U : 0U;
        behind = frame.item_rectangle();
        modal = frame.begin_modal("Confirm", closable);
        if (modal)
        {
            position = frame.window_position();
            size = frame.window_size();
            frame.text_unformatted("Are you sure?");
            if (frame.button("OK"))
            {
                frame.close_current_popup();
            }
            ok = frame.item_rectangle();
            frame.end_popup();
        }
    });
    ui.frames(2, body);
    click(ui, center(open_button), body);
    ui.frames(2, body);
    ASSERT_TRUE(modal);
    EXPECT_NEAR(position.x + size.x * 0.5F, UiHarness::DISPLAY.x * 0.5F, 1.0F);
    EXPECT_NEAR(position.y + size.y * 0.5F, UiHarness::DISPLAY.y * 0.5F, 1.0F);
    EXPECT_EQ(lists(ui), 3U) << "the window, the dimming, the modal";

    // Behind it nothing reacts; clicks outside and Escape leave it open.
    click(ui, center(behind), body);
    EXPECT_EQ(behind_presses, 0U);
    EXPECT_TRUE(modal);
    ui.tap(Key::ESCAPE);
    ui.frame(body);
    EXPECT_TRUE(modal);

    // Its button closes it by code; its close button closes it too.
    click(ui, center(ok), body);
    ui.frame(body);
    EXPECT_FALSE(modal);
    click(ui, center(open_button), body);
    ui.frames(2, body);
    ASSERT_TRUE(modal);
    const Style& style = ui.context().theme().style;
    const float line = ok.height() - style.frame_padding.y * 2;
    click(ui, {.x = position.x + size.x - style.frame_padding.x - line * 0.5F, .y = position.y + ok.height() * 0.5F}, body);
    EXPECT_FALSE(closable);
    EXPECT_FALSE(modal);
    click(ui, center(behind), body);
    EXPECT_EQ(behind_presses, 1U);
}

// ===================================================================================================== tooltips

TEST(UiTooltipTest, TooltipsShowAtTheMouseOverEverythingAndAreNeverHovered)
{
    UiHarness ui;
    UiRectangle button{};
    std::uint32_t presses = 0;
    bool show = false;
    Vector2 tooltip{};
    auto body = in_window([&](UiFrame& frame)
    {
        presses += frame.button("Under") ? 1U : 0U;
        button = frame.item_rectangle();
        if (show && frame.begin_tooltip())
        {
            tooltip = frame.window_position();
            frame.text_unformatted("A tooltip");
            frame.end_tooltip();
        }
    });
    ui.frames(2, body);
    show = true;
    ui.move(center(button).x, center(button).y);
    ui.frames(2, body);
    EXPECT_EQ(lists(ui), 2U);
    EXPECT_FLOAT_EQ(tooltip.x, center(button).x + TOOLTIP_OFFSET_X);
    EXPECT_FLOAT_EQ(tooltip.y, center(button).y + TOOLTIP_OFFSET_Y);
    click(ui, center(button), body);
    EXPECT_EQ(presses, 1U);
}

TEST(UiTooltipTest, ItemTooltipsWaitForTheMouseToStayStill)
{
    UiHarness ui;
    UiRectangle button{};
    auto body = in_window([&](UiFrame& frame)
    {
        frame.button("Explained");
        button = frame.item_rectangle();
        frame.item_tooltip("What it does");
    });
    ui.frames(2, body);

    // Moving over it: no tooltip; still for the delay: the tooltip.
    for (int step = 0; step < 20; ++step)
    {
        ui.move(button.left + 2 + static_cast<float>(step), center(button).y);
        ui.frame(body);
        EXPECT_EQ(lists(ui), 1U) << step;
    }
    ui.wait(TOOLTIP_DELAY_SECONDS);
    ui.frames(3, body);
    EXPECT_EQ(lists(ui), 2U);

    // Away from it: gone.
    ui.move(400, 380);
    ui.frames(2, body);
    EXPECT_EQ(lists(ui), 1U);
}

// ===================================================================================================== drag and drop

TEST(UiDragDropTest, APayloadIsDeliveredToATargetOfItsTypeWhenReleasedOverIt)
{
    UiHarness ui;
    UiRectangle source{};
    UiRectangle target{};
    UiRectangle other{};
    std::uint32_t source_presses = 0;
    std::optional<std::int32_t> delivered;
    std::uint32_t wrong_type = 0;
    bool accepted = false;
    bool dragging = false;
    auto body = in_window([&](UiFrame& frame)
    {
        source_presses += frame.button("Source") ? 1U : 0U;
        source = frame.item_rectangle();
        dragging = frame.begin_drag_drop_source();
        if (dragging)
        {
            accepted = frame.set_drag_drop_payload("number", std::int32_t{42});
            frame.text_unformatted("42");
            frame.end_drag_drop_source();
        }
        frame.button("Target", {.x = 120, .y = 40});
        target = frame.item_rectangle();
        if (frame.begin_drag_drop_target())
        {
            if (const std::optional<UiDragDropPayload> payload = frame.accept_drag_drop_payload("number"))
            {
                std::int32_t value = 0;
                std::memcpy(&value, payload->data.data(), sizeof(value));
                delivered = payload->delivery ? std::optional{value} : std::nullopt;
            }
            frame.end_drag_drop_target();
        }
        frame.button("Other", {.x = 120, .y = 40});
        other = frame.item_rectangle();
        if (frame.begin_drag_drop_target())
        {
            wrong_type += frame.accept_drag_drop_payload("text").has_value() ? 1U : 0U;
            frame.end_drag_drop_target();
        }
    });
    ui.frames(2, body);

    // Pressed on the source, moved past the drag threshold: dragged, a preview at the mouse.
    ui.move(center(source).x, center(source).y);
    ui.frame(body);
    ui.press();
    ui.frame(body);
    EXPECT_FALSE(dragging);
    ui.move(center(source).x + 20, center(source).y);
    ui.frames(2, body);
    EXPECT_TRUE(dragging);
    EXPECT_EQ(lists(ui), 2U);

    // Over the other button (wrong type), then the target: accepted, then delivered on release.
    ui.move(center(other).x, center(other).y);
    ui.frames(2, body);
    EXPECT_FALSE(accepted);
    ui.move(center(target).x, center(target).y);
    ui.frames(2, body);
    EXPECT_TRUE(accepted);
    EXPECT_FALSE(delivered.has_value());
    ui.release();
    ui.frame(body);
    EXPECT_EQ(delivered, 42);
    EXPECT_EQ(wrong_type, 0U);
    EXPECT_EQ(source_presses, 0U);
    ui.frames(2, body);
    EXPECT_FALSE(dragging);
    EXPECT_EQ(lists(ui), 1U);
}

TEST(UiDragDropTest, TargetsPeekBeforeDeliveryAndPayloadsDroppedElsewhereAreLost)
{
    UiHarness ui;
    UiRectangle source{};
    UiRectangle target{};
    bool peeked = false;
    bool delivered = false;
    auto body = in_window([&](UiFrame& frame)
    {
        frame.text_unformatted("Drag me");
        source = frame.item_rectangle();
        if (frame.begin_drag_drop_source(DragDropFlag::SOURCE_ALLOW_NULL_IDENTIFIER | DragDropFlag::SOURCE_NO_PREVIEW_TOOLTIP))
        {
            frame.set_drag_drop_payload("text", std::as_bytes(std::span{"hello", 5}));
            frame.end_drag_drop_source();
        }
        frame.button("Peek", {.x = 120, .y = 40});
        target = frame.item_rectangle();
        peeked = false;
        if (frame.begin_drag_drop_target())
        {
            if (const std::optional<UiDragDropPayload> payload = frame.accept_drag_drop_payload("text", DragDropFlag::ACCEPT_BEFORE_DELIVERY))
            {
                peeked = !payload->delivery && std::string_view{reinterpret_cast<const char*>(payload->data.data()), payload->data.size()} == "hello";
                delivered = delivered || payload->delivery;
            }
            frame.end_drag_drop_target();
        }
    });
    ui.frames(2, body);
    ui.move(center(source).x, center(source).y);
    ui.frame(body);
    ui.press();
    ui.frame(body);
    ui.move(center(target).x, center(target).y);
    ui.frames(3, body);
    EXPECT_TRUE(peeked);
    EXPECT_EQ(lists(ui), 1U) << "no preview";

    // Released elsewhere: not delivered; the drag ends.
    ui.move(400, 380);
    ui.frame(body);
    ui.release();
    ui.frames(3, body);
    EXPECT_FALSE(delivered);
    ui.move(center(target).x, center(target).y);
    ui.frames(2, body);
    EXPECT_FALSE(peeked);
}

TEST(UiDragDropTest, APayloadHeldOverAClosedNodeOrATabOpensIt)
{
    UiHarness ui;
    UiRectangle source{};
    UiRectangle node{};
    bool open = false;
    Tabs tabs{.names = {"One", "Two"}};
    auto body = in_window([&](UiFrame& frame)
    {
        frame.button("Source");
        source = frame.item_rectangle();
        if (frame.begin_drag_drop_source())
        {
            frame.set_drag_drop_payload("number", 1);
            frame.end_drag_drop_source();
        }
        open = frame.tree_node("Folder");
        node = frame.item_rectangle();
        if (open)
        {
            frame.tree_pop();
        }
        tabs(frame);
    });
    ui.frames(2, body);
    ui.move(center(source).x, center(source).y);
    ui.frame(body);
    ui.press();
    ui.frame(body);
    ui.move(center(node).x, center(node).y);
    ui.frames(2, body);
    EXPECT_FALSE(open);
    ui.wait(DRAG_DROP_HOLD_TO_OPEN_SECONDS);
    ui.frames(2, body);
    EXPECT_TRUE(open);
    ui.move(center(tabs.rectangles[1]).x, center(tabs.rectangles[1]).y);
    ui.frames(2, body);
    ui.wait(DRAG_DROP_HOLD_TO_OPEN_SECONDS);
    ui.frames(3, body);
    EXPECT_TRUE(tabs.shown[1]);
    ui.release();
    ui.frame(body);
}

TEST(UiDragDropTest, ColorsAreDraggedFromSwatchesOntoColorEditors)
{
    UiHarness ui;
    const Color red = Color::from_rgba8(255, 0, 0);
    Color edited = Color::from_rgba8(0, 0, 255);
    UiRectangle swatch{};
    UiRectangle editor{};
    auto body = in_window([&](UiFrame& frame)
    {
        frame.color_button("Red", red);
        swatch = frame.item_rectangle();
        frame.color_edit("Edited", edited);
        editor = frame.item_rectangle();
    });
    ui.frames(2, body);
    ui.move(center(swatch).x, center(swatch).y);
    ui.frame(body);
    ui.press();
    ui.frame(body);
    ui.move(editor.right - 6, center(editor).y);
    ui.frames(3, body);
    ui.release();
    ui.frame(body);
    EXPECT_EQ(edited.packed(), red.packed());
}

TEST(UiDragDropTest, SourcesWithoutIdentifiersAreReportedUnlessAllowed)
{
    UiHarness ui;
    UiFrameReport report = ui.frame(in_window([](UiFrame& frame)
    {
        frame.text_unformatted("plain text");
        static_cast<void>(frame.begin_drag_drop_source());
    }));
    EXPECT_TRUE(has_issue(report, UiFrameProblem::INVALID_ARGUMENT));
    report = ui.frame(in_window([](UiFrame& frame) { static_cast<void>(frame.set_drag_drop_payload("x", 1)); }));
    EXPECT_TRUE(has_issue(report, UiFrameProblem::END_WITHOUT_BEGIN));
}

} // namespace
