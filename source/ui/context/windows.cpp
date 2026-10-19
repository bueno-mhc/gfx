/// @file windows.cpp
/// @brief UiFrame's windows: begin_window() / end_window() (title bar, collapse and close buttons, moving,
///        resizing, auto-fit, scrollbars, wheel scrolling), begin_child() / end_child(), the next-window settings,
///        window queries and scrolling.
module gfx.ui.context;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.handles;
import gfx.ui.draw;
import gfx.ui.style;
import gfx.ui.text;
import :state;

namespace gfx::ui
{

namespace
{

/// Bits of WindowState::used_once.
constexpr std::uint8_t ONCE_POSITION = 1U << 0;
constexpr std::uint8_t ONCE_SIZE = 1U << 1;
constexpr std::uint8_t ONCE_COLLAPSED = 1U << 2;
constexpr std::uint8_t ONCE_DOCK = 1U << 3;

/// What a docked window cannot do itself (its node places it, its tab shows its title).
constexpr UiWindowFlag DOCKED_WINDOW_FLAGS = UiWindowFlag::NO_TITLE_BAR | UiWindowFlag::NO_RESIZE | UiWindowFlag::NO_MOVE | UiWindowFlag::NO_COLLAPSE;

/// Whether @p rectangle contains @p point.
constexpr auto
contains(const UiRectangle& rectangle,
         Vector2 point) noexcept -> bool
{
    return point.x >= rectangle.left && point.x < rectangle.right && point.y >= rectangle.top && point.y < rectangle.bottom;
}

/// Whether a setting of @p condition applies to @p window now (and marks a ONCE setting used).
auto
applies(UiCondition condition,
        WindowState& window,
        std::uint8_t once_bit) noexcept -> bool
{
    switch (condition)
    {
        case UiCondition::ALWAYS:
            return true;
        case UiCondition::ONCE:
            if ((window.used_once & once_bit) != 0)
            {
                return false;
            }
            window.used_once = static_cast<std::uint8_t>(window.used_once | once_bit);
            return true;
        case UiCondition::FIRST_USE_EVER:
            return window.first_use;
        case UiCondition::APPEARING:
            return window.appearing;
    }
    return false;
}

/// One scrollbar's rectangles and state.
struct Scrollbar
{
    bool shown = false;        ///< The window has it this frame.
    UiRectangle track{};       ///< Its track.
    UiRectangle grab{};        ///< Its grab.
    ButtonState state{};       ///< Mouse state of the track.
};

/// What a window draws around its content.
struct Decorations
{
    UiRectangle title{};       ///< Title bar.
    UiRectangle collapse{};    ///< Collapse button (empty: none).
    ButtonState collapse_state{};
    UiRectangle close{};       ///< Close button (empty: none).
    ButtonState close_state{};
    UiRectangle grip{};        ///< Resize grip (empty: none).
    ButtonState grip_state{};
    Scrollbar vertical{};      ///< Vertical scrollbar.
    Scrollbar horizontal{};    ///< Horizontal scrollbar.
};

/// The size that fits @p window's content (measured last frame), within its limits.
auto
fit_size(const ContextState& state,
         const WindowState& window) noexcept -> Vector2
{
    const Vector2 content = window.explicit_content_size.x > 0 || window.explicit_content_size.y > 0 ? window.explicit_content_size : window.content_size;
    const float scrollbar = window.scrollbar_y ? state.style.scrollbar_size : 0.0F;
    Vector2 size{.x = content.x + window.padding.x * 2 + scrollbar, .y = content.y + window.padding.y * 2 + window.title_bar_height + window.menu_bar_height};
    size.x = std::max(size.x, state.style.window_minimum_size.x);
    size.y = std::max(size.y, std::max(state.style.window_minimum_size.y, window.title_bar_height));
    if (!window.child && state.display_size.x > 0)
    {
        size.x = std::min(size.x, std::max(state.display_size.x - state.style.window_padding.x * 2, state.style.window_minimum_size.x));
        size.y = std::min(size.y, std::max(state.display_size.y - state.style.window_padding.y * 2, state.style.window_minimum_size.y));
    }
    return size;
}

/// @p size within @p window's minimum and maximum sizes.
auto
constrained(const ContextState& state,
            const WindowState& window,
            Vector2 size) noexcept -> Vector2
{
    const Vector2 minimum{.x = std::max(state.style.window_minimum_size.x, window.minimum_size.x),
                          .y = std::max(state.style.window_minimum_size.y, window.minimum_size.y)};
    size.x = std::max(size.x, minimum.x);
    size.y = std::max(size.y, minimum.y);
    if (window.maximum_size.x > 0)
    {
        size.x = std::min(size.x, std::max(window.maximum_size.x, minimum.x));
    }
    if (window.maximum_size.y > 0)
    {
        size.y = std::min(size.y, std::max(window.maximum_size.y, minimum.y));
    }
    return size;
}

/// Applies the set_next_window_*() settings to @p window.
void
apply_next_window(ContextState& state,
                  WindowState& window,
                  std::uint32_t index) noexcept
{
    NextWindowSettings& next = state.next_window;
    if (next.has_constraints)
    {
        window.minimum_size = next.minimum;
        window.maximum_size = next.maximum;
    }
    else
    {
        window.minimum_size = {};
        window.maximum_size = {};
    }
    if (next.has_size && applies(next.size_condition, window, ONCE_SIZE))
    {
        window.size = next.size;
        window.auto_fit = next.size.x <= 0 || next.size.y <= 0;
    }
    if (next.has_position && applies(next.position_condition, window, ONCE_POSITION))
    {
        // The pivot needs the size: a window fitted to its content uses last frame's fit.
        const Vector2 size = window.size.x > 0 && window.size.y > 0 ? window.size : fit_size(state, window);
        window.position = {.x = next.position.x - size.x * next.pivot.x, .y = next.position.y - size.y * next.pivot.y};
    }
    if (next.has_collapsed && applies(next.collapsed_condition, window, ONCE_COLLAPSED))
    {
        window.collapsed = next.collapsed;
    }
    window.explicit_content_size = next.has_content_size ? next.content_size : Vector2{};
    if (next.has_scroll)
    {
        window.scroll_target = next.scroll;
    }
    window.background_alpha = next.has_background_alpha ? next.background_alpha : 1.0F;
    if (next.focus)
    {
        state.focus_window(index);
    }
    next = NextWindowSettings{};
}

/// Scrollbars, content area, scrolling (targets, wheel, dragging) and the content clip of @p window, whose outer
/// rectangle and title bar are known.
void
layout_area(ContextState& state,
            WindowState& window,
            std::uint32_t index,
            Decorations& decorations,
            const UiRectangle& parent_clip) noexcept
{
    const Style& style = state.style;
    const float bar = style.scrollbar_size;
    const UiRectangle& outer = window.outer;
    const Vector2 content = window.explicit_content_size.x > 0 || window.explicit_content_size.y > 0 ? window.explicit_content_size : window.content_size;

    // Scrollbars from last frame's content (a horizontal one leaves less height, which may need a vertical one).
    const bool allowed = !window.collapsed && !has_any(window.flags, UiWindowFlag::NO_SCROLLBAR);
    const float bars = window.title_bar_height + window.menu_bar_height;
    const float height = outer.height() - bars - window.padding.y * 2;
    const float width = outer.width() - window.padding.x * 2;
    bool vertical = allowed && (has_any(window.flags, UiWindowFlag::ALWAYS_VERTICAL_SCROLLBAR) || content.y > height);
    const bool horizontal = allowed && (has_any(window.flags, UiWindowFlag::ALWAYS_HORIZONTAL_SCROLLBAR)
                                        || (has_any(window.flags, UiWindowFlag::HORIZONTAL_SCROLLBAR) && content.x > width - (vertical ? bar : 0.0F)));
    vertical = vertical || (allowed && horizontal && content.y > height - bar);
    window.scrollbar_x = horizontal;
    window.scrollbar_y = vertical;

    // The content area, and how far it scrolls.
    window.inner = {.left = outer.left,
                    .top = std::min(outer.top + bars, outer.bottom),
                    .right = std::max(outer.left, outer.right - (vertical ? bar : 0.0F)),
                    .bottom = std::max(std::min(outer.top + bars, outer.bottom), outer.bottom - (horizontal ? bar : 0.0F))};
    window.scroll_maximum = {.x = std::max(0.0F, content.x + window.padding.x * 2 - window.inner.width()),
                             .y = std::max(0.0F, content.y + window.padding.y * 2 - window.inner.height())};

    // Scrolling asked for by code, then by the wheel (Shift: horizontally).
    if (window.scroll_target.x >= 0)
    {
        window.scroll.x = window.scroll_target.x;
    }
    if (window.scroll_target.y >= 0)
    {
        window.scroll.y = window.scroll_target.y;
    }
    window.scroll_target = {.x = -1.0F, .y = -1.0F};
    if (state.wheel_window == index && !window.collapsed && !has_any(window.flags, UiWindowFlag::NO_SCROLL_WITH_MOUSE))
    {
        const float step = std::floor(std::min(5.0F * state.current_font().line_height, window.inner.height() * 0.67F));
        const bool sideways = has_any(state.modifiers, KeyModifier::SHIFT);
        const float wheel_x = sideways ? state.mouse_wheel.y : state.mouse_wheel.x;
        const float wheel_y = sideways ? 0.0F : state.mouse_wheel.y;
        window.scroll.x -= wheel_x * step;
        window.scroll.y -= wheel_y * step;
    }

    // Each scrollbar: its grab follows the scroll; dragging it (or clicking its track) scrolls.
    const auto scrollbar = [&](Scrollbar& result, bool along_y)
    {
        result.shown = true;
        result.track = along_y ? UiRectangle{.left = window.inner.right, .top = window.inner.top, .right = outer.right, .bottom = window.inner.bottom}
                               : UiRectangle{.left = outer.left, .top = window.inner.bottom, .right = window.inner.right, .bottom = outer.bottom};

        // A track alone on its side ends before the resize grip, so its grab can always be reached.
        if (!decorations.grip.empty() && along_y && !horizontal)
        {
            result.track.bottom = std::max(result.track.top, std::min(result.track.bottom, decorations.grip.top));
        }
        if (!decorations.grip.empty() && !along_y && !vertical)
        {
            result.track.right = std::max(result.track.left, std::min(result.track.right, decorations.grip.left));
        }
        const float track_length = along_y ? result.track.height() : result.track.width();
        const float visible = along_y ? window.inner.height() : window.inner.width();
        const float maximum = along_y ? window.scroll_maximum.y : window.scroll_maximum.x;
        float& scroll = along_y ? window.scroll.y : window.scroll.x;
        scroll = std::clamp(scroll, 0.0F, maximum);
        const float grab_length = std::clamp(track_length * visible / std::max(visible + maximum, 1.0F), std::min(style.grab_minimum_size, track_length), track_length);
        const float travel = track_length - grab_length;
        const std::uint32_t id = ContextState::hash(window.id, along_y ? "#SCROLLY" : "#SCROLLX");
        result.state = state.button_behavior(window, result.track, id);
        const float mouse = along_y ? state.mouse_position.y : state.mouse_position.x;
        const float start = along_y ? result.track.top : result.track.left;
        if (state.active_id == id && state.mouse[0].clicked)
        {
            // Grabbed where it was clicked; a click on the track centers the grab under the mouse.
            const float grab_start = start + (maximum > 0 ? scroll / maximum * travel : 0.0F);
            const bool on_grab = mouse >= grab_start && mouse < grab_start + grab_length;
            state.active_offset = {.x = 0.0F, .y = on_grab ? mouse - grab_start : grab_length * 0.5F};
        }
        if (result.state.held && travel > 0)
        {
            scroll = std::clamp((mouse - state.active_offset.y - start) / travel, 0.0F, 1.0F) * maximum;
        }
        const float grab_start = start + (maximum > 0 ? scroll / maximum * travel : 0.0F);
        result.grab = along_y ? UiRectangle{.left = result.track.left + 2, .top = grab_start + 2, .right = result.track.right - 2, .bottom = grab_start + grab_length - 2}
                              : UiRectangle{.left = grab_start + 2, .top = result.track.top + 2, .right = grab_start + grab_length - 2, .bottom = result.track.bottom - 2};
    };
    if (vertical)
    {
        scrollbar(decorations.vertical, true);
    }
    if (horizontal)
    {
        scrollbar(decorations.horizontal, false);
    }
    window.scroll.x = std::clamp(window.scroll.x, 0.0F, window.scroll_maximum.x);
    window.scroll.y = std::clamp(window.scroll.y, 0.0F, window.scroll_maximum.y);

    // The content's clip, and where the layout starts.
    window.clip = window.inner.intersection(parent_clip);
    window.cursor_start = {.x = window.inner.left + window.padding.x - window.scroll.x, .y = window.inner.top + window.padding.y - window.scroll.y};
    window.cursor = window.cursor_start;
    window.cursor_maximum = window.cursor_start;
    window.cursor_previous_line = window.cursor_start;
    window.line_height = 0;
    window.previous_line_height = 0;
    window.line_text_offset = 0;
    window.previous_line_text_offset = 0;
    window.indent = 0;
    window.columns = ColumnsState{};
    window.item_width = std::floor(std::max(1.0F, (window.inner.width() - window.padding.x * 2) * DEFAULT_ITEM_WIDTH_FRACTION));
}

/// Which edges of a resizable child region the mouse is over or drags.
struct ResizeEdges
{
    bool right_hovered = false;
    bool right_held = false;
    bool bottom_hovered = false;
    bool bottom_held = false;
};

/// The right (RESIZE_X) and bottom (RESIZE_Y) edges of child region @p child: dragging one resizes it (the size is
/// kept for the next frames), a double click fits it to its content. The edge reaches both sides of the border, so
/// the child and its parent window both let the mouse take it.
auto
resize_child_edges(ContextState& state,
                   WindowState& child,
                   std::uint32_t index,
                   std::uint32_t parent,
                   bool along_x,
                   bool along_y) noexcept -> ResizeEdges
{
    ResizeEdges edges{};
    const WindowState& owner = state.windows[parent];
    for (const bool horizontal : {true, false})
    {
        if (horizontal ? !along_x : !along_y)
        {
            continue;
        }
        const float edge = horizontal ? child.outer.right : child.outer.bottom;
        const UiRectangle hit = horizontal ? UiRectangle{.left = edge - RESIZE_HANDLE_HALF_WIDTH, .top = child.outer.top, .right = edge + RESIZE_HANDLE_HALF_WIDTH,
                                                         .bottom = child.outer.bottom}
                                           : UiRectangle{.left = child.outer.left, .top = edge - RESIZE_HANDLE_HALF_WIDTH, .right = child.outer.right,
                                                         .bottom = edge + RESIZE_HANDLE_HALF_WIDTH};
        const std::uint32_t id = ContextState::hash(child.id, horizontal ? "#RESIZE_X" : "#RESIZE_Y");
        const bool over = state.mouse_present && !state.disabled && !child.hidden && state.moving_window == ContextState::NO_WINDOW
                          && (state.hovered_window == index || state.hovered_window == parent) && contains(hit.intersection(owner.clip), state.mouse_position)
                          && (state.active_id == 0 || state.active_id == id);
        bool held = false;
        if (over)
        {
            state.hovered_id = id;
            if (state.mouse[0].clicked && state.active_id != id)
            {
                state.set_active(id, owner);
                state.active_button = MouseButton::LEFT;
                state.active_offset = {.x = edge - state.mouse_position.x, .y = edge - state.mouse_position.y};
            }
        }
        if (state.active_id == id)
        {
            state.active_id_alive = true;
            if (state.mouse[0].down)
            {
                held = true;
                const float start = horizontal ? child.outer.left : child.outer.top;
                const float moved = horizontal ? state.mouse_position.x + state.active_offset.x : state.mouse_position.y + state.active_offset.y;
                const float padding = horizontal ? child.padding.x : child.padding.y;
                const float size = std::max(moved - start, padding * 2 + 4.0F);
                (horizontal ? child.resized_size.x : child.resized_size.y) = size;
                (horizontal ? child.size.x : child.size.y) = size;
            }
            else
            {
                state.clear_active();
            }
        }
        if (over && state.mouse[0].double_clicked)
        {
            const float fitted = horizontal ? child.content_size.x + child.padding.x * 2 : child.content_size.y + child.padding.y * 2;
            (horizontal ? child.resized_size.x : child.resized_size.y) = std::max(fitted, 4.0F);
            (horizontal ? child.size.x : child.size.y) = std::max(fitted, 4.0F);
        }
        if (over || held)
        {
            state.cursor = horizontal ? CursorShape::RESIZE_EAST_WEST : CursorShape::RESIZE_NORTH_SOUTH;
        }
        (horizontal ? edges.right_hovered : edges.bottom_hovered) = over;
        (horizontal ? edges.right_held : edges.bottom_held) = held;
    }
    child.outer = {.left = child.position.x, .top = child.position.y, .right = child.position.x + child.size.x, .bottom = child.position.y + child.size.y};
    return edges;
}

/// The color of a part in state @p button (normal, hovered, held).
auto
part_color(const ContextState& state,
           const ButtonState& button,
           StyleColor normal,
           StyleColor hovered,
           StyleColor held) noexcept -> Color
{
    return state.color(button.held ? held : button.hovered ? hovered : normal);
}

/// Draws a window's or child's frame: background, title bar, border, buttons, title, grip and scrollbars.
void
draw_decorations(ContextState& state,
                 WindowState& window,
                 std::uint32_t index,
                 const Decorations& decorations,
                 StyleColor background,
                 float rounding,
                 float border,
                 DrawList* front = nullptr) noexcept
{
    DrawList& list = state.list_of(window);
    DrawList& over = front != nullptr ? *front : list;
    const Style& style = state.style;
    const UiRectangle& outer = window.outer;
    const bool decorated = !has_any(window.flags, UiWindowFlag::NO_BACKGROUND);

    // Background (a collapsed window is its title bar), then the title bar over its top.
    if (window.collapsed)
    {
        list.add_rectangle_filled(decorations.title, state.color(StyleColor::TITLE_BACKGROUND_COLLAPSED), rounding);
    }
    else if (decorated)
    {
        const Color fill = state.color(background);
        list.add_rectangle_filled(outer, fill.with_alpha(fill.alpha * window.background_alpha), rounding);
    }
    if (!window.collapsed && window.title_bar_height > 0)
    {
        const StyleColor title = state.title_focus_window() == index ? StyleColor::TITLE_BACKGROUND_ACTIVE : StyleColor::TITLE_BACKGROUND;
        list.push_clip_rectangle(decorations.title);
        list.add_rectangle_filled({.left = outer.left, .top = outer.top, .right = outer.right, .bottom = decorations.title.bottom + rounding},
                                  state.color(title), rounding);
        list.pop_clip_rectangle();
    }
    if (!window.collapsed && window.menu_bar_height > 0)
    {
        // The menu bar under the title bar.
        const UiRectangle menu_bar{.left = outer.left, .top = outer.top + window.title_bar_height, .right = outer.right,
                                   .bottom = std::min(outer.bottom, outer.top + window.title_bar_height + window.menu_bar_height)};
        list.push_clip_rectangle(menu_bar);
        list.add_rectangle_filled({.left = outer.left, .top = window.title_bar_height > 0 ? menu_bar.top : outer.top, .right = outer.right,
                                   .bottom = menu_bar.bottom + rounding},
                                  state.color(StyleColor::MENU_BAR_BACKGROUND), window.title_bar_height > 0 ? 0.0F : rounding);
        list.pop_clip_rectangle();
    }
    if (border > 0 && (decorated || window.collapsed))
    {
        over.add_rectangle(window.collapsed ? decorations.title : outer, state.color(StyleColor::BORDER), border, rounding);
    }

    // Scrollbars: their tracks and grabs.
    for (const Scrollbar* bar : {&decorations.vertical, &decorations.horizontal})
    {
        if (!bar->shown)
        {
            continue;
        }
        list.add_rectangle_filled(bar->track, state.color(StyleColor::SCROLLBAR_BACKGROUND));
        const float grab_rounding = std::min(style.scrollbar_rounding, std::min(bar->grab.width(), bar->grab.height()) * 0.5F);
        list.add_rectangle_filled(bar->grab, part_color(state, bar->state, StyleColor::SCROLLBAR_GRAB, StyleColor::SCROLLBAR_GRAB_HOVERED,
                                                        StyleColor::SCROLLBAR_GRAB_ACTIVE), grab_rounding);
    }

    // The resize grip: a triangle in the bottom-right corner.
    if (!decorations.grip.empty())
    {
        const UiRectangle& grip = decorations.grip;
        const float inset = std::max(border, 1.0F);
        const std::array points{Vector2{.x = grip.right - inset, .y = grip.top}, Vector2{.x = grip.right - inset, .y = grip.bottom - inset},
                                Vector2{.x = grip.left, .y = grip.bottom - inset}};
        over.add_convex_polygon_filled(points, part_color(state, decorations.grip_state, StyleColor::RESIZE_GRIP, StyleColor::RESIZE_GRIP_HOVERED,
                                                          StyleColor::RESIZE_GRIP_ACTIVE));
    }

    // Title bar buttons and the title.
    if (window.title_bar_height <= 0)
    {
        return;
    }
    const auto button_background = [&](const UiRectangle& rectangle, const ButtonState& button)
    {
        if (button.hovered || button.held)
        {
            const Vector2 center{.x = (rectangle.left + rectangle.right) * 0.5F, .y = (rectangle.top + rectangle.bottom) * 0.5F};
            list.add_circle_filled(center, rectangle.width() * 0.5F, state.color(button.held ? StyleColor::BUTTON_ACTIVE : StyleColor::BUTTON_HOVERED));
        }
    };
    const Color text = state.color(StyleColor::TEXT_NORMAL);
    if (!decorations.collapse.empty())
    {
        button_background(decorations.collapse, decorations.collapse_state);
        state.draw_arrow(list, {.x = decorations.collapse.left, .y = decorations.collapse.top}, decorations.collapse.width(),
                         window.collapsed ? ArrowDirection::RIGHT : ArrowDirection::DOWN, text);
    }
    if (!decorations.close.empty())
    {
        button_background(decorations.close, decorations.close_state);
        const UiRectangle& close = decorations.close;
        const Vector2 center{.x = (close.left + close.right) * 0.5F, .y = (close.top + close.bottom) * 0.5F};
        const float arm = close.width() * 0.5F * 0.7071F - 1.0F;
        list.add_line({.x = center.x - arm, .y = center.y - arm}, {.x = center.x + arm, .y = center.y + arm}, text);
        list.add_line({.x = center.x + arm, .y = center.y - arm}, {.x = center.x - arm, .y = center.y + arm}, text);
    }
    const float text_left = decorations.collapse.empty() ? decorations.title.left + style.frame_padding.x
                                                         : decorations.collapse.right + style.item_inner_spacing.x;
    const float text_right = (decorations.close.empty() ? decorations.title.right : decorations.close.left) - style.frame_padding.x;
    if (text_right <= text_left)
    {
        return;
    }
    const TextLayout title = state.layout_text(window.name, 0, "begin_window");
    const float unsaved = has_any(window.flags, UiWindowFlag::UNSAVED_DOCUMENT) ? state.current_font().size * 0.8F : 0.0F;
    const float x = text_left + std::max(0.0F, text_right - text_left - title.width - unsaved) * style.window_title_align.x;
    const float y = decorations.title.top + std::max(0.0F, decorations.title.height() - title.height) * style.window_title_align.y;
    list.push_clip_rectangle({.left = text_left, .top = decorations.title.top, .right = text_right, .bottom = decorations.title.bottom});
    state.draw_text(list, title, {.x = x, .y = y}, text);
    if (unsaved > 0)
    {
        list.add_circle_filled({.x = x + title.width + unsaved * 0.6F, .y = decorations.title.top + decorations.title.height() * 0.5F}, unsaved * 0.25F, text);
    }
    list.pop_clip_rectangle();
}

/// Ends @p window's content: columns, clip, identifiers, measured content size, auto-fit.
void
finish_content(ContextState& state,
               UiFrame& frame,
               WindowState& window) noexcept
{
    if (window.columns.active)
    {
        frame.columns(1);
    }
    if (!window.skip_items)
    {
        window.content_size = {.x = std::max(0.0F, window.cursor_maximum.x - window.cursor_start.x),
                               .y = std::max(0.0F, window.cursor_maximum.y - window.cursor_start.y)};
        window.menu_columns = window.next_menu_columns;
    }
    if (window.identifier_stack_base == 0 || state.id_stack.size() < window.identifier_stack_base)
    {
        state.id_stack.clear();
    }
    else
    {
        if (state.id_stack.size() > window.identifier_stack_base)
        {
            state.report(UiFrameProblem::UNBALANCED_STACK, "end_window", "push_id() without pop_id() in this window", window.name, window.id);
        }
        state.id_stack.resize(window.identifier_stack_base - 1);
    }
    state.list_of(window).pop_clip_rectangle();
    if ((window.auto_fit || has_any(window.flags, UiWindowFlag::ALWAYS_AUTO_RESIZE)) && !window.docked)
    {
        window.size = window.child ? window.size : constrained(state, window, fit_size(state, window));
        window.auto_fit = false;
    }
}

/// Docks window @p index as set_next_window_dock() asked, when its condition applies (once docked there, nothing
/// changes: its node's selection stays the user's).
void
apply_next_dock(ContextState& state,
                std::uint32_t index,
                UiWindowFlag flags) noexcept
{
    NextWindowSettings& next = state.next_window;
    if (!next.has_dock)
    {
        return;
    }
    next.has_dock = false;
    WindowState& window = state.windows[index];
    window.appearing = !window.was_active;
    if (has_any(flags, UiWindowFlag::NO_DOCKING) || !applies(next.dock_condition, window, ONCE_DOCK))
    {
        return;
    }
    const std::uint32_t node = state.dock_node_of(next.dock);
    if (node == DockNodeState::NONE)
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "set_next_window_dock", "the dock node does not exist (a stale handle)", window.name, window.id);
        return;
    }
    const std::uint32_t leaf = state.dock_leaf(node);
    if (window.dock_node == leaf)
    {
        return;
    }
    try
    {
        state.dock_window(index, leaf);
    }
    catch (...)
    {
        state.report(UiFrameProblem::CAPACITY_EXCEEDED, "set_next_window_dock", "the window could not be docked", window.name, window.id);
    }
}

/// begin_window() with or without a close button.
auto
begin_root(ContextState& state,
           std::string_view name,
           bool* open,
           UiWindowFlag flags) noexcept -> bool
{
    if (!state.frame_active)
    {
        state.report(UiFrameProblem::FRAME_NOT_ACTIVE, "begin_window", "call it between UiContext::begin_frame() and end_frame()", name);
        return false;
    }
    if (name.empty())
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "begin_window", "a window needs a name");
        name = "##unnamed";
    }

    // The window of this name (made the first time).
    const std::uint32_t id = ContextState::hash(0, name);
    std::uint32_t index = state.find_window(id);
    try
    {
        if (index == ContextState::NO_WINDOW)
        {
            index = state.create_window(id, name, ContextState::NO_WINDOW);
        }
    }
    catch (...)
    {
        state.report(UiFrameProblem::CAPACITY_EXCEEDED, "begin_window", "the window could not be made", name, id);
        return false;
    }

    // A window made by a loaded layout or a dock builder before it was submitted gets its title now.
    if (state.windows[index].name.empty())
    {
        try
        {
            state.windows[index].name.assign(ContextState::display_text(name));
        }
        catch (...)
        {
            state.report(UiFrameProblem::CAPACITY_EXCEEDED, "begin_window", "the window's title could not be kept", name, id);
        }
    }

    // Docking (the first begin of the frame): as set_next_window_dock() asks; a floating tree's host is begun first;
    // a docked window goes where its node is, without its title bar, unmovable and unresizable by itself.
    DockPlacement placement{};
    if (state.windows[index].begin_count == 0)
    {
        apply_next_dock(state, index, flags);
        if (has_any(flags, UiWindowFlag::NO_DOCKING) && state.windows[index].dock_node != DockNodeState::NONE)
        {
            state.undock_window(index);
        }
        placement = state.prepare_docked_window(state.frame_object, index);
    }
    if (placement.docked)
    {
        flags = flags | DOCKED_WINDOW_FLAGS;
    }
    WindowState& window = state.windows[index];
    state.window_stack.push_back(index);
    state.id_stack.push_back(window.id);

    // A second begin_window() in a frame appends to the window.
    if (window.begin_count++ > 0)
    {
        window.identifier_stack_base = static_cast<std::uint32_t>(state.id_stack.size());
        state.list_of(window).push_clip_rectangle(window.clip);
        state.accessible_begin_window(window);
        return !window.skip_items;
    }
    window.identifier_stack_base = static_cast<std::uint32_t>(state.id_stack.size());

    // Its tab's close button closes a docked window.
    window.closable = open != nullptr;
    if (open != nullptr && window.close_requested)
    {
        *open = false;
    }
    window.close_requested = false;

    // Closed (its close button was clicked): nothing drawn or laid out, but end_window() is expected.
    window.docked = false;
    window.front_list.clear();
    window.front_list.set_pixels_per_unit(state.pixels_per_unit);
    window.grip = {};
    if (open != nullptr && !*open)
    {
        window.skip_items = true;
        window.hidden = true;
        window.draw_list.clear();
        window.draw_list.push_clip_rectangle({});
        state.next_window = NextWindowSettings{};
        return false;
    }

    // Submitted this frame: appearing, settings, flags.
    window.active = true;
    window.appearing = !window.was_active;
    window.last_active_frame = state.frame;
    window.flags = flags;
    window.hidden = false;
    const bool first_use = window.first_use;
    apply_next_window(state, window, index);
    if (window.appearing && !has_any(flags, UiWindowFlag::NO_FOCUS_ON_APPEARING))
    {
        state.focus_window(index);
    }

    // Sizes of this frame; a window never sized fits its content, measured on a first frame that is not drawn.
    const Style& style = state.style;
    const FontMetrics& font = state.current_font();
    window.padding = style.window_padding;
    window.title_bar_height = has_any(flags, UiWindowFlag::NO_TITLE_BAR) ? 0.0F : std::floor(font.line_height + style.frame_padding.y * 2);
    window.menu_bar_height = has_any(flags, UiWindowFlag::MENU_BAR) ? std::floor(font.line_height + style.frame_padding.y * 2) : 0.0F;
    window.next_menu_columns = {};
    window.docked = placement.docked;
    if (placement.docked)
    {
        // Its node's place (its own position and size are kept for when it floats again); shown when selected.
        window.collapsed = false;
        window.auto_fit = false;
        window.hidden = !placement.visible;
    }
    else
    {
        if (window.size.x <= 0 || window.size.y <= 0)
        {
            window.auto_fit = true;
            window.hidden = first_use;
            window.size = fit_size(state, window);
        }
        else if (has_any(flags, UiWindowFlag::ALWAYS_AUTO_RESIZE))
        {
            window.size = fit_size(state, window);
        }
        window.size = constrained(state, window, window.size);

        // Kept on the display: at least a title bar's height of it stays reachable.
        const float margin = std::max(window.title_bar_height, 16.0F);
        if (state.display_size.x > 0 && state.display_size.y > 0)
        {
            window.position.x = std::clamp(window.position.x, margin - window.size.x, std::max(state.display_size.x - margin, 0.0F));
            window.position.y = std::clamp(window.position.y, 0.0F, std::max(state.display_size.y - margin, 0.0F));
        }
    }
    window.first_use = false;

    // The title bar's buttons: collapse, close, a double click on the title.
    Decorations decorations{};
    const auto rectangles = [&]
    {
        if (placement.docked)
        {
            window.outer = placement.area;
            window.hit = placement.area.intersection(placement.clip);
            decorations.title = {.left = window.outer.left, .top = window.outer.top, .right = window.outer.right, .bottom = window.outer.top};
            return;
        }
        const Vector2 size{.x = window.size.x, .y = window.collapsed ? window.title_bar_height : window.size.y};
        window.outer = {.left = window.position.x, .top = window.position.y, .right = window.position.x + size.x, .bottom = window.position.y + size.y};
        window.hit = window.outer;
        decorations.title = {.left = window.outer.left, .top = window.outer.top, .right = window.outer.right, .bottom = window.outer.top + window.title_bar_height};
    };
    rectangles();
    window.clip = window.outer;
    window.skip_items = false;
    DrawList& list = window.draw_list;
    list.clear();
    list.set_pixels_per_unit(state.pixels_per_unit);
    if (window.title_bar_height > 0)
    {
        const float button = font.line_height;
        const float top = decorations.title.top + std::max(0.0F, (window.title_bar_height - button) * 0.5F);
        bool toggle = false;
        if (!has_any(flags, UiWindowFlag::NO_COLLAPSE))
        {
            decorations.collapse = {.left = decorations.title.left + style.frame_padding.x, .top = top,
                                    .right = decorations.title.left + style.frame_padding.x + button, .bottom = top + button};
            decorations.collapse_state = state.button_behavior(window, decorations.collapse, ContextState::hash(window.id, "#COLLAPSE"));
            toggle = decorations.collapse_state.pressed;
        }
        if (open != nullptr)
        {
            decorations.close = {.left = decorations.title.right - style.frame_padding.x - button, .top = top,
                                 .right = decorations.title.right - style.frame_padding.x, .bottom = top + button};
            decorations.close_state = state.button_behavior(window, decorations.close, ContextState::hash(window.id, "#CLOSE"));
            if (decorations.close_state.pressed)
            {
                *open = false;
            }
        }
        const bool on_button = decorations.collapse_state.hovered || decorations.close_state.hovered;
        if (!has_any(flags, UiWindowFlag::NO_COLLAPSE) && state.hovered_window == index && state.mouse[0].double_clicked && !on_button
            && contains(decorations.title, state.mouse_position))
        {
            toggle = true;
        }
        if (toggle)
        {
            window.collapsed = !window.collapsed;
            rectangles();
        }
    }

    // The resize grip: dragging it moves the bottom-right corner, a double click fits the content.
    if (!window.collapsed && !has_any(flags, UiWindowFlag::NO_RESIZE | UiWindowFlag::ALWAYS_AUTO_RESIZE))
    {
        const float grip = std::floor(std::max(font.line_height * 1.35F, style.window_rounding + 1.0F + font.line_height * 0.2F));
        const auto place_grip = [&]
        {
            decorations.grip = {.left = window.outer.right - grip, .top = window.outer.bottom - grip, .right = window.outer.right, .bottom = window.outer.bottom};
        };
        place_grip();
        const std::uint32_t grip_id = ContextState::hash(window.id, "#RESIZE");
        decorations.grip_state = state.button_behavior(window, decorations.grip, grip_id);
        if (state.active_id == grip_id && state.mouse[0].clicked)
        {
            state.active_offset = {.x = window.outer.right - state.mouse_position.x, .y = window.outer.bottom - state.mouse_position.y};
        }
        if (decorations.grip_state.held)
        {
            window.size = constrained(state, window, {.x = state.mouse_position.x + state.active_offset.x - window.position.x,
                                                      .y = state.mouse_position.y + state.active_offset.y - window.position.y});
            rectangles();
            place_grip();
        }
        if (decorations.grip_state.hovered && state.mouse[0].double_clicked)
        {
            window.auto_fit = true;
        }
        if (decorations.grip_state.hovered || decorations.grip_state.held)
        {
            state.cursor = CursorShape::RESIZE_NORTHWEST_SOUTHEAST;
        }
    }

    // Scrollbars, content area and clip; then the drawing (a docked window: square, borderless; a floating tree's
    // host: its border and grip over its docked windows); then the content's clip.
    window.grip = decorations.grip;
    const UiRectangle unclipped{.left = -1.0e9F, .top = -1.0e9F, .right = 1.0e9F, .bottom = 1.0e9F};
    layout_area(state, window, index, decorations, placement.docked ? placement.clip : unclipped);
    if (!window.hidden)
    {
        draw_decorations(state, window, index, decorations, StyleColor::WINDOW_BACKGROUND, placement.docked ? 0.0F : style.window_rounding,
                         placement.docked ? 0.0F : style.window_border_size, window.dock_tree != DockNodeState::NONE ? &window.front_list : nullptr);
    }
    window.skip_items = window.collapsed || (placement.docked && !placement.visible);
    list.push_clip_rectangle(window.clip);
    state.last_item = LastItem{.rectangle = decorations.title, .window = index};
    state.accessible_begin_window(window);
    return !window.skip_items;
}

} // namespace

UiFrame::UiFrame(ContextState& state) noexcept
    : m_state{&state}
{
}

auto
UiFrame::begin_window(std::string_view name,
                      UiWindowFlag flags) noexcept -> bool
{
    return begin_root(*m_state, name, nullptr, flags);
}

auto
UiFrame::begin_window(std::string_view name,
                      bool& open,
                      UiWindowFlag flags) noexcept -> bool
{
    return begin_root(*m_state, name, &open, flags);
}

void
UiFrame::end_window() noexcept
{
    ContextState& state = *m_state;
    if (!state.frame_active || state.window_stack.empty())
    {
        state.report(state.frame_active ? UiFrameProblem::END_WITHOUT_BEGIN : UiFrameProblem::FRAME_NOT_ACTIVE, "end_window",
                     "end_window() without begin_window()");
        return;
    }

    // A table or a child left open is closed first.
    state.close_tables(*this, state.window_stack.back());
    while (state.windows[state.window_stack.back()].child)
    {
        const WindowState& child = state.windows[state.window_stack.back()];
        state.report(UiFrameProblem::UNBALANCED_STACK, "end_window", "begin_child() without end_child()", child.name, child.id);
        end_child();
    }
    state.close_tables(*this, state.window_stack.back());
    const std::uint32_t index = state.window_stack.back();
    WindowState& window = state.windows[index];
    finish_content(state, *this, window);
    state.accessible_end_window(window);
    state.window_stack.pop_back();
    if (!window.active)
    {
        return;
    }

    // A click on the window's empty space (or its title bar) starts moving it (a window docked in a floating tree: the
    // tree's host).
    const UiRectangle title{.left = window.outer.left, .top = window.outer.top, .right = window.outer.right, .bottom = window.outer.top + window.title_bar_height};
    const bool on_window = state.hovered_root == index && state.mouse[0].clicked && state.active_id == 0 && state.hovered_id == 0;
    std::uint32_t mover = index;
    if (window.docked)
    {
        const std::uint32_t root = state.dock_root(window.dock_node);
        const bool floating = root < state.dock_nodes.size() && state.dock_nodes[root].dock_space == 0;
        mover = floating && state.dock_nodes[root].host_window < state.windows.size() ? state.dock_nodes[root].host_window : ContextState::NO_WINDOW;
    }
    const bool allowed = mover != ContextState::NO_WINDOW && !has_any(state.windows[mover].flags, UiWindowFlag::NO_MOVE)
                         && (window.docked || !state.description.input.move_windows_from_title_bar_only || contains(title, state.mouse_position));
    if (on_window && allowed && state.moving_window == ContextState::NO_WINDOW)
    {
        WindowState& moved = state.windows[mover];
        state.moving_window = mover;
        state.moving_offset = {.x = state.mouse_position.x - moved.position.x, .y = state.mouse_position.y - moved.position.y};
        state.set_active(ContextState::hash(moved.id, "#MOVE"), moved);
    }
}

auto
UiFrame::begin_child(std::string_view name,
                     Vector2 size,
                     ChildFlag child_flags,
                     UiWindowFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* parent_pointer = state.current_window("begin_child");
    if (parent_pointer == nullptr)
    {
        return false;
    }
    const auto parent = static_cast<std::uint32_t>(parent_pointer - state.windows.data());

    // The child of this name in this window (made the first time).
    const std::uint32_t id = state.widget_id(name);
    std::uint32_t index = state.find_window(id);
    try
    {
        if (index == ContextState::NO_WINDOW)
        {
            index = state.create_window(id, name, parent);
        }
    }
    catch (...)
    {
        state.report(UiFrameProblem::CAPACITY_EXCEEDED, "begin_child", "the child could not be made", name, id);
        return false;
    }
    WindowState& child = state.windows[index];
    const WindowState& owner = state.windows[parent];
    state.window_stack.push_back(index);
    state.id_stack.push_back(child.id);
    child.identifier_stack_base = static_cast<std::uint32_t>(state.id_stack.size());
    ++child.begin_count;
    child.active = true;
    child.appearing = !child.was_active;
    child.hidden = owner.hidden;
    child.child_flags = child_flags;
    child.flags = flags | UiWindowFlag::NO_TITLE_BAR | UiWindowFlag::NO_RESIZE | UiWindowFlag::NO_MOVE | UiWindowFlag::NO_COLLAPSE
                  | UiWindowFlag::NO_SAVED_SETTINGS | UiWindowFlag::NO_DOCKING;
    child.title_bar_height = 0;
    child.menu_bar_height = has_any(flags, UiWindowFlag::MENU_BAR) ? std::floor(state.current_font().line_height + state.style.frame_padding.y * 2) : 0.0F;
    child.next_menu_columns = {};
    child.collapsed = false;
    state.children_this_frame.push_back(index);

    // A child of a skipped window is skipped too (but end_child() is expected).
    if (owner.skip_items)
    {
        child.skip_items = true;
        child.clip = {};
        state.list_of(child).push_clip_rectangle({});
        state.accessible_begin_window(child);
        return false;
    }

    // Its size: 0 fills what is available, a negative value leaves room, AUTO_RESIZE follows the content.
    const Style& style = state.style;
    const bool framed = has_any(child_flags, ChildFlag::FRAME_STYLE);
    const bool bordered = has_any(child_flags, ChildFlag::BORDERS) || framed;
    child.padding = framed ? style.frame_padding : (bordered || has_any(child_flags, ChildFlag::ALWAYS_USE_WINDOW_PADDING)) ? style.window_padding : Vector2{};
    const Vector2 available{.x = state.content_right(owner) - owner.cursor.x, .y = state.content_bottom(owner) - owner.cursor.y};
    const auto extent = [](float asked, float room, float content) { return asked > 0 ? asked : asked < 0 ? std::max(4.0F, room + asked) : content; };
    const Vector2 fitted{.x = child.content_size.x + child.padding.x * 2, .y = child.content_size.y + child.padding.y * 2};
    child.size = {.x = extent(size.x, available.x, has_any(child_flags, ChildFlag::AUTO_RESIZE_X) ? fitted.x : std::max(4.0F, available.x)),
                  .y = extent(size.y, available.y, has_any(child_flags, ChildFlag::AUTO_RESIZE_Y) ? fitted.y : std::max(4.0F, available.y))};

    // Resized by the user (RESIZE_X, RESIZE_Y): the size dragged to replaces the one asked for.
    const bool resize_x = has_any(child_flags, ChildFlag::RESIZE_X);
    const bool resize_y = has_any(child_flags, ChildFlag::RESIZE_Y);
    if (resize_x && child.resized_size.x > 0)
    {
        child.size.x = child.resized_size.x;
    }
    if (resize_y && child.resized_size.y > 0)
    {
        child.size.y = child.resized_size.y;
    }
    child.position = owner.cursor;
    child.outer = {.left = child.position.x, .top = child.position.y, .right = child.position.x + child.size.x, .bottom = child.position.y + child.size.y};
    const ResizeEdges edges = resize_x || resize_y ? resize_child_edges(state, child, index, parent, resize_x, resize_y) : ResizeEdges{};
    child.hit = child.outer.intersection(owner.clip);
    child.clip = child.hit;

    // Scrollbars, content area, clip; then its background and border.
    Decorations decorations{};
    layout_area(state, child, index, decorations, owner.clip);
    DrawList& list = state.list_of(child);
    if (!child.hidden)
    {
        list.push_clip_rectangle(owner.clip);
        draw_decorations(state, child, index, decorations, framed ? StyleColor::FRAME_BACKGROUND : StyleColor::CHILD_BACKGROUND,
                         framed ? style.frame_rounding : style.child_rounding,
                         bordered ? (framed ? style.frame_border_size : style.child_border_size) : 0.0F);

        // A resizable edge under the mouse, or dragged, is highlighted.
        const UiRectangle& outer = child.outer;
        if (edges.right_hovered || edges.right_held)
        {
            list.add_rectangle_filled({.left = outer.right - 2.0F, .top = outer.top, .right = outer.right, .bottom = outer.bottom},
                                      state.color(edges.right_held ? StyleColor::SEPARATOR_ACTIVE : StyleColor::SEPARATOR_HOVERED));
        }
        if (edges.bottom_hovered || edges.bottom_held)
        {
            list.add_rectangle_filled({.left = outer.left, .top = outer.bottom - 2.0F, .right = outer.right, .bottom = outer.bottom},
                                      state.color(edges.bottom_held ? StyleColor::SEPARATOR_ACTIVE : StyleColor::SEPARATOR_HOVERED));
        }
        list.pop_clip_rectangle();
    }
    child.skip_items = child.clip.empty();
    list.push_clip_rectangle(child.clip);
    state.accessible_begin_window(child);
    return !child.skip_items;
}

void
UiFrame::end_child() noexcept
{
    ContextState& state = *m_state;
    if (state.frame_active && !state.window_stack.empty())
    {
        // A table left open in the child (or one whose own region it is) is closed first.
        state.close_tables(*this, state.window_stack.back());
    }
    if (!state.frame_active || state.window_stack.empty() || !state.windows[state.window_stack.back()].child)
    {
        state.report(state.frame_active ? UiFrameProblem::END_WITHOUT_BEGIN : UiFrameProblem::FRAME_NOT_ACTIVE, "end_child",
                     "end_child() without begin_child()");
        return;
    }
    const std::uint32_t index = state.window_stack.back();
    WindowState& child = state.windows[index];
    finish_content(state, *this, child);
    state.accessible_end_window(child);
    state.window_stack.pop_back();

    // The child is an item of its parent window.
    WindowState& parent = state.windows[state.window_stack.back()];
    if (!parent.skip_items)
    {
        state.item_size(parent, child.size);
        static_cast<void>(state.item_add(parent, child.outer, 0, false, "end_child", child.name));
    }
}

void
UiFrame::set_next_window_position(Vector2 position,
                                  UiCondition condition,
                                  Vector2 pivot) noexcept
{
    NextWindowSettings& next = m_state->next_window;
    next.has_position = true;
    next.position = position;
    next.pivot = pivot;
    next.position_condition = condition;
}

void
UiFrame::set_next_window_size(Vector2 size,
                              UiCondition condition) noexcept
{
    NextWindowSettings& next = m_state->next_window;
    next.has_size = true;
    next.size = size;
    next.size_condition = condition;
}

void
UiFrame::set_next_window_size_constraints(Vector2 minimum,
                                          Vector2 maximum) noexcept
{
    NextWindowSettings& next = m_state->next_window;
    next.has_constraints = true;
    next.minimum = minimum;
    next.maximum = maximum;
}

void
UiFrame::set_next_window_content_size(Vector2 size) noexcept
{
    NextWindowSettings& next = m_state->next_window;
    next.has_content_size = true;
    next.content_size = size;
}

void
UiFrame::set_next_window_collapsed(bool collapsed,
                                   UiCondition condition) noexcept
{
    NextWindowSettings& next = m_state->next_window;
    next.has_collapsed = true;
    next.collapsed = collapsed;
    next.collapsed_condition = condition;
}

void
UiFrame::set_next_window_focus() noexcept
{
    m_state->next_window.focus = true;
}

void
UiFrame::set_next_window_scroll(Vector2 scroll) noexcept
{
    NextWindowSettings& next = m_state->next_window;
    next.has_scroll = true;
    next.scroll = scroll;
}

void
UiFrame::set_next_window_background_alpha(float alpha) noexcept
{
    NextWindowSettings& next = m_state->next_window;
    next.has_background_alpha = true;
    next.background_alpha = std::clamp(alpha, 0.0F, 1.0F);
}

auto
UiFrame::is_window_appearing() const noexcept -> bool
{
    const WindowState* window = m_state->current_window_quiet();
    return window != nullptr && window->appearing;
}

auto
UiFrame::is_window_collapsed() const noexcept -> bool
{
    const WindowState* window = m_state->current_window_quiet();
    return window != nullptr && window->collapsed;
}

auto
UiFrame::is_window_focused(FocusedFlag flags) const noexcept -> bool
{
    const ContextState& state = *m_state;
    const WindowState* window = state.current_window_quiet();
    if (window == nullptr || state.focused_window == ContextState::NO_WINDOW)
    {
        return false;
    }
    if (has_any(flags, FocusedFlag::ANY_WINDOW))
    {
        return true;
    }

    // Focus belongs to root windows: a child is focused with its root, when CHILD_WINDOWS or ROOT_WINDOW allow it.
    if (!window->child || has_any(flags, FocusedFlag::ROOT_WINDOW | FocusedFlag::CHILD_WINDOWS))
    {
        return state.focused_window == window->root;
    }
    return false;
}

auto
UiFrame::is_window_hovered(HoveredFlag flags) const noexcept -> bool
{
    const WindowState* window = m_state->current_window_quiet();
    return window != nullptr && m_state->window_hovered(*window, flags);
}

auto
UiFrame::window_position() const noexcept -> Vector2
{
    const WindowState* window = m_state->current_window_quiet();
    return window == nullptr ? Vector2{} : window->docked ? Vector2{.x = window->outer.left, .y = window->outer.top} : window->position;
}

auto
UiFrame::window_size() const noexcept -> Vector2
{
    const WindowState* window = m_state->current_window_quiet();
    return window != nullptr ? Vector2{.x = window->outer.width(), .y = window->outer.height()} : Vector2{};
}

void
UiFrame::set_window_focus() noexcept
{
    const WindowState* window = m_state->current_window("set_window_focus");
    if (window != nullptr)
    {
        m_state->focus_window(window->root);
    }
}

auto
UiFrame::window_draw_list() noexcept -> DrawList&
{
    WindowState* window = m_state->current_window("window_draw_list");
    return window != nullptr ? m_state->list_of(*window) : m_state->foreground;
}

auto
UiFrame::background_draw_list() noexcept -> DrawList&
{
    return m_state->background;
}

auto
UiFrame::foreground_draw_list() noexcept -> DrawList&
{
    return m_state->foreground;
}

auto
UiFrame::scroll_x() const noexcept -> float
{
    const WindowState* window = m_state->current_window_quiet();
    return window != nullptr ? window->scroll.x : 0.0F;
}

auto
UiFrame::scroll_y() const noexcept -> float
{
    const WindowState* window = m_state->current_window_quiet();
    return window != nullptr ? window->scroll.y : 0.0F;
}

auto
UiFrame::scroll_maximum_x() const noexcept -> float
{
    const WindowState* window = m_state->current_window_quiet();
    return window != nullptr ? window->scroll_maximum.x : 0.0F;
}

auto
UiFrame::scroll_maximum_y() const noexcept -> float
{
    const WindowState* window = m_state->current_window_quiet();
    return window != nullptr ? window->scroll_maximum.y : 0.0F;
}

void
UiFrame::set_scroll_x(float scroll) noexcept
{
    WindowState* window = m_state->current_window("set_scroll_x");
    if (window != nullptr)
    {
        window->scroll_target.x = std::max(scroll, 0.0F);
    }
}

void
UiFrame::set_scroll_y(float scroll) noexcept
{
    WindowState* window = m_state->current_window("set_scroll_y");
    if (window != nullptr)
    {
        window->scroll_target.y = std::max(scroll, 0.0F);
    }
}

void
UiFrame::set_scroll_here_y(float ratio) noexcept
{
    // The cursor's line, in content coordinates, at ratio of the visible height.
    WindowState* window = m_state->current_window("set_scroll_here_y");
    if (window == nullptr)
    {
        return;
    }
    const float line = window->cursor.y - window->cursor_start.y + window->padding.y;
    const float height = window->inner.height();
    window->scroll_target.y = std::max(0.0F, line - std::clamp(ratio, 0.0F, 1.0F) * height + window->line_height * std::clamp(ratio, 0.0F, 1.0F));
}

} // namespace gfx::ui
