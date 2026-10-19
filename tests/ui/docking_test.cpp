// Docking and saved layouts of UiFrame / UiContext driven by synthetic input: a dock builder's layout, docked windows as
// the tabs of their node (selection, closing), tearing a tab off, docking a dragged window beside or into a node and
// onto a floating window (a floating tree), separators, the dock space flags, set_next_window_dock(), the display order
// of docked windows; save_layout() / load_layout() round trips (windows, dock trees, tables) and refused layouts.
#include <gtest/gtest.h>

import std;
import gfx.ui;
import gfx.tests.ui_context_support;

namespace
{

using namespace gfx::ui;
using gfx::Key;
using gfx::MouseButton;
using gfx::Vector2;
using gfx::tests::center;
using gfx::tests::has_issue;
using gfx::tests::UiHarness;

/// The style the tests' contexts use.
const Style STYLE = builtin_theme(ThemeKind::DARK).style;

constexpr DockSpaceIdentifier EDITOR = DockSpaceIdentifier{"editor"};

/// What a window did this frame.
struct WindowProbe
{
    bool begun = false;
    bool docked = false;
    DockNodeHandle node{};
    UiRectangle rectangle{};
};

/// A frame of a dock space over the display and the windows named in @p names (each recorded in @p probes; @p open,
/// when given, is the close flag of each), then the floating windows of @p floating.
struct DockingScene
{
    std::vector<std::string> names{};
    std::vector<WindowProbe> probes{};
    std::vector<bool> open{};
    DockNodeFlag flags = DockNodeFlag::NONE;
    bool submit_dock_space = true;
    DockNodeHandle root{};
    std::function<void(UiFrame&, std::size_t)> content{};

    void operator()(UiFrame& frame)
    {
        probes.assign(names.size(), WindowProbe{});
        if (submit_dock_space)
        {
            root = frame.dock_space(EDITOR, {}, flags);
        }
        for (std::size_t window = 0; window < names.size(); ++window)
        {
            frame.set_next_window_position({.x = 60.0F + 40.0F * static_cast<float>(window), .y = 60.0F + 30.0F * static_cast<float>(window)},
                                           UiCondition::FIRST_USE_EVER);
            frame.set_next_window_size({.x = 200, .y = 150}, UiCondition::FIRST_USE_EVER);
            bool keep = window < open.size() ? static_cast<bool>(open[window]) : true;
            const bool begun = window < open.size() ? frame.begin_window(names[window], keep) : frame.begin_window(names[window]);
            if (window < open.size())
            {
                open[window] = keep;
            }
            WindowProbe& probe = probes[window];
            probe.begun = begun;
            probe.docked = frame.is_window_docked();
            probe.node = frame.window_dock_node();
            const Vector2 position = frame.window_position();
            const Vector2 size = frame.window_size();
            probe.rectangle = {.left = position.x, .top = position.y, .right = position.x + size.x, .bottom = position.y + size.y};
            if (begun)
            {
                frame.text("{} content", names[window]);
                if (content)
                {
                    content(frame, window);
                }
            }
            frame.end_window();
        }
    }
};

/// The editor layout: Outliner on the left (a quarter), Console at the bottom of the rest (30 %), Scene and Assets as
/// tabs of the center (Assets selected, docked last).
void
build_editor(UiContext& context)
{
    UiDockBuilder builder = context.dock_builder(EDITOR);
    const auto [left, rest] = builder.split(builder.root(), ArrowDirection::LEFT, 0.25F);
    const auto [bottom, middle] = builder.split(rest, ArrowDirection::DOWN, 0.3F);
    builder.dock_window("Outliner", left);
    builder.dock_window("Console", bottom);
    builder.dock_window("Scene", middle);
    builder.dock_window("Assets", middle);
    builder.finish();
}

/// Drags the mouse from @p from to @p to in @p steps frames with the left button held, running @p body each frame.
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

/// The height of a node's tab bar (measured in a context of its own: a frame without a scene's windows would hide
/// their tabs).
auto
tab_bar_height(UiHarness& /*ui*/) -> float
{
    UiHarness measure;
    float height = 0;
    measure.frame([&](UiFrame& frame) { height = std::floor(frame.frame_height()); });
    return height;
}

/// The width of a tab whose label is @p title (no close button).
auto
tab_width(UiHarness& /*ui*/,
          std::string_view title) -> float
{
    UiHarness measure;
    float width = 0;
    measure.frame([&](UiFrame& frame) { width = frame.calculate_text_size(title).x + STYLE.frame_padding.x * 2; });
    return width;
}

/// The center of the docking square @p offset steps from the middle of @p area (0, 0: the center square).
auto
target(const UiRectangle& area,
       float dx,
       float dy) -> Vector2
{
    const float size = std::min({DOCKING_TARGET_SIZE, area.width() / 3.5F, area.height() / 3.5F});
    return {.x = (area.left + area.right) * 0.5F + dx * size * 1.25F, .y = (area.top + area.bottom) * 0.5F + dy * size * 1.25F};
}

// ===================================================================================================== builder

TEST(UiDockingTest, ADockBuilderLaysOutWindowsInItsNodes)
{
    UiHarness ui;
    build_editor(ui.context());
    DockingScene scene{.names = {"Outliner", "Scene", "Assets", "Console"}};
    const UiFrameReport report = ui.frames(3, std::ref(scene));
    EXPECT_TRUE(report.ok()) << (report.issues.empty() ? "" : report.issues.front().detail);
    const float bar = tab_bar_height(ui);
    ui.frame(std::ref(scene));

    // Outliner: the left quarter of the display (beside the separator), under its tab bar.
    const float separator = STYLE.docking_separator_size;
    const WindowProbe& outliner = scene.probes[0];
    EXPECT_TRUE(outliner.docked);
    EXPECT_TRUE(outliner.begun);
    EXPECT_TRUE(outliner.node.is_valid());
    EXPECT_FLOAT_EQ(outliner.rectangle.left, 0.0F);
    EXPECT_FLOAT_EQ(outliner.rectangle.top, bar);
    EXPECT_FLOAT_EQ(outliner.rectangle.right, std::round((800.0F - separator) * 0.25F));
    EXPECT_FLOAT_EQ(outliner.rectangle.bottom, 600.0F);

    // Scene and Assets share the middle node: Assets, docked last, is selected; Scene's begin_window() returns false.
    const WindowProbe& assets = scene.probes[2];
    const WindowProbe& middle = scene.probes[1];
    EXPECT_TRUE(assets.begun);
    EXPECT_FALSE(middle.begun);
    EXPECT_TRUE(middle.docked);
    EXPECT_EQ(middle.node, assets.node);
    EXPECT_FLOAT_EQ(assets.rectangle.left, outliner.rectangle.right + separator);
    EXPECT_FLOAT_EQ(assets.rectangle.right, 800.0F);

    // Console: the bottom 30 % of the rest.
    const WindowProbe& console = scene.probes[3];
    EXPECT_TRUE(console.begun);
    EXPECT_FLOAT_EQ(console.rectangle.left, assets.rectangle.left);
    EXPECT_NEAR(console.rectangle.top - bar - separator, assets.rectangle.bottom, 0.01F);
    EXPECT_NEAR(600.0F - (console.rectangle.top - bar), (600.0F - separator) * 0.3F, 1.0F);
    EXPECT_EQ(scene.root, scene.root);
    EXPECT_TRUE(scene.root.is_valid());
}

TEST(UiDockingTest, BuilderMisuseThrows)
{
    UiHarness ui;
    EXPECT_THROW(static_cast<void>(ui.context().dock_builder(DockSpaceIdentifier{})), gfx::InvalidUsageError);
    UiDockBuilder builder = ui.context().dock_builder(EDITOR);
    const auto [left, rest] = builder.split(builder.root(), ArrowDirection::LEFT, 0.5F);
    EXPECT_THROW(static_cast<void>(builder.split(builder.root(), ArrowDirection::LEFT, 0.5F)), gfx::InvalidUsageError) << "the root is split";
    EXPECT_THROW(static_cast<void>(builder.split(left, ArrowDirection::UP, 0.0F)), gfx::InvalidUsageError);
    EXPECT_THROW(builder.dock_window("", left), gfx::InvalidUsageError);
    UiDockBuilder other = ui.context().dock_builder(DockSpaceIdentifier{"other"});
    EXPECT_THROW(other.dock_window("Window", left), gfx::InvalidUsageError) << "a node of another dock space";
    builder.dock_window("Window", builder.root());
    builder.finish();
    EXPECT_THROW(builder.dock_window("Window", rest), gfx::InvalidUsageError) << "finished";

    // Building again empties the tree: the old nodes' handles go stale.
    UiDockBuilder again = ui.context().dock_builder(EDITOR);
    EXPECT_THROW(again.dock_window("Window", rest), gfx::InvalidUsageError);
}

// ===================================================================================================== tabs

TEST(UiDockingTest, ATabClickSelectsItsWindowAndItsCloseButtonClosesIt)
{
    UiHarness ui;
    build_editor(ui.context());
    DockingScene scene{.names = {"Outliner", "Scene", "Assets", "Console"}, .open = {true, true, true, true}};
    ui.frames(3, std::ref(scene));
    const UiRectangle node = scene.probes[2].rectangle;
    const float bar = tab_bar_height(ui);
    ui.frame(std::ref(scene));

    // Scene's tab is the first of the node's bar.
    ui.click(node.left + 6, node.top - bar * 0.5F, std::ref(scene));
    ui.wait(1.0);
    ui.frame(std::ref(scene));
    EXPECT_TRUE(scene.probes[1].begun) << "Scene selected";
    EXPECT_FALSE(scene.probes[2].begun);

    // Its close button (shown on the selected tab) closes it; Assets is shown instead.
    const float close_size = tab_bar_height(ui) - STYLE.frame_padding.y * 2;
    const float scene_tab = tab_width(ui, "Scene") + STYLE.item_inner_spacing.x + close_size;
    ui.click(node.left + scene_tab - STYLE.frame_padding.x - close_size * 0.5F, node.top - bar * 0.5F, std::ref(scene));
    ui.frames(3, std::ref(scene));
    EXPECT_FALSE(scene.open[1]) << "Scene closed by its tab";
    EXPECT_TRUE(scene.probes[2].begun) << "Assets shown";
}

TEST(UiDockingTest, DraggingATabOutUndocksItsWindow)
{
    UiHarness ui;
    build_editor(ui.context());
    DockingScene scene{.names = {"Outliner", "Scene", "Assets", "Console"}};
    ui.frames(3, std::ref(scene));
    const UiRectangle node = scene.probes[2].rectangle;
    const float bar = tab_bar_height(ui);
    ui.frame(std::ref(scene));
    const Vector2 tab{.x = node.left + tab_width(ui, "Scene") + STYLE.item_inner_spacing.x + 6, .y = node.top - bar * 0.5F};
    drag(ui, tab, {.x = tab.x + 40, .y = tab.y + 200}, 6, std::ref(scene));
    ui.frames(2, std::ref(scene));
    EXPECT_FALSE(scene.probes[2].docked) << "Assets floats";
    EXPECT_TRUE(scene.probes[2].begun);
    EXPECT_NEAR(scene.probes[2].rectangle.top, tab.y + 200 - bar * 0.5F, 2.0F) << "under the mouse";
    EXPECT_TRUE(scene.probes[1].begun) << "Scene, alone in the node, is shown";
    EXPECT_TRUE(scene.probes[1].docked);

    // With NO_UNDOCKING, a tab stays.
    UiHarness locked;
    build_editor(locked.context());
    DockingScene fixed{.names = {"Outliner", "Scene", "Assets", "Console"}, .flags = DockNodeFlag::NO_UNDOCKING};
    locked.frames(3, std::ref(fixed));
    drag(locked, tab, {.x = tab.x + 40, .y = tab.y + 200}, 6, std::ref(fixed));
    locked.frames(2, std::ref(fixed));
    EXPECT_TRUE(fixed.probes[2].docked);
}

// ===================================================================================================== docking by dragging

TEST(UiDockingTest, AWindowDraggedOverANodesSquaresDocksIntoOrBesideIt)
{
    UiHarness ui;
    build_editor(ui.context());
    DockingScene scene{.names = {"Outliner", "Scene", "Assets", "Console", "Floating"}};
    ui.frames(3, std::ref(scene));
    const UiRectangle node = scene.probes[2].rectangle;
    const float bar = tab_bar_height(ui);
    const UiRectangle node_area{.left = node.left, .top = node.top - bar, .right = node.right, .bottom = node.bottom};
    const UiRectangle floating = scene.probes[4].rectangle;
    ASSERT_FALSE(scene.probes[4].docked);

    // Its title bar dragged onto the center square: a tab of the node, selected.
    const Vector2 title{.x = floating.left + 30, .y = floating.top + 8};
    const Vector2 middle = target(node_area, 0, 0);
    drag(ui, title, middle, 8, std::ref(scene));
    ui.frames(2, std::ref(scene));
    EXPECT_TRUE(scene.probes[4].docked);
    EXPECT_EQ(scene.probes[4].node, scene.probes[2].node);
    EXPECT_TRUE(scene.probes[4].begun) << "selected";
    EXPECT_FALSE(scene.probes[2].begun);

    // Torn off again, then dropped on the left square: the node splits, the window on its left half.
    const float tabs = tab_width(ui, "Scene") + tab_width(ui, "Assets") + STYLE.item_inner_spacing.x * 2;
    const Vector2 tab{.x = node.left + tabs + 6, .y = node.top - bar * 0.5F};
    drag(ui, tab, {.x = tab.x, .y = tab.y + 250}, 6, std::ref(scene));
    ui.frames(2, std::ref(scene));
    ASSERT_FALSE(scene.probes[4].docked);
    const UiRectangle torn = scene.probes[4].rectangle;
    drag(ui, {.x = torn.left + 30, .y = torn.top + 8}, target(node_area, -1, 0), 8, std::ref(scene));
    ui.frames(2, std::ref(scene));
    EXPECT_TRUE(scene.probes[4].docked);
    EXPECT_NE(scene.probes[4].node, scene.probes[2].node);
    EXPECT_FLOAT_EQ(scene.probes[4].rectangle.left, node.left);
    EXPECT_NEAR(scene.probes[4].rectangle.width(), (node.width() - STYLE.docking_separator_size) * 0.5F, 1.0F);
    EXPECT_NEAR(scene.probes[2].rectangle.right, node.right, 0.01F) << "the node's windows on the right half";
}

TEST(UiDockingTest, AWindowDroppedOntoAFloatingWindowMakesAFloatingTree)
{
    UiHarness ui;
    DockingScene scene{.names = {"First", "Second"}, .submit_dock_space = false};
    ui.frames(3, std::ref(scene));
    const UiRectangle first = scene.probes[0].rectangle;
    const UiRectangle second = scene.probes[1].rectangle;
    drag(ui, {.x = second.left + 30, .y = second.top + 8}, target(first, 0, 0), 8, std::ref(scene));
    ui.frames(2, std::ref(scene));
    EXPECT_TRUE(scene.probes[0].docked);
    EXPECT_TRUE(scene.probes[1].docked);
    EXPECT_EQ(scene.probes[0].node, scene.probes[1].node) << "tabs of one node";
    EXPECT_TRUE(scene.probes[1].begun);
    EXPECT_FALSE(scene.probes[0].begun);

    // The tree is where the first window was; tearing one window off leaves one: the tree dissolves.
    const float bar = tab_bar_height(ui);
    EXPECT_FLOAT_EQ(scene.probes[1].rectangle.left, first.left);
    EXPECT_FLOAT_EQ(scene.probes[1].rectangle.top, first.top + bar);
    const Vector2 tab{.x = first.left + tab_width(ui, "First") + STYLE.item_inner_spacing.x + 6, .y = first.top + bar * 0.5F};
    drag(ui, tab, {.x = tab.x + 200, .y = tab.y + 200}, 6, std::ref(scene));
    ui.frames(2, std::ref(scene));
    EXPECT_FALSE(scene.probes[0].docked);
    EXPECT_FALSE(scene.probes[1].docked);
    EXPECT_TRUE(scene.probes[0].begun);
    EXPECT_TRUE(scene.probes[1].begun);
}

TEST(UiDockingTest, SeparatorsResizeNodesUnlessTheDockSpaceForbidsIt)
{
    for (const bool fixed : {false, true})
    {
        UiHarness ui;
        build_editor(ui.context());
        DockingScene scene{.names = {"Outliner", "Scene", "Assets", "Console"}, .flags = fixed ? DockNodeFlag::NO_RESIZE : DockNodeFlag::NONE};
        ui.frames(3, std::ref(scene));
        const float before = scene.probes[0].rectangle.right;
        drag(ui, {.x = before + 1, .y = 300}, {.x = before + 61, .y = 300}, 6, std::ref(scene));
        ui.frames(2, std::ref(scene));
        EXPECT_NEAR(scene.probes[0].rectangle.right, fixed ? before : before + 60, 1.0F) << fixed;
        EXPECT_NEAR(scene.probes[2].rectangle.left, (fixed ? before : before + 60) + STYLE.docking_separator_size, 1.0F) << fixed;
    }
}

TEST(UiDockingTest, DockSpaceFlagsLimitDockingAndKeepAliveHidesTheWindows)
{
    // NO_DOCKING_SPLIT: the side squares are not offered (a drop there does nothing).
    UiHarness ui;
    build_editor(ui.context());
    DockingScene scene{.names = {"Outliner", "Scene", "Assets", "Console", "Floating"}, .flags = DockNodeFlag::NO_DOCKING_SPLIT};
    ui.frames(3, std::ref(scene));
    const float bar = tab_bar_height(ui);
    const UiRectangle node = scene.probes[2].rectangle;
    const UiRectangle area{.left = node.left, .top = node.top - bar, .right = node.right, .bottom = node.bottom};
    const UiRectangle floating = scene.probes[4].rectangle;
    drag(ui, {.x = floating.left + 30, .y = floating.top + 8}, target(area, -1, 0), 8, std::ref(scene));
    ui.frames(2, std::ref(scene));
    EXPECT_FALSE(scene.probes[4].docked);

    // KEEP_ALIVE_ONLY: nothing drawn, the windows stay docked and hidden.
    scene.flags = DockNodeFlag::KEEP_ALIVE_ONLY;
    ui.frames(2, std::ref(scene));
    EXPECT_TRUE(scene.probes[0].docked);
    EXPECT_FALSE(scene.probes[0].begun);
    scene.flags = DockNodeFlag::NONE;
    ui.frames(2, std::ref(scene));
    EXPECT_TRUE(scene.probes[0].begun);

    // A dock space not submitted: its windows are hidden (still docked).
    scene.submit_dock_space = false;
    ui.frames(3, std::ref(scene));
    EXPECT_FALSE(scene.probes[0].begun);
    EXPECT_TRUE(scene.probes[0].docked);
}

TEST(UiDockingTest, AnEmptyPassThroughCentralNodeLetsTheMouseThrough)
{
    UiHarness ui;
    UiDockBuilder builder = ui.context().dock_builder(EDITOR);
    const auto [left, rest] = builder.split(builder.root(), ArrowDirection::LEFT, 0.25F);
    builder.dock_window("Outliner", left);
    static_cast<void>(rest);
    builder.finish();
    DockingScene scene{.names = {"Outliner"}, .flags = DockNodeFlag::PASS_THROUGH_CENTRAL_NODE};
    ui.frames(3, std::ref(scene));
    ui.move(600, 300);
    ui.frames(2, std::ref(scene));
    EXPECT_FALSE(ui.context().wants_mouse()) << "over the empty central node";
    ui.move(100, 300);
    ui.frames(2, std::ref(scene));
    EXPECT_TRUE(ui.context().wants_mouse()) << "over the Outliner";
}

TEST(UiDockingTest, SetNextWindowDockDocksByItsCondition)
{
    UiHarness ui;
    DockNodeHandle root{};
    bool docked = false;
    UiCondition condition = UiCondition::FIRST_USE_EVER;
    const auto body = [&](UiFrame& frame)
    {
        root = frame.dock_space(EDITOR);
        if (root.is_valid())
        {
            frame.set_next_window_dock(root, condition);
        }
        frame.begin_window("Tool");
        docked = frame.is_window_docked();
        frame.end_window();
    };
    ui.frames(3, body);
    EXPECT_TRUE(docked);

    // Torn off, it stays floating (FIRST_USE_EVER docked it once); with ALWAYS it docks again.
    const float bar = tab_bar_height(ui);
    drag(ui, {.x = 10, .y = bar * 0.5F}, {.x = 300, .y = 300}, 6, body);
    ui.frames(2, body);
    EXPECT_FALSE(docked);
    condition = UiCondition::ALWAYS;
    ui.frames(2, body);
    EXPECT_TRUE(docked);

    // A stale handle (a node of a layout built again) is reported.
    UiDockBuilder builder = ui.context().dock_builder(EDITOR);
    const DockNodeHandle stale = builder.split(builder.root(), ArrowDirection::LEFT, 0.5F).first;
    static_cast<void>(ui.context().dock_builder(EDITOR));
    const UiFrameReport report = ui.frame([&](UiFrame& frame)
    {
        frame.set_next_window_dock(stale);
        frame.begin_window("Tool");
        frame.end_window();
    });
    EXPECT_TRUE(has_issue(report, UiFrameProblem::INVALID_ARGUMENT));
}

TEST(UiDockingTest, FloatingWindowsStayOverDockedOnesWhenTheseAreFocused)
{
    UiHarness ui;
    build_editor(ui.context());
    bool floating_hovered = false;
    bool scene_hovered = false;
    DockingScene scene{.names = {"Outliner", "Scene", "Assets", "Console", "Floating"}};
    scene.content = [&](UiFrame& frame, std::size_t window)
    {
        if (window == 4)
        {
            floating_hovered = frame.is_window_hovered();
        }
        if (window == 2)
        {
            scene_hovered = frame.is_window_hovered();
        }
    };
    ui.frames(3, std::ref(scene));
    const UiRectangle floating = scene.probes[4].rectangle;
    const UiRectangle assets = scene.probes[2].rectangle;
    ASSERT_GT(floating.right, assets.left) << "the floating window overlaps the docked one";

    // A click on the docked window focuses it but brings its host (behind everything) forward, not over the floating one.
    ui.click(assets.right - 20, assets.bottom - 20, std::ref(scene));
    ui.move(std::max(floating.left, assets.left) + 10, floating.top + 40);
    ui.frames(2, std::ref(scene));
    EXPECT_TRUE(floating_hovered);
    EXPECT_FALSE(scene_hovered);
}

// ===================================================================================================== layouts

TEST(UiLayoutTest, WindowsAndDockTreesRoundTrip)
{
    std::string saved{};
    {
        UiHarness ui;
        build_editor(ui.context());
        DockingScene scene{.names = {"Outliner", "Scene", "Assets", "Console", "Free", "First", "Second"}};
        ui.frames(3, std::ref(scene));
        const float bar = tab_bar_height(ui);
        const UiRectangle node = scene.probes[2].rectangle;

        // Scene selected; the left separator moved; Free moved; Second docked onto First (a floating tree).
        ui.click(node.left + 6, node.top - bar * 0.5F, std::ref(scene));
        ui.wait(1.0);
        const float separator = scene.probes[0].rectangle.right;
        drag(ui, {.x = separator + 1, .y = 300}, {.x = separator + 41, .y = 300}, 4, std::ref(scene));
        const UiRectangle free = scene.probes[4].rectangle;
        drag(ui, {.x = free.left + 30, .y = free.top + 8}, {.x = free.left + 80, .y = free.top + 170}, 4, std::ref(scene));
        ASSERT_FALSE(scene.probes[4].docked) << "dropped away from the tab bars and the squares";
        const UiRectangle first = scene.probes[5].rectangle;
        const UiRectangle second = scene.probes[6].rectangle;
        drag(ui, {.x = second.left + 30, .y = second.top + 8}, target(first, 0, 0), 8, std::ref(scene));
        ui.frames(2, std::ref(scene));
        ASSERT_TRUE(scene.probes[1].begun);
        ASSERT_TRUE(scene.probes[6].docked);
        saved = ui.context().save_layout();
    }

    // Loaded into another context (no builder): the same places.
    UiHarness ui;
    const std::expected<void, std::string> loaded = ui.context().load_layout(saved);
    ASSERT_TRUE(loaded.has_value()) << loaded.error() << "\n" << saved;
    DockingScene scene{.names = {"Outliner", "Scene", "Assets", "Console", "Free", "First", "Second"}};
    ui.frames(3, std::ref(scene));
    const float separator = STYLE.docking_separator_size;
    EXPECT_TRUE(scene.probes[0].docked);
    EXPECT_NEAR(scene.probes[0].rectangle.right, std::round((800.0F - separator) * 0.25F) + 40, 1.0F) << saved;
    EXPECT_TRUE(scene.probes[1].begun) << "Scene selected";
    EXPECT_FALSE(scene.probes[2].begun);
    EXPECT_EQ(scene.probes[1].node, scene.probes[2].node);
    EXPECT_TRUE(scene.probes[3].docked);
    EXPECT_FALSE(scene.probes[4].docked);
    EXPECT_FLOAT_EQ(scene.probes[4].rectangle.left, 60.0F + 40.0F * 4 + 50);
    EXPECT_FLOAT_EQ(scene.probes[4].rectangle.top, 60.0F + 30.0F * 4 + 162);
    EXPECT_TRUE(scene.probes[5].docked);
    EXPECT_EQ(scene.probes[5].node, scene.probes[6].node);
    EXPECT_TRUE(scene.probes[6].begun);

    // Saved again: the same layout.
    UiHarness again;
    ASSERT_TRUE(again.context().load_layout(ui.context().save_layout()).has_value());
}

TEST(UiLayoutTest, TablesColumnsRoundTrip)
{
    // A table resized, with a column hidden and sorted by its second column.
    std::array<float, 3> widths{};
    std::array<TableColumnStatus, 3> status{};
    std::vector<TableColumnSortSpecification> sort{};
    bool hide = false;
    UiRectangle header{};
    const auto body = [&](UiFrame& frame)
    {
        frame.set_next_window_position({.x = 20, .y = 20}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size({.x = 420, .y = 300}, UiCondition::FIRST_USE_EVER);
        if (frame.begin_window("Files"))
        {
            header.top = frame.cursor_screen_position().y;
            if (frame.begin_table("files", 3, TableFlag::RESIZABLE | TableFlag::HIDEABLE | TableFlag::SORTABLE | TableFlag::SIZING_FIXED_FIT))
            {
                frame.table_setup_column("Name", TableColumnFlag::NONE, 80.0F);
                frame.table_setup_column("Size", TableColumnFlag::NONE, 60.0F);
                frame.table_setup_column("Kind", TableColumnFlag::NONE, 60.0F);
                if (hide)
                {
                    frame.table_set_column_enabled(2, false);
                }
                frame.table_headers_row();
                const TableSortSpecifications specifications = frame.table_sort_specifications();
                sort.assign(specifications.columns.begin(), specifications.columns.end());
                frame.table_next_row();
                for (std::uint32_t column = 0; column < 3; ++column)
                {
                    status[column] = frame.table_column_status(static_cast<std::int32_t>(column));
                    widths[column] = frame.table_set_column_index(column) ? frame.content_region_available().x : 0.0F;
                    if (column == 1)
                    {
                        header.left = frame.cursor_screen_position().x;
                    }
                }
                frame.end_table();
            }
        }
        frame.end_window();
    };
    std::string saved{};
    {
        UiHarness ui;
        ui.frames(3, body);
        const float border = header.left - STYLE.cell_padding.x;
        drag(ui, {.x = border, .y = header.top + 8}, {.x = border + 30, .y = header.top + 8}, 3, body);
        ui.click(header.left + 50, header.top + 4, body);
        ui.wait(1.0);
        hide = true;
        ui.frames(2, body);
        hide = false;
        ui.frames(2, body);
        ASSERT_FLOAT_EQ(widths[0], 110.0F);
        ASSERT_EQ(sort.size(), 1U);
        ASSERT_EQ(sort[0].column, 1U);
        ASSERT_FALSE(has_any(status[2], TableColumnStatus::ENABLED));
        saved = ui.context().save_layout();
    }
    UiHarness ui;
    ASSERT_TRUE(ui.context().load_layout(saved).has_value()) << saved;
    ui.frames(3, body);
    EXPECT_FLOAT_EQ(widths[0], 110.0F) << saved;
    EXPECT_FLOAT_EQ(widths[1], 60.0F);
    EXPECT_FALSE(has_any(status[2], TableColumnStatus::ENABLED));
    ASSERT_EQ(sort.size(), 1U);
    EXPECT_EQ(sort[0].column, 1U);
    EXPECT_EQ(sort[0].direction, SortDirection::ASCENDING);
}

TEST(UiLayoutTest, LayoutsThatAreNotLayoutsAreRefusedWithThePathOfEachProblem)
{
    UiHarness ui;
    build_editor(ui.context());
    DockingScene scene{.names = {"Outliner", "Scene", "Assets", "Console"}};
    ui.frames(3, std::ref(scene));
    const std::string before = ui.context().save_layout();

    const auto refused = [&](std::string_view json, std::string_view expected)
    {
        const std::expected<void, std::string> result = ui.context().load_layout(json);
        ASSERT_FALSE(result.has_value()) << json;
        EXPECT_NE(result.error().find(expected), std::string::npos) << result.error();
    };
    refused("{\"format\": ", "malformed JSON");
    refused(R"({"format": "other", "version": 1})", "$.format");
    refused(R"({"format": "gfx.ui.layout", "version": 2})", "$.version");
    refused(R"({"format": "gfx.ui.layout", "version": 1, "extra": 0})", "unknown field \"extra\"");
    refused(R"({"format": "gfx.ui.layout", "version": 1, "windows": [{"id": "0x1", "position": [0, 0]}]})", "$.windows[0]");
    refused(R"({"format": "gfx.ui.layout", "version": 1, "windows": [{"id": "12", "position": [0, 0], "size": [1, 1]}]})", "$.windows[0].id");
    refused(R"({"format": "gfx.ui.layout", "version": 1, "dock_trees": [{"dock_space": "0x2", "root": {"split": "LEFT_RIGHT", "ratio": 1.5, "children": [{"windows": []}, {"windows": []}]}}]})",
            "$.dock_trees[0].root.ratio");
    refused(R"({"format": "gfx.ui.layout", "version": 1, "dock_trees": [{"dock_space": "0x2", "root": {"windows": ["0x3", "0x3"]}}]})",
            "$.dock_trees[0].root.windows[1]");
    refused(R"({"format": "gfx.ui.layout", "version": 1, "tables": [{"id": "0x4", "columns": [{"width": 1, "weight": 1, "order": 1, "visible": true, "sort_order": -1, "sort_direction": "NONE"}]}]})",
            "$.tables[0].columns[0].order");

    // Nothing changed (every part or nothing); a layout is loaded outside frames only.
    EXPECT_EQ(ui.context().save_layout(), before);
    ui.frame([&](UiFrame&) { EXPECT_THROW(static_cast<void>(ui.context().load_layout(before)), gfx::InvalidUsageError); });
}

} // namespace
