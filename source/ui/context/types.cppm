/// @file types.cppm
/// @brief gfx.ui.context:types: how a UiContext is made (UiContextDescription, UiContextLimits,
///        UiInputSettings), what a frame reports (UiFrameReport, UiFrameIssue, UiFrameStatistics), and the
///        values widgets take or return (KeyChord and its text, InputTextOptions, PlotOptions, table sort specifications,
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
    std::uint32_t open_states = 1024;         ///< Open states of tree nodes and collapsing headers remembered.
    std::uint32_t tab_bars = 32;              ///< Tab bars remembered (16 tabs each before they grow).
    std::uint32_t tables = 32;                ///< Tables remembered (their columns are allocated when a table first appears).
    std::uint32_t dock_nodes = 64;            ///< Nodes of the dock trees.
    std::size_t drag_drop_payload_bytes = 1024; ///< Bytes of a drag and drop payload.
    std::uint32_t shortcut_routes = 128;      ///< Key chords shortcut() and set_next_item_shortcut() route in a frame.
    std::uint32_t accessibility_nodes = 2048; ///< Nodes of the accessibility tree (when it is built).
    std::size_t accessibility_text_bytes = 64 * 1024; ///< Bytes of its names, descriptions and values.
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
    bool keyboard_navigation = true;                                  ///< The keyboard keymap: Tab, the arrows, Space, Return, Escape, Alt and the
                                                                      ///< Application key move the focus and use items (false: the application sends
                                                                      ///< them as UiFrameInput::navigation, from a rebindable input context).
    float navigation_scroll_speed = DEFAULT_NAVIGATION_SCROLL_SPEED;  ///< UiNavigationInput::scroll at full deflection, UI units per second.
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
    TextLayoutStatistics text_layouts{}; ///< The text layout cache's totals since the context was made.
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

/// @brief The text of a key chord ("Ctrl+Shift+S"), as menus show shortcuts.
struct KeyChordText
{
    std::array<char, MAXIMUM_KEY_CHORD_TEXT_SIZE> storage{}; ///< The characters.
    std::uint8_t size = 0;                                   ///< How many are used.

    /// @brief The text.
    [[nodiscard]] constexpr auto view() const noexcept -> std::string_view
    {
        return {storage.data(), size};
    }
};

/// @brief The text of @p chord: its modifiers ("Ctrl+", "Shift+", "Alt+", "Super+", in that order), then its key: a
///        letter in upper case, another character as itself, a named key in words ("Space", "PageDown", "F5",
///        "Delete"). Empty for KeyCode::UNKNOWN.
[[nodiscard]] constexpr auto
key_chord_text(KeyChord chord) noexcept -> KeyChordText
{
    KeyChordText text{};
    const auto append = [&text](std::string_view part)
    {
        for (const char character : part)
        {
            if (text.size < text.storage.size())
            {
                text.storage[text.size++] = character;
            }
        }
    };
    const auto code = static_cast<std::uint32_t>(std::to_underlying(chord.key));
    if (code == 0)
    {
        return text;
    }

    // The modifiers, either side of each.
    if (has_any(chord.modifiers, KeyModifier::CONTROL))
    {
        append("Ctrl+");
    }
    if (has_any(chord.modifiers, KeyModifier::SHIFT))
    {
        append("Shift+");
    }
    if (has_any(chord.modifiers, KeyModifier::ALT))
    {
        append("Alt+");
    }
    if (has_any(chord.modifiers, KeyModifier::SUPER))
    {
        append("Super+");
    }

    // A character: letters in upper case, others as they are (UTF-8).
    if (code > 0x20 && code != 0x7F && (code & KEY_CODE_NON_PRINTABLE_FLAG) == 0)
    {
        const std::uint32_t character = code >= 'a' && code <= 'z' ? code - 'a' + 'A' : code;
        std::array<char, 4> bytes{};
        std::size_t count = 0;
        if (character < 0x80)
        {
            bytes[count++] = static_cast<char>(character);
        }
        else if (character < 0x800)
        {
            bytes[count++] = static_cast<char>(0xC0 | (character >> 6));
            bytes[count++] = static_cast<char>(0x80 | (character & 0x3F));
        }
        else if (character < 0x10000)
        {
            bytes[count++] = static_cast<char>(0xE0 | (character >> 12));
            bytes[count++] = static_cast<char>(0x80 | ((character >> 6) & 0x3F));
            bytes[count++] = static_cast<char>(0x80 | (character & 0x3F));
        }
        else
        {
            bytes[count++] = static_cast<char>(0xF0 | ((character >> 18) & 0x07));
            bytes[count++] = static_cast<char>(0x80 | ((character >> 12) & 0x3F));
            bytes[count++] = static_cast<char>(0x80 | ((character >> 6) & 0x3F));
            bytes[count++] = static_cast<char>(0x80 | (character & 0x3F));
        }
        append({bytes.data(), count});
        return text;
    }

    // A named key: its enumerator's words, each capitalized ("PAGE_DOWN" is "PageDown").
    if (chord.key == KeyCode::DELETE_FORWARD)
    {
        append("Delete");
        return text;
    }
    const std::string_view name = to_string(chord.key);
    bool word_start = true;
    for (const char character : name)
    {
        if (character == '_')
        {
            word_start = true;
            continue;
        }
        const bool upper = character >= 'A' && character <= 'Z';
        const char shown = word_start || !upper ? character : static_cast<char>(character - 'A' + 'a');
        append({&shown, 1});
        word_start = false;
    }
    return text;
}

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

/// @brief Numbers a multi-component slider or drag edits in place: a contiguous, sized range of writable arithmetic
///        values (std::array, std::span, std::vector, a C array).
template<class Values>
concept EditableNumbers = std::ranges::contiguous_range<Values> && std::ranges::sized_range<Values>
                          && ArithmeticValue<std::ranges::range_value_t<Values>>
                          && !std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<Values>>>;

/// @brief A number of any arithmetic type with its bounds and its type's limits, as the numeric widgets edit it
///        (their templates make it): integers as 64-bit integers, floating-point numbers as doubles.
struct NumberField
{
    /// @brief A value of the field.
    using Value = std::variant<std::int64_t, std::uint64_t, double>;

    Value value{};                    ///< The number.
    Value minimum{};                  ///< Its smallest value (sliders; drags when below maximum).
    Value maximum{};                  ///< Its largest value.
    Value lowest{};                   ///< Its type's lowest value.
    Value highest{};                  ///< Its type's highest value.
    std::string_view default_format{}; ///< How it is shown without a format.

    /// @brief @p number as a Value.
    template<ArithmeticValue Number>
    [[nodiscard]] static constexpr auto wrap(Number number) noexcept -> Value
    {
        if constexpr (std::is_floating_point_v<Number>)
        {
            return static_cast<double>(number);
        }
        else if constexpr (std::is_signed_v<Number>)
        {
            return static_cast<std::int64_t>(number);
        }
        else
        {
            return static_cast<std::uint64_t>(number);
        }
    }

    /// @brief The field of @p value between @p minimum and @p maximum.
    template<ArithmeticValue Number>
    [[nodiscard]] static constexpr auto make(Number value,
                                             Number minimum,
                                             Number maximum) noexcept -> NumberField
    {
        return {.value = wrap(value),
                .minimum = wrap(minimum),
                .maximum = wrap(maximum),
                .lowest = wrap(std::numeric_limits<Number>::lowest()),
                .highest = wrap(std::numeric_limits<Number>::max()),
                .default_format = std::is_same_v<Number, float> ? DEFAULT_FLOAT_FORMAT
                                  : std::is_floating_point_v<Number> ? DEFAULT_DOUBLE_FORMAT
                                                                     : DEFAULT_INTEGER_FORMAT};
    }

    /// @brief The value as a @p Number (within its type: the widgets clamp it to the type's limits).
    template<ArithmeticValue Number>
    [[nodiscard]] constexpr auto as() const noexcept -> Number
    {
        return std::visit([](auto number) { return static_cast<Number>(number); }, value);
    }
};

/// @brief Something a list (combo, list box) can show as an item's text.
template<class Item>
concept TextLike = std::convertible_to<const Item&, std::string_view>;

} // namespace gfx::ui
