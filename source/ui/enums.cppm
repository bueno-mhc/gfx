/// @file enums.cppm
/// @brief Every enumeration and constant of the gfx UI library (widgets, fonts, atlases). No other file
///        of the UI library defines either; the enumeration machinery (traits, verification, bitmask
///        operators, spelling and parsing) comes from gfx.core.enums, which this module re-exports.
///
/// Conventions: enumerators and constants use SCREAMING_SNAKE_CASE and full words; names that platform
/// headers define as macros are avoided, which a CTest test checks. Enumerations stored in `.uiatlas`
/// files (AtlasSectionKind, SdfKind, AtlasTexelFormat, AtlasCompression, AtlasPackerKind) only ever gain
/// enumerators at their end: their values are part of the format (docs/ui-design.md A7).
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

} // namespace gfx::enumeration_verification
