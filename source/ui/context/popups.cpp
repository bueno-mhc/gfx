/// @file popups.cpp
/// @brief Popups (combo lists, color pickers): a stack of windows over the others, opened from a window or from
///        another popup, closed by a click outside them, by Escape, by a choice, or when no longer submitted.
module gfx.ui.context;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.style;
import :state;

namespace gfx::ui
{

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

void
ContextState::open_popup(std::uint32_t id,
                         Vector2 position,
                         float minimum_width,
                         float maximum_height,
                         float flip_bottom) noexcept
{
    // Kept: the popups up to the one this is opened from (none when opened from an ordinary window).
    const WindowState* window = current_window_quiet();
    const std::size_t from = window == nullptr ? popups.size() : popup_level_of(window->root);
    const std::size_t keep = from < popups.size() ? from + 1 : 0;
    if (keep < popups.size() && popups[keep].id == id)
    {
        return;
    }
    close_popups_from(keep);
    try
    {
        popups.push_back({.id = id,
                          .opener = window == nullptr ? NO_WINDOW : window->root,
                          .position = position,
                          .minimum_width = minimum_width,
                          .maximum_height = maximum_height,
                          .flip_bottom = flip_bottom,
                          .alive = true,
                          .opened_frame = frame});
    }
    catch (...)
    {
        report(UiFrameProblem::CAPACITY_EXCEEDED, "open_popup", "the popup stack could not grow");
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
    std::array<char, 24> name{};
    const auto end = std::format_to_n(name.data(), static_cast<std::ptrdiff_t>(name.size()), "##popup_{:08X}", id).out;
    const std::string_view window_name{name.data(), static_cast<std::size_t>(end - name.data())};

    // Placed in the display: above its opener when it does not fit below (by last frame's size), else moved in.
    Vector2 position = popup.position;
    const std::uint32_t existing = find_window(hash(0, window_name));
    if (existing != NO_WINDOW && windows[existing].size.x > 0)
    {
        const Vector2 size = windows[existing].size;
        if (position.y + size.y > display_size.y)
        {
            position.y = popup.flip_bottom >= size.y ? popup.flip_bottom - size.y : display_size.y - size.y;
        }
        position.x = std::min(position.x, display_size.x - size.x);
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
    const bool open = frame_object_reference.begin_window(window_name,
                                                          flags | UiWindowFlag::NO_TITLE_BAR | UiWindowFlag::NO_RESIZE | UiWindowFlag::NO_MOVE
                                                              | UiWindowFlag::NO_SAVED_SETTINGS | UiWindowFlag::NO_DOCKING
                                                              | UiWindowFlag::ALWAYS_AUTO_RESIZE | UiWindowFlag::NO_FOCUS_ON_APPEARING);
    frame_object_reference.pop_style_variable(2);
    frame_object_reference.pop_style_color();

    // The popup's window, focused when it opens (ended at once when it shows nothing).
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
    }
    return open;
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
ContextState::update_popups() noexcept
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

    // A click outside closes the popups above the one under the mouse (all of them when it is under none).
    const bool clicked = mouse[0].clicked || mouse[1].clicked || mouse[2].clicked;
    if (clicked && !popups.empty())
    {
        const std::size_t level = popup_level_of(hovered_root);
        const std::size_t keep = level < popups.size() ? level + 1 : 0;
        for (std::size_t closed = keep; closed < popups.size(); ++closed)
        {
            popups_closed_by_click.push_back(popups[closed].id);
        }
        close_popups_from(keep);
    }

    // Escape closes the top popup (unless a text field uses it).
    if (keys_pressed[std::to_underlying(Key::ESCAPE)] > 0 && !popups.empty() && text_edit.id == 0)
    {
        close_popups_from(popups.size() - 1);
    }
}

} // namespace gfx::ui
