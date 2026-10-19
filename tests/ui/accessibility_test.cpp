// The accessibility tree of UiContext driven by synthetic input: built only while enabled; windows, containers
// (menu bars, tree nodes, tab bars, tables, list boxes, combos' lists, child regions) and items as nodes with their
// roles, names, values, numbers, states, bounds and actions; what the application overrides; what changed since the
// tree before (and only that); the focus; announcements as live nodes; the requests of assistive technologies
// reaching their items; one node per identifier.
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
using gfx::tests::center;
using gfx::tests::UiHarness;

/// Runs @p body inside a focused window "Panel" at (20, 20), 500 by 500.
template<class Body>
auto
in_window(Body&& body,
          UiWindowFlag flags = UiWindowFlag::NONE)
{
    return [body = std::forward<Body>(body), flags, first = true](UiFrame& frame) mutable
    {
        frame.set_next_window_position({.x = 20, .y = 20}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size({.x = 500, .y = 500}, UiCondition::FIRST_USE_EVER);
        if (std::exchange(first, false))
        {
            frame.set_next_window_focus();
        }
        if (frame.begin_window("Panel", flags))
        {
            body(frame);
        }
        frame.end_window();
    };
}

/// A UiHarness whose context builds the accessibility tree.
auto
accessible_harness() -> std::unique_ptr<UiHarness>
{
    auto ui = std::make_unique<UiHarness>();
    ui->context().set_accessibility_enabled(true);
    return ui;
}

/// The first node of @p role named @p name, or nullptr.
auto
find_node(const UiAccessibilityTree& tree,
          AccessibilityRole role,
          std::string_view name) -> const UiAccessibilityNode*
{
    const auto found = std::ranges::find_if(tree.nodes, [&](const UiAccessibilityNode& node) { return node.role == role && node.name == name; });
    return found != tree.nodes.end() ? &*found : nullptr;
}

/// The names of @p node's children, in order.
auto
child_names(const UiAccessibilityTree& tree,
            const UiAccessibilityNode& node) -> std::vector<std::string>
{
    std::vector<std::string> names;
    for (const AccessibilityNodeId child : node.children)
    {
        const UiAccessibilityNode* found = tree.find(child);
        names.emplace_back(found != nullptr ? found->name : std::string_view{"?"});
    }
    return names;
}

// ===================================================================================================== nodes

TEST(UiAccessibilityTest, TheTreeIsBuiltOnlyWhileEnabled)
{
    UiHarness ui;
    auto body = in_window([](UiFrame& frame) { static_cast<void>(frame.button("Save")); });
    ui.frames(2, body);
    EXPECT_TRUE(ui.context().accessibility_tree().nodes.empty());
    EXPECT_FALSE(ui.context().accessibility_enabled());
    ui.context().set_accessibility_enabled(true);
    ui.frame(body);
    const UiAccessibilityTree tree = ui.context().accessibility_tree();
    ASSERT_FALSE(tree.nodes.empty());
    EXPECT_EQ(tree.nodes[0].id, ACCESSIBILITY_ROOT_NODE);
    EXPECT_EQ(tree.previous_frame, 0U);
    EXPECT_EQ(tree.changed.size(), tree.nodes.size()) << "the first tree: every node is new";
    ui.context().set_accessibility_enabled(false);
    ui.frame(body);
    EXPECT_TRUE(ui.context().accessibility_tree().nodes.empty());
}

TEST(UiAccessibilityTest, ItemsAreNodesWithTheirRolesNamesValuesAndStates)
{
    auto ui = accessible_harness();
    bool checked = true;
    std::int32_t volume = 7;
    std::string name = "gfx";
    std::string password = "secret";
    UiRectangle save{};
    auto body = in_window([&](UiFrame& frame)
    {
        static_cast<void>(frame.button("Save##primary"));
        save = frame.item_rectangle();
        static_cast<void>(frame.checkbox("Checked", checked));
        frame.slider("Volume", volume, 0, 10);
        static_cast<void>(frame.input_text("Name", name));
        static_cast<void>(frame.input_text("Password", password, {.flags = InputTextFlag::PASSWORD}));
        frame.text_unformatted("Plain text");
        frame.progress_bar(0.25F, {}, "25%");
        frame.begin_disabled();
        static_cast<void>(frame.button("Disabled"));
        frame.end_disabled();
    });
    ui->frames(2, body);
    const UiAccessibilityTree tree = ui->context().accessibility_tree();

    // The window under the root, its items under it in order.
    const UiAccessibilityNode* window = find_node(tree, AccessibilityRole::WINDOW, "Panel");
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->parent, ACCESSIBILITY_ROOT_NODE);
    EXPECT_EQ(child_names(tree, *window),
              (std::vector<std::string>{"Save", "Checked", "Volume", "Name", "Password", "Plain text", "25%", "Disabled"}));

    const UiAccessibilityNode* button = find_node(tree, AccessibilityRole::BUTTON, "Save");
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->bounds, save);
    EXPECT_EQ(button->states, AccessibilityState::FOCUSABLE);
    EXPECT_EQ(button->actions, AccessibilityAction::FOCUS | AccessibilityAction::CLICK | AccessibilityAction::SCROLL_INTO_VIEW);
    EXPECT_EQ(button->parent, window->id);

    const UiAccessibilityNode* box = find_node(tree, AccessibilityRole::CHECK_BOX, "Checked");
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(has_all(box->states, AccessibilityState::CHECKABLE | AccessibilityState::CHECKED));

    const UiAccessibilityNode* slider = find_node(tree, AccessibilityRole::SLIDER, "Volume");
    ASSERT_NE(slider, nullptr);
    EXPECT_EQ(slider->value, "7");
    EXPECT_TRUE(slider->has_number);
    EXPECT_EQ(slider->number, 7.0);
    EXPECT_EQ(slider->minimum, 0.0);
    EXPECT_EQ(slider->maximum, 10.0);
    EXPECT_TRUE(has_all(slider->actions, AccessibilityAction::INCREMENT | AccessibilityAction::DECREMENT | AccessibilityAction::SET_VALUE));

    const UiAccessibilityNode* field = find_node(tree, AccessibilityRole::TEXT_FIELD, "Name");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->value, "gfx");
    const UiAccessibilityNode* secret = find_node(tree, AccessibilityRole::TEXT_FIELD, "Password");
    ASSERT_NE(secret, nullptr);
    EXPECT_TRUE(has_any(secret->states, AccessibilityState::PROTECTED));
    EXPECT_TRUE(secret->value.empty()) << "a password is not given";

    EXPECT_NE(find_node(tree, AccessibilityRole::STATIC_TEXT, "Plain text"), nullptr);
    const UiAccessibilityNode* progress = find_node(tree, AccessibilityRole::PROGRESS_BAR, "25%");
    ASSERT_NE(progress, nullptr);
    EXPECT_EQ(progress->number, 0.25);
    EXPECT_EQ(progress->maximum, 1.0);

    const UiAccessibilityNode* disabled = find_node(tree, AccessibilityRole::BUTTON, "Disabled");
    ASSERT_NE(disabled, nullptr);
    EXPECT_TRUE(has_any(disabled->states, AccessibilityState::DISABLED));
    EXPECT_EQ(disabled->actions, AccessibilityAction::NONE);
}

TEST(UiAccessibilityTest, ContainersHoldTheirItems)
{
    auto ui = accessible_harness();
    std::int32_t choice = 0;
    auto body = in_window([&](UiFrame& frame)
    {
        if (frame.begin_menu_bar())
        {
            if (frame.begin_menu("File"))
            {
                frame.end_menu();
            }
            frame.end_menu_bar();
        }
        frame.set_next_item_open(true, UiCondition::ONCE);
        if (frame.tree_node("Folder"))
        {
            frame.text_unformatted("Leaf");
            frame.tree_pop();
        }
        if (frame.begin_tab_bar("tabs"))
        {
            if (frame.begin_tab_item("First"))
            {
                static_cast<void>(frame.button("In the first tab"));
                frame.end_tab_item();
            }
            if (frame.begin_tab_item("Second"))
            {
                frame.end_tab_item();
            }
            frame.end_tab_bar();
        }
        if (frame.begin_table("table", 2))
        {
            frame.table_setup_column("Name");
            frame.table_setup_column("Size");
            frame.table_headers_row();
            frame.table_next_row();
            frame.table_next_column();
            frame.text_unformatted("cell");
            frame.end_table();
        }
        if (frame.begin_list_box("Choices", {.x = 200, .y = 80}))
        {
            choice = frame.selectable("One", choice == 0) ? 0 : choice;
            choice = frame.selectable("Two", choice == 1) ? 1 : choice;
            frame.end_list_box();
        }
    }, UiWindowFlag::MENU_BAR);
    ui->frames(3, body);
    const UiAccessibilityTree tree = ui->context().accessibility_tree();
    const auto parent_of = [&tree](const UiAccessibilityNode* node) { return node != nullptr ? tree.find(node->parent) : nullptr; };

    const UiAccessibilityNode* file = find_node(tree, AccessibilityRole::MENU_ITEM, "File");
    ASSERT_NE(file, nullptr);
    ASSERT_NE(parent_of(file), nullptr);
    EXPECT_EQ(parent_of(file)->role, AccessibilityRole::MENU_BAR);
    EXPECT_TRUE(has_any(file->states, AccessibilityState::EXPANDABLE));

    const UiAccessibilityNode* folder = find_node(tree, AccessibilityRole::TREE_ITEM, "Folder");
    ASSERT_NE(folder, nullptr);
    EXPECT_TRUE(has_all(folder->states, AccessibilityState::EXPANDABLE | AccessibilityState::EXPANDED));
    EXPECT_EQ(child_names(tree, *folder), std::vector<std::string>{"Leaf"});

    const UiAccessibilityNode* first = find_node(tree, AccessibilityRole::TAB, "First");
    const UiAccessibilityNode* second = find_node(tree, AccessibilityRole::TAB, "Second");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_TRUE(has_all(first->states, AccessibilityState::SELECTABLE | AccessibilityState::SELECTED));
    EXPECT_FALSE(has_any(second->states, AccessibilityState::SELECTED));
    EXPECT_EQ(parent_of(first)->role, AccessibilityRole::TAB_LIST);
    const UiAccessibilityNode* panel = find_node(tree, AccessibilityRole::TAB_PANEL, "First");
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(child_names(tree, *panel), std::vector<std::string>{"In the first tab"});

    const UiAccessibilityNode* header = find_node(tree, AccessibilityRole::COLUMN_HEADER, "Size");
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(parent_of(header)->role, AccessibilityRole::TABLE);
    const UiAccessibilityNode* cell = find_node(tree, AccessibilityRole::STATIC_TEXT, "cell");
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(parent_of(cell)->role, AccessibilityRole::TABLE);

    const UiAccessibilityNode* list = find_node(tree, AccessibilityRole::LIST, "Choices");
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(child_names(tree, *list), (std::vector<std::string>{"One", "Two"}));
    const UiAccessibilityNode* one = find_node(tree, AccessibilityRole::LIST_ITEM, "One");
    ASSERT_NE(one, nullptr);
    EXPECT_TRUE(has_all(one->states, AccessibilityState::SELECTABLE | AccessibilityState::SELECTED));
}

TEST(UiAccessibilityTest, TheApplicationNamesDescribesAndRolesItems)
{
    auto ui = accessible_harness();
    std::string choice = "Alpha";
    auto body = in_window([&](UiFrame& frame)
    {
        frame.set_next_item_accessible_label("Save the document");
        frame.set_next_item_accessible_description("Writes it to disk");
        static_cast<void>(frame.button("S"));
        frame.set_next_item_accessibility_role(AccessibilityRole::LINK);
        static_cast<void>(frame.button("Website"));
        static_cast<void>(frame.button("Plain"));
        frame.set_next_item_shortcut({.key = KeyCode::Q, .modifiers = KeyModifier::CONTROL});
        static_cast<void>(frame.button("Quit"));
        if (frame.begin_combo("Letter", choice))
        {
            static_cast<void>(frame.selectable("Alpha"));
            frame.end_combo();
        }
    });
    ui->frames(2, body);
    const UiAccessibilityTree tree = ui->context().accessibility_tree();
    const UiAccessibilityNode* save = find_node(tree, AccessibilityRole::BUTTON, "Save the document");
    ASSERT_NE(save, nullptr);
    EXPECT_EQ(save->description, "Writes it to disk");
    EXPECT_NE(find_node(tree, AccessibilityRole::LINK, "Website"), nullptr);
    const UiAccessibilityNode* plain = find_node(tree, AccessibilityRole::BUTTON, "Plain");
    ASSERT_NE(plain, nullptr);
    EXPECT_TRUE(plain->description.empty()) << "what was set applies to the next item only";
    const UiAccessibilityNode* quit = find_node(tree, AccessibilityRole::BUTTON, "Quit");
    ASSERT_NE(quit, nullptr);
    EXPECT_EQ(quit->shortcut, "Ctrl+Q");
    const UiAccessibilityNode* combo = find_node(tree, AccessibilityRole::COMBO_BOX, "Letter");
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->value, "Alpha");
    EXPECT_FALSE(has_any(combo->states, AccessibilityState::EXPANDED));
}

// ===================================================================================================== changes

TEST(UiAccessibilityTest, OnlyWhatChangedIsListedAfterTheFirstTree)
{
    auto ui = accessible_harness();
    bool checked = false;
    bool extra = true;
    UiRectangle box{};
    auto body = in_window([&](UiFrame& frame)
    {
        static_cast<void>(frame.button("Save"));
        static_cast<void>(frame.checkbox("Checked", checked));
        box = frame.item_rectangle();
        if (extra)
        {
            static_cast<void>(frame.button("Extra"));
        }
    });
    ui->frames(3, body);
    UiAccessibilityTree tree = ui->context().accessibility_tree();
    EXPECT_TRUE(tree.changed.empty());
    EXPECT_TRUE(tree.removed.empty());
    EXPECT_EQ(tree.previous_frame + 1, tree.frame);

    // The check box toggled: its node alone.
    ui->move(center(box).x, center(box).y);
    ui->frame(body);
    ui->press();
    ui->frame(body);
    ui->release();
    ui->frame(body);
    tree = ui->context().accessibility_tree();
    ASSERT_EQ(tree.changed.size(), 1U);
    EXPECT_EQ(tree.nodes[tree.changed[0]].name, "Checked");
    EXPECT_TRUE(has_any(tree.nodes[tree.changed[0]].states, AccessibilityState::CHECKED));

    // A button gone: removed, and its window changed (its children).
    const UiAccessibilityNode* gone = find_node(tree, AccessibilityRole::BUTTON, "Extra");
    ASSERT_NE(gone, nullptr);
    const AccessibilityNodeId gone_id = gone->id;
    extra = false;
    ui->frame(body);
    tree = ui->context().accessibility_tree();
    EXPECT_EQ(std::vector(tree.removed.begin(), tree.removed.end()), std::vector{gone_id});
    ASSERT_EQ(tree.changed.size(), 1U);
    EXPECT_EQ(tree.nodes[tree.changed[0]].name, "Panel");
}

TEST(UiAccessibilityTest, TheFocusIsTheFocusedItemsNode)
{
    auto ui = accessible_harness();
    auto body = in_window([](UiFrame& frame)
    {
        static_cast<void>(frame.button("First"));
        static_cast<void>(frame.button("Second"));
    });
    ui->frames(3, body);
    UiAccessibilityTree tree = ui->context().accessibility_tree();
    EXPECT_EQ(tree.focus, find_node(tree, AccessibilityRole::WINDOW, "Panel")->id) << "no item focused: the focused window";
    ui->tap(Key::TAB);
    ui->frame(body);
    ui->frame(body);
    tree = ui->context().accessibility_tree();
    EXPECT_EQ(tree.focus, find_node(tree, AccessibilityRole::BUTTON, "First")->id);
    ui->tap(Key::TAB);
    ui->frame(body);
    tree = ui->context().accessibility_tree();
    EXPECT_TRUE(tree.focus_changed);
    EXPECT_EQ(tree.focus, find_node(tree, AccessibilityRole::BUTTON, "Second")->id);
    ui->frame(body);
    EXPECT_FALSE(ui->context().accessibility_tree().focus_changed);
}

TEST(UiAccessibilityTest, AnnouncementsAreNewLiveNodes)
{
    auto ui = accessible_harness();
    std::string_view message{};
    AccessibilityLiveRegion live = AccessibilityLiveRegion::POLITE;
    auto body = in_window([&](UiFrame& frame)
    {
        if (!message.empty())
        {
            frame.announce(message, live);
            message = {};
        }
    });
    ui->frames(2, body);
    message = "Saved";
    ui->frame(body);
    UiAccessibilityTree tree = ui->context().accessibility_tree();
    const UiAccessibilityNode* saved = find_node(tree, AccessibilityRole::STATIC_TEXT, "Saved");
    ASSERT_NE(saved, nullptr);
    EXPECT_EQ(saved->live_region, AccessibilityLiveRegion::POLITE);
    EXPECT_EQ(saved->parent, ACCESSIBILITY_ROOT_NODE);
    const AccessibilityNodeId first = saved->id;

    // Kept while nothing else is announced; another announcement is another node.
    ui->frame(body);
    EXPECT_NE(find_node(ui->context().accessibility_tree(), AccessibilityRole::STATIC_TEXT, "Saved"), nullptr);
    message = "Failed";
    live = AccessibilityLiveRegion::ASSERTIVE;
    ui->frame(body);
    tree = ui->context().accessibility_tree();
    const UiAccessibilityNode* failed = find_node(tree, AccessibilityRole::STATIC_TEXT, "Failed");
    ASSERT_NE(failed, nullptr);
    EXPECT_EQ(failed->live_region, AccessibilityLiveRegion::ASSERTIVE);
    EXPECT_NE(failed->id, first);
    EXPECT_TRUE(std::ranges::contains(tree.removed, first));
}

TEST(UiAccessibilityTest, OneNodeStandsForItemsOfOneIdentifier)
{
    auto ui = accessible_harness();
    auto body = in_window([](UiFrame& frame)
    {
        static_cast<void>(frame.button("Twice"));
        static_cast<void>(frame.button("Twice"));
    });
    ui->frames(2, body);
    const UiAccessibilityTree tree = ui->context().accessibility_tree();
    EXPECT_EQ(std::ranges::count_if(tree.nodes, [](const UiAccessibilityNode& node) { return node.name == "Twice"; }), 1);
    std::vector<AccessibilityNodeId> identifiers;
    for (const UiAccessibilityNode& node : tree.nodes)
    {
        identifiers.push_back(node.id);
    }
    std::ranges::sort(identifiers);
    EXPECT_EQ(std::ranges::adjacent_find(identifiers), identifiers.end());
}

// ===================================================================================================== requests

TEST(UiAccessibilityTest, RequestsReachTheItemsTheyName)
{
    auto ui = accessible_harness();
    std::uint32_t saved = 0;
    bool second_focused = false;
    std::string name = "old";
    std::int32_t volume = 5;
    bool folder_open = false;
    bool menu_open = false;
    auto body = in_window([&](UiFrame& frame)
    {
        saved += frame.button("Save") ? 1U : 0U;
        static_cast<void>(frame.button("Second"));
        second_focused = frame.is_item_focused();
        static_cast<void>(frame.input_text("Name", name));
        frame.slider("Volume", volume, 0, 10);
        folder_open = frame.tree_node("Folder");
        if (folder_open)
        {
            frame.tree_pop();
        }
        static_cast<void>(frame.button("Menu owner"));
        menu_open = frame.begin_popup_context_item();
        if (menu_open)
        {
            static_cast<void>(frame.menu_item("Inside"));
            frame.end_popup();
        }
    });
    ui->frames(3, body);
    const auto id_of = [&ui](AccessibilityRole role, std::string_view label)
    {
        const UiAccessibilityNode* node = find_node(ui->context().accessibility_tree(), role, label);
        return node != nullptr ? node->id : AccessibilityNodeId{0};
    };
    const AccessibilityNodeId save = id_of(AccessibilityRole::BUTTON, "Save");
    const AccessibilityNodeId second = id_of(AccessibilityRole::BUTTON, "Second");
    const AccessibilityNodeId field = id_of(AccessibilityRole::TEXT_FIELD, "Name");
    const AccessibilityNodeId slider = id_of(AccessibilityRole::SLIDER, "Volume");
    const AccessibilityNodeId folder = id_of(AccessibilityRole::TREE_ITEM, "Folder");
    const AccessibilityNodeId owner = id_of(AccessibilityRole::BUTTON, "Menu owner");

    ui->request(AccessibilityAction::CLICK, save);
    ui->frame(body);
    EXPECT_EQ(saved, 1U);
    ui->request(AccessibilityAction::FOCUS, save);
    ui->request(AccessibilityAction::CLICK, save);
    ui->frame(body);
    EXPECT_EQ(saved, 2U) << "several requests of an item in one frame";
    ui->request(AccessibilityAction::FOCUS, second);
    ui->frame(body);
    EXPECT_TRUE(second_focused);
    ui->request(AccessibilityAction::SET_VALUE, field, "new");
    ui->frame(body);
    EXPECT_EQ(name, "new");
    ui->request(AccessibilityAction::SET_VALUE, slider, {}, 9.0);
    ui->frame(body);
    EXPECT_EQ(volume, 9);
    ui->request(AccessibilityAction::DECREMENT, slider);
    ui->frame(body);
    EXPECT_EQ(volume, 8);
    ui->request(AccessibilityAction::EXPAND, folder);
    ui->frame(body);
    EXPECT_TRUE(folder_open);
    ui->request(AccessibilityAction::COLLAPSE, folder);
    ui->frame(body);
    EXPECT_FALSE(folder_open);
    ui->request(AccessibilityAction::SHOW_CONTEXT_MENU, owner);
    ui->frames(2, body);
    EXPECT_TRUE(menu_open);
}

} // namespace
