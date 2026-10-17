/// @file atlas_builder.cppm
/// @brief AtlasBuilder: builds a `.uiatlas` from a font. It opens the face, subsets it with hb-subset (the
///        requested characters, closed over ligatures and contextual forms, at the requested variation),
///        reads every glyph of the subset, lays out and packs the boxes, generates the distance fields on
///        several threads, extracts kerning and ligatures for the basic shaper and writes the blob. Same
///        inputs and generator version give the same bytes, whatever the number of threads.
export module gfx.ui.atlas_tools:atlas_builder;

import std;
import gfx.ui.enums;
import gfx.ui.atlas_format;
import :font_source;
import :character_selection;
import :font_decoder;
import :sdf_generator;
import :atlas_packer;

export namespace gfx::ui
{

/// @brief What to build.
struct AtlasBuildDescription
{
    std::uint32_t face_index = 0;                          ///< Face of a collection.
    CharacterSelection characters{};                       ///< Characters the atlas must hold (at least one).
    std::vector<FontAxisValue> variation{};                ///< Axis values of a variable font (others keep their default).
    std::string named_instance{};                          ///< Or a named instance ("Bold"); not with variation.
    SdfGenerationDescription field{};                      ///< Field kind, em size, distance range, padding, coloring.
    std::optional<AtlasTexelFormat> texel_format{};        ///< Default: R8_UNORM for SDF and PSDF, R8G8B8A8_UNORM otherwise.
    AtlasPackingDescription packing{};                     ///< Page constraints.
    FontDecoderKind decoder = FontDecoderKind::FREETYPE;   ///< CUSTOM needs AtlasBuilder::use_decoder().
    SdfGeneratorKind generator = SdfGeneratorKind::MSDFGEN; ///< CUSTOM needs AtlasBuilder::use_generator().
    AtlasPackerKind packer = AtlasPackerKind::TIGHT;       ///< CUSTOM needs AtlasBuilder::use_packer().
    bool shaping_font = true;                              ///< Subset the font for HarfBuzz (SHAPING_FONT; needs FontFace::sfnt()).
    bool kerning = true;                                   ///< Pair kerning for the basic shaper (needs shaping_font).
    bool ligatures = true;                                 ///< Standard ligatures for the basic shaper (needs shaping_font).
    AtlasCompression compression = AtlasCompression::ZSTD; ///< Of the pixels and the shaping font.
    int compression_level = 19;                            ///< zstd level (1 to 22).
    std::uint32_t thread_count = 0;                        ///< Threads generating fields; 0: one per hardware thread.
};

/// @brief What a build produced, for the tool's output and for tests.
struct AtlasBuildReport
{
    std::vector<char32_t> missing_characters{}; ///< Requested characters the font lacks (shown as `.notdef`).
    std::uint32_t glyph_count = 0;              ///< Glyphs in the atlas (`.notdef`, closure glyphs and spaces included).
    std::uint32_t color_glyph_count = 0;        ///< Glyphs the font draws in color, held as monochrome outlines.
    std::uint32_t page_count = 0;               ///< Pages.
    std::uint32_t page_width = 0;               ///< Texels.
    std::uint32_t page_height = 0;              ///< Texels.
    std::size_t kerning_pair_count = 0;         ///< KERNING records.
    std::size_t ligature_count = 0;             ///< LIGATURES records.
    std::size_t shaping_font_size = 0;          ///< Bytes of the subset font (uncompressed).
    std::size_t atlas_size = 0;                 ///< Bytes of the blob.
};

/// @brief The blob and its report.
struct AtlasBuildResult
{
    std::vector<std::byte> atlas{}; ///< The `.uiatlas` blob.
    AtlasBuildReport report{};      ///< What it holds.
};

/// @brief Builds atlases with the decoder, generator and packer of its description, or custom ones.
class AtlasBuilder
{
public:
    /// @brief A builder for @p description (validated by build()).
    explicit AtlasBuilder(AtlasBuildDescription description);

    /// @brief Uses @p decoder instead of the description's (whose kind should then be CUSTOM).
    auto use_decoder(std::unique_ptr<FontDecoder> decoder) -> AtlasBuilder&;

    /// @brief Uses @p generator instead of the description's.
    auto use_generator(std::unique_ptr<SdfGenerator> generator) -> AtlasBuilder&;

    /// @brief Uses @p packer instead of the description's.
    auto use_packer(std::unique_ptr<AtlasPacker> packer) -> AtlasBuilder&;

    /// @brief The description.
    [[nodiscard]] auto description() const noexcept -> const AtlasBuildDescription&
    {
        return m_description;
    }

    /// @brief Builds the atlas of @p font.
    /// @throws gfx::InvalidUsageError for an invalid description (no characters, a texel format that does
    ///         not fit the field kind, an unknown axis or instance, both a variation and an instance, a CUSTOM
    ///         kind without its implementation) or a glyph box larger than a page.
    /// @throws gfx::BackendCapabilityError (UiCapabilityError) when the decoder cannot read the font or
    ///         provide its OpenType data for a shaping font, or the generator cannot generate the field kind.
    [[nodiscard]] auto build(const FontSource& font) const -> AtlasBuildResult;

private:
    AtlasBuildDescription m_description{};
    std::unique_ptr<FontDecoder> m_decoder{};
    std::unique_ptr<SdfGenerator> m_generator{};
    std::unique_ptr<AtlasPacker> m_packer{};
};

/// @brief Builds the atlas of @p font described by @p description with gfx's decoders, generators and packers.
[[nodiscard]] auto build_atlas(const FontSource& font,
                               const AtlasBuildDescription& description) -> AtlasBuildResult;

} // namespace gfx::ui
