/// @file text_test_support.cppm
/// @brief Helpers of the text tests: the embedded test atlases and a font library holding them.
export module gfx.tests.text_support;

import std;
export import gfx.ui;
import gfx.tests.text_latin;
import gfx.tests.text_latin_basic;
import gfx.tests.text_hebrew;
import gfx.tests.text_arabic;

export namespace gfx::tests
{

/// @brief The embedded atlas of Noto Sans: Latin, punctuation, U+FFFD (with a shaping font).
[[nodiscard]] constexpr auto
latin_atlas() noexcept -> std::span<const std::byte>
{
    return text_atlases::text_latin;
}

/// @brief The embedded atlas of Noto Sans: printable ASCII, without a shaping font.
[[nodiscard]] constexpr auto
latin_basic_atlas() noexcept -> std::span<const std::byte>
{
    return text_atlases::text_latin_basic;
}

/// @brief The fonts of the text tests, 16 pixels per em.
struct TextFonts
{
    ui::FontLibrary library{};
    ui::FontAtlasHandle latin_atlas{};
    ui::FontAtlasHandle latin_basic_atlas{};
    ui::FontAtlasHandle hebrew_atlas{};
    ui::FontAtlasHandle arabic_atlas{};
    ui::FontHandle latin{};    ///< Latin, then Hebrew and Arabic for what Latin lacks.
    ui::FontHandle basic{};    ///< Latin without a shaping font (basic shaper only).
    ui::FontHandle hebrew{};   ///< Hebrew, then Latin.
    ui::FontHandle arabic{};   ///< Arabic, then Latin.
    ui::FontHandle large{};    ///< Latin at 32 pixels per em.

    TextFonts()
    {
        latin_atlas = library.add_atlas(ui::FontAtlas::load(text_atlases::text_latin));
        latin_basic_atlas = library.add_atlas(ui::FontAtlas::load(text_atlases::text_latin_basic));
        hebrew_atlas = library.add_atlas(ui::FontAtlas::load(text_atlases::text_hebrew));
        arabic_atlas = library.add_atlas(ui::FontAtlas::load(text_atlases::text_arabic));
        latin = library.add_font({.atlas = latin_atlas, .fallbacks = {hebrew_atlas, arabic_atlas}});
        basic = library.add_font({.atlas = latin_basic_atlas});
        hebrew = library.add_font({.atlas = hebrew_atlas, .fallbacks = {latin_atlas}, .language = "he"});
        arabic = library.add_font({.atlas = arabic_atlas, .fallbacks = {latin_atlas}, .language = "ar"});
        large = library.add_font({.atlas = latin_atlas, .size = 32});
    }

    /// @brief A font of @p atlas with @p shaping.
    [[nodiscard]] auto with_shaping(ui::FontAtlasHandle atlas,
                                    ui::TextShaping shaping) -> ui::FontHandle
    {
        return library.add_font({.atlas = atlas, .shaping = shaping});
    }
};

/// @brief The glyphs of @p layout as a vector (gtest prints and compares it).
[[nodiscard]] inline auto
glyph_indices(const ui::TextLayout& layout) -> std::vector<std::uint32_t>
{
    std::vector<std::uint32_t> glyphs;
    for (const ui::LaidOutGlyph& glyph : layout.glyphs)
    {
        glyphs.push_back(glyph.glyph.value());
    }
    return glyphs;
}

} // namespace gfx::tests
