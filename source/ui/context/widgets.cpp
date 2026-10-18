/// @file widgets.cpp
/// @brief UiFrame's basic widgets: text (plain, colored, disabled, wrapped, bulleted, labeled), separators, buttons
///        (normal, small, invisible, arrow), checkboxes, radio buttons, switches and selectables.
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

/// The width text wraps at from the cursor (0: no wrapping), from the wrap position stack.
auto
wrap_width(const ContextState& state,
           const WindowState& window) noexcept -> float
{
    if (state.text_wrap_stack.empty() || state.text_wrap_stack.back() < 0)
    {
        return 0.0F;
    }
    const float position = state.text_wrap_stack.back();
    const float right = position == 0 ? state.content_right(window) : window.cursor_start.x + position;
    return std::max(1.0F, right - window.cursor.x);
}

/// A line (or lines) of text at the cursor, in @p color.
void
text_item(ContextState& state,
          WindowState& window,
          std::string_view value,
          Color color,
          std::string_view operation) noexcept
{
    const TextLayout layout = state.layout_text(value, wrap_width(state, window), operation);
    const Vector2 position{.x = window.cursor.x, .y = window.cursor.y + window.line_text_offset};
    const Vector2 size{.x = layout.width, .y = std::max(layout.height, state.current_font().line_height)};
    const UiRectangle rectangle{.left = position.x, .top = position.y, .right = position.x + size.x, .bottom = position.y + size.y};
    state.item_size(window, size);
    if (state.item_add(window, rectangle, 0, false, operation, {}))
    {
        state.draw_text(state.list_of(window), layout, position, color);
    }
}

/// The size of @p value's layout, with a line of the font at least.
auto
text_size(const ContextState& state,
          const TextLayout& layout) noexcept -> Vector2
{
    return {.x = layout.width, .y = std::max(layout.height, state.current_font().line_height)};
}

/// The color of a frame in button state @p button.
auto
frame_color(const ContextState& state,
            const ButtonState& button) noexcept -> Color
{
    return state.color(button.held && button.hovered ? StyleColor::FRAME_BACKGROUND_ACTIVE
                       : button.hovered               ? StyleColor::FRAME_BACKGROUND_HOVERED
                                                      : StyleColor::FRAME_BACKGROUND);
}

/// A button of @p size at the cursor (moved down by @p offset_y), its label aligned in it.
auto
button_item(ContextState& state,
            WindowState& window,
            std::string_view label,
            Vector2 size,
            Vector2 padding,
            float offset_y,
            ButtonFlag flags,
            std::string_view operation) noexcept -> bool
{
    const Style& style = state.style;
    const std::uint32_t id = state.widget_id(label);
    const TextLayout text = state.layout_text(ContextState::display_text(label), 0, operation);
    const Vector2 label_size = text_size(state, text);
    const Vector2 available{.x = state.content_right(window) - window.cursor.x, .y = state.content_bottom(window) - window.cursor.y};
    const auto extent = [](float asked, float room, float fit) { return asked > 0 ? asked : asked < 0 ? std::max(4.0F, room + asked) : fit; };
    const Vector2 item{.x = extent(size.x, available.x, label_size.x + padding.x * 2), .y = extent(size.y, available.y, label_size.y + padding.y * 2)};
    const Vector2 position{.x = window.cursor.x, .y = window.cursor.y + offset_y};
    const UiRectangle rectangle{.left = position.x, .top = position.y, .right = position.x + item.x, .bottom = position.y + item.y};
    state.item_size(window, item, padding.y);
    if (!state.item_add(window, rectangle, id, true, operation, label))
    {
        return false;
    }
    const ButtonState button = state.button_behavior(window, rectangle, id, flags);

    // The frame, the focus highlight, the label (clipped to the frame when it does not fit).
    DrawList& list = state.list_of(window);
    const StyleColor fill = button.held && button.hovered ? StyleColor::BUTTON_ACTIVE : button.hovered ? StyleColor::BUTTON_HOVERED : StyleColor::BUTTON;
    state.draw_frame(list, rectangle, state.color(fill), style.frame_rounding);
    state.draw_focus(list, rectangle, id, style.frame_rounding);
    const Vector2 inner{.x = item.x - padding.x * 2, .y = item.y - padding.y * 2};
    const Vector2 text_position{.x = rectangle.left + padding.x + std::max(0.0F, inner.x - label_size.x) * style.button_text_align.x,
                                .y = rectangle.top + padding.y + std::max(0.0F, inner.y - label_size.y) * style.button_text_align.y};
    const bool clipped = label_size.x > inner.x || label_size.y > inner.y;
    if (clipped)
    {
        list.push_clip_rectangle(rectangle);
    }
    state.draw_text(list, text, text_position, state.color(StyleColor::TEXT_NORMAL));
    if (clipped)
    {
        list.pop_clip_rectangle();
    }
    return button.pressed;
}

/// A square of side @p side at the cursor and the label after it: the hit box, the square and the label layout.
struct LabeledBox
{
    std::uint32_t id = 0;
    UiRectangle total{};
    UiRectangle box{};
    TextLayout text{};
    Vector2 text_position{};
    bool visible = false;
};

auto
labeled_box(ContextState& state,
            WindowState& window,
            std::string_view label,
            Vector2 box_size,
            std::string_view operation) noexcept -> LabeledBox
{
    const Style& style = state.style;
    LabeledBox result{.id = state.widget_id(label)};
    result.text = state.layout_text(ContextState::display_text(label), 0, operation);
    const Vector2 label_size = result.text.glyphs.empty() && result.text.width == 0 ? Vector2{} : text_size(state, result.text);
    const Vector2 total{.x = box_size.x + (label_size.x > 0 ? style.item_inner_spacing.x + label_size.x : 0.0F),
                        .y = std::max(box_size.y, label_size.y + style.frame_padding.y * 2)};
    const Vector2 position = window.cursor;
    result.total = {.left = position.x, .top = position.y, .right = position.x + total.x, .bottom = position.y + total.y};
    result.box = {.left = position.x, .top = position.y, .right = position.x + box_size.x, .bottom = position.y + box_size.y};
    result.text_position = {.x = result.box.right + style.item_inner_spacing.x, .y = position.y + style.frame_padding.y};
    state.item_size(window, total, style.frame_padding.y);
    result.visible = state.item_add(window, result.total, result.id, true, operation, label);
    return result;
}

} // namespace

void
UiFrame::text_unformatted(std::string_view text) noexcept
{
    WindowState* window = m_state->current_window("text");
    if (window != nullptr && !window->skip_items)
    {
        text_item(*m_state, *window, text, m_state->color(StyleColor::TEXT_NORMAL), "text");
    }
}

void
UiFrame::text_colored_unformatted(Color color,
                                  std::string_view text) noexcept
{
    WindowState* window = m_state->current_window("text_colored");
    if (window != nullptr && !window->skip_items)
    {
        text_item(*m_state, *window, text, m_state->faded(color), "text_colored");
    }
}

void
UiFrame::text_disabled_unformatted(std::string_view text) noexcept
{
    WindowState* window = m_state->current_window("text_disabled");
    if (window != nullptr && !window->skip_items)
    {
        text_item(*m_state, *window, text, m_state->color(StyleColor::TEXT_DISABLED), "text_disabled");
    }
}

void
UiFrame::text_wrapped_unformatted(std::string_view text) noexcept
{
    // Wrapped at the end of the content region unless a wrap position is in effect.
    ContextState& state = *m_state;
    WindowState* window = state.current_window("text_wrapped");
    if (window == nullptr || window->skip_items)
    {
        return;
    }
    const bool push = state.text_wrap_stack.empty() || state.text_wrap_stack.back() < 0;
    if (push)
    {
        state.text_wrap_stack.push_back(0.0F);
    }
    text_item(state, *window, text, state.color(StyleColor::TEXT_NORMAL), "text_wrapped");
    if (push)
    {
        state.text_wrap_stack.pop_back();
    }
}

void
UiFrame::bullet_text_unformatted(std::string_view text) noexcept
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("bullet_text");
    if (window == nullptr || window->skip_items)
    {
        return;
    }

    // A bullet in a square of a line's height, the text after it.
    const FontMetrics& font = state.current_font();
    const float square = font.line_height;
    const TextLayout layout = state.layout_text(text, 0, "bullet_text");
    const Vector2 size{.x = square + state.style.frame_padding.x + layout.width, .y = std::max(layout.height, font.line_height)};
    const Vector2 position{.x = window->cursor.x, .y = window->cursor.y + window->line_text_offset};
    const UiRectangle rectangle{.left = position.x, .top = position.y, .right = position.x + size.x, .bottom = position.y + size.y};
    state.item_size(*window, size);
    if (state.item_add(*window, rectangle, 0, false, "bullet_text", {}))
    {
        DrawList& list = state.list_of(*window);
        list.add_circle_filled({.x = position.x + square * 0.5F, .y = position.y + square * 0.5F}, font.size * 0.20F, state.color(StyleColor::TEXT_NORMAL));
        state.draw_text(list, layout, {.x = position.x + square + state.style.frame_padding.x, .y = position.y}, state.color(StyleColor::TEXT_NORMAL));
    }
}

void
UiFrame::label_unformatted(std::string_view label,
                           std::string_view value) noexcept
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("label");
    if (window == nullptr || window->skip_items)
    {
        return;
    }

    // The value in a frame-high area of the item width, the label after it.
    const Style& style = state.style;
    const float width = state.take_item_width(*window);
    const TextLayout value_layout = state.layout_text(value, 0, "label");
    const TextLayout label_layout = state.layout_text(ContextState::display_text(label), 0, "label");
    const float height = state.current_font().line_height + style.frame_padding.y * 2;
    const Vector2 size{.x = width + (label_layout.width > 0 ? style.item_inner_spacing.x + label_layout.width : 0.0F), .y = height};
    const Vector2 position = window->cursor;
    const UiRectangle rectangle{.left = position.x, .top = position.y, .right = position.x + size.x, .bottom = position.y + size.y};
    state.item_size(*window, size, style.frame_padding.y);
    if (!state.item_add(*window, rectangle, 0, false, "label", label))
    {
        return;
    }
    DrawList& list = state.list_of(*window);
    const Color text = state.color(StyleColor::TEXT_NORMAL);
    list.push_clip_rectangle({.left = position.x, .top = position.y, .right = position.x + width, .bottom = position.y + height});
    state.draw_text(list, value_layout, {.x = position.x, .y = position.y + style.frame_padding.y}, text);
    list.pop_clip_rectangle();
    state.draw_text(list, label_layout, {.x = position.x + width + style.item_inner_spacing.x, .y = position.y + style.frame_padding.y}, text);
}

void
UiFrame::bullet() noexcept
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("bullet");
    if (window == nullptr || window->skip_items)
    {
        return;
    }

    // A bullet in a square of a line's height; the next item follows on the line.
    const FontMetrics& font = state.current_font();
    const float square = font.line_height;
    const Vector2 position{.x = window->cursor.x, .y = window->cursor.y + window->line_text_offset};
    const UiRectangle rectangle{.left = position.x, .top = position.y, .right = position.x + square, .bottom = position.y + square};
    state.item_size(*window, {.x = square, .y = square});
    if (state.item_add(*window, rectangle, 0, false, "bullet", {}))
    {
        state.list_of(*window).add_circle_filled({.x = position.x + square * 0.5F, .y = position.y + square * 0.5F}, font.size * 0.20F,
                                                 state.color(StyleColor::TEXT_NORMAL));
    }
    same_line(0, state.style.frame_padding.x * 2);
}

auto
UiFrame::calculate_text_size(std::string_view text,
                             bool hide_text_after_double_hash,
                             float wrap_width) noexcept -> Vector2
{
    ContextState& state = *m_state;
    const TextLayout layout = state.layout_text(hide_text_after_double_hash ? ContextState::display_text(text) : text, wrap_width, "calculate_text_size");
    return text_size(state, layout);
}

void
UiFrame::separator() noexcept
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("separator");
    if (window == nullptr || window->skip_items)
    {
        return;
    }

    // A one-unit line from the cursor to the end of the content region (of the column, in columns).
    const float right = window->columns.active ? state.content_right(*window) : window->inner.right - window->scroll.x;
    const float left = window->columns.active ? window->cursor.x : window->inner.left - window->scroll.x;
    const UiRectangle rectangle{.left = left, .top = window->cursor.y, .right = right, .bottom = window->cursor.y + 1.0F};
    state.item_size(*window, {.x = 0, .y = 1.0F});
    if (state.item_add(*window, rectangle, 0, false, "separator", {}))
    {
        state.list_of(*window).add_rectangle_filled(rectangle, state.color(StyleColor::SEPARATOR));
    }
}

void
UiFrame::separator_text(std::string_view label) noexcept
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("separator_text");
    if (window == nullptr || window->skip_items)
    {
        return;
    }

    // Lines left and right of the text, at mid-height, as the style aligns and pads it.
    const Style& style = state.style;
    const TextLayout layout = state.layout_text(ContextState::display_text(label), 0, "separator_text");
    const Vector2 label_size = text_size(state, layout);
    const Vector2 padding = style.separator_text_padding;
    const float left = window->cursor.x;
    const float right = state.content_right(*window);
    const float height = std::max(label_size.y + padding.y * 2, style.separator_text_border_size);
    const UiRectangle rectangle{.left = left, .top = window->cursor.y, .right = right, .bottom = window->cursor.y + height};
    state.item_size(*window, {.x = 0, .y = height}, padding.y);
    if (!state.item_add(*window, rectangle, 0, false, "separator_text", label))
    {
        return;
    }
    DrawList& list = state.list_of(*window);
    const float text_x = left + padding.x + std::max(0.0F, right - left - label_size.x - padding.x * 2) * style.separator_text_align.x;
    const float line_y = std::floor(rectangle.top + height * 0.5F - style.separator_text_border_size * 0.5F);
    const Color line = state.color(StyleColor::SEPARATOR);
    const float thickness = style.separator_text_border_size;
    if (thickness > 0 && text_x - padding.x > left)
    {
        list.add_rectangle_filled({.left = left, .top = line_y, .right = text_x - padding.x, .bottom = line_y + thickness}, line);
    }
    if (thickness > 0 && text_x + label_size.x + padding.x < right)
    {
        list.add_rectangle_filled({.left = text_x + label_size.x + padding.x, .top = line_y, .right = right, .bottom = line_y + thickness}, line);
    }
    state.draw_text(list, layout, {.x = text_x, .y = rectangle.top + padding.y}, state.color(StyleColor::TEXT_NORMAL));
}

auto
UiFrame::button(std::string_view label,
                Vector2 size) noexcept -> bool
{
    WindowState* window = m_state->current_window("button");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }
    return button_item(*m_state, *window, label, size, m_state->style.frame_padding, 0.0F, ButtonFlag::NONE, "button");
}

auto
UiFrame::small_button(std::string_view label) noexcept -> bool
{
    // No vertical padding: on the text's baseline.
    WindowState* window = m_state->current_window("small_button");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }
    const Vector2 padding{.x = m_state->style.frame_padding.x, .y = 0.0F};
    return button_item(*m_state, *window, label, {}, padding, window->line_text_offset, ButtonFlag::NONE, "small_button");
}

auto
UiFrame::invisible_button(std::string_view id,
                          Vector2 size,
                          ButtonFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("invisible_button");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }
    if (!(size.x > 0 && size.y > 0))
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "invisible_button", "an invisible button needs a positive size", id);
        return false;
    }
    const std::uint32_t identifier = state.widget_id(id);
    const UiRectangle rectangle{.left = window->cursor.x, .top = window->cursor.y, .right = window->cursor.x + size.x, .bottom = window->cursor.y + size.y};
    state.item_size(*window, size);
    if (!state.item_add(*window, rectangle, identifier, true, "invisible_button", id))
    {
        return false;
    }
    const ButtonState button = state.button_behavior(*window, rectangle, identifier, flags);
    state.draw_focus(state.list_of(*window), rectangle, identifier, state.style.frame_rounding);
    return button.pressed;
}

auto
UiFrame::arrow_button(std::string_view id,
                      ArrowDirection direction) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("arrow_button");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }

    // A square button of a frame's height with an arrow of a line's height in it.
    const Style& style = state.style;
    const float line = state.current_font().line_height;
    const float side = line + style.frame_padding.y * 2;
    const std::uint32_t identifier = state.widget_id(id);
    const UiRectangle rectangle{.left = window->cursor.x, .top = window->cursor.y, .right = window->cursor.x + side, .bottom = window->cursor.y + side};
    state.item_size(*window, {.x = side, .y = side}, style.frame_padding.y);
    if (!state.item_add(*window, rectangle, identifier, true, "arrow_button", id))
    {
        return false;
    }
    const ButtonState button = state.button_behavior(*window, rectangle, identifier);
    DrawList& list = state.list_of(*window);
    const StyleColor fill = button.held && button.hovered ? StyleColor::BUTTON_ACTIVE : button.hovered ? StyleColor::BUTTON_HOVERED : StyleColor::BUTTON;
    state.draw_frame(list, rectangle, state.color(fill), style.frame_rounding);
    state.draw_focus(list, rectangle, identifier, style.frame_rounding);
    state.draw_arrow(list, {.x = rectangle.left + (side - line) * 0.5F, .y = rectangle.top + (side - line) * 0.5F}, line, direction,
                     state.color(StyleColor::TEXT_NORMAL));
    return button.pressed;
}

auto
UiFrame::checkbox(std::string_view label,
                  bool& value) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("checkbox");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }
    const Style& style = state.style;
    const float side = state.current_font().line_height + style.frame_padding.y * 2;
    const LabeledBox item = labeled_box(state, *window, label, {.x = side, .y = side}, "checkbox");
    if (!item.visible)
    {
        return false;
    }

    // A click anywhere on it (box or label) toggles the value.
    const ButtonState button = state.button_behavior(*window, item.total, item.id);
    if (button.pressed)
    {
        value = !value;
        state.mark_edited(item.id);
    }

    // The box, the check mark, the focus highlight, the label.
    DrawList& list = state.list_of(*window);
    state.draw_frame(list, item.box, frame_color(state, button), style.frame_rounding);
    if (value)
    {
        const float pad = std::max(1.0F, std::floor(side / 6.0F));
        const float size = side - pad * 2;
        const float thickness = std::max(size / 5.0F, 1.0F);
        const float third = size / 3.0F;
        const Vector2 bottom{.x = item.box.left + pad + third, .y = item.box.top + pad + size - third * 0.5F};
        const std::array points{Vector2{.x = bottom.x - third, .y = bottom.y - third}, bottom, Vector2{.x = bottom.x + third * 2, .y = bottom.y - third * 2}};
        list.add_polyline(points, state.color(StyleColor::CHECK_MARK), thickness);
    }
    state.draw_focus(list, item.total, item.id, style.frame_rounding);
    state.draw_text(list, item.text, item.text_position, state.color(StyleColor::TEXT_NORMAL));
    return button.pressed;
}

auto
UiFrame::radio_button(std::string_view label,
                      bool active) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("radio_button");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }
    const Style& style = state.style;
    const float side = state.current_font().line_height + style.frame_padding.y * 2;
    const LabeledBox item = labeled_box(state, *window, label, {.x = side, .y = side}, "radio_button");
    if (!item.visible)
    {
        return false;
    }
    const ButtonState button = state.button_behavior(*window, item.total, item.id);
    if (button.pressed)
    {
        state.mark_edited(item.id);
    }

    // A circle, a dot when active, the focus highlight, the label.
    DrawList& list = state.list_of(*window);
    const Vector2 center{.x = (item.box.left + item.box.right) * 0.5F, .y = (item.box.top + item.box.bottom) * 0.5F};
    const float radius = (side - 1.0F) * 0.5F;
    list.add_circle_filled(center, radius, frame_color(state, button));
    if (style.frame_border_size > 0)
    {
        list.add_circle(center, radius, state.color(StyleColor::BORDER), style.frame_border_size);
    }
    if (active)
    {
        const float pad = std::max(1.0F, std::floor(side / 6.0F));
        list.add_circle_filled(center, radius - pad, state.color(StyleColor::CHECK_MARK));
    }
    state.draw_focus(list, item.total, item.id, side * 0.5F);
    state.draw_text(list, item.text, item.text_position, state.color(StyleColor::TEXT_NORMAL));
    return button.pressed;
}

auto
UiFrame::toggle(std::string_view label,
                bool& value) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("toggle");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }
    const Style& style = state.style;
    const float height = state.current_font().line_height + style.frame_padding.y * 2;
    const LabeledBox item = labeled_box(state, *window, label, {.x = std::floor(height * 1.75F), .y = height}, "toggle");
    if (!item.visible)
    {
        return false;
    }
    const ButtonState button = state.button_behavior(*window, item.total, item.id);
    if (button.pressed)
    {
        value = !value;
        state.mark_edited(item.id);
    }

    // A rounded track (the check mark color when on), a knob at its left (off) or right (on) end.
    DrawList& list = state.list_of(*window);
    const Color track = value ? state.color(StyleColor::CHECK_MARK) : frame_color(state, button);
    list.add_rectangle_filled(item.box, button.hovered && value ? track.with_alpha(std::min(1.0F, track.alpha * 1.15F)) : track, height * 0.5F);
    if (style.frame_border_size > 0)
    {
        list.add_rectangle(item.box, state.color(StyleColor::BORDER), style.frame_border_size, height * 0.5F);
    }
    const float radius = height * 0.5F - 2.0F;
    const float knob_x = value ? item.box.right - height * 0.5F : item.box.left + height * 0.5F;
    list.add_circle_filled({.x = knob_x, .y = (item.box.top + item.box.bottom) * 0.5F}, radius, state.color(StyleColor::TEXT_NORMAL));
    state.draw_focus(list, item.total, item.id, height * 0.5F);
    state.draw_text(list, item.text, item.text_position, state.color(StyleColor::TEXT_NORMAL));
    return button.pressed;
}

auto
UiFrame::selectable(std::string_view label,
                    bool selected,
                    SelectableFlag flags,
                    Vector2 size) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("selectable");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }

    // Laid out at its label's size; its highlight spans the content width and half the item spacing around it,
    // so selectables in a column touch.
    const Style& style = state.style;
    const std::uint32_t id = state.widget_id(label);
    const TextLayout text = state.layout_text(ContextState::display_text(label), 0, "selectable");
    const Vector2 label_size = text_size(state, text);
    const float width = size.x > 0 ? size.x : std::max(label_size.x, state.content_right(*window) - window->cursor.x);
    const float height = size.y > 0 ? size.y : label_size.y;
    const Vector2 position = window->cursor;
    const UiRectangle hit{.left = position.x,
                          .top = position.y - std::floor(style.item_spacing.y * 0.5F),
                          .right = position.x + width,
                          .bottom = position.y + height + std::floor(style.item_spacing.y * 0.5F)};
    state.item_size(*window, {.x = size.x > 0 ? size.x : label_size.x, .y = height});
    if (!state.item_add(*window, hit, id, true, "selectable", label))
    {
        return false;
    }
    const bool was_disabled = state.disabled;
    state.disabled = state.disabled || has_any(flags, SelectableFlag::DISABLED);
    const ButtonState button = state.button_behavior(*window, hit, id);
    const bool pressed = button.pressed || (has_any(flags, SelectableFlag::ALLOW_DOUBLE_CLICK) && button.hovered && state.mouse[0].double_clicked);

    // The highlight (selected, hovered, held), the focus highlight, the label.
    DrawList& list = state.list_of(*window);
    const bool highlighted = has_any(flags, SelectableFlag::HIGHLIGHT);
    if (selected || button.hovered || button.held || highlighted)
    {
        const StyleColor fill = button.held && button.hovered ? StyleColor::HEADER_ACTIVE
                                : button.hovered || highlighted ? StyleColor::HEADER_HOVERED
                                                                : StyleColor::HEADER;
        list.add_rectangle_filled(hit, state.color(fill));
    }
    state.draw_focus(list, hit, id, 0.0F);
    const Vector2 text_position{.x = position.x + std::max(0.0F, width - label_size.x) * style.selectable_text_align.x,
                                .y = position.y + std::max(0.0F, height - label_size.y) * style.selectable_text_align.y};
    state.draw_text(list, text, text_position, state.color(StyleColor::TEXT_NORMAL));
    state.disabled = was_disabled;

    // Chosen in a popup (a combo's list, a menu): the popup closes (a submenu's with the menus it came from).
    if (pressed && !has_any(flags, SelectableFlag::NO_AUTO_CLOSE_POPUPS))
    {
        state.close_current_popup(static_cast<std::uint32_t>(window - state.windows.data()));
    }
    return pressed;
}

auto
UiFrame::selectable(std::string_view label,
                    bool* selected,
                    SelectableFlag flags,
                    Vector2 size) noexcept -> bool
{
    if (!selectable(label, selected != nullptr && *selected, flags, size))
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
