/// @file engine.cppm
/// @brief Internal partition of gfx.ui.text: the parts of the layout engine — bidi analysis and visual
///        runs (bidi.cpp, SheenBidi), break opportunities (line_breaking.cpp, libunibreak), and the two
///        shapers (shaping.cpp: HarfBuzz and the basic shaper). They work on the code points of one
///        paragraph; clusters are indices of code points in it.
module gfx.ui.text:engine;

import std;
import gfx.ui.enums;
import gfx.ui.handles;
import :font_atlas;

namespace gfx::ui::text_engine
{

/// @brief A glyph a shaper produced, in pixels.
struct ShapedGlyph
{
    GlyphIndex glyph{};        ///< In the run's atlas.
    std::uint32_t cluster = 0; ///< Index in the paragraph of the first code point it draws.
    float advance = 0;         ///< Pen movement after it.
    float x_offset = 0;        ///< Right of the pen position.
    float y_offset = 0;        ///< Above the baseline.
};

/// @brief Code points [begin, end) of a line in one bidi run, and the run's embedding level.
struct VisualRun
{
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    std::uint8_t level = 0;
};

/// @brief OpenType script tags (ISO 15924) of the scripts the basic shaper handles.
inline constexpr std::uint32_t SCRIPT_LATIN = 0x4C61746EU;     // 'Latn'
inline constexpr std::uint32_t SCRIPT_GREEK = 0x4772656BU;     // 'Grek'
inline constexpr std::uint32_t SCRIPT_CYRILLIC = 0x4379726CU;  // 'Cyrl'
inline constexpr std::uint32_t SCRIPT_COMMON = 0x5A797979U;    // 'Zyyy'
inline constexpr std::uint32_t SCRIPT_INHERITED = 0x5A696E68U; // 'Zinh'

/// @brief Whether the basic shaper handles text of the script @p tag.
[[nodiscard]] constexpr auto
is_basic_script(std::uint32_t tag) noexcept -> bool
{
    return tag == SCRIPT_LATIN || tag == SCRIPT_GREEK || tag == SCRIPT_CYRILLIC || tag == SCRIPT_COMMON || tag == SCRIPT_INHERITED;
}

/// @brief Whether @p code_point is a combining mark (general category Mn, Mc or Me).
[[nodiscard]] auto is_mark(char32_t code_point) noexcept -> bool;

/// @brief Whether the script of @p code_point itself is Common or Inherited (punctuation, digits, spaces,
///        marks): such characters stay in the atlas of the character before them when it has them.
[[nodiscard]] auto is_common_or_inherited(char32_t code_point) noexcept -> bool;

/// @brief The bidi levels and scripts of a paragraph, and the visual runs of its lines (SheenBidi).
class BidiParagraph
{
public:
    BidiParagraph();
    BidiParagraph(const BidiParagraph&) = delete;
    auto operator=(const BidiParagraph&) -> BidiParagraph& = delete;
    ~BidiParagraph();

    /// @brief Analyzes @p code_points (one paragraph, which must outlive the analysis) in @p direction.
    void analyze(std::span<const std::uint32_t> code_points,
                 ParagraphDirection direction);

    /// @brief Embedding level of every code point (odd: right to left).
    [[nodiscard]] auto levels() const noexcept -> std::span<const std::uint8_t>;

    /// @brief Script (ISO 15924 tag) of every code point, common and inherited ones resolved from their neighbors.
    [[nodiscard]] auto scripts() const noexcept -> std::span<const std::uint32_t>;

    /// @brief Level of the paragraph (0: left to right, 1: right to left).
    [[nodiscard]] auto base_level() const noexcept -> std::uint8_t;

    /// @brief Appends the runs of the line [@p begin, @p end) to @p runs, in visual order (UAX #9 L1, L2).
    void line_runs(std::uint32_t begin,
                   std::uint32_t end,
                   std::vector<VisualRun>& runs) const;

private:
    struct State;
    std::unique_ptr<State> m_state{};
};

/// @brief Initializes libunibreak's tables (once): for TextBoundaries.
void initialize_libunibreak_for_boundaries();

/// @brief Line break status after every code point (UAX #14): 0 a break is required, 1 allowed, 2 not.
void find_line_breaks(std::span<const std::uint32_t> code_points,
                      const std::string& language,
                      std::vector<char>& breaks);

/// @brief Grapheme cluster boundary after every code point (UAX #29): 0 a boundary, 1 none.
void find_grapheme_breaks(std::span<const std::uint32_t> code_points,
                          const std::string& language,
                          std::vector<char>& breaks);

/// @brief Line break status: a break is required after the code point.
inline constexpr char LINE_BREAK_REQUIRED = 0;

/// @brief Line break status: a break is allowed after the code point.
inline constexpr char LINE_BREAK_ALLOWED = 1;

/// @brief Grapheme status: a grapheme cluster ends after the code point.
inline constexpr char GRAPHEME_BOUNDARY = 0;

/// @brief Shapes runs with HarfBuzz. Keeps one HarfBuzz font per atlas and a reused buffer.
class HarfBuzzShaper
{
public:
    HarfBuzzShaper();
    HarfBuzzShaper(const HarfBuzzShaper&) = delete;
    auto operator=(const HarfBuzzShaper&) -> HarfBuzzShaper& = delete;
    ~HarfBuzzShaper();

    /// @brief Appends the glyphs of code points [@p begin, @p end) of @p paragraph (the whole paragraph is
    ///        their context: joining, contextual forms) to @p glyphs, in visual order, shaped with the
    ///        shaping font of @p atlas at @p size pixels per em. @p atlas must have a shaping font.
    void shape(const std::shared_ptr<const FontAtlas>& atlas,
               std::span<const std::uint32_t> paragraph,
               std::uint32_t begin,
               std::uint32_t end,
               bool right_to_left,
               std::uint32_t script,
               const std::string& language,
               float size,
               std::vector<ShapedGlyph>& glyphs);

private:
    struct State;
    std::unique_ptr<State> m_state{};
};

/// @brief Appends the glyphs of code points [@p begin, @p end) of @p paragraph to @p glyphs, shaped by the
///        basic shaper: the atlas's character map, its standard ligatures (longest first) and its pair
///        kerning, at @p size pixels per em; in visual order (reversed for right to left, without joining).
void shape_basic(const FontAtlas& atlas,
                 std::span<const std::uint32_t> paragraph,
                 std::uint32_t begin,
                 std::uint32_t end,
                 bool right_to_left,
                 float size,
                 std::vector<ShapedGlyph>& glyphs);

} // namespace gfx::ui::text_engine
