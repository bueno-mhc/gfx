/// @file menus.cpp
/// @brief Menu bars (a window's, the display's main one), menus (popups opened from a bar item or from a row of
///        another menu, which opens on hover), menu items with shortcuts and check marks, and their keyboard
///        navigation (Tab, Up, Down, Right opens a submenu, Left closes it, Return chooses, Escape closes).
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

constexpr std::string_view MAIN_MENU_BAR_WINDOW = "##main_menu_bar";

/// Whether point @p point is in triangle @p a, @p b, @p c (either winding).
auto
in_triangle(Vector2 a,
            Vector2 b,
            Vector2 c,
            Vector2 point) noexcept -> bool
{
    const auto side = [](Vector2 from, Vector2 to, Vector2 at) { return (to.x - from.x) * (at.y - from.y) - (to.y - from.y) * (at.x - from.x); };
    const float first = side(a, b, point);
    const float second = side(b, c, point);
    const float third = side(c, a, point);
    return (first >= 0 && second >= 0 && third >= 0) || (first <= 0 && second <= 0 && third <= 0);
}

/// Whether the mouse moves toward submenu window @p child (it then keeps the submenu open over other rows).
auto
moving_toward(const ContextState& state,
              const WindowState& child) noexcept -> bool
{
    if (state.mouse_delta.x == 0 && state.mouse_delta.y == 0)
    {
        return false;
    }
    const Vector2 now = state.mouse_position;
    const Vector2 before{.x = now.x - state.mouse_delta.x, .y = now.y - state.mouse_delta.y};
    const float edge = child.outer.left >= before.x ? child.outer.left : child.outer.right;
    const float pad = state.style.window_padding.y;
    return in_triangle(before, {.x = edge, .y = child.outer.top - pad}, {.x = edge, .y = child.outer.bottom + pad}, now);
}

/// The window of the menu open at @p level, if any.
auto
child_menu(const ContextState& state,
           std::size_t level) noexcept -> const WindowState*
{
    if (level >= state.popups.size() || !state.popups[level].menu || state.popups[level].window == ContextState::NO_WINDOW)
    {
        return nullptr;
    }
    return &state.windows[state.popups[level].window];
}

/// A check mark in the square of side @p size at @p position.
void
draw_check_mark(DrawList& list,
                Vector2 position,
                float size,
                Color color)
{
    const float thickness = std::max(size / 5.0F, 1.0F);
    const float third = size / 3.0F;
    const Vector2 bottom{.x = position.x + third, .y = position.y + size - third * 0.5F};
    const std::array points{Vector2{.x = bottom.x - third, .y = bottom.y - third}, bottom, Vector2{.x = bottom.x + third * 2, .y = bottom.y - third * 2}};
    list.add_polyline(points, color, thickness);
}

/// A row of a menu: its rectangle, its columns (label, shortcut, mark) and the item it registers.
struct MenuRow
{
    UiRectangle hit{};
    float label_x = 0;
    float shortcut_x = 0;
    float mark_x = 0;
    float top = 0;
    bool visible = false;
};

auto
menu_row(ContextState& state,
         WindowState& window,
         std::uint32_t id,
         float label_width,
         float shortcut_width,
         std::string_view label,
         std::string_view operation) noexcept -> MenuRow
{
    // The columns of last frame's rows (at least this row's), measured again this frame.
    const Style& style = state.style;
    const FontMetrics& font = state.current_font();
    window.next_menu_columns[0] = std::max(window.next_menu_columns[0], label_width);
    window.next_menu_columns[1] = std::max(window.next_menu_columns[1], shortcut_width);
    const float label_column = std::max(window.menu_columns[0], label_width);
    const float shortcut_column = std::max(window.menu_columns[1], shortcut_width);
    MenuRow row{};
    const Vector2 position = window.cursor;
    row.label_x = position.x;
    row.shortcut_x = position.x + label_column + style.item_spacing.x * 2;
    row.mark_x = row.shortcut_x + (shortcut_column > 0 ? shortcut_column + style.item_spacing.x : 0.0F);
    row.top = position.y;
    const float width = row.mark_x + font.size - position.x;
    const float height = font.line_height;
    row.hit = {.left = position.x - std::floor(style.item_spacing.x * 0.5F),
               .top = position.y - std::floor(style.item_spacing.y * 0.5F),
               .right = std::max(state.content_right(window), position.x + width) + std::floor(style.item_spacing.x * 0.5F),
               .bottom = position.y + height + std::floor(style.item_spacing.y * 0.5F)};
    state.item_size(window, {.x = width, .y = height});
    row.visible = state.item_add(window, row.hit, id, true, operation, label);
    return row;
}

/// A menu or menu item in a menu bar: its rectangle at the bar's next place.
auto
bar_item(ContextState& state,
         WindowState& window,
         std::uint32_t id,
         float label_width,
         std::string_view label,
         std::string_view operation) noexcept -> UiRectangle
{
    const Style& style = state.style;
    MenuBarState& bar = state.menu_bar;
    const UiRectangle rectangle{.left = bar.x - std::floor(style.item_spacing.x * 0.5F),
                                .top = bar.rectangle.top,
                                .right = bar.x + label_width + std::floor(style.item_spacing.x * 0.5F),
                                .bottom = bar.rectangle.bottom};
    bar.x += label_width + style.item_spacing.x;
    static_cast<void>(state.item_add(window, rectangle, id, true, operation, label));
    return rectangle;
}

/// Whether a menu of the bar of window @p window is open at @p level.
auto
bar_menu_open(const ContextState& state,
              std::size_t level,
              std::uint32_t window) noexcept -> bool
{
    return level < state.popups.size() && state.popups[level].bar_window == window;
}

} // namespace

auto
UiFrame::begin_menu_bar() noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("begin_menu_bar");
    if (window == nullptr || window->skip_items || window->menu_bar_height <= 0)
    {
        if (window != nullptr && window->menu_bar_height <= 0 && !window->skip_items)
        {
            state.report(UiFrameProblem::INVALID_ARGUMENT, "begin_menu_bar", "the window has no menu bar: give it UiWindowFlag::MENU_BAR", window->name, window->id);
        }
        return false;
    }
    if (state.menu_bar.active)
    {
        state.report(UiFrameProblem::UNBALANCED_STACK, "begin_menu_bar", "begin_menu_bar() inside another menu bar");
        return false;
    }

    // The bar under the title bar: its items go side by side in it, clipped to it.
    MenuBarState& bar = state.menu_bar;
    bar.active = true;
    bar.window = static_cast<std::uint32_t>(window - state.windows.data());
    bar.rectangle = {.left = window->outer.left,
                     .top = window->outer.top + window->title_bar_height,
                     .right = window->outer.right,
                     .bottom = window->outer.top + window->title_bar_height + window->menu_bar_height};
    bar.saved_clip = window->clip;
    bar.x = bar.rectangle.left + std::max(window->padding.x, state.style.item_spacing.x);
    window->clip = bar.rectangle.intersection(window->hit);
    state.list_of(*window).push_clip_rectangle(window->clip, false);
    return true;
}

void
UiFrame::end_menu_bar() noexcept
{
    ContextState& state = *m_state;
    MenuBarState& bar = state.menu_bar;
    const WindowState* current = state.current_window_quiet();
    if (!bar.active || current == nullptr || static_cast<std::uint32_t>(current - state.windows.data()) != bar.window)
    {
        state.report(UiFrameProblem::END_WITHOUT_BEGIN, "end_menu_bar", "end_menu_bar() without begin_menu_bar() in this window");
        return;
    }
    WindowState& window = state.windows[bar.window];
    state.list_of(window).pop_clip_rectangle();
    window.clip = bar.saved_clip;
    bar = MenuBarState{};
}

auto
UiFrame::begin_main_menu_bar() noexcept -> bool
{
    ContextState& state = *m_state;
    if (!state.frame_active)
    {
        state.report(UiFrameProblem::FRAME_NOT_ACTIVE, "begin_main_menu_bar", "call it between UiContext::begin_frame() and end_frame()");
        return false;
    }

    // A window across the top of the display that is only a menu bar.
    const float height = std::floor(state.current_font().line_height + state.style.frame_padding.y * 2);
    set_next_window_position({}, UiCondition::ALWAYS);
    set_next_window_size({.x = state.display_size.x, .y = height}, UiCondition::ALWAYS);
    push_style_variable(StyleVariable::WINDOW_ROUNDING, 0.0F);
    push_style_variable(StyleVariable::WINDOW_BORDER_SIZE, 0.0F);
    push_style_variable(StyleVariable::WINDOW_MINIMUM_SIZE, Vector2{});
    static_cast<void>(begin_window(MAIN_MENU_BAR_WINDOW, UiWindowFlag::NO_TITLE_BAR | UiWindowFlag::NO_RESIZE | UiWindowFlag::NO_MOVE
                                                             | UiWindowFlag::NO_SCROLLBAR | UiWindowFlag::NO_SAVED_SETTINGS | UiWindowFlag::MENU_BAR
                                                             | UiWindowFlag::NO_COLLAPSE | UiWindowFlag::NO_DOCKING | UiWindowFlag::NO_SCROLL_WITH_MOUSE));
    pop_style_variable(3);
    if (!begin_menu_bar())
    {
        end_window();
        return false;
    }
    return true;
}

void
UiFrame::end_main_menu_bar() noexcept
{
    ContextState& state = *m_state;
    const WindowState* window = state.current_window_quiet();
    if (window == nullptr || window->id != ContextState::hash(0, MAIN_MENU_BAR_WINDOW))
    {
        state.report(UiFrameProblem::END_WITHOUT_BEGIN, "end_main_menu_bar", "end_main_menu_bar() without a begin_main_menu_bar() that returned true");
        return;
    }
    end_menu_bar();
    end_window();
}

auto
UiFrame::begin_menu(std::string_view label,
                    bool enabled) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window_pointer = state.current_window("begin_menu");
    if (window_pointer == nullptr || window_pointer->skip_items)
    {
        return false;
    }
    WindowState& window = *window_pointer;
    const Style& style = state.style;
    const FontMetrics& font = state.current_font();
    const auto index = static_cast<std::uint32_t>(&window - state.windows.data());
    const std::uint32_t id = state.widget_id(label);
    const bool in_bar = state.menu_bar.active && state.menu_bar.window == index;
    const std::size_t level = state.current_popup_level();
    const bool in_popup = level > 0;
    const bool open = level < state.popups.size() && state.popups[level].id == id;
    const TextLayout text = state.layout_text(ContextState::display_text(label), 0, "begin_menu");
    const bool was_disabled = state.disabled;
    state.disabled = state.disabled || !enabled;

    bool want_open = false;
    bool want_close = false;
    bool keyboard = false;
    Vector2 position{};
    float flip_bottom = -1;
    float flip_right = -1;
    DrawList& list = state.list_of(window);
    if (in_bar)
    {
        // A bar item: a press opens or closes it; hovered while another menu of the bar is open, it opens; Down or
        // Return opens it from the keyboard with its first item focused.
        const UiRectangle rectangle = bar_item(state, window, id, text.width, label, "begin_menu");
        const ButtonState button = state.button_behavior(window, rectangle, id, ButtonFlag::PRESS_ON_CLICK);
        keyboard = state.keyboard_focused(window, id) && (state.presses(Key::DOWN) > 0 || button.pressed);
        const bool closed_by_click = std::ranges::contains(state.popups_closed_by_click, id);
        want_open = !open && ((button.pressed && !closed_by_click) || (button.hovered && bar_menu_open(state, level, index)) || keyboard);
        want_close = open && button.pressed && !keyboard;
        position = {.x = rectangle.left, .y = rectangle.bottom};
        flip_bottom = rectangle.top;
        if (open || button.hovered)
        {
            list.add_rectangle_filled(rectangle, state.color(open ? StyleColor::HEADER : StyleColor::HEADER_HOVERED));
        }
        state.draw_focus(list, rectangle, id, 0.0F);
        state.draw_text(list, text, {.x = rectangle.left + std::floor(style.item_spacing.x * 0.5F), .y = rectangle.top + (rectangle.height() - text.height) * 0.5F},
                        state.color(enabled ? StyleColor::TEXT_NORMAL : StyleColor::TEXT_DISABLED));
    }
    else
    {
        // A row: hovered, it opens (unless the mouse moves toward the open sibling submenu); a press or Right or
        // Return opens it too, Right and Return from the keyboard with its first item focused.
        const MenuRow row = menu_row(state, window, id, text.width, 0, label, "begin_menu");
        if (row.visible)
        {
            const ButtonState button = state.button_behavior(window, row.hit, id, ButtonFlag::PRESS_ON_CLICK);
            const WindowState* sibling = open ? nullptr : child_menu(state, level);
            const bool sibling_kept = sibling != nullptr && moving_toward(state, *sibling);
            keyboard = state.keyboard_focused(window, id) && (state.presses(Key::RIGHT) > 0 || button.pressed);
            want_open = !open && ((button.hovered && !sibling_kept && state.active_id != id) || button.pressed || keyboard);
            position = {.x = window.outer.right - 1.0F, .y = row.top - window.padding.y};
            flip_right = window.outer.left + 1.0F;
            if (open || button.hovered)
            {
                list.add_rectangle_filled(row.hit, state.color(StyleColor::HEADER_HOVERED));
            }
            state.draw_focus(list, row.hit, id, 0.0F);
            const Color color = state.color(enabled ? StyleColor::TEXT_NORMAL : StyleColor::TEXT_DISABLED);
            state.draw_text(list, text, {.x = row.label_x, .y = row.top}, color);
            const float size = font.size * 0.7F;
            state.draw_arrow(list, {.x = row.mark_x + (font.size - size) * 0.5F, .y = row.top + (font.line_height - size) * 0.5F}, size, ArrowDirection::RIGHT, color);
        }
    }
    state.disabled = was_disabled;

    // Opened: a popup above the current one (a submenu when this menu is in a popup).
    if (want_open && enabled)
    {
        if (PopupState* popup = state.open_popup(id, position, 0, 0, flip_bottom, flip_right))
        {
            popup->menu = true;
            popup->submenu = in_popup && !in_bar;
            popup->bar_window = in_bar ? index : ContextState::NO_WINDOW;
            popup->opener_item = id;
            popup->focus_first = keyboard;
        }
    }
    if (want_close)
    {
        state.close_popups_from(level);
    }
    return state.begin_popup(*this, id, UiWindowFlag::NONE);
}

void
UiFrame::end_menu() noexcept
{
    ContextState& state = *m_state;
    const WindowState* window = state.current_window_quiet();
    const std::size_t level = window == nullptr ? state.popups.size() : state.popup_level_of(window->root);
    if (window == nullptr || window->child || level >= state.popups.size() || !state.popups[level].menu)
    {
        state.report(UiFrameProblem::END_WITHOUT_BEGIN, "end_menu", "end_menu() without a begin_menu() that returned true");
        return;
    }
    state.end_popup(*this);
}

auto
UiFrame::menu_item(std::string_view label,
                   std::string_view shortcut,
                   bool selected,
                   bool enabled) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window_pointer = state.current_window("menu_item");
    if (window_pointer == nullptr || window_pointer->skip_items)
    {
        return false;
    }
    WindowState& window = *window_pointer;
    const Style& style = state.style;
    const FontMetrics& font = state.current_font();
    const auto index = static_cast<std::uint32_t>(&window - state.windows.data());
    const std::uint32_t id = state.widget_id(label);
    const bool in_bar = state.menu_bar.active && state.menu_bar.window == index;
    const std::size_t level = state.current_popup_level();
    const TextLayout text = state.layout_text(ContextState::display_text(label), 0, "menu_item");
    const TextLayout shortcut_text = state.layout_text(shortcut, 0, "menu_item");
    const bool was_disabled = state.disabled;
    state.disabled = state.disabled || !enabled;
    DrawList& list = state.list_of(window);
    const Color color = state.color(enabled ? StyleColor::TEXT_NORMAL : StyleColor::TEXT_DISABLED);
    bool pressed = false;
    if (in_bar)
    {
        // In a bar: a button.
        const UiRectangle rectangle = bar_item(state, window, id, text.width, label, "menu_item");
        const ButtonState button = state.button_behavior(window, rectangle, id);
        pressed = button.pressed;
        if (button.hovered || selected)
        {
            list.add_rectangle_filled(rectangle, state.color(button.held ? StyleColor::HEADER_ACTIVE : button.hovered ? StyleColor::HEADER_HOVERED : StyleColor::HEADER));
        }
        state.draw_focus(list, rectangle, id, 0.0F);
        state.draw_text(list, text, {.x = rectangle.left + std::floor(style.item_spacing.x * 0.5F), .y = rectangle.top + (rectangle.height() - text.height) * 0.5F}, color);
    }
    else
    {
        // A row: the label, the shortcut, the check mark; hovered, it closes the submenu of a sibling (unless the
        // mouse moves toward it); chosen, it closes its menus.
        const MenuRow row = menu_row(state, window, id, text.width, shortcut_text.width, label, "menu_item");
        if (row.visible)
        {
            const ButtonState button = state.button_behavior(window, row.hit, id);
            pressed = button.pressed;
            if (const WindowState* sibling = child_menu(state, level); button.hovered && sibling != nullptr && !moving_toward(state, *sibling))
            {
                state.close_popups_from(level);
            }
            if (button.hovered || button.held)
            {
                list.add_rectangle_filled(row.hit, state.color(button.held && button.hovered ? StyleColor::HEADER_ACTIVE : StyleColor::HEADER_HOVERED));
            }
            state.draw_focus(list, row.hit, id, 0.0F);
            state.draw_text(list, text, {.x = row.label_x, .y = row.top}, color);
            state.draw_text(list, shortcut_text, {.x = row.shortcut_x, .y = row.top}, state.color(StyleColor::TEXT_DISABLED));
            if (selected)
            {
                const float size = font.size * 0.8F;
                draw_check_mark(list, {.x = row.mark_x + (font.size - size) * 0.5F, .y = row.top + (font.line_height - size) * 0.5F}, size,
                                state.color(StyleColor::CHECK_MARK));
            }
        }
    }
    state.disabled = was_disabled;
    if (pressed && level > 0)
    {
        state.close_current_popup(index);
    }
    return pressed;
}

auto
UiFrame::menu_item(std::string_view label,
                   std::string_view shortcut,
                   bool* selected,
                   bool enabled) noexcept -> bool
{
    if (!menu_item(label, shortcut, selected != nullptr && *selected, enabled))
    {
        return false;
    }
    if (selected != nullptr)
    {
        *selected = !*selected;
        m_state->mark_edited(m_state->last_item.id);
    }
    return true;
}

} // namespace gfx::ui
