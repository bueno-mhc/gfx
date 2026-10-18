/// @file queries.cpp
/// @brief UiFrame's queries: the last item, the keyboard focus, keys, mouse buttons, the frame's time and size.
module gfx.ui.context;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.draw;
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

/// The key table index of @p key (0 for keys outside the table).
constexpr auto
key_index(Key key) noexcept -> std::size_t
{
    const std::size_t index = std::to_underlying(key);
    return index < 256 ? index : 0;
}

/// The state of mouse button @p button.
auto
button_state(const ContextState& state,
             MouseButton button) noexcept -> const MouseButtonState&
{
    return state.mouse[std::to_underlying(button)];
}

} // namespace

auto
UiFrame::is_item_hovered(HoveredFlag flags) const noexcept -> bool
{
    // The mouse over the last item, in its window, unless something blocks it (as flags allow).
    const ContextState& state = *m_state;
    const LastItem& item = state.last_item;
    if (item.window == ContextState::NO_WINDOW || !state.mouse_present)
    {
        return false;
    }
    if (item.disabled && !has_any(flags, HoveredFlag::ALLOW_WHEN_DISABLED))
    {
        return false;
    }
    const WindowState& window = state.windows[item.window];
    if (!has_any(flags, HoveredFlag::ALLOW_WHEN_OVERLAPPED) && !state.window_hovered(window, flags))
    {
        return false;
    }
    const bool field_edited = state.active_id == state.text_edit.id && !state.text_edit.mouse_selecting;
    if (state.active_id != 0 && state.active_id != item.id && !field_edited && !has_any(flags, HoveredFlag::ALLOW_WHEN_BLOCKED_BY_ACTIVE_ITEM))
    {
        return false;
    }

    // A drag and drop source does not report hovering while dragged (unless SOURCE_NO_DISABLE_HOVER).
    const DragDropState& drag = state.drag_drop;
    if (drag.active && item.id != 0 && item.id == drag.source_id && !has_any(drag.source_flags, DragDropFlag::SOURCE_NO_DISABLE_HOVER))
    {
        return false;
    }
    return contains(item.rectangle.intersection(item.clip), state.mouse_position);
}

auto
UiFrame::is_item_active() const noexcept -> bool
{
    return m_state->last_item.id != 0 && m_state->active_id == m_state->last_item.id;
}

auto
UiFrame::is_item_focused() const noexcept -> bool
{
    return m_state->last_item.id != 0 && m_state->focus_id == m_state->last_item.id;
}

auto
UiFrame::is_item_clicked(MouseButton button) const noexcept -> bool
{
    return button_state(*m_state, button).clicked && is_item_hovered();
}

auto
UiFrame::is_item_visible() const noexcept -> bool
{
    return m_state->last_item.visible;
}

auto
UiFrame::is_item_edited() const noexcept -> bool
{
    return m_state->last_item.edited;
}

auto
UiFrame::is_item_activated() const noexcept -> bool
{
    const ContextState& state = *m_state;
    const std::uint32_t id = state.last_item.id;
    return id != 0 && ((state.active_id == id && state.active_id_previous_frame != id) || (state.deactivated_id == id && state.active_id_previous_frame != id));
}

auto
UiFrame::is_item_deactivated() const noexcept -> bool
{
    const ContextState& state = *m_state;
    return state.last_item.id != 0 && state.deactivated_id == state.last_item.id;
}

auto
UiFrame::is_item_deactivated_after_edit() const noexcept -> bool
{
    return is_item_deactivated() && m_state->deactivated_after_edit;
}

auto
UiFrame::is_any_item_hovered() const noexcept -> bool
{
    return m_state->hovered_id != 0;
}

auto
UiFrame::is_any_item_active() const noexcept -> bool
{
    return m_state->active_id != 0;
}

auto
UiFrame::is_any_item_focused() const noexcept -> bool
{
    return m_state->focus_id != 0;
}

auto
UiFrame::item_rectangle() const noexcept -> UiRectangle
{
    return m_state->last_item.rectangle;
}

void
UiFrame::set_item_default_focus() noexcept
{
    ContextState& state = *m_state;
    const WindowState* window = state.current_window_quiet();
    if (window != nullptr && window->appearing && state.last_item.id != 0)
    {
        state.default_focus_request = state.last_item.id;
    }
}

void
UiFrame::set_keyboard_focus_here() noexcept
{
    m_state->focus_here_requested = true;
}

auto
UiFrame::is_key_down(Key key) const noexcept -> bool
{
    return key_index(key) != 0 && m_state->keys_down[key_index(key)];
}

auto
UiFrame::is_key_pressed(Key key,
                        bool repeat) const noexcept -> bool
{
    const std::size_t index = key_index(key);
    return index != 0 && (repeat ? m_state->keys_pressed[index] > 0 : m_state->keys_pressed[index] > m_state->keys_repeated[index]);
}

auto
UiFrame::is_key_released(Key key) const noexcept -> bool
{
    return key_index(key) != 0 && m_state->keys_released[key_index(key)];
}

auto
UiFrame::key_modifiers() const noexcept -> KeyModifier
{
    return m_state->modifiers;
}

auto
UiFrame::is_mouse_down(MouseButton button) const noexcept -> bool
{
    return button_state(*m_state, button).down;
}

auto
UiFrame::is_mouse_clicked(MouseButton button,
                          bool repeat) const noexcept -> bool
{
    // A press, or (with repeat) a repeat tick while held: after the delay, one per interval.
    const MouseButtonState& state = button_state(*m_state, button);
    if (state.clicked || !repeat || !state.down || state.previous_held_seconds < 0)
    {
        return state.clicked;
    }
    const double delay = m_state->description.input.repeat_delay_seconds;
    const double interval = std::max(m_state->description.input.repeat_interval_seconds, 1.0e-3);
    const auto ticks = [delay, interval](double held) { return held < delay ? -1.0 : std::floor((held - delay) / interval); };
    return ticks(state.held_seconds) > ticks(state.previous_held_seconds);
}

auto
UiFrame::is_mouse_released(MouseButton button) const noexcept -> bool
{
    return button_state(*m_state, button).released;
}

auto
UiFrame::is_mouse_double_clicked(MouseButton button) const noexcept -> bool
{
    return button_state(*m_state, button).double_clicked;
}

auto
UiFrame::is_mouse_dragging(MouseButton button,
                           float threshold) const noexcept -> bool
{
    const MouseButtonState& state = button_state(*m_state, button);
    const float limit = threshold < 0 ? m_state->description.input.drag_threshold : threshold;
    return state.down && state.drag_maximum >= limit;
}

auto
UiFrame::mouse_drag_delta(MouseButton button,
                          float threshold) const noexcept -> Vector2
{
    const MouseButtonState& state = button_state(*m_state, button);
    const float limit = threshold < 0 ? m_state->description.input.drag_threshold : threshold;
    if (!state.down || state.drag_maximum < limit)
    {
        return {};
    }
    return {.x = m_state->mouse_position.x - state.clicked_position.x, .y = m_state->mouse_position.y - state.clicked_position.y};
}

auto
UiFrame::mouse_position() const noexcept -> Vector2
{
    return m_state->mouse_position;
}

auto
UiFrame::mouse_wheel() const noexcept -> Vector2
{
    return m_state->mouse_wheel;
}

auto
UiFrame::is_mouse_hovering_rectangle(Vector2 minimum,
                                     Vector2 maximum,
                                     bool clip) const noexcept -> bool
{
    const ContextState& state = *m_state;
    UiRectangle rectangle{.left = minimum.x, .top = minimum.y, .right = maximum.x, .bottom = maximum.y};
    const WindowState* window = state.current_window_quiet();
    if (clip && window != nullptr)
    {
        rectangle = rectangle.intersection(window->clip);
    }
    return state.mouse_present && contains(rectangle, state.mouse_position);
}

void
UiFrame::set_mouse_cursor(CursorShape shape) noexcept
{
    m_state->cursor = shape;
}

auto
UiFrame::mouse_cursor() const noexcept -> CursorShape
{
    return m_state->cursor;
}

auto
UiFrame::wants_mouse() const noexcept -> bool
{
    const ContextState& state = *m_state;
    return state.hovered_root != ContextState::NO_WINDOW || state.active_id != 0 || state.moving_window != ContextState::NO_WINDOW;
}

auto
UiFrame::wants_keyboard() const noexcept -> bool
{
    return m_state->focus_id != 0 && m_state->focus_visible;
}

auto
UiFrame::time() const noexcept -> double
{
    return m_state->time;
}

auto
UiFrame::delta_time() const noexcept -> float
{
    return m_state->delta_time;
}

auto
UiFrame::frame_count() const noexcept -> std::uint64_t
{
    return m_state->frame;
}

auto
UiFrame::display_size() const noexcept -> Vector2
{
    return m_state->display_size;
}

} // namespace gfx::ui
