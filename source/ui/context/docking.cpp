/// @file docking.cpp
/// @brief Docking: dock trees (split nodes and leaves holding docked windows as the tabs of a tab bar), dock spaces (in
///        a window or over the display), floating trees and their host windows, where docked windows go, the
///        separators between nodes, tearing a tab off (undocking), docking a dragged window over a node or another
///        window, the display order of docked windows, UiDockBuilder, and UiFrame's docking calls.
module gfx.ui.context;

import std;
import gfx.core.enums;
import gfx.core.handles;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.errors;
import gfx.ui.handles;
import gfx.ui.draw;
import gfx.ui.style;
import gfx.ui.text;
import :state;

namespace gfx::ui
{

namespace
{

constexpr std::uint32_t NONE = DockNodeState::NONE;

/// The squares a dragged window is offered, in DockDropState::targets.
constexpr std::uint32_t TARGET_CENTER = 0;
constexpr std::uint32_t TARGET_LEFT = 1;
constexpr std::uint32_t TARGET_RIGHT = 2;
constexpr std::uint32_t TARGET_TOP = 3;
constexpr std::uint32_t TARGET_BOTTOM = 4;
constexpr std::uint32_t NO_TARGET = 5;

/// Marks a publication order entry as a window's front list.
constexpr std::uint32_t FRONT_LIST = 0x80000000U;

/// The main menu bar's window (menus.cpp).
constexpr std::string_view MAIN_MENU_BAR_WINDOW = "##main_menu_bar";

/// Issues dock node handles.
struct DockHandles final : HandleIssuer
{
    [[nodiscard]] static constexpr auto key() noexcept -> HandleKey
    {
        return handle_key();
    }
};

/// Whether @p rectangle contains @p point.
constexpr auto
contains(const UiRectangle& rectangle,
         Vector2 point) noexcept -> bool
{
    return point.x >= rectangle.left && point.x < rectangle.right && point.y >= rectangle.top && point.y < rectangle.bottom;
}

/// A short name built in place (no allocation).
struct ShortName
{
    std::array<char, 192> text{};
    std::size_t size = 0;

    [[nodiscard]] auto view() const noexcept -> std::string_view
    {
        return {text.data(), size};
    }
};

/// "@p prefix" followed by @p id in hexadecimal.
auto
name_with_identifier(std::string_view prefix,
                     std::uint32_t id) noexcept -> ShortName
{
    ShortName name{};
    const auto result = std::format_to_n(name.text.data(), static_cast<std::ptrdiff_t>(name.text.size()), "{}{:08X}", prefix, id);
    name.size = std::min(static_cast<std::size_t>(result.size), name.text.size());
    return name;
}

/// The label of the tab of window @p window: its title, then "###" and its identifier (so the tab's identifier
/// depends on the window only).
auto
tab_label(const WindowState& window) noexcept -> ShortName
{
    std::string_view title = window.name;
    if (title.size() > 160)
    {
        // Cut on a character boundary.
        std::size_t size = 160;
        while (size > 0 && (static_cast<unsigned char>(title[size]) & 0xC0U) == 0x80U)
        {
            --size;
        }
        title = title.substr(0, size);
    }
    ShortName label{};
    const auto result = std::format_to_n(label.text.data(), static_cast<std::ptrdiff_t>(label.text.size()), "{}###{:08X}", title, window.id);
    label.size = std::min(static_cast<std::size_t>(result.size), label.text.size());
    return label;
}

/// The identifier of window @p window's tab in tab bar @p bar.
auto
tab_identifier(std::uint32_t bar,
               std::uint32_t window) noexcept -> std::uint32_t
{
    return ContextState::hash(bar, name_with_identifier("###", window).view());
}

/// Whether window @p window shows in its node (submitted this frame or the last).
auto
present(const ContextState& state,
        std::uint32_t window_id) noexcept -> bool
{
    const std::uint32_t index = state.find_window(window_id);
    return index != ContextState::NO_WINDOW && (state.windows[index].active || state.windows[index].was_active);
}

/// Frees node @p node (its slot is reused, its handles go stale).
void
free_node(ContextState& state,
          std::uint32_t node) noexcept
{
    DockNodeState& entry = state.dock_nodes[node];
    entry.alive = false;
    entry.generation = entry.generation % MAXIMUM_HANDLE_GENERATION + 1U;
    entry.windows.clear();
    entry.children = {NONE, NONE};
    entry.parent = NONE;
    try
    {
        state.free_dock_nodes.push_back(node);
    }
    catch (...)
    {
        // The slot is lost; nothing else is.
    }
}

/// Window @p window leaves its node (the node is kept).
void
detach_window(ContextState& state,
              std::uint32_t window) noexcept
{
    WindowState& entry = state.windows[window];
    const std::uint32_t node = entry.dock_node;
    entry.dock_node = NONE;
    entry.docked = false;
    if (node >= state.dock_nodes.size() || !state.dock_nodes[node].alive)
    {
        return;
    }
    DockNodeState& leaf = state.dock_nodes[node];
    const auto found = std::ranges::find(leaf.windows, entry.id);
    if (found == leaf.windows.end())
    {
        return;
    }
    const auto position = static_cast<std::size_t>(found - leaf.windows.begin());
    leaf.windows.erase(found);
    if (leaf.selected == entry.id)
    {
        leaf.selected = leaf.windows.empty() ? 0U : leaf.windows[std::min(position, leaf.windows.size() - 1)];
        leaf.selection_changed = true;
    }
}

/// Floating tree @p root reduced to one leaf with one window: the window floats where the tree was.
void
dissolve_floating_tree(ContextState& state,
                       std::uint32_t root) noexcept
{
    DockNodeState& tree = state.dock_nodes[root];
    const std::uint32_t host = tree.host_window;
    const std::uint32_t window = tree.windows.empty() ? ContextState::NO_WINDOW : state.find_window(tree.windows.front());
    if (host < state.windows.size())
    {
        state.windows[host].dock_tree = NONE;
    }
    if (window != ContextState::NO_WINDOW)
    {
        detach_window(state, window);
        if (host < state.windows.size())
        {
            WindowState& floating = state.windows[window];
            floating.position = state.windows[host].position;
            floating.size = state.windows[host].size;
            floating.collapsed = false;
        }
    }
    free_node(state, root);
}

/// After a window left leaf @p node: an empty leaf (not central) goes away, its parent taking its sibling's place;
/// a floating tree left with one window dissolves, an empty one goes away.
void
prune_node(ContextState& state,
           std::uint32_t node) noexcept
{
    if (node >= state.dock_nodes.size() || !state.dock_nodes[node].alive || state.dock_nodes[node].split())
    {
        return;
    }
    DockNodeState& leaf = state.dock_nodes[node];
    if (leaf.windows.empty() && !leaf.central)
    {
        const std::uint32_t parent = leaf.parent;
        if (parent == NONE)
        {
            // An empty floating tree.
            if (leaf.host_window < state.windows.size())
            {
                state.windows[leaf.host_window].dock_tree = NONE;
            }
            free_node(state, node);
            return;
        }

        // The parent takes the sibling's place: its split, or its windows.
        DockNodeState& up = state.dock_nodes[parent];
        const std::uint32_t sibling = up.children[0] == node ? up.children[1] : up.children[0];
        DockNodeState& other = state.dock_nodes[sibling];
        up.children = other.children;
        up.side_by_side = other.side_by_side;
        up.ratio = other.ratio;
        up.central = other.central;
        std::swap(up.windows, other.windows);
        up.selected = other.selected;
        up.selection_changed = true;
        for (const std::uint32_t child : up.children)
        {
            if (child != NONE)
            {
                state.dock_nodes[child].parent = parent;
            }
        }
        for (const std::uint32_t id : up.windows)
        {
            if (const std::uint32_t window = state.find_window(id); window != ContextState::NO_WINDOW)
            {
                state.windows[window].dock_node = parent;
            }
        }
        free_node(state, sibling);
        free_node(state, node);
        node = parent;
    }

    // A floating tree of one leaf and one window dissolves.
    const std::uint32_t root = state.dock_root(node);
    const DockNodeState& tree = state.dock_nodes[root];
    if (tree.dock_space == 0 && !tree.split() && tree.windows.size() <= 1)
    {
        if (tree.windows.empty())
        {
            if (tree.host_window < state.windows.size())
            {
                state.windows[tree.host_window].dock_tree = NONE;
            }
            free_node(state, root);
            return;
        }
        dissolve_floating_tree(state, root);
    }
}

/// Every window docked in @p node's subtree, through @p visit(window identifier).
template<class Visitor>
void
for_each_docked(const ContextState& state,
                std::uint32_t node,
                Visitor&& visit,
                std::uint32_t depth = 0)
{
    if (node >= state.dock_nodes.size() || depth > 64)
    {
        return;
    }
    const DockNodeState& entry = state.dock_nodes[node];
    if (entry.split())
    {
        for_each_docked(state, entry.children[0], visit, depth + 1);
        for_each_docked(state, entry.children[1], visit, depth + 1);
        return;
    }
    for (const std::uint32_t id : entry.windows)
    {
        visit(id);
    }
}

/// Frees @p node's subtree (not @p node itself), its windows undocked.
void
free_subtree(ContextState& state,
             std::uint32_t node,
             std::uint32_t depth = 0) noexcept
{
    if (node >= state.dock_nodes.size() || depth > 64)
    {
        return;
    }
    const std::array children = state.dock_nodes[node].children;
    for (const std::uint32_t child : children)
    {
        if (child != NONE)
        {
            free_subtree(state, child, depth + 1);
            free_node(state, child);
        }
    }
    for (const std::uint32_t id : state.dock_nodes[node].windows)
    {
        if (const std::uint32_t window = state.find_window(id); window != ContextState::NO_WINDOW && state.windows[window].dock_node == node)
        {
            state.windows[window].dock_node = NONE;
            state.windows[window].docked = false;
        }
    }
    state.dock_nodes[node].windows.clear();
    state.dock_nodes[node].children = {NONE, NONE};
}

/// A floating tree for window @p window, where it is: a root leaf holding it, and a host window.
auto
create_floating_tree(ContextState& state,
                     std::uint32_t window) -> std::uint32_t
{
    const std::uint32_t root = state.create_dock_node(0, NONE);
    const ShortName name = name_with_identifier("##dock_host_", state.dock_nodes[root].id);
    const std::uint32_t id = ContextState::hash(0, name.view());
    std::uint32_t host = state.find_window(id);
    if (host == ContextState::NO_WINDOW)
    {
        host = state.create_window(id, name.view(), ContextState::NO_WINDOW);
    }
    const WindowState& target = state.windows[window];
    WindowState& host_window = state.windows[host];
    host_window.position = {.x = target.outer.left, .y = target.outer.top};
    host_window.size = {.x = std::max(target.outer.width(), MINIMUM_DOCK_NODE_SIZE * 2), .y = std::max(target.outer.height(), MINIMUM_DOCK_NODE_SIZE * 2)};
    host_window.first_use = false;
    host_window.dock_tree = root;
    state.dock_nodes[root].host_window = host;
    state.dock_window(window, root);
    return root;
}

/// Begins and lays out floating tree @p root's host window (its windows go in its nodes).
void
begin_floating_host(ContextState& state,
                    UiFrame& frame,
                    std::uint32_t root) noexcept
{
    // The settings asked for the next window are not the host's.
    const NextWindowSettings next = state.next_window;
    state.next_window = NextWindowSettings{};
    const ShortName name = name_with_identifier("##dock_host_", state.dock_nodes[root].id);
    frame.push_style_variable(StyleVariable::WINDOW_PADDING, Vector2{});
    const bool open = frame.begin_window(name.view(), UiWindowFlag::NO_TITLE_BAR | UiWindowFlag::NO_COLLAPSE | UiWindowFlag::NO_SCROLLBAR
                                                          | UiWindowFlag::NO_SCROLL_WITH_MOUSE | UiWindowFlag::NO_SAVED_SETTINGS);
    frame.pop_style_variable();
    if (open && !state.window_stack.empty())
    {
        const std::uint32_t host = state.window_stack.back();
        state.dock_nodes[root].host_window = host;
        state.windows[host].dock_tree = root;
        state.layout_dock_tree(frame, root, state.windows[host].inner, host, DockNodeFlag::NONE);
    }
    frame.end_window();
    state.next_window = next;
}

/// Window @p window torn off its node by its tab: it floats under the mouse, being moved.
void
tear_off(ContextState& state,
         std::uint32_t window) noexcept
{
    const std::uint32_t node = state.windows[window].dock_node;
    if (node >= state.dock_nodes.size())
    {
        return;
    }
    const float title = std::floor(state.current_font().line_height + state.style.frame_padding.y * 2);
    const Vector2 size{.x = std::max(state.dock_nodes[node].content.width(), MINIMUM_DOCK_NODE_SIZE * 2),
                       .y = std::max(state.dock_nodes[node].content.height(), MINIMUM_DOCK_NODE_SIZE) + title};
    state.undock_window(window);
    WindowState& floating = state.windows[window];
    floating.size = size;
    floating.collapsed = false;
    floating.position = {.x = state.mouse_position.x - std::min(size.x * 0.5F, 40.0F), .y = state.mouse_position.y - title * 0.5F};
    state.moving_window = window;
    state.moving_offset = {.x = state.mouse_position.x - floating.position.x, .y = state.mouse_position.y - floating.position.y};
    state.set_active(ContextState::hash(floating.id, "#MOVE"), floating);
    state.active_button = MouseButton::LEFT;
    state.focus_window(window);
}

/// The tab bar of leaf @p node over @p bar in @p host: a tab per window shown, selection, closing, tearing off;
/// the node's selection and tab order follow the bar.
void
leaf_tab_bar(ContextState& state,
             UiFrame& frame,
             std::uint32_t node,
             std::uint32_t host,
             const UiRectangle& rectangle) noexcept
{
    TabBarState* bar = begin_tab_bar_at(state, state.windows[host], ContextState::hash(state.dock_nodes[node].id, "#TABS"), rectangle,
                                        TabBarFlag::REORDERABLE | TabBarFlag::FITTING_POLICY_SHRINK, "dock_space", {});
    if (bar == nullptr)
    {
        return;
    }
    const std::uint32_t bar_id = bar->id;
    const auto bar_index = static_cast<std::size_t>(bar - state.tab_bars.data());
    if (state.dock_nodes[node].selection_changed && state.dock_nodes[node].selected != 0)
    {
        bar->selected = tab_identifier(bar_id, state.dock_nodes[node].selected);
        bar->next_selected = 0;
    }
    state.dock_nodes[node].selection_changed = false;

    // A tab per window shown (indices only: a tooltip may add windows).
    std::uint32_t torn = ContextState::NO_WINDOW;
    const bool undocking = !has_any(state.dock_nodes[node].flags, DockNodeFlag::NO_UNDOCKING);
    for (std::size_t position = 0; position < state.dock_nodes[node].windows.size(); ++position)
    {
        const std::uint32_t id = state.dock_nodes[node].windows[position];
        const std::uint32_t window = state.find_window(id);
        if (window == ContextState::NO_WINDOW || !(state.windows[window].active || state.windows[window].was_active))
        {
            continue;
        }
        const ShortName label = tab_label(state.windows[window]);
        const TabItemFlag flags = has_any(state.windows[window].flags, UiWindowFlag::UNSAVED_DOCUMENT) ? TabItemFlag::UNSAVED_DOCUMENT : TabItemFlag::NONE;
        bool open = true;
        const bool selected = state.windows[window].closable ? frame.begin_tab_item(label.view(), open, flags) : frame.begin_tab_item(label.view(), flags);
        if (selected)
        {
            frame.end_tab_item();
        }
        const std::uint32_t tab = tab_identifier(bar_id, id);
        if (!open)
        {
            state.windows[window].close_requested = true;
        }
        if (state.active_id == tab && state.mouse[0].clicked)
        {
            state.focus_window(window);
        }
        const float center = (rectangle.top + rectangle.bottom) * 0.5F;
        if (undocking && state.active_id == tab && state.mouse[0].down && std::abs(state.mouse_position.y - center) > rectangle.height() * 1.5F)
        {
            torn = window;
        }
    }
    frame.end_tab_bar();

    // The node's selection and tab order follow the bar.
    bar = &state.tab_bars[bar_index];
    DockNodeState& leaf = state.dock_nodes[node];
    for (const std::uint32_t id : leaf.windows)
    {
        if (bar->selected == tab_identifier(bar_id, id) && present(state, id))
        {
            leaf.selected = id;
        }
    }
    state.dock_scratch.clear();
    for (const TabState& tab : bar->tabs)
    {
        for (const std::uint32_t id : leaf.windows)
        {
            if (tab.id == tab_identifier(bar_id, id) && state.dock_scratch.size() < state.dock_scratch.capacity())
            {
                state.dock_scratch.push_back(id);
            }
        }
    }
    std::size_t next = 0;
    for (std::uint32_t& id : leaf.windows)
    {
        if (next < state.dock_scratch.size() && std::ranges::contains(state.dock_scratch, id))
        {
            id = state.dock_scratch[next++];
        }
    }
    if (torn != ContextState::NO_WINDOW)
    {
        tear_off(state, torn);
    }
}

/// Lays out @p node over @p area of @p host: a split node's separator and children, a leaf's tab bar or empty
/// background.
void
layout_node(ContextState& state,
            UiFrame& frame,
            std::uint32_t node,
            UiRectangle area,
            std::uint32_t host,
            DockNodeFlag flags,
            std::uint32_t depth) noexcept
{
    if (node >= state.dock_nodes.size() || !state.dock_nodes[node].alive || depth > 64)
    {
        return;
    }
    {
        DockNodeState& entry = state.dock_nodes[node];
        entry.rectangle = area;
        entry.content = area;
        entry.clip = state.windows[host].clip;
        entry.last_frame = state.frame;
        entry.host_window = host;
        entry.flags = flags;
        entry.tab_bar = false;
    }
    const Style& style = state.style;
    DrawList& list = state.list_of(state.windows[host]);
    if (state.dock_nodes[node].split())
    {
        // The children beside the separator (each kept at MINIMUM_DOCK_NODE_SIZE when there is room).
        DockNodeState& entry = state.dock_nodes[node];
        const bool side_by_side = entry.side_by_side;
        const float separator = std::max(style.docking_separator_size, 1.0F);
        const float start = side_by_side ? area.left : area.top;
        const float total = std::max(0.0F, (side_by_side ? area.width() : area.height()) - separator);
        const auto first_size = [&](float ratio)
        {
            const float size = std::round(total * ratio);
            return total >= MINIMUM_DOCK_NODE_SIZE * 2 ? std::clamp(size, MINIMUM_DOCK_NODE_SIZE, total - MINIMUM_DOCK_NODE_SIZE) : size;
        };
        float first = first_size(entry.ratio);
        const auto bar_at = [&](float size)
        {
            return side_by_side ? UiRectangle{.left = start + size, .top = area.top, .right = start + size + separator, .bottom = area.bottom}
                                : UiRectangle{.left = area.left, .top = start + size, .right = area.right, .bottom = start + size + separator};
        };
        UiRectangle bar = bar_at(first);

        // Dragging the separator moves the ratio (not with NO_RESIZE).
        if (!has_any(flags, DockNodeFlag::NO_RESIZE) && total > 0)
        {
            const float grow = std::max(0.0F, RESIZE_HANDLE_HALF_WIDTH - separator * 0.5F);
            const UiRectangle hit = (side_by_side ? UiRectangle{.left = bar.left - grow, .top = bar.top, .right = bar.right + grow, .bottom = bar.bottom}
                                                  : UiRectangle{.left = bar.left, .top = bar.top - grow, .right = bar.right, .bottom = bar.bottom + grow})
                                        .intersection(state.windows[host].clip);
            const std::uint32_t id = ContextState::hash(entry.id, "#SPLIT");
            const LastItem saved = state.last_item;
            const ButtonState button = state.button_behavior(state.windows[host], hit, id);
            state.last_item = saved;
            if (state.active_id == id && state.mouse[0].clicked)
            {
                state.active_offset = {.x = state.mouse_position.x - bar.left, .y = state.mouse_position.y - bar.top};
            }
            if (button.held)
            {
                const float moved = side_by_side ? state.mouse_position.x - state.active_offset.x - start : state.mouse_position.y - state.active_offset.y - start;
                state.dock_nodes[node].ratio = std::clamp(moved / total, 0.0F, 1.0F);
                first = first_size(state.dock_nodes[node].ratio);
                bar = bar_at(first);
            }
            if (button.hovered || button.held)
            {
                state.cursor = side_by_side ? CursorShape::RESIZE_EAST_WEST : CursorShape::RESIZE_NORTH_SOUTH;
                list.add_rectangle_filled(bar, state.color(button.held ? StyleColor::SEPARATOR_ACTIVE : StyleColor::SEPARATOR_HOVERED));
            }
        }
        const std::array children = state.dock_nodes[node].children;
        const UiRectangle first_area = side_by_side ? UiRectangle{.left = area.left, .top = area.top, .right = bar.left, .bottom = area.bottom}
                                                    : UiRectangle{.left = area.left, .top = area.top, .right = area.right, .bottom = bar.top};
        const UiRectangle second_area = side_by_side ? UiRectangle{.left = bar.right, .top = area.top, .right = area.right, .bottom = area.bottom}
                                                     : UiRectangle{.left = area.left, .top = bar.bottom, .right = area.right, .bottom = area.bottom};
        layout_node(state, frame, children[0], first_area, host, flags, depth + 1);
        layout_node(state, frame, children[1], second_area, host, flags, depth + 1);
        return;
    }

    // A leaf: its tab bar (none for a lone window with AUTO_HIDE_TAB_BAR), or its empty background.
    std::size_t shown = 0;
    for (const std::uint32_t id : state.dock_nodes[node].windows)
    {
        shown += present(state, id) ? 1U : 0U;
    }
    DockNodeState& leaf = state.dock_nodes[node];
    if (shown == 0)
    {
        const bool pass_through = leaf.central && has_any(flags, DockNodeFlag::PASS_THROUGH_CENTRAL_NODE);
        if (pass_through)
        {
            state.windows[host].hole = area;
        }
        else
        {
            list.add_rectangle_filled(area, state.color(StyleColor::DOCKING_EMPTY_BACKGROUND));
        }
        return;
    }
    if (shown == 1 && has_any(flags, DockNodeFlag::AUTO_HIDE_TAB_BAR))
    {
        return;
    }
    const float height = std::floor(state.current_font().line_height + style.frame_padding.y * 2);
    const UiRectangle bar{.left = area.left, .top = area.top, .right = area.right, .bottom = std::min(area.bottom, area.top + height)};
    leaf.tab_bar = true;
    leaf.content = {.left = area.left, .top = bar.bottom, .right = area.right, .bottom = std::max(bar.bottom, area.bottom)};
    list.add_rectangle_filled(bar, state.color(StyleColor::TITLE_BACKGROUND));
    leaf_tab_bar(state, frame, node, host, bar);
}

/// The nodes and window under the mouse that the window being moved could dock into, skipping @p moving and the
/// windows of its tree.
auto
drop_candidate(const ContextState& state,
               std::uint32_t moving) noexcept -> std::uint32_t
{
    const std::uint32_t tree = state.windows[moving].dock_tree;
    for (const std::uint32_t index : std::views::reverse(state.display_order))
    {
        const WindowState& window = state.windows[index];
        if (index == moving || !window.active || window.hidden || window.popup || window.tooltip || has_any(window.flags, UiWindowFlag::NO_MOUSE_INPUTS))
        {
            continue;
        }
        if (tree != NONE && window.dock_node != NONE && state.dock_root(window.dock_node) == tree)
        {
            continue;
        }
        if (contains(window.hit, state.mouse_position))
        {
            return index;
        }
    }
    return ContextState::NO_WINDOW;
}

} // namespace

// ===================================================================================================== nodes

auto
ContextState::dock_handle(std::uint32_t node) const noexcept -> DockNodeHandle
{
    if (node >= dock_nodes.size() || !dock_nodes[node].alive)
    {
        return {};
    }
    try
    {
        return DockNodeHandle::create(DockHandles::key(), node, dock_nodes[node].generation);
    }
    catch (...)
    {
        return {};
    }
}

auto
ContextState::dock_node_of(DockNodeHandle handle) const noexcept -> std::uint32_t
{
    if (!handle.is_valid())
    {
        return NONE;
    }
    const std::uint32_t index = handle.index(DockHandles::key());
    const std::uint32_t generation = handle.generation(DockHandles::key());
    return index < dock_nodes.size() && dock_nodes[index].alive && dock_nodes[index].generation == generation ? index : NONE;
}

auto
ContextState::create_dock_node(std::uint32_t dock_space,
                               std::uint32_t parent) -> std::uint32_t
{
    // A free slot (its window list's storage kept), else a new one.
    std::uint32_t index = NONE;
    if (!free_dock_nodes.empty())
    {
        index = free_dock_nodes.back();
        free_dock_nodes.pop_back();
    }
    else
    {
        if (dock_nodes.size() == dock_nodes.capacity())
        {
            ++allocations;
            report(UiFrameProblem::CAPACITY_EXCEEDED, "dock_space", "more dock nodes than UiContextLimits::dock_nodes");
        }
        index = static_cast<std::uint32_t>(dock_nodes.size());
        dock_nodes.emplace_back();
    }
    DockNodeState& node = dock_nodes[index];
    const std::uint32_t generation = node.generation;
    std::vector<std::uint32_t> windows_storage = std::move(node.windows);
    windows_storage.clear();
    node = DockNodeState{};
    node.generation = generation;
    node.windows = std::move(windows_storage);
    node.alive = true;
    node.dock_space = dock_space;
    node.parent = parent;

    // Its identifier: the counter's bytes hashed.
    ++dock_node_counter;
    std::array<char, 4> bytes{};
    for (std::size_t byte = 0; byte < bytes.size(); ++byte)
    {
        bytes[byte] = static_cast<char>((dock_node_counter >> (8U * byte)) & 0xFFU);
    }
    node.id = hash(0x646F636BU, {bytes.data(), bytes.size()});
    node.id = node.id != 0 ? node.id : 1U;
    return index;
}

auto
ContextState::dock_root(std::uint32_t node) const noexcept -> std::uint32_t
{
    for (std::uint32_t depth = 0; node < dock_nodes.size() && dock_nodes[node].parent != NONE && depth < 64; ++depth)
    {
        node = dock_nodes[node].parent;
    }
    return node;
}

auto
ContextState::dock_leaf(std::uint32_t node) const noexcept -> std::uint32_t
{
    // The central leaf of the subtree, else its first leaf.
    std::uint32_t first = NONE;
    std::uint32_t central = NONE;
    const auto visit = [&](auto&& self, std::uint32_t index, std::uint32_t depth) -> void
    {
        if (index >= dock_nodes.size() || depth > 64)
        {
            return;
        }
        const DockNodeState& entry = dock_nodes[index];
        if (!entry.split())
        {
            first = first == NONE ? index : first;
            central = central == NONE && entry.central ? index : central;
            return;
        }
        self(self, entry.children[0], depth + 1);
        self(self, entry.children[1], depth + 1);
    };
    visit(visit, node, 0);
    return central != NONE ? central : first;
}

auto
ContextState::dock_space_root(std::uint32_t dock_space) -> std::uint32_t
{
    if (const std::optional<std::uint32_t> found = dock_space_roots.find(dock_space); found.has_value() && *found < dock_nodes.size() && dock_nodes[*found].alive)
    {
        return *found;
    }
    const std::uint32_t root = create_dock_node(dock_space, NONE);
    dock_nodes[root].central = true;
    if (dock_space_roots.insert(dock_space, root))
    {
        ++allocations;
    }
    return root;
}

void
ContextState::dock_window(std::uint32_t window,
                          std::uint32_t node)
{
    // Already there: selected. Elsewhere: it leaves that node after it arrives here (its pruning may move this node's
    // windows up into their parent).
    WindowState& entry = windows[window];
    if (entry.dock_node == node)
    {
        dock_nodes[node].selected = entry.id;
        dock_nodes[node].selection_changed = true;
        return;
    }
    const std::uint32_t previous = entry.dock_node;
    detach_window(*this, window);
    dock_nodes[node].windows.push_back(entry.id);
    dock_nodes[node].selected = entry.id;
    dock_nodes[node].selection_changed = true;
    entry.dock_node = node;
    prune_node(*this, previous);
}

void
ContextState::undock_window(std::uint32_t window) noexcept
{
    const std::uint32_t node = windows[window].dock_node;
    detach_window(*this, window);
    prune_node(*this, node);
}

auto
ContextState::split_dock_node(std::uint32_t node,
                              ArrowDirection direction,
                              float ratio) -> std::pair<std::uint32_t, std::uint32_t>
{
    // Two children: the new empty leaf on the direction's side, the rest keeping the leaf's windows and role.
    const std::uint32_t dock_space = dock_nodes[node].dock_space;
    const std::uint32_t first = create_dock_node(dock_space, node);
    const std::uint32_t second = create_dock_node(dock_space, node);
    const bool fresh_first = direction == ArrowDirection::LEFT || direction == ArrowDirection::UP;
    const std::uint32_t fresh = fresh_first ? first : second;
    const std::uint32_t rest = fresh_first ? second : first;
    DockNodeState& split = dock_nodes[node];
    DockNodeState& kept = dock_nodes[rest];
    std::swap(kept.windows, split.windows);
    kept.selected = split.selected;
    kept.selection_changed = true;
    kept.central = split.central;
    kept.flags = split.flags;
    dock_nodes[fresh].flags = split.flags;
    split.central = false;
    split.selected = 0;
    split.children = {first, second};
    split.side_by_side = direction == ArrowDirection::LEFT || direction == ArrowDirection::RIGHT;
    split.ratio = fresh_first ? ratio : 1.0F - ratio;
    for (const std::uint32_t id : kept.windows)
    {
        if (const std::uint32_t window = find_window(id); window != NO_WINDOW)
        {
            windows[window].dock_node = rest;
        }
    }
    return {fresh, rest};
}

void
ContextState::clear_dock_tree(std::uint32_t root) noexcept
{
    free_subtree(*this, root);
    DockNodeState& node = dock_nodes[root];
    node.selected = 0;
    node.selection_changed = true;
    node.central = node.dock_space != 0;
}

void
ContextState::clear_docking() noexcept
{
    for (std::uint32_t node = 0; node < dock_nodes.size(); ++node)
    {
        if (dock_nodes[node].alive && dock_nodes[node].parent == NONE)
        {
            free_subtree(*this, node);
            free_node(*this, node);
        }
    }
    for (WindowState& window : windows)
    {
        window.dock_node = NONE;
        window.docked = false;
        window.dock_tree = NONE;
    }
    dock_space_roots.clear();
    dock_drop = DockDropState{};
}

auto
ContextState::dock_host_of(std::uint32_t window) const noexcept -> std::uint32_t
{
    const std::uint32_t node = windows[window].dock_node;
    if (node >= dock_nodes.size() || !dock_nodes[node].alive)
    {
        return NO_WINDOW;
    }
    const std::uint32_t host = dock_nodes[dock_root(node)].host_window;
    return host < windows.size() ? windows[host].root : NO_WINDOW;
}

// ===================================================================================================== layout

auto
ContextState::prepare_docked_window(UiFrame& ui_frame,
                                    std::uint32_t window) noexcept -> DockPlacement
{
    // Docked in a leaf that still exists (else floating again).
    const std::uint32_t node = windows[window].dock_node;
    if (node == NONE)
    {
        return {};
    }
    if (node >= dock_nodes.size() || !dock_nodes[node].alive || dock_nodes[node].split())
    {
        windows[window].dock_node = NONE;
        return {};
    }

    // A floating tree's host is begun first (once a frame), so its nodes are placed.
    const std::uint32_t root = dock_root(node);
    if (dock_nodes[root].dock_space == 0)
    {
        const std::uint32_t host = dock_nodes[root].host_window;
        if (host < windows.size() && !windows[host].active)
        {
            begin_floating_host(*this, ui_frame, root);
        }
    }

    // Placed where its node was laid out (this frame or the last); shown when it is the node's selected window.
    const DockNodeState& leaf = dock_nodes[node];
    if (leaf.last_frame == 0 || leaf.last_frame + 1 < frame || has_any(leaf.flags, DockNodeFlag::KEEP_ALIVE_ONLY))
    {
        return {.docked = true, .visible = false, .area = leaf.content, .clip = {}};
    }
    return {.docked = true, .visible = leaf.selected == windows[window].id, .area = leaf.content, .clip = leaf.clip};
}

void
ContextState::layout_dock_tree(UiFrame& ui_frame,
                               std::uint32_t root,
                               const UiRectangle& area,
                               std::uint32_t host,
                               DockNodeFlag flags) noexcept
{
    layout_node(*this, ui_frame, root, area, host, flags, 0);
}

void
ContextState::arrange_docked_windows() noexcept
{
    // Each root window, then the windows docked in the trees it hosts (and theirs), then its front list.
    dock_order.clear();
    publish_order.clear();
    dock_placed.assign(windows.size(), 0);
    const auto place = [&](auto&& self, std::uint32_t index, std::uint32_t depth) -> void
    {
        if (dock_placed[index] != 0 || depth > 16)
        {
            return;
        }
        dock_placed[index] = 1;
        dock_order.push_back(index);
        publish_order.push_back(index);
        for (const std::uint32_t other : display_order)
        {
            if (other != index && dock_placed[other] == 0 && windows[other].docked && dock_host_of(other) == index)
            {
                self(self, other, depth + 1);
            }
        }
        if (!windows[index].front_list.vertices().empty())
        {
            publish_order.push_back(index | FRONT_LIST);
        }
    };
    for (const std::uint32_t index : display_order)
    {
        const std::uint32_t host = windows[index].docked ? dock_host_of(index) : NO_WINDOW;
        if (host != NO_WINDOW && host != index && std::ranges::contains(display_order, host))
        {
            continue;
        }
        place(place, index, 0);
    }
    for (const std::uint32_t index : display_order)
    {
        place(place, index, 0);
    }
    std::swap(display_order, dock_order);
}

// ===================================================================================================== dragging to dock

void
ContextState::update_dock_drop() noexcept
{
    dock_drop = DockDropState{};
    dock_overlay.clear();
    dock_overlay.set_pixels_per_unit(pixels_per_unit);
    if (moving_window == NO_WINDOW || !mouse_present)
    {
        return;
    }
    const WindowState& moving = windows[moving_window];
    if (has_any(moving.flags, UiWindowFlag::NO_DOCKING) || moving.popup || moving.tooltip || moving.child || moving.docked)
    {
        return;
    }

    // Under the mouse: a docked window (its node), a host (the leaf under the mouse), or a floating window.
    const std::uint32_t candidate = drop_candidate(*this, moving_window);
    if (candidate == NO_WINDOW)
    {
        return;
    }
    const WindowState& window = windows[candidate];
    std::uint32_t node = NONE;
    if (window.docked && window.dock_node < dock_nodes.size())
    {
        node = window.dock_node;
    }
    else
    {
        for (std::uint32_t index = 0; index < dock_nodes.size(); ++index)
        {
            const DockNodeState& entry = dock_nodes[index];
            if (entry.alive && !entry.split() && entry.last_frame == frame && entry.host_window < windows.size()
                && windows[entry.host_window].root == candidate && contains(entry.rectangle, mouse_position))
            {
                node = index;
            }
        }
    }
    const bool hosts_trees = window.dock_tree != NONE || std::ranges::any_of(dock_nodes, [&](const DockNodeState& entry)
    {
        return entry.alive && entry.last_frame == frame && entry.host_window < windows.size() && windows[entry.host_window].root == candidate;
    });
    if (node == NONE && (hosts_trees || has_any(window.flags, UiWindowFlag::NO_DOCKING) || window.child))
    {
        return;
    }
    dock_drop.moving = moving_window;
    dock_drop.node = node;
    dock_drop.window = node == NONE ? candidate : NONE;
    dock_drop.area = node != NONE ? dock_nodes[node].rectangle : window.outer;
    const DockNodeFlag flags = node != NONE ? dock_nodes[node].flags : DockNodeFlag::NONE;

    // The squares: in the middle of the area, as room allows; not over the central node or beside nodes as the flags
    // forbid.
    const UiRectangle& area = dock_drop.area;
    const float size = std::min({DOCKING_TARGET_SIZE, area.width() / 3.5F, area.height() / 3.5F});
    if (size < 8.0F)
    {
        return;
    }
    const Vector2 center{.x = (area.left + area.right) * 0.5F, .y = (area.top + area.bottom) * 0.5F};
    const float step = size * 1.25F;
    const auto square = [&](float dx, float dy)
    {
        return UiRectangle{.left = center.x + dx - size * 0.5F, .top = center.y + dy - size * 0.5F, .right = center.x + dx + size * 0.5F,
                           .bottom = center.y + dy + size * 0.5F};
    };
    const bool center_allowed = !(node != NONE && dock_nodes[node].central && has_any(flags, DockNodeFlag::NO_DOCKING_OVER_CENTRAL_NODE));
    const bool sides_allowed = !has_any(flags, DockNodeFlag::NO_DOCKING_SPLIT);
    if (center_allowed)
    {
        dock_drop.targets[TARGET_CENTER] = square(0, 0);
    }
    if (sides_allowed)
    {
        dock_drop.targets[TARGET_LEFT] = square(-step, 0);
        dock_drop.targets[TARGET_RIGHT] = square(step, 0);
        dock_drop.targets[TARGET_TOP] = square(0, -step);
        dock_drop.targets[TARGET_BOTTOM] = square(0, step);
    }
    for (std::uint32_t target = 0; target < dock_drop.targets.size(); ++target)
    {
        if (!dock_drop.targets[target].empty() && contains(dock_drop.targets[target], mouse_position))
        {
            dock_drop.target = target;
        }
    }
    if (dock_drop.target == NO_TARGET && center_allowed && node != NONE && dock_nodes[node].tab_bar
        && contains({.left = area.left, .top = area.top, .right = area.right, .bottom = dock_nodes[node].content.top}, mouse_position))
    {
        dock_drop.target = TARGET_CENTER;
    }

    // The preview where the window would go, then the squares.
    const float half_width = area.width() * 0.5F;
    const float half_height = area.height() * 0.5F;
    switch (dock_drop.target)
    {
        case TARGET_CENTER: dock_drop.preview = area; break;
        case TARGET_LEFT:   dock_drop.preview = {.left = area.left, .top = area.top, .right = area.left + half_width, .bottom = area.bottom}; break;
        case TARGET_RIGHT:  dock_drop.preview = {.left = area.right - half_width, .top = area.top, .right = area.right, .bottom = area.bottom}; break;
        case TARGET_TOP:    dock_drop.preview = {.left = area.left, .top = area.top, .right = area.right, .bottom = area.top + half_height}; break;
        case TARGET_BOTTOM: dock_drop.preview = {.left = area.left, .top = area.bottom - half_height, .right = area.right, .bottom = area.bottom}; break;
        default:            break;
    }
    try
    {
        if (!dock_drop.preview.empty())
        {
            dock_overlay.add_rectangle_filled(dock_drop.preview, color(StyleColor::DOCKING_PREVIEW));
        }
        const Color square_color = color(StyleColor::BUTTON);
        const Color hovered = color(StyleColor::BUTTON_ACTIVE);
        const Color mark = color(StyleColor::TEXT_NORMAL);
        for (std::uint32_t target = 0; target < dock_drop.targets.size(); ++target)
        {
            const UiRectangle& box = dock_drop.targets[target];
            if (box.empty())
            {
                continue;
            }
            dock_overlay.add_rectangle_filled(box, target == dock_drop.target ? hovered : square_color, style.frame_rounding);
            dock_overlay.add_rectangle(box, mark, 1.0F, style.frame_rounding);
            const float inset = size * 0.2F;
            UiRectangle part{.left = box.left + inset, .top = box.top + inset, .right = box.right - inset, .bottom = box.bottom - inset};
            switch (target)
            {
                case TARGET_LEFT:   part.right = (part.left + part.right) * 0.5F; break;
                case TARGET_RIGHT:  part.left = (part.left + part.right) * 0.5F; break;
                case TARGET_TOP:    part.bottom = (part.top + part.bottom) * 0.5F; break;
                case TARGET_BOTTOM: part.top = (part.top + part.bottom) * 0.5F; break;
                default:            break;
            }
            dock_overlay.add_rectangle_filled(part, mark.with_alpha(mark.alpha * 0.6F));
        }
    }
    catch (...)
    {
        report(UiFrameProblem::CAPACITY_EXCEEDED, "end_frame", "the docking targets could not be drawn");
    }
}

void
ContextState::apply_dock_drop() noexcept
{
    const DockDropState drop = dock_drop;
    dock_drop = DockDropState{};
    if (drop.moving != moving_window || drop.target == NO_TARGET || moving_window == NO_WINDOW)
    {
        return;
    }
    try
    {
        // The node docked into: the one under the mouse, or a floating tree made for the window under it.
        std::uint32_t node = drop.node;
        if (node == NONE)
        {
            if (drop.window >= windows.size() || windows[drop.window].dock_node != NONE)
            {
                return;
            }
            node = create_floating_tree(*this, drop.window);
        }
        if (node >= dock_nodes.size() || !dock_nodes[node].alive || dock_nodes[node].split())
        {
            return;
        }

        // A side square: the node is split first, the window (or tree) going into the new leaf.
        if (drop.target != TARGET_CENTER)
        {
            static constexpr std::array DIRECTIONS{ArrowDirection::LEFT, ArrowDirection::LEFT, ArrowDirection::RIGHT, ArrowDirection::UP, ArrowDirection::DOWN};
            node = split_dock_node(node, DIRECTIONS[drop.target], 0.5F).first;
        }

        // The moved window docks; a moved floating tree's windows dock in their tree's order (its host goes away).
        const std::uint32_t moved = drop.moving;
        const std::uint32_t tree = windows[moved].dock_tree;
        std::uint32_t focus = moved;
        if (tree != NONE && tree < dock_nodes.size() && dock_nodes[tree].alive)
        {
            dock_scratch.clear();
            for_each_docked(*this, tree, [&](std::uint32_t id)
            {
                if (dock_scratch.size() < dock_scratch.capacity())
                {
                    dock_scratch.push_back(id);
                }
            });
            const std::uint32_t selected = dock_nodes[tree].split() ? 0U : dock_nodes[tree].selected;
            windows[moved].dock_tree = NONE;
            free_subtree(*this, tree);
            free_node(*this, tree);
            for (const std::uint32_t id : dock_scratch)
            {
                if (const std::uint32_t window = find_window(id); window != NO_WINDOW)
                {
                    dock_window(window, node);
                    focus = id == selected || focus == moved ? window : focus;
                }
            }
        }
        else
        {
            dock_window(moved, node);
        }
        if (focus < windows.size() && windows[focus].dock_node < dock_nodes.size())
        {
            DockNodeState& leaf = dock_nodes[windows[focus].dock_node];
            leaf.selected = windows[focus].id;
            leaf.selection_changed = true;
        }
        focus_window(focus);
    }
    catch (...)
    {
        report(UiFrameProblem::CAPACITY_EXCEEDED, "begin_frame", "the window could not be docked");
    }
}

// ===================================================================================================== UiFrame

auto
UiFrame::dock_space(DockSpaceIdentifier id,
                    Vector2 size,
                    DockNodeFlag flags) noexcept -> DockNodeHandle
{
    ContextState& state = *m_state;
    if (!state.frame_active)
    {
        state.report(UiFrameProblem::FRAME_NOT_ACTIVE, "dock_space", "call it between UiContext::begin_frame() and end_frame()");
        return {};
    }
    if (!id.is_valid())
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "dock_space", "a dock space needs an identifier");
        return {};
    }

    // Outside any window: a host window over the display (under the main menu bar), behind every other window.
    const bool over_display = state.window_stack.empty();
    if (over_display)
    {
        const NextWindowSettings next = state.next_window;
        float top = 0;
        if (const std::uint32_t bar = state.find_window(ContextState::hash(0, MAIN_MENU_BAR_WINDOW)); bar != ContextState::NO_WINDOW
            && (state.windows[bar].active || state.windows[bar].was_active))
        {
            top = state.windows[bar].outer.bottom;
        }
        state.next_window = NextWindowSettings{};
        set_next_window_position({.x = 0, .y = top}, UiCondition::ALWAYS);
        set_next_window_size({.x = std::max(state.display_size.x, 1.0F), .y = std::max(state.display_size.y - top, 1.0F)}, UiCondition::ALWAYS);
        push_style_variable(StyleVariable::WINDOW_PADDING, Vector2{});
        push_style_variable(StyleVariable::WINDOW_ROUNDING, 0.0F);
        push_style_variable(StyleVariable::WINDOW_BORDER_SIZE, 0.0F);
        const ShortName name = name_with_identifier("##dock_space_", id.value());
        static_cast<void>(begin_window(name.view(), UiWindowFlag::NO_TITLE_BAR | UiWindowFlag::NO_COLLAPSE | UiWindowFlag::NO_RESIZE | UiWindowFlag::NO_MOVE
                                                        | UiWindowFlag::NO_BRING_TO_FRONT_ON_FOCUS | UiWindowFlag::NO_SAVED_SETTINGS | UiWindowFlag::NO_DOCKING
                                                        | UiWindowFlag::NO_SCROLLBAR | UiWindowFlag::NO_SCROLL_WITH_MOUSE | UiWindowFlag::NO_BACKGROUND
                                                        | UiWindowFlag::NO_NAVIGATION_FOCUS));
        pop_style_variable(3);
        state.next_window = next;
        const std::uint32_t host = state.window_stack.back();
        const auto found = std::ranges::find(state.display_order, host);
        if (found != state.display_order.end())
        {
            std::rotate(state.display_order.begin(), found, found + 1);
        }
    }
    const std::uint32_t host = state.window_stack.back();

    // Its area: as asked, else what is available; an item of the window.
    WindowState& window = state.windows[host];
    std::uint32_t root = DockNodeState::NONE;
    try
    {
        root = state.dock_space_root(id.value());
    }
    catch (...)
    {
        state.report(UiFrameProblem::CAPACITY_EXCEEDED, "dock_space", "the dock space could not be made");
    }
    if (root != DockNodeState::NONE && !window.skip_items)
    {
        if (state.dock_nodes[root].last_frame == state.frame)
        {
            state.report(UiFrameProblem::IDENTIFIER_CONFLICT, "dock_space", "a dock space was submitted twice in a frame: give each its own identifier");
        }
        const Vector2 available{.x = state.content_right(window) - window.cursor.x, .y = state.content_bottom(window) - window.cursor.y};
        const auto extent = [](float asked, float room) { return std::max(MINIMUM_DOCK_NODE_SIZE, asked > 0 ? asked : room + asked); };
        const Vector2 area_size{.x = extent(size.x, available.x), .y = extent(size.y, available.y)};
        const UiRectangle area{.left = window.cursor.x, .top = window.cursor.y, .right = window.cursor.x + area_size.x, .bottom = window.cursor.y + area_size.y};
        state.item_size(window, area_size);
        static_cast<void>(state.item_add(window, area, 0, false, "dock_space", {}));
        window.hole = {};
        if (has_any(flags, DockNodeFlag::KEEP_ALIVE_ONLY))
        {
            // Kept, not drawn: its windows stay docked, hidden.
            const auto keep = [&](auto&& self, std::uint32_t node, std::uint32_t depth) -> void
            {
                if (node >= state.dock_nodes.size() || depth > 64)
                {
                    return;
                }
                state.dock_nodes[node].last_frame = state.frame;
                state.dock_nodes[node].flags = flags;
                state.dock_nodes[node].host_window = host;
                self(self, state.dock_nodes[node].children[0], depth + 1);
                self(self, state.dock_nodes[node].children[1], depth + 1);
            };
            keep(keep, root, 0);
        }
        else
        {
            state.layout_dock_tree(*this, root, area, host, flags);
        }
    }
    if (over_display)
    {
        end_window();
    }
    return state.dock_handle(root);
}

void
UiFrame::set_next_window_dock(DockNodeHandle node,
                              UiCondition condition) noexcept
{
    NextWindowSettings& next = m_state->next_window;
    next.has_dock = true;
    next.dock = node;
    next.dock_condition = condition;
}

auto
UiFrame::is_window_docked() noexcept -> bool
{
    const WindowState* window = m_state->current_window_quiet();
    return window != nullptr && window->docked;
}

auto
UiFrame::window_dock_node() noexcept -> DockNodeHandle
{
    const WindowState* window = m_state->current_window_quiet();
    return window != nullptr && window->docked ? m_state->dock_handle(window->dock_node) : DockNodeHandle{};
}

// ===================================================================================================== builder

namespace
{

/// Throws the InvalidUsageError of a dock builder's misuse.
[[noreturn]] void
throw_builder_error(std::string_view operation,
                    std::string problem,
                    std::string suggestion)
{
    throw InvalidUsageError({.backend = UiSubsystem::WIDGETS, .operation = std::string{operation}, .problem = std::move(problem),
                             .suggestion = std::move(suggestion)});
}

} // namespace

auto
UiContext::dock_builder(DockSpaceIdentifier dock_space) -> UiDockBuilder
{
    if (!dock_space.is_valid())
    {
        throw_builder_error("UiContext::dock_builder", "the dock space identifier is null", "name the dock space: \"editor\"_dock_space");
    }
    ContextState& state = *m_state;
    const std::uint32_t root = state.dock_space_root(dock_space.value());
    state.clear_dock_tree(root);
    return UiDockBuilder{state, dock_space.value(), state.dock_handle(root)};
}

UiDockBuilder::UiDockBuilder(ContextState& state,
                             std::uint32_t dock_space,
                             DockNodeHandle root) noexcept
    : m_state{&state}
    , m_dock_space{dock_space}
    , m_root{root}
{
}

auto
UiDockBuilder::node_of(DockNodeHandle handle,
                       std::string_view operation) const -> std::uint32_t
{
    if (m_finished)
    {
        throw_builder_error(operation, "the builder was finished", "make another one with UiContext::dock_builder()");
    }
    const std::uint32_t node = m_state->dock_node_of(handle);
    if (node == DockNodeState::NONE || m_state->dock_nodes[node].dock_space != m_dock_space)
    {
        throw_builder_error(operation, std::format("{} names no node of this dock space", handle), "use root() or a node split() returned");
    }
    return node;
}

auto
UiDockBuilder::split(DockNodeHandle node,
                     ArrowDirection direction,
                     float ratio) -> std::pair<DockNodeHandle, DockNodeHandle>
{
    const std::uint32_t index = node_of(node, "UiDockBuilder::split");
    if (m_state->dock_nodes[index].split())
    {
        throw_builder_error("UiDockBuilder::split", std::format("{} is split already", node), "split one of its children (split() returned them)");
    }
    if (!(ratio > 0.0F && ratio < 1.0F))
    {
        throw_builder_error("UiDockBuilder::split", std::format("the ratio {} is not between 0 and 1", ratio), "give the new node's share of the size, e.g. 0.25");
    }
    const auto [fresh, rest] = m_state->split_dock_node(index, direction, ratio);
    return {m_state->dock_handle(fresh), m_state->dock_handle(rest)};
}

void
UiDockBuilder::dock_window(std::string_view window,
                           DockNodeHandle node)
{
    const std::uint32_t index = m_state->dock_leaf(node_of(node, "UiDockBuilder::dock_window"));
    if (window.empty())
    {
        throw_builder_error("UiDockBuilder::dock_window", "the window name is empty", "give the name begin_window() is called with");
    }

    // The window of this name (made now when it was never submitted).
    const std::uint32_t id = ContextState::hash(0, window);
    std::uint32_t target = m_state->find_window(id);
    if (target == ContextState::NO_WINDOW)
    {
        target = m_state->create_window(id, window, ContextState::NO_WINDOW);
    }
    m_state->dock_window(target, index);
}

void
UiDockBuilder::finish() noexcept
{
    m_finished = true;
}

} // namespace gfx::ui
