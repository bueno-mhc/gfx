/// @file enums.cppm
/// @brief Every enumeration and constant of the gfx UI library (widgets, fonts, atlases). No other file
///        of the UI library defines either; the enumeration machinery (traits, verification, bitmask
///        operators, spelling and parsing) comes from gfx.core.enums, which this module re-exports.
///
/// Conventions: enumerators and constants use SCREAMING_SNAKE_CASE and full words; names that platform
/// headers define as macros are avoided, which a CTest test checks. Enumerations stored in `.uiatlas`
/// files (AtlasSectionKind, SdfKind, AtlasTexelFormat, AtlasCompression, AtlasPackerKind, FontDecoderKind,
/// SdfGeneratorKind, EdgeColoring, AtlasGlyphFlag) only ever gain enumerators at their end: their values
/// are part of the format (docs/ui-atlas-format.md).
export module gfx.ui.enums;

import std;
export import gfx.core.enums;

export namespace gfx::ui
{

// The bitmask operators and helpers of gfx, found by argument-dependent lookup for the bitmask
// enumerations of this namespace too.
using gfx::operator|;
using gfx::operator&;
using gfx::operator^;
using gfx::operator~;
using gfx::operator|=;
using gfx::operator&=;
using gfx::operator^=;
using gfx::has_all;
using gfx::has_any;
using gfx::is_empty;
using gfx::to_string;

// =================================================================================================
// Enumerations
// =================================================================================================

/// @brief The part of the UI library an error comes from: errors name it as their backend
///        ("[gfx::ui::FONT] FontAtlas::load: ...").
enum class UiSubsystem : std::uint8_t
{
    GRAPHICS,      ///< Rendering through the graphics abstraction (pipelines, images, buffers).
    FONT,          ///< Font sources, decoders, distance fields, atlases, shaping and layout.
    INPUT,         ///< Input from the platform library: keys, mouse, text input, navigation.
    PLATFORM,      ///< Platform services: clipboard, cursors, input method placement.
    ACCESSIBILITY, ///< Accessibility trees and screen-reader providers.
};

/// @brief Container format of a font file.
enum class FontFormat : std::uint8_t
{
    TRUETYPE,      ///< OpenType with TrueType outlines (.ttf).
    OPENTYPE_CFF,  ///< OpenType with CFF outlines (.otf).
    OPENTYPE_CFF2, ///< OpenType with CFF2 outlines (variable .otf).
    COLLECTION,    ///< Several faces in one file (.ttc, .otc).
    WOFF,          ///< Web Open Font Format 1 (zlib-compressed tables).
    WOFF2,         ///< Web Open Font Format 2 (Brotli-compressed, transformed tables).
    CUSTOM,        ///< A format a custom FontDecoder reads.
};

/// @brief Library that reads font outlines at atlas build time.
enum class FontDecoderKind : std::uint8_t
{
    FREETYPE,     ///< FreeType: every format of FontFormat but CUSTOM, variable fonts.
    STB_TRUETYPE, ///< stb_truetype: TrueType, OpenType CFF and collections.
    CUSTOM,       ///< A FontDecoder of the application.
};

/// @brief What the texels of an atlas page store (the distance field kind).
enum class SdfKind : std::uint8_t
{
    SDF,   ///< One channel: true signed distance (soft effects, rounded corners).
    PSDF,  ///< One channel: pseudo signed distance (hard effects).
    MSDF,  ///< Three channels: multi-channel distance (sharp corners).
    MTSDF, ///< Four channels: MSDF in RGB and the true distance in alpha (sharp corners and soft effects).
};

/// @brief Texel format of an atlas page.
enum class AtlasTexelFormat : std::uint8_t
{
    R8_UNORM,            ///< One 8-bit channel (SDF, PSDF).
    R8G8B8A8_UNORM,      ///< Four 8-bit channels (MSDF with alpha unused, MTSDF).
    R16_SFLOAT,          ///< One 16-bit floating-point channel (SDF, PSDF; wide distance ranges).
    R16G16B16A16_SFLOAT, ///< Four 16-bit floating-point channels (MSDF, MTSDF; wide distance ranges).
};

/// @brief Compression of a section of an atlas.
enum class AtlasCompression : std::uint8_t
{
    NONE, ///< Stored as is.
    ZSTD, ///< zstd frame.
};

/// @brief Section of a `.uiatlas` blob (docs/ui-design.md A7).
enum class AtlasSectionKind : std::uint8_t
{
    FACE_METRICS,  ///< Units per em, ascender, descender, line gap, x-height, cap height, underline, names, axes.
    GLYPHS,        ///< Per glyph: advance, plane bounds, atlas bounds, page, flags.
    CHARACTER_MAP, ///< Sorted code point to glyph pairs.
    KERNING,       ///< Sorted glyph pair adjustments.
    LIGATURES,     ///< Standard ligatures, for the basic shaper.
    SHAPING_FONT,  ///< The subset font whose OpenType tables HarfBuzz shapes with.
    PIXELS,        ///< Atlas pages: dimensions, texel format, distance range, texels.
    DESCRIPTOR,    ///< The parameters the atlas was built with.
};

/// @brief How glyphs are placed in an atlas.
enum class AtlasPackerKind : std::uint8_t
{
    TIGHT, ///< Rectangles packed tightly (smallest atlas).
    GRID,  ///< Uniform cells (simple addressing, larger atlas).
    CUSTOM, ///< An AtlasPacker of the application.
};

/// @brief Direction in which text runs.
enum class TextDirection : std::uint8_t
{
    LEFT_TO_RIGHT, ///< Latin, Cyrillic, Greek, CJK, ...
    RIGHT_TO_LEFT, ///< Arabic, Hebrew, ...
};

/// @brief A named set of characters an atlas can include: a Unicode block (CHARACTER_SET_RANGES gives
///        its code points). Atlas descriptions name them ("BASIC_LATIN LATIN_1_SUPPLEMENT ARROWS").
enum class CharacterSet : std::uint8_t
{
    BASIC_LATIN,            ///< U+0020 to U+007E: printable ASCII.
    LATIN_1_SUPPLEMENT,     ///< U+00A0 to U+00FF: Western European letters and symbols.
    LATIN_EXTENDED_A,       ///< U+0100 to U+017F: Central and Eastern European letters.
    LATIN_EXTENDED_B,       ///< U+0180 to U+024F.
    GREEK,                  ///< U+0370 to U+03FF: Greek and Coptic.
    CYRILLIC,               ///< U+0400 to U+04FF.
    HEBREW,                 ///< U+0590 to U+05FF.
    ARABIC,                 ///< U+0600 to U+06FF.
    GENERAL_PUNCTUATION,    ///< U+2000 to U+206F: dashes, quotation marks, ellipsis, spaces.
    CURRENCY_SYMBOLS,       ///< U+20A0 to U+20CF.
    LETTERLIKE_SYMBOLS,     ///< U+2100 to U+214F.
    ARROWS,                 ///< U+2190 to U+21FF.
    MATHEMATICAL_OPERATORS, ///< U+2200 to U+22FF.
    BOX_DRAWING,            ///< U+2500 to U+257F.
    GEOMETRIC_SHAPES,       ///< U+25A0 to U+25FF.
};

/// @brief Kind of a segment of a glyph outline.
enum class OutlineSegmentKind : std::uint8_t
{
    LINE,      ///< A straight line: two points.
    QUADRATIC, ///< A quadratic Bezier curve (TrueType): start, control and end points.
    CUBIC,     ///< A cubic Bezier curve (CFF, CFF2): start, two control points and end point.
};

/// @brief Algorithm that generates the distance fields of an atlas at build time.
enum class SdfGeneratorKind : std::uint8_t
{
    MSDFGEN,  ///< msdfgen: SDF, PSDF, MSDF and MTSDF.
    BUILT_IN, ///< gfx's own exact-distance kernel: SDF only, builds without msdfgen.
    CUSTOM,   ///< An SdfGenerator of the application.
};

/// @brief How msdfgen assigns channels to the edges of a glyph before generating an MSDF or MTSDF.
enum class EdgeColoring : std::uint8_t
{
    SIMPLE,   ///< msdfgen's simple coloring: fastest.
    INK_TRAP, ///< Keeps the corners of ink traps sharp (msdf-atlas-gen's default).
    DISTANCE, ///< Colors by distance between edges: best quality, slowest.
};

/// @brief Properties of a glyph of an atlas (stored in `.uiatlas` files).
enum class AtlasGlyphFlag : std::uint16_t
{
    NONE         = 0,       ///< No bit.
    WHITESPACE   = 1U << 0, ///< No outline (a space): advances only, has no box in the atlas.
    COLOR_LAYERS = 1U << 1, ///< The font draws it in color (COLR); the atlas holds its monochrome outline.
    NO_OUTLINE   = 1U << 2, ///< The font has no outline for it (bitmap-only glyph): drawn as nothing.
};

/// @brief How much of a `.uiatlas` blob AtlasView::parse() verifies.
enum class AtlasVerification : std::uint8_t
{
    STRUCTURE, ///< Header, section table, bounds, alignment and the tables' ordering.
    CHECKSUMS, ///< STRUCTURE and every xxHash64 checksum (the whole blob is read).
};

/// @brief Problem AtlasView::parse() found in a `.uiatlas` blob.
enum class AtlasProblem : std::uint8_t
{
    TRUNCATED,                 ///< The blob is shorter than its header or than the size its header gives.
    BAD_MAGIC,                 ///< The blob does not start with UI_ATLAS_MAGIC: not an atlas.
    UNSUPPORTED_VERSION,       ///< The major version is not UI_ATLAS_VERSION_MAJOR.
    WRONG_ENDIANNESS,          ///< The endianness tag reads differently: written big-endian.
    SIZE_MISMATCH,             ///< The size in the header is not the size of the blob.
    CHECKSUM_MISMATCH,         ///< The checksum of the header does not match the blob.
    SECTION_OUT_OF_BOUNDS,     ///< A section (or the section table) extends past the end of the blob.
    MISALIGNED_SECTION,        ///< A section does not start at a multiple of UI_ATLAS_ALIGNMENT.
    SECTION_CHECKSUM_MISMATCH, ///< The checksum of a section does not match its bytes.
    UNKNOWN_COMPRESSION,       ///< A section names a compression this reader does not know.
    MISSING_SECTION,           ///< A required section is absent.
    DUPLICATE_SECTION,         ///< A section that may appear once appears several times.
    MALFORMED_SECTION,         ///< A section's content is inconsistent (sizes, counts, ordering, compression).
};

/// @brief Base direction of a paragraph of text.
enum class ParagraphDirection : std::uint8_t
{
    AUTOMATIC,     ///< From the first strong character (UAX #9 rules P2 and P3); left to right when there is none.
    LEFT_TO_RIGHT, ///< Left to right, whatever the text.
    RIGHT_TO_LEFT, ///< Right to left, whatever the text.
};

/// @brief Horizontal alignment of the lines of a text in its width.
enum class TextAlignment : std::uint8_t
{
    START,  ///< Where the paragraph's direction starts: left for left-to-right paragraphs, right otherwise.
    END,    ///< Where it ends.
    LEFT,   ///< Left, whatever the direction.
    CENTER, ///< Centered.
    RIGHT,  ///< Right, whatever the direction.
};

/// @brief Which shaper turns runs of characters into glyphs.
enum class TextShaping : std::uint8_t
{
    AUTOMATIC, ///< The basic shaper for left-to-right Latin, Greek, Cyrillic and common characters; HarfBuzz otherwise.
    BASIC,     ///< The basic shaper (character map, standard ligatures, pair kerning) for every run.
    HARFBUZZ,  ///< HarfBuzz for every run (needs the atlas's shaping font).
};

/// @brief What went wrong while laying a text out (the layout is still made: these are reported).
enum class TextLayoutProblem : std::uint8_t
{
    NONE            = 0,       ///< No bit.
    UNKNOWN_FONT    = 1U << 0, ///< The font handle names no font: the layout is empty.
    INVALID_UTF8    = 1U << 1, ///< Bytes that are not UTF-8 were laid out as U+FFFD.
    MISSING_GLYPHS  = 1U << 2, ///< Characters no atlas of the font's chain has were drawn as `.notdef`.
    NO_SHAPING_FONT = 1U << 3, ///< A run needed HarfBuzz but its atlas has no shaping font: the basic shaper shaped it.
    CACHE_FULL      = 1U << 4, ///< Every cache entry was in use this frame: the layout lives until the next frame only.
};

/// @brief What a draw command samples.
enum class DrawTextureKind : std::uint8_t
{
    NONE,      ///< Nothing (shapes, solid triangles): compatible with any texture.
    FONT_PAGE, ///< A page of a font atlas, uploaded by the renderer.
    IMAGE,     ///< An image of the application, named by its bindless sampled-image index.
};

/// @brief How the UI shader draws a vertex's primitive (bits 0 to 7 of UiVertex::mode; the values are the
///        shader's).
enum class UiShaderMode : std::uint8_t
{
    SOLID,        ///< The vertex color (triangles, with alpha for anti-aliased fringes).
    SHAPE,        ///< A rounded box evaluated as a signed distance: rectangles, borders, circles, lines.
    GLYPH,        ///< A glyph of a distance-field atlas, with its outline.
    GLYPH_SHADOW, ///< A glyph's shadow or glow: its true distance, softened.
    IMAGE,        ///< A texture, tinted by the vertex color.
};

/// @brief How the atlas tool embeds a `.uiatlas` blob into a generated C++ module.
enum class AtlasEmbedding : std::uint8_t
{
    EMBED_DIRECTIVE, ///< `#embed` (GCC 15, Clang 19): fast to compile.
    BYTE_LIST,       ///< A list of byte literals: for compilers without `#embed` (MSVC).
};

// =================================================================================================
// Constants
// =================================================================================================

/// @brief First bytes of every `.uiatlas` blob.
inline constexpr std::array<char, 8> UI_ATLAS_MAGIC{'G', 'F', 'X', 'U', 'I', 'A', 'T', 'L'};

/// @brief Major version of the `.uiatlas` format: readers refuse other major versions.
inline constexpr std::uint16_t UI_ATLAS_VERSION_MAJOR = 1;

/// @brief Minor version of the `.uiatlas` format: readers accept every minor version of their major.
inline constexpr std::uint16_t UI_ATLAS_VERSION_MINOR = 0;

/// @brief Written as a little-endian 32-bit value: a reader that sees other bytes has the wrong endianness.
inline constexpr std::uint32_t UI_ATLAS_ENDIANNESS_TAG = 0x01020304U;

/// @brief Alignment, in bytes, of every structure and section of a `.uiatlas` blob.
inline constexpr std::size_t UI_ATLAS_ALIGNMENT = 8;

/// @brief Size, in bytes, of the header of a `.uiatlas` blob.
inline constexpr std::size_t UI_ATLAS_HEADER_SIZE = 64;

/// @brief Version of the atlas generator, stored in every atlas: raised whenever the same inputs would
///        produce different bytes (the pixels, the packing or the tables).
inline constexpr std::uint32_t UI_ATLAS_GENERATOR_VERSION = 1;

/// @brief U+FFFD, which stands for bytes that are not UTF-8.
inline constexpr char32_t REPLACEMENT_CHARACTER = U'\uFFFD';

/// @brief Largest number of glyphs a ligature of the LIGATURES section replaces.
inline constexpr std::size_t MAXIMUM_LIGATURE_COMPONENTS = 4;

/// @brief The code points of a CharacterSet, first and last included.
struct CharacterRange
{
    char32_t first = 0; ///< First code point.
    char32_t last = 0;  ///< Last code point (included).
};

/// @brief The code points of every CharacterSet, indexed by its value.
inline constexpr std::array<CharacterRange, 15> CHARACTER_SET_RANGES{{
    {U'\u0020', U'\u007E'}, // BASIC_LATIN
    {U'\u00A0', U'\u00FF'}, // LATIN_1_SUPPLEMENT
    {U'\u0100', U'\u017F'}, // LATIN_EXTENDED_A
    {U'\u0180', U'\u024F'}, // LATIN_EXTENDED_B
    {U'\u0370', U'\u03FF'}, // GREEK
    {U'\u0400', U'\u04FF'}, // CYRILLIC
    {U'\u0590', U'\u05FF'}, // HEBREW
    {U'\u0600', U'\u06FF'}, // ARABIC
    {U'\u2000', U'\u206F'}, // GENERAL_PUNCTUATION
    {U'\u20A0', U'\u20CF'}, // CURRENCY_SYMBOLS
    {U'\u2100', U'\u214F'}, // LETTERLIKE_SYMBOLS
    {U'\u2190', U'\u21FF'}, // ARROWS
    {U'\u2200', U'\u22FF'}, // MATHEMATICAL_OPERATORS
    {U'\u2500', U'\u257F'}, // BOX_DRAWING
    {U'\u25A0', U'\u25FF'}, // GEOMETRIC_SHAPES
}};

} // namespace gfx::ui

// =================================================================================================
// Enumeration descriptions
// =================================================================================================
export namespace gfx
{

template<>
struct EnumerationTraits<ui::UiSubsystem>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::string_view BACKEND_LIBRARY = "gfx::ui"; ///< Errors name these subsystems "gfx::ui::<NAME>".
    static constexpr std::array NAMES{
        EnumeratorName{ui::UiSubsystem::GRAPHICS, "GRAPHICS"},
        EnumeratorName{ui::UiSubsystem::FONT, "FONT"},
        EnumeratorName{ui::UiSubsystem::INPUT, "INPUT"},
        EnumeratorName{ui::UiSubsystem::PLATFORM, "PLATFORM"},
        EnumeratorName{ui::UiSubsystem::ACCESSIBILITY, "ACCESSIBILITY"},
    };
};

template<>
struct EnumerationTraits<ui::FontFormat>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::FontFormat::TRUETYPE, "TRUETYPE"},
        EnumeratorName{ui::FontFormat::OPENTYPE_CFF, "OPENTYPE_CFF"},
        EnumeratorName{ui::FontFormat::OPENTYPE_CFF2, "OPENTYPE_CFF2"},
        EnumeratorName{ui::FontFormat::COLLECTION, "COLLECTION"},
        EnumeratorName{ui::FontFormat::WOFF, "WOFF"},
        EnumeratorName{ui::FontFormat::WOFF2, "WOFF2"},
        EnumeratorName{ui::FontFormat::CUSTOM, "CUSTOM"},
    };
};

template<>
struct EnumerationTraits<ui::FontDecoderKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::FontDecoderKind::FREETYPE, "FREETYPE"},
        EnumeratorName{ui::FontDecoderKind::STB_TRUETYPE, "STB_TRUETYPE"},
        EnumeratorName{ui::FontDecoderKind::CUSTOM, "CUSTOM"},
    };
};

template<>
struct EnumerationTraits<ui::SdfKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::SdfKind::SDF, "SDF"},
        EnumeratorName{ui::SdfKind::PSDF, "PSDF"},
        EnumeratorName{ui::SdfKind::MSDF, "MSDF"},
        EnumeratorName{ui::SdfKind::MTSDF, "MTSDF"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasTexelFormat>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasTexelFormat::R8_UNORM, "R8_UNORM"},
        EnumeratorName{ui::AtlasTexelFormat::R8G8B8A8_UNORM, "R8G8B8A8_UNORM"},
        EnumeratorName{ui::AtlasTexelFormat::R16_SFLOAT, "R16_SFLOAT"},
        EnumeratorName{ui::AtlasTexelFormat::R16G16B16A16_SFLOAT, "R16G16B16A16_SFLOAT"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasCompression>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasCompression::NONE, "NONE"},
        EnumeratorName{ui::AtlasCompression::ZSTD, "ZSTD"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasSectionKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasSectionKind::FACE_METRICS, "FACE_METRICS"},
        EnumeratorName{ui::AtlasSectionKind::GLYPHS, "GLYPHS"},
        EnumeratorName{ui::AtlasSectionKind::CHARACTER_MAP, "CHARACTER_MAP"},
        EnumeratorName{ui::AtlasSectionKind::KERNING, "KERNING"},
        EnumeratorName{ui::AtlasSectionKind::LIGATURES, "LIGATURES"},
        EnumeratorName{ui::AtlasSectionKind::SHAPING_FONT, "SHAPING_FONT"},
        EnumeratorName{ui::AtlasSectionKind::PIXELS, "PIXELS"},
        EnumeratorName{ui::AtlasSectionKind::DESCRIPTOR, "DESCRIPTOR"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasPackerKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasPackerKind::TIGHT, "TIGHT"},
        EnumeratorName{ui::AtlasPackerKind::GRID, "GRID"},
        EnumeratorName{ui::AtlasPackerKind::CUSTOM, "CUSTOM"},
    };
};

template<>
struct EnumerationTraits<ui::TextDirection>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::TextDirection::LEFT_TO_RIGHT, "LEFT_TO_RIGHT"},
        EnumeratorName{ui::TextDirection::RIGHT_TO_LEFT, "RIGHT_TO_LEFT"},
    };
};

template<>
struct EnumerationTraits<ui::CharacterSet>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::CharacterSet::BASIC_LATIN, "BASIC_LATIN"},
        EnumeratorName{ui::CharacterSet::LATIN_1_SUPPLEMENT, "LATIN_1_SUPPLEMENT"},
        EnumeratorName{ui::CharacterSet::LATIN_EXTENDED_A, "LATIN_EXTENDED_A"},
        EnumeratorName{ui::CharacterSet::LATIN_EXTENDED_B, "LATIN_EXTENDED_B"},
        EnumeratorName{ui::CharacterSet::GREEK, "GREEK"},
        EnumeratorName{ui::CharacterSet::CYRILLIC, "CYRILLIC"},
        EnumeratorName{ui::CharacterSet::HEBREW, "HEBREW"},
        EnumeratorName{ui::CharacterSet::ARABIC, "ARABIC"},
        EnumeratorName{ui::CharacterSet::GENERAL_PUNCTUATION, "GENERAL_PUNCTUATION"},
        EnumeratorName{ui::CharacterSet::CURRENCY_SYMBOLS, "CURRENCY_SYMBOLS"},
        EnumeratorName{ui::CharacterSet::LETTERLIKE_SYMBOLS, "LETTERLIKE_SYMBOLS"},
        EnumeratorName{ui::CharacterSet::ARROWS, "ARROWS"},
        EnumeratorName{ui::CharacterSet::MATHEMATICAL_OPERATORS, "MATHEMATICAL_OPERATORS"},
        EnumeratorName{ui::CharacterSet::BOX_DRAWING, "BOX_DRAWING"},
        EnumeratorName{ui::CharacterSet::GEOMETRIC_SHAPES, "GEOMETRIC_SHAPES"},
    };
};

template<>
struct EnumerationTraits<ui::OutlineSegmentKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::OutlineSegmentKind::LINE, "LINE"},
        EnumeratorName{ui::OutlineSegmentKind::QUADRATIC, "QUADRATIC"},
        EnumeratorName{ui::OutlineSegmentKind::CUBIC, "CUBIC"},
    };
};

template<>
struct EnumerationTraits<ui::SdfGeneratorKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::SdfGeneratorKind::MSDFGEN, "MSDFGEN"},
        EnumeratorName{ui::SdfGeneratorKind::BUILT_IN, "BUILT_IN"},
        EnumeratorName{ui::SdfGeneratorKind::CUSTOM, "CUSTOM"},
    };
};

template<>
struct EnumerationTraits<ui::EdgeColoring>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::EdgeColoring::SIMPLE, "SIMPLE"},
        EnumeratorName{ui::EdgeColoring::INK_TRAP, "INK_TRAP"},
        EnumeratorName{ui::EdgeColoring::DISTANCE, "DISTANCE"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasGlyphFlag>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasGlyphFlag::WHITESPACE, "WHITESPACE"},
        EnumeratorName{ui::AtlasGlyphFlag::COLOR_LAYERS, "COLOR_LAYERS"},
        EnumeratorName{ui::AtlasGlyphFlag::NO_OUTLINE, "NO_OUTLINE"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasVerification>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasVerification::STRUCTURE, "STRUCTURE"},
        EnumeratorName{ui::AtlasVerification::CHECKSUMS, "CHECKSUMS"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasProblem>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasProblem::TRUNCATED, "TRUNCATED"},
        EnumeratorName{ui::AtlasProblem::BAD_MAGIC, "BAD_MAGIC"},
        EnumeratorName{ui::AtlasProblem::UNSUPPORTED_VERSION, "UNSUPPORTED_VERSION"},
        EnumeratorName{ui::AtlasProblem::WRONG_ENDIANNESS, "WRONG_ENDIANNESS"},
        EnumeratorName{ui::AtlasProblem::SIZE_MISMATCH, "SIZE_MISMATCH"},
        EnumeratorName{ui::AtlasProblem::CHECKSUM_MISMATCH, "CHECKSUM_MISMATCH"},
        EnumeratorName{ui::AtlasProblem::SECTION_OUT_OF_BOUNDS, "SECTION_OUT_OF_BOUNDS"},
        EnumeratorName{ui::AtlasProblem::MISALIGNED_SECTION, "MISALIGNED_SECTION"},
        EnumeratorName{ui::AtlasProblem::SECTION_CHECKSUM_MISMATCH, "SECTION_CHECKSUM_MISMATCH"},
        EnumeratorName{ui::AtlasProblem::UNKNOWN_COMPRESSION, "UNKNOWN_COMPRESSION"},
        EnumeratorName{ui::AtlasProblem::MISSING_SECTION, "MISSING_SECTION"},
        EnumeratorName{ui::AtlasProblem::DUPLICATE_SECTION, "DUPLICATE_SECTION"},
        EnumeratorName{ui::AtlasProblem::MALFORMED_SECTION, "MALFORMED_SECTION"},
    };
};

template<>
struct EnumerationTraits<ui::ParagraphDirection>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::ParagraphDirection::AUTOMATIC, "AUTOMATIC"},
        EnumeratorName{ui::ParagraphDirection::LEFT_TO_RIGHT, "LEFT_TO_RIGHT"},
        EnumeratorName{ui::ParagraphDirection::RIGHT_TO_LEFT, "RIGHT_TO_LEFT"},
    };
};

template<>
struct EnumerationTraits<ui::TextAlignment>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::TextAlignment::START, "START"},
        EnumeratorName{ui::TextAlignment::END, "END"},
        EnumeratorName{ui::TextAlignment::LEFT, "LEFT"},
        EnumeratorName{ui::TextAlignment::CENTER, "CENTER"},
        EnumeratorName{ui::TextAlignment::RIGHT, "RIGHT"},
    };
};

template<>
struct EnumerationTraits<ui::TextShaping>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::TextShaping::AUTOMATIC, "AUTOMATIC"},
        EnumeratorName{ui::TextShaping::BASIC, "BASIC"},
        EnumeratorName{ui::TextShaping::HARFBUZZ, "HARFBUZZ"},
    };
};

template<>
struct EnumerationTraits<ui::TextLayoutProblem>
{
    static constexpr bool IS_BITMASK = true;
    static constexpr std::array NAMES{
        EnumeratorName{ui::TextLayoutProblem::UNKNOWN_FONT, "UNKNOWN_FONT"},
        EnumeratorName{ui::TextLayoutProblem::INVALID_UTF8, "INVALID_UTF8"},
        EnumeratorName{ui::TextLayoutProblem::MISSING_GLYPHS, "MISSING_GLYPHS"},
        EnumeratorName{ui::TextLayoutProblem::NO_SHAPING_FONT, "NO_SHAPING_FONT"},
        EnumeratorName{ui::TextLayoutProblem::CACHE_FULL, "CACHE_FULL"},
    };
};

template<>
struct EnumerationTraits<ui::DrawTextureKind>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::DrawTextureKind::NONE, "NONE"},
        EnumeratorName{ui::DrawTextureKind::FONT_PAGE, "FONT_PAGE"},
        EnumeratorName{ui::DrawTextureKind::IMAGE, "IMAGE"},
    };
};

template<>
struct EnumerationTraits<ui::UiShaderMode>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::UiShaderMode::SOLID, "SOLID"},
        EnumeratorName{ui::UiShaderMode::SHAPE, "SHAPE"},
        EnumeratorName{ui::UiShaderMode::GLYPH, "GLYPH"},
        EnumeratorName{ui::UiShaderMode::GLYPH_SHADOW, "GLYPH_SHADOW"},
        EnumeratorName{ui::UiShaderMode::IMAGE, "IMAGE"},
    };
};

template<>
struct EnumerationTraits<ui::AtlasEmbedding>
{
    static constexpr bool IS_BITMASK = false;
    static constexpr std::array NAMES{
        EnumeratorName{ui::AtlasEmbedding::EMBED_DIRECTIVE, "EMBED_DIRECTIVE"},
        EnumeratorName{ui::AtlasEmbedding::BYTE_LIST, "BYTE_LIST"},
    };
};

} // namespace gfx

// =================================================================================================
// Verification: names, order and completeness of every description, at compile time
// =================================================================================================
namespace gfx::enumeration_verification
{

static_assert(verify<ui::UiSubsystem>(), "EnumerationTraits<ui::UiSubsystem>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::FontFormat>(), "EnumerationTraits<ui::FontFormat>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::FontDecoderKind>(), "EnumerationTraits<ui::FontDecoderKind>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::SdfKind>(), "EnumerationTraits<ui::SdfKind>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasTexelFormat>(), "EnumerationTraits<ui::AtlasTexelFormat>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasCompression>(), "EnumerationTraits<ui::AtlasCompression>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasSectionKind>(), "EnumerationTraits<ui::AtlasSectionKind>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasPackerKind>(), "EnumerationTraits<ui::AtlasPackerKind>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::TextDirection>(), "EnumerationTraits<ui::TextDirection>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::CharacterSet>(), "EnumerationTraits<ui::CharacterSet>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::OutlineSegmentKind>(), "EnumerationTraits<ui::OutlineSegmentKind>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::SdfGeneratorKind>(), "EnumerationTraits<ui::SdfGeneratorKind>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::EdgeColoring>(), "EnumerationTraits<ui::EdgeColoring>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasGlyphFlag>(), "EnumerationTraits<ui::AtlasGlyphFlag>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasVerification>(), "EnumerationTraits<ui::AtlasVerification>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasProblem>(), "EnumerationTraits<ui::AtlasProblem>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::AtlasEmbedding>(), "EnumerationTraits<ui::AtlasEmbedding>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::DrawTextureKind>(), "EnumerationTraits<ui::DrawTextureKind>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::UiShaderMode>(), "EnumerationTraits<ui::UiShaderMode>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::ParagraphDirection>(), "EnumerationTraits<ui::ParagraphDirection>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::TextAlignment>(), "EnumerationTraits<ui::TextAlignment>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::TextShaping>(), "EnumerationTraits<ui::TextShaping>::NAMES is out of sync with the enumeration");
static_assert(verify<ui::TextLayoutProblem>(), "EnumerationTraits<ui::TextLayoutProblem>::NAMES is out of sync with the enumeration");
static_assert(ui::CHARACTER_SET_RANGES.size() == ENUMERATOR_COUNT<ui::CharacterSet>,
              "CHARACTER_SET_RANGES needs one range per CharacterSet");
static_assert(std::ranges::all_of(ui::CHARACTER_SET_RANGES, [](const ui::CharacterRange& range) { return range.first <= range.last; }),
              "every range of CHARACTER_SET_RANGES starts before it ends");

} // namespace gfx::enumeration_verification
