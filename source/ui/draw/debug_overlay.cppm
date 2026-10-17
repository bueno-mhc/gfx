/// @file debug_overlay.cppm
/// @brief DebugOverlay: lines of text (frame metrics, or anything a program wants to show) in a panel over
///        a frame, drawn into a DrawList with a font of the application — SDF text like every text of gfx.
export module gfx.ui.debug_overlay;

import std;
import gfx.core.types;
import gfx.ui.handles;
import gfx.ui.text;
import gfx.ui.draw;

export namespace gfx::ui
{

/// @brief How a DebugOverlay looks.
struct DebugOverlayDescription
{
    bool visible = false;                                                ///< Shown before the first toggle().
    Vector2 position{.x = 8, .y = 8};                                    ///< Panel corner, pixels from the top left.
    float padding = 6;                                                   ///< Pixels between the panel's edge and the text.
    float corner_radius = 4;                                             ///< Of the panel.
    TextStyle text{};                                                    ///< Of the lines (white by default).
    Color background{.red = 0, .green = 0, .blue = 0, .alpha = 0.75F};   ///< Of the panel.
};

/// @brief Lines of text in a panel. Not thread-safe; it reuses a text buffer, so drawing the same lines
///        again allocates nothing.
///
/// @code
/// gfx::ui::DebugOverlay overlay{{.visible = true}};
/// if (frame.was_action_just_pressed("toggle_metrics"_action)) { overlay.toggle(); }
/// overlay.draw(list, text, fonts, font, metrics.text());   // then the renderer draws the list
/// @endcode
class DebugOverlay
{
public:
    /// @brief An overlay with @p description's look.
    explicit DebugOverlay(const DebugOverlayDescription& description = {})
        : m_description{description}
    {
    }

    /// @brief Whether draw() draws.
    [[nodiscard]] auto is_visible() const noexcept -> bool
    {
        return m_description.visible;
    }

    /// @brief Shows or hides the overlay.
    void set_visible(bool visible) noexcept
    {
        m_description.visible = visible;
    }

    /// @brief Shows the overlay if hidden, hides it if shown.
    void toggle() noexcept
    {
        m_description.visible = !m_description.visible;
    }

    /// @brief Moves the panel's corner to @p position.
    void set_position(Vector2 position) noexcept
    {
        m_description.position = position;
    }

    /// @brief Draws @p lines laid out by @p text in @p font (atlases from @p fonts) into @p list, and returns
    ///        the panel (empty when hidden or when there is nothing to show).
    auto draw(DrawList& list,
              TextLayoutEngine& text,
              const FontLibrary& fonts,
              FontHandle font,
              std::span<const std::string> lines) -> UiRectangle;

private:
    DebugOverlayDescription m_description{};
    std::string m_text{};
};

} // namespace gfx::ui
