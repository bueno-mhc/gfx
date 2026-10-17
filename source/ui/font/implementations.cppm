/// @file implementations.cppm
/// @brief Internal partition of gfx.ui.atlas_tools: the factories of gfx's decoders and generators (each
///        class is declared in the unit that defines it) and the HarfBuzz steps of the atlas builder.
module gfx.ui.atlas_tools:implementations;

import std;
import gfx.ui.enums;
import gfx.ui.errors;
import gfx.ui.atlas_format;
import :font_decoder;
import :sdf_generator;

namespace gfx::ui
{

/// @brief Throws an InvalidUsageError of the FONT subsystem.
[[noreturn]] inline void
throw_font_usage_error(std::string_view operation,
                       std::string problem,
                       std::string suggestion,
                       std::source_location location = std::source_location::current())
{
    throw InvalidUsageError({.backend = UiSubsystem::FONT,
                             .operation = std::string{operation},
                             .problem = std::move(problem),
                             .suggestion = std::move(suggestion)},
                            location);
}

/// @brief The FreeType decoder (freetype_decoder.cpp).
[[nodiscard]] auto make_freetype_decoder() -> std::unique_ptr<FontDecoder>;

/// @brief The stb_truetype decoder (stb_truetype_decoder.cpp).
[[nodiscard]] auto make_stb_truetype_decoder() -> std::unique_ptr<FontDecoder>;

/// @brief The msdfgen generator (msdfgen_generator.cpp).
[[nodiscard]] auto make_msdfgen_generator() -> std::unique_ptr<SdfGenerator>;

/// @brief The built-in exact-distance generator (built_in_generator.cpp).
[[nodiscard]] auto make_built_in_generator() -> std::unique_ptr<SdfGenerator>;

/// @brief The subset of the OpenType font @p sfnt (face @p face_index of a collection) holding the glyphs
///        of @p code_points and every glyph its layout tables can substitute them with (ligatures,
///        contextual and positional forms), its layout tables kept, at the instance of @p axes (every
///        axis pinned: the subset is a static font). hb-subset.
/// @throws gfx::BackendCapabilityError (UiCapabilityError) when HarfBuzz cannot subset or instance the font.
[[nodiscard]] auto subset_shaping_font(std::span<const std::byte> sfnt,
                                       std::uint32_t face_index,
                                       std::span<const char32_t> code_points,
                                       std::span<const FontAxisValue> axes,
                                       std::string_view font_name) -> std::vector<std::byte>;

/// @brief Kerning pairs and standard ligatures of a shaping font, for the basic shaper.
struct PairTables
{
    std::vector<KerningPair> kerning{};  ///< Sorted by left then right glyph, em.
    std::vector<Ligature> ligatures{};   ///< Sorted by first component, longest first.
};

/// @brief The pair kerning and the standard ligatures (two to four characters) HarfBuzz applies to the
///        characters of @p characters written in scripts the basic shaper handles (Latin, Greek, Cyrillic
///        and common punctuation), found by shaping every pair (and extending the pairs that ligate).
[[nodiscard]] auto extract_pair_tables(std::span<const std::byte> shaping_font,
                                       std::span<const CharacterMapping> characters,
                                       bool kerning,
                                       bool ligatures) -> PairTables;

} // namespace gfx::ui
