/// @file frame.cppm
/// @brief gfx.ui.context:frame: UiFrame, the immediate-mode interface. UiContext::begin_frame() returns it; every
///        window, layout call and widget of the frame is one of its member functions, until
///        UiContext::end_frame(). Every member is noexcept: misuse is reported in the frame's UiFrameReport.
///
/// The widgets of later phases are declared already (the whole surface can be reviewed): until their phase,
/// each does nothing, returns false (or an empty value) and reports UiFrameProblem::NOT_IMPLEMENTED.
export module gfx.ui.context:frame;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.handles;
import gfx.ui.draw;
import gfx.ui.style;
import :types;

namespace gfx::ui
{

struct ContextState;

} // namespace gfx::ui

export namespace gfx::ui
{

class UiContext;

/// @brief One frame of a UiContext: windows, layout and widgets, described every frame (immediate mode) while the
///        context keeps what must survive between frames (window places, the active and focused items). Use it
///        between UiContext::begin_frame() and UiContext::end_frame(), on the thread that runs the frames.
///
/// Identifiers: interactive widgets are identified by their label hashed with the identifier stack (the window,
/// push_id()). "Save##toolbar" shows "Save" but hashes the whole label; "Save###save" hashes "###save" only, so
/// the shown text may change. Two interactive items with one identifier in a frame are reported.
///
/// @code
/// gfx::ui::UiFrame& frame = ui.begin_frame(input);
/// if (frame.begin_window("Inspector"))
/// {
///     frame.text("{} objects", objects.size());
///     frame.checkbox("Visible", object.visible);
///     if (frame.button("Reset"))
///     {
///         reset(object);
///     }
/// }
/// frame.end_window();
/// const gfx::ui::UiFrameReport report = ui.end_frame();
/// @endcode
class UiFrame
{
public:
    UiFrame(const UiFrame&) = delete;
    auto operator=(const UiFrame&) -> UiFrame& = delete;

    // ===================================================================================== windows

    /// @brief Starts a window named @p name (its title; "##" hides the rest, see the class). Returns false when
    ///        it is collapsed: skip its content, but always call end_window().
    auto begin_window(std::string_view name,
                      UiWindowFlag flags = UiWindowFlag::NONE) noexcept -> bool;

    /// @brief Starts a window with a close button, which sets @p open to false. Nothing is drawn while @p open
    ///        is false (begin_window() returns false; still call end_window()).
    auto begin_window(std::string_view name,
                      bool& open,
                      UiWindowFlag flags = UiWindowFlag::NONE) noexcept -> bool;

    /// @brief Ends the window begin_window() started.
    void end_window() noexcept;

    /// @brief Starts a scrolling region inside the current window, @p size UI units (0: the space available; a
    ///        negative value: the space available minus it). Returns false when it is not visible: skip its
    ///        content, but always call end_child().
    auto begin_child(std::string_view name,
                     Vector2 size = {},
                     ChildFlag child_flags = ChildFlag::NONE,
                     UiWindowFlag flags = UiWindowFlag::NONE) noexcept -> bool;

    /// @brief Ends the child region begin_child() started (an item of its parent window).
    void end_child() noexcept;

    /// @brief Places the next window at @p position (UI units), its @p pivot there (0, 0: its top-left corner;
    ///        0.5, 0.5: its center).
    void set_next_window_position(Vector2 position,
                                  UiCondition condition = UiCondition::ALWAYS,
                                  Vector2 pivot = {}) noexcept;

    /// @brief Sizes the next window (0 on an axis: fitted to its content).
    void set_next_window_size(Vector2 size,
                              UiCondition condition = UiCondition::ALWAYS) noexcept;

    /// @brief Limits the size of the next window (a maximum of 0 or less: no limit on that axis).
    void set_next_window_size_constraints(Vector2 minimum,
                                          Vector2 maximum) noexcept;

    /// @brief Gives the next window's content this size (instead of measuring it), for scrolling.
    void set_next_window_content_size(Vector2 size) noexcept;

    /// @brief Collapses or expands the next window.
    void set_next_window_collapsed(bool collapsed,
                                   UiCondition condition = UiCondition::ALWAYS) noexcept;

    /// @brief Focuses the next window (brings it to the front).
    void set_next_window_focus() noexcept;

    /// @brief Scrolls the next window to @p scroll (a negative value leaves that axis).
    void set_next_window_scroll(Vector2 scroll) noexcept;

    /// @brief The next window's background opacity (multiplies its WINDOW_BACKGROUND color).
    void set_next_window_background_alpha(float alpha) noexcept;

    /// @brief Whether the current window appears this frame (first submitted, or after frames without).
    [[nodiscard]] auto is_window_appearing() const noexcept -> bool;

    /// @brief Whether the current window is collapsed.
    [[nodiscard]] auto is_window_collapsed() const noexcept -> bool;

    /// @brief Whether the current window (by @p flags: its root, its children, any window) has the focus.
    [[nodiscard]] auto is_window_focused(FocusedFlag flags = FocusedFlag::NONE) const noexcept -> bool;

    /// @brief Whether the mouse is over the current window (by @p flags: its root, its children, any window), and
    ///        nothing in front of it.
    [[nodiscard]] auto is_window_hovered(HoveredFlag flags = HoveredFlag::NONE) const noexcept -> bool;

    /// @brief The current window's top-left corner, UI units.
    [[nodiscard]] auto window_position() const noexcept -> Vector2;

    /// @brief The current window's size, UI units.
    [[nodiscard]] auto window_size() const noexcept -> Vector2;

    /// @brief Focuses the current window (brings it to the front).
    void set_window_focus() noexcept;

    /// @brief The draw list of the current window: custom drawing over its background, under its later items
    ///        (screen coordinates, UI units).
    [[nodiscard]] auto window_draw_list() noexcept -> DrawList&;

    /// @brief A draw list under every window.
    [[nodiscard]] auto background_draw_list() noexcept -> DrawList&;

    /// @brief A draw list over every window.
    [[nodiscard]] auto foreground_draw_list() noexcept -> DrawList&;

    // ===================================================================================== scrolling

    /// @brief Horizontal scroll of the current window.
    [[nodiscard]] auto scroll_x() const noexcept -> float;

    /// @brief Vertical scroll of the current window.
    [[nodiscard]] auto scroll_y() const noexcept -> float;

    /// @brief Largest horizontal scroll of the current window (0: its content fits).
    [[nodiscard]] auto scroll_maximum_x() const noexcept -> float;

    /// @brief Largest vertical scroll of the current window (0: its content fits).
    [[nodiscard]] auto scroll_maximum_y() const noexcept -> float;

    /// @brief Scrolls the current window horizontally (applied next frame, clamped).
    void set_scroll_x(float scroll) noexcept;

    /// @brief Scrolls the current window vertically (applied next frame, clamped).
    void set_scroll_y(float scroll) noexcept;

    /// @brief Scrolls the current window so the cursor's line is at @p ratio of its height (0 top, 0.5 center).
    void set_scroll_here_y(float ratio = 0.5F) noexcept;

    // ===================================================================================== layout

    /// @brief A horizontal line across the window (or the current columns).
    void separator() noexcept;

    /// @brief A separator with @p label in it.
    void separator_text(std::string_view label) noexcept;

    /// @brief Puts the next item on the line of the previous one: @p offset_from_start from the window's
    ///        content start (0: right after it), @p spacing after it (negative: the style's item spacing).
    void same_line(float offset_from_start = 0.0F,
                   float spacing = -1.0F) noexcept;

    /// @brief Ends the line (an empty line when it has no item).
    void new_line() noexcept;

    /// @brief Vertical space (the style's item spacing).
    void spacing() noexcept;

    /// @brief An empty item of @p size UI units.
    void dummy(Vector2 size) noexcept;

    /// @brief Moves the next lines right by @p width (0: the style's indent spacing).
    void indent(float width = 0.0F) noexcept;

    /// @brief Moves the next lines left by @p width (0: the style's indent spacing).
    void unindent(float width = 0.0F) noexcept;

    /// @brief Starts a group: its items become one item (for same_line() and the item queries) at end_group().
    void begin_group() noexcept;

    /// @brief Ends the group begin_group() started.
    void end_group() noexcept;

    /// @brief Splits the window into @p count columns (1 ends them); next_column() moves to the next. @p id
    ///        tells column sets of one window apart.
    void columns(std::uint32_t count = 1,
                 std::string_view id = {},
                 bool borders = true) noexcept;

    /// @brief Moves to the next column (and to a new row after the last).
    void next_column() noexcept;

    /// @brief Index of the current column (0 without columns).
    [[nodiscard]] auto column_index() const noexcept -> std::uint32_t;

    /// @brief Columns of the current set (1 without columns).
    [[nodiscard]] auto column_count() const noexcept -> std::uint32_t;

    /// @brief Width of column @p index (the current one when negative).
    [[nodiscard]] auto column_width(std::int32_t index = -1) const noexcept -> float;

    /// @brief Where the next item goes, from the window's content start (scrolling included), UI units.
    [[nodiscard]] auto cursor_position() const noexcept -> Vector2;

    /// @brief Moves where the next item goes, from the window's content start.
    void set_cursor_position(Vector2 position) noexcept;

    /// @brief Where the next item goes, in screen coordinates (UI units).
    [[nodiscard]] auto cursor_screen_position() const noexcept -> Vector2;

    /// @brief Moves where the next item goes, in screen coordinates.
    void set_cursor_screen_position(Vector2 position) noexcept;

    /// @brief Space from the cursor to the end of the window's content region, UI units.
    [[nodiscard]] auto content_region_available() const noexcept -> Vector2;

    /// @brief Moves the text baseline of the line down to align text with framed widgets that follow.
    void align_text_to_frame_padding() noexcept;

    /// @brief Height of a line of text in the current font.
    [[nodiscard]] auto text_line_height() const noexcept -> float;

    /// @brief Height of a line of text plus the item spacing.
    [[nodiscard]] auto text_line_height_with_spacing() const noexcept -> float;

    /// @brief Height of a framed widget (a line of text plus the frame padding).
    [[nodiscard]] auto frame_height() const noexcept -> float;

    /// @brief Height of a framed widget plus the item spacing.
    [[nodiscard]] auto frame_height_with_spacing() const noexcept -> float;

    /// @brief Width of the next framed widgets: @p width > 0 UI units, 0 the default, < 0 the space available
    ///        minus it.
    void push_item_width(float width) noexcept;

    /// @brief Restores the item width of before the last push_item_width().
    void pop_item_width() noexcept;

    /// @brief Width of the next framed widget only (as push_item_width()).
    void set_next_item_width(float width) noexcept;

    /// @brief Width the next framed widget gets.
    [[nodiscard]] auto calculate_item_width() const noexcept -> float;

    /// @brief Wraps text at @p wrap_x from the window's content start (0: the end of the content region; < 0:
    ///        no wrapping).
    void push_text_wrap_position(float wrap_x = 0.0F) noexcept;

    /// @brief Restores the wrap position of before the last push_text_wrap_position().
    void pop_text_wrap_position() noexcept;

    // ===================================================================================== identifiers

    /// @brief Hashes @p name into the identifiers of the next widgets (loop items with the same labels).
    void push_id(std::string_view name) noexcept;

    /// @brief Hashes @p value into the identifiers of the next widgets.
    void push_id(std::int32_t value) noexcept;

    /// @brief Restores the identifier stack of before the last push_id().
    void pop_id() noexcept;

    /// @brief The identifier a widget labeled @p label gets here.
    [[nodiscard]] auto identifier(std::string_view label) noexcept -> WidgetIdentifier;

    // ===================================================================================== text

    /// @brief Text, as is (no formatting).
    void text_unformatted(std::string_view text) noexcept;

    /// @brief Formatted text (std::format).
    template<class... Arguments>
    void text(std::format_string<const Arguments&...> format,
              const Arguments&... arguments) noexcept
    {
        text_unformatted(format_text(format, arguments...));
    }

    /// @brief Formatted text in @p color.
    template<class... Arguments>
    void text_colored(Color color,
                      std::format_string<const Arguments&...> format,
                      const Arguments&... arguments) noexcept
    {
        text_colored_unformatted(color, format_text(format, arguments...));
    }

    /// @brief Formatted text in the TEXT_DISABLED color.
    template<class... Arguments>
    void text_disabled(std::format_string<const Arguments&...> format,
                       const Arguments&... arguments) noexcept
    {
        text_disabled_unformatted(format_text(format, arguments...));
    }

    /// @brief Formatted text wrapped at the end of the content region.
    template<class... Arguments>
    void text_wrapped(std::format_string<const Arguments&...> format,
                      const Arguments&... arguments) noexcept
    {
        text_wrapped_unformatted(format_text(format, arguments...));
    }

    /// @brief Formatted text after a bullet.
    template<class... Arguments>
    void bullet_text(std::format_string<const Arguments&...> format,
                     const Arguments&... arguments) noexcept
    {
        bullet_text_unformatted(format_text(format, arguments...));
    }

    /// @brief A value (formatted) with its @p label on the right, aligned as framed widgets are.
    template<class... Arguments>
    void label(std::string_view label,
               std::format_string<const Arguments&...> format,
               const Arguments&... arguments) noexcept
    {
        label_unformatted(label, format_text(format, arguments...));
    }

    /// @brief Text in @p color, as is.
    void text_colored_unformatted(Color color,
                                  std::string_view text) noexcept;

    /// @brief Text in the TEXT_DISABLED color, as is.
    void text_disabled_unformatted(std::string_view text) noexcept;

    /// @brief Text wrapped at the end of the content region, as is.
    void text_wrapped_unformatted(std::string_view text) noexcept;

    /// @brief Text after a bullet, as is.
    void bullet_text_unformatted(std::string_view text) noexcept;

    /// @brief A value with its @p label on the right, as is.
    void label_unformatted(std::string_view label,
                           std::string_view value) noexcept;

    /// @brief A bullet, and the cursor after it on the same line.
    void bullet() noexcept;

    /// @brief Size of @p text in the current font, UI units (@p wrap_width > 0: wrapped at it).
    [[nodiscard]] auto calculate_text_size(std::string_view text,
                                           bool hide_text_after_double_hash = false,
                                           float wrap_width = -1.0F) noexcept -> Vector2;

    // ===================================================================================== widgets

    /// @brief A button: true when it is clicked (released over it, or activated with the keyboard). @p size 0 on
    ///        an axis fits the label; a negative value is the space available minus it.
    auto button(std::string_view label,
                Vector2 size = {}) noexcept -> bool;

    /// @brief A button without vertical frame padding (fits in a line of text).
    auto small_button(std::string_view label) noexcept -> bool;

    /// @brief An invisible clickable area of @p size (> 0): custom widgets.
    auto invisible_button(std::string_view id,
                          Vector2 size,
                          ButtonFlag flags = ButtonFlag::NONE) noexcept -> bool;

    /// @brief A square button with an arrow pointing to @p direction.
    auto arrow_button(std::string_view id,
                      ArrowDirection direction) noexcept -> bool;

    /// @brief A checkbox: clicking it toggles @p value; true when it changed.
    auto checkbox(std::string_view label,
                  bool& value) noexcept -> bool;

    /// @brief A checkbox showing whether @p flags has every bit of @p value; clicking it sets or clears them.
    template<BitmaskEnumeration Flags>
    auto checkbox_flags(std::string_view label,
                        Flags& flags,
                        Flags value) noexcept -> bool
    {
        bool set = has_all(flags, value);
        if (!checkbox(label, set))
        {
            return false;
        }
        flags = set ? (flags | value) : (flags & ~value);
        return true;
    }

    /// @brief A radio button, drawn selected when @p active; true when it is clicked.
    auto radio_button(std::string_view label,
                      bool active) noexcept -> bool;

    /// @brief A radio button of the group @p value belongs to: selected when @p value equals @p button_value,
    ///        which clicking it assigns; true when it is clicked.
    template<std::equality_comparable Value>
        requires std::is_nothrow_copy_assignable_v<Value>
    auto radio_button(std::string_view label,
                      Value& value,
                      const Value& button_value) noexcept -> bool
    {
        if (!radio_button(label, value == button_value))
        {
            return false;
        }
        value = button_value;
        return true;
    }

    /// @brief An on/off switch: clicking it toggles @p value; true when it changed (the request's `switch`).
    auto toggle(std::string_view label,
                bool& value) noexcept -> bool;

    /// @brief A line that can be selected, highlighted when @p selected; true when it is clicked. @p size 0 on
    ///        an axis: the space available (width), a line of text (height).
    auto selectable(std::string_view label,
                    bool selected = false,
                    SelectableFlag flags = SelectableFlag::NONE,
                    Vector2 size = {}) noexcept -> bool;

    /// @brief A selectable toggling @p *selected when it is clicked; true then (a pointer: a bool lvalue would make
    ///        the overload with `bool selected` ambiguous).
    auto selectable(std::string_view label,
                    bool* selected,
                    SelectableFlag flags = SelectableFlag::NONE,
                    Vector2 size = {}) noexcept -> bool;

    /// @brief Disables the next items while @p disabled (drawn faded, no input) until end_disabled().
    void begin_disabled(bool disabled = true) noexcept;

    /// @brief Ends what begin_disabled() started.
    void end_disabled() noexcept;

    // ===================================================================================== style and fonts

    /// @brief Draws the next text in @p font (a font of the context's library).
    void push_font(FontHandle font) noexcept;

    /// @brief Restores the font of before the last push_font().
    void pop_font() noexcept;

    /// @brief The current font.
    [[nodiscard]] auto font() const noexcept -> FontHandle;

    /// @brief Size of the current font, UI units per em.
    [[nodiscard]] auto font_size() const noexcept -> float;

    /// @brief Draws @p element in @p color until pop_style_color().
    void push_style_color(StyleColor element,
                          Color color) noexcept;

    /// @brief Restores the last @p count colors pushed.
    void pop_style_color(std::uint32_t count = 1) noexcept;

    /// @brief Changes the float style variable @p variable until pop_style_variable().
    void push_style_variable(StyleVariable variable,
                             float value) noexcept;

    /// @brief Changes the Vector2 style variable @p variable until pop_style_variable().
    void push_style_variable(StyleVariable variable,
                             Vector2 value) noexcept;

    /// @brief Restores the last @p count style variables pushed.
    void pop_style_variable(std::uint32_t count = 1) noexcept;

    /// @brief The style in effect (the theme's, with the variables pushed).
    [[nodiscard]] auto style() const noexcept -> const Style&;

    /// @brief The color of @p element in effect.
    [[nodiscard]] auto style_color(StyleColor element) const noexcept -> Color;

    // ===================================================================================== items

    /// @brief Whether the mouse is over the last item (and nothing in front of it, by @p flags).
    [[nodiscard]] auto is_item_hovered(HoveredFlag flags = HoveredFlag::NONE) const noexcept -> bool;

    /// @brief Whether the last item is active (held, being edited).
    [[nodiscard]] auto is_item_active() const noexcept -> bool;

    /// @brief Whether the last item has the keyboard focus.
    [[nodiscard]] auto is_item_focused() const noexcept -> bool;

    /// @brief Whether @p button was clicked over the last item this frame.
    [[nodiscard]] auto is_item_clicked(MouseButton button = MouseButton::LEFT) const noexcept -> bool;

    /// @brief Whether the last item is visible (not clipped).
    [[nodiscard]] auto is_item_visible() const noexcept -> bool;

    /// @brief Whether the last item changed its value this frame.
    [[nodiscard]] auto is_item_edited() const noexcept -> bool;

    /// @brief Whether the last item became active this frame.
    [[nodiscard]] auto is_item_activated() const noexcept -> bool;

    /// @brief Whether the last item stopped being active this frame.
    [[nodiscard]] auto is_item_deactivated() const noexcept -> bool;

    /// @brief Whether the last item stopped being active this frame after changing its value.
    [[nodiscard]] auto is_item_deactivated_after_edit() const noexcept -> bool;

    /// @brief Whether the mouse is over any item.
    [[nodiscard]] auto is_any_item_hovered() const noexcept -> bool;

    /// @brief Whether any item is active.
    [[nodiscard]] auto is_any_item_active() const noexcept -> bool;

    /// @brief Whether any item has the keyboard focus.
    [[nodiscard]] auto is_any_item_focused() const noexcept -> bool;

    /// @brief The last item's rectangle, screen coordinates (UI units).
    [[nodiscard]] auto item_rectangle() const noexcept -> UiRectangle;

    /// @brief Gives the keyboard focus to the last item when its window appears and nothing has it.
    void set_item_default_focus() noexcept;

    /// @brief Gives the keyboard focus to the next interactive item.
    void set_keyboard_focus_here() noexcept;

    // ===================================================================================== keyboard and mouse

    /// @brief Whether @p key is held.
    [[nodiscard]] auto is_key_down(Key key) const noexcept -> bool;

    /// @brief Whether @p key was pressed this frame (also on key repeat when @p repeat).
    [[nodiscard]] auto is_key_pressed(Key key,
                                      bool repeat = true) const noexcept -> bool;

    /// @brief Whether @p key was released this frame.
    [[nodiscard]] auto is_key_released(Key key) const noexcept -> bool;

    /// @brief Modifiers held.
    [[nodiscard]] auto key_modifiers() const noexcept -> KeyModifier;

    /// @brief Whether @p button is held.
    [[nodiscard]] auto is_mouse_down(MouseButton button) const noexcept -> bool;

    /// @brief Whether @p button was pressed this frame (also on repeats while held, when @p repeat).
    [[nodiscard]] auto is_mouse_clicked(MouseButton button,
                                        bool repeat = false) const noexcept -> bool;

    /// @brief Whether @p button was released this frame.
    [[nodiscard]] auto is_mouse_released(MouseButton button) const noexcept -> bool;

    /// @brief Whether @p button was pressed for the second time of a double click this frame.
    [[nodiscard]] auto is_mouse_double_clicked(MouseButton button) const noexcept -> bool;

    /// @brief Whether @p button is held and the mouse moved more than @p threshold (negative: the context's
    ///        drag threshold) since it was pressed.
    [[nodiscard]] auto is_mouse_dragging(MouseButton button,
                                         float threshold = -1.0F) const noexcept -> bool;

    /// @brief How far the mouse moved since @p button was pressed (zero below @p threshold).
    [[nodiscard]] auto mouse_drag_delta(MouseButton button = MouseButton::LEFT,
                                        float threshold = -1.0F) const noexcept -> Vector2;

    /// @brief Mouse position, UI units.
    [[nodiscard]] auto mouse_position() const noexcept -> Vector2;

    /// @brief Wheel notches of the frame.
    [[nodiscard]] auto mouse_wheel() const noexcept -> Vector2;

    /// @brief Whether the mouse is in the rectangle from @p minimum to @p maximum (and in the current clip
    ///        rectangle when @p clip).
    [[nodiscard]] auto is_mouse_hovering_rectangle(Vector2 minimum,
                                                   Vector2 maximum,
                                                   bool clip = true) const noexcept -> bool;

    /// @brief Shows the mouse cursor as @p shape this frame.
    void set_mouse_cursor(CursorShape shape) noexcept;

    /// @brief The mouse cursor shape of this frame so far.
    [[nodiscard]] auto mouse_cursor() const noexcept -> CursorShape;

    /// @brief Whether the UI uses the mouse (over a window, an item held): the application should ignore it.
    [[nodiscard]] auto wants_mouse() const noexcept -> bool;

    /// @brief Whether the UI uses the keyboard (an item has the keyboard focus): the application should ignore it.
    [[nodiscard]] auto wants_keyboard() const noexcept -> bool;

    /// @brief Seconds of the frame (UiFrameInput::time).
    [[nodiscard]] auto time() const noexcept -> double;

    /// @brief Seconds since the previous frame.
    [[nodiscard]] auto delta_time() const noexcept -> float;

    /// @brief Frames begun since the context was made (this one included).
    [[nodiscard]] auto frame_count() const noexcept -> std::uint64_t;

    /// @brief Size of the display, UI units.
    [[nodiscard]] auto display_size() const noexcept -> Vector2;

    // ===================================================================================== diagnostics

    /// @brief A window showing the previous frame's report and statistics (the close button sets @p open).
    void show_diagnostics_window(bool& open) noexcept;

    // ===================================================================================== input widgets

    /// @brief A text field editing @p text (grown as needed); true when it changed (with
    ///        InputTextFlag::ENTER_RETURNS_TRUE: when Return confirmed it). Click or Tab into it to edit: arrows,
    ///        Control+arrows (words), Home and End, Backspace and Delete, Shift to select, Control+A, C, X, V, Z, Y;
    ///        Return confirms, Escape reverts.
    auto input_text(std::string_view label,
                    std::string& text,
                    const InputTextOptions& options = {}) noexcept -> bool;

    /// @brief A text field editing the NUL-terminated text in @p buffer (its size minus one bytes at most).
    auto input_text(std::string_view label,
                    std::span<char> buffer,
                    const InputTextOptions& options = {}) noexcept -> bool;

    /// @brief A multi-line text field, @p options.size big (0: the item width, 8 lines); Return types a line break
    ///        (Control+Return with InputTextFlag::CONTROL_ENTER_FOR_NEW_LINE), Up and Down move between lines.
    auto input_text_multiline(std::string_view label,
                              std::string& text,
                              const InputTextOptions& options = {}) noexcept -> bool;

    /// @brief A numeric text field with step buttons (- and +, Control: @p step_fast; @p step 0: no buttons);
    ///        true when @p value changed. @p format is a std::format format with one field ("{:.2f} m/s"); empty:
    ///        DEFAULT_INTEGER_FORMAT, DEFAULT_FLOAT_FORMAT or DEFAULT_DOUBLE_FORMAT.
    template<ArithmeticValue Value>
    auto input_number(std::string_view label,
                      Value& value,
                      Value step = Value{},
                      Value step_fast = Value{},
                      std::string_view format = {},
                      InputTextFlag flags = InputTextFlag::NONE) noexcept -> bool
    {
        NumberField field = NumberField::make(value, Value{}, Value{});
        if (!number_input(label, field, NumberField::wrap(step), NumberField::wrap(step_fast), format, flags))
        {
            return false;
        }
        value = field.as<Value>();
        return true;
    }

    /// @brief A slider from @p minimum to @p maximum (drag it, or Control+click or double-click it to type a
    ///        value); true when @p value changed. @p format as input_number()'s; the value is rounded to the format's
    ///        precision unless SliderFlag::NO_ROUND_TO_FORMAT.
    template<ArithmeticValue Value>
    auto slider(std::string_view label,
                Value& value,
                Value minimum,
                Value maximum,
                std::string_view format = {},
                SliderFlag flags = SliderFlag::NONE) noexcept -> bool
    {
        NumberField field = NumberField::make(value, minimum, maximum);
        if (!number_slider(label, field, format, flags))
        {
            return false;
        }
        value = field.as<Value>();
        return true;
    }

    /// @brief One slider per element of @p values (std::array, std::span, std::vector, a C array), side by side in
    ///        the item width.
    template<EditableNumbers Values>
    auto slider(std::string_view label,
                Values&& values,
                std::ranges::range_value_t<Values> minimum,
                std::ranges::range_value_t<Values> maximum,
                std::string_view format = {},
                SliderFlag flags = SliderFlag::NONE) noexcept -> bool
    {
        bool changed = false;
        const auto count = static_cast<std::size_t>(std::ranges::size(values));
        auto* const numbers = std::ranges::data(values);
        const float width = begin_components(label, count);
        for (std::size_t component = 0; component < count; ++component)
        {
            next_component(component, width);
            changed = slider("##component", numbers[component], minimum, maximum, format, flags) || changed;
        }
        end_components(label, count);
        return changed;
    }

    /// @brief A value changed by dragging horizontally, @p speed per UI unit (Shift faster, Alt slower), clamped to
    ///        @p minimum..@p maximum when the minimum is below the maximum; Control+click or a double click types a
    ///        value. True when @p value changed.
    template<ArithmeticValue Value>
    auto drag(std::string_view label,
              Value& value,
              float speed = 1.0F,
              Value minimum = Value{},
              Value maximum = Value{},
              std::string_view format = {},
              SliderFlag flags = SliderFlag::NONE) noexcept -> bool
    {
        NumberField field = NumberField::make(value, minimum, maximum);
        if (!number_drag(label, field, speed, format, flags))
        {
            return false;
        }
        value = field.as<Value>();
        return true;
    }

    /// @brief One drag per element of @p values (std::array, std::span, std::vector, a C array), side by side in the
    ///        item width.
    template<EditableNumbers Values>
    auto drag(std::string_view label,
              Values&& values,
              float speed = 1.0F,
              std::ranges::range_value_t<Values> minimum = {},
              std::ranges::range_value_t<Values> maximum = {},
              std::string_view format = {},
              SliderFlag flags = SliderFlag::NONE) noexcept -> bool
    {
        bool changed = false;
        const auto count = static_cast<std::size_t>(std::ranges::size(values));
        auto* const numbers = std::ranges::data(values);
        const float width = begin_components(label, count);
        for (std::size_t component = 0; component < count; ++component)
        {
            next_component(component, width);
            changed = drag("##component", numbers[component], speed, minimum, maximum, format, flags) || changed;
        }
        end_components(label, count);
        return changed;
    }

    /// @brief A color editor: one drag per channel (or a hexadecimal field), a preview that opens a picker, the
    ///        label; true when @p color changed.
    auto color_edit(std::string_view label,
                    Color& color,
                    ColorEditFlag flags = ColorEditFlag::NONE) noexcept -> bool;

    /// @brief A color picker: a saturation/value square and a hue bar (or a hue wheel and a triangle), an alpha
    ///        bar with ColorEditFlag::ALPHA_BAR, a preview and the channels' drags; true when @p color changed.
    auto color_picker(std::string_view label,
                      Color& color,
                      ColorEditFlag flags = ColorEditFlag::NONE) noexcept -> bool;

    /// @brief A square of @p color (over a checkerboard where it is translucent), @p size big (0: a frame's height);
    ///        true when it is clicked.
    auto color_button(std::string_view id,
                      Color color,
                      ColorEditFlag flags = ColorEditFlag::NONE,
                      Vector2 size = {}) noexcept -> bool;

    /// @brief A combo box choosing @p selected among @p items (a popup lists them); true when it changed.
    template<std::ranges::random_access_range Items>
        requires TextLike<std::ranges::range_value_t<Items>>
    auto combo(std::string_view label,
               std::size_t& selected,
               const Items& items,
               ComboFlag flags = ComboFlag::NONE) noexcept -> bool
    {
        const auto count = static_cast<std::size_t>(std::ranges::size(items));
        const auto at = [&items](std::size_t index) { return std::string_view{std::ranges::begin(items)[static_cast<std::ranges::range_difference_t<const Items>>(index)]}; };
        const std::string_view preview = selected < count ? at(selected) : std::string_view{};
        if (!begin_combo(label, preview, flags))
        {
            return false;
        }
        bool changed = false;
        for (std::size_t item = 0; item < count; ++item)
        {
            push_id(static_cast<std::int32_t>(item));
            if (selectable(at(item), item == selected))
            {
                changed = changed || item != selected;
                selected = item;
            }
            if (item == selected && is_window_appearing())
            {
                set_scroll_here_y();
            }
            pop_id();
        }
        end_combo();
        return changed;
    }

    /// @brief Starts a combo box showing @p preview; while its popup is open (true), its items follow (selectables
    ///        close it when chosen), then end_combo().
    auto begin_combo(std::string_view label,
                     std::string_view preview,
                     ComboFlag flags = ComboFlag::NONE) noexcept -> bool;

    /// @brief Ends the popup begin_combo() opened (only when it returned true).
    void end_combo() noexcept;

    /// @brief A list box choosing @p selected among @p items, @p height_in_items tall (it scrolls); true when it
    ///        changed.
    template<std::ranges::random_access_range Items>
        requires TextLike<std::ranges::range_value_t<Items>>
    auto list_box(std::string_view label,
                  std::size_t& selected,
                  const Items& items,
                  std::uint32_t height_in_items = 8) noexcept -> bool
    {
        const float height = static_cast<float>(height_in_items) * text_line_height_with_spacing() + style().frame_padding.y * 2;
        if (!begin_list_box(label, {.x = 0, .y = height}))
        {
            end_list_box();
            return false;
        }
        bool changed = false;
        const auto count = static_cast<std::size_t>(std::ranges::size(items));
        const auto at = [&items](std::size_t index) { return std::string_view{std::ranges::begin(items)[static_cast<std::ranges::range_difference_t<const Items>>(index)]}; };
        for (std::size_t item = 0; item < count; ++item)
        {
            push_id(static_cast<std::int32_t>(item));
            if (selectable(at(item), item == selected))
            {
                changed = changed || item != selected;
                selected = item;
            }
            pop_id();
        }
        end_list_box();
        return changed;
    }

    /// @brief Starts a list box (a framed region that scrolls) of @p size (0: the item width, about 7 lines), its
    ///        label on the right; its items follow, then end_list_box() (always: like begin_child()).
    auto begin_list_box(std::string_view label,
                        Vector2 size = {}) noexcept -> bool;

    /// @brief Ends the list box begin_list_box() started.
    void end_list_box() noexcept;

    // ===================================================================================== structure

    /// @brief A tree node; true while it is open: its children follow (indented, under its identifier), then
    ///        tree_pop(). Its open state is remembered; a click toggles it (with OPEN_ON_ARROW, on the arrow only; with
    ///        OPEN_ON_DOUBLE_CLICK, a double click), as do Space and Return, Left closes it and Right opens it when it
    ///        has the keyboard focus, and a dragged payload held over it opens it. A LEAF is always open.
    auto tree_node(std::string_view label,
                   TreeNodeFlag flags = TreeNodeFlag::NONE) noexcept -> bool;

    /// @brief Indents and pushes @p id, as an open tree node does (tree_pop() then).
    void tree_push(std::string_view id) noexcept;

    /// @brief Ends an open tree node or a tree_push().
    void tree_pop() noexcept;

    /// @brief A collapsing header: a framed tree node spanning the width; true while it is open (no tree_pop()).
    auto collapsing_header(std::string_view label,
                           TreeNodeFlag flags = TreeNodeFlag::NONE) noexcept -> bool;

    /// @brief A collapsing header with a close button that sets @p visible to false (the request's
    ///        `collapsing_header_ex`); nothing is shown while @p visible is false.
    auto collapsing_header(std::string_view label,
                           bool& visible,
                           TreeNodeFlag flags = TreeNodeFlag::NONE) noexcept -> bool;

    /// @brief Opens or closes the next tree node or collapsing header (ONCE and FIRST_USE_EVER: unless its state is
    ///        remembered; APPEARING: when its window appears).
    void set_next_item_open(bool open,
                            UiCondition condition = UiCondition::ALWAYS) noexcept;

    /// @brief Starts a tab bar; its tabs follow while it returns true, then end_tab_bar(). Tabs are laid out from
    ///        last frame's: LEADING ones on the left, TRAILING ones on the right, the others between them, shrunk to
    ///        fit (or scrolled with FITTING_POLICY_SCROLL); REORDERABLE tabs move by dragging.
    auto begin_tab_bar(std::string_view id,
                       TabBarFlag flags = TabBarFlag::NONE) noexcept -> bool;

    /// @brief Ends the tab bar begin_tab_bar() started.
    void end_tab_bar() noexcept;

    /// @brief A tab; true while it is selected: its content follows (under its identifier), then end_tab_item().
    ///        A press selects it from the next frame; the first tab of a bar without a selection is selected at
    ///        once.
    auto begin_tab_item(std::string_view label,
                        TabItemFlag flags = TabItemFlag::NONE) noexcept -> bool;

    /// @brief A tab with a close button (or the middle button) that sets @p open to false; nothing is shown while
    ///        @p open is false.
    auto begin_tab_item(std::string_view label,
                        bool& open,
                        TabItemFlag flags = TabItemFlag::NONE) noexcept -> bool;

    /// @brief Ends the content of the tab begin_tab_item() selected.
    void end_tab_item() noexcept;

    /// @brief A tab that behaves as a button (never selected); true when it is clicked.
    auto tab_item_button(std::string_view label,
                         TabItemFlag flags = TabItemFlag::NONE) noexcept -> bool;

    /// @brief Starts the current window's menu bar (UiWindowFlag::MENU_BAR); its menus and items follow side by
    ///        side while it returns true, then end_menu_bar().
    auto begin_menu_bar() noexcept -> bool;

    /// @brief Ends the menu bar begin_menu_bar() started.
    void end_menu_bar() noexcept;

    /// @brief Starts a menu bar across the top of the display (a window that is only a menu bar); true: its menus
    ///        follow, then end_main_menu_bar().
    auto begin_main_menu_bar() noexcept -> bool;

    /// @brief Ends the menu bar begin_main_menu_bar() started.
    void end_main_menu_bar() noexcept;

    /// @brief A menu; true while it is open: its items follow, then end_menu(). In a menu bar, a press opens it
    ///        and hovering it opens it while another menu of the bar is open; in a menu, hovering its row opens it
    ///        as a submenu on the side. With the keyboard focus, Down (in a bar) or Right (in a menu) or Return opens it
    ///        with its first item focused; Up and Down move through a menu, Left closes a submenu, Escape the menu.
    auto begin_menu(std::string_view label,
                    bool enabled = true) noexcept -> bool;

    /// @brief Ends the menu begin_menu() opened.
    void end_menu() noexcept;

    /// @brief A menu item showing @p shortcut (text only: shortcuts are bound in U9) and a check mark when
    ///        @p selected; true when it is chosen, which closes its menus.
    auto menu_item(std::string_view label,
                   std::string_view shortcut = {},
                   bool selected = false,
                   bool enabled = true) noexcept -> bool;

    /// @brief A menu item toggling @p *selected when it is chosen.
    auto menu_item(std::string_view label,
                   std::string_view shortcut,
                   bool* selected,
                   bool enabled = true) noexcept -> bool;

    /// @brief Opens the popup @p id at the mouse (begin_popup() with the same identifier stack shows it), above the
    ///        popup this is called from (others close). Open already, it opens again at the mouse unless NO_REOPEN.
    void open_popup(std::string_view id,
                    PopupFlag flags = PopupFlag::NONE) noexcept;

    /// @brief The popup @p id; true while it is open: its content follows, then end_popup(). A click outside it,
    ///        Escape, a selectable or menu item in it, or not calling this for a frame closes it.
    auto begin_popup(std::string_view id,
                     UiWindowFlag flags = UiWindowFlag::NONE) noexcept -> bool;

    /// @brief A modal popup opened with open_popup(@p name): a titled window centered when it appears, which blocks
    ///        the windows and popups behind it (they are dimmed and receive no mouse input) and closes only by code
    ///        (close_current_popup()).
    auto begin_modal(std::string_view name,
                     UiWindowFlag flags = UiWindowFlag::NONE) noexcept -> bool;

    /// @brief A modal popup with a close button, which closes it and sets @p open to false.
    auto begin_modal(std::string_view name,
                     bool& open,
                     UiWindowFlag flags = UiWindowFlag::NONE) noexcept -> bool;

    /// @brief Ends the popup or modal begin_popup() or begin_modal() showed.
    void end_popup() noexcept;

    /// @brief A popup opened when the button (the right one by default) is released over the last item; its
    ///        identifier is the item's unless @p id is given.
    auto begin_popup_context_item(std::string_view id = {},
                                  PopupFlag flags = PopupFlag::MOUSE_BUTTON_RIGHT) noexcept -> bool;

    /// @brief A popup opened when the button is released over the current window (with NO_OPEN_OVER_ITEMS, over
    ///        no item).
    auto begin_popup_context_window(std::string_view id = {},
                                    PopupFlag flags = PopupFlag::MOUSE_BUTTON_RIGHT) noexcept -> bool;

    /// @brief Closes the popup the current content is in (a submenu closes the menus it came from).
    void close_current_popup() noexcept;

    /// @brief Whether the popup @p id is open at the current level (ANY_POPUP_LEVEL: at any; ANY_POPUP_IDENTIFIER:
    ///        any popup).
    [[nodiscard]] auto is_popup_open(std::string_view id,
                                     PopupFlag flags = PopupFlag::NONE) noexcept -> bool;

    /// @brief Starts a tooltip window at the mouse, over everything (never hovered); its content follows, then
    ///        end_tooltip(). Tooltips of one frame share a window.
    auto begin_tooltip() noexcept -> bool;

    /// @brief Ends the tooltip begin_tooltip() started.
    void end_tooltip() noexcept;

    /// @brief A tooltip showing @p text.
    void tooltip(std::string_view text) noexcept;

    /// @brief A tooltip shown while the last item is hovered, once the mouse has stayed still over it for
    ///        TOOLTIP_DELAY_SECONDS; true: its content follows, then end_tooltip().
    auto begin_item_tooltip() noexcept -> bool;

    /// @brief A tooltip showing @p text while the last item is hovered (as begin_item_tooltip()).
    void item_tooltip(std::string_view text) noexcept;

    /// @brief Makes the last item a drag and drop source; true while it is dragged (held past the drag threshold):
    ///        set_drag_drop_payload(), the preview's content (a tooltip at the mouse), then end_drag_drop_source().
    auto begin_drag_drop_source(DragDropFlag flags = DragDropFlag::NONE) noexcept -> bool;

    /// @brief The dragged data (copied), named by @p type (1 to MAXIMUM_PAYLOAD_TYPE_SIZE bytes); true when a target
    ///        accepted it last frame. With UiCondition::ONCE, only the first call of the drag copies.
    auto set_drag_drop_payload(std::string_view type,
                               std::span<const std::byte> data,
                               UiCondition condition = UiCondition::ALWAYS) noexcept -> bool;

    /// @brief The dragged data: a trivially copyable value.
    template<class Value>
        requires std::is_trivially_copyable_v<Value>
    auto set_drag_drop_payload(std::string_view type,
                               const Value& value,
                               UiCondition condition = UiCondition::ALWAYS) noexcept -> bool
    {
        return set_drag_drop_payload(type, std::as_bytes(std::span{&value, 1}), condition);
    }

    /// @brief Ends the source begin_drag_drop_source() started.
    void end_drag_drop_source() noexcept;

    /// @brief Makes the last item a drop target; true while a payload is dragged over it:
    ///        accept_drag_drop_payload(), then end_drag_drop_target().
    auto begin_drag_drop_target() noexcept -> bool;

    /// @brief The payload of @p type dropped on the target when the button is released (with ACCEPT_BEFORE_DELIVERY,
    ///        while it is over it too); nothing for another type. Nested targets: the smallest accepts it. The view
    ///        stays valid until the next frame.
    [[nodiscard]] auto accept_drag_drop_payload(std::string_view type,
                                                DragDropFlag flags = DragDropFlag::NONE) noexcept -> std::optional<UiDragDropPayload>;

    /// @brief Ends the target begin_drag_drop_target() started.
    void end_drag_drop_target() noexcept;

    // ===================================================================================== U7: tables and visualization

    /// @brief Starts a table of @p columns columns; its rows follow while it returns true, then end_table().
    ///        Phase U7.
    auto begin_table(std::string_view id,
                     std::uint32_t columns,
                     TableFlag flags = TableFlag::NONE,
                     Vector2 outer_size = {},
                     float inner_width = 0.0F) noexcept -> bool;

    /// @brief Ends the table begin_table() started. Phase U7.
    void end_table() noexcept;

    /// @brief Describes the next column (before the first row). Phase U7.
    void table_setup_column(std::string_view label,
                            TableColumnFlag flags = TableColumnFlag::NONE,
                            float width_or_weight = 0.0F) noexcept;

    /// @brief Keeps the first @p columns columns and @p rows rows in view while scrolling. Phase U7.
    void table_setup_scroll_freeze(std::uint32_t columns,
                                   std::uint32_t rows) noexcept;

    /// @brief A header row of the columns' labels (sorting, context menu). Phase U7.
    void table_headers_row() noexcept;

    /// @brief Starts a row at least @p minimum_height tall. Phase U7.
    void table_next_row(TableRowFlag flags = TableRowFlag::NONE,
                        float minimum_height = 0.0F) noexcept;

    /// @brief Moves to the next cell; false when its column is hidden. Phase U7.
    auto table_next_column() noexcept -> bool;

    /// @brief Moves to the cell of column @p column; false when it is hidden. Phase U7.
    auto table_set_column_index(std::uint32_t column) noexcept -> bool;

    /// @brief How the table is sorted. Phase U7.
    [[nodiscard]] auto table_sort_specifications() noexcept -> TableSortSpecifications;

    /// @brief A splitter between two areas of sizes @p first and @p second (changed by dragging), each at least
    ///        its minimum; true while it is dragged. Phase U7.
    auto splitter(std::string_view id,
                  bool vertical,
                  float thickness,
                  float& first,
                  float& second,
                  float first_minimum = 0.0F,
                  float second_minimum = 0.0F) noexcept -> bool;

    /// @brief A line plot of @p values. Phase U7.
    void plot_lines(std::string_view label,
                    std::span<const float> values,
                    const PlotOptions& options = {}) noexcept;

    /// @brief A histogram of @p values. Phase U7.
    void plot_histogram(std::string_view label,
                        std::span<const float> values,
                        const PlotOptions& options = {}) noexcept;

    /// @brief A progress bar filled to @p fraction (0 to 1), showing @p overlay (empty: the percentage). Phase U7.
    void progress_bar(float fraction,
                      Vector2 size = {},
                      std::string_view overlay = {}) noexcept;

    /// @brief The image of bindless sampled-image index @p image, @p size UI units. Phase U7.
    void image(std::uint32_t image,
               Vector2 size,
               const UiRectangle& texture_coordinates = {.left = 0, .top = 0, .right = 1, .bottom = 1},
               Color tint = {.red = 1, .green = 1, .blue = 1, .alpha = 1}) noexcept;

    /// @brief An image with rounded corners. Phase U7.
    void image_rounded(std::uint32_t image,
                       Vector2 size,
                       float rounding,
                       const UiRectangle& texture_coordinates = {.left = 0, .top = 0, .right = 1, .bottom = 1},
                       Color tint = {.red = 1, .green = 1, .blue = 1, .alpha = 1}) noexcept;

    /// @brief A button showing an image; true when it is clicked. Phase U7.
    auto image_button(std::string_view id,
                      std::uint32_t image,
                      Vector2 size,
                      const UiRectangle& texture_coordinates = {.left = 0, .top = 0, .right = 1, .bottom = 1}) noexcept -> bool;

    // ===================================================================================== U8: docking

    /// @brief A dock space filling @p size (0: the space available): windows dock into it. Phase U8.
    auto dock_space(DockSpaceIdentifier id,
                    Vector2 size = {},
                    DockNodeFlag flags = DockNodeFlag::NONE) noexcept -> DockNodeHandle;

    /// @brief Docks the next window into @p node. Phase U8.
    void set_next_window_dock(DockNodeHandle node,
                              UiCondition condition = UiCondition::ALWAYS) noexcept;

    /// @brief Whether the current window is docked. Phase U8.
    [[nodiscard]] auto is_window_docked() noexcept -> bool;

    /// @brief The node the current window is docked in (null when floating). Phase U8.
    [[nodiscard]] auto window_dock_node() noexcept -> DockNodeHandle;

    // ===================================================================================== U9: mapping and accessibility

    /// @brief Whether @p chord was pressed, routed as @p flags say (the focused window by default). Phase U9.
    auto shortcut(KeyChord chord,
                  ShortcutFlag flags = ShortcutFlag::NONE) noexcept -> bool;

    /// @brief Makes @p chord activate the next item. Phase U9.
    void set_next_item_shortcut(KeyChord chord,
                                ShortcutFlag flags = ShortcutFlag::NONE) noexcept;

    /// @brief What assistive technologies announce for the next item (instead of its label). Phase U9.
    void set_next_item_accessible_label(std::string_view label) noexcept;

    /// @brief A longer description of the next item for assistive technologies. Phase U9.
    void set_next_item_accessible_description(std::string_view description) noexcept;

    /// @brief The role of the next item for assistive technologies (instead of the widget's). Phase U9.
    void set_next_item_accessibility_role(AccessibilityRole role) noexcept;

    /// @brief Announces @p text to assistive technologies (a status message). Phase U9.
    void announce(std::string_view text,
                  AccessibilityLiveRegion live_region = AccessibilityLiveRegion::POLITE) noexcept;

private:
    friend struct ContextState;

    explicit UiFrame(ContextState& state) noexcept;

    /// Formats into the frame's buffer (or the frame arena when longer); empty when formatting failed.
    template<class... Arguments>
    [[nodiscard]] auto format_text(std::format_string<const Arguments&...> format,
                                   const Arguments&... arguments) noexcept -> std::string_view
    {
        try
        {
            const auto result = std::format_to_n(m_format_buffer.data(), static_cast<std::ptrdiff_t>(m_format_buffer.size()), format, arguments...);
            const auto size = static_cast<std::size_t>(result.size);
            if (size <= m_format_buffer.size())
            {
                return {m_format_buffer.data(), size};
            }

            // Longer than the buffer: formatted again into the frame arena (cut to the buffer when it is full).
            const std::span<char> storage = arena_characters(size);
            if (storage.empty())
            {
                return {m_format_buffer.data(), m_format_buffer.size()};
            }
            std::format_to_n(storage.data(), static_cast<std::ptrdiff_t>(storage.size()), format, arguments...);
            return {storage.data(), storage.size()};
        }
        catch (...)
        {
            report_format_failure();
            return {};
        }
    }

    /// @p size characters of the frame arena (empty when it cannot provide them).
    [[nodiscard]] auto arena_characters(std::size_t size) noexcept -> std::span<char>;

    /// Reports text that could not be formatted.
    void report_format_failure() noexcept;

    /// Reports @p operation, a widget of a later phase, and returns false.
    auto not_implemented(std::string_view operation,
                         std::string_view detail) noexcept -> bool;

    /// The numeric widgets on any arithmetic type (the templates' cores).
    auto number_input(std::string_view label,
                      NumberField& field,
                      NumberField::Value step,
                      NumberField::Value step_fast,
                      std::string_view format,
                      InputTextFlag flags) noexcept -> bool;

    auto number_slider(std::string_view label,
                       NumberField& field,
                       std::string_view format,
                       SliderFlag flags) noexcept -> bool;

    auto number_drag(std::string_view label,
                     NumberField& field,
                     float speed,
                     std::string_view format,
                     SliderFlag flags) noexcept -> bool;

    /// Components side by side (multi-value sliders and drags): a group and the label's identifier; returns each
    /// component's width.
    auto begin_components(std::string_view label,
                          std::size_t count) noexcept -> float;

    /// Moves to component @p component (on the line of the previous one, its identifier pushed).
    void next_component(std::size_t component,
                        float width) noexcept;

    /// Ends the @p count components: the label after them.
    void end_components(std::string_view label,
                        std::size_t count) noexcept;

    ContextState* m_state = nullptr;
    std::array<char, UI_FORMAT_BUFFER_CAPACITY> m_format_buffer{};
};

} // namespace gfx::ui
