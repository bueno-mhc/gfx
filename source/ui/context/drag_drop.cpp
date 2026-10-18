/// @file drag_drop.cpp
/// @brief Drag and drop: an item dragged past the drag threshold becomes a source carrying a typed payload (copied)
///        and a preview tooltip; items under the mouse become targets that accept payloads of their type, the
///        smallest one winning; the payload is delivered to the winner when the button is released.
module gfx.ui.context;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.draw;
import gfx.ui.style;
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

/// An identifier for an item that has none: its window and rectangle.
auto
rectangle_key(std::uint32_t window,
              const UiRectangle& rectangle) noexcept -> std::uint32_t
{
    const std::array<float, 4> corners{rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
    std::array<char, sizeof(corners)> bytes{};
    std::memcpy(bytes.data(), corners.data(), sizeof(corners));
    return ContextState::hash(window + 0x9E3779B9U, {bytes.data(), bytes.size()});
}

/// The index of @p button in ContextState::mouse.
constexpr auto
button_index(MouseButton button) noexcept -> std::size_t
{
    return std::to_underlying(button);
}

} // namespace

auto
ContextState::drag_hold_opens(std::uint32_t id) noexcept -> bool
{
    // A payload held over the same item for DRAG_DROP_HOLD_TO_OPEN_SECONDS opens it (once per hold).
    DragDropState& drag = drag_drop;
    if (!drag.active || has_any(drag.source_flags, DragDropFlag::SOURCE_NO_HOLD_TO_OPEN_OTHERS))
    {
        return false;
    }
    drag.hold_seen = true;
    if (drag.hold_id != id)
    {
        drag.hold_id = id;
        drag.hold_start = time;
        return false;
    }
    if (time - drag.hold_start >= DRAG_DROP_HOLD_TO_OPEN_SECONDS)
    {
        drag.hold_start = std::numeric_limits<double>::infinity();
        return true;
    }
    return false;
}

auto
UiFrame::begin_drag_drop_source(DragDropFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    DragDropState& drag = state.drag_drop;
    WindowState* window = state.current_window("begin_drag_drop_source");
    if (window == nullptr && !has_any(flags, DragDropFlag::SOURCE_EXTERNAL))
    {
        return false;
    }
    if (drag.in_source)
    {
        state.report(UiFrameProblem::UNBALANCED_STACK, "begin_drag_drop_source", "begin_drag_drop_source() inside another source: call end_drag_drop_source() first");
        return false;
    }

    // The source: outside the UI (always dragging), or the last item, held and dragged past the drag threshold.
    // An item without an identifier becomes active on a press (it has no behavior of its own).
    std::uint32_t source = 0;
    MouseButton button = MouseButton::LEFT;
    if (has_any(flags, DragDropFlag::SOURCE_EXTERNAL))
    {
        source = ContextState::hash(0, "#external_drag_drop_source");
    }
    else
    {
        const LastItem& item = state.last_item;
        source = item.id;
        if (source == 0)
        {
            if (!has_any(flags, DragDropFlag::SOURCE_ALLOW_NULL_IDENTIFIER))
            {
                state.report(UiFrameProblem::INVALID_ARGUMENT, "begin_drag_drop_source",
                             "the last item has no identifier: make an interactive item the source, or use DragDropFlag::SOURCE_ALLOW_NULL_IDENTIFIER");
                return false;
            }
            source = rectangle_key(item.window, item.rectangle);
            if (state.mouse[0].clicked && is_item_hovered())
            {
                state.set_active(source, *window);
                state.active_button = MouseButton::LEFT;
            }
            if (state.active_id == source)
            {
                state.active_id_alive = true;
                if (!state.mouse[0].down)
                {
                    state.clear_active();
                }
            }
        }
        if (state.active_id != source && !(drag.active && drag.source_id == source))
        {
            return false;
        }
        button = drag.active && drag.source_id == source ? drag.button : state.active_button;
        const auto& mouse = state.mouse[button_index(button)];
        const bool dragging = mouse.down && (mouse.drag_maximum >= state.description.input.drag_threshold || (drag.active && drag.source_id == source));
        if (!dragging)
        {
            return false;
        }
    }

    // A new drag: the payload starts empty.
    if (!drag.active || drag.source_id != source)
    {
        drag.active = true;
        drag.external = has_any(flags, DragDropFlag::SOURCE_EXTERNAL);
        drag.source_id = source;
        drag.source_window = window == nullptr ? ContextState::NO_WINDOW : static_cast<std::uint32_t>(window - state.windows.data());
        drag.button = button;
        drag.type.clear();
        drag.data.clear();
        drag.payload_set = false;
        drag.accepted_frame = 0;
        drag.delivered = false;
        drag.data_frame = state.frame;
    }
    drag.source_flags = flags;
    drag.in_source = true;
    if (!has_any(flags, DragDropFlag::SOURCE_NO_DISABLE_HOVER))
    {
        state.last_item.hovered = false;
    }

    // The preview: a tooltip at the mouse (laid out but not drawn while a target hides it).
    drag.source_tooltip = !has_any(flags, DragDropFlag::SOURCE_NO_PREVIEW_TOOLTIP);
    if (drag.source_tooltip)
    {
        state.begin_tooltip_window(*this, drag.hide_preview);
    }
    return true;
}

auto
UiFrame::set_drag_drop_payload(std::string_view type,
                               std::span<const std::byte> data,
                               UiCondition condition) noexcept -> bool
{
    ContextState& state = *m_state;
    DragDropState& drag = state.drag_drop;
    if (!drag.in_source)
    {
        state.report(UiFrameProblem::END_WITHOUT_BEGIN, "set_drag_drop_payload", "call it after begin_drag_drop_source() returned true", type);
        return false;
    }
    if (type.empty() || type.size() > MAXIMUM_PAYLOAD_TYPE_SIZE)
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "set_drag_drop_payload", "a payload type has 1 to MAXIMUM_PAYLOAD_TYPE_SIZE bytes", type);
        return false;
    }

    // Copied (with UiCondition::ONCE, only the first time of the drag).
    if (condition != UiCondition::ONCE || !drag.payload_set)
    {
        if (data.size() > drag.data.capacity())
        {
            ++state.allocations;
            state.report(UiFrameProblem::CAPACITY_EXCEEDED, "set_drag_drop_payload", "a payload larger than UiContextLimits::drag_drop_payload_bytes", type);
        }
        try
        {
            drag.type.assign(type);
            drag.data.assign(data.begin(), data.end());
            drag.payload_set = true;
        }
        catch (...)
        {
            state.report(UiFrameProblem::CAPACITY_EXCEEDED, "set_drag_drop_payload", "the payload could not be copied", type);
            return false;
        }
    }
    drag.data_frame = state.frame;
    return drag.accepted_frame != 0 && drag.accepted_frame + 1 >= state.frame;
}

void
UiFrame::end_drag_drop_source() noexcept
{
    ContextState& state = *m_state;
    DragDropState& drag = state.drag_drop;
    if (!drag.in_source)
    {
        state.report(UiFrameProblem::END_WITHOUT_BEGIN, "end_drag_drop_source", "end_drag_drop_source() without a begin_drag_drop_source() that returned true");
        return;
    }
    if (drag.source_tooltip)
    {
        end_tooltip();
    }
    drag.in_source = false;
    drag.source_tooltip = false;
}

auto
UiFrame::begin_drag_drop_target() noexcept -> bool
{
    // The last item, under the mouse in the hovered window's hierarchy, while a payload is dragged (not its own).
    ContextState& state = *m_state;
    DragDropState& drag = state.drag_drop;
    if (!drag.active || state.last_item.window == ContextState::NO_WINDOW || !state.mouse_present)
    {
        return false;
    }
    if (drag.in_target)
    {
        state.report(UiFrameProblem::UNBALANCED_STACK, "begin_drag_drop_target", "begin_drag_drop_target() inside another target: call end_drag_drop_target() first");
        return false;
    }
    const LastItem& item = state.last_item;
    const WindowState& window = state.windows[item.window];
    const UiRectangle rectangle = item.rectangle.intersection(window.clip);
    if (state.hovered_root != window.root || !contains(rectangle, state.mouse_position))
    {
        return false;
    }
    const std::uint32_t target = item.id != 0 ? item.id : rectangle_key(item.window, item.rectangle);
    if (target == drag.source_id)
    {
        return false;
    }
    drag.in_target = true;
    drag.target_id = target;
    drag.target_rectangle = rectangle;
    return true;
}

auto
UiFrame::accept_drag_drop_payload(std::string_view type,
                                  DragDropFlag flags) noexcept -> std::optional<UiDragDropPayload>
{
    ContextState& state = *m_state;
    DragDropState& drag = state.drag_drop;
    if (!drag.in_target)
    {
        state.report(UiFrameProblem::END_WITHOUT_BEGIN, "accept_drag_drop_payload", "call it after begin_drag_drop_target() returned true", type);
        return std::nullopt;
    }
    if (!drag.payload_set || (!type.empty() && drag.type != type))
    {
        return std::nullopt;
    }

    // The smallest target under the mouse accepts it; the one that accepted it last frame shows it and receives it
    // when the button is released.
    const float area = drag.target_rectangle.width() * drag.target_rectangle.height();
    if (area > drag.accept_area)
    {
        return std::nullopt;
    }
    drag.accept_id = drag.target_id;
    drag.accept_area = area;
    drag.accepted_frame = state.frame;
    drag.hide_preview_next = drag.hide_preview_next || has_any(flags, DragDropFlag::ACCEPT_NO_PREVIEW_TOOLTIP);
    const bool previewed = drag.accept_id_previous == drag.target_id;
    if (previewed && !has_any(flags, DragDropFlag::ACCEPT_NO_DRAW_DEFAULT_RECTANGLE))
    {
        const WindowState* window = state.current_window_quiet();
        if (window != nullptr)
        {
            const UiRectangle& r = drag.target_rectangle;
            state.list_of(*window).add_rectangle({.left = r.left - 3.5F, .top = r.top - 3.5F, .right = r.right + 3.5F, .bottom = r.bottom + 3.5F},
                                                 state.color(StyleColor::DRAG_DROP_TARGET), 2.0F);
        }
    }
    const bool delivery = previewed && !state.mouse[button_index(drag.button)].down;
    if (delivery)
    {
        drag.delivered = true;
    }
    if (!delivery && !has_any(flags, DragDropFlag::ACCEPT_BEFORE_DELIVERY))
    {
        return std::nullopt;
    }
    return UiDragDropPayload{.type = drag.type, .data = drag.data, .delivery = delivery};
}

void
UiFrame::end_drag_drop_target() noexcept
{
    ContextState& state = *m_state;
    if (!state.drag_drop.in_target)
    {
        state.report(UiFrameProblem::END_WITHOUT_BEGIN, "end_drag_drop_target", "end_drag_drop_target() without a begin_drag_drop_target() that returned true");
        return;
    }
    state.drag_drop.in_target = false;
}

} // namespace gfx::ui
