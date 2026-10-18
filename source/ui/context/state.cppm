/// @file state.cppm
/// @brief gfx.ui.context:state (internal): ContextState, everything a UiContext holds — retained windows, the
///        per-frame input, interaction and layout state, the stacks, the published draw data, the report — and
///        the helpers the implementation units share (layout, item behavior, drawing). Every member has an
///        initializer (toolchain defect 7).
module gfx.ui.context:state;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.handles;
import gfx.ui.draw;
import gfx.ui.style;
import gfx.ui.text;
import gfx.ui.input;
import :types;
import :frame;

namespace gfx::ui
{

/// Open-addressing map from identifiers (never 0) to indices. It allocates only when it grows past half full.
class IdentifierMap
{
public:
    /// Room for @p entries without growing.
    void reserve(std::size_t entries);

    /// The value of @p key.
    [[nodiscard]] auto find(std::uint32_t key) const noexcept -> std::optional<std::uint32_t>;

    /// Adds (or replaces) @p key; true when the table grew (an allocation).
    auto insert(std::uint32_t key,
                std::uint32_t value) -> bool;

private:
    std::vector<std::uint32_t> m_keys{};
    std::vector<std::uint32_t> m_values{};
    std::size_t m_count = 0;
};

/// Identifiers seen in a frame (conflict detection): fixed capacity, cleared in constant time.
class IdentifierSet
{
public:
    /// Room for @p entries.
    void reserve(std::size_t entries);

    /// Empties the set.
    void clear() noexcept;

    /// Adds @p key: true when it was not in the set, false when it was, nothing when the set is too full to
    /// tell.
    auto insert(std::uint32_t key) noexcept -> std::optional<bool>;

private:
    std::vector<std::uint32_t> m_keys{};
    std::vector<std::uint32_t> m_stamps{};
    std::uint32_t m_generation = 1;
    std::size_t m_count = 0;
};

/// The frame arena's upstream: counts the allocations made because the arena's buffer was too small.
class CountingResource final : public std::pmr::memory_resource
{
public:
    std::uint32_t allocations = 0; ///< Allocations since the last reset.

private:
    auto do_allocate(std::size_t bytes,
                     std::size_t alignment) -> void* override
    {
        ++allocations;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* memory,
                       std::size_t bytes,
                       std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(memory, bytes, alignment);
    }

    [[nodiscard]] auto do_is_equal(const std::pmr::memory_resource& other) const noexcept -> bool override
    {
        return this == &other;
    }
};

/// The legacy columns of a window (UiFrame::columns()).
struct ColumnsState
{
    bool active = false;          ///< Columns are in effect.
    std::uint32_t id = 0;         ///< Identifier of the set.
    std::uint32_t count = 1;      ///< Columns.
    std::uint32_t current = 0;    ///< Current column.
    bool borders = true;          ///< Lines between columns.
    float start_x = 0;            ///< Left edge of the first column, screen.
    float width = 0;              ///< Width of all columns.
    float start_y = 0;            ///< Top of the set, screen.
    float row_start_y = 0;        ///< Top of the current row.
    float maximum_y = 0;          ///< Bottom of the tallest column of the row so far.
    float saved_indent = 0;       ///< The window's indentation before the columns.
};

/// What a window remembers between frames, and its layout state during one.
struct WindowState
{
    std::uint32_t id = 0;                         ///< Identifier (its name hashed, a child's with its parent's).
    std::string name{};                           ///< Title (the name before "##").
    std::uint32_t parent = 0xFFFFFFFFU;           ///< Index of the parent window (children), or none.
    std::uint32_t root = 0;                       ///< Index of the root window (itself for roots).
    bool child = false;                           ///< A child region (begin_child()).
    UiWindowFlag flags = UiWindowFlag::NONE;      ///< Flags of this frame.
    ChildFlag child_flags = ChildFlag::NONE;      ///< Child flags of this frame.
    Vector2 position{};                           ///< Top-left corner, screen (UI units).
    Vector2 size{};                               ///< Size (expanded), UI units (0: never sized).
    Vector2 content_size{};                       ///< Content measured at the last end_window().
    Vector2 explicit_content_size{};              ///< set_next_window_content_size() (0: measured).
    Vector2 scroll{};                             ///< Scroll.
    Vector2 scroll_maximum{};                     ///< Largest scroll.
    Vector2 scroll_target{-1.0F, -1.0F};          ///< Scroll asked for (negative: none).
    float scroll_target_ratio = 0;                ///< set_scroll_here_y()'s ratio (with scroll_target.y).
    bool collapsed = false;                       ///< Collapsed to its title bar.
    bool active = false;                          ///< Submitted this frame.
    bool was_active = false;                      ///< Submitted last frame.
    bool appearing = false;                       ///< Appears this frame.
    bool hidden = false;                          ///< Measured but not drawn this frame (its first frame, to fit).
    bool auto_fit = false;                        ///< Fit to its content at end_window().
    bool first_use = true;                        ///< Never submitted before.
    std::uint8_t used_once = 0;                   ///< UiCondition::ONCE settings applied (bits: position, size, collapsed).
    std::uint32_t begin_count = 0;                ///< begin_window() (begin_child()) calls this frame: later ones append.
    std::uint64_t last_active_frame = 0;          ///< Last frame it was submitted.
    float background_alpha = 1;                   ///< Opacity of its background this frame.
    Vector2 minimum_size{};                       ///< Size constraints of this frame.
    Vector2 maximum_size{};                       ///< (0: none).
    bool scrollbar_x = false;                     ///< A horizontal scrollbar this frame.
    bool scrollbar_y = false;                     ///< A vertical scrollbar this frame.
    float title_bar_height = 0;                   ///< Height of its title bar this frame.
    UiRectangle outer{};                          ///< Its rectangle, screen.
    UiRectangle hit{};                            ///< Where the mouse hovers it: its rectangle in its parent's clip.
    UiRectangle inner{};                          ///< Content area (title bar and scrollbars excluded), screen.
    UiRectangle clip{};                           ///< What its content is clipped to.
    Vector2 padding{};                            ///< Padding of this frame.
    Vector2 cursor{};                             ///< Where the next item goes, screen.
    Vector2 cursor_start{};                       ///< Start of the content, screen (scroll included).
    Vector2 cursor_maximum{};                     ///< Bottom-right of the content laid out so far.
    Vector2 cursor_previous_line{};               ///< End of the previous item (same_line()).
    float line_height = 0;                        ///< Height of the current line so far.
    float previous_line_height = 0;               ///< Height of the previous line.
    float line_text_offset = 0;                   ///< Baseline offset of the current line (align_text_to_frame_padding()).
    float previous_line_text_offset = 0;          ///< Of the previous line.
    float indent = 0;                             ///< Indentation from the content start.
    float item_width = 0;                         ///< Default item width this frame.
    bool skip_items = false;                      ///< Items are not laid out (collapsed, clipped child).
    ColumnsState columns{};                       ///< Legacy columns.
    std::uint32_t identifier_stack_base = 0;      ///< Identifier stack depth at begin.
    DrawList draw_list{};                         ///< Its drawing (roots; children draw into their root's).
};

/// The state of a mouse button.
struct MouseButtonState
{
    bool down = false;               ///< Held.
    bool clicked = false;            ///< Pressed this frame.
    bool released = false;           ///< Released this frame.
    bool double_clicked = false;     ///< The press of this frame is the second of a double click.
    std::uint32_t click_count = 0;   ///< Presses in a row within the double click time and distance.
    double clicked_time = -1;        ///< When it was last pressed.
    Vector2 clicked_position{};      ///< Where.
    double held_seconds = -1;        ///< How long it has been held (-1: not held).
    double previous_held_seconds = -1; ///< The same last frame (repeats).
    float drag_maximum = 0;          ///< Largest distance from clicked_position while held.
};

/// What the next window's begin_window() applies.
struct NextWindowSettings
{
    bool has_position = false;                         ///< set_next_window_position() was called.
    Vector2 position{};                                ///< Its position.
    Vector2 pivot{};                                   ///< Its pivot.
    UiCondition position_condition = UiCondition::ALWAYS; ///< Its condition.
    bool has_size = false;                             ///< set_next_window_size() was called.
    Vector2 size{};                                    ///< Its size.
    UiCondition size_condition = UiCondition::ALWAYS;  ///< Its condition.
    bool has_constraints = false;                      ///< set_next_window_size_constraints() was called.
    Vector2 minimum{};                                 ///< Minimum size.
    Vector2 maximum{};                                 ///< Maximum size.
    bool has_content_size = false;                     ///< set_next_window_content_size() was called.
    Vector2 content_size{};                            ///< Its content size.
    bool has_collapsed = false;                        ///< set_next_window_collapsed() was called.
    bool collapsed = false;                            ///< Its value.
    UiCondition collapsed_condition = UiCondition::ALWAYS; ///< Its condition.
    bool focus = false;                                ///< set_next_window_focus() was called.
    bool has_scroll = false;                           ///< set_next_window_scroll() was called.
    Vector2 scroll{};                                  ///< Its scroll.
    bool has_background_alpha = false;                 ///< set_next_window_background_alpha() was called.
    float background_alpha = 1;                        ///< Its opacity.
};

/// What the last item reported.
struct LastItem
{
    std::uint32_t id = 0;                ///< Identifier (0: not interactive).
    UiRectangle rectangle{};             ///< Rectangle, screen.
    std::uint32_t window = 0xFFFFFFFFU;  ///< Its window.
    bool hovered = false;                ///< The mouse is over it.
    bool visible = false;                ///< Not clipped.
    bool edited = false;                 ///< Its value changed this frame.
    bool disabled = false;               ///< Submitted disabled.
};

/// A color changed by push_style_color().
struct ColorBackup
{
    StyleColor element = StyleColor::TEXT_NORMAL; ///< The element.
    Color previous{};                      ///< Its color before.
};

/// A style variable changed by push_style_variable().
struct VariableBackup
{
    StyleVariable variable = StyleVariable::ALPHA; ///< The variable.
    Vector2 previous{};                            ///< Its value before (x only for floats).
};

/// A font of the font stack and its metrics.
struct FontMetrics
{
    FontHandle font{};       ///< The font (null: none).
    float size = 13.0F;      ///< UI units per em.
    float line_height = 16.0F; ///< Height of a line.
};

/// A group of begin_group().
struct GroupState
{
    std::uint32_t window = 0;          ///< Its window.
    Vector2 start{};                   ///< Cursor at begin_group().
    Vector2 cursor_maximum{};          ///< The window's cursor maximum then.
    float indent = 0;                  ///< The window's indentation then.
    float line_height = 0;             ///< The current line's height then.
    float line_text_offset = 0;        ///< The current line's baseline offset then.
};

/// Draw data published by end_frame(): the lists swapped out of the windows, and the leases on them.
struct DrawDataSlot
{
    std::vector<DrawList> lists{};            ///< The lists (kept: their arrays are reused).
    std::vector<const DrawList*> pointers{};  ///< UiDrawData::lists.
    UiDrawData data{};                        ///< What draw_data() returns.
    std::atomic<std::uint32_t> leases{0};     ///< UiDrawDataLease objects holding it.
};

/// What button_behavior() found.
struct ButtonState
{
    bool hovered = false; ///< The mouse is over it (and may use it).
    bool held = false;    ///< Held (mouse or keyboard).
    bool pressed = false; ///< Clicked (or activated) this frame.
};

/// Everything a UiContext holds.
struct ContextState
{
    /// No window.
    static constexpr std::uint32_t NO_WINDOW = 0xFFFFFFFFU;

    explicit ContextState(const UiContextDescription& context_description);

    // ----------------------------------------------------------------------------------------- reporting

    /// Records an issue of the frame.
    void report(UiFrameProblem problem,
                std::string_view operation,
                std::string_view detail,
                std::string_view name = {},
                std::uint32_t identifier = 0) noexcept;

    /// The current window, or nullptr (reported: no frame, no window) for @p operation.
    [[nodiscard]] auto current_window(std::string_view operation) noexcept -> WindowState*;

    /// The current window without reporting (queries), or nullptr.
    [[nodiscard]] auto current_window_quiet() const noexcept -> const WindowState*;

    // ----------------------------------------------------------------------------------------- identifiers

    /// The identifier of @p label under @p seed ("###" rules included): FNV-1a of the seed's bytes then the
    /// label's, as WidgetIdentifier::from_name() of those bytes.
    [[nodiscard]] static auto hash(std::uint32_t seed,
                                   std::string_view label) noexcept -> std::uint32_t;

    /// @p label without its "##" part.
    [[nodiscard]] static auto display_text(std::string_view label) noexcept -> std::string_view;

    /// The identifier of @p label under the identifier stack.
    [[nodiscard]] auto widget_id(std::string_view label) const noexcept -> std::uint32_t;

    /// The top of the identifier stack.
    [[nodiscard]] auto id_seed() const noexcept -> std::uint32_t;

    // ----------------------------------------------------------------------------------------- windows

    /// The window of @p id, or NO_WINDOW.
    [[nodiscard]] auto find_window(std::uint32_t id) const noexcept -> std::uint32_t;

    /// A new window (its index).
    auto create_window(std::uint32_t id,
                       std::string_view name,
                       std::uint32_t parent) -> std::uint32_t;

    /// Focuses root window @p index (and brings it to the front unless its flags say otherwise).
    void focus_window(std::uint32_t index) noexcept;

    /// The draw list window @p window draws into (its root's).
    [[nodiscard]] auto list_of(const WindowState& window) noexcept -> DrawList&;

    /// Right edge of @p window's content region, screen (where it is with the content scrolled to its start, so
    /// the space available does not change with scrolling).
    [[nodiscard]] auto content_right(const WindowState& window) const noexcept -> float;

    /// Bottom edge of @p window's content region, screen.
    [[nodiscard]] auto content_bottom(const WindowState& window) const noexcept -> float;

    /// The width of the next framed item in @p window (and the next item width is used up).
    [[nodiscard]] auto take_item_width(const WindowState& window) noexcept -> float;

    /// Whether @p window, or a window of its hierarchy as @p flags say, is the hovered window.
    [[nodiscard]] auto window_hovered(const WindowState& window,
                                      HoveredFlag flags) const noexcept -> bool;

    // ----------------------------------------------------------------------------------------- fonts and text

    /// The metrics of @p font (null: the fallback metrics).
    [[nodiscard]] auto metrics_of(FontHandle font) noexcept -> FontMetrics;

    /// The current font and its metrics.
    [[nodiscard]] auto current_font() const noexcept -> const FontMetrics&;

    /// @p text laid out in the current font (wrapped at @p wrap_width when > 0); problems are reported for
    /// @p operation.
    [[nodiscard]] auto layout_text(std::string_view text,
                                   float wrap_width,
                                   std::string_view operation) noexcept -> TextLayout;

    /// Draws @p layout at @p position (top-left) in @p color.
    void draw_text(DrawList& list,
                   const TextLayout& layout,
                   Vector2 position,
                   Color color) noexcept;

    // ----------------------------------------------------------------------------------------- layout and items

    /// Advances @p window's cursor past an item of @p size (@p text_offset: its text baseline below its top).
    void item_size(WindowState& window,
                   Vector2 size,
                   float text_offset = 0) noexcept;

    /// Registers an item: the last item, identifier conflicts, the keyboard focus order. Returns whether it is
    /// visible (not clipped).
    auto item_add(WindowState& window,
                  const UiRectangle& rectangle,
                  std::uint32_t id,
                  bool focusable,
                  std::string_view operation,
                  std::string_view label) noexcept -> bool;

    /// Whether the mouse may use the item @p id of @p rectangle in @p window.
    [[nodiscard]] auto item_hoverable(const WindowState& window,
                                      const UiRectangle& rectangle,
                                      std::uint32_t id) noexcept -> bool;

    /// Mouse and keyboard behavior of a clickable item.
    auto button_behavior(WindowState& window,
                         const UiRectangle& rectangle,
                         std::uint32_t id,
                         ButtonFlag flags = ButtonFlag::NONE) noexcept -> ButtonState;

    /// Makes @p id the active item.
    void set_active(std::uint32_t id,
                    const WindowState& window) noexcept;

    /// No item active.
    void clear_active() noexcept;

    /// Item @p id (the last item) changed its value this frame.
    void mark_edited(std::uint32_t id) noexcept;

    // ----------------------------------------------------------------------------------------- drawing

    /// The color of @p element in effect, faded by the style's alpha (and disabled_alpha when disabled).
    [[nodiscard]] auto color(StyleColor element) const noexcept -> Color;

    /// @p value faded as color() fades.
    [[nodiscard]] auto faded(Color value) const noexcept -> Color;

    /// A frame: filled, with a border when the style has one.
    void draw_frame(DrawList& list,
                    const UiRectangle& rectangle,
                    Color fill,
                    float rounding) noexcept;

    /// The keyboard focus highlight around item @p id, when it has the focus.
    void draw_focus(DrawList& list,
                    const UiRectangle& rectangle,
                    std::uint32_t id,
                    float rounding) noexcept;

    /// An arrow (a triangle) of @p size at @p position.
    void draw_arrow(DrawList& list,
                    Vector2 position,
                    float size,
                    ArrowDirection direction,
                    Color color) noexcept;

    // ----------------------------------------------------------------------------------------- data

    UiContextDescription description{};
    UiNullPlatformServices null_services{};
    UiPlatformServices* services = nullptr;
    FontLibrary fonts{};
    TextLayoutEngine text;
    FontHandle default_font{};
    Theme theme{};

    // The frame.
    bool frame_active = false;
    std::uint64_t frame = 0;
    double time = 0;
    double previous_time = -1;
    float delta_time = 1.0F / 60.0F;
    Vector2 display_size{};
    float pixels_per_unit = 1;
    Style style{};
    std::array<Color, ENUMERATOR_COUNT<StyleColor>> colors{};

    // Input.
    std::array<bool, 256> keys_down{};
    std::array<std::uint8_t, 256> keys_pressed{};
    std::array<std::uint8_t, 256> keys_repeated{};
    std::array<bool, 256> keys_released{};
    KeyModifier modifiers = KeyModifier::NONE;
    std::array<MouseButtonState, 5> mouse{};
    Vector2 mouse_position{};
    bool mouse_present = false;
    Vector2 mouse_wheel{};
    std::string typed_text{};

    // Windows.
    std::vector<WindowState> windows{};
    IdentifierMap window_lookup{};
    std::vector<std::uint32_t> display_order{};
    std::vector<std::uint32_t> window_stack{};
    std::vector<std::uint32_t> children_last_frame{};
    std::vector<std::uint32_t> children_this_frame{};
    std::uint32_t hovered_window = NO_WINDOW;
    std::uint32_t hovered_root = NO_WINDOW;
    std::uint32_t wheel_window = NO_WINDOW;
    std::uint32_t focused_window = NO_WINDOW;
    std::uint32_t moving_window = NO_WINDOW;
    Vector2 moving_offset{};
    std::uint32_t windows_created = 0;
    NextWindowSettings next_window{};

    // Items.
    std::uint32_t hovered_id = 0;
    std::uint32_t hovered_id_previous = 0;
    std::uint32_t active_id = 0;
    std::uint32_t active_id_previous_frame = 0;
    bool active_id_alive = false;
    bool active_id_edited = false;
    bool active_id_edited_previous = false;
    std::uint32_t active_window = NO_WINDOW;
    MouseButton active_button = MouseButton::LEFT;
    Vector2 active_offset{};
    bool active_by_keyboard = false;
    std::uint32_t deactivated_id = 0;
    bool deactivated_after_edit = false;
    std::uint32_t focus_id = 0;
    bool focus_visible = false;
    bool focus_alive = false;
    std::int32_t focus_step = 0;          // Tab (+1) or Shift+Tab (-1) this frame
    bool focus_here_requested = false;    // set_keyboard_focus_here(): the next focusable item
    bool focus_seen_current = false;
    std::uint32_t focus_first = 0;
    std::uint32_t focus_last = 0;
    std::uint32_t focus_before_current = 0;
    std::uint32_t focus_after_current = 0;
    std::uint32_t focus_here_result = 0;
    std::uint32_t focus_here_window = 0xFFFFFFFFU;
    std::uint32_t default_focus_request = 0;
    LastItem last_item{};
    bool next_item_has_width = false;
    float next_item_width = 0;
    IdentifierSet frame_ids{};
    bool frame_ids_full = false;
    std::uint32_t items = 0;

    // Stacks.
    std::vector<std::uint32_t> id_stack{};
    std::vector<ColorBackup> color_stack{};
    std::vector<VariableBackup> variable_stack{};
    std::vector<FontMetrics> font_stack{};
    std::vector<float> item_width_stack{};
    std::vector<float> text_wrap_stack{};
    std::vector<GroupState> group_stack{};
    std::vector<bool> disabled_stack{};
    bool disabled = false;
    FontMetrics fallback_font{};
    FontMetrics default_font_metrics{};

    // Drawing and publication.
    DrawList background{};
    DrawList foreground{};
    std::array<DrawDataSlot, 3> slots{};
    std::atomic<std::uint32_t> published{3};
    UiDrawData empty_data{};
    CursorShape cursor = CursorShape::ARROW;
    CursorShape applied_cursor = CursorShape::ARROW;
    bool wants_mouse = false;
    bool wants_keyboard = false;

    // The frame arena and the report.
    std::vector<std::byte> arena_buffer{};
    CountingResource arena_upstream{};
    std::pmr::monotonic_buffer_resource arena{};
    std::uint32_t allocations = 0;
    std::size_t arena_used = 0;
    std::array<UiFrameIssue, MAXIMUM_FRAME_ISSUES> issues{};
    std::size_t issue_count = 0;
    std::uint32_t dropped_issues = 0;
    UiFrameProblem problems = UiFrameProblem::NONE;
    std::array<UiFrameIssue, MAXIMUM_FRAME_ISSUES> report_issues{};
    UiFrameReport report_value{};
    bool no_font_reported = false;

    UiFrame frame_object;
};

} // namespace gfx::ui
