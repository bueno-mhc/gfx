/// @file debug_overlay.cppm
/// @brief DebugOverlay: lines of text (frame metrics, or anything a debug build wants to show) drawn
///        into a rendering pass with a built-in 5 x 7 pixel font, as cleared rectangles: no shader, no
///        pipeline and no font texture, so it works with any color format and on any backend.
///        Debug builds only: in other builds it draws nothing.
export module gfx.interface:debug_overlay;

import std;
import gfx.enums;
import :common_types;
import :commands;
import :recorder;

namespace gfx::debug_font
{

/// @brief The pattern of one character: 7 rows of 5 columns, '#' lit and '.' unlit.
struct GlyphPattern
{
    char character = ' ';
    std::array<std::string_view, 7> rows{};
};

/// @brief Printable ASCII, from ' ' to '~'.
inline constexpr std::array<GlyphPattern, 95> PATTERNS{{
    {' ', {".....", ".....", ".....", ".....", ".....", ".....", "....."}},
    {'!', {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}},
    {'"', {".#.#.", ".#.#.", ".#.#.", ".....", ".....", ".....", "....."}},
    {'#', {".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#."}},
    {'$', {"..#..", ".####", "#.#..", ".###.", "..#.#", "####.", "..#.."}},
    {'%', {"##...", "##..#", "...#.", "..#..", ".#...", "#..##", "...##"}},
    {'&', {".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#"}},
    {'\'', {"..#..", "..#..", ".#...", ".....", ".....", ".....", "....."}},
    {'(', {"...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."}},
    {')', {".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."}},
    {'*', {".....", "..#..", "#.#.#", ".###.", "#.#.#", "..#..", "....."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {',', {".....", ".....", ".....", ".....", ".##..", "..#..", ".#..."}},
    {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
    {'.', {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}},
    {'/', {".....", "....#", "...#.", "..#..", ".#...", "#....", "....."}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
    {':', {".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."}},
    {';', {".....", ".##..", ".##..", ".....", ".##..", "..#..", ".#..."}},
    {'<', {"...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#."}},
    {'=', {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
    {'>', {".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..."}},
    {'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
    {'@', {".###.", "#...#", "....#", ".##.#", "#.#.#", "#.#.#", ".###."}},
    {'A', {".###.", "#...#", "#...#", "#...#", "#####", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'[', {".###.", ".#...", ".#...", ".#...", ".#...", ".#...", ".###."}},
    {'\\', {".....", "#....", ".#...", "..#..", "...#.", "....#", "....."}},
    {']', {".###.", "...#.", "...#.", "...#.", "...#.", "...#.", ".###."}},
    {'^', {"..#..", ".#.#.", "#...#", ".....", ".....", ".....", "....."}},
    {'_', {".....", ".....", ".....", ".....", ".....", ".....", "#####"}},
    {'`', {".#...", "..#..", "...#.", ".....", ".....", ".....", "....."}},
    {'a', {".....", ".....", ".###.", "....#", ".####", "#...#", ".####"}},
    {'b', {"#....", "#....", "#.##.", "##..#", "#...#", "#...#", "####."}},
    {'c', {".....", ".....", ".###.", "#....", "#....", "#...#", ".###."}},
    {'d', {"....#", "....#", ".##.#", "#..##", "#...#", "#...#", ".####"}},
    {'e', {".....", ".....", ".###.", "#...#", "#####", "#....", ".###."}},
    {'f', {"..##.", ".#..#", ".#...", "###..", ".#...", ".#...", ".#..."}},
    {'g', {".....", ".####", "#...#", "#...#", ".####", "....#", ".###."}},
    {'h', {"#....", "#....", "#.##.", "##..#", "#...#", "#...#", "#...#"}},
    {'i', {"..#..", ".....", ".##..", "..#..", "..#..", "..#..", ".###."}},
    {'j', {"...#.", ".....", "..##.", "...#.", "...#.", "#..#.", ".##.."}},
    {'k', {"#....", "#....", "#..#.", "#.#..", "##...", "#.#..", "#..#."}},
    {'l', {".##..", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'m', {".....", ".....", "##.#.", "#.#.#", "#.#.#", "#...#", "#...#"}},
    {'n', {".....", ".....", "#.##.", "##..#", "#...#", "#...#", "#...#"}},
    {'o', {".....", ".....", ".###.", "#...#", "#...#", "#...#", ".###."}},
    {'p', {".....", ".....", "####.", "#...#", "####.", "#....", "#...."}},
    {'q', {".....", ".....", ".##.#", "#..##", ".####", "....#", "....#"}},
    {'r', {".....", ".....", "#.##.", "##..#", "#....", "#....", "#...."}},
    {'s', {".....", ".....", ".###.", "#....", ".###.", "....#", "####."}},
    {'t', {".#...", ".#...", "###..", ".#...", ".#...", ".#..#", "..##."}},
    {'u', {".....", ".....", "#...#", "#...#", "#...#", "#..##", ".##.#"}},
    {'v', {".....", ".....", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'w', {".....", ".....", "#...#", "#...#", "#.#.#", "#.#.#", ".#.#."}},
    {'x', {".....", ".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#"}},
    {'y', {".....", ".....", "#...#", "#...#", ".####", "....#", ".###."}},
    {'z', {".....", ".....", "#####", "...#.", "..#..", ".#...", "#####"}},
    {'{', {"...#.", "..#..", "..#..", ".#...", "..#..", "..#..", "...#."}},
    {'|', {"..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'}', {".#...", "..#..", "..#..", "...#.", "..#..", "..#..", ".#..."}},
    {'~', {".....", ".....", ".#...", "#.#.#", "...#.", ".....", "....."}},
}};

/// @brief Reached at compile time only for a malformed pattern (the compiler names this function).
inline void debug_font_pattern_is_malformed() {}

/// @brief Each glyph as 7 rows of bits, the leftmost column in bit 4; checked when the table is built:
///        characters in order, 5 columns of '#' or '.' per row.
consteval auto
make_glyphs() -> std::array<std::array<std::uint8_t, 7>, 95>
{
    std::array<std::array<std::uint8_t, 7>, 95> glyphs{};
    for (std::size_t index = 0; index < PATTERNS.size(); ++index)
    {
        const GlyphPattern& pattern = PATTERNS[index];
        if (static_cast<std::size_t>(pattern.character) != index + 32)
        {
            debug_font_pattern_is_malformed();
        }
        for (std::size_t row = 0; row < 7; ++row)
        {
            const std::string_view text = pattern.rows[row];
            if (text.size() != 5)
            {
                debug_font_pattern_is_malformed();
            }
            std::uint8_t bits = 0;
            for (std::size_t column = 0; column < 5; ++column)
            {
                if (text[column] != '#' && text[column] != '.')
                {
                    debug_font_pattern_is_malformed();
                }
                bits = static_cast<std::uint8_t>(bits | (text[column] == '#' ? 0x10U >> column : 0U));
            }
            glyphs[index][row] = bits;
        }
    }
    return glyphs;
}

inline constexpr std::array<std::array<std::uint8_t, 7>, 95> GLYPHS = make_glyphs();

static_assert(GLYPHS['?' - 32][0] == 0b01110 && GLYPHS['|' - 32][6] == 0b00100, "the font table is built as written");

} // namespace gfx::debug_font

export namespace gfx
{

/// @brief How a DebugOverlay looks.
struct DebugOverlayDescription
{
    bool visible = false;                                           ///< Shown before the first toggle().
    std::uint32_t scale = 2;                                        ///< Screen pixels per font pixel (at least 1).
    Offset position{.x = 8, .y = 8, .z = 0};                        ///< Panel corner, from the drawn area's corner, in pixels.
    std::array<float, 4> text_color{1.0F, 1.0F, 1.0F, 1.0F};        ///< RGBA of the text.
    std::array<float, 4> background_color{0.0F, 0.0F, 0.0F, 1.0F}; ///< RGBA of the panel behind it.
};

/// @brief Lines of text drawn over a frame: a solid panel and characters of a built-in 5 x 7 pixel
///        font (printable ASCII; other characters show as '?'). It draws with two
///        RenderingCommands::clear_attachments() calls, which commands statistics count as transfers.
///        Debug builds only (enabled()): in other builds draw() returns at once, so code using it builds
///        unchanged. Not thread-safe; one overlay per recording thread.
///
/// @code
/// gfx::DebugOverlay overlay{{.visible = true}};
/// if (frame.was_action_just_pressed("toggle_metrics"_action)) { overlay.toggle(); }
/// overlay.draw(scope, render_area, metrics.text());     // inside the frame's rendering pass, last
/// @endcode
class DebugOverlay
{
public:
    static constexpr std::int32_t GLYPH_WIDTH = 5;   ///< Font pixels of a glyph's width.
    static constexpr std::int32_t GLYPH_HEIGHT = 7;  ///< Font pixels of a glyph's height.
    static constexpr std::int32_t GLYPH_ADVANCE = 6; ///< Font pixels from one character to the next.
    static constexpr std::int32_t LINE_ADVANCE = 9;  ///< Font pixels from one line to the next.
    static constexpr std::int32_t PADDING = 3;       ///< Font pixels between the panel's edge and the text.

    /// @brief An overlay with @p description's look.
    explicit DebugOverlay(const DebugOverlayDescription& description = {})
        : m_description{description}
    {
        m_description.scale = std::max(m_description.scale, 1U);
    }

    /// @brief Whether overlays draw in this build (Debug builds).
    [[nodiscard]] static constexpr auto enabled() noexcept -> bool
    {
        return DEBUG_BUILD;
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

    /// @brief Screen pixels per font pixel.
    [[nodiscard]] auto scale() const noexcept -> std::uint32_t
    {
        return m_description.scale;
    }

    /// @brief Changes the screen pixels per font pixel (e.g. from the window's content scale).
    void set_scale(std::uint32_t scale) noexcept
    {
        m_description.scale = std::max(scale, 1U);
    }

    /// @brief Size in pixels of the panel that shows @p lines.
    [[nodiscard]] auto measure(std::span<const std::string> lines) const noexcept -> Extent
    {
        if (lines.empty())
        {
            return Extent{.width = 0, .height = 0, .depth = 1};
        }
        std::size_t longest = 0;
        for (const std::string& line : lines)
        {
            longest = std::max(longest, line.size());
        }
        const auto scale = static_cast<std::int64_t>(m_description.scale);
        const std::int64_t width = (static_cast<std::int64_t>(longest) * GLYPH_ADVANCE - (longest > 0 ? 1 : 0) + 2 * PADDING) * scale;
        const std::int64_t height = (static_cast<std::int64_t>(lines.size()) * LINE_ADVANCE - (LINE_ADVANCE - GLYPH_HEIGHT) + 2 * PADDING) * scale;
        return Extent{.width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height), .depth = 1};
    }

    /// @brief Draws @p lines in the current rendering pass of @p commands, into color attachment
    ///        @p color_attachment, clipped to @p area (which must lie inside the pass's render area).
    ///        Nothing is drawn when the overlay is hidden, or in builds other than Debug.
    void draw(RenderingCommands& commands,
              const Rectangle& area,
              std::span<const std::string> lines,
              std::uint32_t color_attachment = 0)
    {
        // Debug builds only, when shown.
        if constexpr (!DEBUG_BUILD)
        {
            return;
        }
        if (!m_description.visible || lines.empty())
        {
            return;
        }

        // The panel, clipped to the area.
        const Extent size = measure(lines);
        const Rectangle panel = intersect(Rectangle{.x = area.x + m_description.position.x,
                                                    .y = area.y + m_description.position.y,
                                                    .width = size.width,
                                                    .height = size.height},
                                          area);
        if (panel.width == 0 || panel.height == 0)
        {
            return;
        }
        const std::array background{ClearAttachment{.color_attachment = color_attachment, .color = m_description.background_color}};
        const std::array panel_rectangle{ClearRectangle{.area = panel}};
        commands.clear_attachments(background, panel_rectangle);

        // Each lit run of a glyph row is one rectangle (no allocation once the vector has grown).
        const auto scale = static_cast<std::int32_t>(m_description.scale);
        m_rectangles.clear();
        for (std::size_t line = 0; line < lines.size(); ++line)
        {
            const std::int32_t top = area.y + m_description.position.y + (PADDING + static_cast<std::int32_t>(line) * LINE_ADVANCE) * scale;
            for (std::size_t column = 0; column < lines[line].size(); ++column)
            {
                const std::int32_t left = area.x + m_description.position.x + (PADDING + static_cast<std::int32_t>(column) * GLYPH_ADVANCE) * scale;
                add_glyph(glyph_of(lines[line][column]), left, top, scale, panel);
            }
        }
        if (!m_rectangles.empty())
        {
            const std::array text{ClearAttachment{.color_attachment = color_attachment, .color = m_description.text_color}};
            commands.clear_attachments(text, m_rectangles);
        }
    }

private:
    /// @brief The rows of character @p character ('?' outside printable ASCII).
    [[nodiscard]] static auto glyph_of(char character) noexcept -> const std::array<std::uint8_t, 7>&
    {
        const auto code = static_cast<unsigned char>(character);
        return debug_font::GLYPHS[code >= 32 && code <= 126 ? code - 32U : '?' - 32U];
    }

    /// @brief The part of @p rectangle inside @p clip (empty when they do not overlap).
    [[nodiscard]] static auto intersect(const Rectangle& rectangle,
                                        const Rectangle& clip) noexcept -> Rectangle
    {
        const std::int64_t left = std::max<std::int64_t>(rectangle.x, clip.x);
        const std::int64_t top = std::max<std::int64_t>(rectangle.y, clip.y);
        const std::int64_t right = std::min<std::int64_t>(std::int64_t{rectangle.x} + rectangle.width, std::int64_t{clip.x} + clip.width);
        const std::int64_t bottom = std::min<std::int64_t>(std::int64_t{rectangle.y} + rectangle.height, std::int64_t{clip.y} + clip.height);
        if (right <= left || bottom <= top)
        {
            return Rectangle{};
        }
        return Rectangle{.x = static_cast<std::int32_t>(left),
                         .y = static_cast<std::int32_t>(top),
                         .width = static_cast<std::uint32_t>(right - left),
                         .height = static_cast<std::uint32_t>(bottom - top)};
    }

    /// @brief Adds the rectangles of the lit runs of @p glyph, whose top-left corner is (@p left, @p top).
    void add_glyph(const std::array<std::uint8_t, 7>& glyph,
                   std::int32_t left,
                   std::int32_t top,
                   std::int32_t scale,
                   const Rectangle& clip)
    {
        for (std::int32_t row = 0; row < GLYPH_HEIGHT; ++row)
        {
            const std::uint8_t bits = glyph[static_cast<std::size_t>(row)];
            std::int32_t column = 0;
            while (column < GLYPH_WIDTH)
            {
                // A run of lit columns, as one rectangle.
                if ((bits & (0x10U >> column)) == 0)
                {
                    ++column;
                    continue;
                }
                const std::int32_t start = column;
                while (column < GLYPH_WIDTH && (bits & (0x10U >> column)) != 0)
                {
                    ++column;
                }
                const Rectangle run = intersect(Rectangle{.x = left + start * scale,
                                                          .y = top + row * scale,
                                                          .width = static_cast<std::uint32_t>((column - start) * scale),
                                                          .height = static_cast<std::uint32_t>(scale)},
                                                clip);
                if (run.width != 0 && run.height != 0)
                {
                    m_rectangles.push_back(ClearRectangle{.area = run});
                }
            }
        }
    }

private:
    DebugOverlayDescription m_description{};
    std::vector<ClearRectangle> m_rectangles{};
};

} // namespace gfx
