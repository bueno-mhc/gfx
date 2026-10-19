/// @file reports.cpp
/// @brief UiFrame's frame arena, its reports and the diagnostics window.
module gfx.ui.context;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.handles;
import gfx.ui.draw;
import :state;

namespace gfx::ui
{

namespace
{

/// The spelling of the single problem @p problem (allocation-free).
auto
problem_name(UiFrameProblem problem) noexcept -> std::string_view
{
    for (const auto& entry : EnumerationTraits<UiFrameProblem>::NAMES)
    {
        if (entry.value == problem)
        {
            return entry.name;
        }
    }
    return "PROBLEMS";
}

} // namespace

auto
UiFrame::arena_characters(std::size_t size) noexcept -> std::span<char>
{
    try
    {
        void* memory = m_state->arena.allocate(size, alignof(char));
        m_state->arena_used += size;
        return {static_cast<char*>(memory), size};
    }
    catch (...)
    {
        m_state->report(UiFrameProblem::CAPACITY_EXCEEDED, "text", "the frame arena could not provide memory: the text was cut");
        return {};
    }
}

void
UiFrame::report_format_failure() noexcept
{
    m_state->report(UiFrameProblem::INVALID_ARGUMENT, "text", "the text could not be formatted (a formatter threw)");
}

void
UiFrame::show_diagnostics_window(bool& open) noexcept
{
    if (!open)
    {
        return;
    }

    // The previous frame's statistics and each of its issues.
    const UiFrameReport& report = m_state->report_value;
    const UiFrameStatistics& statistics = report.statistics;
    set_next_window_size({.x = 460.0F, .y = 280.0F}, UiCondition::FIRST_USE_EVER);
    if (begin_window("UI diagnostics", open))
    {
        text("{} windows, {} items, {} draw lists", statistics.windows, statistics.items, statistics.draw_lists);
        text("{} vertices, {} indices", statistics.vertices, statistics.indices);
        text("frame arena {} bytes, {} allocations", statistics.arena_bytes, statistics.allocations);
        separator();
        if (report.ok())
        {
            text_unformatted("No problem in the last frame.");
        }
        for (const UiFrameIssue& issue : report.issues)
        {
            text_wrapped("{} in {} {}: {}", problem_name(issue.problem), issue.operation, issue.name(), issue.detail);
        }
        if (report.dropped_issues > 0)
        {
            text_disabled("{} more issues", report.dropped_issues);
        }
    }
    end_window();
}

} // namespace gfx::ui
