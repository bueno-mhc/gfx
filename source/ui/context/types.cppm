/// @file types.cppm
/// @brief gfx.ui.context:types: how a UiContext is made (UiContextDescription, UiContextLimits,
///        UiInputSettings), what a frame reports (UiFrameReport, UiFrameIssue, UiFrameStatistics), and the
///        values widgets take or return (KeyChord, InputTextOptions, PlotOptions, table sort specifications,
///        drag and drop payloads).
export module gfx.ui.context:types;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.input;
import gfx.ui.style;
import gfx.ui.text;

export namespace gfx::ui
{

/// @brief Capacities a context reserves when it is made. Exceeding one allocates (windows) or skips a check
///        (identifier conflicts); either is reported as UiFrameProblem::CAPACITY_EXCEEDED, never thrown.
struct UiContextLimits
{
    std::uint32_t windows = 64;               ///< Windows and child windows remembered.
    std::uint32_t items_per_frame = 4096;     ///< Interactive items a frame checks for identifier conflicts.
    std::uint32_t stack_depth = 64;           ///< Depth of the identifier, style, font, group and window stacks.
    std::size_t frame_arena_bytes = 64 * 1024; ///< Per-frame scratch memory (long formatted texts, long identifiers).
};

/// @brief How mouse and keyboard input is interpreted.
struct UiInputSettings
{
    double double_click_seconds = DEFAULT_DOUBLE_CLICK_SECONDS;       ///< Longest time between the clicks of a double click.
    float double_click_distance = DEFAULT_DOUBLE_CLICK_DISTANCE;      ///< Largest distance between them, UI units.
    float drag_threshold = DEFAULT_DRAG_THRESHOLD;                    ///< Distance before a held button drags, UI units.
    double repeat_delay_seconds = DEFAULT_REPEAT_DELAY_SECONDS;       ///< Time a button is held before ButtonFlag::REPEAT repeats.
    double repeat_interval_seconds = DEFAULT_REPEAT_INTERVAL_SECONDS; ///< Time between repeats.
    bool move_windows_from_title_bar_only = false;                    ///< Windows move by their title bar only (otherwise by any empty area).
};

/// @brief How a UiContext is made.
struct UiContextDescription
{
    UiPlatformServices* services = nullptr;              ///< Clipboard, cursors, text input (not owned; null: in-memory null services).
    UiContextLimits limits{};                            ///< Reserved capacities.
    UiInputSettings input{};                             ///< Click, drag and repeat thresholds.
    Theme theme = builtin_theme(ThemeKind::DARK);        ///< The look (UiContext::set_theme() changes it).
    TextLayoutEngineDescription text{};                  ///< The text layout cache.
};

/// @brief Something that went wrong in a frame.
struct UiFrameIssue
{
    UiFrameProblem problem = UiFrameProblem::NONE; ///< What.
    std::string_view operation{};                  ///< The function that met it ("button", "end_window").
    std::string_view detail{};                     ///< Why, or what to do (a literal).
    std::uint32_t identifier = 0;                  ///< The widget's or window's identifier (0: none).
    std::array<char, MAXIMUM_ISSUE_NAME_SIZE> name_storage{}; ///< The label or window name involved (cut).
    std::uint8_t name_size = 0;                    ///< Bytes of name_storage used.

    /// @brief The label or window name involved (empty when none).
    [[nodiscard]] auto name() const noexcept -> std::string_view
    {
        return {name_storage.data(), name_size};
    }
};

/// @brief What a frame drew and used.
struct UiFrameStatistics
{
    std::uint32_t windows = 0;       ///< Windows submitted (child windows included).
    std::uint32_t items = 0;         ///< Items laid out.
    std::uint32_t draw_lists = 0;    ///< Draw lists published.
    std::uint32_t vertices = 0;      ///< Vertices published.
    std::uint32_t indices = 0;       ///< Indices published.
    std::size_t arena_bytes = 0;     ///< Frame arena memory used.
    std::uint32_t allocations = 0;   ///< Allocations made because a reserved capacity was exceeded.
};

/// @brief What UiContext::end_frame() reports: the frame's problems (UiFrameProblem::NONE when it went well),
///        its issues and statistics. The issues stay valid until the next end_frame().
struct UiFrameReport
{
    UiFrameProblem problems = UiFrameProblem::NONE; ///< Every problem of the frame.
    std::span<const UiFrameIssue> issues{};         ///< Each issue (at most MAXIMUM_FRAME_ISSUES).
    std::uint32_t dropped_issues = 0;               ///< Issues beyond MAXIMUM_FRAME_ISSUES.
    UiFrameStatistics statistics{};                 ///< What the frame drew and used.

    /// @brief Whether the frame had no problem.
    [[nodiscard]] auto ok() const noexcept -> bool
    {
        return problems == UiFrameProblem::NONE;
    }
};

/// @brief A key with modifiers, by symbol of the keyboard layout (Control+S stays on the key labeled S on AZERTY).
struct KeyChord
{
    KeyCode key = KeyCode::UNKNOWN;            ///< The key.
    KeyModifier modifiers = KeyModifier::NONE; ///< Modifiers held (KeyModifier::CONTROL: either Control key).

    /// @brief Chords compare member by member.
    [[nodiscard]] constexpr auto operator==(const KeyChord&) const noexcept -> bool = default;
};

/// @brief How a text field behaves (UiFrame::input_text()).
struct InputTextOptions
{
    std::string_view hint{};                    ///< Shown while the text is empty.
    InputTextFlag flags = InputTextFlag::NONE;  ///< Filters and behavior.
    Vector2 size{};                             ///< Multi-line fields: their size (0: the default).
};

/// @brief How plot_lines() and plot_histogram() draw.
struct PlotOptions
{
    std::string_view overlay{};                 ///< Text drawn over the plot.
    float minimum = std::numeric_limits<float>::quiet_NaN(); ///< Bottom of the scale (NaN: the smallest value).
    float maximum = std::numeric_limits<float>::quiet_NaN(); ///< Top of the scale (NaN: the largest value).
    Vector2 size{};                             ///< Size of the graph (0: the item width, a default height).
};

/// @brief How one column of a table is sorted.
struct TableColumnSortSpecification
{
    std::uint32_t column = 0;                     ///< Index of the column.
    std::uint32_t order = 0;                      ///< Rank in a sort by several columns (0: first).
    SortDirection direction = SortDirection::NONE; ///< Ascending or descending.
};

/// @brief How a table is sorted (UiFrame::table_sort_specifications()).
struct TableSortSpecifications
{
    std::span<const TableColumnSortSpecification> columns{}; ///< The sorted columns, by rank.
    bool changed = false;                                    ///< Whether the sort changed since it was last read: sort the data again.
};

/// @brief Data dragged from a drag and drop source.
struct UiDragDropPayload
{
    std::string_view type{};            ///< Application-defined type ("color", "asset"): targets accept by type.
    std::span<const std::byte> data{};  ///< The bytes (copied by the UI when the source set them).
    bool delivery = false;              ///< The mouse button was released over the target: take it now.
};

/// @brief A number a slider, drag or numeric field can edit.
template<class Value>
concept ArithmeticValue = std::is_arithmetic_v<Value> && !std::same_as<Value, bool>;

/// @brief Something a list (combo, list box) can show as an item's text.
template<class Item>
concept TextLike = std::convertible_to<const Item&, std::string_view>;

} // namespace gfx::ui
