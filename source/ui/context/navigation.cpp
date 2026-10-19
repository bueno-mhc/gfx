/// @file navigation.cpp
/// @brief Keyboard and gamepad navigation: the keymap that turns keys into navigation commands, the commands of
///        UiFrameInput::navigation (a gamepad, an input context), the focus moving between items (Tab order,
///        directions scored as Dear ImGui scores them), between windows and to the menu bar, and the scrolling that
///        shows the item the focus moved to.
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

/// Commands a held key, button or stick repeats.
constexpr UiNavigationCommand REPEATING_COMMANDS = UiNavigationCommand::MOVE_LEFT | UiNavigationCommand::MOVE_RIGHT | UiNavigationCommand::MOVE_UP
                                                   | UiNavigationCommand::MOVE_DOWN | UiNavigationCommand::NEXT | UiNavigationCommand::PREVIOUS;

/// The directional moves.
constexpr UiNavigationCommand MOVES = UiNavigationCommand::MOVE_LEFT | UiNavigationCommand::MOVE_RIGHT | UiNavigationCommand::MOVE_UP
                                      | UiNavigationCommand::MOVE_DOWN;

/// The main menu bar's window (menus.cpp).
constexpr std::string_view MAIN_MENU_BAR_WINDOW = "##main_menu_bar";

/// The index of command @p command (one bit) in NavigationState's arrays.
constexpr auto
command_index(UiNavigationCommand command) noexcept -> std::size_t
{
    return static_cast<std::size_t>(std::countr_zero(std::to_underlying(command)));
}

/// The command of index @p index.
constexpr auto
command_of(std::size_t index) noexcept -> UiNavigationCommand
{
    return static_cast<UiNavigationCommand>(1U << index);
}

/// The signed distance from interval [@p first, @p last] to [@p other_first, @p other_last] (0 when they overlap).
constexpr auto
interval_distance(float first,
                  float last,
                  float other_first,
                  float other_last) noexcept -> float
{
    if (last < other_first)
    {
        return last - other_first;
    }
    if (other_last < first)
    {
        return first - other_last;
    }
    return 0.0F;
}

/// The direction of (@p x, @p y) from the origin: the axis of the larger magnitude.
constexpr auto
quadrant(float x,
         float y) noexcept -> UiNavigationCommand
{
    if (std::abs(x) > std::abs(y))
    {
        return x > 0 ? UiNavigationCommand::MOVE_RIGHT : UiNavigationCommand::MOVE_LEFT;
    }
    return y > 0 ? UiNavigationCommand::MOVE_DOWN : UiNavigationCommand::MOVE_UP;
}

/// @p value between @p first and @p last at @p ratio.
constexpr auto
between(float first,
        float last,
        float ratio) noexcept -> float
{
    return first + (last - first) * ratio;
}

} // namespace

// ===================================================================================================== commands

void
ContextState::update_navigation(const UiFrameInput& input) noexcept
{
    NavigationState& state = navigation;
    state.presses.fill(0);
    state.repeats.fill(0);
    state.consumed = UiNavigationCommand::NONE;
    state.external = UiNavigationCommand::NONE;
    state.move = UiNavigationCommand::NONE;
    const auto add = [&state](UiNavigationCommand command, bool repeat)
    {
        const std::size_t index = std::min(command_index(command), NAVIGATION_COMMANDS - 1);
        state.presses[index] = static_cast<std::uint8_t>(std::min(state.presses[index] + 1, 255));
        state.repeats[index] = static_cast<std::uint8_t>(std::min(state.repeats[index] + (repeat ? 1 : 0), 255));
    };

    // The keyboard keymap: Tab (with Control: the windows), the arrows, Space, Return, Escape, Alt alone and the
    // Application key (or Shift+F10). While a field is edited, its keys are its own: only Tab and Escape remain.
    UiNavigationCommand keyboard_down = UiNavigationCommand::NONE;
    const bool editing = text_edit.id != 0;
    if (!input.focused || !input.mouse_buttons.empty())
    {
        state.alt_alone = false;
    }
    if (description.input.keyboard_navigation && input.focused)
    {
        const bool shift = has_any(input.modifiers, KeyModifier::SHIFT);
        const bool control = has_any(input.modifiers, KeyModifier::CONTROL);
        const bool other = has_any(input.modifiers, KeyModifier::ALT | KeyModifier::SUPER);
        for (const UiKeyEvent& event : input.keys)
        {
            const bool alt = event.key == Key::LEFT_ALT || event.key == Key::RIGHT_ALT;
            if (!event.down)
            {
                // Alt released with nothing pressed since it went down: the menu bar.
                if (alt && state.alt_alone && !editing)
                {
                    add(UiNavigationCommand::MENU, false);
                }
                state.alt_alone = alt ? false : state.alt_alone;
                continue;
            }
            if (alt)
            {
                state.alt_alone = event.repeat ? state.alt_alone : true;
                continue;
            }
            state.alt_alone = false;
            switch (event.key)
            {
                case Key::TAB:
                    add(control ? (shift ? UiNavigationCommand::PREVIOUS_WINDOW : UiNavigationCommand::NEXT_WINDOW)
                                : (shift ? UiNavigationCommand::PREVIOUS : UiNavigationCommand::NEXT),
                        event.repeat);
                    break;
                case Key::ESCAPE:
                    add(UiNavigationCommand::CANCEL, event.repeat);
                    break;
                case Key::LEFT:
                case Key::RIGHT:
                case Key::UP:
                case Key::DOWN:
                    if (!editing)
                    {
                        add(event.key == Key::LEFT    ? UiNavigationCommand::MOVE_LEFT
                            : event.key == Key::RIGHT ? UiNavigationCommand::MOVE_RIGHT
                            : event.key == Key::UP    ? UiNavigationCommand::MOVE_UP
                                                      : UiNavigationCommand::MOVE_DOWN,
                            event.repeat);
                    }
                    break;
                case Key::SPACE:
                    if (!editing && !control && !other)
                    {
                        add(UiNavigationCommand::ACTIVATE, event.repeat);
                    }
                    break;
                case Key::RETURN:
                case Key::KEYPAD_ENTER:
                    if (!editing && !other)
                    {
                        add(UiNavigationCommand::ACTIVATE, event.repeat);
                        add(UiNavigationCommand::INPUT, event.repeat);
                    }
                    break;
                case Key::APPLICATION:
                case Key::MENU:
                    add(UiNavigationCommand::CONTEXT_MENU, event.repeat);
                    break;
                case Key::F10:
                    if (shift)
                    {
                        add(UiNavigationCommand::CONTEXT_MENU, event.repeat);
                    }
                    break;
                default:
                    break;
            }
        }
        const auto held = [this](Key key) { return keys_down[std::to_underlying(key)]; };
        if (!editing && (held(Key::SPACE) || held(Key::RETURN) || held(Key::KEYPAD_ENTER)))
        {
            keyboard_down = keyboard_down | UiNavigationCommand::ACTIVATE;
        }
    }

    // The commands of an input context or a gamepad: presses, and repeats of held moves.
    const UiNavigationCommand down = input.focused ? input.navigation.down : UiNavigationCommand::NONE;
    const UiNavigationCommand pressed = input.focused ? input.navigation.pressed : UiNavigationCommand::NONE;
    const double delay = description.input.repeat_delay_seconds;
    const double interval = std::max(description.input.repeat_interval_seconds, 1.0e-3);
    const auto ticks = [delay, interval](double held) { return held < delay ? -1.0 : std::floor((held - delay) / interval); };
    for (std::size_t index = 0; index < NAVIGATION_COMMANDS; ++index)
    {
        const UiNavigationCommand command = command_of(index);
        double& held = state.held_seconds[index];
        if (has_any(pressed, command) || (has_any(down, command) && held < 0))
        {
            // Pressed, or held from now: once.
            add(command, false);
            state.external = state.external | command;
            held = has_any(down, command) ? 0.0 : -1.0;
        }
        else if (has_any(down, command) && held >= 0)
        {
            const double previous = held;
            held += delta_time;
            if (has_any(REPEATING_COMMANDS, command) && ticks(held) > ticks(previous))
            {
                add(command, true);
            }
        }
        else
        {
            held = -1.0;
        }
    }
    state.down = keyboard_down | down;
    state.scroll = input.focused ? input.navigation.scroll : Vector2{};

    // The directional move of the frame (none while a field is edited).
    if (!editing)
    {
        for (const UiNavigationCommand move : {UiNavigationCommand::MOVE_LEFT, UiNavigationCommand::MOVE_RIGHT, UiNavigationCommand::MOVE_UP,
                                               UiNavigationCommand::MOVE_DOWN})
        {
            if (state.presses[command_index(move)] > 0)
            {
                state.move = move;
                break;
            }
        }
    }
    state.best_id = 0;
    state.best_window = NO_WINDOW;
    state.best_box = std::numeric_limits<float>::max();
    state.best_center = std::numeric_limits<float>::max();
    state.menu_bar_first = 0;
}

void
ContextState::begin_navigation() noexcept
{
    NavigationState& state = navigation;
    const auto back_from_menu_bar = [this, &state]
    {
        const std::uint32_t window = state.menu_return_window;
        const std::uint32_t id = state.menu_return_id;
        state.menu_return_window = NO_WINDOW;
        state.menu_return_id = 0;
        focus_window(window);
        focus_id = id;
        focus_visible = id != 0;
    };

    // A click leaves the menu bar's keyboard mode.
    if (mouse[0].clicked || mouse[1].clicked || mouse[2].clicked)
    {
        state.menu_return_window = NO_WINDOW;
        state.menu_return_id = 0;
    }

    // Tab and Shift+Tab step through the focused window's items.
    focus_step = 0;
    if (focused_window != NO_WINDOW)
    {
        focus_step = navigation_triggered(UiNavigationCommand::NEXT) ? 1 : navigation_triggered(UiNavigationCommand::PREVIOUS) ? -1 : 0;
    }

    // MENU: into the menu bar of the focused window, else of the main menu bar (its first item, found this frame);
    // again: back to the item focused before, the bar's menus closed.
    if (navigation_pressed(UiNavigationCommand::MENU))
    {
        consume_navigation(UiNavigationCommand::MENU);
        if (state.menu_return_window != NO_WINDOW || state.menu_return_id != 0)
        {
            const std::size_t level = popup_level_of(focused_window);
            if (level < popups.size() && popups[level].menu)
            {
                close_popups_from(level);
            }
            back_from_menu_bar();
        }
        else
        {
            const std::uint32_t main_menu_bar = find_window(hash(0, MAIN_MENU_BAR_WINDOW));
            const bool own_bar = focused_window != NO_WINDOW && windows[focused_window].menu_bar_height > 0 && !windows[focused_window].popup;
            const std::uint32_t target = own_bar ? focused_window
                                         : main_menu_bar != NO_WINDOW && windows[main_menu_bar].was_active ? main_menu_bar
                                                                                                          : NO_WINDOW;
            if (target != NO_WINDOW)
            {
                state.menu_bar_window = target;
                state.menu_return_window = focused_window != NO_WINDOW ? focused_window : target;
                state.menu_return_id = focus_id;
            }
        }
    }

    // CANCEL that closed no popup: back from the menu bar, else the keyboard focus is dropped.
    if (navigation_pressed(UiNavigationCommand::CANCEL))
    {
        if (state.menu_return_window != NO_WINDOW && popup_level_of(focused_window) == popups.size())
        {
            consume_navigation(UiNavigationCommand::CANCEL);
            back_from_menu_bar();
        }
        else
        {
            focus_id = 0;
            focus_visible = false;
        }
    }
}

auto
ContextState::navigation_count(UiNavigationCommand command) const noexcept -> std::uint32_t
{
    const std::size_t index = command_index(command);
    return index < NAVIGATION_COMMANDS ? navigation.presses[index] : 0U;
}

auto
ContextState::navigation_pressed(UiNavigationCommand command) const noexcept -> bool
{
    const std::size_t index = command_index(command);
    return index < NAVIGATION_COMMANDS && navigation.presses[index] > navigation.repeats[index] && !has_any(navigation.consumed, command);
}

auto
ContextState::navigation_triggered(UiNavigationCommand command) const noexcept -> bool
{
    const std::size_t index = command_index(command);
    return index < NAVIGATION_COMMANDS && navigation.presses[index] > 0 && !has_any(navigation.consumed, command);
}

auto
ContextState::navigation_held(UiNavigationCommand command) const noexcept -> bool
{
    return has_any(navigation.down, command);
}

void
ContextState::consume_navigation(UiNavigationCommand commands) noexcept
{
    navigation.consumed = navigation.consumed | commands;
}

// ===================================================================================================== moves

void
ContextState::score_navigation(std::uint32_t id,
                               const WindowState& window,
                               const UiRectangle& rectangle) noexcept
{
    // Items of the focused window (and its child regions) the move can reach, other than the focused one.
    NavigationState& state = navigation;
    if (state.move == UiNavigationCommand::NONE || id == focus_id || state.focus_root != focused_window
        || has_any(window.flags, UiWindowFlag::NO_NAVIGATION_INPUTS))
    {
        return;
    }

    // Dear ImGui's scoring (NavScoreItem): the distance between the boxes (vertically between their middle 60%, so
    // that items touching vertically are still apart), horizontally scaled down when they are apart on both axes;
    // the quadrant of the candidate from the box distances, else from the centers. The nearest box in the move's
    // quadrant wins, then the nearest center, then the later item to the right or below.
    const UiRectangle& current = state.focus_rectangle;
    float box_x = interval_distance(rectangle.left, rectangle.right, current.left, current.right);
    const float box_y = interval_distance(between(rectangle.top, rectangle.bottom, 0.2F), between(rectangle.top, rectangle.bottom, 0.8F),
                                          between(current.top, current.bottom, 0.2F), between(current.top, current.bottom, 0.8F));
    if (box_y != 0 && box_x != 0)
    {
        box_x = box_x / 1000.0F + (box_x > 0 ? 1.0F : -1.0F);
    }
    const float box = std::abs(box_x) + std::abs(box_y);
    const float center_x = (rectangle.left + rectangle.right) - (current.left + current.right);
    const float center_y = (rectangle.top + rectangle.bottom) - (current.top + current.bottom);
    const float center = std::abs(center_x) + std::abs(center_y);
    const UiNavigationCommand direction = box_x != 0 || box_y != 0         ? quadrant(box_x, box_y)
                                          : center_x != 0 || center_y != 0 ? quadrant(center_x, center_y)
                                          : id < focus_id                  ? UiNavigationCommand::MOVE_LEFT
                                                                           : UiNavigationCommand::MOVE_RIGHT;
    if (direction != state.move)
    {
        return;
    }
    const bool vertical = state.move == UiNavigationCommand::MOVE_UP || state.move == UiNavigationCommand::MOVE_DOWN;
    const bool better = box < state.best_box || (box == state.best_box && center < state.best_center)
                        || (box == state.best_box && center == state.best_center && (vertical ? box_y : box_x) < 0);
    if (better)
    {
        state.best_id = id;
        state.best_window = static_cast<std::uint32_t>(&window - windows.data());
        state.best_rectangle = rectangle;
        state.best_box = box;
        state.best_center = center;
    }
}

void
ContextState::resolve_navigation() noexcept
{
    NavigationState& state = navigation;
    focus_arrived_id = 0;
    std::uint32_t shown_window = NO_WINDOW;
    UiRectangle shown{};
    const auto move_to = [&](std::uint32_t id,
                             std::uint32_t window,
                             const UiRectangle& rectangle,
                             bool arrived)
    {
        focus_id = id;
        focus_visible = true;
        focus_arrived_id = arrived ? id : 0;
        shown_window = window;
        shown = rectangle;
    };

    // set_keyboard_focus_here(), Tab and Shift+Tab (wrapping around; the item they reach knows it arrived: a text
    // field starts editing), or a direction (the best item in it; none in a popup: around to the first or last
    // item; no item focused yet: the first or last).
    const bool moving = state.move != UiNavigationCommand::NONE && !has_any(state.consumed, state.move);
    if (focus_here_result != 0)
    {
        focus_window(focus_here_window);
        move_to(focus_here_result, state.here_window, state.here_rectangle, true);
    }
    else if (focus_step > 0 && focus_first != 0)
    {
        const bool after = focus_seen_current && focus_after_current != 0;
        move_to(after ? focus_after_current : focus_first, after ? state.after_window : state.first_window,
                after ? state.after_rectangle : state.first_rectangle, true);
    }
    else if (focus_step < 0 && focus_last != 0)
    {
        const bool before = focus_seen_current && focus_before_current != 0;
        move_to(before ? focus_before_current : focus_last, before ? state.before_window : state.last_window,
                before ? state.before_rectangle : state.last_rectangle, true);
    }
    else if (moving && focus_first != 0)
    {
        const bool forward = state.move == UiNavigationCommand::MOVE_DOWN || state.move == UiNavigationCommand::MOVE_RIGHT;
        const std::size_t level = popup_level_of(focused_window);
        const bool wraps = level < popups.size() && !popups[level].modal;
        if (state.best_id != 0)
        {
            move_to(state.best_id, state.best_window, state.best_rectangle, false);
        }
        else if (state.focus_root != focused_window || !focus_seen_current || wraps)
        {
            move_to(forward ? focus_first : focus_last, forward ? state.first_window : state.last_window,
                    forward ? state.first_rectangle : state.last_rectangle, false);
        }
    }
    else if (focus_id != 0 && !focus_alive)
    {
        focus_id = 0;
        focus_visible = false;
    }
    if (default_focus_request != 0 && focus_id == 0)
    {
        focus_id = default_focus_request;
        focus_visible = true;
    }
    default_focus_request = 0;

    // MENU: into the menu bar of the focused window (or the main menu bar), its first item; again: back to the item
    // focused before.
    if (state.menu_bar_window != NO_WINDOW)
    {
        if (state.menu_bar_first != 0)
        {
            focus_window(state.menu_bar_window);
            focus_id = state.menu_bar_first;
            focus_visible = true;
        }
        else
        {
            state.menu_return_id = 0;
            state.menu_return_window = NO_WINDOW;
        }
        state.menu_bar_window = NO_WINDOW;
    }

    // NEXT_WINDOW and PREVIOUS_WINDOW: the next (or previous) window in creation order that shows and can be focused,
    // with the item it last focused.
    const bool cycle_next = navigation_triggered(UiNavigationCommand::NEXT_WINDOW);
    const bool cycle_previous = navigation_triggered(UiNavigationCommand::PREVIOUS_WINDOW);
    if (cycle_next != cycle_previous && !windows.empty())
    {
        const std::uint32_t main_menu_bar = find_window(hash(0, MAIN_MENU_BAR_WINDOW));
        const auto candidate = [&](std::uint32_t index)
        {
            const WindowState& window = windows[index];
            if (window.child || !window.active || window.hidden || window.popup || window.tooltip || window.dock_tree != DockNodeState::NONE
                || index == main_menu_bar || has_any(window.flags, UiWindowFlag::NO_NAVIGATION_FOCUS))
            {
                return false;
            }
            if (window.docked && window.dock_node < dock_nodes.size() && dock_nodes[window.dock_node].selected != window.id)
            {
                return false;
            }
            return true;
        };
        const auto count = static_cast<std::uint32_t>(windows.size());
        const std::uint32_t start = focused_window != NO_WINDOW ? focused_window : (cycle_next ? count - 1 : 0);
        for (std::uint32_t step = 1; step <= count; ++step)
        {
            const std::uint32_t index = cycle_next ? (start + step) % count : (start + count - step) % count;
            if (candidate(index))
            {
                if (index != focused_window)
                {
                    const std::uint32_t remembered = windows[index].navigation_last_id;
                    focus_window(index);
                    focus_id = remembered;
                    focus_visible = remembered != 0;
                }
                break;
            }
        }
    }

    // The scroll of a gamepad's stick: the focused item's window, else the focused window (the first that scrolls).
    if ((state.scroll.x != 0 || state.scroll.y != 0) && focused_window != NO_WINDOW)
    {
        std::uint32_t index = state.focus_root == focused_window && state.focus_window != NO_WINDOW ? state.focus_window : focused_window;
        while (index != NO_WINDOW)
        {
            WindowState& window = windows[index];
            if (window.scroll_maximum.x > 0 || window.scroll_maximum.y > 0 || window.parent == NO_WINDOW)
            {
                const float distance = description.input.navigation_scroll_speed * delta_time;
                window.scroll_target = {.x = std::clamp(window.scroll.x + state.scroll.x * distance, 0.0F, window.scroll_maximum.x),
                                        .y = std::clamp(window.scroll.y + state.scroll.y * distance, 0.0F, window.scroll_maximum.y)};
                break;
            }
            index = window.parent;
        }
    }

    // The item the keyboard moved to shows; the next moves start from it (else from where the focused item was this
    // frame; unknown: from nothing).
    if (shown_window != NO_WINDOW)
    {
        scroll_into_view(shown_window, shown);
        state.focus_rectangle = shown;
        state.focus_root = windows[shown_window].root;
        state.focus_window = shown_window;
    }
    else if (state.next_focus_id == focus_id && focus_id != 0)
    {
        state.focus_rectangle = state.next_focus_rectangle;
        state.focus_root = state.next_focus_root;
        state.focus_window = state.next_focus_window;
    }
    else
    {
        state.focus_root = NO_WINDOW;
        state.focus_window = NO_WINDOW;
    }
    state.next_focus_id = 0;
    state.next_focus_root = NO_WINDOW;
    state.next_focus_window = NO_WINDOW;
    state.first_window = NO_WINDOW;
    state.last_window = NO_WINDOW;
    state.before_window = NO_WINDOW;
    state.after_window = NO_WINDOW;
    state.here_window = NO_WINDOW;
    if (focused_window != NO_WINDOW && focus_id != 0)
    {
        windows[focused_window].navigation_last_id = focus_id;
    }
}

void
ContextState::scroll_into_view(std::uint32_t window,
                               const UiRectangle& rectangle) noexcept
{
    // Each window from the item's up to its root scrolls so that what is inside it shows (with the item spacing
    // around it); a parent shows the child region it holds.
    UiRectangle target = rectangle;
    std::uint32_t index = window;
    while (index != NO_WINDOW && index < windows.size())
    {
        WindowState& current = windows[index];
        const UiRectangle& inner = current.inner;
        const Vector2 margin = style.item_spacing;
        float scroll_x = current.scroll_target.x >= 0 ? current.scroll_target.x : current.scroll.x;
        float scroll_y = current.scroll_target.y >= 0 ? current.scroll_target.y : current.scroll.y;
        if (target.top < inner.top + margin.y)
        {
            scroll_y -= inner.top + margin.y - target.top;
        }
        else if (target.bottom > inner.bottom - margin.y)
        {
            scroll_y += std::min(target.bottom - (inner.bottom - margin.y), target.top - (inner.top + margin.y));
        }
        if (target.left < inner.left + margin.x)
        {
            scroll_x -= inner.left + margin.x - target.left;
        }
        else if (target.right > inner.right - margin.x)
        {
            scroll_x += std::min(target.right - (inner.right - margin.x), target.left - (inner.left + margin.x));
        }
        scroll_x = std::clamp(scroll_x, 0.0F, current.scroll_maximum.x);
        scroll_y = std::clamp(scroll_y, 0.0F, current.scroll_maximum.y);
        if (scroll_x != current.scroll.x || scroll_y != current.scroll.y)
        {
            current.scroll_target = {.x = scroll_x, .y = scroll_y};
        }
        if (!current.child)
        {
            break;
        }
        target = current.outer;
        index = current.parent;
    }
}

} // namespace gfx::ui
