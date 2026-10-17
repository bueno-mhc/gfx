/// @file text_layout.cppm
/// @brief TextLayoutEngine: UTF-8 text → positioned glyphs. Paragraphs are split at line separators, their
///        characters given bidi levels (SheenBidi, UAX #9) and scripts (UAX #24), then cut into runs of one
///        level, script and atlas (the first of the font's chain that has the character); runs are shaped
///        (HarfBuzz with the atlas's shaping font, or the basic shaper for simple left-to-right runs), lines
///        broken at UAX #14 opportunities (libunibreak), reordered visually and aligned. Results are cached:
///        laying the same text out again allocates nothing.
export module gfx.ui.text:text_layout;

import std;
import gfx.ui.enums;
import gfx.ui.handles;
import :font_library;

export namespace gfx::ui
{

/// @brief How a text is laid out.
struct TextLayoutDescription
{
    float wrap_width = 0;                                      ///< Pixels; lines break to fit it (0 or less: only at line separators).
    TextAlignment alignment = TextAlignment::START;            ///< Of every line, in the wrap width (or the widest line).
    ParagraphDirection direction = ParagraphDirection::AUTOMATIC; ///< Base direction of every paragraph.
    float line_spacing = 1.0F;                                 ///< Multiplies the font's line height.

    /// @brief Descriptions compare member by member (they are part of the cache's key).
    [[nodiscard]] constexpr auto operator==(const TextLayoutDescription&) const noexcept -> bool = default;
};

/// @brief A glyph placed by a layout.
struct LaidOutGlyph
{
    FontAtlasHandle atlas{};    ///< The atlas holding it.
    GlyphIndex glyph{};         ///< Its index in that atlas (0: `.notdef`, for a character no atlas has).
    float x = 0;                ///< Pixels from the layout's left edge to the glyph's origin.
    float y = 0;                ///< Pixels from the layout's top edge down to the glyph's baseline.
    float advance = 0;          ///< Pixels the pen moves after it (kerning included).
    std::uint32_t cluster = 0;  ///< Byte offset in the text of the first character it draws.
    bool right_to_left = false; ///< Whether it belongs to a right-to-left run.
};

/// @brief A line of a layout: its glyphs in visual order (left to right) and its place.
struct LaidOutLine
{
    std::uint32_t first_glyph = 0; ///< Index of its first glyph in TextLayout::glyphs.
    std::uint32_t glyph_count = 0; ///< Glyphs it has.
    std::uint32_t text_begin = 0;  ///< Byte offset of its first character.
    std::uint32_t text_end = 0;    ///< Byte offset after its last character (its line separator excluded).
    float left = 0;                ///< Pixels from the layout's left edge to its first visible glyph.
    float width = 0;               ///< Pixels, trailing whitespace excluded.
    float top = 0;                 ///< Pixels from the layout's top edge.
    float baseline = 0;            ///< Pixels from the layout's top edge to its baseline.
    float height = 0;              ///< Pixels to the next line.
    bool right_to_left = false;    ///< Whether its paragraph runs right to left.
};

/// @brief A laid-out text: a view of glyphs and lines the engine holds, valid until its next new_frame().
struct TextLayout
{
    std::span<const LaidOutGlyph> glyphs{};             ///< Every glyph, line after line.
    std::span<const LaidOutLine> lines{};               ///< Every line, top to bottom (an empty text has one).
    float width = 0;                                    ///< Pixels: the widest line.
    float height = 0;                                   ///< Pixels: every line.
    std::uint32_t missing_glyph_count = 0;              ///< Characters drawn as `.notdef`.
    TextLayoutProblem problems = TextLayoutProblem::NONE; ///< What went wrong.
};

/// @brief Where a caret stands.
struct CaretPosition
{
    std::uint32_t line = 0; ///< Index of its line.
    float x = 0;            ///< Pixels from the layout's left edge.
    float top = 0;          ///< Pixels from the layout's top edge.
    float height = 0;       ///< Pixels (the line's height).
};

/// @brief Where the caret before the character at byte @p offset of the text stands (offsets past the text
///        stand after its last character). Inside a ligature, the ligature's width is shared by its characters.
[[nodiscard]] auto caret_position(const TextLayout& layout,
                                  std::uint32_t offset) noexcept -> CaretPosition;

/// @brief The byte offset of the caret position nearest to (@p x, @p y), pixels from the layout's top-left.
[[nodiscard]] auto hit_test(const TextLayout& layout,
                            float x,
                            float y) noexcept -> std::uint32_t;

/// @brief Capacities of a TextLayoutEngine.
struct TextLayoutEngineDescription
{
    std::uint32_t cache_capacity = 1024; ///< Layouts kept; the least recently used one not used this frame is replaced.
};

/// @brief What the engine's cache did.
struct TextLayoutStatistics
{
    std::uint64_t hits = 0;      ///< Layouts found in the cache.
    std::uint64_t misses = 0;    ///< Layouts made.
    std::uint64_t evictions = 0; ///< Layouts replaced.
    std::uint64_t overflows = 0; ///< Layouts made while every entry was in use this frame (CACHE_FULL).
    std::size_t cached = 0;      ///< Layouts held.
};

/// @brief Lays texts out with the fonts of a FontLibrary (which must outlive it). One thread at a time.
class TextLayoutEngine
{
public:
    /// @brief An engine for the fonts of @p fonts.
    explicit TextLayoutEngine(const FontLibrary& fonts,
                              TextLayoutEngineDescription description = {});

    TextLayoutEngine(const TextLayoutEngine&) = delete;
    auto operator=(const TextLayoutEngine&) -> TextLayoutEngine& = delete;
    ~TextLayoutEngine();

    /// @brief The layout of the UTF-8 @p text in @p font: from the cache, or made and cached. Never throws:
    ///        problems are reported in the layout (an unknown font gives an empty layout).
    [[nodiscard]] auto layout(std::string_view text,
                              FontHandle font,
                              const TextLayoutDescription& description = {}) noexcept -> TextLayout;

    /// @brief Starts a frame: layouts returned before may be replaced from now on.
    void new_frame() noexcept;

    /// @brief Forgets every cached layout.
    void clear() noexcept;

    /// @brief What the cache did since the engine was made.
    [[nodiscard]] auto statistics() const noexcept -> TextLayoutStatistics;

private:
    struct State;
    std::unique_ptr<State> m_state{};
};

} // namespace gfx::ui
