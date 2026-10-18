/// @file trees.cpp
/// @brief Tree nodes and collapsing headers: their open state remembered by identifier, toggled by a click (on the
///        arrow, a double click, as their flags say), by the keyboard (Space, Return, Left, Right) or by a dragged
///        payload held over them; open nodes indent their children under their identifier.
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

/// The close button of a collapsing header: a cross in a circle shown while hovered.
auto
close_button(ContextState& state,
             WindowState& window,
             const UiRectangle& rectangle,
             std::uint32_t id) noexcept -> bool
{
    static_cast<void>(state.item_add(window, rectangle, id, false, "collapsing_header", {}));
    const ButtonState button = state.button_behavior(window, rectangle, id);
    DrawList& list = state.list_of(window);
    const Vector2 center{.x = (rectangle.left + rectangle.right) * 0.5F, .y = (rectangle.top + rectangle.bottom) * 0.5F};
    if (button.hovered || button.held)
    {
        list.add_circle_filled(center, rectangle.width() * 0.5F, state.color(button.held ? StyleColor::BUTTON_ACTIVE : StyleColor::BUTTON_HOVERED));
    }
    const float arm = rectangle.width() * 0.5F * 0.7071F - 1.0F;
    const Color text = state.color(StyleColor::TEXT_NORMAL);
    list.add_line({.x = center.x - arm, .y = center.y - arm}, {.x = center.x + arm, .y = center.y + arm}, text);
    list.add_line({.x = center.x + arm, .y = center.y - arm}, {.x = center.x - arm, .y = center.y + arm}, text);
    return button.pressed;
}

/// A tree node or collapsing header: its row, its behavior and drawing; returns whether it is open (and pushes
/// it when it is, unless NO_TREE_PUSH_ON_OPEN).
auto
tree_node_behavior(ContextState& state,
                   UiFrame& frame,
                   WindowState& window,
                   std::string_view label,
                   TreeNodeFlag flags,
                   bool* visible,
                   std::string_view operation) noexcept -> bool
{
    const Style& style = state.style;
    const FontMetrics& font = state.current_font();
    const std::uint32_t id = state.widget_id(label);
    const auto index = static_cast<std::uint32_t>(&window - state.windows.data());
    const bool framed = has_any(flags, TreeNodeFlag::FRAMED);
    const bool leaf = has_any(flags, TreeNodeFlag::LEAF);

    // Its open state: remembered, the flags' default the first time, or what set_next_item_open() asks.
    const std::optional<bool> remembered = state.remembered_open(id);
    bool open = remembered.value_or(has_any(flags, TreeNodeFlag::DEFAULT_OPEN));
    if (state.next_item_has_open)
    {
        const UiCondition condition = state.next_item_open_condition;
        const bool applies = condition == UiCondition::ALWAYS || (condition == UiCondition::APPEARING && window.appearing)
                             || ((condition == UiCondition::ONCE || condition == UiCondition::FIRST_USE_EVER) && !remembered.has_value());
        open = applies ? state.next_item_open : open;
        state.next_item_has_open = false;
    }
    if (leaf)
    {
        open = true;
    }
    else if (!remembered.has_value() || *remembered != open)
    {
        state.remember_open(id, open);
    }

    // Its row: a line of text (a frame with FRAMED or FRAME_PADDING); the arrow, then the label.
    const Vector2 padding = framed || has_any(flags, TreeNodeFlag::FRAME_PADDING)
                                ? style.frame_padding
                                : Vector2{.x = style.frame_padding.x, .y = std::min(window.line_text_offset, style.frame_padding.y)};
    const TextLayout text = state.layout_text(ContextState::display_text(label), 0, operation);
    const float height = font.line_height + padding.y * 2;
    const float arrow_offset = font.size + (framed ? padding.x * 3 : padding.x * 2);
    const Vector2 position = window.cursor;
    UiRectangle row{.left = position.x, .top = position.y, .right = state.content_right(window), .bottom = position.y + height};
    if (has_any(flags, TreeNodeFlag::SPAN_FULL_WIDTH))
    {
        row.left = window.inner.left + window.padding.x;
    }
    if (framed)
    {
        row.left -= std::floor(window.padding.x * 0.5F - 1.0F);
        row.right += std::floor(window.padding.x * 0.5F);
    }
    if (has_any(flags, TreeNodeFlag::SPAN_TEXT_WIDTH))
    {
        row.right = std::min(row.right, position.x + arrow_offset + text.width + padding.x);
    }
    UiRectangle hit = row;
    if (!framed && !has_any(flags, TreeNodeFlag::SPAN_AVAILABLE_WIDTH | TreeNodeFlag::SPAN_FULL_WIDTH))
    {
        hit.right = std::min(row.right, position.x + arrow_offset + text.width + (text.width > 0 ? style.item_spacing.x * 2 : 0.0F));
    }
    const float close_size = font.line_height;
    const UiRectangle close{.left = row.right - padding.x - close_size,
                            .top = row.top + std::max(0.0F, (height - close_size) * 0.5F),
                            .right = row.right - padding.x,
                            .bottom = row.top + std::max(0.0F, (height - close_size) * 0.5F) + close_size};
    if (visible != nullptr)
    {
        hit.right = std::min(hit.right, close.left - style.item_inner_spacing.x);
    }
    state.item_size(window, {.x = arrow_offset + text.width + padding.x, .y = height}, padding.y);
    const bool shown = state.item_add(window, hit, id, true, operation, label);
    if (shown)
    {
        // The mouse: a click toggles it (on the arrow at once; elsewhere when released over it), or only on the
        // arrow, or a double click; the keyboard: Space or Return, Left closes, Right opens.
        const float arrow_left = row.left;
        const float arrow_right = position.x + font.size + padding.x * 2;
        const float press_x = state.active_id == id ? state.mouse[0].clicked_position.x : state.mouse_position.x;
        const bool over_arrow = press_x >= arrow_left && press_x < arrow_right;
        const ButtonState button = state.button_behavior(window, hit, id, over_arrow ? ButtonFlag::PRESS_ON_CLICK : ButtonFlag::NONE);
        bool toggled = false;
        if (!leaf)
        {
            const bool keyboard = state.keyboard_focused(window, id);
            const bool keyboard_activated = keyboard && (state.presses(Key::SPACE) > 0 || state.presses(Key::RETURN) > 0 || state.presses(Key::KEYPAD_ENTER) > 0);
            if (button.pressed)
            {
                toggled = keyboard_activated || !has_any(flags, TreeNodeFlag::OPEN_ON_ARROW | TreeNodeFlag::OPEN_ON_DOUBLE_CLICK);
                toggled = toggled || (has_any(flags, TreeNodeFlag::OPEN_ON_ARROW) && over_arrow);
            }
            toggled = toggled || (has_any(flags, TreeNodeFlag::OPEN_ON_DOUBLE_CLICK) && button.hovered && state.mouse[0].double_clicked);
            toggled = toggled || (keyboard && open && state.presses(Key::LEFT) > 0) || (keyboard && !open && state.presses(Key::RIGHT) > 0);

            // A dragged payload held over a closed node opens it.
            const bool payload_over = state.drag_drop.active && state.mouse_present && state.hovered_window == index
                                      && contains(hit.intersection(window.clip), state.mouse_position);
            toggled = toggled || (payload_over && !open && state.drag_hold_opens(id));
            if (toggled)
            {
                open = !open;
                state.remember_open(id, open);
                state.mark_edited(id);
            }
        }

        // The frame (FRAMED) or the highlight (hovered, held, SELECTED), the arrow or bullet, the label.
        DrawList& list = state.list_of(window);
        const bool selected = has_any(flags, TreeNodeFlag::SELECTED);
        const StyleColor fill = button.held && button.hovered ? StyleColor::HEADER_ACTIVE : button.hovered ? StyleColor::HEADER_HOVERED : StyleColor::HEADER;
        if (framed)
        {
            state.draw_frame(list, row, state.color(fill), style.frame_rounding);
        }
        else if (button.hovered || selected)
        {
            list.add_rectangle_filled(row, state.color(fill));
        }
        state.draw_focus(list, row, id, framed ? style.frame_rounding : 0.0F);
        const Color text_color = state.color(StyleColor::TEXT_NORMAL);
        const float text_top = row.top + padding.y;
        if (has_any(flags, TreeNodeFlag::BULLET))
        {
            list.add_circle_filled({.x = position.x + padding.x + font.size * 0.5F, .y = text_top + font.line_height * 0.5F}, font.size * 0.20F, text_color);
        }
        else if (!leaf)
        {
            const float scale = framed ? 1.0F : 0.70F;
            const float size = font.size * scale;
            state.draw_arrow(list, {.x = position.x + padding.x + (font.size - size) * 0.5F, .y = text_top + (font.line_height - size) * 0.5F}, size,
                             open ? ArrowDirection::DOWN : ArrowDirection::RIGHT, text_color);
        }
        const bool clipped = text.width > row.right - (position.x + arrow_offset);
        if (clipped)
        {
            list.push_clip_rectangle(row);
        }
        state.draw_text(list, text, {.x = position.x + arrow_offset, .y = text_top}, text_color);
        if (clipped)
        {
            list.pop_clip_rectangle();
        }

        // A collapsing header's close button hides it.
        if (visible != nullptr && close_button(state, window, close, ContextState::hash(id, "#CLOSE")))
        {
            *visible = false;
        }
        state.last_item.id = id;
        state.last_item.rectangle = hit;
    }

    // Open: its children follow, indented under its identifier (also when it is clipped).
    if (open && !has_any(flags, TreeNodeFlag::NO_TREE_PUSH_ON_OPEN))
    {
        frame.indent();
        state.id_stack.push_back(id);
        try
        {
            state.tree_stack.push_back(index);
        }
        catch (...)
        {
            state.report(UiFrameProblem::CAPACITY_EXCEEDED, operation, "the tree stack could not grow");
        }
    }
    return open;
}

} // namespace

auto
UiFrame::tree_node(std::string_view label,
                   TreeNodeFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("tree_node");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }
    return tree_node_behavior(state, *this, *window, label, flags, nullptr, "tree_node");
}

void
UiFrame::tree_push(std::string_view id) noexcept
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("tree_push");
    if (window == nullptr)
    {
        return;
    }
    indent();
    push_id(id);
    try
    {
        state.tree_stack.push_back(static_cast<std::uint32_t>(window - state.windows.data()));
    }
    catch (...)
    {
        state.report(UiFrameProblem::CAPACITY_EXCEEDED, "tree_push", "the tree stack could not grow");
    }
}

void
UiFrame::tree_pop() noexcept
{
    ContextState& state = *m_state;
    const WindowState* window = state.current_window_quiet();
    if (state.tree_stack.empty() || window == nullptr || state.tree_stack.back() != static_cast<std::uint32_t>(window - state.windows.data()))
    {
        state.report(UiFrameProblem::END_WITHOUT_BEGIN, "tree_pop", "tree_pop() without an open tree_node() or tree_push() in this window");
        return;
    }
    state.tree_stack.pop_back();
    unindent();
    pop_id();
}

auto
UiFrame::collapsing_header(std::string_view label,
                           TreeNodeFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("collapsing_header");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }
    return tree_node_behavior(state, *this, *window, label, flags | TreeNodeFlag::COLLAPSING_HEADER, nullptr, "collapsing_header");
}

auto
UiFrame::collapsing_header(std::string_view label,
                           bool& visible,
                           TreeNodeFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("collapsing_header");
    if (window == nullptr || window->skip_items || !visible)
    {
        return false;
    }
    return tree_node_behavior(state, *this, *window, label, flags | TreeNodeFlag::COLLAPSING_HEADER, &visible, "collapsing_header");
}

void
UiFrame::set_next_item_open(bool open,
                            UiCondition condition) noexcept
{
    m_state->next_item_has_open = true;
    m_state->next_item_open = open;
    m_state->next_item_open_condition = condition;
}

} // namespace gfx::ui
