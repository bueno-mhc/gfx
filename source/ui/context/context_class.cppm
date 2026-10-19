/// @file context_class.cppm
/// @brief gfx.ui.context:context: UiContext, which owns everything a UI keeps between frames (fonts, theme,
///        windows, the active and focused items, the published draw data), and UiDrawDataLease, which lets a render
///        thread read published draw data while the next frame is built.
export module gfx.ui.context:context;

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

struct ContextState;

} // namespace gfx::ui

export namespace gfx::ui
{

/// @brief Published draw data a thread reads: the context does not reuse its draw lists while a lease holds them.
///        Move-only; destroying it releases them.
class UiDrawDataLease
{
public:
    /// @brief A lease of nothing (empty draw data).
    UiDrawDataLease() noexcept = default;

    UiDrawDataLease(UiDrawDataLease&& other) noexcept;
    auto operator=(UiDrawDataLease&& other) noexcept -> UiDrawDataLease&;
    UiDrawDataLease(const UiDrawDataLease&) = delete;
    auto operator=(const UiDrawDataLease&) -> UiDrawDataLease& = delete;
    ~UiDrawDataLease();

    /// @brief The draw data (empty when nothing was published).
    [[nodiscard]] auto data() const noexcept -> const UiDrawData&;

private:
    friend class UiContext;

    UiDrawDataLease(const UiDrawData& data,
                    std::atomic<std::uint32_t>& leases) noexcept;

    void release() noexcept;

    const UiDrawData* m_data = nullptr;
    std::atomic<std::uint32_t>* m_leases = nullptr;
};

/// @brief Builds the dock layout of a dock space by code (UiContext::dock_builder()): its tree starts as one empty
///        node (root()), split into more, windows docked into its leaves. Each call changes the layout at once; the
///        windows appear docked the next time they are submitted.
///
/// @code
/// gfx::ui::UiDockBuilder builder = ui.dock_builder("editor"_dock_space);
/// const auto [left, rest] = builder.split(builder.root(), gfx::ui::ArrowDirection::LEFT, 0.25F);
/// const auto [bottom, center] = builder.split(rest, gfx::ui::ArrowDirection::DOWN, 0.3F);
/// builder.dock_window("Outliner", left);
/// builder.dock_window("Console", bottom);
/// builder.dock_window("Scene", center);
/// builder.finish();
/// @endcode
class UiDockBuilder
{
public:
    /// @brief The dock space's root node (the whole dock space before it is split).
    [[nodiscard]] auto root() const noexcept -> DockNodeHandle
    {
        return m_root;
    }

    /// @brief Splits leaf @p node: a new empty leaf on @p direction's side, @p ratio (between 0 and 1) of its size;
    ///        returns it and the rest (which keeps the leaf's windows and, in a dock space, its central role).
    /// @throws InvalidUsageError when @p node is not a leaf of this dock space, @p ratio is out of range, or after
    ///         finish().
    auto split(DockNodeHandle node,
               ArrowDirection direction,
               float ratio) -> std::pair<DockNodeHandle, DockNodeHandle>;

    /// @brief Docks the window named @p window (begin_window()'s name) into @p node as its selected tab (a split
    ///        node: into its central, else first, leaf).
    /// @throws InvalidUsageError when @p node is not a node of this dock space, @p window is empty, or after finish().
    void dock_window(std::string_view window,
                     DockNodeHandle node);

    /// @brief Ends the building (later calls throw).
    void finish() noexcept;

private:
    friend class UiContext;

    UiDockBuilder(ContextState& state,
                  std::uint32_t dock_space,
                  DockNodeHandle root) noexcept;

    /// The node of @p handle in this dock space's tree (thrown otherwise, for @p operation).
    [[nodiscard]] auto node_of(DockNodeHandle handle,
                               std::string_view operation) const -> std::uint32_t;

    ContextState* m_state = nullptr;
    std::uint32_t m_dock_space = 0;
    DockNodeHandle m_root{};
    bool m_finished = false;
};

/// @brief An immediate-mode UI: begin_frame() → windows and widgets on the UiFrame → end_frame() → draw_data()
///        for a UiRenderer. Owns its fonts (a FontLibrary), its theme and everything that survives between frames.
///        Frames run on one thread at a time; draw data may be read on another (acquire_draw_data()).
///
/// @code
/// gfx::ui::UiContext ui;
/// ui.load_font(gfx::ui::FontAtlas::load(interface_font), 16.0F);
/// gfx::ui::UiRenderer renderer{*device, ui.fonts(), {.color_format = format}};
///
/// // Each frame:
/// gfx::ui::UiFrame& frame = ui.begin_frame(input);
/// // ... windows and widgets ...
/// const gfx::ui::UiFrameReport report = ui.end_frame();
/// renderer.record(scope, ui.draw_data());
/// @endcode
class UiContext
{
public:
    /// @brief A context made as @p description says (capacities are reserved now).
    explicit UiContext(const UiContextDescription& description = {});

    UiContext(const UiContext&) = delete;
    auto operator=(const UiContext&) -> UiContext& = delete;
    ~UiContext();

    /// @brief The fonts (atlases and fonts): add them outside frames. A UiRenderer reads them.
    [[nodiscard]] auto fonts() noexcept -> FontLibrary&;

    /// @brief The fonts.
    [[nodiscard]] auto fonts() const noexcept -> const FontLibrary&;

    /// @brief Adds @p atlas and a font of it at @p size UI units per em; the first font loaded becomes the
    ///        default font.
    /// @throws InvalidUsageError for a null atlas or a size that is not positive.
    auto load_font(std::shared_ptr<const FontAtlas> atlas,
                   float size) -> FontHandle;

    /// @brief Draws text in @p font when no font is pushed.
    /// @throws InvalidUsageError when @p font names no font of fonts().
    void set_default_font(FontHandle font);

    /// @brief The font text is drawn in when no font is pushed (null: no font loaded).
    [[nodiscard]] auto default_font() const noexcept -> FontHandle;

    /// @brief Changes the look from the next frame.
    void set_theme(const Theme& theme) noexcept;

    /// @brief The look.
    [[nodiscard]] auto theme() const noexcept -> const Theme&;

    /// @brief Starts a frame with @p input. A frame still open is ended first (and reported).
    [[nodiscard]] auto begin_frame(const UiFrameInput& input) noexcept -> UiFrame&;

    /// @brief Ends the frame: closes what was left open, publishes the draw data, applies the cursor. Returns
    ///        the frame's report (its issues stay valid until the next end_frame()).
    auto end_frame() noexcept -> UiFrameReport;

    /// @brief The last published draw data (the frame thread only; valid until the second end_frame() after).
    [[nodiscard]] auto draw_data() const noexcept -> const UiDrawData&;

    /// @brief The last published draw data for any thread: its lists are not reused while the lease lives.
    ///        end_frame() waits for a lease only when two published frames are leased at once.
    [[nodiscard]] auto acquire_draw_data() const noexcept -> UiDrawDataLease;

    /// @brief The last end_frame()'s report.
    [[nodiscard]] auto last_report() const noexcept -> const UiFrameReport&;

    /// @brief Whether the UI used the mouse in the last frame (the application should ignore it).
    [[nodiscard]] auto wants_mouse() const noexcept -> bool;

    /// @brief Whether the UI used the keyboard in the last frame (the application should ignore it).
    [[nodiscard]] auto wants_keyboard() const noexcept -> bool;

    /// @brief Whether a text field wanted text input in the last frame.
    [[nodiscard]] auto wants_text_input() const noexcept -> bool;

    /// @brief The mouse cursor shape the last frame asked for.
    [[nodiscard]] auto mouse_cursor() const noexcept -> CursorShape;

    /// @brief Frames begun since the context was made.
    [[nodiscard]] auto frame_count() const noexcept -> std::uint64_t;

    /// @brief Builds the layout of @p dock_space by code: its tree is emptied (its windows undocked) and the builder
    ///        starts from its root. Call it outside a frame, or in a frame before the dock space is submitted.
    /// @throws InvalidUsageError for a null identifier.
    [[nodiscard]] auto dock_builder(DockSpaceIdentifier dock_space) -> UiDockBuilder;

    /// @brief The layout as JSON (format LAYOUT_FORMAT, version LAYOUT_FORMAT_VERSION): the windows' places, sizes
    ///        and collapsed states, every dock tree (dock spaces' and floating ones, with their splits, ratios, tabs
    ///        and selected tabs) and the tables' columns (widths, weights, order, visibility, sort). Windows and
    ///        tables with NO_SAVED_SETTINGS, child regions, popups and tooltips are left out.
    [[nodiscard]] auto save_layout() const -> std::string;

    /// @brief Restores what save_layout() saved (call it outside a frame): every part or nothing; an error message
    ///        (with the JSON path of the problem) when @p json is not such a layout.
    /// @throws InvalidUsageError during a frame.
    auto load_layout(std::string_view json) -> std::expected<void, std::string>;

private:
    std::unique_ptr<ContextState> m_state;
};

} // namespace gfx::ui
