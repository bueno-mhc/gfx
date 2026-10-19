/// @file tabs.cpp
/// @brief Tab bars: tabs remembered between frames and laid out from last frame's (leading ones on the left,
///        trailing ones on the right, the others between them, shrunk or scrolled when they do not fit), selected
///        by a click (from the next frame), closed by their close button or the middle button, reordered by
///        dragging; a button listing the tabs; tooltips for cut labels.
module gfx.ui.context;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.draw;
import gfx.ui.style;
import gfx.ui.text;
import :state;

namespace gfx::ui
{

namespace
{

/// Whether @p rectangle contains @p point.
constexpr auto
contains(const UiRectangle& rectangle,
         Vector2 point) noexcept -> bool
{
    return point.x >= rectangle.left && point.x < rectangle.right && point.y >= rectangle.top && point.y < rectangle.bottom;
}

/// Where a tab goes: before the others, between them, after them.
auto
tab_section(TabItemFlag flags) noexcept -> int
{
    return has_any(flags, TabItemFlag::LEADING) ? 0 : has_any(flags, TabItemFlag::TRAILING) ? 2 : 1;
}

auto
find_tab(TabBarState& bar,
         std::uint32_t id) noexcept -> TabState*
{
    const auto found = std::ranges::find(bar.tabs, id, &TabState::id);
    return found == bar.tabs.end() ? nullptr : &*found;
}

/// The tab bar begin_tab_bar() started last, or nullptr (reported for @p operation).
auto
current_bar(ContextState& state,
            std::string_view operation) noexcept -> TabBarState*
{
    if (state.tab_bar_stack.empty())
    {
        state.report(UiFrameProblem::END_WITHOUT_BEGIN, operation, "call it between begin_tab_bar() returning true and end_tab_bar()");
        return nullptr;
    }
    return &state.tab_bars[state.tab_bar_stack.back()];
}

/// Lays out the tabs submitted last time: leading tabs and the tab list button on the left, trailing tabs on the
/// right, the others between (shrunk, or scrolled with arrow buttons, when they do not fit).
void
layout_tabs(const ContextState& state,
            TabBarState& bar,
            float button_size) noexcept
{
    const float spacing = state.style.item_inner_spacing.x;
    float left = bar.bar.left + (has_any(bar.flags, TabBarFlag::TAB_LIST_POPUP_BUTTON) ? button_size + spacing : 0.0F);
    float right = bar.bar.right;
    for (TabState& tab : bar.tabs)
    {
        if (tab_section(tab.flags) == 0)
        {
            tab.width = tab.content_width;
            tab.offset = left - bar.bar.left;
            left += tab.width + spacing;
        }
    }
    for (TabState& tab : std::views::reverse(bar.tabs))
    {
        if (tab_section(tab.flags) == 2)
        {
            tab.width = tab.content_width;
            right -= tab.width;
            tab.offset = right - bar.bar.left;
            right -= spacing;
        }
    }

    // The others: their widths (shrunk to fit unless they scroll), then their places in the area left.
    float total = 0;
    std::size_t count = 0;
    for (const TabState& tab : bar.tabs)
    {
        if (tab_section(tab.flags) == 1)
        {
            total += tab.content_width + (count > 0 ? spacing : 0.0F);
            ++count;
        }
    }
    float available = std::max(0.0F, right - left);
    const bool scrolling = has_any(bar.flags, TabBarFlag::FITTING_POLICY_SCROLL);
    float cap = std::numeric_limits<float>::max();
    if (total > available && scrolling && !has_any(bar.flags, TabBarFlag::NO_TAB_LIST_SCROLLING_BUTTONS))
    {
        available = std::max(0.0F, available - button_size * 2);
    }
    else if (total > available && !scrolling && count > 0)
    {
        // The widest tabs shrink first, to a common width, no narrower than MINIMUM_TAB_WIDTH.
        const float room = available - spacing * static_cast<float>(count - 1);
        float low = MINIMUM_TAB_WIDTH;
        float high = 0;
        for (const TabState& tab : bar.tabs)
        {
            high = tab_section(tab.flags) == 1 ? std::max(high, tab.content_width) : high;
        }
        for (int iteration = 0; iteration < 32 && high - low > 0.25F; ++iteration)
        {
            const float middle = (low + high) * 0.5F;
            float sum = 0;
            for (const TabState& tab : bar.tabs)
            {
                sum += tab_section(tab.flags) == 1 ? std::min(tab.content_width, middle) : 0.0F;
            }
            (sum > room ? high : low) = middle;
        }
        cap = std::max(MINIMUM_TAB_WIDTH, low);
    }
    float offset = 0;
    for (TabState& tab : bar.tabs)
    {
        if (tab_section(tab.flags) == 1)
        {
            tab.width = std::min(tab.content_width, cap);
            tab.offset = offset;
            offset += tab.width + spacing;
        }
    }
    bar.next_offset = offset;
    bar.scrolling = {.left = left, .top = bar.bar.top, .right = left + available, .bottom = bar.bar.bottom};

    // Scrolling: toward its target, within what the tabs need.
    const float maximum = std::max(0.0F, offset - spacing - available);
    if (bar.scroll_target >= 0)
    {
        bar.scroll = bar.scroll_target;
        bar.scroll_target = -1;
    }
    bar.scroll = scrolling ? std::clamp(bar.scroll, 0.0F, maximum) : 0.0F;
}

/// Scrolls so tab @p tab is in view.
void
scroll_to(TabBarState& bar,
          const TabState& tab) noexcept
{
    if (tab_section(tab.flags) != 1)
    {
        return;
    }
    const float width = bar.scrolling.width();
    if (tab.offset < bar.scroll)
    {
        bar.scroll_target = tab.offset;
    }
    else if (tab.offset + tab.width > bar.scroll + width)
    {
        bar.scroll_target = tab.offset + tab.width - width;
    }
}

/// A small button of the bar: an arrow, pressed on the mouse going down (repeating while held when @p repeat).
auto
bar_button(ContextState& state,
           WindowState& window,
           const UiRectangle& rectangle,
           std::uint32_t id,
           ArrowDirection direction,
           bool repeat) noexcept -> bool
{
    static_cast<void>(state.item_add(window, rectangle, id, false, "begin_tab_bar", {}));
    const ButtonState button = state.button_behavior(window, rectangle, id, repeat ? ButtonFlag::REPEAT : ButtonFlag::PRESS_ON_CLICK);
    DrawList& list = state.list_of(window);
    const StyleColor fill = button.held && button.hovered ? StyleColor::BUTTON_ACTIVE : button.hovered ? StyleColor::BUTTON_HOVERED : StyleColor::BUTTON;
    list.add_rectangle_filled(rectangle, state.color(fill), state.style.frame_rounding);
    const float size = std::min(rectangle.width(), rectangle.height()) * 0.8F;
    state.draw_arrow(list, {.x = rectangle.left + (rectangle.width() - size) * 0.5F, .y = rectangle.top + (rectangle.height() - size) * 0.5F}, size, direction,
                     state.color(StyleColor::TEXT_NORMAL));
    return button.pressed;
}

/// A tab: placed from last frame's layout (a new one after the others), its behavior and drawing. Returns whether
/// it is selected (tab_item_button(): whether it was pressed).
auto
tab_item(ContextState& state,
         UiFrame& frame,
         std::string_view label,
         bool* open,
         TabItemFlag flags,
         bool tab_button) noexcept -> bool
{
    TabBarState* bar_pointer = current_bar(state, tab_button ? "tab_item_button" : "begin_tab_item");
    if (bar_pointer == nullptr)
    {
        return false;
    }
    TabBarState& bar = *bar_pointer;
    WindowState& window = state.windows[bar.window];
    if (window.skip_items || (open != nullptr && !*open))
    {
        return false;
    }
    if (bar.content_tab != 0)
    {
        state.report(UiFrameProblem::UNBALANCED_STACK, "begin_tab_item", "begin_tab_item() inside a tab's content: call end_tab_item() first", label);
    }
    const Style& style = state.style;
    const FontMetrics& font = state.current_font();
    const std::uint32_t id = state.widget_id(label);

    // The tab of this identifier (a new one goes after the others of its section).
    TabState* tab = find_tab(bar, id);
    const bool appearing = tab == nullptr;
    if (appearing)
    {
        const int section = tab_section(flags);
        auto position = bar.tabs.end();
        if (section < 2)
        {
            position = std::ranges::find_if(bar.tabs, [section](const TabState& other) { return tab_section(other.flags) > section; });
        }
        try
        {
            state.allocations += bar.tabs.size() == bar.tabs.capacity() ? 1U : 0U;
            tab = &*bar.tabs.insert(position, TabState{.id = id, .flags = flags});
        }
        catch (...)
        {
            state.report(UiFrameProblem::CAPACITY_EXCEEDED, "begin_tab_item", "the tab could not be remembered", label);
            return false;
        }
    }
    tab->flags = flags;
    tab->last_frame = state.frame;
    tab->button = tab_button;
    const std::string_view text_label = ContextState::display_text(label);
    tab->label_size = static_cast<std::uint8_t>(std::min(text_label.size(), tab->label.size()));
    std::copy_n(text_label.begin(), tab->label_size, tab->label.begin());

    // Its width: the label, the padding, room for the close button (or the unsaved marker).
    const TextLayout text = state.layout_text(text_label, 0, "begin_tab_item");
    const bool closable = open != nullptr;
    const bool unsaved = has_any(flags, TabItemFlag::UNSAVED_DOCUMENT);
    const float close_size = font.line_height;
    tab->content_width = text.width + style.frame_padding.x * 2 + (closable || unsaved ? style.item_inner_spacing.x + close_size : 0.0F);
    if (appearing)
    {
        tab->width = tab->content_width;
        tab->offset = tab_section(flags) == 1 ? bar.next_offset : 0.0F;
        bar.next_offset += tab_section(flags) == 1 ? tab->width + style.item_inner_spacing.x : 0.0F;
    }

    // Selection: by code or as a new tab from the next frame; the first tab of a bar that has none at once.
    if (!tab_button)
    {
        if ((has_any(flags, TabItemFlag::SET_SELECTED) && bar.selected != id)
            || (appearing && !bar.appearing && has_any(bar.flags, TabBarFlag::AUTO_SELECT_NEW_TABS)))
        {
            bar.next_selected = id;
        }
        if (bar.selected == 0)
        {
            bar.selected = id;
        }
    }
    const bool selected = !tab_button && bar.selected == id;

    // Its rectangle: in the scrolling area, or at its fixed place.
    const bool middle = tab_section(flags) == 1;
    const float left = middle ? bar.scrolling.left + tab->offset - bar.scroll : bar.bar.left + tab->offset;
    const UiRectangle rectangle{.left = left, .top = bar.bar.top, .right = left + tab->width, .bottom = bar.bar.bottom};
    const UiRectangle clip = (middle ? bar.scrolling : bar.bar).intersection(window.clip);
    const UiRectangle visible = rectangle.intersection(clip);
    const UiRectangle close{.left = rectangle.right - style.frame_padding.x - close_size,
                            .top = rectangle.top + std::max(0.0F, (rectangle.height() - close_size) * 0.5F),
                            .right = rectangle.right - style.frame_padding.x,
                            .bottom = rectangle.top + std::max(0.0F, (rectangle.height() - close_size) * 0.5F) + close_size};
    const std::uint32_t close_id = ContextState::hash(id, "#CLOSE");
    const bool mouse_over = state.item_hoverable(window, visible, id);
    const bool close_visible = closable && (selected || mouse_over || state.active_id == close_id || state.hovered_id_previous == close_id);
    UiRectangle hit = visible;
    if (close_visible)
    {
        hit.right = std::min(hit.right, close.left);
    }
    if (!state.item_add(window, hit, id, true, "begin_tab_item", label))
    {
        if (selected && !has_any(flags, TabItemFlag::NO_PUSH_IDENTIFIER))
        {
            state.id_stack.push_back(id);
            bar.content_pushed = true;
        }
        bar.content_tab = selected ? id : 0;
        return selected;
    }

    // A press selects it from the next frame (a tab button is pressed when released over it); the middle button
    // closes it; a dragged payload held over it selects it.
    const ButtonState button = state.button_behavior(window, hit, id, tab_button ? ButtonFlag::NONE : ButtonFlag::PRESS_ON_CLICK);
    const bool hovered = button.hovered || state.hovered_id_previous == close_id;
    if (button.pressed && !tab_button)
    {
        bar.next_selected = id;
        scroll_to(bar, *tab);
    }
    const bool middle_closes = !has_any(flags, TabItemFlag::NO_CLOSE_WITH_MIDDLE_MOUSE_BUTTON) && !has_any(bar.flags, TabBarFlag::NO_CLOSE_WITH_MIDDLE_MOUSE_BUTTON);
    if (closable && middle_closes && mouse_over && state.mouse[std::to_underlying(MouseButton::MIDDLE)].clicked)
    {
        *open = false;
    }
    if (!selected && !tab_button && state.drag_drop.active && state.mouse_present && state.hovered_window == bar.window
        && contains(visible, state.mouse_position) && state.drag_hold_opens(id))
    {
        bar.next_selected = id;
    }

    // Dragged past the threshold: it moves among its section's reorderable tabs (end_tab_bar() places it).
    if (has_any(bar.flags, TabBarFlag::REORDERABLE) && middle && !has_any(flags, TabItemFlag::NO_REORDER) && state.active_id == id && state.mouse[0].down
        && state.mouse[0].drag_maximum >= state.description.input.drag_threshold)
    {
        bar.dragged = id;
    }

    // The tab (its top corners rounded), the overline of a selected tab, the label (cut before the close
    // button), the close button or the unsaved marker, the focus highlight.
    DrawList& list = state.list_of(window);
    list.push_clip_rectangle(clip);
    const StyleColor fill = selected ? StyleColor::TAB_SELECTED : hovered ? StyleColor::TAB_HOVERED : StyleColor::TAB;
    list.push_clip_rectangle({.left = rectangle.left, .top = rectangle.top, .right = rectangle.right, .bottom = rectangle.bottom});
    list.add_rectangle_filled({.left = rectangle.left, .top = rectangle.top, .right = rectangle.right, .bottom = rectangle.bottom + style.tab_rounding},
                              state.color(fill), style.tab_rounding);
    if (selected && has_any(bar.flags, TabBarFlag::DRAW_SELECTED_OVERLINE))
    {
        list.add_rectangle_filled({.left = rectangle.left + style.tab_rounding * 0.5F, .top = rectangle.top, .right = rectangle.right - style.tab_rounding * 0.5F,
                                   .bottom = rectangle.top + 1.0F},
                                  state.color(StyleColor::TAB_SELECTED_OVERLINE));
    }
    list.pop_clip_rectangle();
    const float text_right = (closable || unsaved) ? close.left - style.item_inner_spacing.x * 0.5F : rectangle.right - style.frame_padding.x;
    list.push_clip_rectangle({.left = rectangle.left, .top = rectangle.top, .right = std::max(rectangle.left, text_right), .bottom = rectangle.bottom});
    state.draw_text(list, text, {.x = rectangle.left + style.frame_padding.x, .y = rectangle.top + style.frame_padding.y}, state.color(StyleColor::TEXT_NORMAL));
    list.pop_clip_rectangle();
    if (unsaved && !close_visible)
    {
        list.add_circle_filled({.x = (close.left + close.right) * 0.5F, .y = (close.top + close.bottom) * 0.5F}, close_size * 0.2F, state.color(StyleColor::TEXT_NORMAL));
    }
    state.draw_focus(list, rectangle, id, style.tab_rounding);
    list.pop_clip_rectangle();
    if (close_visible)
    {
        static_cast<void>(state.item_add(window, close.intersection(clip), close_id, false, "begin_tab_item", label));
        const ButtonState close_button = state.button_behavior(window, close.intersection(clip), close_id);
        const Vector2 center{.x = (close.left + close.right) * 0.5F, .y = (close.top + close.bottom) * 0.5F};
        list.push_clip_rectangle(clip);
        if (close_button.hovered || close_button.held)
        {
            list.add_circle_filled(center, close_size * 0.5F, state.color(close_button.held ? StyleColor::BUTTON_ACTIVE : StyleColor::BUTTON_HOVERED));
        }
        const float arm = close_size * 0.5F * 0.7071F - 1.0F;
        list.add_line({.x = center.x - arm, .y = center.y - arm}, {.x = center.x + arm, .y = center.y + arm}, state.color(StyleColor::TEXT_NORMAL));
        list.add_line({.x = center.x + arm, .y = center.y - arm}, {.x = center.x - arm, .y = center.y + arm}, state.color(StyleColor::TEXT_NORMAL));
        list.pop_clip_rectangle();
        if (close_button.pressed)
        {
            *open = false;
        }
    }
    state.last_item = LastItem{.id = id, .rectangle = visible, .window = bar.window, .hovered = button.hovered, .visible = true, .disabled = state.disabled};

    // A cut label shows in a tooltip.
    const bool cut = tab->width + 0.5F < tab->content_width;
    if (cut && !has_any(flags, TabItemFlag::NO_TOOLTIP) && !has_any(bar.flags, TabBarFlag::NO_TOOLTIP) && state.drag_drop.active == false)
    {
        frame.item_tooltip(text_label);
        state.last_item = LastItem{.id = id, .rectangle = visible, .window = bar.window, .hovered = button.hovered, .visible = true, .disabled = state.disabled};
    }

    // Selected: its content follows, under its identifier.
    if (tab_button)
    {
        return button.pressed;
    }
    if (selected)
    {
        bar.content_tab = id;
        bar.content_pushed = !has_any(flags, TabItemFlag::NO_PUSH_IDENTIFIER);
        if (bar.content_pushed)
        {
            state.id_stack.push_back(id);
        }
    }
    return selected;
}

} // namespace

auto
begin_tab_bar_at(ContextState& state,
                 WindowState& window,
                 std::uint32_t bar_id,
                 const UiRectangle& rectangle,
                 TabBarFlag flags,
                 std::string_view operation,
                 std::string_view id) noexcept -> TabBarState*
{
    // The bar of this identifier (made the first time).
    std::uint32_t index = state.tab_bar_lookup.find(bar_id).value_or(ContextState::NO_WINDOW);
    if (index == ContextState::NO_WINDOW)
    {
        try
        {
            state.allocations += state.tab_bars.size() == state.tab_bars.capacity() ? 1U : 0U;
            index = static_cast<std::uint32_t>(state.tab_bars.size());
            state.tab_bars.push_back(TabBarState{.id = bar_id});
            state.tab_bars.back().tabs.reserve(16);
            state.allocations += state.tab_bar_lookup.insert(bar_id, index) ? 1U : 0U;
        }
        catch (...)
        {
            state.report(UiFrameProblem::CAPACITY_EXCEEDED, operation, "the tab bar could not be remembered", id, bar_id);
            return nullptr;
        }
    }
    TabBarState& bar = state.tab_bars[index];
    if (bar.last_frame == state.frame)
    {
        state.report(UiFrameProblem::IDENTIFIER_CONFLICT, operation, "a tab bar was begun twice in a frame: give each its own identifier", id, bar_id);
    }
    bar.appearing = bar.last_frame == 0 || bar.last_frame + 1 < state.frame;
    bar.previous_frame = bar.last_frame;
    bar.last_frame = state.frame;
    bar.flags = flags;
    bar.window = static_cast<std::uint32_t>(&window - state.windows.data());
    bar.content_tab = 0;
    bar.content_pushed = false;
    bar.dragged = 0;

    // Tabs not submitted the last time the bar was are gone; a selected one gives the selection to its neighbor.
    for (std::size_t tab = 0; tab < bar.tabs.size();)
    {
        if (bar.tabs[tab].last_frame >= bar.previous_frame)
        {
            ++tab;
            continue;
        }
        if (bar.tabs[tab].id == bar.selected)
        {
            const std::size_t neighbor = tab + 1 < bar.tabs.size() ? tab + 1 : tab > 0 ? tab - 1 : bar.tabs.size();
            bar.selected = neighbor < bar.tabs.size() && !bar.tabs[neighbor].button && bar.tabs[neighbor].last_frame >= bar.previous_frame ? bar.tabs[neighbor].id : 0;
        }
        bar.tabs.erase(bar.tabs.begin() + static_cast<std::ptrdiff_t>(tab));
    }

    // The selection asked for last frame.
    if (bar.next_selected != 0 && find_tab(bar, bar.next_selected) != nullptr)
    {
        bar.selected = bar.next_selected;
    }
    bar.next_selected = 0;
    if (bar.selected != 0 && find_tab(bar, bar.selected) == nullptr)
    {
        bar.selected = 0;
    }
    if (bar.selected == 0)
    {
        const auto first = std::ranges::find_if(bar.tabs, [](const TabState& tab) { return !tab.button; });
        bar.selected = first == bar.tabs.end() ? 0 : first->id;
    }

    // The tabs laid out from last frame's.
    bar.bar = rectangle;
    layout_tabs(state, bar, rectangle.height());
    if (TabState* selected = find_tab(bar, bar.selected); selected != nullptr && has_any(flags, TabBarFlag::FITTING_POLICY_SCROLL) && bar.previous_frame + 1 == state.frame)
    {
        scroll_to(bar, *selected);
    }
    try
    {
        state.tab_bar_stack.push_back(index);
    }
    catch (...)
    {
        state.report(UiFrameProblem::CAPACITY_EXCEEDED, operation, "the tab bar stack could not grow", id, bar_id);
        return nullptr;
    }

    // The tabs are identified under the bar (and whatever push_id() adds inside it).
    state.id_stack.push_back(bar_id);
    return &state.tab_bars[index];
}

auto
UiFrame::begin_tab_bar(std::string_view id,
                       TabBarFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("begin_tab_bar");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }

    // The bar: a frame's height across the content width.
    const Style& style = state.style;
    const float height = state.current_font().line_height + style.frame_padding.y * 2;
    const UiRectangle rectangle{.left = window->cursor.x, .top = window->cursor.y, .right = state.content_right(*window), .bottom = window->cursor.y + height};
    state.item_size(*window, {.x = rectangle.width(), .y = height}, style.frame_padding.y);
    static_cast<void>(state.item_add(*window, rectangle, 0, false, "begin_tab_bar", {}));
    return begin_tab_bar_at(state, *window, state.widget_id(id), rectangle, flags, "begin_tab_bar", id) != nullptr;
}

void
UiFrame::end_tab_bar() noexcept
{
    ContextState& state = *m_state;
    TabBarState* bar_pointer = current_bar(state, "end_tab_bar");
    if (bar_pointer == nullptr)
    {
        return;
    }
    TabBarState& bar = *bar_pointer;
    WindowState& window = state.windows[bar.window];
    if (bar.content_tab != 0)
    {
        state.report(UiFrameProblem::UNBALANCED_STACK, "end_tab_bar", "begin_tab_item() returned true without end_tab_item()");
        end_tab_item();
    }
    const Style& style = state.style;
    DrawList& list = state.list_of(window);
    const float height = bar.bar.height();

    // A dragged tab moves to where the mouse is among the reorderable tabs of its section.
    if (bar.dragged != 0)
    {
        const auto from = std::ranges::find(bar.tabs, bar.dragged, &TabState::id);
        const float x = state.mouse_position.x - bar.scrolling.left + bar.scroll;
        const auto to = std::ranges::find_if(bar.tabs, [x](const TabState& tab)
        {
            return tab_section(tab.flags) == 1 && !has_any(tab.flags, TabItemFlag::NO_REORDER) && x >= tab.offset && x < tab.offset + tab.width;
        });
        if (from != bar.tabs.end() && to != bar.tabs.end() && from != to)
        {
            const TabState moved = *from;
            const auto target = static_cast<std::ptrdiff_t>(to - bar.tabs.begin());
            bar.tabs.erase(from);
            bar.tabs.insert(bar.tabs.begin() + target, moved);
        }
    }

    // The line under the tabs, the scrolling arrows and the tab list.
    list.add_rectangle_filled({.left = bar.bar.left, .top = bar.bar.bottom - 1.0F, .right = bar.bar.right, .bottom = bar.bar.bottom},
                              state.color(StyleColor::TAB_SELECTED));
    const float tabs_width = bar.next_offset - style.item_inner_spacing.x;
    if (has_any(bar.flags, TabBarFlag::FITTING_POLICY_SCROLL) && !has_any(bar.flags, TabBarFlag::NO_TAB_LIST_SCROLLING_BUTTONS) && tabs_width > bar.scrolling.width())
    {
        const float step = std::max(bar.scrolling.width() * 0.33F, MINIMUM_TAB_WIDTH);
        const UiRectangle left{.left = bar.scrolling.right, .top = bar.bar.top, .right = bar.scrolling.right + height, .bottom = bar.bar.bottom};
        const UiRectangle right{.left = left.right, .top = bar.bar.top, .right = left.right + height, .bottom = bar.bar.bottom};
        if (bar_button(state, window, left, ContextState::hash(bar.id, "#SCROLL_LEFT"), ArrowDirection::LEFT, true))
        {
            bar.scroll_target = std::max(0.0F, bar.scroll - step);
        }
        if (bar_button(state, window, right, ContextState::hash(bar.id, "#SCROLL_RIGHT"), ArrowDirection::RIGHT, true))
        {
            bar.scroll_target = bar.scroll + step;
        }
    }
    if (has_any(bar.flags, TabBarFlag::TAB_LIST_POPUP_BUTTON))
    {
        const UiRectangle button{.left = bar.bar.left, .top = bar.bar.top, .right = bar.bar.left + height, .bottom = bar.bar.bottom};
        const std::uint32_t popup = ContextState::hash(bar.id, "#TAB_LIST");
        const std::uint32_t button_id = ContextState::hash(bar.id, "#TAB_LIST_BUTTON");
        if (bar_button(state, window, button, button_id, ArrowDirection::DOWN, false) && !state.popup_open(popup)
            && !std::ranges::contains(state.popups_closed_by_click, popup))
        {
            if (PopupState* opened = state.open_popup(popup, {.x = button.left, .y = button.bottom}, 0, 0, button.top))
            {
                opened->opener_item = button_id;
            }
        }
        if (state.begin_popup(*this, popup, UiWindowFlag::NONE))
        {
            std::int32_t row = 0;
            for (const TabState& tab : bar.tabs)
            {
                if (tab.button)
                {
                    continue;
                }
                push_id(row++);
                if (selectable({tab.label.data(), tab.label_size}, tab.id == bar.selected))
                {
                    bar.next_selected = tab.id;
                    scroll_to(bar, tab);
                }
                pop_id();
            }
            state.end_popup(*this);
        }
    }
    if (!state.id_stack.empty() && state.id_stack.back() == bar.id)
    {
        state.id_stack.pop_back();
    }
    else
    {
        state.report(UiFrameProblem::UNBALANCED_STACK, "end_tab_bar", "push_id() without pop_id() inside the tab bar");
    }
    state.tab_bar_stack.pop_back();
}

auto
UiFrame::begin_tab_item(std::string_view label,
                        TabItemFlag flags) noexcept -> bool
{
    return tab_item(*m_state, *this, label, nullptr, flags, false);
}

auto
UiFrame::begin_tab_item(std::string_view label,
                        bool& open,
                        TabItemFlag flags) noexcept -> bool
{
    return tab_item(*m_state, *this, label, &open, flags, false);
}

void
UiFrame::end_tab_item() noexcept
{
    ContextState& state = *m_state;
    TabBarState* bar = current_bar(state, "end_tab_item");
    if (bar == nullptr)
    {
        return;
    }
    if (bar->content_tab == 0)
    {
        state.report(UiFrameProblem::END_WITHOUT_BEGIN, "end_tab_item", "end_tab_item() without a begin_tab_item() that returned true");
        return;
    }
    if (bar->content_pushed && !state.id_stack.empty() && state.id_stack.back() == bar->content_tab)
    {
        state.id_stack.pop_back();
    }
    bar->content_tab = 0;
    bar->content_pushed = false;
}

auto
UiFrame::tab_item_button(std::string_view label,
                         TabItemFlag flags) noexcept -> bool
{
    return tab_item(*m_state, *this, label, nullptr, flags, true);
}

} // namespace gfx::ui
