/// @file context.cpp
/// @brief UiContext: the state's containers and helpers, fonts and themes, begin_frame() (input), end_frame()
///        (unclosed stacks, keyboard focus, cursor, publication, report) and the draw data leases.
module gfx.ui.context;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.errors;
import gfx.ui.handles;
import gfx.ui.draw;
import gfx.ui.style;
import gfx.ui.text;
import gfx.ui.input;
import :state;

namespace gfx::ui
{

namespace
{

/// The smallest power of two holding @p entries at half load.
auto
table_capacity(std::size_t entries) -> std::size_t
{
    return std::bit_ceil(std::max<std::size_t>(entries * 2, 16));
}

/// Slot of @p key in a table of @p capacity (a power of two): Fibonacci hashing.
auto
slot_of(std::uint32_t key,
        std::size_t capacity) noexcept -> std::size_t
{
    return static_cast<std::size_t>((std::uint64_t{key} * 0x9E3779B97F4A7C15ULL) >> 32U) & (capacity - 1);
}

/// Index of mouse button @p button in ContextState::mouse.
constexpr auto
button_index(MouseButton button) noexcept -> std::size_t
{
    return std::to_underlying(button);
}

/// The mouse button bit of @p button.
constexpr auto
button_bit(MouseButton button) noexcept -> MouseButtonMask
{
    return static_cast<MouseButtonMask>(1U << std::to_underlying(button));
}

/// Distance between @p first and @p second.
auto
distance(Vector2 first,
         Vector2 second) noexcept -> float
{
    return std::hypot(first.x - second.x, first.y - second.y);
}

/// Whether @p rectangle contains @p point (left and top edges included).
constexpr auto
contains(const UiRectangle& rectangle,
         Vector2 point) noexcept -> bool
{
    return point.x >= rectangle.left && point.x < rectangle.right && point.y >= rectangle.top && point.y < rectangle.bottom;
}

/// Throws the InvalidUsageError of a context operation.
[[noreturn]] void
throw_context_error(std::string_view operation,
                    std::string problem,
                    std::string suggestion)
{
    throw InvalidUsageError({.backend = UiSubsystem::WIDGETS, .operation = std::string{operation}, .problem = std::move(problem),
                             .suggestion = std::move(suggestion)});
}

} // namespace

// ===================================================================================================== containers

void
IdentifierMap::reserve(std::size_t entries)
{
    // Rehash into a table that holds the entries at half load.
    const std::size_t capacity = table_capacity(entries);
    if (capacity <= m_keys.size())
    {
        return;
    }
    std::vector<std::uint32_t> keys(capacity, 0);
    std::vector<std::uint32_t> values(capacity, 0);
    for (std::size_t slot = 0; slot < m_keys.size(); ++slot)
    {
        if (m_keys[slot] == 0)
        {
            continue;
        }
        std::size_t target = slot_of(m_keys[slot], capacity);
        while (keys[target] != 0)
        {
            target = (target + 1) & (capacity - 1);
        }
        keys[target] = m_keys[slot];
        values[target] = m_values[slot];
    }
    m_keys = std::move(keys);
    m_values = std::move(values);
}

void
IdentifierMap::clear() noexcept
{
    std::ranges::fill(m_keys, 0U);
    m_count = 0;
}

auto
IdentifierMap::find(std::uint32_t key) const noexcept -> std::optional<std::uint32_t>
{
    if (m_keys.empty() || key == 0)
    {
        return std::nullopt;
    }
    for (std::size_t slot = slot_of(key, m_keys.size());; slot = (slot + 1) & (m_keys.size() - 1))
    {
        if (m_keys[slot] == key)
        {
            return m_values[slot];
        }
        if (m_keys[slot] == 0)
        {
            return std::nullopt;
        }
    }
}

auto
IdentifierMap::insert(std::uint32_t key,
                      std::uint32_t value) -> bool
{
    // Grow past half load (the only allocation), then probe for the key or a free slot.
    bool grew = false;
    if ((m_count + 1) * 2 > m_keys.size())
    {
        reserve(m_count + 1);
        grew = true;
    }
    for (std::size_t slot = slot_of(key, m_keys.size());; slot = (slot + 1) & (m_keys.size() - 1))
    {
        if (m_keys[slot] == key || m_keys[slot] == 0)
        {
            m_count += m_keys[slot] == 0 ? 1U : 0U;
            m_keys[slot] = key;
            m_values[slot] = value;
            return grew;
        }
    }
}

void
IdentifierSet::reserve(std::size_t entries)
{
    const std::size_t capacity = table_capacity(entries);
    m_keys.assign(capacity, 0);
    m_stamps.assign(capacity, 0);
    m_count = 0;
}

void
IdentifierSet::clear() noexcept
{
    // A new generation: every stamp is stale.
    ++m_generation;
    m_count = 0;
}

auto
IdentifierSet::insert(std::uint32_t key) noexcept -> std::optional<bool>
{
    if (m_keys.empty() || (m_count + 1) * 2 > m_keys.size())
    {
        return std::nullopt;
    }
    for (std::size_t slot = slot_of(key, m_keys.size());; slot = (slot + 1) & (m_keys.size() - 1))
    {
        if (m_stamps[slot] != m_generation)
        {
            m_stamps[slot] = m_generation;
            m_keys[slot] = key;
            ++m_count;
            return true;
        }
        if (m_keys[slot] == key)
        {
            return false;
        }
    }
}

// ===================================================================================================== state

ContextState::ContextState(const UiContextDescription& context_description)
    : description{context_description}
    , services{context_description.services != nullptr ? context_description.services : &null_services}
    , text{fonts, context_description.text}
    , theme{context_description.theme}
    , arena_buffer(std::max<std::size_t>(context_description.limits.frame_arena_bytes, 1))
    , arena{arena_buffer.data(), arena_buffer.size(), &arena_upstream}
    , frame_object{*this}
{
    // Every capacity a frame needs, reserved now: frames allocate only when a limit is exceeded.
    const UiContextLimits& limits = description.limits;
    windows.reserve(limits.windows);
    window_lookup.reserve(limits.windows);
    display_order.reserve(limits.windows);
    children_last_frame.reserve(limits.windows);
    children_this_frame.reserve(limits.windows);
    window_stack.reserve(limits.stack_depth);
    id_stack.reserve(limits.stack_depth);
    color_stack.reserve(limits.stack_depth);
    variable_stack.reserve(limits.stack_depth);
    font_stack.reserve(limits.stack_depth);
    item_width_stack.reserve(limits.stack_depth);
    text_wrap_stack.reserve(limits.stack_depth);
    group_stack.reserve(limits.stack_depth);
    disabled_stack.reserve(limits.stack_depth);
    frame_ids.reserve(limits.items_per_frame);
    typed_text.reserve(256);
    composition.reserve(64);
    popups.reserve(limits.stack_depth);
    popups_closed_by_click.reserve(limits.stack_depth);
    list_box_labels.reserve(limits.stack_depth);
    text_edit.initial.reserve(256);
    text_edit.ended_text.reserve(256);
    text_edit.display.reserve(256);
    text_edit.filtered.reserve(256);
    open_states.reserve(limits.open_states);
    tab_bars.reserve(limits.tab_bars);
    tab_bar_lookup.reserve(limits.tab_bars);
    tab_bar_stack.reserve(limits.stack_depth);
    tables.reserve(limits.tables);
    table_lookup.reserve(limits.tables);
    table_stack.reserve(limits.stack_depth);
    dock_nodes.reserve(limits.dock_nodes);
    free_dock_nodes.reserve(limits.dock_nodes);
    dock_space_roots.reserve(16);
    dock_order.reserve(limits.windows);
    publish_order.reserve(std::size_t{limits.windows} * 2);
    dock_placed.reserve(limits.windows);
    dock_scratch.reserve(limits.windows);
    tree_stack.reserve(limits.stack_depth);
    drag_drop.type.reserve(MAXIMUM_PAYLOAD_TYPE_SIZE);
    drag_drop.data.reserve(limits.drag_drop_payload_bytes);
    for (DrawDataSlot& slot : slots)
    {
        slot.lists.reserve(std::size_t{limits.windows} + 2);
        slot.pointers.reserve(std::size_t{limits.windows} + 2);
    }
    style = theme.style;
    colors = theme.colors;
}

void
ContextState::report(UiFrameProblem problem,
                     std::string_view operation,
                     std::string_view detail,
                     std::string_view name,
                     std::uint32_t identifier) noexcept
{
    // The problem always counts; the issue only while the report has room.
    problems = problems | problem;
    if (issue_count == issues.size())
    {
        ++dropped_issues;
        problems = problems | UiFrameProblem::ISSUES_DROPPED;
        return;
    }
    UiFrameIssue& issue = issues[issue_count++];
    issue = UiFrameIssue{.problem = problem, .operation = operation, .detail = detail, .identifier = identifier};
    const std::size_t size = std::min(name.size(), issue.name_storage.size());
    std::copy_n(name.data(), size, issue.name_storage.data());
    issue.name_size = static_cast<std::uint8_t>(size);
}

auto
ContextState::current_window(std::string_view operation) noexcept -> WindowState*
{
    if (!frame_active)
    {
        report(UiFrameProblem::FRAME_NOT_ACTIVE, operation, "call it between UiContext::begin_frame() and end_frame()");
        return nullptr;
    }
    if (window_stack.empty())
    {
        report(UiFrameProblem::WIDGET_OUTSIDE_WINDOW, operation, "submit widgets between begin_window() and end_window()");
        return nullptr;
    }
    return &windows[window_stack.back()];
}

auto
ContextState::current_window_quiet() const noexcept -> const WindowState*
{
    return frame_active && !window_stack.empty() ? &windows[window_stack.back()] : nullptr;
}

auto
ContextState::hash(std::uint32_t seed,
                   std::string_view label) noexcept -> std::uint32_t
{
    // "###" restarts the identifier: only it and what follows are hashed.
    if (const std::size_t restart = label.find("###"); restart != std::string_view::npos)
    {
        label = label.substr(restart);
    }

    // FNV-1a over the seed's bytes (little endian), then the label's (the 0 → 1 rule of name_hash()).
    std::uint32_t value = NAME_HASH_OFFSET_BASIS;
    for (std::uint32_t byte = 0; byte < 4; ++byte)
    {
        value ^= (seed >> (8U * byte)) & 0xFFU;
        value *= NAME_HASH_PRIME;
    }
    for (const char character : label)
    {
        value ^= static_cast<std::uint8_t>(character);
        value *= NAME_HASH_PRIME;
    }
    return value == 0 ? 1U : value;
}

auto
ContextState::display_text(std::string_view label) noexcept -> std::string_view
{
    const std::size_t hidden = label.find("##");
    return hidden == std::string_view::npos ? label : label.substr(0, hidden);
}

auto
ContextState::widget_id(std::string_view label) const noexcept -> std::uint32_t
{
    return hash(id_seed(), label);
}

auto
ContextState::id_seed() const noexcept -> std::uint32_t
{
    return id_stack.empty() ? 0U : id_stack.back();
}

auto
ContextState::find_window(std::uint32_t id) const noexcept -> std::uint32_t
{
    return window_lookup.find(id).value_or(NO_WINDOW);
}

auto
ContextState::create_window(std::uint32_t id,
                            std::string_view name,
                            std::uint32_t parent) -> std::uint32_t
{
    // Beyond the reserved capacity, the window table grows: an allocation, reported.
    if (windows.size() == windows.capacity())
    {
        ++allocations;
        report(UiFrameProblem::CAPACITY_EXCEEDED, "begin_window", "more windows than UiContextLimits::windows", name, id);
    }
    const auto index = static_cast<std::uint32_t>(windows.size());
    WindowState& window = windows.emplace_back();
    window.id = id;
    window.name.assign(display_text(name));
    window.parent = parent;
    window.root = parent == NO_WINDOW ? index : windows[parent].root;
    window.child = parent != NO_WINDOW;
    if (window_lookup.insert(id, index))
    {
        ++allocations;
    }

    // Root windows cascade from the top-left corner and start in front.
    if (!window.child)
    {
        const float offset = 20.0F * static_cast<float>(windows_created % 10);
        window.position = {.x = 60.0F + offset, .y = 60.0F + offset};
        ++windows_created;
        display_order.push_back(index);
    }
    return index;
}

void
ContextState::focus_window(std::uint32_t index) noexcept
{
    // The keyboard focus leaves items of other windows.
    if (index != NO_WINDOW && focused_window != index)
    {
        focus_id = 0;
        focus_visible = false;
    }
    focused_window = index;
    if (index == NO_WINDOW)
    {
        return;
    }

    // In front of the others: last of the display order (a docked window: its tree's host, which it follows).
    if (windows[index].dock_node != DockNodeState::NONE)
    {
        const std::uint32_t host = dock_host_of(index);
        index = host != NO_WINDOW ? host : index;
    }
    if (has_any(windows[index].flags, UiWindowFlag::NO_BRING_TO_FRONT_ON_FOCUS))
    {
        return;
    }
    const auto found = std::ranges::find(display_order, index);
    if (found != display_order.end())
    {
        std::rotate(found, found + 1, display_order.end());
    }
}

auto
ContextState::list_of(const WindowState& window) noexcept -> DrawList&
{
    return windows[window.root].draw_list;
}

auto
ContextState::content_right(const WindowState& window) const noexcept -> float
{
    if (window.table < tables.size())
    {
        // A table's cell: its column's content.
        const TableState& table = tables[window.table];
        return table.columns[static_cast<std::size_t>(table.column)].content_right;
    }
    if (window.columns.active)
    {
        const ColumnsState& columns = window.columns;
        return columns.start_x + columns.width * static_cast<float>(columns.current + 1) / static_cast<float>(columns.count) - style.item_spacing.x;
    }
    return window.inner.right - window.padding.x - window.scroll.x;
}

auto
ContextState::content_bottom(const WindowState& window) const noexcept -> float
{
    return window.inner.bottom - window.padding.y - window.scroll.y;
}

auto
ContextState::take_item_width(const WindowState& window) noexcept -> float
{
    // The next item's width, else the stack's, else the window's default; negative: what is left minus it.
    float width = window.item_width;
    if (next_item_has_width)
    {
        width = next_item_width;
        next_item_has_width = false;
    }
    else if (!item_width_stack.empty())
    {
        width = item_width_stack.back();
    }
    if (width == 0)
    {
        width = window.item_width;
    }
    else if (width < 0)
    {
        width = std::max(1.0F, content_right(window) - window.cursor.x + width);
    }
    return std::max(1.0F, width);
}

auto
ContextState::window_hovered(const WindowState& window,
                             HoveredFlag flags) const noexcept -> bool
{
    if (hovered_window == NO_WINDOW)
    {
        return false;
    }
    if (has_any(flags, HoveredFlag::ANY_WINDOW))
    {
        return true;
    }
    const auto index = static_cast<std::uint32_t>(&window - windows.data());
    const std::uint32_t reference = has_any(flags, HoveredFlag::ROOT_WINDOW) ? window.root : index;
    if (hovered_window == reference)
    {
        return true;
    }
    if (!has_any(flags, HoveredFlag::CHILD_WINDOWS))
    {
        return false;
    }

    // A descendant of the reference window.
    for (std::uint32_t current = windows[hovered_window].parent; current != NO_WINDOW; current = windows[current].parent)
    {
        if (current == reference)
        {
            return true;
        }
    }
    return false;
}

auto
ContextState::metrics_of(FontHandle font) noexcept -> FontMetrics
{
    // A laid-out empty text has one line: the font's line height.
    const FontDescription* font_description = font.is_valid() ? fonts.font(font) : nullptr;
    if (font_description == nullptr)
    {
        return fallback_font;
    }
    const TextLayout line = text.layout("", font);
    return {.font = font, .size = font_description->size, .line_height = line.height > 0 ? line.height : font_description->size * 1.2F};
}

auto
ContextState::current_font() const noexcept -> const FontMetrics&
{
    return font_stack.empty() ? default_font_metrics : font_stack.back();
}

auto
ContextState::layout_text(std::string_view value,
                          float wrap_width,
                          std::string_view operation) noexcept -> TextLayout
{
    const FontMetrics& font = current_font();
    if (!font.font.is_valid())
    {
        if (!no_font_reported)
        {
            report(UiFrameProblem::NO_FONT, operation, "load a font with UiContext::load_font()");
            no_font_reported = true;
        }
        return {};
    }
    const TextLayout layout = text.layout(value, font.font, {.wrap_width = wrap_width > 0 ? wrap_width : 0.0F});
    if (layout.problems != TextLayoutProblem::NONE)
    {
        report(UiFrameProblem::TEXT_LAYOUT, operation,
               has_any(layout.problems, TextLayoutProblem::MISSING_GLYPHS) ? "characters the font's atlases lack were drawn as .notdef"
               : has_any(layout.problems, TextLayoutProblem::INVALID_UTF8) ? "bytes that are not UTF-8 were drawn as U+FFFD"
               : has_any(layout.problems, TextLayoutProblem::CACHE_FULL)   ? "the text layout cache is full (TextLayoutEngineDescription::cache_capacity)"
                                                                            : "the text could not be laid out as asked",
               value);
    }
    return layout;
}

void
ContextState::draw_text(DrawList& list,
                        const TextLayout& layout,
                        Vector2 position,
                        Color color) noexcept
{
    if (layout.glyphs.empty() || color.alpha <= 0)
    {
        return;
    }
    try
    {
        list.add_text(fonts, layout, position, {.color = color});
    }
    catch (...)
    {
        report(UiFrameProblem::CAPACITY_EXCEEDED, "draw_text", "the draw list could not grow");
    }
}

auto
ContextState::color(StyleColor element) const noexcept -> Color
{
    return faded(colors[std::to_underlying(element)]);
}

auto
ContextState::faded(Color value) const noexcept -> Color
{
    return value.with_alpha(value.alpha * style.alpha * (disabled ? style.disabled_alpha : 1.0F));
}

void
ContextState::draw_frame(DrawList& list,
                         const UiRectangle& rectangle,
                         Color fill,
                         float rounding) noexcept
{
    list.add_rectangle_filled(rectangle, fill, rounding);
    if (style.frame_border_size > 0)
    {
        list.add_rectangle(rectangle, color(StyleColor::BORDER), style.frame_border_size, rounding);
    }
}

void
ContextState::draw_focus(DrawList& list,
                         const UiRectangle& rectangle,
                         std::uint32_t id,
                         float rounding) noexcept
{
    // Around the item, outside it, when the keyboard put the focus there.
    if (id == 0 || id != focus_id || !focus_visible)
    {
        return;
    }
    constexpr float MARGIN = 2.0F;
    const UiRectangle around{.left = rectangle.left - MARGIN, .top = rectangle.top - MARGIN, .right = rectangle.right + MARGIN, .bottom = rectangle.bottom + MARGIN};
    list.add_rectangle(around, color(StyleColor::NAVIGATION_HIGHLIGHT), 2.0F, rounding + MARGIN);
}

void
ContextState::draw_arrow(DrawList& list,
                         Vector2 position,
                         float size,
                         ArrowDirection direction,
                         Color color) noexcept
{
    // A triangle in the square of side size at position (Dear ImGui's proportions).
    const Vector2 center{.x = position.x + size * 0.5F, .y = position.y + size * 0.5F};
    const float radius = size * 0.40F;
    std::array<Vector2, 3> points{};
    const auto at = [&center](float x, float y) { return Vector2{.x = center.x + x, .y = center.y + y}; };
    switch (direction)
    {
        case ArrowDirection::RIGHT:
            points = {at(0.750F * radius, 0), at(-0.750F * radius, 0.866F * radius), at(-0.750F * radius, -0.866F * radius)};
            break;
        case ArrowDirection::LEFT:
            points = {at(-0.750F * radius, 0), at(0.750F * radius, -0.866F * radius), at(0.750F * radius, 0.866F * radius)};
            break;
        case ArrowDirection::DOWN:
            points = {at(0, 0.750F * radius), at(0.866F * radius, -0.750F * radius), at(-0.866F * radius, -0.750F * radius)};
            break;
        case ArrowDirection::UP:
            points = {at(0, -0.750F * radius), at(-0.866F * radius, 0.750F * radius), at(0.866F * radius, 0.750F * radius)};
            break;
    }
    list.add_convex_polygon_filled(points, color);
}

// ===================================================================================================== items

void
ContextState::item_size(WindowState& window,
                        Vector2 size,
                        float text_offset) noexcept
{
    if (window.skip_items)
    {
        return;
    }

    // The line grows to the item; the cursor goes to the start of the next line.
    const float line_height = std::max(window.line_height, size.y);
    window.cursor_previous_line = {.x = window.cursor.x + size.x, .y = window.cursor.y};
    window.cursor_maximum.x = std::max(window.cursor_maximum.x, window.cursor.x + size.x);
    window.cursor_maximum.y = std::max(window.cursor_maximum.y, window.cursor.y + line_height);
    window.cursor.x = window.cursor_start.x + window.indent;
    window.cursor.y += line_height + style.item_spacing.y;
    window.previous_line_height = line_height;
    window.previous_line_text_offset = std::max(window.line_text_offset, text_offset);
    window.line_height = 0;
    window.line_text_offset = 0;
    ++items;
}

auto
ContextState::item_add(WindowState& window,
                       const UiRectangle& rectangle,
                       std::uint32_t id,
                       bool focusable,
                       std::string_view operation,
                       std::string_view label) noexcept -> bool
{
    // The last item: what the item queries read.
    const UiRectangle visible = rectangle.intersection(window.clip);
    last_item = LastItem{.id = id,
                         .rectangle = rectangle,
                         .window = static_cast<std::uint32_t>(&window - windows.data()),
                         .visible = !visible.empty() && !window.hidden,
                         .disabled = disabled,
                         .clip = window.clip};
    if (id == 0)
    {
        return last_item.visible;
    }

    // Alive this frame: the active and focused items stay so.
    active_id_alive = active_id_alive || id == active_id;
    focus_alive = focus_alive || id == focus_id;

    // Two interactive items with one identifier in a frame are a conflict.
    if (!frame_ids_full)
    {
        const std::optional<bool> inserted = frame_ids.insert(id);
        if (!inserted.has_value())
        {
            frame_ids_full = true;
            report(UiFrameProblem::CAPACITY_EXCEEDED, operation, "more interactive items than UiContextLimits::items_per_frame: conflicts are not checked",
                   label, id);
        }
        else if (!*inserted)
        {
            report(UiFrameProblem::IDENTIFIER_CONFLICT, operation, "two items have this identifier: use push_id() or a \"##\" suffix", label, id);
        }
    }

    // set_keyboard_focus_here(): this item, in any window.
    if (focusable && !disabled && focus_here_requested)
    {
        focus_here_result = id;
        focus_here_window = window.root;
        focus_here_requested = false;
    }

    // The keyboard focus order of the focused window: first, last, around the focused item.
    if (focusable && !disabled && focused_window != NO_WINDOW && window.root == focused_window)
    {
        focus_first = focus_first == 0 ? id : focus_first;
        if (focus_seen_current && focus_after_current == 0)
        {
            focus_after_current = id;
        }
        if (id == focus_id)
        {
            focus_seen_current = true;
        }
        else if (!focus_seen_current)
        {
            focus_before_current = id;
        }
        focus_last = id;
    }
    return last_item.visible;
}

auto
ContextState::item_hoverable(const WindowState& window,
                             const UiRectangle& rectangle,
                             std::uint32_t id) noexcept -> bool
{
    // The mouse, over the item, in its window (the frontmost under the mouse), in its clip, and free.
    if (!mouse_present || disabled || window.hidden || moving_window != NO_WINDOW)
    {
        return false;
    }
    if (hovered_window != static_cast<std::uint32_t>(&window - windows.data()))
    {
        return false;
    }
    if (active_id != 0 && active_id != id && (active_id != text_edit.id || text_edit.mouse_selecting))
    {
        // Another item holds the mouse (a text field being edited does not, unless it is selecting).
        return false;
    }
    return contains(rectangle, mouse_position) && contains(window.clip, mouse_position);
}

void
ContextState::set_active(std::uint32_t id,
                         const WindowState& window) noexcept
{
    active_id = id;
    active_window = static_cast<std::uint32_t>(&window - windows.data());
    active_id_alive = true;
    active_id_edited = false;
    active_by_keyboard = false;
}

void
ContextState::take_focus(std::uint32_t id) noexcept
{
    focus_id = id;
    focus_visible = false;
    focus_alive = true;
}

auto
ContextState::keyboard_focused(const WindowState& window,
                               std::uint32_t id) const noexcept -> bool
{
    return focus_id == id && focus_visible && !disabled && focused_window != NO_WINDOW && window.root == focused_window;
}

auto
ContextState::presses(Key key) const noexcept -> std::uint32_t
{
    return keys_pressed[std::to_underlying(key)];
}

auto
ContextState::remembered_open(std::uint32_t id) const noexcept -> std::optional<bool>
{
    const std::optional<std::uint32_t> value = open_states.find(id);
    return value.has_value() ? std::optional<bool>{*value != 0} : std::nullopt;
}

void
ContextState::remember_open(std::uint32_t id,
                            bool open) noexcept
{
    try
    {
        if (open_states.insert(id, open ? 1U : 0U))
        {
            ++allocations;
            report(UiFrameProblem::CAPACITY_EXCEEDED, "tree_node", "more open states than UiContextLimits::open_states: the table grew");
        }
    }
    catch (...)
    {
        report(UiFrameProblem::CAPACITY_EXCEEDED, "tree_node", "the open states could not grow");
    }
}

void
ContextState::mark_edited(std::uint32_t id) noexcept
{
    last_item.edited = true;
    active_id_edited = active_id_edited || active_id == id;
    deactivated_after_edit = deactivated_after_edit || deactivated_id == id;
}

void
ContextState::clear_active() noexcept
{
    if (active_id != 0)
    {
        deactivated_id = active_id;
        deactivated_after_edit = active_id_edited;
    }
    active_id = 0;
    active_window = NO_WINDOW;
}

auto
ContextState::button_behavior(WindowState& window,
                              const UiRectangle& rectangle,
                              std::uint32_t id,
                              ButtonFlag flags) noexcept -> ButtonState
{
    ButtonState state{};

    // Hovered: the mouse may use the item.
    if (item_hoverable(window, rectangle, id))
    {
        hovered_id = id;
        state.hovered = true;
        last_item.hovered = true;
    }

    // Pressed on a button the item reacts to: it becomes active (and takes the focus, without the highlight).
    const ButtonFlag mouse_buttons = flags & (ButtonFlag::MOUSE_BUTTON_LEFT | ButtonFlag::MOUSE_BUTTON_RIGHT | ButtonFlag::MOUSE_BUTTON_MIDDLE);
    const auto reacts_to = [mouse_buttons](MouseButton button)
    {
        switch (button)
        {
            case MouseButton::LEFT:   return mouse_buttons == ButtonFlag::NONE || has_any(mouse_buttons, ButtonFlag::MOUSE_BUTTON_LEFT);
            case MouseButton::RIGHT:  return has_any(mouse_buttons, ButtonFlag::MOUSE_BUTTON_RIGHT);
            case MouseButton::MIDDLE: return has_any(mouse_buttons, ButtonFlag::MOUSE_BUTTON_MIDDLE);
            default:                  return false;
        }
    };
    if (state.hovered && active_id != id)
    {
        for (const MouseButton button : {MouseButton::LEFT, MouseButton::RIGHT, MouseButton::MIDDLE})
        {
            if (reacts_to(button) && mouse[button_index(button)].clicked)
            {
                set_active(id, window);
                active_button = button;
                take_focus(id);
                state.pressed = has_any(flags, ButtonFlag::PRESS_ON_CLICK);
                break;
            }
        }
    }

    // Held while the button is down; pressed when it is released over the item (or on repeats).
    if (active_id == id && !active_by_keyboard)
    {
        active_id_alive = true;
        const MouseButtonState& button = mouse[button_index(active_button)];
        if (button.down)
        {
            state.held = true;
            if (has_any(flags, ButtonFlag::REPEAT) && state.hovered && button.previous_held_seconds >= 0)
            {
                // A repeat tick: delay, then one per interval.
                const double delay = description.input.repeat_delay_seconds;
                const double interval = std::max(description.input.repeat_interval_seconds, 1.0e-3);
                const auto ticks = [delay, interval](double held) { return held < delay ? -1.0 : std::floor((held - delay) / interval); };
                state.pressed = state.pressed || ticks(button.held_seconds) > ticks(button.previous_held_seconds);
            }
        }
        else
        {
            // Not pressed after it was dragged as a drag and drop source.
            const bool over = contains(rectangle, mouse_position) && mouse_present;
            const bool dragged = drag_drop.active && drag_drop.source_id == id;
            state.pressed = state.pressed || (!has_any(flags, ButtonFlag::PRESS_ON_CLICK) && over && !dragged);
            clear_active();
        }
    }

    // The keyboard: Space or Return activates the item the keyboard focused.
    if (focus_id == id && focus_visible && !disabled && focused_window != NO_WINDOW && window.root == focused_window)
    {
        const auto pressed = [this](Key key) { return keys_pressed[std::to_underlying(key)] > keys_repeated[std::to_underlying(key)]; };
        const auto held = [this](Key key) { return keys_down[std::to_underlying(key)]; };
        if (pressed(Key::SPACE) || pressed(Key::RETURN) || pressed(Key::KEYPAD_ENTER))
        {
            state.pressed = true;
        }
        state.held = state.held || held(Key::SPACE) || held(Key::RETURN);
    }
    return state;
}

// ===================================================================================================== UiContext

UiContext::UiContext(const UiContextDescription& description)
    : m_state{std::make_unique<ContextState>(description)}
{
}

UiContext::~UiContext() = default;

auto
UiContext::fonts() noexcept -> FontLibrary&
{
    return m_state->fonts;
}

auto
UiContext::fonts() const noexcept -> const FontLibrary&
{
    return m_state->fonts;
}

auto
UiContext::load_font(std::shared_ptr<const FontAtlas> atlas,
                     float size) -> FontHandle
{
    if (atlas == nullptr)
    {
        throw_context_error("UiContext::load_font", "the atlas is null", "load it with FontAtlas::load() or FontAtlas::load_file()");
    }
    if (!(size > 0))
    {
        throw_context_error("UiContext::load_font", std::format("the size {} is not positive", size), "give the size in UI units per em, e.g. 16");
    }
    ContextState& state = *m_state;
    const FontAtlasHandle atlas_handle = state.fonts.add_atlas(std::move(atlas));
    const FontHandle font = state.fonts.add_font({.atlas = atlas_handle, .size = size});
    if (!state.default_font.is_valid())
    {
        state.default_font = font;
    }
    return font;
}

void
UiContext::set_default_font(FontHandle font)
{
    if (m_state->fonts.font(font) == nullptr)
    {
        throw_context_error("UiContext::set_default_font", std::format("{} names no font of the context's library", font),
                            "use a font returned by load_font() or fonts().add_font()");
    }
    m_state->default_font = font;
}

auto
UiContext::default_font() const noexcept -> FontHandle
{
    return m_state->default_font;
}

void
UiContext::set_theme(const Theme& theme) noexcept
{
    m_state->theme = theme;
}

auto
UiContext::theme() const noexcept -> const Theme&
{
    return m_state->theme;
}

auto
UiContext::begin_frame(const UiFrameInput& input) noexcept -> UiFrame&
{
    ContextState& state = *m_state;

    // A frame still open is ended first.
    if (state.frame_active)
    {
        state.report(UiFrameProblem::UNBALANCED_STACK, "begin_frame", "the previous frame was not ended: end_frame() closed it");
        static_cast<void>(end_frame());
    }

    // Time, display and the frame's scratch memory.
    ++state.frame;
    state.time = input.time;
    state.delta_time = state.previous_time < 0 ? 1.0F / 60.0F : static_cast<float>(std::max(input.time - state.previous_time, 0.0));
    state.previous_time = input.time;
    state.display_size = input.display_size;
    state.pixels_per_unit = input.pixels_per_unit > 0 ? input.pixels_per_unit : 1.0F;
    state.text.new_frame();
    state.arena.release();
    state.arena_upstream.allocations = 0;
    state.arena_used = 0;
    state.frame_ids.clear();
    state.frame_ids_full = false;
    state.items = 0;
    state.no_font_reported = false;

    // Keys: presses, repeats and releases of the frame; a window without focus holds nothing.
    state.keys_pressed.fill(0);
    state.key_codes_pressed.fill(0);
    state.keys_repeated.fill(0);
    state.keys_released.fill(false);
    if (!input.focused)
    {
        for (std::size_t key = 0; key < state.keys_down.size(); ++key)
        {
            state.keys_released[key] = state.keys_released[key] || state.keys_down[key];
        }
        state.keys_down.fill(false);
    }
    for (const UiKeyEvent& event : input.keys)
    {
        const std::size_t key = std::to_underlying(event.key);
        if (key == 0 || key >= state.keys_down.size() || !input.focused)
        {
            continue;
        }
        if (event.down)
        {
            // Shortcuts by the layout's symbol: letters lowercase.
            auto code = static_cast<std::uint32_t>(std::to_underlying(event.key_code));
            code = code >= 'A' && code <= 'Z' ? code - 'A' + 'a' : code;
            if (code < state.key_codes_pressed.size())
            {
                state.key_codes_pressed[code] = static_cast<std::uint8_t>(std::min(state.key_codes_pressed[code] + 1, 255));
            }
            state.keys_pressed[key] = static_cast<std::uint8_t>(std::min(state.keys_pressed[key] + 1, 255));
            state.keys_repeated[key] = static_cast<std::uint8_t>(std::min(state.keys_repeated[key] + (event.repeat ? 1 : 0), 255));
            state.keys_down[key] = true;
        }
        else
        {
            state.keys_down[key] = false;
            state.keys_released[key] = true;
        }
    }
    state.modifiers = input.focused ? input.modifiers : KeyModifier::NONE;
    state.typed_text.assign(input.focused ? input.text : std::string_view{});
    state.composition.assign(input.focused ? input.composition : std::string_view{});
    state.composition_cursor = std::clamp(input.composition_cursor, 0, static_cast<std::int32_t>(state.composition.size()));

    // The mouse: position, wheel, and per button its presses, releases, double clicks and drags.
    state.mouse_present = input.mouse_present;
    if (input.mouse_present)
    {
        state.mouse_position = input.mouse_position;
    }
    state.mouse_delta = state.mouse_present && state.previous_mouse_present
                            ? Vector2{.x = state.mouse_position.x - state.previous_mouse_position.x, .y = state.mouse_position.y - state.previous_mouse_position.y}
                            : Vector2{};
    state.previous_mouse_position = state.mouse_position;
    state.previous_mouse_present = state.mouse_present;
    state.mouse_wheel = input.mouse_wheel;
    for (MouseButtonState& button : state.mouse)
    {
        button.clicked = false;
        button.released = false;
        button.double_clicked = false;
    }
    for (const UiMouseButtonEvent& event : input.mouse_buttons)
    {
        MouseButtonState& button = state.mouse[button_index(event.button)];
        if (event.down)
        {
            const bool again = state.time - button.clicked_time <= state.description.input.double_click_seconds
                               && distance(event.position, button.clicked_position) <= state.description.input.double_click_distance;
            button.click_count = again ? button.click_count + 1 : 1;
            button.double_clicked = button.double_clicked || button.click_count == 2;
            button.clicked = true;
            button.clicked_time = state.time;
            button.clicked_position = event.position;
            button.down = true;
            button.drag_maximum = 0;
        }
        else
        {
            button.released = button.released || button.down;
            button.down = false;
        }
    }
    for (const MouseButton which : {MouseButton::LEFT, MouseButton::MIDDLE, MouseButton::RIGHT, MouseButton::BACK, MouseButton::FORWARD})
    {
        // The buttons held after the events correct missed events (and focus losses).
        MouseButtonState& button = state.mouse[button_index(which)];
        const bool held = input.focused && has_any(input.mouse_buttons_held, button_bit(which));
        if (button.down && !held)
        {
            button.released = true;
        }
        button.down = held || (button.down && button.clicked && input.focused);
        button.previous_held_seconds = button.held_seconds;
        button.held_seconds = !button.down ? -1.0 : button.clicked ? 0.0 : button.held_seconds + state.delta_time;
        if (button.down)
        {
            button.drag_maximum = std::max(button.drag_maximum, distance(state.mouse_position, button.clicked_position));
        }
    }

    // Windows of last frame: the frontmost root under the mouse, then its deepest child under it.
    for (WindowState& window : state.windows)
    {
        window.was_active = window.active;
        window.active = false;
        window.begin_count = 0;
    }
    state.hovered_root = ContextState::NO_WINDOW;
    state.hovered_window = ContextState::NO_WINDOW;
    if (state.mouse_present)
    {
        // Popups are over every window (the top one first), then the display order.
        const auto under_mouse = [&state](std::uint32_t index)
        {
            // Not through a dock space's pass-through hole, nor over a floating tree's grip (its host's).
            const WindowState& window = state.windows[index];
            if (!window.was_active || window.hidden || has_any(window.flags, UiWindowFlag::NO_MOUSE_INPUTS) || !contains(window.hit, state.mouse_position)
                || contains(window.hole, state.mouse_position))
            {
                return false;
            }
            if (window.docked && window.dock_node < state.dock_nodes.size())
            {
                const DockNodeState& root = state.dock_nodes[state.dock_root(window.dock_node)];
                if (root.dock_space == 0 && root.host_window < state.windows.size() && contains(state.windows[root.host_window].grip, state.mouse_position))
                {
                    return false;
                }
            }
            return true;
        };
        const std::size_t modal = state.top_modal_level();
        const std::size_t lowest = modal < state.popups.size() ? modal : 0;
        for (std::size_t level = state.popups.size(); level > lowest; --level)
        {
            const PopupState& popup = state.popups[level - 1];
            if (popup.window != ContextState::NO_WINDOW && under_mouse(popup.window))
            {
                state.hovered_root = popup.window;
                state.hovered_window = popup.window;
                break;
            }
        }
        for (const std::uint32_t index : std::views::reverse(state.display_order))
        {
            if (state.hovered_root != ContextState::NO_WINDOW || modal < state.popups.size())
            {
                break;
            }
            if (!state.windows[index].popup && !state.windows[index].tooltip && under_mouse(index))
            {
                state.hovered_root = index;
                state.hovered_window = index;
            }
        }
        for (const std::uint32_t index : state.children_last_frame)
        {
            const WindowState& child = state.windows[index];
            if (child.root == state.hovered_root && child.was_active && !has_any(child.flags, UiWindowFlag::NO_MOUSE_INPUTS)
                && contains(child.hit, state.mouse_position))
            {
                state.hovered_window = index;
            }
        }
    }
    if (state.moving_window != ContextState::NO_WINDOW)
    {
        state.hovered_root = state.moving_window;
        state.hovered_window = state.moving_window;
    }
    std::swap(state.children_last_frame, state.children_this_frame);
    state.children_this_frame.clear();

    // The wheel scrolls the hovered window, or the first parent that can scroll.
    state.wheel_window = state.hovered_window;
    while (state.wheel_window != ContextState::NO_WINDOW)
    {
        const WindowState& window = state.windows[state.wheel_window];
        const bool can_scroll = !has_any(window.flags, UiWindowFlag::NO_SCROLL_WITH_MOUSE)
                                && (window.scroll_maximum.y > 0 || window.scroll_maximum.x > 0);
        if (can_scroll || window.parent == ContextState::NO_WINDOW)
        {
            break;
        }
        state.wheel_window = window.parent;
    }

    // A click focuses the window under it (or nothing).
    const bool clicked = state.mouse[0].clicked || state.mouse[1].clicked || state.mouse[2].clicked;
    const bool blocked_by_modal = state.top_modal_level() < state.popups.size() && state.hovered_root == ContextState::NO_WINDOW;
    if (clicked && state.moving_window == ContextState::NO_WINDOW && !blocked_by_modal)
    {
        state.focus_window(state.hovered_root);
        if (state.hovered_root == ContextState::NO_WINDOW)
        {
            state.focus_id = 0;
            state.focus_visible = false;
        }
    }

    // A window being moved follows the mouse until the button is released.
    if (state.moving_window != ContextState::NO_WINDOW)
    {
        if (state.mouse[0].down)
        {
            WindowState& window = state.windows[state.moving_window];
            window.position = {.x = state.mouse_position.x - state.moving_offset.x, .y = state.mouse_position.y - state.moving_offset.y};
        }
        else
        {
            state.apply_dock_drop();
            state.moving_window = ContextState::NO_WINDOW;
            state.clear_active();
        }
    }

    // Popups: those no longer submitted close, a click outside closes them, Escape the top one.
    const bool escape_closed_popup = state.update_popups();

    // The field being edited: a click outside it commits it (the field reads the result this frame).
    state.text_edit.ended_id = 0;
    state.text_input_wanted = false;
    if (state.text_edit.id != 0 && clicked && !contains(state.text_edit.frame, state.mouse_position))
    {
        state.end_text_edit(true);
    }

    // The active item stays active only while it is submitted (a field no longer submitted stops being edited).
    if (state.active_id != 0 && !state.active_id_alive)
    {
        state.clear_active();
    }
    if (state.text_edit.id != 0 && state.active_id != state.text_edit.id)
    {
        state.end_text_edit(true);
    }
    state.active_id_alive = false;
    state.active_id_previous_frame = state.active_id;
    state.active_id_edited_previous = state.active_id_edited;
    state.deactivated_id = 0;
    state.deactivated_after_edit = false;
    state.hovered_id_previous = state.hovered_id;
    state.hovered_id = 0;

    // The keyboard focus: Tab and Shift+Tab move it (Down and Up too in a popup that is not a modal), Escape drops
    // it (unless it closed a popup).
    const auto pressed = [&state](Key key) { return state.keys_pressed[std::to_underlying(key)] > 0; };
    state.focus_step = 0;
    if (state.focused_window != ContextState::NO_WINDOW && pressed(Key::TAB))
    {
        state.focus_step = has_any(state.modifiers, KeyModifier::SHIFT) ? -1 : 1;
    }
    const std::size_t focused_level = state.popup_level_of(state.focused_window);
    if (focused_level < state.popups.size() && !state.popups[focused_level].modal && state.text_edit.id == 0)
    {
        state.focus_step = pressed(Key::DOWN) ? 1 : pressed(Key::UP) ? -1 : state.focus_step;
    }
    if (pressed(Key::ESCAPE) && !escape_closed_popup)
    {
        state.focus_id = 0;
        state.focus_visible = false;
    }
    state.focus_alive = false;
    state.focus_seen_current = false;
    state.focus_first = 0;
    state.focus_last = 0;
    state.focus_before_current = 0;
    state.focus_after_current = 0;
    state.focus_here_requested = false;
    state.focus_here_result = 0;

    // The style and colors of the theme; stacks empty; the font.
    state.style = state.theme.style;
    state.colors = state.theme.colors;
    state.disabled = false;
    state.default_font_metrics = state.metrics_of(state.default_font);
    state.cursor = CursorShape::ARROW;
    state.next_window = NextWindowSettings{};
    state.next_item_has_width = false;
    state.last_item = LastItem{};
    state.next_item_has_open = false;
    state.tree_stack.clear();
    state.tab_bar_stack.clear();
    state.table_stack.clear();
    state.menu_bar = MenuBarState{};
    state.tooltip_seen = false;
    state.tooltip_depth = 0;

    // A drag and drop ends after its delivery, or once its source stopped setting the payload and its button is up
    // (at once with DragDropFlag::PAYLOAD_AUTO_EXPIRE).
    DragDropState& drag = state.drag_drop;
    if (drag.active)
    {
        const bool elapsed = drag.data_frame + 1 < state.frame
                             && (has_any(drag.source_flags, DragDropFlag::PAYLOAD_AUTO_EXPIRE) || !state.mouse[button_index(drag.button)].down);
        if (drag.delivered || elapsed)
        {
            drag.active = false;
            drag.type.clear();
            drag.data.clear();
            if (state.active_id == drag.source_id && !state.mouse[button_index(drag.button)].down)
            {
                state.clear_active();
            }
        }
    }
    drag.accept_id_previous = drag.accept_id;
    drag.accept_id = 0;
    drag.accept_area = std::numeric_limits<float>::max();
    drag.hide_preview = drag.hide_preview_next;
    drag.hide_preview_next = false;
    drag.in_source = false;
    drag.in_target = false;
    drag.hold_seen = false;
    for (DrawList* list : {&state.background, &state.foreground})
    {
        list->clear();
        list->set_pixels_per_unit(state.pixels_per_unit);
    }
    state.frame_active = true;
    return state.frame_object;
}

auto
UiContext::end_frame() noexcept -> UiFrameReport
{
    ContextState& state = *m_state;
    UiFrame& frame = state.frame_object;
    if (!state.frame_active)
    {
        state.report(UiFrameProblem::FRAME_NOT_ACTIVE, "end_frame", "call begin_frame() first");
        return state.report_value;
    }

    // What was left open is closed, and reported.
    while (!state.table_stack.empty())
    {
        state.report(UiFrameProblem::UNBALANCED_STACK, "end_frame", "begin_table() without end_table()");
        frame.end_table();
    }
    if (!state.group_stack.empty())
    {
        state.report(UiFrameProblem::UNBALANCED_STACK, "end_frame", "begin_group() without end_group()");
        while (!state.group_stack.empty())
        {
            frame.end_group();
        }
    }
    while (!state.window_stack.empty())
    {
        const WindowState& window = state.windows[state.window_stack.back()];
        state.report(UiFrameProblem::UNBALANCED_STACK, "end_frame", window.child ? "begin_child() without end_child()" : "begin_window() without end_window()",
                     window.name, window.id);
        if (window.child)
        {
            frame.end_child();
        }
        else
        {
            frame.end_window();
        }
    }
    const auto unbalanced = [&state](bool left, std::string_view detail)
    {
        if (left)
        {
            state.report(UiFrameProblem::UNBALANCED_STACK, "end_frame", detail);
        }
    };
    unbalanced(!state.id_stack.empty(), "push_id() without pop_id()");
    unbalanced(!state.color_stack.empty(), "push_style_color() without pop_style_color()");
    unbalanced(!state.variable_stack.empty(), "push_style_variable() without pop_style_variable()");
    unbalanced(!state.font_stack.empty(), "push_font() without pop_font()");
    unbalanced(!state.item_width_stack.empty(), "push_item_width() without pop_item_width()");
    unbalanced(!state.text_wrap_stack.empty(), "push_text_wrap_position() without pop_text_wrap_position()");
    unbalanced(!state.disabled_stack.empty(), "begin_disabled() without end_disabled()");
    unbalanced(!state.tree_stack.empty(), "tree_node() or tree_push() without tree_pop()");
    unbalanced(!state.tab_bar_stack.empty(), "begin_tab_bar() without end_tab_bar()");
    unbalanced(state.menu_bar.active, "begin_menu_bar() without end_menu_bar()");
    unbalanced(state.drag_drop.in_source, "begin_drag_drop_source() without end_drag_drop_source()");
    unbalanced(state.drag_drop.in_target, "begin_drag_drop_target() without end_drag_drop_target()");
    if (!state.tooltip_seen)
    {
        state.tooltip_key = 0;
    }
    if (!state.drag_drop.hold_seen)
    {
        state.drag_drop.hold_id = 0;
    }
    state.id_stack.clear();
    state.color_stack.clear();
    state.variable_stack.clear();
    state.font_stack.clear();
    state.item_width_stack.clear();
    state.text_wrap_stack.clear();
    state.disabled_stack.clear();
    state.disabled = false;

    // The keyboard focus moves for the next frame (wrapping around), or leaves an item no longer submitted; the
    // item it moved to knows it arrived (a text field starts editing).
    state.focus_arrived_id = 0;
    if (state.focus_here_result != 0)
    {
        state.focus_window(state.focus_here_window);
        state.focus_id = state.focus_here_result;
        state.focus_visible = true;
        state.focus_arrived_id = state.focus_id;
    }
    else if (state.focus_step > 0 && state.focus_first != 0)
    {
        state.focus_id = state.focus_seen_current && state.focus_after_current != 0 ? state.focus_after_current : state.focus_first;
        state.focus_visible = true;
        state.focus_arrived_id = state.focus_id;
    }
    else if (state.focus_step < 0 && state.focus_last != 0)
    {
        state.focus_id = state.focus_seen_current && state.focus_before_current != 0 ? state.focus_before_current : state.focus_last;
        state.focus_visible = true;
        state.focus_arrived_id = state.focus_id;
    }
    else if (state.focus_id != 0 && !state.focus_alive)
    {
        state.focus_id = 0;
        state.focus_visible = false;
    }
    if (state.default_focus_request != 0 && state.focus_id == 0)
    {
        state.focus_id = state.default_focus_request;
        state.focus_visible = true;
    }
    state.default_focus_request = 0;

    // What the application should leave to the UI.
    const bool mouse_held_by_ui = (state.active_id != 0 && (state.active_id != state.text_edit.id || state.text_edit.mouse_selecting))
                                  || state.moving_window != ContextState::NO_WINDOW;
    state.wants_mouse = state.hovered_root != ContextState::NO_WINDOW || mouse_held_by_ui || !state.popups.empty() || state.drag_drop.active;
    state.wants_keyboard = (state.focus_id != 0 && state.focus_visible) || state.text_edit.id != 0;

    // Text input (IME, on-screen keyboards) while a field is edited, placed at its cursor.
    const bool text_input = state.text_edit.id != 0 && state.text_input_wanted;
    if (text_input != state.applied_text_input || (text_input && state.text_input_area != state.applied_text_input_area))
    {
        state.applied_text_input = text_input;
        state.applied_text_input_area = state.text_input_area;
        try
        {
            state.services->set_text_input(text_input, state.text_input_area);
        }
        catch (...)
        {
            state.report(UiFrameProblem::PLATFORM_SERVICE, "end_frame", "the platform could not start or stop text input");
        }
    }

    // The cursor shape, when it changed.
    if (state.cursor != state.applied_cursor)
    {
        try
        {
            state.services->set_cursor(state.cursor);
            state.applied_cursor = state.cursor;
        }
        catch (...)
        {
            state.report(UiFrameProblem::PLATFORM_SERVICE, "end_frame", "the platform could not change the mouse cursor");
            state.applied_cursor = state.cursor;
        }
    }

    // Publication into a slot that is neither the published one nor leased (waiting only when both others are).
    const std::uint32_t published = state.published.load();
    std::uint32_t target = 3;
    while (target == 3)
    {
        for (std::uint32_t slot = 0; slot < state.slots.size(); ++slot)
        {
            if (slot != published && state.slots[slot].leases.load() == 0)
            {
                target = slot;
                break;
            }
        }
        if (target == 3)
        {
            const std::uint32_t waited = published == 0 ? 1U : 0U;
            state.slots[waited].leases.wait(state.slots[waited].leases.load());
        }
    }
    state.update_dock_drop();
    state.arrange_docked_windows();
    DrawDataSlot& slot = state.slots[target];
    std::size_t count = 0;
    const auto publish = [&](DrawList& list)
    {
        if (list.vertices().empty())
        {
            return;
        }
        if (count == slot.lists.size())
        {
            state.allocations += slot.lists.size() == slot.lists.capacity() ? 1U : 0U;
            slot.lists.emplace_back();
        }
        std::swap(slot.lists[count], list);
        ++count;
    };
    publish(state.background);
    std::uint32_t windows_drawn = 0;
    for (const std::uint32_t entry : state.publish_order)
    {
        // Windows in display order (docked ones after their host, then the host's front list).
        const std::uint32_t index = entry & 0x7FFFFFFFU;
        WindowState& window = state.windows[index];
        if ((entry & 0x80000000U) != 0)
        {
            if (window.active && !window.hidden)
            {
                publish(window.front_list);
            }
            continue;
        }
        windows_drawn += window.active ? 1U : 0U;
        if (window.active && !window.hidden && !window.popup && !window.tooltip)
        {
            publish(window.draw_list);
        }
    }
    publish(state.dock_overlay);
    const std::size_t modal = state.top_modal_level();
    for (std::size_t level = 0; level < state.popups.size(); ++level)
    {
        // Popups over the windows, in the order they opened; what is under the top-most modal is dimmed.
        const PopupState& popup = state.popups[level];
        if (popup.window != ContextState::NO_WINDOW)
        {
            WindowState& window = state.windows[popup.window];
            if (window.active && !window.hidden && window.popup)
            {
                if (level == modal)
                {
                    state.modal_dim.clear();
                    state.modal_dim.set_pixels_per_unit(state.pixels_per_unit);
                    try
                    {
                        state.modal_dim.add_rectangle_filled({.right = state.display_size.x, .bottom = state.display_size.y},
                                                             state.color(StyleColor::MODAL_WINDOW_DIM_BACKGROUND));
                    }
                    catch (...)
                    {
                        state.report(UiFrameProblem::CAPACITY_EXCEEDED, "end_frame", "the modal's dimming could not be drawn");
                    }
                    publish(state.modal_dim);
                }
                publish(window.draw_list);
            }
        }
    }
    for (const std::uint32_t index : state.display_order)
    {
        // Tooltips over everything.
        WindowState& window = state.windows[index];
        if (window.active && !window.hidden && window.tooltip)
        {
            publish(window.draw_list);
        }
    }
    publish(state.foreground);
    slot.pointers.clear();
    UiFrameStatistics statistics{.windows = static_cast<std::uint32_t>(state.children_this_frame.size()) + windows_drawn, .items = state.items};
    for (std::size_t list = 0; list < count; ++list)
    {
        slot.pointers.push_back(&slot.lists[list]);
        statistics.vertices += static_cast<std::uint32_t>(slot.lists[list].vertices().size());
        statistics.indices += static_cast<std::uint32_t>(slot.lists[list].indices().size());
    }
    statistics.draw_lists = static_cast<std::uint32_t>(count);
    slot.data = UiDrawData{.lists = slot.pointers,
                           .width = std::round(state.display_size.x * state.pixels_per_unit),
                           .height = std::round(state.display_size.y * state.pixels_per_unit),
                           .scale = state.pixels_per_unit};
    state.published.store(target);

    // The report: the frame's issues copied out (valid until the next end_frame()), the counters reset.
    state.allocations += state.arena_upstream.allocations;
    if (state.arena_upstream.allocations != 0)
    {
        state.report(UiFrameProblem::CAPACITY_EXCEEDED, "end_frame", "the frame arena was too small (UiContextLimits::frame_arena_bytes)");
    }
    statistics.allocations = state.allocations;
    statistics.arena_bytes = state.arena_used;
    std::copy_n(state.issues.begin(), state.issue_count, state.report_issues.begin());
    state.report_value = UiFrameReport{.problems = state.problems,
                                       .issues = std::span{state.report_issues.data(), state.issue_count},
                                       .dropped_issues = state.dropped_issues,
                                       .statistics = statistics};
    state.issue_count = 0;
    state.dropped_issues = 0;
    state.problems = UiFrameProblem::NONE;
    state.allocations = 0;
    state.frame_active = false;
    return state.report_value;
}

auto
UiContext::draw_data() const noexcept -> const UiDrawData&
{
    const std::uint32_t published = m_state->published.load();
    return published < m_state->slots.size() ? m_state->slots[published].data : m_state->empty_data;
}

auto
UiContext::acquire_draw_data() const noexcept -> UiDrawDataLease
{
    // Lease the published slot, then check it is still the published one (end_frame() may have moved on).
    ContextState& state = *m_state;
    for (;;)
    {
        const std::uint32_t published = state.published.load();
        if (published >= state.slots.size())
        {
            return {};
        }
        DrawDataSlot& slot = state.slots[published];
        slot.leases.fetch_add(1);
        if (state.published.load() == published)
        {
            return UiDrawDataLease{slot.data, slot.leases};
        }
        slot.leases.fetch_sub(1);
        slot.leases.notify_all();
    }
}

auto
UiContext::last_report() const noexcept -> const UiFrameReport&
{
    return m_state->report_value;
}

auto
UiContext::wants_mouse() const noexcept -> bool
{
    return m_state->wants_mouse;
}

auto
UiContext::wants_keyboard() const noexcept -> bool
{
    return m_state->wants_keyboard;
}

auto
UiContext::wants_text_input() const noexcept -> bool
{
    return m_state->text_edit.id != 0;
}

auto
UiContext::mouse_cursor() const noexcept -> CursorShape
{
    return m_state->applied_cursor;
}

auto
UiContext::frame_count() const noexcept -> std::uint64_t
{
    return m_state->frame;
}

// ===================================================================================================== leases

UiDrawDataLease::UiDrawDataLease(const UiDrawData& data,
                                 std::atomic<std::uint32_t>& leases) noexcept
    : m_data{&data}
    , m_leases{&leases}
{
}

UiDrawDataLease::UiDrawDataLease(UiDrawDataLease&& other) noexcept
    : m_data{std::exchange(other.m_data, nullptr)}
    , m_leases{std::exchange(other.m_leases, nullptr)}
{
}

auto
UiDrawDataLease::operator=(UiDrawDataLease&& other) noexcept -> UiDrawDataLease&
{
    if (this != &other)
    {
        release();
        m_data = std::exchange(other.m_data, nullptr);
        m_leases = std::exchange(other.m_leases, nullptr);
    }
    return *this;
}

UiDrawDataLease::~UiDrawDataLease()
{
    release();
}

auto
UiDrawDataLease::data() const noexcept -> const UiDrawData&
{
    static constexpr UiDrawData EMPTY{};
    return m_data != nullptr ? *m_data : EMPTY;
}

void
UiDrawDataLease::release() noexcept
{
    if (m_leases != nullptr)
    {
        m_leases->fetch_sub(1);
        m_leases->notify_all();
        m_leases = nullptr;
        m_data = nullptr;
    }
}

} // namespace gfx::ui
