/// @file later.cpp
/// @brief UiFrame's frame arena, its reports, the diagnostics window, and the widgets of later phases (U8, U9):
///        declared so the whole interface can be reviewed; until their phase each reports
///        UiFrameProblem::NOT_IMPLEMENTED (once per frame) and does nothing.
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

auto
UiFrame::not_implemented(std::string_view operation,
                         std::string_view detail) noexcept -> bool
{
    // Once per frame and operation: a widget called in a loop does not fill the report.
    ContextState& state = *m_state;
    const auto issues = std::span{state.issues.data(), state.issue_count};
    const bool reported = std::ranges::any_of(issues, [operation](const UiFrameIssue& issue)
    {
        return issue.problem == UiFrameProblem::NOT_IMPLEMENTED && issue.operation == operation;
    });
    if (reported)
    {
        state.problems = state.problems | UiFrameProblem::NOT_IMPLEMENTED;
    }
    else
    {
        state.report(UiFrameProblem::NOT_IMPLEMENTED, operation, detail);
    }
    return false;
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

// ===================================================================================================== U8

auto
UiFrame::dock_space(DockSpaceIdentifier /*id*/,
                    Vector2 /*size*/,
                    DockNodeFlag /*flags*/) noexcept -> DockNodeHandle
{
    static_cast<void>(not_implemented("dock_space", "docking arrives in phase U8"));
    return {};
}

void
UiFrame::set_next_window_dock(DockNodeHandle /*node*/,
                              UiCondition /*condition*/) noexcept
{
    static_cast<void>(not_implemented("set_next_window_dock", "docking arrives in phase U8"));
}

auto
UiFrame::is_window_docked() noexcept -> bool
{
    return not_implemented("is_window_docked", "docking arrives in phase U8");
}

auto
UiFrame::window_dock_node() noexcept -> DockNodeHandle
{
    static_cast<void>(not_implemented("window_dock_node", "docking arrives in phase U8"));
    return {};
}

// ===================================================================================================== U9

auto
UiFrame::shortcut(KeyChord /*chord*/,
                  ShortcutFlag /*flags*/) noexcept -> bool
{
    return not_implemented("shortcut", "shortcuts arrive in phase U9");
}

void
UiFrame::set_next_item_shortcut(KeyChord /*chord*/,
                                ShortcutFlag /*flags*/) noexcept
{
    static_cast<void>(not_implemented("set_next_item_shortcut", "shortcuts arrive in phase U9"));
}

void
UiFrame::set_next_item_accessible_label(std::string_view /*label*/) noexcept
{
    static_cast<void>(not_implemented("set_next_item_accessible_label", "accessibility arrives in phase U9"));
}

void
UiFrame::set_next_item_accessible_description(std::string_view /*description*/) noexcept
{
    static_cast<void>(not_implemented("set_next_item_accessible_description", "accessibility arrives in phase U9"));
}

void
UiFrame::set_next_item_accessibility_role(AccessibilityRole /*role*/) noexcept
{
    static_cast<void>(not_implemented("set_next_item_accessibility_role", "accessibility arrives in phase U9"));
}

void
UiFrame::announce(std::string_view /*text*/,
                  AccessibilityLiveRegion /*live_region*/) noexcept
{
    static_cast<void>(not_implemented("announce", "accessibility arrives in phase U9"));
}

} // namespace gfx::ui
