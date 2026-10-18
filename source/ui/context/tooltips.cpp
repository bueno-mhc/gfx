/// @file tooltips.cpp
/// @brief Tooltips: one window per frame at the mouse, over every window and popup, never hovered; item tooltips
///        shown once the mouse has stayed still over the item for TOOLTIP_DELAY_SECONDS.
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

constexpr std::string_view TOOLTIP_WINDOW = "##tooltip";

/// A key for the last item: its identifier, or its window and rectangle when it has none.
auto
last_item_key(const ContextState& state) noexcept -> std::uint32_t
{
    const LastItem& item = state.last_item;
    if (item.id != 0)
    {
        return item.id;
    }
    const std::array<float, 4> corners{item.rectangle.left, item.rectangle.top, item.rectangle.right, item.rectangle.bottom};
    std::array<char, sizeof(corners)> bytes{};
    std::memcpy(bytes.data(), corners.data(), sizeof(corners));
    return ContextState::hash(item.window + 1U, {bytes.data(), bytes.size()});
}

} // namespace

void
ContextState::begin_tooltip_window(UiFrame& frame_object_reference,
                                   bool hidden) noexcept
{
    // At the mouse, on the display (on the other side of the mouse when it does not fit, by last frame's size);
    // a second tooltip of the frame appends to the first.
    const std::uint32_t existing = find_window(hash(0, TOOLTIP_WINDOW));
    const bool appending = existing != NO_WINDOW && windows[existing].begin_count > 0;
    if (!appending)
    {
        Vector2 position{.x = mouse_position.x + TOOLTIP_OFFSET_X, .y = mouse_position.y + TOOLTIP_OFFSET_Y};
        if (existing != NO_WINDOW && windows[existing].size.x > 0)
        {
            const Vector2 size = windows[existing].size;
            if (position.x + size.x > display_size.x)
            {
                position.x = std::max(0.0F, mouse_position.x - TOOLTIP_OFFSET_X * 0.5F - size.x);
            }
            if (position.y + size.y > display_size.y)
            {
                position.y = std::max(0.0F, mouse_position.y - TOOLTIP_OFFSET_Y * 0.5F - size.y);
            }
        }
        frame_object_reference.set_next_window_position(position, UiCondition::ALWAYS);
    }
    frame_object_reference.push_style_color(StyleColor::WINDOW_BACKGROUND, colors[std::to_underlying(StyleColor::POPUP_BACKGROUND)]);
    frame_object_reference.push_style_variable(StyleVariable::WINDOW_ROUNDING, style.popup_rounding);
    frame_object_reference.push_style_variable(StyleVariable::WINDOW_BORDER_SIZE, style.popup_border_size);
    static_cast<void>(frame_object_reference.begin_window(TOOLTIP_WINDOW, UiWindowFlag::NO_TITLE_BAR | UiWindowFlag::NO_MOVE | UiWindowFlag::NO_RESIZE
                                                                              | UiWindowFlag::NO_SAVED_SETTINGS | UiWindowFlag::ALWAYS_AUTO_RESIZE
                                                                              | UiWindowFlag::NO_FOCUS_ON_APPEARING | UiWindowFlag::NO_DOCKING
                                                                              | UiWindowFlag::NO_INPUTS));
    frame_object_reference.pop_style_variable(2);
    frame_object_reference.pop_style_color();
    WindowState& window = windows[window_stack.back()];
    window.tooltip = true;
    window.hidden = window.hidden || hidden;
    tooltip_frame = frame;
    ++tooltip_depth;
}

auto
UiFrame::begin_tooltip() noexcept -> bool
{
    ContextState& state = *m_state;
    if (!state.frame_active)
    {
        state.report(UiFrameProblem::FRAME_NOT_ACTIVE, "begin_tooltip", "call it between UiContext::begin_frame() and end_frame()");
        return false;
    }
    state.begin_tooltip_window(*this, false);
    return true;
}

void
UiFrame::end_tooltip() noexcept
{
    ContextState& state = *m_state;
    const WindowState* window = state.current_window_quiet();
    if (window == nullptr || !window->tooltip || state.tooltip_depth == 0)
    {
        state.report(UiFrameProblem::END_WITHOUT_BEGIN, "end_tooltip", "end_tooltip() without begin_tooltip()");
        return;
    }
    --state.tooltip_depth;
    end_window();
}

void
UiFrame::tooltip(std::string_view text) noexcept
{
    if (begin_tooltip())
    {
        text_unformatted(text);
        end_tooltip();
    }
}

auto
UiFrame::begin_item_tooltip() noexcept -> bool
{
    // The last item hovered (disabled ones too), the mouse still over it for the delay; once shown, the tooltip
    // stays while the mouse moves over the item.
    ContextState& state = *m_state;
    if (!is_item_hovered(HoveredFlag::ALLOW_WHEN_DISABLED))
    {
        return false;
    }
    const std::uint32_t key = last_item_key(state);
    state.tooltip_seen = true;
    if (key != state.tooltip_key)
    {
        state.tooltip_key = key;
        state.tooltip_since = state.time;
        state.tooltip_shown = false;
    }
    if (!state.tooltip_shown && (state.mouse_delta.x != 0 || state.mouse_delta.y != 0))
    {
        state.tooltip_since = state.time;
    }
    state.tooltip_shown = state.tooltip_shown || state.time - state.tooltip_since >= TOOLTIP_DELAY_SECONDS;
    return state.tooltip_shown && begin_tooltip();
}

void
UiFrame::item_tooltip(std::string_view text) noexcept
{
    if (begin_item_tooltip())
    {
        text_unformatted(text);
        end_tooltip();
    }
}

} // namespace gfx::ui
