/// @file later.cpp
/// @brief UiFrame's frame arena, its reports, the diagnostics window, and the widgets of later phases (U6 to U9):
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

// ===================================================================================================== U6

auto
UiFrame::tree_node(std::string_view /*label*/,
                   TreeNodeFlag /*flags*/) noexcept -> bool
{
    return not_implemented("tree_node", "trees arrive in phase U6");
}

void
UiFrame::tree_push(std::string_view /*id*/) noexcept
{
    static_cast<void>(not_implemented("tree_push", "trees arrive in phase U6"));
}

void
UiFrame::tree_pop() noexcept
{
    static_cast<void>(not_implemented("tree_pop", "trees arrive in phase U6"));
}

auto
UiFrame::collapsing_header(std::string_view /*label*/,
                           TreeNodeFlag /*flags*/) noexcept -> bool
{
    return not_implemented("collapsing_header", "collapsing headers arrive in phase U6");
}

auto
UiFrame::collapsing_header(std::string_view /*label*/,
                           bool& /*visible*/,
                           TreeNodeFlag /*flags*/) noexcept -> bool
{
    return not_implemented("collapsing_header", "collapsing headers arrive in phase U6");
}

void
UiFrame::set_next_item_open(bool /*open*/,
                            UiCondition /*condition*/) noexcept
{
    static_cast<void>(not_implemented("set_next_item_open", "trees arrive in phase U6"));
}

auto
UiFrame::begin_tab_bar(std::string_view /*id*/,
                       TabBarFlag /*flags*/) noexcept -> bool
{
    return not_implemented("begin_tab_bar", "tab bars arrive in phase U6");
}

void
UiFrame::end_tab_bar() noexcept
{
    static_cast<void>(not_implemented("end_tab_bar", "tab bars arrive in phase U6"));
}

auto
UiFrame::begin_tab_item(std::string_view /*label*/,
                        TabItemFlag /*flags*/) noexcept -> bool
{
    return not_implemented("begin_tab_item", "tab bars arrive in phase U6");
}

auto
UiFrame::begin_tab_item(std::string_view /*label*/,
                        bool& /*open*/,
                        TabItemFlag /*flags*/) noexcept -> bool
{
    return not_implemented("begin_tab_item", "tab bars arrive in phase U6");
}

void
UiFrame::end_tab_item() noexcept
{
    static_cast<void>(not_implemented("end_tab_item", "tab bars arrive in phase U6"));
}

auto
UiFrame::tab_item_button(std::string_view /*label*/,
                         TabItemFlag /*flags*/) noexcept -> bool
{
    return not_implemented("tab_item_button", "tab bars arrive in phase U6");
}

auto
UiFrame::begin_menu_bar() noexcept -> bool
{
    return not_implemented("begin_menu_bar", "menus arrive in phase U6");
}

void
UiFrame::end_menu_bar() noexcept
{
    static_cast<void>(not_implemented("end_menu_bar", "menus arrive in phase U6"));
}

auto
UiFrame::begin_main_menu_bar() noexcept -> bool
{
    return not_implemented("begin_main_menu_bar", "menus arrive in phase U6");
}

void
UiFrame::end_main_menu_bar() noexcept
{
    static_cast<void>(not_implemented("end_main_menu_bar", "menus arrive in phase U6"));
}

auto
UiFrame::begin_menu(std::string_view /*label*/,
                    bool /*enabled*/) noexcept -> bool
{
    return not_implemented("begin_menu", "menus arrive in phase U6");
}

void
UiFrame::end_menu() noexcept
{
    static_cast<void>(not_implemented("end_menu", "menus arrive in phase U6"));
}

auto
UiFrame::menu_item(std::string_view /*label*/,
                   std::string_view /*shortcut*/,
                   bool /*selected*/,
                   bool /*enabled*/) noexcept -> bool
{
    return not_implemented("menu_item", "menus arrive in phase U6");
}

auto
UiFrame::menu_item(std::string_view /*label*/,
                   std::string_view /*shortcut*/,
                   bool* /*selected*/,
                   bool /*enabled*/) noexcept -> bool
{
    return not_implemented("menu_item", "menus arrive in phase U6");
}

void
UiFrame::open_popup(std::string_view /*id*/,
                    PopupFlag /*flags*/) noexcept
{
    static_cast<void>(not_implemented("open_popup", "popups arrive in phase U6"));
}

auto
UiFrame::begin_popup(std::string_view /*id*/,
                     UiWindowFlag /*flags*/) noexcept -> bool
{
    return not_implemented("begin_popup", "popups arrive in phase U6");
}

auto
UiFrame::begin_modal(std::string_view /*name*/,
                     UiWindowFlag /*flags*/) noexcept -> bool
{
    return not_implemented("begin_modal", "modal popups arrive in phase U6");
}

auto
UiFrame::begin_modal(std::string_view /*name*/,
                     bool& /*open*/,
                     UiWindowFlag /*flags*/) noexcept -> bool
{
    return not_implemented("begin_modal", "modal popups arrive in phase U6");
}

void
UiFrame::end_popup() noexcept
{
    static_cast<void>(not_implemented("end_popup", "popups arrive in phase U6"));
}

auto
UiFrame::begin_popup_context_item(std::string_view /*id*/,
                                  PopupFlag /*flags*/) noexcept -> bool
{
    return not_implemented("begin_popup_context_item", "popups arrive in phase U6");
}

auto
UiFrame::begin_popup_context_window(std::string_view /*id*/,
                                    PopupFlag /*flags*/) noexcept -> bool
{
    return not_implemented("begin_popup_context_window", "popups arrive in phase U6");
}

void
UiFrame::close_current_popup() noexcept
{
    static_cast<void>(not_implemented("close_current_popup", "popups arrive in phase U6"));
}

auto
UiFrame::is_popup_open(std::string_view /*id*/,
                       PopupFlag /*flags*/) noexcept -> bool
{
    return not_implemented("is_popup_open", "popups arrive in phase U6");
}

auto
UiFrame::begin_tooltip() noexcept -> bool
{
    return not_implemented("begin_tooltip", "tooltips arrive in phase U6");
}

void
UiFrame::end_tooltip() noexcept
{
    static_cast<void>(not_implemented("end_tooltip", "tooltips arrive in phase U6"));
}

void
UiFrame::tooltip(std::string_view /*text*/) noexcept
{
    static_cast<void>(not_implemented("tooltip", "tooltips arrive in phase U6"));
}

auto
UiFrame::begin_item_tooltip() noexcept -> bool
{
    return not_implemented("begin_item_tooltip", "tooltips arrive in phase U6");
}

void
UiFrame::item_tooltip(std::string_view /*text*/) noexcept
{
    static_cast<void>(not_implemented("item_tooltip", "tooltips arrive in phase U6"));
}

auto
UiFrame::begin_drag_drop_source(DragDropFlag /*flags*/) noexcept -> bool
{
    return not_implemented("begin_drag_drop_source", "drag and drop arrives in phase U6");
}

auto
UiFrame::set_drag_drop_payload(std::string_view /*type*/,
                               std::span<const std::byte> /*data*/,
                               UiCondition /*condition*/) noexcept -> bool
{
    return not_implemented("set_drag_drop_payload", "drag and drop arrives in phase U6");
}

void
UiFrame::end_drag_drop_source() noexcept
{
    static_cast<void>(not_implemented("end_drag_drop_source", "drag and drop arrives in phase U6"));
}

auto
UiFrame::begin_drag_drop_target() noexcept -> bool
{
    return not_implemented("begin_drag_drop_target", "drag and drop arrives in phase U6");
}

auto
UiFrame::accept_drag_drop_payload(std::string_view /*type*/,
                                  DragDropFlag /*flags*/) noexcept -> std::optional<UiDragDropPayload>
{
    static_cast<void>(not_implemented("accept_drag_drop_payload", "drag and drop arrives in phase U6"));
    return std::nullopt;
}

void
UiFrame::end_drag_drop_target() noexcept
{
    static_cast<void>(not_implemented("end_drag_drop_target", "drag and drop arrives in phase U6"));
}

// ===================================================================================================== U7

auto
UiFrame::begin_table(std::string_view /*id*/,
                     std::uint32_t /*columns*/,
                     TableFlag /*flags*/,
                     Vector2 /*outer_size*/,
                     float /*inner_width*/) noexcept -> bool
{
    return not_implemented("begin_table", "tables arrive in phase U7");
}

void
UiFrame::end_table() noexcept
{
    static_cast<void>(not_implemented("end_table", "tables arrive in phase U7"));
}

void
UiFrame::table_setup_column(std::string_view /*label*/,
                            TableColumnFlag /*flags*/,
                            float /*width_or_weight*/) noexcept
{
    static_cast<void>(not_implemented("table_setup_column", "tables arrive in phase U7"));
}

void
UiFrame::table_setup_scroll_freeze(std::uint32_t /*columns*/,
                                   std::uint32_t /*rows*/) noexcept
{
    static_cast<void>(not_implemented("table_setup_scroll_freeze", "tables arrive in phase U7"));
}

void
UiFrame::table_headers_row() noexcept
{
    static_cast<void>(not_implemented("table_headers_row", "tables arrive in phase U7"));
}

void
UiFrame::table_next_row(TableRowFlag /*flags*/,
                        float /*minimum_height*/) noexcept
{
    static_cast<void>(not_implemented("table_next_row", "tables arrive in phase U7"));
}

auto
UiFrame::table_next_column() noexcept -> bool
{
    return not_implemented("table_next_column", "tables arrive in phase U7");
}

auto
UiFrame::table_set_column_index(std::uint32_t /*column*/) noexcept -> bool
{
    return not_implemented("table_set_column_index", "tables arrive in phase U7");
}

auto
UiFrame::table_sort_specifications() noexcept -> TableSortSpecifications
{
    static_cast<void>(not_implemented("table_sort_specifications", "tables arrive in phase U7"));
    return {};
}

auto
UiFrame::splitter(std::string_view /*id*/,
                  bool /*vertical*/,
                  float /*thickness*/,
                  float& /*first*/,
                  float& /*second*/,
                  float /*first_minimum*/,
                  float /*second_minimum*/) noexcept -> bool
{
    return not_implemented("splitter", "splitters arrive in phase U7");
}

void
UiFrame::plot_lines(std::string_view /*label*/,
                    std::span<const float> /*values*/,
                    const PlotOptions& /*options*/) noexcept
{
    static_cast<void>(not_implemented("plot_lines", "plots arrive in phase U7"));
}

void
UiFrame::plot_histogram(std::string_view /*label*/,
                        std::span<const float> /*values*/,
                        const PlotOptions& /*options*/) noexcept
{
    static_cast<void>(not_implemented("plot_histogram", "plots arrive in phase U7"));
}

void
UiFrame::progress_bar(float /*fraction*/,
                      Vector2 /*size*/,
                      std::string_view /*overlay*/) noexcept
{
    static_cast<void>(not_implemented("progress_bar", "progress bars arrive in phase U7"));
}

void
UiFrame::image(std::uint32_t /*image*/,
               Vector2 /*size*/,
               const UiRectangle& /*texture_coordinates*/,
               Color /*tint*/) noexcept
{
    static_cast<void>(not_implemented("image", "image widgets arrive in phase U7 (DrawList::add_image() draws images now)"));
}

void
UiFrame::image_rounded(std::uint32_t /*image*/,
                       Vector2 /*size*/,
                       float /*rounding*/,
                       const UiRectangle& /*texture_coordinates*/,
                       Color /*tint*/) noexcept
{
    static_cast<void>(not_implemented("image_rounded", "image widgets arrive in phase U7"));
}

auto
UiFrame::image_button(std::string_view /*id*/,
                      std::uint32_t /*image*/,
                      Vector2 /*size*/,
                      const UiRectangle& /*texture_coordinates*/) noexcept -> bool
{
    return not_implemented("image_button", "image widgets arrive in phase U7");
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
