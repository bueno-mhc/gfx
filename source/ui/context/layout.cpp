/// @file layout.cpp
/// @brief UiFrame's layout: lines (same_line(), new_line(), spacing(), dummy(), indentation), groups, the legacy
///        columns, the cursor, item widths, text wrapping and the identifier stack.
module gfx.ui.context;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.handles;
import gfx.ui.draw;
import gfx.ui.style;
import :state;

namespace gfx::ui
{

namespace
{

/// Left edge of column @p column's content.
auto
column_content_x(const ContextState& state,
                 const ColumnsState& columns,
                 std::uint32_t column) noexcept -> float
{
    const float start = columns.start_x + columns.width * static_cast<float>(column) / static_cast<float>(columns.count);
    return start + (column > 0 ? state.style.item_spacing.x : 0.0F);
}

/// Ends @p window's line when an item is on it (same_line() was called after it).
void
end_line(ContextState& state,
         WindowState& window) noexcept
{
    if (window.line_height > 0)
    {
        state.item_size(window, {});
    }
}

} // namespace

void
UiFrame::same_line(float offset_from_start,
                   float spacing) noexcept
{
    WindowState* window = m_state->current_window("same_line");
    if (window == nullptr || window->skip_items)
    {
        return;
    }

    // Back to the previous item's line: after it, or at an offset from the content start.
    if (offset_from_start != 0)
    {
        window->cursor.x = window->cursor_start.x + offset_from_start + std::max(spacing, 0.0F);
    }
    else
    {
        window->cursor.x = window->cursor_previous_line.x + (spacing < 0 ? m_state->style.item_spacing.x : spacing);
    }
    window->cursor.y = window->cursor_previous_line.y;
    window->line_height = window->previous_line_height;
    window->line_text_offset = window->previous_line_text_offset;
}

void
UiFrame::new_line() noexcept
{
    WindowState* window = m_state->current_window("new_line");
    if (window == nullptr || window->skip_items)
    {
        return;
    }

    // A line with items ends; an empty one is a line of text high.
    if (window->line_height > 0)
    {
        m_state->item_size(*window, {});
    }
    else
    {
        m_state->item_size(*window, {.x = 0, .y = m_state->current_font().line_height});
    }
}

void
UiFrame::spacing() noexcept
{
    WindowState* window = m_state->current_window("spacing");
    if (window != nullptr)
    {
        m_state->item_size(*window, {});
    }
}

void
UiFrame::dummy(Vector2 size) noexcept
{
    WindowState* window = m_state->current_window("dummy");
    if (window == nullptr || window->skip_items)
    {
        return;
    }
    const UiRectangle rectangle{.left = window->cursor.x, .top = window->cursor.y, .right = window->cursor.x + size.x, .bottom = window->cursor.y + size.y};
    m_state->item_size(*window, size);
    static_cast<void>(m_state->item_add(*window, rectangle, 0, false, "dummy", {}));
}

void
UiFrame::indent(float width) noexcept
{
    WindowState* window = m_state->current_window("indent");
    if (window == nullptr)
    {
        return;
    }
    window->indent += width != 0 ? width : m_state->style.indent_spacing;
    window->cursor.x = window->cursor_start.x + window->indent;
}

void
UiFrame::unindent(float width) noexcept
{
    WindowState* window = m_state->current_window("unindent");
    if (window == nullptr)
    {
        return;
    }
    window->indent -= width != 0 ? width : m_state->style.indent_spacing;
    window->cursor.x = window->cursor_start.x + window->indent;
}

void
UiFrame::begin_group() noexcept
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("begin_group");
    if (window == nullptr)
    {
        return;
    }

    // The group's lines start at its first item: the indentation moves there, the extent is measured anew.
    state.group_stack.push_back({.window = static_cast<std::uint32_t>(window - state.windows.data()),
                                 .start = window->cursor,
                                 .cursor_maximum = window->cursor_maximum,
                                 .indent = window->indent,
                                 .line_height = window->line_height,
                                 .line_text_offset = window->line_text_offset});
    window->indent = window->cursor.x - window->cursor_start.x;
    window->cursor_maximum = window->cursor;
    window->line_height = 0;
}

void
UiFrame::end_group() noexcept
{
    ContextState& state = *m_state;
    if (state.group_stack.empty())
    {
        state.report(UiFrameProblem::END_WITHOUT_BEGIN, "end_group", "end_group() without begin_group()");
        return;
    }
    const GroupState group = state.group_stack.back();
    state.group_stack.pop_back();
    WindowState& window = state.windows[group.window];
    if (state.window_stack.empty() || state.window_stack.back() != group.window)
    {
        state.report(UiFrameProblem::UNBALANCED_STACK, "end_group", "the group began in another window", window.name, window.id);
    }

    // The group's extent becomes one item, laid out where the group began.
    end_line(state, window);
    const UiRectangle rectangle{.left = group.start.x,
                                .top = group.start.y,
                                .right = std::max(window.cursor_maximum.x, group.start.x),
                                .bottom = std::max(window.cursor_maximum.y, group.start.y)};
    window.indent = group.indent;
    window.cursor = group.start;
    window.cursor_maximum = {.x = std::max(group.cursor_maximum.x, rectangle.right), .y = std::max(group.cursor_maximum.y, rectangle.bottom)};
    window.line_height = group.line_height;
    window.line_text_offset = group.line_text_offset;
    state.item_size(window, {.x = rectangle.width(), .y = rectangle.height()});
    static_cast<void>(state.item_add(window, rectangle, 0, false, "end_group", {}));
}

void
UiFrame::columns(std::uint32_t count,
                 std::string_view id,
                 bool borders) noexcept
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("columns");
    if (window == nullptr || window->skip_items)
    {
        return;
    }
    if (count == 0 || count > MAXIMUM_COLUMNS)
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "columns", "between 1 and MAXIMUM_COLUMNS columns", id);
        count = std::clamp<std::uint32_t>(count, 1, MAXIMUM_COLUMNS);
    }
    ColumnsState& columns = window->columns;

    // The set in effect ends: the cursor goes below its tallest column; its borders are drawn.
    if (columns.active)
    {
        end_line(state, *window);
        const float bottom = std::max(columns.maximum_y, window->cursor.y);
        if (columns.borders && !window->hidden)
        {
            DrawList& list = state.list_of(*window);
            for (std::uint32_t column = 1; column < columns.count; ++column)
            {
                const float x = std::floor(columns.start_x + columns.width * static_cast<float>(column) / static_cast<float>(columns.count));
                list.add_rectangle_filled({.left = x, .top = columns.start_y, .right = x + 1.0F, .bottom = bottom - state.style.item_spacing.y},
                                          state.color(StyleColor::SEPARATOR));
            }
        }
        window->indent = columns.saved_indent;
        window->cursor = {.x = window->cursor_start.x + window->indent, .y = bottom};
        window->cursor_maximum.y = std::max(window->cursor_maximum.y, bottom);
        window->line_height = 0;
        columns = ColumnsState{};
    }
    if (count <= 1)
    {
        return;
    }

    // A new set over the content region's width, starting on a new line.
    end_line(state, *window);
    const float start = window->cursor_start.x + window->indent;
    columns = ColumnsState{.active = true,
                           .id = ContextState::hash(state.id_seed(), id),
                           .count = count,
                           .current = 0,
                           .borders = borders,
                           .start_x = start,
                           .width = std::max(1.0F, window->inner.right - window->padding.x - window->scroll.x - start),
                           .start_y = window->cursor.y,
                           .row_start_y = window->cursor.y,
                           .maximum_y = window->cursor.y,
                           .saved_indent = window->indent};
    window->indent = column_content_x(state, columns, 0) - window->cursor_start.x;
}

void
UiFrame::next_column() noexcept
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("next_column");
    if (window == nullptr || window->skip_items)
    {
        return;
    }
    ColumnsState& columns = window->columns;
    if (!columns.active)
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "next_column", "next_column() without columns()");
        return;
    }

    // The column's extent counts for its row; after the last column, a new row below the tallest.
    end_line(state, *window);
    columns.maximum_y = std::max(columns.maximum_y, window->cursor.y);
    if (++columns.current == columns.count)
    {
        columns.current = 0;
        columns.row_start_y = columns.maximum_y;
    }
    window->indent = column_content_x(state, columns, columns.current) - window->cursor_start.x;
    window->cursor = {.x = window->cursor_start.x + window->indent, .y = columns.row_start_y};
    window->line_height = 0;
    window->line_text_offset = 0;
}

auto
UiFrame::column_index() const noexcept -> std::uint32_t
{
    const WindowState* window = m_state->current_window_quiet();
    return window != nullptr && window->columns.active ? window->columns.current : 0U;
}

auto
UiFrame::column_count() const noexcept -> std::uint32_t
{
    const WindowState* window = m_state->current_window_quiet();
    return window != nullptr && window->columns.active ? window->columns.count : 1U;
}

auto
UiFrame::column_width(std::int32_t index) const noexcept -> float
{
    const WindowState* window = m_state->current_window_quiet();
    if (window == nullptr)
    {
        return 0.0F;
    }
    if (!window->columns.active)
    {
        return m_state->content_right(*window) - (window->cursor_start.x + window->indent);
    }
    static_cast<void>(index);
    return window->columns.width / static_cast<float>(window->columns.count);
}

auto
UiFrame::cursor_position() const noexcept -> Vector2
{
    const WindowState* window = m_state->current_window_quiet();
    return window != nullptr ? Vector2{.x = window->cursor.x - window->cursor_start.x, .y = window->cursor.y - window->cursor_start.y} : Vector2{};
}

void
UiFrame::set_cursor_position(Vector2 position) noexcept
{
    WindowState* window = m_state->current_window("set_cursor_position");
    if (window == nullptr)
    {
        return;
    }
    set_cursor_screen_position({.x = window->cursor_start.x + position.x, .y = window->cursor_start.y + position.y});
}

auto
UiFrame::cursor_screen_position() const noexcept -> Vector2
{
    const WindowState* window = m_state->current_window_quiet();
    return window != nullptr ? window->cursor : Vector2{};
}

void
UiFrame::set_cursor_screen_position(Vector2 position) noexcept
{
    // The content extends to where the cursor goes (for scrolling).
    WindowState* window = m_state->current_window("set_cursor_screen_position");
    if (window == nullptr)
    {
        return;
    }
    window->cursor = position;
    window->cursor_maximum = {.x = std::max(window->cursor_maximum.x, position.x), .y = std::max(window->cursor_maximum.y, position.y)};
}

auto
UiFrame::content_region_available() const noexcept -> Vector2
{
    const WindowState* window = m_state->current_window_quiet();
    if (window == nullptr)
    {
        return {};
    }
    return {.x = std::max(0.0F, m_state->content_right(*window) - window->cursor.x), .y = std::max(0.0F, m_state->content_bottom(*window) - window->cursor.y)};
}

void
UiFrame::align_text_to_frame_padding() noexcept
{
    WindowState* window = m_state->current_window("align_text_to_frame_padding");
    if (window == nullptr)
    {
        return;
    }
    const Style& style = m_state->style;
    window->line_height = std::max(window->line_height, m_state->current_font().line_height + style.frame_padding.y * 2);
    window->line_text_offset = std::max(window->line_text_offset, style.frame_padding.y);
}

auto
UiFrame::text_line_height() const noexcept -> float
{
    return m_state->current_font().line_height;
}

auto
UiFrame::text_line_height_with_spacing() const noexcept -> float
{
    return m_state->current_font().line_height + m_state->style.item_spacing.y;
}

auto
UiFrame::frame_height() const noexcept -> float
{
    return m_state->current_font().line_height + m_state->style.frame_padding.y * 2;
}

auto
UiFrame::frame_height_with_spacing() const noexcept -> float
{
    return frame_height() + m_state->style.item_spacing.y;
}

void
UiFrame::push_item_width(float width) noexcept
{
    m_state->item_width_stack.push_back(width);
}

void
UiFrame::pop_item_width() noexcept
{
    if (m_state->item_width_stack.empty())
    {
        m_state->report(UiFrameProblem::UNBALANCED_STACK, "pop_item_width", "pop_item_width() without push_item_width()");
        return;
    }
    m_state->item_width_stack.pop_back();
}

void
UiFrame::set_next_item_width(float width) noexcept
{
    m_state->next_item_has_width = true;
    m_state->next_item_width = width;
}

auto
UiFrame::calculate_item_width() const noexcept -> float
{
    // What take_item_width() would give, without using the next item width up.
    const ContextState& state = *m_state;
    const WindowState* window = state.current_window_quiet();
    if (window == nullptr)
    {
        return 0.0F;
    }
    float width = state.next_item_has_width ? state.next_item_width : !state.item_width_stack.empty() ? state.item_width_stack.back() : window->item_width;
    if (width == 0)
    {
        width = window->item_width;
    }
    else if (width < 0)
    {
        width = state.content_right(*window) - window->cursor.x + width;
    }
    return std::max(1.0F, width);
}

void
UiFrame::push_text_wrap_position(float wrap_x) noexcept
{
    m_state->text_wrap_stack.push_back(wrap_x);
}

void
UiFrame::pop_text_wrap_position() noexcept
{
    if (m_state->text_wrap_stack.empty())
    {
        m_state->report(UiFrameProblem::UNBALANCED_STACK, "pop_text_wrap_position", "pop_text_wrap_position() without push_text_wrap_position()");
        return;
    }
    m_state->text_wrap_stack.pop_back();
}

void
UiFrame::push_id(std::string_view name) noexcept
{
    m_state->id_stack.push_back(m_state->widget_id(name));
}

void
UiFrame::push_id(std::int32_t value) noexcept
{
    // The integer's four bytes (little endian), hashed as a label.
    std::array<char, 4> bytes{};
    for (std::size_t byte = 0; byte < bytes.size(); ++byte)
    {
        bytes[byte] = static_cast<char>((static_cast<std::uint32_t>(value) >> (8U * byte)) & 0xFFU);
    }
    m_state->id_stack.push_back(m_state->widget_id({bytes.data(), bytes.size()}));
}

void
UiFrame::pop_id() noexcept
{
    // A window's own identifier stays until its end_window().
    ContextState& state = *m_state;
    const WindowState* window = state.current_window_quiet();
    const std::size_t floor = window != nullptr ? window->identifier_stack_base : 0;
    if (state.id_stack.size() <= floor)
    {
        state.report(UiFrameProblem::UNBALANCED_STACK, "pop_id", "pop_id() without push_id()");
        return;
    }
    state.id_stack.pop_back();
}

auto
UiFrame::identifier(std::string_view label) noexcept -> WidgetIdentifier
{
    // The seed's bytes and the label (from "###"), hashed by Identifier::from_name(): what widgets use.
    if (const std::size_t restart = label.find("###"); restart != std::string_view::npos)
    {
        label = label.substr(restart);
    }
    const std::uint32_t seed = m_state->id_seed();
    std::array<char, 256> local{};
    std::span<char> bytes = local;
    if (label.size() + 4 > local.size())
    {
        bytes = arena_characters(label.size() + 4);
        if (bytes.empty())
        {
            return {};
        }
    }
    for (std::size_t byte = 0; byte < 4; ++byte)
    {
        bytes[byte] = static_cast<char>((seed >> (8U * byte)) & 0xFFU);
    }
    std::ranges::copy(label, bytes.begin() + 4);
    return WidgetIdentifier::from_name({bytes.data(), label.size() + 4});
}

} // namespace gfx::ui
