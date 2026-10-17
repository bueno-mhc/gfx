/// @file font_decoder.cppm
/// @brief FontDecoder and FontFace: what reads fonts at atlas build time. A decoder opens faces of a
///        FontSource; a face gives metrics, the character map, advances, outlines and variation axes, and
///        the OpenType data HarfBuzz subsets into the atlas's shaping font. gfx provides FreeType (every
///        format, variable fonts) and stb_truetype (TrueType, CFF, collections); applications may
///        implement both interfaces for other formats.
export module gfx.ui.atlas_tools:font_decoder;

import std;
import gfx.ui.enums;
import gfx.ui.handles;
import gfx.ui.atlas_format;
import :font_source;
import :outline;

export namespace gfx::ui
{

/// @brief Metrics and names of a face, in font units.
struct FontFaceMetrics
{
    std::uint32_t units_per_em = 0;       ///< Font units per em (1000 for CFF, often 2048 for TrueType).
    std::int32_t ascender = 0;            ///< Above the baseline (hhea, or OS/2 when the font sets USE_TYPO_METRICS).
    std::int32_t descender = 0;           ///< Below the baseline (negative).
    std::int32_t line_gap = 0;            ///< Added between lines.
    std::int32_t x_height = 0;            ///< Height of lowercase letters (0 when the font does not say).
    std::int32_t cap_height = 0;          ///< Height of capital letters (0 when the font does not say).
    std::int32_t underline_position = 0;  ///< Top of the underline (negative below the baseline).
    std::int32_t underline_thickness = 0; ///< Thickness of the underline.
    std::string family{};                 ///< Family name ("Noto Sans").
    std::string style{};                  ///< Style name ("Regular").
};

/// @brief A variation axis of a variable font.
struct VariationAxis
{
    std::uint32_t tag = 0;     ///< OpenType tag (font_tag("wght")).
    std::string name{};        ///< Name from the font ("Weight").
    double minimum = 0;        ///< Smallest value.
    double default_value = 0;  ///< Value of the default instance.
    double maximum = 0;        ///< Largest value.
};

/// @brief A named instance of a variable font ("Bold"): a value for every axis.
struct NamedInstance
{
    std::string name{};                    ///< Style name from the font.
    std::vector<FontAxisValue> coordinates{}; ///< One value per axis, in the order of the axes.
};

/// @brief A face opened by a FontDecoder. Not thread-safe: the atlas builder reads every glyph before
///        generating distance fields on several threads.
class FontFace
{
public:
    virtual ~FontFace() = default;

    /// @brief The decoder that opened the face.
    [[nodiscard]] virtual auto decoder() const noexcept -> FontDecoderKind = 0;

    /// @brief The container format the face was read from.
    [[nodiscard]] virtual auto format() const noexcept -> FontFormat = 0;

    /// @brief Metrics and names (at the current variation).
    [[nodiscard]] virtual auto metrics() const -> FontFaceMetrics = 0;

    /// @brief Glyphs of the face; glyph indices are below it, 0 being `.notdef`.
    [[nodiscard]] virtual auto glyph_count() const noexcept -> std::uint32_t = 0;

    /// @brief Every Unicode character the face maps to a glyph, by code point.
    [[nodiscard]] virtual auto character_map() const -> std::vector<CharacterMapping> = 0;

    /// @brief Horizontal advance of @p glyph, in font units (at the current variation).
    [[nodiscard]] virtual auto advance(GlyphIndex glyph) const -> double = 0;

    /// @brief Outline of @p glyph, in font units (at the current variation); empty for spaces.
    [[nodiscard]] virtual auto outline(GlyphIndex glyph) const -> GlyphOutline = 0;

    /// @brief COLOR_LAYERS when the font draws @p glyph in color, NO_OUTLINE when it has no outline for
    ///        it (bitmap-only glyph); NONE otherwise.
    [[nodiscard]] virtual auto glyph_flags(GlyphIndex glyph) const -> AtlasGlyphFlag = 0;

    /// @brief Variation axes (none for a static font).
    [[nodiscard]] virtual auto variation_axes() const -> std::vector<VariationAxis>;

    /// @brief Named instances (none for a static font).
    [[nodiscard]] virtual auto named_instances() const -> std::vector<NamedInstance>;

    /// @brief Selects the instance at @p coordinates (axes not listed keep their default); empty selects
    ///        the default instance.
    /// @throws gfx::BackendCapabilityError (UiCapabilityError) when the decoder cannot apply variations.
    /// @throws gfx::InvalidUsageError for an axis the face lacks or a value outside its range.
    virtual void set_variation(std::span<const FontAxisValue> coordinates);

    /// @brief The OpenType (sfnt) data of the face's file: the file itself, or its decoded tables for WOFF
    ///        and WOFF2 (a collection whole: sfnt_face_index() selects the face). Empty when the decoder
    ///        cannot provide it, in which case atlases are built without a shaping font.
    [[nodiscard]] virtual auto sfnt() const -> std::span<const std::byte>;

    /// @brief The face of sfnt() this face is (0 unless sfnt() is a collection).
    [[nodiscard]] virtual auto sfnt_face_index() const noexcept -> std::uint32_t;
};

/// @brief Opens faces of fonts.
class FontDecoder
{
public:
    virtual ~FontDecoder() = default;

    /// @brief Which decoder this is.
    [[nodiscard]] virtual auto kind() const noexcept -> FontDecoderKind = 0;

    /// @brief Faces in @p source (1 unless it is a collection).
    /// @throws gfx::BackendCapabilityError (UiCapabilityError) when the decoder cannot read its format.
    [[nodiscard]] virtual auto face_count(const FontSource& source) const -> std::uint32_t = 0;

    /// @brief Opens face @p face_index of @p source.
    /// @throws gfx::BackendCapabilityError (UiCapabilityError) when the decoder cannot read its format.
    /// @throws gfx::InvalidUsageError when the source is not a font or has no such face.
    [[nodiscard]] virtual auto open(const FontSource& source,
                                    std::uint32_t face_index = 0) const -> std::unique_ptr<FontFace> = 0;
};

/// @brief The decoder of @p kind: FREETYPE or STB_TRUETYPE.
/// @throws gfx::InvalidUsageError for CUSTOM (implement FontDecoder and give it to the AtlasBuilder).
[[nodiscard]] auto create_font_decoder(FontDecoderKind kind) -> std::unique_ptr<FontDecoder>;

} // namespace gfx::ui
