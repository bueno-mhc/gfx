/// @file popups.cpp
/// @brief Popups: a stack of windows over the others (combo lists, color pickers, menus, the application's popups
///        and modals), opened from a window or from another popup, closed by a click outside them, by Escape, by a
///        choice, or when no longer submitted. Modals block everything below them. The public popup API.
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

/// The mouse button a context popup opens with (the right one unless a button flag says), as an index of
/// ContextState::mouse.
auto
popup_button(PopupFlag flags) noexcept -> std::size_t
{
    const MouseButton button = has_any(flags, PopupFlag::MOUSE_BUTTON_LEFT)     ? MouseButton::LEFT
                               : has_any(flags, PopupFlag::MOUSE_BUTTON_MIDDLE) ? MouseButton::MIDDLE
                                                                                : MouseButton::RIGHT;
    return std::to_underlying(button);
}

/// The name of popup @p id's window.
struct PopupName
{
    std::array<char, 24> characters{};
    std::size_t size = 0;

    [[nodiscard]] auto view() const noexcept -> std::string_view
    {
        return {characters.data(), size};
    }
};

auto
popup_name(std::uint32_t id) noexcept -> PopupName
{
    PopupName name{};
    name.size = static_cast<std::size_t>(std::format_to_n(name.characters.data(), static_cast<std::ptrdiff_t>(name.characters.size()), "##popup_{:08X}", id).out
                                         - name.characters.data());
    return name;
}

} // namespace

auto
ContextState::popup_open(std::uint32_t id) const noexcept -> bool
{
    return std::ranges::any_of(popups, [id](const PopupState& popup) { return popup.id == id; });
}

auto
ContextState::popup_level_of(std::uint32_t window) const noexcept -> std::size_t
{
    if (window == NO_WINDOW)
    {
        return popups.size();
    }
    const std::uint32_t root = windows[window].root;
    for (std::size_t level = 0; level < popups.size(); ++level)
    {
        if (popups[level].window == root)
        {
            return level;
        }
    }
    return popups.size();
}

auto
ContextState::current_popup_level() const noexcept -> std::size_t
{
    const WindowState* window = current_window_quiet();
    if (window == nullptr)
    {
        return 0;
    }
    const std::size_t level = popup_level_of(window->root);
    return level < popups.size() ? level + 1 : 0;
}

auto
ContextState::top_modal_level() const noexcept -> std::size_t
{
    for (std::size_t level = popups.size(); level > 0; --level)
    {
        if (popups[level - 1].modal)
        {
            return level - 1;
        }
    }
    return popups.size();
}

auto
ContextState::open_popup(std::uint32_t id,
                         Vector2 position,
                         float minimum_width,
                         float maximum_height,
                         float flip_bottom,
                         float flip_right) noexcept -> PopupState*
{
    // Kept: the popups up to the one this is opened from (none when opened from an ordinary window); already open
    // at that level, it stays (the popups above it close).
    const WindowState* window = current_window_quiet();
    const std::size_t level = current_popup_level();
    if (level < popups.size() && popups[level].id == id)
    {
        close_popups_from(level + 1);
        return &popups[level];
    }
    close_popups_from(level);
    try
    {
        popups.push_back({.id = id,
                          .opener = window == nullptr ? NO_WINDOW : window->root,
                          .position = position,
                          .minimum_width = minimum_width,
                          .maximum_height = maximum_height,
                          .flip_bottom = flip_bottom,
                          .flip_right = flip_right,
                          .alive = true,
                          .opened_frame = frame});
        return &popups.back();
    }
    catch (...)
    {
        report(UiFrameProblem::CAPACITY_EXCEEDED, "open_popup", "the popup stack could not grow");
        return nullptr;
    }
}

auto
ContextState::begin_popup(UiFrame& frame_object_reference,
                          std::uint32_t id,
                          UiWindowFlag flags) noexcept -> bool
{
    const auto found = std::ranges::find(popups, id, &PopupState::id);
    if (found == popups.end())
    {
        return false;
    }
    PopupState& popup = *found;
    popup.alive = true;

    // A window of its own (named by its identifier), at its place, sized to its content within its limits, drawn
    // with the popup colors and borders.
    const PopupName name = popup_name(id);

    // Placed in the display: on the other side of its opener when it does not fit (by last frame's size), else moved in.
    Vector2 position = popup.position;
    const std::uint32_t existing = find_window(hash(0, name.view()));
    if (existing != NO_WINDOW && windows[existing].size.x > 0)
    {
        const Vector2 size = windows[existing].size;
        if (position.y + size.y > display_size.y)
        {
            position.y = popup.flip_bottom >= size.y ? popup.flip_bottom - size.y : display_size.y - size.y;
        }
        if (position.x + size.x > display_size.x)
        {
            position.x = popup.flip_right >= size.x ? popup.flip_right - size.x : display_size.x - size.x;
        }
        position = {.x = std::max(position.x, 0.0F), .y = std::max(position.y, 0.0F)};
    }
    frame_object_reference.set_next_window_position(position, UiCondition::ALWAYS);
    if (popup.minimum_width > 0 || popup.maximum_height > 0)
    {
        frame_object_reference.set_next_window_size_constraints({.x = popup.minimum_width, .y = 0}, {.x = 0, .y = popup.maximum_height});
    }
    frame_object_reference.push_style_color(StyleColor::WINDOW_BACKGROUND, colors[std::to_underlying(StyleColor::POPUP_BACKGROUND)]);
    frame_object_reference.push_style_variable(StyleVariable::WINDOW_ROUNDING, style.popup_rounding);
    frame_object_reference.push_style_variable(StyleVariable::WINDOW_BORDER_SIZE, style.popup_border_size);
    const bool open = frame_object_reference.begin_window(name.view(),
                                                          flags | UiWindowFlag::NO_TITLE_BAR | UiWindowFlag::NO_RESIZE | UiWindowFlag::NO_MOVE
                                                              | UiWindowFlag::NO_SAVED_SETTINGS | UiWindowFlag::NO_DOCKING
                                                              | UiWindowFlag::ALWAYS_AUTO_RESIZE | UiWindowFlag::NO_FOCUS_ON_APPEARING);
    frame_object_reference.pop_style_variable(2);
    frame_object_reference.pop_style_color();

    // The popup's window, focused when it opens (ended at once when it shows nothing); opened from the keyboard,
    // its first item takes the focus.
    const std::uint32_t index = window_stack.back();
    windows[index].popup = true;
    if (popup.window != index || popup.opened_frame == frame)
    {
        popup.window = index;
        focus_window(index);
    }
    if (!open)
    {
        frame_object_reference.end_window();
        return false;
    }
    if (popup.focus_first)
    {
        focus_here_requested = true;
        popup.focus_first = false;
    }
    return true;
}

auto
ContextState::begin_modal_popup(UiFrame& frame_object_reference,
                                std::uint32_t id,
                                std::string_view name,
                                bool* open,
                                UiWindowFlag flags) noexcept -> bool
{
    const auto found = std::ranges::find(popups, id, &PopupState::id);
    if (found == popups.end())
    {
        return false;
    }
    const auto level = static_cast<std::size_t>(found - popups.begin());
    PopupState& popup = *found;
    popup.alive = true;
    popup.modal = true;

    // A titled window, centered on the display when it opens (unless placed by code: on its first two frames, the
    // second knowing its size), with the popup background.
    if (!next_window.has_position && frame <= popup.opened_frame + 1)
    {
        frame_object_reference.set_next_window_position({.x = display_size.x * 0.5F, .y = display_size.y * 0.5F}, UiCondition::ALWAYS, {.x = 0.5F, .y = 0.5F});
    }
    frame_object_reference.push_style_color(StyleColor::WINDOW_BACKGROUND, colors[std::to_underlying(StyleColor::POPUP_BACKGROUND)]);
    const UiWindowFlag window_flags = flags | UiWindowFlag::NO_COLLAPSE | UiWindowFlag::NO_SAVED_SETTINGS | UiWindowFlag::NO_DOCKING;
    const bool visible = open != nullptr ? frame_object_reference.begin_window(name, *open, window_flags) : frame_object_reference.begin_window(name, window_flags);
    frame_object_reference.pop_style_color();
    const std::uint32_t index = window_stack.back();
    windows[index].popup = true;
    if (popup.window != index || popup.opened_frame == frame)
    {
        popup.window = index;
        focus_window(index);
    }

    // Its close button closes it.
    if (open != nullptr && !*open)
    {
        frame_object_reference.end_window();
        close_popups_from(level);
        return false;
    }
    if (!visible)
    {
        frame_object_reference.end_window();
        return false;
    }
    if (popup.focus_first)
    {
        focus_here_requested = true;
        popup.focus_first = false;
    }
    return true;
}

auto
ContextState::title_focus_window() const noexcept -> std::uint32_t
{
    // From the focused popup down the stack to the ordinary window that opened the first one.
    std::uint32_t window = focused_window;
    for (std::size_t level = popup_level_of(window); level < popups.size(); level = popup_level_of(window))
    {
        window = popups[level].opener;
    }
    return window;
}

void
ContextState::end_popup(UiFrame& frame_object_reference) noexcept
{
    frame_object_reference.end_window();
}

void
ContextState::close_popups_from(std::size_t level) noexcept
{
    if (level >= popups.size())
    {
        return;
    }

    // The focus goes back to where the first closed popup was opened from.
    const std::uint32_t opener = popups[level].opener;
    const bool focused_inside = focused_window != NO_WINDOW && popup_level_of(focused_window) >= level && popup_level_of(focused_window) < popups.size();
    popups.resize(level);
    if (focused_inside)
    {
        focused_window = opener;
    }
}

void
ContextState::close_current_popup(std::uint32_t window) noexcept
{
    // A submenu closes the popups it was opened from too (not a modal).
    std::size_t level = popup_level_of(window);
    if (level >= popups.size())
    {
        return;
    }
    while (level > 0 && popups[level].submenu && !popups[level - 1].modal)
    {
        --level;
    }
    close_popups_from(level);
}

auto
ContextState::update_popups() noexcept -> bool
{
    // Popups their owner did not submit last frame close (with those above them).
    popups_closed_by_click.clear();
    for (std::size_t level = 0; level < popups.size(); ++level)
    {
        if (!popups[level].alive)
        {
            close_popups_from(level);
            break;
        }
    }
    for (PopupState& popup : popups)
    {
        popup.alive = false;
    }

    // A click outside closes the popups above the one under the mouse (all of them when it is under none), never
    // the top-most modal or what is below it.
    const std::size_t modal = top_modal_level();
    const std::size_t floor = modal < popups.size() ? modal + 1 : 0;
    const bool clicked = mouse[0].clicked || mouse[1].clicked || mouse[2].clicked;
    if (clicked && !popups.empty())
    {
        const std::size_t level = popup_level_of(hovered_root);
        const std::size_t keep = std::max(level < popups.size() ? level + 1 : 0, floor);
        for (std::size_t closed = keep; closed < popups.size(); ++closed)
        {
            popups_closed_by_click.push_back(popups[closed].id);
        }
        close_popups_from(keep);
    }
    if (text_edit.id != 0 || popups.empty())
    {
        return false;
    }

    // The navigation (unless a text field uses the keyboard): CANCEL closes the top popup (not a modal), MOVE_LEFT
    // the focused submenu; the focus goes back to the item that opened it.
    const auto close_to_opener = [this](std::size_t level)
    {
        const std::uint32_t opener_item = popups[level].opener_item;
        close_popups_from(level);
        if (opener_item != 0)
        {
            focus_id = opener_item;
            focus_visible = true;
        }
    };
    if (navigation_pressed(UiNavigationCommand::CANCEL) && !popups.back().modal)
    {
        consume_navigation(UiNavigationCommand::CANCEL);
        close_to_opener(popups.size() - 1);
        return true;
    }
    const std::size_t focused = popup_level_of(focused_window);
    if (navigation_triggered(UiNavigationCommand::MOVE_LEFT) && focused < popups.size() && popups[focused].submenu)
    {
        consume_navigation(UiNavigationCommand::MOVE_LEFT);
        close_to_opener(focused);
    }
    return false;
}

// ===================================================================================================== public API

void
UiFrame::open_popup(std::string_view id,
                    PopupFlag flags) noexcept
{
    ContextState& state = *m_state;
    if (!state.frame_active)
    {
        state.report(UiFrameProblem::FRAME_NOT_ACTIVE, "open_popup", "call it between UiContext::begin_frame() and end_frame()", id);
        return;
    }

    // At the current level: above the popup this is called from, else over the windows.
    const std::uint32_t popup = state.widget_id(id);
    const std::size_t level = state.current_popup_level();
    if (has_any(flags, PopupFlag::NO_OPEN_OVER_EXISTING_POPUP) && level < state.popups.size())
    {
        return;
    }
    const bool open_here = level < state.popups.size() && state.popups[level].id == popup;
    if (open_here && has_any(flags, PopupFlag::NO_REOPEN))
    {
        return;
    }
    if (PopupState* opened = state.open_popup(popup, state.mouse_position, 0, 0))
    {
        // Reopened: at the mouse again, and focused again.
        opened->position = state.mouse_position;
        opened->opened_frame = state.frame;
        opened->opener_item = state.last_item.id;
    }
}

auto
UiFrame::begin_popup(std::string_view id,
                     UiWindowFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    if (!state.frame_active)
    {
        state.report(UiFrameProblem::FRAME_NOT_ACTIVE, "begin_popup", "call it between UiContext::begin_frame() and end_frame()", id);
        return false;
    }
    return state.begin_popup(*this, state.widget_id(id), flags);
}

auto
UiFrame::begin_modal(std::string_view name,
                     UiWindowFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    if (!state.frame_active)
    {
        state.report(UiFrameProblem::FRAME_NOT_ACTIVE, "begin_modal", "call it between UiContext::begin_frame() and end_frame()", name);
        return false;
    }
    return state.begin_modal_popup(*this, state.widget_id(name), name, nullptr, flags);
}

auto
UiFrame::begin_modal(std::string_view name,
                     bool& open,
                     UiWindowFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    if (!state.frame_active)
    {
        state.report(UiFrameProblem::FRAME_NOT_ACTIVE, "begin_modal", "call it between UiContext::begin_frame() and end_frame()", name);
        return false;
    }
    return state.begin_modal_popup(*this, state.widget_id(name), name, &open, flags);
}

void
UiFrame::end_popup() noexcept
{
    ContextState& state = *m_state;
    const WindowState* window = state.current_window_quiet();
    if (window == nullptr || window->child || !window->popup)
    {
        state.report(UiFrameProblem::END_WITHOUT_BEGIN, "end_popup", "end_popup() without a begin_popup() or begin_modal() that returned true");
        return;
    }
    state.end_popup(*this);
}

auto
UiFrame::begin_popup_context_item(std::string_view id,
                                  PopupFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("begin_popup_context_item");
    if (window == nullptr || window->skip_items)
    {
        return false;
    }

    // The popup of the last item (its own identifier unless one is given), opened when the button is released
    // over it.
    const std::uint32_t popup = id.empty() ? state.last_item.id : state.widget_id(id);
    if (popup == 0)
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "begin_popup_context_item", "the last item has no identifier: give the popup one");
        return false;
    }
    // From the keyboard (CONTEXT_MENU on the focused item) or an assistive technology (SHOW_CONTEXT_MENU): under the
    // item, its first item focused.
    const LastItem& item = state.last_item;
    const bool keyboard = item.id != 0 && item.window < state.windows.size() && state.keyboard_focused(state.windows[item.window], item.id)
                          && state.navigation_pressed(UiNavigationCommand::CONTEXT_MENU);
    const bool requested = state.item_request(AccessibilityAction::SHOW_CONTEXT_MENU) != nullptr;
    const bool clicked = state.mouse[popup_button(flags)].released && is_item_hovered(HoveredFlag::ALLOW_WHEN_BLOCKED_BY_ACTIVE_ITEM);
    if (clicked || keyboard || requested)
    {
        const std::size_t level = state.current_popup_level();
        const bool open_here = level < state.popups.size() && state.popups[level].id == popup;
        if (!(open_here && has_any(flags, PopupFlag::NO_REOPEN)) && !(has_any(flags, PopupFlag::NO_OPEN_OVER_EXISTING_POPUP) && level < state.popups.size()))
        {
            const Vector2 position = clicked ? state.mouse_position : Vector2{.x = item.rectangle.left, .y = item.rectangle.bottom};
            if (PopupState* opened = state.open_popup(popup, position, 0, 0))
            {
                opened->position = position;
                opened->opened_frame = state.frame;
                opened->opener_item = item.id;
                opened->focus_first = !clicked;
            }
        }
        if (keyboard)
        {
            state.consume_navigation(UiNavigationCommand::CONTEXT_MENU);
        }
    }
    return state.begin_popup(*this, popup, UiWindowFlag::NONE);
}

auto
UiFrame::begin_popup_context_window(std::string_view id,
                                    PopupFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* window = state.current_window("begin_popup_context_window");
    if (window == nullptr)
    {
        return false;
    }

    // Opened when the button is released over the window (with NO_OPEN_OVER_ITEMS: over no item).
    const std::uint32_t popup = state.widget_id(id.empty() ? std::string_view{"window_context"} : id);
    const auto index = static_cast<std::uint32_t>(window - state.windows.data());
    const bool over_window = state.hovered_window == index && state.mouse_present;
    const bool over_item = state.hovered_id != 0;
    const bool clicked = state.mouse[popup_button(flags)].released && over_window && !(has_any(flags, PopupFlag::NO_OPEN_OVER_ITEMS) && over_item);

    // From the keyboard: CONTEXT_MENU no item used while the window has the focus (with NO_OPEN_OVER_ITEMS: no item
    // focused), at the window's content start, its first item focused.
    const bool keyboard = window->root == state.focused_window && state.navigation_pressed(UiNavigationCommand::CONTEXT_MENU)
                          && !(has_any(flags, PopupFlag::NO_OPEN_OVER_ITEMS) && state.focus_id != 0);
    if (clicked || keyboard)
    {
        const std::size_t level = state.current_popup_level();
        const bool open_here = level < state.popups.size() && state.popups[level].id == popup;
        if (!(open_here && has_any(flags, PopupFlag::NO_REOPEN)) && !(has_any(flags, PopupFlag::NO_OPEN_OVER_EXISTING_POPUP) && level < state.popups.size()))
        {
            const Vector2 position = clicked ? state.mouse_position : Vector2{.x = window->inner.left + window->padding.x, .y = window->inner.top + window->padding.y};
            if (PopupState* opened = state.open_popup(popup, position, 0, 0))
            {
                opened->position = position;
                opened->opened_frame = state.frame;
                opened->focus_first = !clicked;
            }
        }
        if (keyboard)
        {
            state.consume_navigation(UiNavigationCommand::CONTEXT_MENU);
        }
    }
    return state.begin_popup(*this, popup, UiWindowFlag::NONE);
}

void
UiFrame::close_current_popup() noexcept
{
    ContextState& state = *m_state;
    const WindowState* window = state.current_window_quiet();
    if (window == nullptr || state.popup_level_of(window->root) >= state.popups.size())
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "close_current_popup", "call it inside a popup, a modal or a menu");
        return;
    }
    state.close_current_popup(window->root);
}

auto
UiFrame::is_popup_open(std::string_view id,
                       PopupFlag flags) noexcept -> bool
{
    // At the current level (popups opened from where this is called), or at any with ANY_POPUP_LEVEL; any popup
    // with ANY_POPUP_IDENTIFIER.
    ContextState& state = *m_state;
    const bool any_level = has_any(flags, PopupFlag::ANY_POPUP_LEVEL);
    const bool any_identifier = has_any(flags, PopupFlag::ANY_POPUP_IDENTIFIER);
    const std::size_t level = state.current_popup_level();
    if (any_identifier)
    {
        return any_level ? !state.popups.empty() : level < state.popups.size();
    }
    const std::uint32_t popup = state.widget_id(id);
    if (any_level)
    {
        return state.popup_open(popup);
    }
    return level < state.popups.size() && state.popups[level].id == popup;
}

} // namespace gfx::ui
