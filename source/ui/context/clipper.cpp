/// @file clipper.cpp
/// @brief UiListClipper: of a long list of items of one height (a table's rows), only the visible ones (and those
///        asked for) are submitted; the cursor is moved past the others, so the window still scrolls over the whole
///        list.
module gfx.ui.context;

import std;
import gfx.core.types;
import gfx.ui.enums;
import :state;

namespace gfx::ui
{

namespace
{

/// Steps of a list: begun, its first item measured (or its height known), ranges given out, done.
constexpr std::uint32_t STEP_BEGUN = 0;
constexpr std::uint32_t STEP_MEASURED = 1;
constexpr std::uint32_t STEP_RANGES = 2;
constexpr std::uint32_t STEP_DONE = 3;

} // namespace

UiListClipper::~UiListClipper()
{
    end();
}

auto
UiListClipper::list_window() const noexcept -> WindowState*
{
    // Still the frame and the window the list began in.
    if (m_state == nullptr || !m_state->frame_active || m_state->frame != m_frame || m_state->window_stack.empty()
        || m_state->window_stack.back() != m_window)
    {
        return nullptr;
    }
    return &m_state->windows[m_window];
}

void
UiListClipper::begin(UiFrame& frame,
                     std::uint32_t count,
                     float item_height) noexcept
{
    // A list in progress ends; the new one starts where the next item goes.
    end();
    ContextState& state = *frame.m_state;
    m_state = nullptr;
    m_step = STEP_DONE;
    m_display_start = 0;
    m_display_end = 0;
    WindowState* window = state.current_window("UiListClipper::begin");
    if (window == nullptr)
    {
        return;
    }
    m_state = &state;
    m_frame = state.frame;
    m_window = static_cast<std::uint32_t>(window - state.windows.data());
    m_count = count;
    m_item_height = item_height > 0 ? item_height : 0.0F;
    m_next = 0;
    m_included_count = 0;
    m_range_count = 0;
    m_range = 0;
    m_step = STEP_BEGUN;
    m_start = state.clipper_position(*window);
}

void
UiListClipper::include_item(std::uint32_t item) noexcept
{
    if (item < m_count && m_included_count < m_included.size() && m_step < STEP_RANGES)
    {
        m_included[m_included_count++] = item;
    }
}

void
UiListClipper::seek(std::uint32_t item) noexcept
{
    // Items between the last one submitted and this one are skipped: the cursor moves past them.
    WindowState* window = list_window();
    if (window == nullptr || item <= m_next)
    {
        return;
    }
    m_state->clipper_seek(*window, m_start + m_item_height * static_cast<float>(item), item - m_next);
    m_next = item;
}

auto
UiListClipper::step() noexcept -> bool
{
    WindowState* window = list_window();
    if (window == nullptr || m_step == STEP_DONE)
    {
        m_step = STEP_DONE;
        m_display_start = 0;
        m_display_end = 0;
        return false;
    }

    // First step: nothing to submit, or the first item alone when its height is to be measured.
    if (m_step == STEP_BEGUN)
    {
        if (m_count == 0 || window->skip_items)
        {
            end();
            return false;
        }
        m_step = STEP_MEASURED;
        if (m_item_height <= 0)
        {
            m_display_start = 0;
            m_display_end = 1;
            m_next = 1;
            return true;
        }
    }

    // The visible items (and those asked for), merged into ranges in order.
    if (m_step == STEP_MEASURED)
    {
        if (m_item_height <= 0)
        {
            m_item_height = std::max(m_state->clipper_position(*window) - m_start, 1.0F);
        }
        const auto [top, bottom] = m_state->clipper_visible(*window);
        const auto item_at = [this](float y, bool up)
        {
            const float position = (y - m_start) / m_item_height;
            const float rounded = up ? std::ceil(position) : std::floor(position);
            return static_cast<std::uint32_t>(std::clamp(rounded, 0.0F, static_cast<float>(m_count)));
        };
        std::array<std::pair<std::uint32_t, std::uint32_t>, 5> ranges{};
        std::size_t count = 0;
        ranges[count++] = {std::max(item_at(top, false), m_next), std::max(item_at(bottom, true), m_next)};
        for (std::uint32_t included = 0; included < m_included_count; ++included)
        {
            if (m_included[included] >= m_next)
            {
                ranges[count++] = {m_included[included], m_included[included] + 1};
            }
        }
        // In order (an insertion sort of five at most: std::ranges::sort draws GCC's -Warray-bounds at -O3).
        count = std::min(count, ranges.size());
        for (std::size_t range = 1; range < count; ++range)
        {
            for (std::size_t place = range; place > 0 && ranges[place] < ranges[place - 1]; --place)
            {
                std::swap(ranges[place], ranges[place - 1]);
            }
        }
        m_range_count = 0;
        for (std::size_t range = 0; range < count; ++range)
        {
            const auto [first, last] = ranges[range];
            if (first >= last)
            {
                continue;
            }
            if (m_range_count > 0 && first <= m_ranges[(m_range_count - 1) * 2 + 1])
            {
                m_ranges[(m_range_count - 1) * 2 + 1] = std::max(m_ranges[(m_range_count - 1) * 2 + 1], last);
                continue;
            }
            m_ranges[m_range_count * 2] = first;
            m_ranges[m_range_count * 2 + 1] = last;
            ++m_range_count;
        }
        m_range = 0;
        m_step = STEP_RANGES;
    }

    // The next range; after the last, the cursor goes past the list.
    while (m_range < m_range_count)
    {
        const std::uint32_t first = std::max(m_ranges[m_range * 2], m_next);
        const std::uint32_t last = m_ranges[m_range * 2 + 1];
        ++m_range;
        if (first >= last)
        {
            continue;
        }
        seek(first);
        m_display_start = first;
        m_display_end = last;
        m_next = last;
        return true;
    }
    end();
    return false;
}

void
UiListClipper::end() noexcept
{
    // Unfinished: the items not submitted are skipped (the window's content keeps the whole list's height).
    if (m_step != STEP_DONE && m_state != nullptr)
    {
        WindowState* window = list_window();
        if (window != nullptr && m_item_height <= 0 && m_next > 0)
        {
            m_item_height = std::max(m_state->clipper_position(*window) - m_start, 1.0F);
        }
        if (window != nullptr && m_item_height > 0)
        {
            seek(m_count);
        }
    }
    m_step = STEP_DONE;
    m_display_start = 0;
    m_display_end = 0;
}

} // namespace gfx::ui
