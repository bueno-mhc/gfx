/// @file atlas_writer.cppm
/// @brief write_atlas(): serializes the content of an atlas into a `.uiatlas` blob (docs/ui-atlas-format.md):
///        sections in a fixed order, 8-byte aligned, pixels and shaping font compressed with zstd, every
///        checksum computed. The same content always gives the same bytes.
export module gfx.ui.atlas_tools:atlas_writer;

import std;
import gfx.ui.enums;
import gfx.ui.atlas_format;

export namespace gfx::ui
{

/// @brief The face an atlas was built from: metrics in font units, names, and the variation it was built at.
struct AtlasFaceContent
{
    std::uint32_t units_per_em = 0;       ///< Font units per em.
    std::int32_t ascender = 0;            ///< Above the baseline.
    std::int32_t descender = 0;           ///< Below the baseline (negative).
    std::int32_t line_gap = 0;            ///< Added between lines.
    std::int32_t x_height = 0;            ///< Height of lowercase letters.
    std::int32_t cap_height = 0;          ///< Height of capital letters.
    std::int32_t underline_position = 0;  ///< Top of the underline.
    std::int32_t underline_thickness = 0; ///< Thickness of the underline.
    std::string family{};                 ///< Family name.
    std::string style{};                  ///< Style name.
    std::vector<FontAxisValue> axes{};    ///< Every variation axis and the value the atlas was built at.
};

/// @brief Everything an atlas holds. Glyphs are sorted by glyph index, the character map by code point,
///        kerning by left then right glyph, ligatures by first component then longest first; every page
///        has descriptor.page_width × page_height texels of descriptor.texel_format, rows from the top.
struct AtlasContent
{
    AtlasDescriptor descriptor{};                          ///< Build parameters (page_count and glyph_count are filled in).
    AtlasFaceContent face{};                               ///< Metrics and names.
    std::vector<AtlasGlyph> glyphs{};                      ///< GLYPHS.
    std::vector<CharacterMapping> characters{};            ///< CHARACTER_MAP.
    std::vector<KerningPair> kerning{};                    ///< KERNING (omitted when empty).
    std::vector<Ligature> ligatures{};                     ///< LIGATURES (omitted when empty).
    std::vector<std::byte> shaping_font{};                 ///< SHAPING_FONT (omitted when empty).
    std::vector<std::vector<std::byte>> pages{};           ///< PIXELS, one per page.
    std::uint64_t source_font_hash = 0;                    ///< xxHash64 of the font file.
    AtlasCompression compression = AtlasCompression::ZSTD; ///< Of the pixels and the shaping font.
    int compression_level = 19;                            ///< zstd level (1 to 22).
};

/// @brief The `.uiatlas` blob of @p content.
/// @throws gfx::InvalidUsageError when the content is inconsistent (unsorted tables, pages of the wrong
///         size, a texel format that does not fit the field kind, references to missing glyphs).
[[nodiscard]] auto write_atlas(const AtlasContent& content) -> std::vector<std::byte>;

} // namespace gfx::ui
