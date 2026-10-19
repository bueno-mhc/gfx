/// @file shortcuts.cpp
/// @brief Keyboard shortcuts: key chords matched by the keyboard layout's symbols, and routed as Dear ImGui routes
///        them — each chord is given, next frame, to the call that scored best this frame (the window holding the
///        keyboard focus first, then its parents, then global routes), a text field being edited keeping the chords
///        it types and edits with. set_next_item_shortcut() activates an item.
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

/// The score of a route through the active item (ShortcutFlag::ROUTE_ACTIVE).
constexpr std::uint32_t ACTIVE_SCORE = 1;

/// The score of a route from the window holding the keyboard focus (others on its chain add their distance).
constexpr std::uint32_t FOCUSED_SCORE = 2;

/// The score of a global route.
constexpr std::uint32_t GLOBAL_SCORE = 1000;

/// The modifiers that tell chords apart: Shift, Control, Alt and Super, either side.
constexpr auto
modifier_groups(KeyModifier modifiers) noexcept -> std::uint32_t
{
    return (has_any(modifiers, KeyModifier::SHIFT) ? 1U : 0U) | (has_any(modifiers, KeyModifier::CONTROL) ? 2U : 0U)
           | (has_any(modifiers, KeyModifier::ALT) ? 4U : 0U) | (has_any(modifiers, KeyModifier::SUPER) ? 8U : 0U);
}

/// The key of @p chord in the route table (never 0 for a chord with a key).
constexpr auto
chord_key(KeyChord chord) noexcept -> std::uint64_t
{
    auto code = static_cast<std::uint64_t>(std::to_underlying(chord.key));
    code = code >= 'A' && code <= 'Z' ? code - 'A' + 'a' : code;
    return (code << 4) | modifier_groups(chord.modifiers);
}

/// Whether a text field being edited uses @p chord itself: typing (no modifier, or Shift), and Control (or Super)
/// with the keys it edits with.
constexpr auto
text_field_uses(KeyChord chord) noexcept -> bool
{
    const std::uint32_t groups = modifier_groups(chord.modifiers);
    if ((groups & ~1U) == 0)
    {
        return true;
    }
    if ((groups & ~(1U | 2U | 8U)) != 0 || (groups & (2U | 8U)) == 0)
    {
        return false;
    }
    const auto code = static_cast<std::uint32_t>(std::to_underlying(chord.key));
    const std::uint32_t letter = code >= 'A' && code <= 'Z' ? code - 'A' + 'a' : code;
    switch (letter)
    {
        case 'a':
        case 'c':
        case 'v':
        case 'x':
        case 'y':
        case 'z':
            return true;
        default:
            break;
    }
    switch (chord.key)
    {
        case KeyCode::LEFT:
        case KeyCode::RIGHT:
        case KeyCode::UP:
        case KeyCode::DOWN:
        case KeyCode::HOME:
        case KeyCode::END:
        case KeyCode::BACKSPACE:
        case KeyCode::DELETE_FORWARD:
        case KeyCode::INSERT:
        case KeyCode::RETURN:
            return true;
        default:
            return false;
    }
}

} // namespace

auto
ContextState::chord_pressed(KeyChord chord,
                            bool repeat) const noexcept -> bool
{
    // The modifiers held are the chord's exactly (either side of each).
    if (modifier_groups(modifiers) != modifier_groups(chord.modifiers))
    {
        return false;
    }

    // Characters by key code (letters in lower case), named keys by the physical key they name.
    auto code = static_cast<std::uint32_t>(std::to_underlying(chord.key));
    code = code >= 'A' && code <= 'Z' ? code - 'A' + 'a' : code;
    if (code == 0)
    {
        return false;
    }
    if (code < key_codes_pressed.size())
    {
        return repeat ? key_codes_pressed[code] > 0 : key_codes_pressed[code] > key_codes_repeated[code];
    }
    if ((code & KEY_CODE_NON_PRINTABLE_FLAG) != 0)
    {
        const std::uint32_t key = code & ~KEY_CODE_NON_PRINTABLE_FLAG;
        if (key < keys_pressed.size())
        {
            return repeat ? keys_pressed[key] > 0 : keys_pressed[key] > keys_repeated[key];
        }
    }
    return false;
}

auto
ContextState::route_shortcut(KeyChord chord,
                             ShortcutFlag flags,
                             std::uint32_t owner,
                             const WindowState* window,
                             bool owner_active) noexcept -> bool
{
    const bool repeat = has_any(flags, ShortcutFlag::REPEAT);
    if (chord.key == KeyCode::UNKNOWN)
    {
        return false;
    }

    // ROUTE_ALWAYS: no route, nothing taken from the others.
    if (has_any(flags, ShortcutFlag::ROUTE_ALWAYS))
    {
        return chord_pressed(chord, repeat);
    }

    // A text field being edited keeps the chords it types and edits with.
    if (text_edit.id != 0 && text_field_uses(chord) && !(has_any(flags, ShortcutFlag::ROUTE_ACTIVE) && owner == text_edit.id))
    {
        return false;
    }

    // The route's score: through the active item; from the window holding the keyboard focus or a window it is in
    // (nearer is better); global. Not routed: nothing.
    std::uint32_t score = std::numeric_limits<std::uint32_t>::max();
    if (has_any(flags, ShortcutFlag::ROUTE_ACTIVE))
    {
        score = owner_active ? ACTIVE_SCORE : score;
    }
    else if (has_any(flags, ShortcutFlag::ROUTE_GLOBAL))
    {
        score = GLOBAL_SCORE;
    }
    else if (window != nullptr && focused_window != NO_WINDOW)
    {
        // The chain from the window of the focused item (else the focused window) up to its root.
        std::uint32_t index = navigation.focus_root == focused_window && navigation.focus_window != NO_WINDOW ? navigation.focus_window : focused_window;
        const auto current = static_cast<std::uint32_t>(window - windows.data());
        for (std::uint32_t distance = 0; index != NO_WINDOW && index < windows.size(); ++distance)
        {
            if (index == current)
            {
                score = FOCUSED_SCORE + distance;
                break;
            }
            index = windows[index].child ? windows[index].parent : NO_WINDOW;
        }
    }
    if (score == std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }

    // Its route: the best score this frame wins it next frame (the first of equal scores).
    const std::uint64_t key = chord_key(chord);
    const std::size_t capacity = shortcut_routes.size();
    ShortcutRoute* route = nullptr;
    ShortcutRoute* free_slot = nullptr;
    for (std::size_t probe = 0; probe < capacity; ++probe)
    {
        ShortcutRoute& slot = shortcut_routes[(key * 0x9E3779B97F4A7C15ULL + probe) % capacity];
        if (slot.chord == key)
        {
            route = &slot;
            break;
        }
        if (slot.chord == 0)
        {
            free_slot = free_slot == nullptr ? &slot : free_slot;
            break;
        }
        if (slot.frame + 1 < frame && free_slot == nullptr)
        {
            // Not routed for two frames: reusable (its winner has expired).
            free_slot = &slot;
        }
    }
    if (route == nullptr)
    {
        if (free_slot == nullptr)
        {
            report(UiFrameProblem::CAPACITY_EXCEEDED, "shortcut", "more key chords than UiContextLimits::shortcut_routes: this one is not routed");
            return false;
        }
        route = free_slot;
        *route = ShortcutRoute{.chord = key};
    }
    if (route->frame != frame)
    {
        route->frame = frame;
        route->owner = owner;
        route->score = score;
    }
    else if (score < route->score)
    {
        route->owner = owner;
        route->score = score;
    }

    // It fires when it was pressed and this owner won last frame.
    return route->has_winner && route->winner == owner && chord_pressed(chord, repeat);
}

void
ContextState::update_shortcut_routes() noexcept
{
    for (ShortcutRoute& route : shortcut_routes)
    {
        if (route.chord == 0)
        {
            continue;
        }
        route.has_winner = route.frame == frame;
        route.winner = route.owner;
    }
}

auto
ContextState::item_activated(std::uint32_t id) const noexcept -> bool
{
    return id != 0 && (shortcut_activated_id == id || (accessibility.last_item == id && item_request(AccessibilityAction::CLICK) != nullptr));
}

// ===================================================================================================== UiFrame

auto
UiFrame::shortcut(KeyChord chord,
                  ShortcutFlag flags) noexcept -> bool
{
    ContextState& state = *m_state;
    if (!state.frame_active)
    {
        state.report(UiFrameProblem::FRAME_NOT_ACTIVE, "shortcut", "call it between UiContext::begin_frame() and end_frame()");
        return false;
    }

    // Owned by the active item (ROUTE_ACTIVE), else by the current window (0: outside windows).
    const WindowState* window = state.current_window_quiet();
    const bool active = state.last_item.id != 0 && state.active_id == state.last_item.id;
    const std::uint32_t owner = has_any(flags, ShortcutFlag::ROUTE_ACTIVE) ? state.last_item.id : window != nullptr ? window->id : 0;
    return state.route_shortcut(chord, flags, owner, window, active);
}

void
UiFrame::set_next_item_shortcut(KeyChord chord,
                                ShortcutFlag flags) noexcept
{
    ContextState& state = *m_state;
    state.next_item.has_shortcut = true;
    state.next_item.shortcut = chord;
    state.next_item.shortcut_flags = flags;
}

} // namespace gfx::ui
