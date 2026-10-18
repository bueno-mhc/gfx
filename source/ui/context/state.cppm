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
    bool popup = false;                           ///< A popup (combo lists, color pickers): drawn over the other windows.
    bool tooltip = false;                         ///< A tooltip: drawn over the popups, never hovered.
    float menu_bar_height = 0;                    ///< Height of its menu bar this frame (UiWindowFlag::MENU_BAR), or 0.
    std::array<float, 2> menu_columns{};          ///< Menus: widths of the labels and shortcuts of last frame's items.
    std::array<float, 2> next_menu_columns{};     ///< ...measured this frame.
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

/// The text field being edited (one at a time), and the scratch text it lays out.
struct TextEditState
{
    std::uint32_t id = 0;                      ///< The field (0: none).
    TextEditor editor{};                       ///< Its text, cursor, selection and history.
    std::string initial{};                     ///< The text when editing started (Escape reverts to it).
    InputTextFlag flags = InputTextFlag::NONE; ///< The field's flags.
    bool multiline = false;                    ///< A multi-line field.
    bool numeric = false;                      ///< A slider's or drag's typed value (applied when editing ends).
    float scroll_x = 0;                        ///< Single-line fields: how far the text is scrolled.
    double changed_time = 0;                   ///< Last edit or motion (the cursor shows a while after).
    UiRectangle frame{};                       ///< The field's rectangle this frame (a click elsewhere ends editing).
    UiRectangle cursor{};                      ///< The cursor's rectangle (where the IME's window goes).
    float preferred_x = -1;                    ///< Multi-line fields: the column Up and Down keep (-1: none).
    bool mouse_selecting = false;              ///< A drag selects.
    std::uint32_t ended_id = 0;                ///< The field whose editing ended this frame.
    bool ended_committed = false;              ///< It ended by Return, Tab or a click elsewhere (not Escape).
    std::string ended_text{};                  ///< Its text then (numeric fields apply it).
    std::string display{};                     ///< What is laid out (masked, with the composition).
    std::string filtered{};                    ///< Typed text after the filters.
    TextBoundaries mask_boundaries{};          ///< Graphemes of a password field not being edited (its mask).
    std::uint64_t started_frame = 0;           ///< The frame editing started (a slider's typed input keeps everything selected).
};

/// An open popup (a combo's list, a color editor's picker).
struct PopupState
{
    std::uint32_t id = 0;                  ///< Identifier.
    std::uint32_t window = 0xFFFFFFFFU;    ///< Its window, once begun.
    std::uint32_t opener = 0xFFFFFFFFU;    ///< The root window it was opened from.
    Vector2 position{};                    ///< Its top-left corner.
    float minimum_width = 0;               ///< At least this wide (a combo's width).
    float maximum_height = 0;              ///< At most this tall (0: no limit).
    float flip_bottom = -1;                ///< Where its bottom goes when it does not fit below (-1: it is moved up instead).
    float flip_right = -1;                 ///< Where its right edge goes when it does not fit on the right (-1: it is moved left instead).
    bool alive = false;                    ///< Begun this frame.
    std::uint64_t opened_frame = 0;        ///< When it opened.
    bool modal = false;                    ///< A modal (begin_modal()): blocks the windows and popups below it.
    bool menu = false;                     ///< A menu (begin_menu()).
    bool submenu = false;                  ///< A menu opened from a menu row inside a popup (closing it closes its parent).
    std::uint32_t bar_window = 0xFFFFFFFFU; ///< The window whose menu bar opened it, or none.
    std::uint32_t opener_item = 0;         ///< The item that opened it (the keyboard focus returns to it).
    bool focus_first = false;              ///< Opened from the keyboard: its first item takes the focus.
};

/// A tab of a tab bar, remembered between frames.
struct TabState
{
    std::uint32_t id = 0;                              ///< Identifier.
    TabItemFlag flags = TabItemFlag::NONE;             ///< Flags of its last submission.
    std::uint64_t last_frame = 0;                      ///< Last frame it was submitted.
    float content_width = 0;                           ///< The width its label (and close button) asks for.
    float width = 0;                                   ///< Its width after fitting.
    float offset = 0;                                  ///< From the bar's left edge (leading and trailing tabs) or the scrolled area's.
    bool button = false;                               ///< A tab_item_button().
    std::array<char, MAXIMUM_ISSUE_NAME_SIZE> label{}; ///< Its label (cut): the tab list, tooltips.
    std::uint8_t label_size = 0;                       ///< Bytes of label used.
};

/// A tab bar, remembered between frames.
struct TabBarState
{
    std::uint32_t id = 0;                       ///< Identifier.
    TabBarFlag flags = TabBarFlag::NONE;        ///< Flags of this frame.
    std::vector<TabState> tabs{};               ///< In order: leading tabs, the others, trailing tabs.
    std::uint32_t selected = 0;                 ///< The selected tab (0: none yet).
    std::uint32_t next_selected = 0;            ///< Selected from the next frame (a click, SET_SELECTED).
    std::uint64_t last_frame = 0;               ///< Last frame it was submitted.
    std::uint64_t previous_frame = 0;           ///< The frame it was submitted before that.
    std::uint32_t window = 0xFFFFFFFFU;         ///< Its window this frame.
    UiRectangle bar{};                          ///< Its rectangle this frame.
    UiRectangle scrolling{};                    ///< Where the tabs that are neither leading nor trailing are drawn.
    float scroll = 0;                           ///< How far those tabs are scrolled (FITTING_POLICY_SCROLL).
    float scroll_target = -1;                   ///< Where they scroll to (-1: nowhere).
    float next_offset = 0;                      ///< Where a tab appearing this frame goes, in the scrolling area.
    std::uint32_t content_tab = 0;              ///< The tab whose content is being submitted (end_tab_item()).
    bool content_pushed = false;                ///< Its identifier was pushed.
    std::uint32_t dragged = 0;                  ///< The tab being dragged to another place (REORDERABLE).
    bool appearing = false;                     ///< Not submitted last frame.
};

/// The menu bar being filled (begin_menu_bar()).
struct MenuBarState
{
    bool active = false;                      ///< Between begin_menu_bar() and end_menu_bar().
    std::uint32_t window = 0xFFFFFFFFU;       ///< Its window.
    UiRectangle rectangle{};                  ///< Its rectangle.
    UiRectangle saved_clip{};                 ///< The window's content clip, restored by end_menu_bar().
    float x = 0;                              ///< Where its next menu goes.
};

/// A drag and drop in progress.
struct DragDropState
{
    bool active = false;                          ///< A payload is dragged.
    bool external = false;                        ///< From outside the UI (DragDropFlag::SOURCE_EXTERNAL).
    std::uint32_t source_id = 0;                  ///< The source item.
    std::uint32_t source_window = 0xFFFFFFFFU;    ///< Its window.
    MouseButton button = MouseButton::LEFT;       ///< The button that drags.
    DragDropFlag source_flags = DragDropFlag::NONE; ///< The source's flags.
    std::string type{};                           ///< The payload's type.
    std::vector<std::byte> data{};                ///< The payload's bytes.
    bool payload_set = false;                     ///< set_drag_drop_payload() was called during this drag.
    std::uint64_t data_frame = 0;                 ///< Last frame the source set the payload.
    std::uint64_t accepted_frame = 0;             ///< Last frame a target accepted it.
    std::uint32_t accept_id = 0;                  ///< The target accepting it this frame (the smallest).
    float accept_area = 0;                        ///< Its area.
    std::uint32_t accept_id_previous = 0;         ///< The target that accepted it last frame (it receives the delivery).
    bool delivered = false;                       ///< A target received it: the drag ends.
    bool hide_preview = false;                    ///< A target hides the source's preview tooltip this frame.
    bool hide_preview_next = false;               ///< ...next frame.
    bool in_source = false;                       ///< Between begin_drag_drop_source() and end_drag_drop_source().
    bool source_tooltip = false;                  ///< The source began a preview tooltip.
    bool in_target = false;                       ///< Between begin_drag_drop_target() and end_drag_drop_target().
    std::uint32_t target_id = 0;                  ///< The target being submitted.
    UiRectangle target_rectangle{};               ///< Its rectangle.
    std::uint32_t hold_id = 0;                    ///< The tree node or tab the payload hovers (hold to open).
    double hold_start = 0;                        ///< Since when.
    bool hold_seen = false;                       ///< Hovered this frame.
};

/// A list box's label, drawn by end_list_box().
struct ListBoxLabel
{
    std::array<char, MAXIMUM_ISSUE_NAME_SIZE> text{}; ///< The label (cut).
    std::uint8_t size = 0;                            ///< Bytes used.
    bool begun = false;                               ///< begin_list_box() began a region (and a group).
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

    /// Gives the keyboard focus to item @p id, clicked (no highlight); it stays alive this frame although its
    /// item_add() already ran.
    void take_focus(std::uint32_t id) noexcept;

    /// Whether the keyboard drives item @p id of @p window (it has the visible focus in the focused window).
    [[nodiscard]] auto keyboard_focused(const WindowState& window,
                                        std::uint32_t id) const noexcept -> bool;

    /// The presses of @p key this frame.
    [[nodiscard]] auto presses(Key key) const noexcept -> std::uint32_t;

    /// Item @p id (the last item) changed its value this frame.
    void mark_edited(std::uint32_t id) noexcept;

    // ----------------------------------------------------------------------------------------- popups

    /// Whether popup @p id is open.
    [[nodiscard]] auto popup_open(std::uint32_t id) const noexcept -> bool;

    /// Opens popup @p id at @p position at the current level (popups opened from inside a popup stay above it;
    /// others close; already open there, it stays and the popups above it close). Returns it (nullptr when the
    /// stack could not grow).
    auto open_popup(std::uint32_t id,
                    Vector2 position,
                    float minimum_width,
                    float maximum_height,
                    float flip_bottom = -1,
                    float flip_right = -1) noexcept -> PopupState*;

    /// The stack level a popup opened now goes to: above the popup the current window is in, else 0.
    [[nodiscard]] auto current_popup_level() const noexcept -> std::size_t;

    /// The level of the top-most modal, or the stack's size.
    [[nodiscard]] auto top_modal_level() const noexcept -> std::size_t;

    /// Closes the popup window @p window is in, and the menus it was opened from (a submenu closes its parent).
    void close_current_popup(std::uint32_t window) noexcept;

    /// Begins modal @p id's window, named @p name, when it is open (end_popup() then); @p open: its close button.
    auto begin_modal_popup(UiFrame& frame,
                           std::uint32_t id,
                           std::string_view name,
                           bool* open,
                           UiWindowFlag flags) noexcept -> bool;

    /// The root window whose title shows active: the focused window, or the window the focused popup (or the popup
    /// it was opened from) was opened from.
    [[nodiscard]] auto title_focus_window() const noexcept -> std::uint32_t;

    /// Begins popup @p id's window when it is open and shows something (true: end_popup() then).
    auto begin_popup(UiFrame& frame,
                     std::uint32_t id,
                     UiWindowFlag flags) noexcept -> bool;

    /// Ends the popup begin_popup() began.
    void end_popup(UiFrame& frame) noexcept;

    /// Closes the popups from stack level @p level up; the focus goes back to the first one's opener.
    void close_popups_from(std::size_t level) noexcept;

    /// The stack level of the popup whose window is @p window (or of its root), or the stack's size.
    [[nodiscard]] auto popup_level_of(std::uint32_t window) const noexcept -> std::size_t;

    /// Popups at the start of a frame: those not begun last frame close; a click outside closes those above it
    /// (never a modal); Escape closes the top one (not a modal) and Left a submenu, the keyboard focus going back to
    /// their opener. Returns whether Escape closed one.
    auto update_popups() noexcept -> bool;

    // ----------------------------------------------------------------------------------------- tooltips and drag and drop

    /// Begins a tooltip window at the mouse (appending to the frame's tooltip); @p hidden: laid out, not drawn.
    void begin_tooltip_window(UiFrame& frame,
                              bool hidden) noexcept;

    /// Whether a dragged payload has hovered item @p id long enough to open it (tree nodes, tabs); call while it is
    /// hovered.
    auto drag_hold_opens(std::uint32_t id) noexcept -> bool;

    /// The open state remembered for tree node @p id (nothing the first time).
    [[nodiscard]] auto remembered_open(std::uint32_t id) const noexcept -> std::optional<bool>;

    /// Remembers @p open for tree node @p id.
    void remember_open(std::uint32_t id,
                       bool open) noexcept;

    // ----------------------------------------------------------------------------------------- text fields

    /// Starts editing field @p id holding @p value.
    void start_text_edit(std::uint32_t id,
                         std::string_view value,
                         InputTextFlag flags,
                         bool multiline,
                         bool numeric,
                         std::size_t capacity) noexcept;

    /// Ends the edit in progress: committed (Return, Tab, a click elsewhere) or canceled (Escape).
    void end_text_edit(bool committed) noexcept;

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
    std::array<std::uint8_t, 128> key_codes_pressed{}; ///< Presses per ASCII key code (letters lowercase): shortcuts.

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

    // Input widgets.
    TextEditState text_edit{};
    std::uint32_t focus_arrived_id = 0;
    bool text_input_wanted = false;
    UiRectangle text_input_area{};
    bool applied_text_input = false;
    UiRectangle applied_text_input_area{};
    std::string composition{};
    std::int32_t composition_cursor = 0;
    Vector2 mouse_delta{};
    Vector2 previous_mouse_position{};
    bool previous_mouse_present = false;
    std::uint32_t temp_input_id = 0;
    double drag_remainder = 0;
    bool drag_started = false;
    std::vector<PopupState> popups{};
    std::vector<std::uint32_t> popups_closed_by_click{};
    float saved_hue = 0;
    float saved_saturation = 0;
    std::uint32_t saved_hue_color = 0;
    std::vector<ListBoxLabel> list_box_labels{};
    LastItem combo_item{};

    // Structure.
    IdentifierMap open_states{};
    bool next_item_has_open = false;
    bool next_item_open = false;
    UiCondition next_item_open_condition = UiCondition::ALWAYS;
    std::vector<std::uint32_t> tree_stack{};
    std::vector<TabBarState> tab_bars{};
    IdentifierMap tab_bar_lookup{};
    std::vector<std::uint32_t> tab_bar_stack{};
    MenuBarState menu_bar{};
    DrawList modal_dim{};
    std::uint64_t tooltip_frame = 0;
    std::uint32_t tooltip_key = 0;
    double tooltip_since = 0;
    bool tooltip_shown = false;
    bool tooltip_seen = false;
    std::uint32_t tooltip_depth = 0;
    DragDropState drag_drop{};
    ColorEditFlag color_options = ColorEditFlag::DISPLAY_RGB | ColorEditFlag::INTEGER_CHANNELS | ColorEditFlag::PICKER_HUE_BAR;

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

/// A text field to draw and edit (text_field()).
struct TextFieldRequest
{
    std::uint32_t id = 0;                      ///< Its identifier (registered with item_add() already).
    UiRectangle frame{};                       ///< The area the mouse uses (the frame, or the visible area).
    Vector2 origin{};                          ///< Where the text's top-left goes when not scrolled horizontally.
    UiRectangle clip{};                        ///< Where text is visible.
    std::string_view text{};                   ///< Its value (when not being edited).
    std::string_view edit_text{};              ///< Numeric fields: the text editing starts from (the value without the format's other text).
    std::string_view hint{};                   ///< Shown while it is empty and not edited.
    InputTextFlag flags = InputTextFlag::NONE; ///< Filters and behavior.
    std::size_t capacity = 0;                  ///< Largest text, bytes (0: no limit).
    float wrap_width = 0;                      ///< Multi-line word wrap width (0: none).
    bool multiline = false;                    ///< Return types line breaks, Up and Down move between lines.
    bool numeric = false;                      ///< A number's field: editing starts from edit_text.
    bool framed = true;                        ///< Draws its frame.
    bool centered = false;                     ///< The text is centered while not edited (sliders, drags).
    std::string_view label{};                  ///< For reports.
};

/// What a text field did this frame.
struct TextFieldResult
{
    bool active = false;     ///< Being edited (after this frame's input).
    bool changed = false;    ///< The text changed (it differs from the request's text).
    bool entered = false;    ///< Return confirmed it.
    bool ended = false;      ///< Editing ended this frame.
    bool committed = false;  ///< ...by Return, Tab or a click elsewhere (not Escape).
    std::string_view text{}; ///< The text after this frame (the edited one while editing; the final one when it ended).
    UiRectangle cursor{};    ///< The cursor (screen) while editing.
    Vector2 size{};          ///< The laid-out text's size.
};

/// Draws and edits a text field: activation by click, Tab or Return; mouse selection; keys, shortcuts, filters,
/// the clipboard, undo; the IME composition at the cursor; horizontal scrolling of single-line fields.
auto text_field(ContextState& state,
                WindowState& window,
                const TextFieldRequest& request) noexcept -> TextFieldResult;

} // namespace gfx::ui
