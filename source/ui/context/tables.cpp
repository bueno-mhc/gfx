/// @file tables.cpp
/// @brief UiFrame's tables: begin_table() / end_table(); columns (set-up, sizing policies, widths, resizing by their
///        borders, reordering by their headers, hiding, freezing); rows and cells (a draw channel per column, clipping,
///        indentation); header rows (sorting, the context menu); backgrounds and borders; the table queries; and what
///        the list clipper asks of windows and tables.
module gfx.ui.context;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.handles;
import gfx.ui.draw;
import gfx.ui.style;
import gfx.ui.text;
import :state;

namespace gfx::ui
{

namespace
{

constexpr std::uint32_t NONE = TableState::NONE;

/// Draw channels: row and cell backgrounds then the borders; highlights spanning a row; then a channel per column
/// (and one more per column for the frozen rows, drawn last).
constexpr std::uint32_t BACKGROUND_CHANNEL = 0;
constexpr std::uint32_t SPAN_CHANNEL = 1;
constexpr std::uint32_t FIRST_COLUMN_CHANNEL = 2;

/// Unbounded coordinates (clips that do not limit an axis).
constexpr float FAR = 1.0e9F;

/// The sizing policies.
constexpr TableFlag SIZING_POLICIES = TableFlag::SIZING_FIXED_FIT | TableFlag::SIZING_FIXED_SAME | TableFlag::SIZING_STRETCH_PROPORTIONAL
                                      | TableFlag::SIZING_STRETCH_SAME;

/// Whether @p rectangle contains @p point.
constexpr auto
contains(const UiRectangle& rectangle,
         Vector2 point) noexcept -> bool
{
    return point.x >= rectangle.left && point.x < rectangle.right && point.y >= rectangle.top && point.y < rectangle.bottom;
}

/// Whether column @p column has a fixed width (else it stretches).
auto
is_fixed(const TableColumnState& column) noexcept -> bool
{
    return has_any(column.flags, TableColumnFlag::WIDTH_FIXED);
}

/// Whether @p table's sizing policy makes fixed columns by default.
auto
fixed_policy(const TableState& table) noexcept -> bool
{
    return has_any(table.flags, TableFlag::SIZING_FIXED_FIT | TableFlag::SIZING_FIXED_SAME);
}

/// @p flags with the defaults begin_table() applies.
auto
table_flags_with_defaults(ContextState& state,
                          TableFlag flags,
                          const WindowState& host,
                          std::string_view id) noexcept -> TableFlag
{
    // One sizing policy: the lowest one given, else fixed when the table scrolls sideways (or its window fits its
    // content), stretched otherwise.
    const TableFlag policies = flags & SIZING_POLICIES;
    if (std::popcount(std::to_underlying(policies)) > 1)
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "begin_table", "more than one SIZING_* flag: the first is used", id);
        flags = (flags & ~SIZING_POLICIES) | static_cast<TableFlag>(std::to_underlying(policies) & (~std::to_underlying(policies) + 1U));
    }
    if ((flags & SIZING_POLICIES) == TableFlag::NONE)
    {
        const bool fit = has_any(flags, TableFlag::SCROLL_X) || has_any(host.flags, UiWindowFlag::ALWAYS_AUTO_RESIZE);
        flags = flags | (fit ? TableFlag::SIZING_FIXED_FIT : TableFlag::SIZING_STRETCH_SAME);
    }
    if (has_any(flags, TableFlag::SIZING_FIXED_SAME))
    {
        flags = flags | TableFlag::NO_KEEP_COLUMNS_VISIBLE;
    }
    if (has_any(flags, TableFlag::RESIZABLE))
    {
        flags = flags | TableFlag::BORDERS_INNER_VERTICAL;
    }
    if (has_any(flags, TableFlag::SCROLL_X | TableFlag::SCROLL_Y))
    {
        flags = (flags | TableFlag::NO_HOST_EXTEND_Y) & ~TableFlag::NO_HOST_EXTEND_X;
    }
    return flags;
}

/// @p flags of column @p index with the defaults of @p table.
auto
column_flags_with_defaults(const TableState& table,
                           TableColumnFlag flags,
                           std::uint32_t index) noexcept -> TableColumnFlag
{
    if (has_all(flags, TableColumnFlag::WIDTH_FIXED | TableColumnFlag::WIDTH_STRETCH))
    {
        flags = flags & ~TableColumnFlag::WIDTH_STRETCH;
    }
    if (!has_any(flags, TableColumnFlag::WIDTH_FIXED | TableColumnFlag::WIDTH_STRETCH))
    {
        flags = flags | (fixed_policy(table) ? TableColumnFlag::WIDTH_FIXED : TableColumnFlag::WIDTH_STRETCH);
    }
    if (!has_any(table.flags, TableFlag::RESIZABLE))
    {
        flags = flags | TableColumnFlag::NO_RESIZE;
    }
    if (!has_any(flags, TableColumnFlag::INDENT_ENABLE | TableColumnFlag::INDENT_DISABLE))
    {
        flags = flags | (index == 0 ? TableColumnFlag::INDENT_ENABLE : TableColumnFlag::INDENT_DISABLE);
    }
    if (has_all(flags, TableColumnFlag::NO_SORT_ASCENDING | TableColumnFlag::NO_SORT_DESCENDING))
    {
        flags = flags | TableColumnFlag::NO_SORT;
    }
    return flags;
}

/// The table being submitted, or nullptr (reported for @p operation).
auto
current_table(ContextState& state,
              std::string_view operation) noexcept -> TableState*
{
    if (!state.frame_active)
    {
        state.report(UiFrameProblem::FRAME_NOT_ACTIVE, operation, "call it between UiContext::begin_frame() and end_frame()");
        return nullptr;
    }
    if (state.table_stack.empty())
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, operation, "call it between begin_table() and end_table()");
        return nullptr;
    }
    return &state.tables[state.table_stack.back()];
}

/// The table being submitted, or nullptr (queries: not reported).
auto
current_table_quiet(const ContextState& state) noexcept -> const TableState*
{
    return state.frame_active && !state.table_stack.empty() ? &state.tables[state.table_stack.back()] : nullptr;
}

/// The enabled columns of @p table in display order, through @p visit(column index).
template<class Visitor>
void
for_each_enabled(const TableState& table,
                 Visitor&& visit)
{
    for (const std::uint32_t index : table.display)
    {
        if (table.columns[index].enabled)
        {
            visit(index);
        }
    }
}

/// The first and last enabled columns of @p table in display order (NONE when none is).
auto
enabled_ends(const TableState& table) noexcept -> std::pair<std::uint32_t, std::uint32_t>
{
    std::uint32_t first = NONE;
    std::uint32_t last = NONE;
    for_each_enabled(table, [&](std::uint32_t index)
    {
        first = first == NONE ? index : first;
        last = index;
    });
    return {first, last};
}

/// The enabled column next to @p index in display order (@p direction -1: before it, +1: after it), or NONE.
auto
enabled_neighbor(const TableState& table,
                 std::uint32_t index,
                 std::int32_t direction) noexcept -> std::uint32_t
{
    auto position = static_cast<std::int64_t>(table.columns[index].display_order) + direction;
    for (; position >= 0 && position < static_cast<std::int64_t>(table.display.size()); position += direction)
    {
        const std::uint32_t candidate = table.display[static_cast<std::size_t>(position)];
        if (table.columns[candidate].enabled)
        {
            return candidate;
        }
    }
    return NONE;
}

/// Padding left of column @p index's content (the outer padding for the first enabled column).
auto
left_inset(const TableState& table,
           std::uint32_t index,
           std::uint32_t first) noexcept -> float
{
    return index == first && !table.pad_outer ? 0.0F : table.padding_x;
}

/// Padding right of column @p index's content (the outer padding for the last enabled column).
auto
right_inset(const TableState& table,
            std::uint32_t index,
            std::uint32_t last) noexcept -> float
{
    return index == last && !table.pad_outer ? 0.0F : table.padding_x;
}

// ===================================================================================================== sorting

/// The directions column @p column can be sorted in, in the order clicks cycle through them (NONE last with
/// SORT_TRISTATE).
auto
sort_directions(const TableState& table,
                const TableColumnState& column) noexcept -> std::pair<std::array<SortDirection, 3>, std::size_t>
{
    std::array<SortDirection, 3> directions{};
    std::size_t count = 0;
    const bool descending_first = has_any(column.flags, TableColumnFlag::PREFER_SORT_DESCENDING);
    for (const SortDirection direction : {descending_first ? SortDirection::DESCENDING : SortDirection::ASCENDING,
                                          descending_first ? SortDirection::ASCENDING : SortDirection::DESCENDING})
    {
        const TableColumnFlag forbidden = direction == SortDirection::ASCENDING ? TableColumnFlag::NO_SORT_ASCENDING : TableColumnFlag::NO_SORT_DESCENDING;
        if (!has_any(column.flags, forbidden))
        {
            directions[count++] = direction;
        }
    }
    if (has_any(table.flags, TableFlag::SORT_TRISTATE))
    {
        directions[count++] = SortDirection::NONE;
    }
    return {directions, count};
}

/// The direction a click sorts column @p column in next.
auto
next_sort_direction(const TableState& table,
                    const TableColumnState& column) noexcept -> SortDirection
{
    const auto [directions, count] = sort_directions(table, column);
    if (count == 0)
    {
        return SortDirection::NONE;
    }
    if (column.sort_order < 0)
    {
        return directions[0];
    }
    for (std::size_t direction = 0; direction < count; ++direction)
    {
        if (directions[direction] == column.sort_direction)
        {
            return directions[(direction + 1) % count];
        }
    }
    return directions[0];
}

/// Whether column @p column can be sorted by.
auto
sortable(const TableState& table,
         const TableColumnState& column) noexcept -> bool
{
    return has_any(table.flags, TableFlag::SORTABLE) && !has_any(column.flags, TableColumnFlag::NO_SORT);
}

/// Makes @p table's sort consistent with its flags (ranks from 0, one column without SORT_MULTIPLE, one at least
/// without SORT_TRISTATE, allowed directions only) and rebuilds its specifications; the sort changed when they differ.
void
sanitize_sort(TableState& table) noexcept
{
    const bool tables_sort = has_any(table.flags, TableFlag::SORTABLE);
    std::array<std::uint32_t, MAXIMUM_TABLE_COLUMNS> order{};
    std::uint32_t sorted = 0;
    for (std::uint32_t index = 0; index < table.columns.size(); ++index)
    {
        TableColumnState& column = table.columns[index];
        if (column.sort_order >= 0 && (!tables_sort || !sortable(table, column) || column.sort_direction == SortDirection::NONE))
        {
            column.sort_order = -1;
            column.sort_direction = SortDirection::NONE;
        }
        if (column.sort_order < 0)
        {
            continue;
        }
        const auto [directions, count] = sort_directions(table, column);
        const std::span allowed{directions.data(), count};
        if (std::ranges::find(allowed, column.sort_direction) == allowed.end())
        {
            column.sort_direction = directions[0];
        }
        order[sorted++] = index;
    }

    // By rank (ties: by index), then renumbered; one column only without SORT_MULTIPLE.
    std::ranges::sort(std::span{order.data(), sorted}, [&table](std::uint32_t first, std::uint32_t second)
    {
        const TableColumnState& a = table.columns[first];
        const TableColumnState& b = table.columns[second];
        return a.sort_order != b.sort_order ? a.sort_order < b.sort_order : first < second;
    });
    if (!has_any(table.flags, TableFlag::SORT_MULTIPLE) && sorted > 1)
    {
        for (std::uint32_t rank = 1; rank < sorted; ++rank)
        {
            table.columns[order[rank]].sort_order = -1;
            table.columns[order[rank]].sort_direction = SortDirection::NONE;
        }
        sorted = 1;
    }

    // Sorted by one column at least unless SORT_TRISTATE: the first that can be.
    if (tables_sort && sorted == 0 && !has_any(table.flags, TableFlag::SORT_TRISTATE))
    {
        for (std::uint32_t index = 0; index < table.columns.size(); ++index)
        {
            TableColumnState& column = table.columns[index];
            if (column.enabled && sortable(table, column))
            {
                column.sort_direction = next_sort_direction(table, column);
                order[sorted++] = index;
                break;
            }
        }
    }

    // The specifications, compared with the previous ones.
    bool changed = table.sort.size() != sorted;
    for (std::uint32_t rank = 0; rank < sorted; ++rank)
    {
        TableColumnState& column = table.columns[order[rank]];
        column.sort_order = static_cast<std::int32_t>(rank);
        const TableColumnSortSpecification specification{.column = order[rank], .order = rank, .direction = column.sort_direction};
        if (!changed)
        {
            const TableColumnSortSpecification& previous = table.sort[rank];
            changed = previous.column != specification.column || previous.direction != specification.direction;
        }
        if (changed)
        {
            break;
        }
    }
    if (!changed)
    {
        return;
    }
    table.sort.clear();
    for (std::uint32_t rank = 0; rank < sorted; ++rank)
    {
        TableColumnState& column = table.columns[order[rank]];
        column.sort_order = static_cast<std::int32_t>(rank);
        table.sort.push_back({.column = order[rank], .order = rank, .direction = column.sort_direction});
    }
    table.sort_changed = true;
}

/// A click on column @p index's header: it is sorted in its next direction, alone or (@p add, SORT_MULTIPLE) after the
/// columns already sorted.
void
sort_by_column(TableState& table,
               std::uint32_t index,
               bool add) noexcept
{
    TableColumnState& clicked = table.columns[index];
    const SortDirection direction = next_sort_direction(table, clicked);
    add = add && has_any(table.flags, TableFlag::SORT_MULTIPLE);
    std::int32_t last_rank = -1;
    for (std::uint32_t other = 0; other < table.columns.size(); ++other)
    {
        TableColumnState& column = table.columns[other];
        if (other != index && !add)
        {
            column.sort_order = -1;
            column.sort_direction = SortDirection::NONE;
        }
        last_rank = std::max(last_rank, other != index ? column.sort_order : -1);
    }
    if (clicked.sort_order < 0)
    {
        clicked.sort_order = add ? last_rank + 1 : 0;
    }
    clicked.sort_direction = direction;
    if (direction == SortDirection::NONE)
    {
        clicked.sort_order = -1;
    }
    sanitize_sort(table);
    table.sort_changed = true;
}

// ===================================================================================================== widths

/// The width a fixed column of @p table gets from its content (SIZING_FIXED_SAME: the widest fixed column's).
auto
content_width(const TableState& table,
              const TableColumnState& column) noexcept -> float
{
    if (!has_any(table.flags, TableFlag::SIZING_FIXED_SAME))
    {
        return column.auto_width;
    }
    float widest = 0;
    for (const TableColumnState& other : table.columns)
    {
        if (other.enabled && is_fixed(other))
        {
            widest = std::max(widest, other.auto_width);
        }
    }
    return widest;
}

/// The default weight of stretched column @p column.
auto
default_weight(const TableState& table,
               const TableColumnState& column) noexcept -> float
{
    if (column.initial_width_or_weight > 0)
    {
        return column.initial_width_or_weight;
    }
    return has_any(table.flags, TableFlag::SIZING_STRETCH_PROPORTIONAL) ? std::max(column.auto_width, 1.0F) : 1.0F;
}

/// The widths of @p table's enabled columns and their offsets: fixed columns as asked (or as wide as their content),
/// stretched ones sharing what is left by weight.
void
compute_widths(TableState& table) noexcept
{
    const auto [first, last] = enabled_ends(table);
    float insets = 0;
    float fixed_total = 0;
    float weight_total = 0;
    bool stretches = false;
    for_each_enabled(table, [&](std::uint32_t index)
    {
        TableColumnState& column = table.columns[index];
        insets += left_inset(table, index, first) + right_inset(table, index, last);
        if (is_fixed(column))
        {
            // Following its content while it auto-fits (or always when the user cannot resize it and no width was asked).
            const bool follows = column.auto_fit_frames > 0 || (has_any(column.flags, TableColumnFlag::NO_RESIZE) && column.initial_width_or_weight <= 0);
            if (follows)
            {
                column.width_request = content_width(table, column);
            }
            column.width = std::max(MINIMUM_TABLE_COLUMN_WIDTH, column.width_request >= 0 ? column.width_request : content_width(table, column));
            fixed_total += column.width;
        }
        else
        {
            if (column.auto_fit_frames > 0 && column.initial_width_or_weight <= 0 && has_any(table.flags, TableFlag::SIZING_STRETCH_PROPORTIONAL))
            {
                column.stretch_weight = std::max(column.auto_width, 1.0F);
            }
            if (column.stretch_weight <= 0)
            {
                column.stretch_weight = default_weight(table, column);
            }
            weight_total += column.stretch_weight;
            stretches = true;
        }
    });

    // What stretched columns share: the visible width (or the inner width when it scrolls sideways).
    float width = table.work.width();
    if (has_any(table.flags, TableFlag::SCROLL_X) && table.inner_width > 0)
    {
        width = table.inner_width;
    }
    const float available = std::max(0.0F, width - insets - fixed_total);
    const bool precise = has_any(table.flags, TableFlag::PRECISE_WIDTHS);
    float given = 0;
    if (stretches)
    {
        for_each_enabled(table, [&](std::uint32_t index)
        {
            TableColumnState& column = table.columns[index];
            if (!is_fixed(column))
            {
                const float share = available * column.stretch_weight / std::max(weight_total, 1.0e-6F);
                column.width = std::max(MINIMUM_TABLE_COLUMN_WIDTH, precise ? share : std::floor(share));
                given += column.width;
            }
        });

        // Whole units left by rounding down go one by one to the stretched columns, the last ones first.
        float remaining = precise ? 0.0F : available - given;
        for (auto position = table.display.size(); position-- > 0 && remaining >= 1.0F;)
        {
            TableColumnState& column = table.columns[table.display[position]];
            if (column.enabled && !is_fixed(column))
            {
                column.width += 1.0F;
                remaining -= 1.0F;
            }
        }
    }

    // Offsets in display order.
    float offset = 0;
    for_each_enabled(table, [&](std::uint32_t index)
    {
        TableColumnState& column = table.columns[index];
        column.offset = offset;
        offset += left_inset(table, index, first) + column.width + right_inset(table, index, last);
    });
    table.total_width = offset;
}

/// The screen places and clips of @p table's columns (frozen columns stay in view while it scrolls sideways).
void
place_columns(TableState& table,
              const WindowState& window) noexcept
{
    const auto [first, last] = enabled_ends(table);
    table.origin_x = table.scrolls ? table.work.left - window.scroll.x : table.work.left;
    const bool freezes = has_any(table.flags, TableFlag::SCROLL_X) && table.freeze_columns > 0;
    std::uint32_t frozen = 0;
    table.frozen_right = table.work.left;
    for (const std::uint32_t index : table.display)
    {
        TableColumnState& column = table.columns[index];
        column.frozen = false;
        if (!column.enabled)
        {
            column.minimum_x = column.maximum_x = column.content_left = column.content_right = table.origin_x + column.offset;
            column.clip_left = column.clip_right = column.minimum_x;
            continue;
        }
        column.frozen = freezes && frozen < table.freeze_columns;
        frozen += column.frozen ? 1U : 0U;
        column.minimum_x = (column.frozen ? table.work.left : table.origin_x) + column.offset;
        column.content_left = column.minimum_x + left_inset(table, index, first);
        column.content_right = column.content_left + column.width;
        column.maximum_x = column.content_right + right_inset(table, index, last);
        column.item_width = std::floor(std::max(1.0F, column.width) * DEFAULT_ITEM_WIDTH_FRACTION);
        if (column.frozen)
        {
            table.frozen_right = std::max(table.frozen_right, column.maximum_x);
        }
    }

    // Clips: the column (the whole table without clipping), right of the frozen columns for the others, in view.
    for (TableColumnState& column : table.columns)
    {
        if (!column.enabled)
        {
            continue;
        }
        const bool unclipped = has_any(table.flags, TableFlag::NO_CLIP) || has_any(column.flags, TableColumnFlag::NO_CLIP);
        float left = unclipped ? table.work.left : column.minimum_x;
        float right = unclipped ? table.work.right : column.maximum_x;
        if (!column.frozen)
        {
            left = std::max(left, table.frozen_right);
        }
        left = std::max(left, table.region_clip.left);
        right = std::min(right, table.region_clip.right);
        column.clip_left = left;
        column.clip_right = std::max(left, right);
    }
}

/// The widest column @p index of @p table may become: the others after it keep their minimum in view (unless the
/// table scrolls sideways or NO_KEEP_COLUMNS_VISIBLE).
auto
maximum_width(const TableState& table,
              std::uint32_t index) noexcept -> float
{
    if (has_any(table.flags, TableFlag::SCROLL_X | TableFlag::NO_KEEP_COLUMNS_VISIBLE))
    {
        return FAR;
    }
    const auto [first, last] = enabled_ends(table);
    float after = 0;
    for (std::uint32_t other = enabled_neighbor(table, index, 1); other != NONE; other = enabled_neighbor(table, other, 1))
    {
        after += MINIMUM_TABLE_COLUMN_WIDTH + left_inset(table, other, first) + right_inset(table, other, last);
    }
    const TableColumnState& column = table.columns[index];
    return std::max(MINIMUM_TABLE_COLUMN_WIDTH, table.work.right - column.content_left - right_inset(table, index, last) - after);
}

/// Stretched columns' weights from their widths (after a resize), their total weight kept.
void
weights_from_widths(TableState& table) noexcept
{
    float weights = 0;
    float widths = 0;
    for (const TableColumnState& column : table.columns)
    {
        if (column.enabled && !is_fixed(column))
        {
            weights += column.stretch_weight;
            widths += column.width;
        }
    }
    if (widths <= 0)
    {
        return;
    }
    for (TableColumnState& column : table.columns)
    {
        if (column.enabled && !is_fixed(column))
        {
            column.stretch_weight = column.width / widths * weights;
        }
    }
}

/// Resizes column @p index of @p table to @p width (Dear ImGui's rules): a fixed column with no stretched column
/// before it changes alone; otherwise the next column (the previous one for the last) gives or takes the difference,
/// and stretched columns' weights follow their new widths.
void
resize_column(TableState& table,
              std::uint32_t index,
              float width) noexcept
{
    TableColumnState& column = table.columns[index];
    width = std::clamp(width, MINIMUM_TABLE_COLUMN_WIDTH, maximum_width(table, index));
    column.auto_fit_frames = 0;
    bool stretched_before = false;
    for (const std::uint32_t other : table.display)
    {
        if (other == index)
        {
            break;
        }
        stretched_before = stretched_before || (table.columns[other].enabled && !is_fixed(table.columns[other]));
    }
    const std::uint32_t next_index = enabled_neighbor(table, index, 1);
    if (is_fixed(column) && (!stretched_before || next_index == NONE))
    {
        column.width_request = width;
        return;
    }
    const std::uint32_t partner_index = next_index != NONE ? next_index : enabled_neighbor(table, index, -1);
    if (partner_index == NONE)
    {
        return;
    }

    // The two widths keep their sum.
    TableColumnState& partner = table.columns[partner_index];
    partner.auto_fit_frames = 0;
    const float partner_width = std::max(partner.width - (width - column.width), MINIMUM_TABLE_COLUMN_WIDTH);
    const float column_width = column.width + partner.width - partner_width;
    column.width = column_width;
    partner.width = partner_width;
    if (is_fixed(column))
    {
        column.width_request = column_width;
    }
    if (is_fixed(partner))
    {
        partner.width_request = partner_width;
    }
    if (!is_fixed(column) || !is_fixed(partner))
    {
        weights_from_widths(table);
    }
}

/// Column @p index fitted to its content (measured last frame).
void
fit_column(TableState& table,
           std::uint32_t index) noexcept
{
    TableColumnState& column = table.columns[index];
    if (is_fixed(column))
    {
        column.width_request = std::max(MINIMUM_TABLE_COLUMN_WIDTH, content_width(table, column));
        column.auto_fit_frames = 0;
        return;
    }
    resize_column(table, index, content_width(table, column));
}

/// Every column back to its default size (from the next frame): asked widths and weights, else its content's.
void
reset_column_sizes(TableState& table) noexcept
{
    for (TableColumnState& column : table.columns)
    {
        column.width_request = column.initial_width_or_weight > 0 && is_fixed(column) ? column.initial_width_or_weight : -1.0F;
        column.stretch_weight = -1;
        column.auto_fit_frames = column.initial_width_or_weight > 0 ? 0 : 1;
    }
}

/// Moves column @p index one enabled column left (@p direction -1) or right (+1) in the display order, unless that
/// passes a NO_REORDER column or the frozen columns' edge.
void
reorder_column(TableState& table,
               std::uint32_t index,
               std::int32_t direction) noexcept
{
    const std::uint32_t target = enabled_neighbor(table, index, direction);
    if (target == NONE || has_any(table.columns[target].flags, TableColumnFlag::NO_REORDER))
    {
        return;
    }
    const std::uint32_t from = table.columns[index].display_order;
    const std::uint32_t to = table.columns[target].display_order;
    if (has_any(table.flags, TableFlag::SCROLL_X) && table.freeze_columns > 0 && ((from < table.freeze_columns) != (to < table.freeze_columns)))
    {
        return;
    }
    for (std::uint32_t position = std::min(from, to); position <= std::max(from, to); ++position)
    {
        if (has_any(table.columns[table.display[position]].flags, TableColumnFlag::NO_REORDER) && table.display[position] != index)
        {
            return;
        }
    }

    // The column moves to the target's place; those between shift by one.
    auto begin = table.display.begin();
    if (from < to)
    {
        std::rotate(begin + from, begin + from + 1, begin + to + 1);
    }
    else
    {
        std::rotate(begin + to, begin + from, begin + from + 1);
    }
    for (std::uint32_t position = 0; position < table.display.size(); ++position)
    {
        table.columns[table.display[position]].display_order = position;
    }
}

// ===================================================================================================== rows and cells

/// Where row offset @p offset of row @p row is on screen (frozen rows do not scroll).
auto
row_screen_y(const TableState& table,
             std::int32_t row,
             float offset) noexcept -> float
{
    const bool frozen = has_any(table.flags, TableFlag::SCROLL_Y) && row < static_cast<std::int32_t>(table.freeze_rows);
    return (frozen ? table.work.top : table.origin_y) + offset;
}

/// The clip of row @p row's backgrounds (frozen rows: the whole table; the others: below the frozen rows).
auto
row_clip(const TableState& table,
         std::int32_t row) noexcept -> UiRectangle
{
    const bool frozen = has_any(table.flags, TableFlag::SCROLL_Y) && row < static_cast<std::int32_t>(table.freeze_rows);
    return frozen ? table.region_clip : table.body_clip;
}

/// The draw channel of column @p index's cells in row @p row.
auto
column_channel(const TableState& table,
               std::uint32_t index,
               std::int32_t row) noexcept -> std::uint32_t
{
    const bool frozen = has_any(table.flags, TableFlag::SCROLL_Y) && table.freeze_rows > 0 && row < static_cast<std::int32_t>(table.freeze_rows);
    return FIRST_COLUMN_CHANNEL + index + (frozen ? static_cast<std::uint32_t>(table.columns.size()) : 0U);
}

/// The window table @p table's cells are in.
auto
table_window(ContextState& state,
             const TableState& table) noexcept -> WindowState&
{
    return state.windows[table.window];
}

/// Ends the cell being filled: its content's width and height are measured, its clip popped, indentation kept.
void
end_cell(ContextState& state,
         TableState& table) noexcept
{
    if (!table.cell_open)
    {
        return;
    }
    WindowState& window = table_window(state, table);
    TableColumnState& column = table.columns[static_cast<std::size_t>(table.column)];
    if (column.enabled)
    {
        const bool header = has_any(table.row_flags, TableRowFlag::HEADERS);
        if (!(header && has_any(column.flags, TableColumnFlag::NO_HEADER_WIDTH)))
        {
            column.measured_width = std::max(column.measured_width, window.cursor_maximum.x - column.content_left);
            column.measured = true;
        }
        table.row_content_bottom = std::max(table.row_content_bottom, window.cursor_maximum.y);
    }
    if (table.background_pushed)
    {
        state.pop_table_background(window);
    }
    state.list_of(window).pop_clip_rectangle();

    // Indentation changed in the cell (a tree node) carries to the next rows' INDENT_ENABLE columns.
    table.indent_offset += window.indent - table.cell_indent_start;
    window.indent = table.base_indent + table.indent_offset;
    table.cell_indent_start = window.indent;
    window.cursor.x = window.cursor_start.x + window.indent;
    window.clip = table.base_clip;
    window.skip_items = false;
    window.item_width = table.base_item_width;
    window.table = ContextState::NO_WINDOW;
    table.cell_open = false;
}

/// Starts the cell of column @p index in the open row: its channel and clip, the cursor at its content's start.
void
begin_cell(ContextState& state,
           TableState& table,
           std::uint32_t index) noexcept
{
    end_cell(state, table);
    WindowState& window = table_window(state, table);
    TableColumnState& column = table.columns[index];
    table.column = static_cast<std::int32_t>(index);
    table.cell_open = true;
    window.table = static_cast<std::uint32_t>(&table - state.tables.data());

    // Drawn into the column's channel, clipped to the column (nothing while measuring or when hidden).
    DrawList& list = state.list_of(window);
    table.splitter.set_channel(list, column_channel(table, index, table.row));
    const UiRectangle region = row_clip(table, table.row);
    UiRectangle clip{.left = column.clip_left, .top = region.top, .right = column.clip_right, .bottom = region.bottom};
    if (table.measuring || !column.enabled)
    {
        clip = {};
    }
    window.clip = clip;
    list.push_clip_rectangle(clip);

    // The cursor at the content's start; the table's indentation in INDENT_ENABLE columns.
    table.indent_offset += window.indent - table.cell_indent_start;
    const bool indented = has_any(column.flags, TableColumnFlag::INDENT_ENABLE);
    window.indent = column.content_left - window.cursor_start.x + (indented ? table.indent_offset : 0.0F);
    table.cell_indent_start = window.indent;
    window.cursor = {.x = window.cursor_start.x + window.indent, .y = table.row_screen_top + table.padding_y};
    window.cursor_maximum = window.cursor;
    window.cursor_previous_line = window.cursor;
    window.line_height = 0;
    window.previous_line_height = 0;
    window.line_text_offset = 0;
    window.previous_line_text_offset = 0;
    window.item_width = column.item_width;
    window.skip_items = !column.enabled;
}

/// Ends the open row: its height from its cells, its backgrounds and its top border drawn; the next row's place.
void
end_row(ContextState& state,
        TableState& table) noexcept
{
    if (!table.row_open)
    {
        return;
    }
    end_cell(state, table);
    WindowState& window = table_window(state, table);
    const bool header = has_any(table.row_flags, TableRowFlag::HEADERS);
    const float height = std::max(table.row_minimum_height, table.row_content_bottom + table.padding_y - table.row_screen_top);
    const float top = table.row_screen_top;
    const float bottom = top + height;

    // Backgrounds and the border above the row, under the cells (channel 0).
    if (!table.measuring)
    {
        DrawList& list = state.list_of(window);
        table.splitter.set_channel(list, BACKGROUND_CHANNEL);
        list.push_clip_rectangle(row_clip(table, table.row));
        const UiRectangle row{.left = table.work.left, .top = top, .right = table.work.right, .bottom = bottom};
        Color first{};
        if (table.row_color_set[0])
        {
            first = table.row_colors[0];
        }
        else if (header)
        {
            first = state.color(StyleColor::TABLE_HEADER_BACKGROUND);
        }
        else if (has_any(table.flags, TableFlag::ROW_BACKGROUND))
        {
            first = state.color((table.body_row & 1) != 0 ? StyleColor::TABLE_ROW_BACKGROUND_ALTERNATE : StyleColor::TABLE_ROW_BACKGROUND);
        }
        list.add_rectangle_filled(row, first);
        if (table.row_color_set[1])
        {
            list.add_rectangle_filled(row, table.row_colors[1]);
        }
        for (const TableCellBackground& cell : table.cell_backgrounds)
        {
            const TableColumnState& column = table.columns[cell.column];
            if (column.enabled)
            {
                list.add_rectangle_filled({.left = std::max(column.minimum_x, column.clip_left), .top = top, .right = std::min(column.maximum_x, column.clip_right),
                                           .bottom = bottom},
                                          cell.color);
            }
        }
        if (has_any(table.flags, TableFlag::BORDERS_INNER_HORIZONTAL) && table.row > 0)
        {
            const StyleColor border = table.previous_row_header ? StyleColor::TABLE_BORDER_STRONG : StyleColor::TABLE_BORDER_LIGHT;
            list.add_rectangle_filled({.left = table.outer.left, .top = top, .right = table.outer.right, .bottom = top + 1.0F}, state.color(border));
        }
        list.pop_clip_rectangle();
    }

    // The next row; after the last frozen row, the others are clipped below it.
    table.next_row_top = table.row_top + height;
    if (header)
    {
        table.header_bottom = bottom;
    }
    if (has_any(table.flags, TableFlag::SCROLL_Y) && table.row + 1 == static_cast<std::int32_t>(table.freeze_rows))
    {
        table.frozen_bottom = table.next_row_top;
        table.body_clip.top = std::min(std::max(table.region_clip.top, table.work.top + table.frozen_bottom), table.region_clip.bottom);
        if (!table.measuring && window.scroll.y > 0 && !has_any(table.flags, TableFlag::BORDERS_INNER_HORIZONTAL))
        {
            DrawList& list = state.list_of(window);
            list.push_clip_rectangle(table.region_clip);
            list.add_rectangle_filled({.left = table.outer.left, .top = bottom - 1.0F, .right = table.outer.right, .bottom = bottom},
                                      state.color(StyleColor::TABLE_BORDER_STRONG));
            list.pop_clip_rectangle();
        }
    }
    window.cursor = {.x = window.cursor_start.x + window.indent, .y = bottom};
    window.cursor_maximum.y = std::max(window.cursor_maximum.y, bottom);
    table.previous_row_header = header;
    table.row_open = false;
}

/// Starts row @p flags of @p table at least @p minimum_height tall.
void
begin_row(ContextState& state,
          TableState& table,
          TableRowFlag flags,
          float minimum_height) noexcept
{
    end_row(state, table);
    table.row += 1;
    table.row_flags = flags;
    if (!has_any(flags, TableRowFlag::HEADERS))
    {
        table.body_row += 1;
    }
    table.row_open = true;
    table.row_top = table.next_row_top;
    table.row_screen_top = row_screen_y(table, table.row, table.row_top);
    table.row_minimum_height = std::max(minimum_height, 0.0F);
    table.row_content_bottom = table.row_screen_top + table.padding_y;
    table.row_color_set = {};
    table.cell_backgrounds.clear();
    table.column = -1;
}

/// The hit area of column @p index's border (resizing), from last frame's height.
auto
border_rectangle(const TableState& table,
                 const TableColumnState& column) noexcept -> UiRectangle
{
    const bool header_only = has_any(table.flags, TableFlag::NO_BORDERS_IN_BODY) && table.last_header_height > 0;
    const float height = header_only ? table.last_header_height : std::max(table.last_outer_height, table.last_header_height);
    return {.left = column.maximum_x - RESIZE_HANDLE_HALF_WIDTH,
            .top = table.outer.top,
            .right = column.maximum_x + RESIZE_HANDLE_HALF_WIDTH,
            .bottom = table.outer.top + height};
}

/// The identifier of column @p index's border.
auto
border_identifier(const TableState& table,
                  std::uint32_t index) noexcept -> std::uint32_t
{
    const std::uint32_t id = table.id + 1U + index;
    return id != 0 ? id : 1U;
}

/// Whether column @p index's border can be dragged (resizable, and not the last column's when one stretches: that
/// border is the table's edge).
auto
resizable_border(const TableState& table,
                 std::uint32_t index,
                 std::uint32_t last,
                 bool stretches) noexcept -> bool
{
    const TableColumnState& column = table.columns[index];
    if (!column.enabled || has_any(column.flags, TableColumnFlag::NO_RESIZE))
    {
        return false;
    }
    return !(index == last && stretches);
}

/// The borders' mouse behavior: dragging one resizes its column, a double click fits it to its content. Before the
/// cells, so a press on a border is the border's.
auto
update_borders(ContextState& state,
               TableState& table,
               WindowState& window) noexcept -> bool
{
    table.resized_column = NONE;
    if (!has_any(table.flags, TableFlag::RESIZABLE) || table.measuring)
    {
        return false;
    }
    const auto [first, last] = enabled_ends(table);
    bool stretches = false;
    for_each_enabled(table, [&](std::uint32_t index) { stretches = stretches || !is_fixed(table.columns[index]); });
    const LastItem saved_item = state.last_item;
    bool changed = false;
    for (const std::uint32_t index : table.display)
    {
        if (!resizable_border(table, index, last, stretches))
        {
            continue;
        }
        const TableColumnState& column = table.columns[index];
        const UiRectangle hit = border_rectangle(table, column).intersection(table.region_clip);
        if (hit.empty())
        {
            continue;
        }
        const std::uint32_t id = border_identifier(table, index);
        const ButtonState button = state.button_behavior(window, hit, id);
        if (state.active_id == id && state.mouse[0].clicked)
        {
            state.active_offset = {.x = column.maximum_x - state.mouse_position.x, .y = 0.0F};
        }
        if (button.hovered || button.held)
        {
            state.cursor = CursorShape::RESIZE_EAST_WEST;
        }
        if (button.hovered && state.mouse[0].double_clicked)
        {
            fit_column(table, index);
            changed = true;
        }
        else if (button.held)
        {
            table.resized_column = index;
            const float right = state.mouse_position.x + state.active_offset.x;
            const float width = right - right_inset(table, index, last) - column.content_left;
            if (width != column.width)
            {
                resize_column(table, index, width);
                changed = true;
            }
        }
    }
    state.last_item = saved_item;
    return changed;
}

/// Lays out @p table's columns once its set-up is known (the first row or cell of the frame).
void
layout_table(ContextState& state,
             TableState& table) noexcept
{
    if (table.layout_done)
    {
        return;
    }
    table.layout_done = true;
    WindowState& window = table_window(state, table);

    // Columns without set-up get the defaults; new columns their initial state.
    for (std::uint32_t index = 0; index < table.columns.size(); ++index)
    {
        TableColumnState& column = table.columns[index];
        if (index >= table.setup_columns)
        {
            column.label = {};
            column.flags = column_flags_with_defaults(table, TableColumnFlag::NONE, index);
            column.initial_width_or_weight = 0;
        }
        if (!column.initialized)
        {
            column.initialized = true;
            column.user_enabled = !(has_any(table.flags, TableFlag::HIDEABLE) && has_any(column.flags, TableColumnFlag::DEFAULT_HIDE));
            column.width_request = is_fixed(column) && column.initial_width_or_weight > 0 ? column.initial_width_or_weight : -1.0F;
            column.stretch_weight = !is_fixed(column) && column.initial_width_or_weight > 0 ? column.initial_width_or_weight : -1.0F;
            const bool follows = column.initial_width_or_weight <= 0 && (is_fixed(column) || has_any(table.flags, TableFlag::SIZING_STRETCH_PROPORTIONAL));
            column.auto_fit_frames = follows ? 2 : 0;
            if (has_any(column.flags, TableColumnFlag::DEFAULT_SORT) && sortable(table, column) && column.sort_order < 0)
            {
                std::int32_t rank = -1;
                for (const TableColumnState& other : table.columns)
                {
                    rank = std::max(rank, other.sort_order);
                }
                column.sort_order = rank + 1;
                column.sort_direction = sort_directions(table, column).first[0];
            }
        }
        if (!has_any(table.flags, TableFlag::HIDEABLE) || has_any(column.flags, TableColumnFlag::NO_HIDE))
        {
            column.user_enabled = true;
        }
        column.enabled = column.user_enabled && !has_any(column.flags, TableColumnFlag::DISABLED);
        column.measured = false;
        column.measured_width = 0;
    }

    // At least one column shown.
    if (enabled_ends(table).first == NONE)
    {
        for (const std::uint32_t index : table.display)
        {
            TableColumnState& column = table.columns[index];
            if (!has_any(column.flags, TableColumnFlag::DISABLED))
            {
                column.user_enabled = column.enabled = true;
                break;
            }
        }
    }

    // A header dragged past its neighbor last frame moves; the sort follows the flags.
    if (table.reorder_column != NONE && table.reorder_direction != 0 && table.reorder_column < table.columns.size())
    {
        reorder_column(table, table.reorder_column, table.reorder_direction);
    }
    table.reorder_column = NONE;
    table.reorder_direction = 0;
    sanitize_sort(table);
    table.sort_initialized = true;

    // Widths and places; the borders' dragging may change them (laid out again then).
    compute_widths(table);
    const bool stretches = std::ranges::any_of(table.columns, [](const TableColumnState& column) { return column.enabled && !is_fixed(column); });
    if (!table.scrolls && has_any(table.flags, TableFlag::NO_HOST_EXTEND_X) && !stretches)
    {
        table.outer.right = std::min(table.outer.right, table.outer.left + table.total_width);
        table.work.right = table.outer.right;
        table.region_clip.right = std::min(table.region_clip.right, table.outer.right);
        table.body_clip.right = table.region_clip.right;
    }
    place_columns(table, window);
    if (update_borders(state, table, window))
    {
        compute_widths(table);
        place_columns(table, window);
    }

    // The column under the mouse (within last frame's height).
    table.hovered_column = NONE;
    const float height = std::max(table.last_outer_height, 1.0F);
    const UiRectangle area = UiRectangle{.left = table.outer.left, .top = table.outer.top, .right = table.outer.right, .bottom = table.outer.top + height}
                                 .intersection(table.region_clip);
    const auto window_index = static_cast<std::uint32_t>(&window - state.windows.data());
    if (state.mouse_present && state.hovered_window == window_index && contains(area, state.mouse_position))
    {
        for_each_enabled(table, [&](std::uint32_t index)
        {
            const TableColumnState& column = table.columns[index];
            if (state.mouse_position.x >= column.clip_left && state.mouse_position.x < column.clip_right)
            {
                table.hovered_column = index;
            }
        });
    }

    // The draw channels.
    const std::uint32_t row_sets = has_any(table.flags, TableFlag::SCROLL_Y) && table.freeze_rows > 0 ? 2U : 1U;
    try
    {
        table.splitter.split(state.list_of(window), FIRST_COLUMN_CHANNEL + static_cast<std::uint32_t>(table.columns.size()) * row_sets);
    }
    catch (...)
    {
        state.report(UiFrameProblem::CAPACITY_EXCEEDED, "begin_table", "the table's draw channels could not be made");
    }
}

/// The context menu's popup identifier of @p table.
auto
context_menu_identifier(const TableState& table) noexcept -> std::uint32_t
{
    return ContextState::hash(table.id, "##context_menu");
}

/// Opens @p table's context menu for column @p column (NONE: none in particular).
void
open_context_menu(ContextState& state,
                  TableState& table,
                  std::uint32_t column) noexcept
{
    if (!has_any(table.flags, TableFlag::RESIZABLE | TableFlag::REORDERABLE | TableFlag::HIDEABLE))
    {
        return;
    }
    if (PopupState* opened = state.open_popup(context_menu_identifier(table), state.mouse_position, 0, 0))
    {
        opened->position = state.mouse_position;
        opened->opened_frame = state.frame;
        table.context_column = column;
    }
}

/// The color of a vertical border after column @p index.
auto
vertical_border_color(const ContextState& state,
                      const TableState& table,
                      std::uint32_t index,
                      bool hovered) noexcept -> Color
{
    if (table.resized_column == index)
    {
        return state.color(StyleColor::SEPARATOR_ACTIVE);
    }
    if (hovered)
    {
        return state.color(StyleColor::SEPARATOR_HOVERED);
    }
    const bool frozen_edge = table.columns[index].frozen && enabled_neighbor(table, index, 1) != NONE && !table.columns[enabled_neighbor(table, index, 1)].frozen;
    const bool header_only = has_any(table.flags, TableFlag::NO_BORDERS_IN_BODY | TableFlag::NO_BORDERS_IN_BODY_UNTIL_RESIZE);
    return state.color(frozen_edge || header_only ? StyleColor::TABLE_BORDER_STRONG : StyleColor::TABLE_BORDER_LIGHT);
}

/// The vertical borders between columns (in the table's background channel, over the rows' backgrounds).
void
draw_inner_borders(ContextState& state,
                   TableState& table,
                   WindowState& window,
                   float bottom) noexcept
{
    if (table.measuring || !has_any(table.flags, TableFlag::BORDERS_INNER_VERTICAL))
    {
        return;
    }
    DrawList& list = state.list_of(window);
    table.splitter.set_channel(list, BACKGROUND_CHANNEL);
    list.push_clip_rectangle(table.region_clip);
    const auto [first, last] = enabled_ends(table);
    bool stretches = false;
    for_each_enabled(table, [&](std::uint32_t index) { stretches = stretches || !is_fixed(table.columns[index]); });
    const bool header_only = has_any(table.flags, TableFlag::NO_BORDERS_IN_BODY)
                             || (has_any(table.flags, TableFlag::NO_BORDERS_IN_BODY_UNTIL_RESIZE) && table.resized_column == NONE);
    const auto window_index = static_cast<std::uint32_t>(&window - state.windows.data());
    for_each_enabled(table, [&](std::uint32_t index)
    {
        // Not after the last column when it reaches the table's edge (the outer border is there).
        const TableColumnState& column = table.columns[index];
        if (index == last && (stretches || column.maximum_x >= table.outer.right - 1.0F))
        {
            return;
        }
        const float y_bottom = header_only && table.header_bottom >= 0 ? table.header_bottom : bottom;
        const float x = std::floor(column.maximum_x) - (index == last ? 1.0F : 0.0F);
        const bool hovered = state.hovered_id == border_identifier(table, index) && state.hovered_window == window_index;
        const UiRectangle line{.left = x, .top = table.outer.top, .right = x + 1.0F, .bottom = y_bottom};
        const UiRectangle visible = column.frozen ? line : line.intersection({.left = table.frozen_right, .top = -FAR, .right = FAR, .bottom = FAR});
        list.add_rectangle_filled(visible, vertical_border_color(state, table, index, hovered));
    });
    list.pop_clip_rectangle();
}

/// The outer borders (after the table's region ends, in its host's clip).
void
draw_outer_borders(ContextState& state,
                   const TableState& table,
                   WindowState& host) noexcept
{
    if (table.measuring || !has_any(table.flags, TableFlag::BORDERS_OUTER))
    {
        return;
    }
    DrawList& list = state.list_of(host);
    const Color strong = state.color(StyleColor::TABLE_BORDER_STRONG);
    const UiRectangle& outer = table.outer;
    if (has_all(table.flags, TableFlag::BORDERS_OUTER))
    {
        list.add_rectangle(outer, strong);
        return;
    }
    if (has_any(table.flags, TableFlag::BORDERS_OUTER_VERTICAL))
    {
        list.add_rectangle_filled({.left = outer.left, .top = outer.top, .right = outer.left + 1.0F, .bottom = outer.bottom}, strong);
        list.add_rectangle_filled({.left = outer.right - 1.0F, .top = outer.top, .right = outer.right, .bottom = outer.bottom}, strong);
    }
    if (has_any(table.flags, TableFlag::BORDERS_OUTER_HORIZONTAL))
    {
        list.add_rectangle_filled({.left = outer.left, .top = outer.top, .right = outer.right, .bottom = outer.top + 1.0F}, strong);
        list.add_rectangle_filled({.left = outer.left, .top = outer.bottom - 1.0F, .right = outer.right, .bottom = outer.bottom}, strong);
    }
}

/// The table the clipper's window @p window is filling (its cells are in that window), or nullptr.
auto
list_table(ContextState& state,
           const WindowState& window) noexcept -> TableState*
{
    if (state.table_stack.empty())
    {
        return nullptr;
    }
    TableState& table = state.tables[state.table_stack.back()];
    return table.window == static_cast<std::uint32_t>(&window - state.windows.data()) ? &table : nullptr;
}

/// The context menu: column sizes, the order, and which columns are shown.
void
draw_context_menu(UiFrame& frame,
                  TableState& table) noexcept
{
    bool separator = false;
    const std::uint32_t column = table.context_column;
    const bool stretches = std::ranges::any_of(table.columns, [](const TableColumnState& entry) { return entry.enabled && !is_fixed(entry); });
    if (has_any(table.flags, TableFlag::RESIZABLE))
    {
        const bool one = column < table.columns.size() && table.columns[column].enabled && !has_any(table.columns[column].flags, TableColumnFlag::NO_RESIZE);
        if (frame.menu_item("Size column to fit###size_one", {}, false, one))
        {
            fit_column(table, column);
        }
        if (frame.menu_item(stretches ? "Size all columns to default###size_all" : "Size all columns to fit###size_all"))
        {
            if (stretches)
            {
                reset_column_sizes(table);
            }
            else
            {
                for_each_enabled(table, [&table](std::uint32_t index) { fit_column(table, index); });
            }
        }
        separator = true;
    }
    if (has_any(table.flags, TableFlag::REORDERABLE))
    {
        bool reordered = false;
        for (std::uint32_t position = 0; position < table.display.size(); ++position)
        {
            reordered = reordered || table.display[position] != position;
        }
        if (frame.menu_item("Reset order###reset_order", {}, false, reordered))
        {
            for (std::uint32_t position = 0; position < table.display.size(); ++position)
            {
                table.display[position] = position;
                table.columns[position].display_order = position;
            }
        }
        separator = true;
    }
    if (has_any(table.flags, TableFlag::HIDEABLE))
    {
        if (separator)
        {
            frame.separator();
        }
        const auto shown = static_cast<std::uint32_t>(std::ranges::count_if(table.columns, [](const TableColumnState& entry) { return entry.enabled; }));
        for (std::uint32_t index = 0; index < table.columns.size(); ++index)
        {
            TableColumnState& entry = table.columns[index];
            if (has_any(entry.flags, TableColumnFlag::DISABLED))
            {
                continue;
            }
            const std::string_view name = ContextState::display_text(entry.label);
            const bool can_change = !has_any(entry.flags, TableColumnFlag::NO_HIDE) && !(entry.user_enabled && shown <= 1);
            frame.push_id(static_cast<std::int32_t>(index));
            if (frame.menu_item(name.empty() ? std::string_view{"(unnamed)"} : name, {}, entry.user_enabled, can_change))
            {
                entry.user_enabled = !entry.user_enabled;
            }
            frame.pop_id();
        }
    }
}

/// Whether column @p index of @p table should get content this frame (shown, and in view or measured).
auto
requests_output(const TableState& table,
                std::uint32_t index) noexcept -> bool
{
    const TableColumnState& column = table.columns[index];
    return column.enabled && (table.measuring || column.clip_right > column.clip_left);
}

} // namespace

// ===================================================================================================== begin and end

auto
UiFrame::begin_table(std::string_view id,
                     std::uint32_t columns,
                     TableFlag flags,
                     Vector2 outer_size,
                     float inner_width) noexcept -> bool
{
    ContextState& state = *m_state;
    WindowState* host_pointer = state.current_window("begin_table");
    if (host_pointer == nullptr || host_pointer->skip_items)
    {
        return false;
    }
    if (columns == 0 || columns > MAXIMUM_TABLE_COLUMNS)
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "begin_table", "a table has between 1 and MAXIMUM_TABLE_COLUMNS columns", id);
        return false;
    }
    const auto host_index = static_cast<std::uint32_t>(host_pointer - state.windows.data());

    // The table of this identifier (made the first time; its columns reset when their count changes).
    const std::uint32_t table_id = state.widget_id(id);
    std::uint32_t index = state.table_lookup.find(table_id).value_or(NONE);
    try
    {
        if (index == NONE)
        {
            const bool grows = state.tables.size() == state.tables.capacity();
            index = static_cast<std::uint32_t>(state.tables.size());
            state.tables.emplace_back();
            state.tables.back().id = table_id;
            if (state.table_lookup.insert(table_id, index) || grows)
            {
                ++state.allocations;
                state.report(UiFrameProblem::CAPACITY_EXCEEDED, "begin_table", "more tables than UiContextLimits::tables: their storage grew", id, table_id);
            }
        }
        TableState& created = state.tables[index];
        if (created.columns.size() != columns)
        {
            created.columns.assign(columns, TableColumnState{});
            created.display.resize(columns);
            for (std::uint32_t column = 0; column < columns; ++column)
            {
                created.display[column] = column;
                created.columns[column].display_order = column;
            }
            created.sort.clear();
            created.sort.reserve(columns);
            created.cell_backgrounds.clear();
            created.cell_backgrounds.reserve(columns);
            created.sort_initialized = false;
            created.sort_changed = true;
        }
    }
    catch (...)
    {
        state.report(UiFrameProblem::CAPACITY_EXCEEDED, "begin_table", "the table could not be made", id, table_id);
        return false;
    }
    if (std::ranges::find(state.table_stack, index) != state.table_stack.end())
    {
        state.report(UiFrameProblem::IDENTIFIER_CONFLICT, "begin_table", "this table is being submitted already: give the inner one another identifier", id,
                     table_id);
        return false;
    }
    TableState& table = state.tables[index];
    WindowState& host = state.windows[host_index];
    const Style& style = state.style;

    // Flags, padding, and its rectangle: the width available unless asked; as tall as its rows unless it scrolls.
    table.flags = table_flags_with_defaults(state, flags, host, id);
    table.measuring = table.last_frame == 0;
    table.scrolls = has_any(table.flags, TableFlag::SCROLL_X | TableFlag::SCROLL_Y);
    table.inner_width = inner_width;
    table.padding_x = has_any(table.flags, TableFlag::NO_PAD_INNER_X) ? 0.0F : style.cell_padding.x;
    table.padding_y = style.cell_padding.y;
    table.pad_outer = has_any(table.flags, TableFlag::NO_PAD_OUTER_X)  ? false
                      : has_any(table.flags, TableFlag::PAD_OUTER_X) ? true
                                                                       : has_any(table.flags, TableFlag::BORDERS_OUTER_VERTICAL);
    const Vector2 available{.x = std::max(1.0F, state.content_right(host) - host.cursor.x), .y = std::max(1.0F, state.content_bottom(host) - host.cursor.y)};
    const auto extent = [](float asked, float room) { return asked > 0 ? asked : asked < 0 ? std::max(1.0F, room + asked) : room; };
    const float width = extent(outer_size.x, available.x);
    const float height = table.scrolls ? extent(outer_size.y, available.y) : std::max(outer_size.y, 0.0F);
    table.minimum_height = height;
    table.outer = {.left = host.cursor.x, .top = host.cursor.y, .right = host.cursor.x + width, .bottom = host.cursor.y + height};
    table.host_window = host_index;

    // The host's layout, restored by end_table().
    table.saved_cursor = host.cursor;
    table.saved_cursor_maximum = host.cursor_maximum;
    table.saved_cursor_previous_line = host.cursor_previous_line;
    table.saved_line_height = host.line_height;
    table.saved_previous_line_height = host.previous_line_height;
    table.saved_line_text_offset = host.line_text_offset;
    table.saved_indent = host.indent;
    table.saved_item_width = host.item_width;
    table.saved_clip = host.clip;
    table.identifier_depth = static_cast<std::uint32_t>(state.id_stack.size());
    try
    {
        state.table_stack.push_back(index);
    }
    catch (...)
    {
        state.report(UiFrameProblem::CAPACITY_EXCEEDED, "begin_table", "the table stack could not grow", id, table_id);
        return false;
    }
    state.id_stack.push_back(table.id);
    state.accessible_push(state.accessible_node((static_cast<AccessibilityNodeId>(host.id) << 32) | table.id, AccessibilityRole::TABLE, table.outer,
                                                ContextState::display_text(id)));

    // Its rows in a child region of its rectangle when it scrolls; else in the window, from its top.
    if (table.scrolls)
    {
        const UiWindowFlag child_flags = has_any(table.flags, TableFlag::SCROLL_X) ? UiWindowFlag::HORIZONTAL_SCROLLBAR : UiWindowFlag::NONE;
        const bool visible = begin_child("##table", {.x = width, .y = height}, ChildFlag::NONE, child_flags | UiWindowFlag::NO_SAVED_SETTINGS);
        const std::uint32_t child_index = state.window_stack.back();
        if (!visible)
        {
            state.table_stack.pop_back();
            end_child();
            state.accessible_pop();
            state.id_stack.resize(table.identifier_depth);
            return false;
        }
        const WindowState& child = state.windows[child_index];
        table.window = child_index;
        table.work = child.inner;
        table.region_clip = child.clip;
        table.origin_y = child.cursor_start.y;
    }
    else
    {
        const float bottom = has_any(table.flags, TableFlag::NO_HOST_EXTEND_Y) && height > 0 ? table.outer.top + height : FAR;
        table.window = host_index;
        table.work = {.left = table.outer.left, .top = table.outer.top, .right = table.outer.right, .bottom = bottom};
        table.region_clip = table.work.intersection(host.clip);
        table.origin_y = table.outer.top;
    }

    // The frame's set-up and rows start empty.
    const WindowState& cells = state.windows[table.window];
    table.body_clip = table.region_clip;
    table.freeze_columns = 0;
    table.freeze_rows = 0;
    table.setup_columns = 0;
    table.layout_done = false;
    table.row = -1;
    table.body_row = -1;
    table.row_open = false;
    table.row_flags = TableRowFlag::NONE;
    table.previous_row_header = false;
    table.next_row_top = 0;
    table.frozen_bottom = -1;
    table.header_bottom = -1;
    table.column = -1;
    table.cell_open = false;
    table.indent_offset = 0;
    table.base_indent = cells.indent;
    table.base_item_width = cells.item_width;
    table.base_clip = cells.clip;
    table.cell_indent_start = cells.indent;
    table.background_pushed = false;
    table.hovered_column = NONE;
    table.resized_column = NONE;
    return true;
}

void
UiFrame::end_table() noexcept
{
    ContextState& state = *m_state;
    if (!state.frame_active || state.table_stack.empty())
    {
        state.report(state.frame_active ? UiFrameProblem::END_WITHOUT_BEGIN : UiFrameProblem::FRAME_NOT_ACTIVE, "end_table", "end_table() without begin_table()");
        return;
    }
    TableState& table = state.tables[state.table_stack.back()];
    layout_table(state, table);
    end_row(state, table);
    WindowState& window = table_window(state, table);

    // Its height: its rows' (at least the height asked), or its region's when it scrolls.
    const float rows_bottom = table.origin_y + table.next_row_top;
    if (!table.scrolls)
    {
        table.outer.bottom = has_any(table.flags, TableFlag::NO_HOST_EXTEND_Y) && table.minimum_height > 0
                                 ? table.outer.top + table.minimum_height
                                 : std::max(table.outer.top + table.minimum_height, rows_bottom);
    }
    const float bottom = table.scrolls ? table.work.bottom : table.outer.bottom;
    draw_inner_borders(state, table, window, bottom);
    try
    {
        table.splitter.merge(state.list_of(window));
    }
    catch (...)
    {
        state.report(UiFrameProblem::CAPACITY_EXCEEDED, "end_table", "the table's drawing could not be merged");
    }

    // What its columns' content measured is their content width next frame; this frame's heights for the borders.
    for (TableColumnState& column : table.columns)
    {
        if (column.measured)
        {
            column.auto_width = column.measured_width;
        }
        if (column.auto_fit_frames > 0)
        {
            --column.auto_fit_frames;
        }
    }
    table.last_outer_height = bottom - table.outer.top;
    table.last_header_height = table.header_bottom >= 0 ? table.header_bottom - table.outer.top : 0.0F;
    table.last_frame = state.frame;

    // A right click in its body opens the context menu (CONTEXT_MENU_IN_BODY).
    const auto window_index = static_cast<std::uint32_t>(&window - state.windows.data());
    const UiRectangle body = UiRectangle{.left = table.outer.left, .top = table.outer.top, .right = table.outer.right, .bottom = bottom}.intersection(table.region_clip);
    if (has_any(table.flags, TableFlag::CONTEXT_MENU_IN_BODY) && state.mouse[std::to_underlying(MouseButton::RIGHT)].released && state.mouse_present
        && state.hovered_window == window_index && contains(body, state.mouse_position) && !state.popup_open(context_menu_identifier(table)))
    {
        open_context_menu(state, table, table.hovered_column);
    }

    // The cells' window gets its own layout back; the table is an item of its host.
    state.table_stack.pop_back();
    state.accessible_pop();
    window.table = ContextState::NO_WINDOW;
    window.indent = table.base_indent;
    window.item_width = table.base_item_width;
    window.clip = table.base_clip;
    window.skip_items = false;
    if (table.scrolls)
    {
        window.cursor_maximum = {.x = window.cursor_start.x + table.total_width, .y = std::max(rows_bottom, window.cursor_start.y)};
        end_child();
    }
    if (state.id_stack.size() > std::size_t{table.identifier_depth} + 1)
    {
        state.report(UiFrameProblem::UNBALANCED_STACK, "end_table", "push_id() without pop_id() in this table");
    }
    state.id_stack.resize(std::min<std::size_t>(state.id_stack.size(), table.identifier_depth));
    WindowState& host = state.windows[table.host_window];
    if (!table.scrolls)
    {
        host.cursor = table.saved_cursor;
        host.cursor_maximum = table.saved_cursor_maximum;
        host.cursor_previous_line = table.saved_cursor_previous_line;
        host.line_height = table.saved_line_height;
        host.previous_line_height = table.saved_previous_line_height;
        host.line_text_offset = table.saved_line_text_offset;
        host.indent = table.saved_indent;
        host.item_width = table.saved_item_width;
        host.clip = table.saved_clip;
        state.item_size(host, {.x = table.outer.width(), .y = table.outer.height()});
        static_cast<void>(state.item_add(host, table.outer, 0, false, "end_table", {}));
    }
    draw_outer_borders(state, table, host);

    // The context menu, in its own window.
    const std::uint32_t popup = context_menu_identifier(table);
    if (state.popup_open(popup))
    {
        if (state.begin_popup(*this, popup, UiWindowFlag::NONE))
        {
            draw_context_menu(*this, table);
            end_popup();
        }
    }
    else
    {
        table.context_column = NONE;
    }
}

// ===================================================================================================== set-up

void
UiFrame::table_setup_column(std::string_view label,
                            TableColumnFlag flags,
                            float width_or_weight) noexcept
{
    ContextState& state = *m_state;
    TableState* table = current_table(state, "table_setup_column");
    if (table == nullptr)
    {
        return;
    }
    if (table->layout_done)
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "table_setup_column", "call it before the table's first row", label);
        return;
    }
    if (table->setup_columns >= table->columns.size())
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "table_setup_column", "more table_setup_column() calls than the table has columns", label);
        return;
    }

    // The label is kept for the frame (the header row and the context menu draw it later).
    const std::uint32_t index = table->setup_columns++;
    TableColumnState& column = table->columns[index];
    column.label = {};
    if (!label.empty())
    {
        const std::span<char> copy = arena_characters(label.size());
        if (!copy.empty())
        {
            std::ranges::copy(label, copy.begin());
            column.label = {copy.data(), copy.size()};
        }
    }
    column.flags = column_flags_with_defaults(*table, flags, index);
    column.initial_width_or_weight = std::max(width_or_weight, 0.0F);
}

void
UiFrame::table_setup_scroll_freeze(std::uint32_t columns,
                                   std::uint32_t rows) noexcept
{
    ContextState& state = *m_state;
    TableState* table = current_table(state, "table_setup_scroll_freeze");
    if (table == nullptr)
    {
        return;
    }
    if (table->layout_done)
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "table_setup_scroll_freeze", "call it before the table's first row");
        return;
    }
    table->freeze_columns = has_any(table->flags, TableFlag::SCROLL_X) ? std::min(columns, static_cast<std::uint32_t>(table->columns.size())) : 0U;
    table->freeze_rows = has_any(table->flags, TableFlag::SCROLL_Y) ? rows : 0U;
}

// ===================================================================================================== rows and cells

void
UiFrame::table_next_row(TableRowFlag flags,
                        float minimum_height) noexcept
{
    ContextState& state = *m_state;
    TableState* table = current_table(state, "table_next_row");
    if (table == nullptr)
    {
        return;
    }
    layout_table(state, *table);
    begin_row(state, *table, flags, minimum_height);
}

auto
UiFrame::table_next_column() noexcept -> bool
{
    ContextState& state = *m_state;
    TableState* table = current_table(state, "table_next_column");
    if (table == nullptr)
    {
        return false;
    }

    // The next cell of the row; after its last cell, the first cell of a new row.
    layout_table(state, *table);
    std::uint32_t column = 0;
    if (table->row_open && table->column + 1 < static_cast<std::int32_t>(table->columns.size()))
    {
        column = static_cast<std::uint32_t>(table->column + 1);
    }
    else
    {
        begin_row(state, *table, TableRowFlag::NONE, 0.0F);
    }
    begin_cell(state, *table, column);
    return requests_output(*table, column);
}

auto
UiFrame::table_set_column_index(std::uint32_t column) noexcept -> bool
{
    ContextState& state = *m_state;
    TableState* table = current_table(state, "table_set_column_index");
    if (table == nullptr)
    {
        return false;
    }
    if (column >= table->columns.size())
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "table_set_column_index", "the table has no such column");
        return false;
    }
    layout_table(state, *table);
    if (!table->row_open)
    {
        begin_row(state, *table, TableRowFlag::NONE, 0.0F);
    }
    if (!table->cell_open || table->column != static_cast<std::int32_t>(column))
    {
        begin_cell(state, *table, column);
    }
    return requests_output(*table, column);
}

void
UiFrame::table_headers_row() noexcept
{
    ContextState& state = *m_state;
    TableState* table_pointer = current_table(state, "table_headers_row");
    if (table_pointer == nullptr)
    {
        return;
    }

    // A row a line of text tall (with the cell padding), a header in each shown column.
    TableState& table = *table_pointer;
    layout_table(state, table);
    const float height = state.current_font().line_height + table.padding_y * 2;
    begin_row(state, table, TableRowFlag::HEADERS, height);
    const float top = table.row_screen_top;
    for (std::uint32_t column = 0; column < table.columns.size(); ++column)
    {
        if (table_set_column_index(column))
        {
            table_header(column);
        }
    }

    // A right click on the row right of the last column opens the context menu too.
    const WindowState& window = table_window(state, table);
    const auto window_index = static_cast<std::uint32_t>(&window - state.windows.data());
    const std::uint32_t last = enabled_ends(table).second;
    const float right = last != NONE ? table.columns[last].maximum_x : table.work.left;
    const UiRectangle rest = UiRectangle{.left = right, .top = top, .right = table.work.right, .bottom = top + height}.intersection(row_clip(table, table.row));
    if (state.mouse[std::to_underlying(MouseButton::RIGHT)].released && state.mouse_present && state.hovered_window == window_index
        && contains(rest, state.mouse_position))
    {
        open_context_menu(state, table, NONE);
    }
}

void
UiFrame::table_header(std::uint32_t index) noexcept
{
    ContextState& state = *m_state;
    TableState& table = state.tables[state.table_stack.back()];
    WindowState& window = table_window(state, table);
    TableColumnState& column = table.columns[index];
    const Style& style = state.style;

    // Identified by its label under its column's index (unnamed columns differ).
    push_id(static_cast<std::int32_t>(index));
    const std::uint32_t id = state.widget_id(column.label);
    pop_id();

    // Its label, its sort arrow (room kept in every sortable column) and its rank in a sort by several columns.
    const std::string_view label = has_any(column.flags, TableColumnFlag::NO_HEADER_LABEL) ? std::string_view{} : ContextState::display_text(column.label);
    const TextLayout text = state.layout_text(label, 0, "table_headers_row");
    const float line = state.current_font().line_height;
    const bool can_sort = sortable(table, column);
    const float arrow = can_sort ? std::floor(line * 0.65F) : 0.0F;
    const float arrow_space = can_sort ? arrow + style.item_inner_spacing.x : 0.0F;
    const bool ranked = can_sort && column.sort_order >= 0 && table.sort.size() > 1;
    const std::string_view rank = ranked ? format_text("{}", column.sort_order + 1) : std::string_view{};
    const TextLayout rank_text = state.layout_text(rank, 0, "table_headers_row");
    const float rank_space = ranked ? rank_text.width + style.item_inner_spacing.x : 0.0F;
    const Vector2 position = window.cursor;
    const UiRectangle cell{.left = column.minimum_x,
                           .top = table.row_screen_top,
                           .right = column.maximum_x,
                           .bottom = table.row_screen_top + std::max(table.row_minimum_height, line + table.padding_y * 2)};
    state.item_size(window, {.x = text.width + arrow_space + rank_space, .y = line});
    if (!state.item_add(window, cell, id, true, "table_headers_row", column.label))
    {
        return;
    }
    const ButtonState button = state.button_behavior(window, cell, id);
    static_cast<void>(state.describe_item(AccessibilityRole::COLUMN_HEADER, AccessibilityState::NONE, can_sort ? AccessibilityAction::CLICK : AccessibilityAction::NONE));

    // Dragged past a neighbor: it moves there next frame (the mouse moving that way, so it does not swing back).
    const bool reorderable = has_any(table.flags, TableFlag::REORDERABLE) && !has_any(column.flags, TableColumnFlag::NO_REORDER);
    const bool dragged = state.mouse[0].drag_maximum >= state.description.input.drag_threshold;
    if (button.held && reorderable && dragged && !state.drag_drop.active)
    {
        table.reorder_column = index;
        if (state.mouse_delta.x < 0 && state.mouse_position.x < cell.left)
        {
            table.reorder_direction = -1;
        }
        else if (state.mouse_delta.x > 0 && state.mouse_position.x > cell.right)
        {
            table.reorder_direction = 1;
        }
    }

    // A click (not a drag that reorders) sorts by it; Shift adds it to the sort.
    if (button.pressed && can_sort && !(reorderable && dragged))
    {
        sort_by_column(table, index, has_any(state.modifiers, KeyModifier::SHIFT));
    }

    // Its background: held, hovered, its context menu open, or its column hovered (HIGHLIGHT_HOVERED_COLUMN).
    const bool menu_open = table.context_column == index && state.popup_open(context_menu_identifier(table));
    std::optional<Color> fill{};
    if (button.held && button.hovered)
    {
        fill = state.color(StyleColor::HEADER_ACTIVE);
    }
    else if (button.hovered || menu_open)
    {
        fill = state.color(button.hovered ? StyleColor::HEADER_HOVERED : StyleColor::HEADER);
    }
    else if (has_any(table.flags, TableFlag::HIGHLIGHT_HOVERED_COLUMN) && table.hovered_column == index)
    {
        const Color hovered = state.color(StyleColor::HEADER_HOVERED);
        fill = hovered.with_alpha(hovered.alpha * 0.5F);
    }
    if (fill.has_value() && table.cell_backgrounds.size() < table.cell_backgrounds.capacity())
    {
        table.cell_backgrounds.push_back({.column = index, .color = *fill});
    }

    // The label (cut before the arrow), the arrow and the rank; the whole label in a tooltip when it is cut.
    DrawList& list = state.list_of(window);
    state.draw_focus(list, cell, id, 0.0F);
    const float label_right = column.content_right - arrow_space - rank_space;
    const bool cut = position.x + text.width > label_right;
    if (cut)
    {
        list.push_clip_rectangle({.left = cell.left, .top = cell.top, .right = label_right, .bottom = cell.bottom});
    }
    state.draw_text(list, text, position, state.color(StyleColor::TEXT_NORMAL));
    if (cut)
    {
        list.pop_clip_rectangle();
    }
    if (can_sort && column.sort_order >= 0)
    {
        const float arrow_x = column.content_right - arrow;
        state.draw_arrow(list, {.x = arrow_x, .y = position.y + (line - arrow) * 0.5F}, arrow,
                         column.sort_direction == SortDirection::ASCENDING ? ArrowDirection::UP : ArrowDirection::DOWN, state.color(StyleColor::TEXT_NORMAL));
        if (ranked)
        {
            state.draw_text(list, rank_text, {.x = arrow_x - style.item_inner_spacing.x - rank_text.width, .y = position.y}, state.color(StyleColor::TEXT_DISABLED));
        }
    }
    if (cut && button.hovered && state.active_id == 0 && !label.empty())
    {
        item_tooltip(label);
    }

    // A right click opens the context menu for this column.
    if (button.hovered && state.mouse[std::to_underlying(MouseButton::RIGHT)].released)
    {
        open_context_menu(state, table, index);
    }
}

auto
UiFrame::table_sort_specifications() noexcept -> TableSortSpecifications
{
    ContextState& state = *m_state;
    TableState* table = current_table(state, "table_sort_specifications");
    if (table == nullptr || !has_any(table->flags, TableFlag::SORTABLE))
    {
        return {};
    }

    // The sort of this frame (after the set-up); once read, it is no longer "changed".
    layout_table(state, *table);
    const TableSortSpecifications result{.columns = table->sort, .changed = table->sort_changed};
    table->sort_changed = false;
    return result;
}

// ===================================================================================================== queries

auto
UiFrame::table_column_count() const noexcept -> std::uint32_t
{
    const TableState* table = current_table_quiet(*m_state);
    return table != nullptr ? static_cast<std::uint32_t>(table->columns.size()) : 0U;
}

auto
UiFrame::table_column_index() const noexcept -> std::int32_t
{
    const TableState* table = current_table_quiet(*m_state);
    return table != nullptr && table->row_open ? table->column : -1;
}

auto
UiFrame::table_row_index() const noexcept -> std::int32_t
{
    const TableState* table = current_table_quiet(*m_state);
    return table != nullptr ? table->row : -1;
}

auto
UiFrame::table_column_status(std::int32_t column) const noexcept -> TableColumnStatus
{
    const TableState* table = current_table_quiet(*m_state);
    if (table == nullptr)
    {
        return TableColumnStatus::NONE;
    }
    const std::int32_t index = column < 0 ? table->column : column;
    if (index < 0 || index >= static_cast<std::int32_t>(table->columns.size()))
    {
        return TableColumnStatus::NONE;
    }
    const TableColumnState& entry = table->columns[static_cast<std::size_t>(index)];
    TableColumnStatus status = TableColumnStatus::NONE;
    if (entry.enabled)
    {
        status = status | TableColumnStatus::ENABLED;
    }
    if (entry.enabled && table->layout_done && entry.clip_right > entry.clip_left)
    {
        status = status | TableColumnStatus::VISIBLE;
    }
    if (entry.sort_order >= 0)
    {
        status = status | TableColumnStatus::SORTED;
    }
    if (table->hovered_column == static_cast<std::uint32_t>(index))
    {
        status = status | TableColumnStatus::HOVERED;
    }
    return status;
}

auto
UiFrame::table_hovered_column() const noexcept -> std::int32_t
{
    const TableState* table = current_table_quiet(*m_state);
    return table != nullptr && table->hovered_column != NONE ? static_cast<std::int32_t>(table->hovered_column) : -1;
}

void
UiFrame::table_set_column_enabled(std::uint32_t column,
                                  bool enabled) noexcept
{
    ContextState& state = *m_state;
    TableState* table = current_table(state, "table_set_column_enabled");
    if (table == nullptr)
    {
        return;
    }
    if (column >= table->columns.size())
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "table_set_column_enabled", "the table has no such column");
        return;
    }
    TableColumnState& entry = table->columns[column];
    if (enabled || !has_any(entry.flags, TableColumnFlag::NO_HIDE))
    {
        entry.user_enabled = enabled;
    }
}

void
UiFrame::table_set_background_color(TableBackgroundTarget target,
                                    Color color,
                                    std::int32_t column) noexcept
{
    ContextState& state = *m_state;
    TableState* table = current_table(state, "table_set_background_color");
    if (table == nullptr)
    {
        return;
    }
    if (!table->row_open)
    {
        state.report(UiFrameProblem::INVALID_ARGUMENT, "table_set_background_color", "call it in a row (after table_next_row() or table_next_column())");
        return;
    }
    switch (target)
    {
        case TableBackgroundTarget::ROW:
        case TableBackgroundTarget::ROW_OVERLAY:
        {
            const std::size_t slot = target == TableBackgroundTarget::ROW ? 0 : 1;
            table->row_colors[slot] = color;
            table->row_color_set[slot] = true;
            return;
        }
        case TableBackgroundTarget::CELL:
        {
            const std::int32_t index = column < 0 ? table->column : column;
            if (index < 0 || index >= static_cast<std::int32_t>(table->columns.size()))
            {
                state.report(UiFrameProblem::INVALID_ARGUMENT, "table_set_background_color", "no cell: give a column, or call it in a cell");
                return;
            }
            const auto found = std::ranges::find(table->cell_backgrounds, static_cast<std::uint32_t>(index), &TableCellBackground::column);
            if (found != table->cell_backgrounds.end())
            {
                found->color = color;
            }
            else if (table->cell_backgrounds.size() < table->cell_backgrounds.capacity())
            {
                table->cell_backgrounds.push_back({.column = static_cast<std::uint32_t>(index), .color = color});
            }
            return;
        }
    }
}

// ===================================================================================================== ContextState

auto
ContextState::cell_table(const WindowState& window) noexcept -> TableState*
{
    return window.table < tables.size() ? &tables[window.table] : nullptr;
}

auto
ContextState::push_table_background(WindowState& window) noexcept -> bool
{
    // The row's clip (the whole table's width) in the channel under the cells.
    TableState* table = cell_table(window);
    if (table == nullptr || !table->cell_open || table->background_pushed)
    {
        return false;
    }
    DrawList& list = list_of(window);
    table->background_pushed = true;
    table->background_channel = table->splitter.channel();
    table->background_clip = window.clip;
    table->splitter.set_channel(list, SPAN_CHANNEL);
    const UiRectangle clip = table->measuring ? UiRectangle{} : row_clip(*table, table->row);
    window.clip = clip;
    list.push_clip_rectangle(clip, false);
    return true;
}

void
ContextState::pop_table_background(WindowState& window) noexcept
{
    TableState* table = cell_table(window);
    if (table == nullptr || !table->background_pushed)
    {
        return;
    }
    DrawList& list = list_of(window);
    list.pop_clip_rectangle();
    window.clip = table->background_clip;
    table->splitter.set_channel(list, table->background_channel);
    table->background_pushed = false;
}

void
ContextState::close_tables(UiFrame& ui_frame,
                           std::uint32_t window) noexcept
{
    while (!table_stack.empty())
    {
        const TableState& table = tables[table_stack.back()];
        if (table.host_window != window && table.window != window)
        {
            return;
        }
        report(UiFrameProblem::UNBALANCED_STACK, "end_window", "begin_table() without end_table()");
        ui_frame.end_table();
    }
}

auto
ContextState::clipper_position(WindowState& window) noexcept -> float
{
    // In a table: where its next row starts (the open row ends).
    if (TableState* table = list_table(*this, window))
    {
        layout_table(*this, *table);
        end_row(*this, *table);
        return row_screen_y(*table, table->row + 1, table->next_row_top);
    }
    return window.cursor.y;
}

void
ContextState::clipper_seek(WindowState& window,
                           float y,
                           std::uint32_t skipped) noexcept
{
    // In a table: the next row starts there, its index (and the alternating backgrounds) as if the rows between
    // were submitted.
    if (TableState* table = list_table(*this, window))
    {
        end_row(*this, *table);
        table->row += static_cast<std::int32_t>(skipped);
        table->body_row += static_cast<std::int32_t>(skipped);
        table->next_row_top = y - row_screen_y(*table, table->row + 1, 0.0F);
        return;
    }
    window.cursor = {.x = window.cursor_start.x + window.indent, .y = y};
    window.cursor_maximum.y = std::max(window.cursor_maximum.y, y - style.item_spacing.y);
    window.cursor_previous_line = window.cursor;
    window.line_height = 0;
    window.line_text_offset = 0;
}

auto
ContextState::clipper_visible(const WindowState& window) const noexcept -> std::pair<float, float>
{
    if (!table_stack.empty())
    {
        const TableState& table = tables[table_stack.back()];
        if (table.window == static_cast<std::uint32_t>(&window - windows.data()))
        {
            return {table.body_clip.top, table.body_clip.bottom};
        }
    }
    return {window.clip.top, window.clip.bottom};
}

} // namespace gfx::ui
