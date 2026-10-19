/// @file accessibility.cpp
/// @brief The accessibility tree: while enabled, each frame's windows, containers and items become nodes (roles,
///        names, values, states, bounds, actions), finished at end_frame() and compared with the tree before so that
///        providers send only the changes; announcements are live nodes; the requests of assistive technologies
///        (focus, click, set a value...) reach the items they name.
module gfx.ui.context;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.handles;
import gfx.ui.draw;
import gfx.ui.input;
import :state;

namespace gfx::ui
{

namespace
{

/// No record.
constexpr std::uint32_t NO_RECORD = AccessibilityBuildState::NONE;

/// The first identifier of announcement nodes (identifiers below 2^32 are the root's and theirs: windows' and items'
/// have their window in the high bits).
constexpr AccessibilityNodeId FIRST_ANNOUNCEMENT = 2;

/// The internal windows that are no node (their content goes to the node they are in): the hosts of floating dock
/// trees, dock spaces over the display, the main menu bar's window (its bar is a node).
auto
hidden_host(const WindowState& window) noexcept -> bool
{
    return window.dock_tree != DockNodeState::NONE || window.name.starts_with("##dock_space_") || window.name == "##main_menu_bar";
}

/// The slot where @p id starts its probe in a table of @p capacity (a power of two) slots.
constexpr auto
slot_of(AccessibilityNodeId id,
        std::size_t capacity) noexcept -> std::size_t
{
    return static_cast<std::size_t>((id * 0x9E3779B97F4A7C15ULL) >> 32) & (capacity - 1);
}

/// Whether @p first and @p second say the same (texts and children compared by content).
auto
same_node(const UiAccessibilityNode& first,
          const UiAccessibilityNode& second) noexcept -> bool
{
    return first.id == second.id && first.parent == second.parent && first.role == second.role && first.states == second.states
           && first.actions == second.actions && first.live_region == second.live_region && first.bounds == second.bounds
           && first.name == second.name && first.description == second.description && first.value == second.value
           && first.shortcut == second.shortcut && first.has_number == second.has_number && first.number == second.number
           && first.minimum == second.minimum && first.maximum == second.maximum && first.step == second.step
           && std::ranges::equal(first.children, second.children);
}

} // namespace

auto
AccessibilityTreeData::find(AccessibilityNodeId id) const noexcept -> std::optional<std::uint32_t>
{
    if (keys.empty() || id == 0)
    {
        return std::nullopt;
    }
    const std::size_t mask = keys.size() - 1;
    for (std::size_t slot = slot_of(id, keys.size());; slot = (slot + 1) & mask)
    {
        if (keys[slot] == id)
        {
            return indices[slot];
        }
        if (keys[slot] == 0)
        {
            return std::nullopt;
        }
    }
}

// ===================================================================================================== building

void
ContextState::begin_accessibility(const UiFrameInput& input) noexcept
{
    AccessibilityBuildState& state = accessibility;

    // The requests of the frame, their texts copied (the input's views live only until begin_frame() returns); a
    // FOCUS request focuses its item (or its window) at once.
    state.requests.clear();
    state.request_text.clear();
    try
    {
        std::size_t text_size = 0;
        for (const UiAccessibilityRequest& request : input.accessibility_requests)
        {
            text_size += request.text.size();
        }
        allocations += state.request_text.capacity() < text_size || state.requests.capacity() < input.accessibility_requests.size() ? 1U : 0U;
        state.request_text.reserve(text_size);
        for (const UiAccessibilityRequest& request : input.accessibility_requests)
        {
            UiAccessibilityRequest copy = request;
            const std::size_t offset = state.request_text.size();
            state.request_text.append(request.text);
            copy.text = std::string_view{state.request_text}.substr(offset, request.text.size());
            state.requests.push_back(copy);
        }
    }
    catch (...)
    {
        report(UiFrameProblem::CAPACITY_EXCEEDED, "begin_frame", "the requests of assistive technologies could not be copied: some are dropped");
    }
    for (const UiAccessibilityRequest& request : state.requests)
    {
        if (request.action != AccessibilityAction::FOCUS)
        {
            continue;
        }
        const std::uint32_t window = find_window(static_cast<std::uint32_t>(request.target >> 32));
        if (window != NO_WINDOW)
        {
            focus_window(windows[window].root);
            focus_id = static_cast<std::uint32_t>(request.target & 0xFFFFFFFFU);
            focus_visible = focus_id != 0;
        }
    }

    // The tree of the frame starts with its root: the whole display.
    state.building = state.enabled;
    state.records.clear();
    state.text.clear();
    state.containers.clear();
    state.text_counter = 0;
    state.last_record = NO_RECORD;
    state.last_window = NO_WINDOW;
    state.next_window_role = AccessibilityRole::GENERIC;
    if (!state.building)
    {
        return;
    }
    state.records.push_back(AccessibilityRecord{
        .node = {.id = ACCESSIBILITY_ROOT_NODE, .role = AccessibilityRole::WINDOW, .bounds = {.right = display_size.x, .bottom = display_size.y}},
        .parent = 0});
    state.containers.push_back(0);
}

auto
ContextState::accessible_node(AccessibilityNodeId id,
                              AccessibilityRole role,
                              const UiRectangle& bounds,
                              std::string_view name) noexcept -> std::uint32_t
{
    AccessibilityBuildState& state = accessibility;
    if (!state.building || state.containers.empty())
    {
        return NO_RECORD;
    }
    try
    {
        const std::uint32_t parent = state.containers.back();
        allocations += state.records.size() == state.records.capacity() ? 1U : 0U;
        state.records.push_back(AccessibilityRecord{.node = {.id = id, .parent = state.records[parent].node.id, .role = role, .bounds = bounds},
                                                    .parent = parent});
    }
    catch (...)
    {
        report(UiFrameProblem::CAPACITY_EXCEEDED, "accessibility", "the accessibility tree could not grow: a node is missing");
        return NO_RECORD;
    }
    const auto record = static_cast<std::uint32_t>(state.records.size() - 1);
    accessible_text(record, 0, name);
    return record;
}

void
ContextState::accessible_text(std::uint32_t record,
                              std::uint32_t field,
                              std::string_view content) noexcept
{
    AccessibilityBuildState& state = accessibility;
    if (record >= state.records.size() || field > 3 || content.empty())
    {
        return;
    }
    try
    {
        allocations += state.text.size() + content.size() > state.text.capacity() ? 1U : 0U;
        const auto offset = static_cast<std::uint32_t>(state.text.size());
        state.text.append(content);
        state.records[record].text[field * 2] = offset;
        state.records[record].text[field * 2 + 1] = static_cast<std::uint32_t>(content.size());
    }
    catch (...)
    {
        report(UiFrameProblem::CAPACITY_EXCEEDED, "accessibility", "the accessibility texts could not grow: a text is missing");
    }
}

void
ContextState::accessible_push(std::uint32_t record) noexcept
{
    AccessibilityBuildState& state = accessibility;
    if (!state.building || state.containers.empty())
    {
        return;
    }
    try
    {
        allocations += state.containers.size() == state.containers.capacity() ? 1U : 0U;
        state.containers.push_back(record < state.records.size() ? record : state.containers.back());
    }
    catch (...)
    {
        report(UiFrameProblem::CAPACITY_EXCEEDED, "accessibility", "the accessibility containers could not grow");
    }
}

void
ContextState::accessible_pop() noexcept
{
    AccessibilityBuildState& state = accessibility;
    if (state.containers.size() > 1)
    {
        state.containers.pop_back();
    }
}

void
ContextState::accessible_begin_window(WindowState& window) noexcept
{
    AccessibilityBuildState& state = accessibility;
    if (!state.building || state.containers.empty())
    {
        return;
    }
    window.accessibility_depth = static_cast<std::uint32_t>(state.containers.size());

    // A window begun again this frame continues its node.
    if (window.accessibility_frame == frame && window.accessibility_record < state.records.size())
    {
        accessible_push(window.accessibility_record);
        return;
    }

    // Its node, under the root (child regions: under where they are), with the role of what it is; internal hosts and
    // windows not shown yet are none (what they hold goes where they are).
    const AccessibilityRole role = std::exchange(state.next_window_role, AccessibilityRole::GENERIC);
    std::uint32_t record = NO_RECORD;
    if (!hidden_host(window) && !window.hidden)
    {
        const std::size_t level = popup_level_of(static_cast<std::uint32_t>(&window - windows.data()));
        const bool modal = window.popup && level < popups.size() && popups[level].modal;
        const bool menu = window.popup && level < popups.size() && popups[level].menu;
        const AccessibilityRole kind = window.tooltip                     ? AccessibilityRole::TOOLTIP
                                       : modal                            ? AccessibilityRole::DIALOG
                                       : menu                             ? AccessibilityRole::MENU
                                       : role != AccessibilityRole::GENERIC ? role
                                       : window.child                     ? AccessibilityRole::GROUP
                                                                          : AccessibilityRole::WINDOW;
        if (!window.child)
        {
            state.containers.push_back(0);
        }
        record = accessible_node(static_cast<AccessibilityNodeId>(window.id) << 32, kind, window.outer, display_text(window.name));
        if (!window.child)
        {
            state.containers.pop_back();
        }
        if (record != NO_RECORD && modal)
        {
            state.records[record].node.states = AccessibilityState::MODAL;
        }
    }
    window.accessibility_record = record;
    window.accessibility_frame = frame;
    accessible_push(record);
}

void
ContextState::accessible_end_window(const WindowState& window) noexcept
{
    AccessibilityBuildState& state = accessibility;
    if (state.building && window.accessibility_depth > 0 && window.accessibility_depth <= state.containers.size())
    {
        state.containers.resize(window.accessibility_depth);
    }
}

auto
ContextState::describe_item(AccessibilityRole role,
                            AccessibilityState states,
                            AccessibilityAction actions,
                            std::string_view value) noexcept -> std::uint32_t
{
    AccessibilityBuildState& state = accessibility;
    if (!state.building || state.last_window >= windows.size() || windows[state.last_window].hidden)
    {
        return NO_RECORD;
    }
    if (state.last_record != NO_RECORD)
    {
        return state.last_record;
    }

    // Its node: its window's identifier and its own (an item without one: the window's count of such items).
    const WindowState& window = windows[state.last_window];
    const std::uint32_t low = state.last_item != 0 ? state.last_item : hash(window.id ^ 0x5F3759DFU, std::string_view{reinterpret_cast<const char*>(&state.text_counter), sizeof(std::uint32_t)});
    state.text_counter += state.last_item != 0 ? 0U : 1U;
    const std::string_view name = state.pending_label ? std::string_view{next_item.label} : display_text(state.last_label);
    const std::uint32_t record = accessible_node((static_cast<AccessibilityNodeId>(window.id) << 32) | low, state.pending_role ? next_item.role : role,
                                                 state.last_rectangle, name);
    if (record == NO_RECORD)
    {
        return NO_RECORD;
    }
    UiAccessibilityNode& node = state.records[record].node;
    node.states = states | (last_item.disabled || disabled ? AccessibilityState::DISABLED : AccessibilityState::NONE)
                  | (state.last_focusable ? AccessibilityState::FOCUSABLE : AccessibilityState::NONE);
    node.actions = last_item.disabled || disabled ? AccessibilityAction::NONE
                                      : actions | (state.last_focusable ? AccessibilityAction::FOCUS : AccessibilityAction::NONE) | AccessibilityAction::SCROLL_INTO_VIEW;
    if (state.pending_description)
    {
        accessible_text(record, 1, next_item.description);
    }
    accessible_text(record, 2, value);
    if (item_shortcut.key != KeyCode::UNKNOWN)
    {
        accessible_text(record, 3, key_chord_text(item_shortcut).view());
    }
    state.last_record = record;
    return record;
}

void
ContextState::describe_text(const WindowState& window,
                            const UiRectangle& rectangle,
                            std::string_view content,
                            AccessibilityRole role) noexcept
{
    AccessibilityBuildState& state = accessibility;
    if (!state.building || window.hidden || content.empty())
    {
        return;
    }
    const std::uint32_t low = hash(window.id ^ 0x5F3759DFU, std::string_view{reinterpret_cast<const char*>(&state.text_counter), sizeof(std::uint32_t)});
    ++state.text_counter;
    static_cast<void>(accessible_node((static_cast<AccessibilityNodeId>(window.id) << 32) | low, role, rectangle, content));
}

void
ContextState::describe_number(std::uint32_t record,
                              double value,
                              double minimum,
                              double maximum,
                              double step) noexcept
{
    if (record >= accessibility.records.size())
    {
        return;
    }
    UiAccessibilityNode& node = accessibility.records[record].node;
    node.has_number = true;
    node.number = value;
    node.minimum = minimum;
    node.maximum = maximum;
    node.step = step;
}

auto
ContextState::item_request(AccessibilityAction action) const noexcept -> const UiAccessibilityRequest*
{
    // A request of the frame naming the last item (several may: a screen reader focuses an item, then clicks it).
    const AccessibilityBuildState& state = accessibility;
    if (state.requests.empty() || state.last_item == 0 || state.last_window >= windows.size())
    {
        return nullptr;
    }
    const AccessibilityNodeId node = (static_cast<AccessibilityNodeId>(windows[state.last_window].id) << 32) | state.last_item;
    const auto found = std::ranges::find_if(state.requests, [node, action](const UiAccessibilityRequest& request)
    {
        return request.target == node && request.action == action;
    });
    return found != state.requests.end() ? &*found : nullptr;
}

// ===================================================================================================== the finished tree

void
ContextState::finish_accessibility() noexcept
{
    AccessibilityBuildState& state = accessibility;
    state.changed.clear();
    state.removed.clear();
    if (!state.building)
    {
        return;
    }

    // The last announcement: a live text under the root, a new node each time.
    if (!state.announcement.empty())
    {
        state.containers.resize(1);
        const std::uint32_t record = accessible_node(FIRST_ANNOUNCEMENT + state.announcement_counter, AccessibilityRole::STATIC_TEXT, {}, state.announcement);
        if (record != NO_RECORD)
        {
            state.records[record].node.live_region = state.announcement_live;
            accessible_text(record, 2, state.announcement);
        }
    }
    state.building = false;

    // The tree before becomes the one compared with; this frame's takes the other buffers.
    state.current ^= 1U;
    AccessibilityTreeData& tree = state.trees[state.current];
    const AccessibilityTreeData& before = state.trees[state.current ^ 1U];
    const std::size_t count = state.records.size();
    try
    {
        // The index by identifier (twice as many slots as nodes, a power of two); a second node of an identifier
        // (an identifier conflict) is left out, its children going to its parent.
        std::size_t capacity = 16;
        while (capacity < count * 2)
        {
            capacity *= 2;
        }
        allocations += tree.keys.capacity() < capacity || tree.nodes.capacity() < count || tree.children.capacity() < count ? 1U : 0U;
        tree.keys.assign(std::max(capacity, tree.keys.size()), 0);
        tree.indices.resize(tree.keys.size());
        tree.nodes.clear();
        tree.children.clear();
        std::swap(tree.text, state.text);
        state.child_counts.assign(count * 2, NO_RECORD);
        std::uint32_t* kept = state.child_counts.data();
        std::uint32_t* parent = state.child_counts.data() + count;
        const std::size_t mask = tree.keys.size() - 1;
        for (std::size_t record = 0; record < count; ++record)
        {
            const AccessibilityRecord& source = state.records[record];
            const std::uint32_t up = source.parent;
            parent[record] = record == 0 ? 0U : kept[up] != NO_RECORD ? up : parent[up];
            std::size_t slot = slot_of(source.node.id, tree.keys.size());
            while (tree.keys[slot] != 0 && tree.keys[slot] != source.node.id)
            {
                slot = (slot + 1) & mask;
            }
            if (tree.keys[slot] == source.node.id || source.node.id == 0)
            {
                continue;
            }
            tree.keys[slot] = source.node.id;
            tree.indices[slot] = static_cast<std::uint32_t>(tree.nodes.size());
            kept[record] = static_cast<std::uint32_t>(tree.nodes.size());
            UiAccessibilityNode node = source.node;
            const auto view_of = [&tree, &source](std::size_t field)
            {
                return std::string_view{tree.text}.substr(source.text[field * 2], source.text[field * 2 + 1]);
            };
            node.name = view_of(0);
            node.description = view_of(1);
            node.value = view_of(2);
            node.shortcut = view_of(3);
            node.parent = record == 0 ? 0 : state.records[parent[record]].node.id;
            tree.nodes.push_back(node);
        }

        // Children, in the order they were made: counted, then placed one node after another.
        std::vector<std::uint32_t>& offsets = state.changed;
        offsets.assign(tree.nodes.size() + 1, 0);
        for (std::size_t record = 1; record < count; ++record)
        {
            if (kept[record] != NO_RECORD)
            {
                ++offsets[kept[parent[record]] + 1];
            }
        }
        for (std::size_t node = 0; node < tree.nodes.size(); ++node)
        {
            offsets[node + 1] += offsets[node];
        }
        tree.children.resize(offsets.back());
        for (std::size_t record = 1; record < count; ++record)
        {
            if (kept[record] != NO_RECORD)
            {
                tree.children[offsets[kept[parent[record]]]++] = tree.nodes[kept[record]].id;
            }
        }
        std::size_t start = 0;
        for (std::size_t node = 0; node < tree.nodes.size(); ++node)
        {
            tree.nodes[node].children = std::span<const AccessibilityNodeId>{tree.children}.subspan(start, offsets[node] - start);
            start = offsets[node];
        }
    }
    catch (...)
    {
        report(UiFrameProblem::CAPACITY_EXCEEDED, "accessibility", "the accessibility tree could not be finished: it is empty this frame");
        tree.nodes.clear();
        tree.children.clear();
        std::ranges::fill(tree.keys, 0);
    }

    // The focus: the focused item's node, else the focused window's, else the root.
    tree.focus = ACCESSIBILITY_ROOT_NODE;
    if (focus_id != 0)
    {
        const auto found = std::ranges::find_if(tree.nodes, [this](const UiAccessibilityNode& node)
        {
            return (node.id >> 32) != 0 && (node.id & 0xFFFFFFFFU) == focus_id;
        });
        tree.focus = found != tree.nodes.end() ? found->id : tree.focus;
    }
    if (tree.focus == ACCESSIBILITY_ROOT_NODE && focused_window != NO_WINDOW)
    {
        const AccessibilityNodeId window = static_cast<AccessibilityNodeId>(windows[focused_window].id) << 32;
        tree.focus = tree.find(window).has_value() ? window : tree.focus;
    }

    // What changed since the tree before (every node when there is none).
    tree.frame = frame;
    state.previous_frame = before.frame;
    state.focus_changed = before.frame == 0 || before.focus != tree.focus;
    state.changed.clear();
    try
    {
        allocations += state.changed.capacity() < tree.nodes.size() ? 1U : 0U;
        for (std::size_t node = 0; node < tree.nodes.size(); ++node)
        {
            const std::optional<std::uint32_t> previous = before.frame != 0 ? before.find(tree.nodes[node].id) : std::nullopt;
            if (!previous.has_value() || !same_node(tree.nodes[node], before.nodes[*previous]))
            {
                state.changed.push_back(static_cast<std::uint32_t>(node));
            }
        }
        if (before.frame != 0)
        {
            for (const UiAccessibilityNode& node : before.nodes)
            {
                if (!tree.find(node.id).has_value())
                {
                    allocations += state.removed.size() == state.removed.capacity() ? 1U : 0U;
                    state.removed.push_back(node.id);
                }
            }
        }
    }
    catch (...)
    {
        report(UiFrameProblem::CAPACITY_EXCEEDED, "accessibility", "the changes of the accessibility tree could not be listed");
    }
}

// ===================================================================================================== UiFrame and UiContext

void
UiFrame::set_next_item_accessible_label(std::string_view label) noexcept
{
    ContextState& state = *m_state;
    try
    {
        state.next_item.label.assign(label);
        state.next_item.has_label = true;
    }
    catch (...)
    {
        state.report(UiFrameProblem::CAPACITY_EXCEEDED, "set_next_item_accessible_label", "the label could not be kept");
    }
}

void
UiFrame::set_next_item_accessible_description(std::string_view description) noexcept
{
    ContextState& state = *m_state;
    try
    {
        state.next_item.description.assign(description);
        state.next_item.has_description = true;
    }
    catch (...)
    {
        state.report(UiFrameProblem::CAPACITY_EXCEEDED, "set_next_item_accessible_description", "the description could not be kept");
    }
}

void
UiFrame::set_next_item_accessibility_role(AccessibilityRole role) noexcept
{
    ContextState& state = *m_state;
    state.next_item.role = role;
    state.next_item.has_role = true;
}

void
UiFrame::announce(std::string_view text,
                  AccessibilityLiveRegion live_region) noexcept
{
    // Kept while accessibility is enabled: the next tree has it as a new live node.
    ContextState& state = *m_state;
    if (!state.accessibility.enabled || text.empty())
    {
        return;
    }
    try
    {
        state.accessibility.announcement.assign(text);
        state.accessibility.announcement_live = live_region;
        state.accessibility.announcement_counter = (state.accessibility.announcement_counter + 1) & 0x7FFFFFFFU;
    }
    catch (...)
    {
        state.report(UiFrameProblem::CAPACITY_EXCEEDED, "announce", "the announcement could not be kept");
    }
}

void
UiContext::set_accessibility_enabled(bool enabled) noexcept
{
    // Disabled: the trees are forgotten (enabled again, the first tree is new).
    AccessibilityBuildState& state = m_state->accessibility;
    state.enabled = enabled;
    if (enabled)
    {
        // The capacities of the limits, reserved the first time (a context without accessibility reserves nothing).
        const UiContextLimits& limits = m_state->description.limits;
        try
        {
            const std::size_t nodes = limits.accessibility_nodes;
            state.records.reserve(nodes);
            state.text.reserve(limits.accessibility_text_bytes);
            state.containers.reserve(limits.stack_depth * 2);
            state.changed.reserve(nodes + 1);
            state.removed.reserve(nodes);
            state.child_counts.reserve(nodes * 2);
            state.requests.reserve(16);
            state.request_text.reserve(256);
            state.announcement.reserve(256);
            std::size_t slots = 16;
            while (slots < nodes * 2)
            {
                slots *= 2;
            }
            for (AccessibilityTreeData& tree : state.trees)
            {
                tree.nodes.reserve(nodes);
                tree.children.reserve(nodes);
                tree.text.reserve(limits.accessibility_text_bytes);
                tree.keys.reserve(slots);
                tree.indices.reserve(slots);
            }
        }
        catch (...)
        {
            m_state->report(UiFrameProblem::CAPACITY_EXCEEDED, "set_accessibility_enabled", "the accessibility tree's capacities could not be reserved");
        }
    }
    else
    {
        for (AccessibilityTreeData& tree : state.trees)
        {
            tree.nodes.clear();
            tree.children.clear();
            tree.frame = 0;
        }
        state.changed.clear();
        state.removed.clear();
        state.announcement.clear();
    }
}

auto
UiContext::accessibility_enabled() const noexcept -> bool
{
    return m_state->accessibility.enabled;
}

auto
UiContext::accessibility_tree() const noexcept -> UiAccessibilityTree
{
    const AccessibilityBuildState& state = m_state->accessibility;
    const AccessibilityTreeData& tree = state.trees[state.current];
    if (!state.enabled || tree.frame == 0)
    {
        return {};
    }
    return UiAccessibilityTree{.nodes = tree.nodes,
                               .changed = state.changed,
                               .removed = state.removed,
                               .focus = tree.focus,
                               .focus_changed = state.focus_changed,
                               .frame = tree.frame,
                               .previous_frame = state.previous_frame,
                               .pixels_per_unit = m_state->pixels_per_unit};
}

} // namespace gfx::ui
