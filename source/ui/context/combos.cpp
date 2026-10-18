/// @file combos.cpp
/// @brief Combo boxes (a preview frame opening a popup list of selectables) and list boxes (a framed scrolling
///        region of selectables with the label on its right).
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

auto
UiFrame::begin_combo(std::string_view label,
                     std::string_view preview,
                     ComboFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("begin_combo");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }

    // The frame: the preview, then the arrow (a square of the frame's height), then the label.
    const Style& style = state.style;
    const std::uint32_t id = state.widget_id(label);
    const std::uint32_t popup = ContextState::hash(id, "#popup");
    const float line = state.current_font().line_height;
    const float height = line + style.frame_padding.y * 2;
    const bool arrow = !has_any(flags, ComboFlag::NO_ARROW_BUTTON);
    const bool show_preview = !has_any(flags, ComboFlag::NO_PREVIEW);
    const float arrow_size = arrow ? height : 0.0F;
    const TextLayout preview_layout = state.layout_text(preview, 0, "begin_combo");
    const TextLayout label_layout = state.layout_text(ContextState::display_text(label), 0, "begin_combo");
    const float width = !show_preview                                     ? arrow_size
                        : has_any(flags, ComboFlag::WIDTH_FIT_PREVIEW) ? preview_layout.width + style.frame_padding.x * 2 + arrow_size
                                                                          : state.take_item_width(*window);
    const Vector2 position = window->cursor;
    const UiRectangle frame{.left = position.x, .top = position.y, .right = position.x + std::max(width, 1.0F), .bottom = position.y + height};
    const float total = frame.width() + (label_layout.width > 0 ? style.item_inner_spacing.x + label_layout.width : 0.0F);
    state.item_size(*window, {.x = total, .y = height}, style.frame_padding.y);
    if (!state.item_add(*window, frame, id, true, "begin_combo", label))
    {
        return false;
    }

    // Pressed (on the mouse going down, or Space/Return): the popup opens below it (above when it does not fit),
    // at least as wide; a click that just closed it does not reopen it.
    const ButtonState button = state.button_behavior(*window, frame, id, ButtonFlag::PRESS_ON_CLICK);
    const bool open = state.popup_open(popup);
    if (button.pressed && !open && !std::ranges::contains(state.popups_closed_by_click, popup))
    {
        const std::uint32_t items = has_any(flags, ComboFlag::HEIGHT_SMALL)   ? COMBO_SMALL_ITEMS
                                    : has_any(flags, ComboFlag::HEIGHT_LARGE) ? COMBO_LARGE_ITEMS
                                    : has_any(flags, ComboFlag::HEIGHT_LARGEST) ? 0U
                                                                                : COMBO_REGULAR_ITEMS;
        const float maximum = items == 0 ? 0.0F : static_cast<float>(items) * (line + style.item_spacing.y) - style.item_spacing.y + style.window_padding.y * 2;
        if (PopupState* opened = state.open_popup(popup, {.x = frame.left, .y = frame.bottom}, frame.width(), maximum, frame.top))
        {
            opened->opener_item = id;
        }
    }
    else if (button.pressed && open)
    {
        state.close_popups_from(static_cast<std::size_t>(std::ranges::find(state.popups, popup, &PopupState::id) - state.popups.begin()));
    }

    // The frame and arrow, the preview (clipped before the arrow), the label.
    DrawList& list = state.list_of(*window);
    const bool popup_now = state.popup_open(popup);
    const Color frame_color = state.color(button.hovered || popup_now ? StyleColor::FRAME_BACKGROUND_HOVERED : StyleColor::FRAME_BACKGROUND);
    const UiRectangle arrow_box{.left = frame.right - arrow_size, .top = frame.top, .right = frame.right, .bottom = frame.bottom};
    if (show_preview)
    {
        state.draw_frame(list, frame, frame_color, style.frame_rounding);
    }
    if (arrow)
    {
        const Color arrow_color = state.color(button.hovered || popup_now ? StyleColor::BUTTON_HOVERED : StyleColor::BUTTON);
        list.add_rectangle_filled(arrow_box, arrow_color, show_preview ? 0.0F : style.frame_rounding);
        state.draw_arrow(list, {.x = arrow_box.left + (height - line) * 0.5F, .y = arrow_box.top + (height - line) * 0.5F}, line, ArrowDirection::DOWN,
                         state.color(StyleColor::TEXT_NORMAL));
    }
    state.draw_focus(list, frame, id, style.frame_rounding);
    if (show_preview)
    {
        list.push_clip_rectangle({.left = frame.left, .top = frame.top, .right = arrow_box.left - style.frame_padding.x * 0.5F, .bottom = frame.bottom});
        state.draw_text(list, preview_layout, {.x = frame.left + style.frame_padding.x, .y = frame.top + style.frame_padding.y}, state.color(StyleColor::TEXT_NORMAL));
        list.pop_clip_rectangle();
    }
    state.draw_text(list, label_layout, {.x = frame.right + style.item_inner_spacing.x, .y = frame.top + style.frame_padding.y}, state.color(StyleColor::TEXT_NORMAL));

    // While open, its items go into the popup (end_combo() makes the combo the last item again).
    state.combo_item = state.last_item;
    return state.begin_popup(*this, popup, UiWindowFlag::NONE);
}

void
UiFrame::end_combo() noexcept
{
    ContextState& state = *m_state;
    const WindowState* window = state.current_window_quiet();
    if (window == nullptr || !window->popup)
    {
        state.report(UiFrameProblem::END_WITHOUT_BEGIN, "end_combo", "end_combo() without a begin_combo() that returned true");
        return;
    }
    state.end_popup(*this);
    state.last_item = state.combo_item;
}

auto
UiFrame::begin_list_box(std::string_view label,
                        Vector2 size) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("begin_list_box");
    ListBoxLabel stored{};
    if (window == nullptr || window->skip_items)
    {
        // end_list_box() still follows: it finds an empty label and no region.
        try
        {
            state.list_box_labels.push_back(stored);
        }
        catch (...)
        {
            state.report(UiFrameProblem::CAPACITY_EXCEEDED, "begin_list_box", "the list box stack could not grow");
        }
        return false;
    }

    // A framed child region of the item width and about seven items (it scrolls), the label kept for the end.
    const Style& style = state.style;
    const float line = state.current_font().line_height;
    const Vector2 extent{.x = size.x > 0 ? size.x : state.take_item_width(*window),
                         .y = size.y > 0 ? size.y : std::floor((line + style.item_spacing.y) * 7.25F + style.frame_padding.y * 2)};
    const std::string_view text = ContextState::display_text(label);
    stored.size = static_cast<std::uint8_t>(std::min(text.size(), stored.text.size()));
    std::copy_n(text.begin(), stored.size, stored.text.begin());
    stored.begun = true;
    try
    {
        state.list_box_labels.push_back(stored);
    }
    catch (...)
    {
        state.report(UiFrameProblem::CAPACITY_EXCEEDED, "begin_list_box", "the list box stack could not grow");
    }
    begin_group();
    return begin_child(label, extent, ChildFlag::FRAME_STYLE);
}

void
UiFrame::end_list_box() noexcept
{
    ContextState& state = *m_state;
    if (state.list_box_labels.empty())
    {
        state.report(UiFrameProblem::END_WITHOUT_BEGIN, "end_list_box", "end_list_box() without begin_list_box()");
        return;
    }
    const ListBoxLabel stored = state.list_box_labels.back();
    state.list_box_labels.pop_back();
    if (!stored.begun)
    {
        return;
    }
    end_child();
    WindowState* parent = state.current_window("end_list_box");
    if (parent == nullptr)
    {
        return;
    }

    // The label on the right of the region's first line.
    const std::string_view text{stored.text.data(), stored.size};
    if (!text.empty())
    {
        const Style& style = state.style;
        same_line(0, style.item_inner_spacing.x);
        const TextLayout layout = state.layout_text(text, 0, "end_list_box");
        const float height = state.current_font().line_height + style.frame_padding.y * 2;
        const UiRectangle rectangle{.left = parent->cursor.x, .top = parent->cursor.y, .right = parent->cursor.x + layout.width, .bottom = parent->cursor.y + height};
        state.item_size(*parent, {.x = layout.width, .y = height}, style.frame_padding.y);
        if (state.item_add(*parent, rectangle, 0, false, "end_list_box", {}))
        {
            state.draw_text(state.list_of(*parent), layout, {.x = rectangle.left, .y = rectangle.top + style.frame_padding.y}, state.color(StyleColor::TEXT_NORMAL));
        }
    }
    end_group();
}

} // namespace gfx::ui
