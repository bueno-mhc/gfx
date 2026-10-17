// The atlas builder: what an atlas holds (glyphs, closure, kerning, ligatures, shaping font, pages),
// reproducible bytes, every field kind, texel format, font format and decoder, variable-font instances,
// missing characters, several pages, refused descriptions and custom implementations.
#include <gtest/gtest.h>

import std;
import gfx.tests.ui_atlas_support;

namespace
{

using namespace gfx::ui;
using gfx::tests::latin_description;
using gfx::tests::load_font;
using gfx::tests::view_of;

/// The median of the first three channels of an RGBA8 texel.
auto
median(std::span<const std::byte> texel) -> int
{
    const int red = std::to_integer<int>(texel[0]);
    const int green = std::to_integer<int>(texel[1]);
    const int blue = std::to_integer<int>(texel[2]);
    return std::max(std::min(red, green), std::min(std::max(red, green), blue));
}

/// The value the atlas of @p view was built at for axis @p tag.
auto
axis_value(const AtlasView& view,
           std::string_view tag) -> float
{
    const AtlasFaceMetrics face = view.face_metrics();
    for (std::size_t index = 0; index < face.axis_count; ++index)
    {
        if (face.axis(index).tag == font_tag(tag))
        {
            return face.axis(index).value;
        }
    }
    return -1;
}

/// The glyph records of @p view.
auto
glyphs_of(const AtlasView& view) -> std::vector<AtlasGlyph>
{
    std::vector<AtlasGlyph> glyphs;
    for (std::size_t index = 0; index < view.glyph_count(); ++index)
    {
        glyphs.push_back(view.glyph_at(index));
    }
    return glyphs;
}

/// Whether two glyph records are the same.
auto
same_glyph(const AtlasGlyph& left,
           const AtlasGlyph& right) -> bool
{
    return left.glyph == right.glyph && left.flags == right.flags && left.page == right.page && left.advance == right.advance
           && left.plane.left == right.plane.left && left.plane.top == right.plane.top && left.plane.right == right.plane.right
           && left.plane.bottom == right.plane.bottom && left.texels.left == right.texels.left && left.texels.top == right.texels.top
           && left.texels.right == right.texels.right && left.texels.bottom == right.texels.bottom;
}

TEST(AtlasBuilderTest, BuildsAnMtsdfAtlasOfLatinText)
{
    const AtlasBuildResult result = build_atlas(load_font("NotoSans.ttf"), latin_description());
    const AtlasView view = view_of(result.atlas);
    const AtlasDescriptor& descriptor = view.descriptor();
    EXPECT_EQ(descriptor.kind, SdfKind::MTSDF);
    EXPECT_EQ(descriptor.texel_format, AtlasTexelFormat::R8G8B8A8_UNORM);
    EXPECT_EQ(descriptor.decoder, FontDecoderKind::FREETYPE);
    EXPECT_EQ(descriptor.generator, SdfGeneratorKind::MSDFGEN);
    EXPECT_EQ(descriptor.packer, AtlasPackerKind::TIGHT);
    EXPECT_EQ(descriptor.em_size, 32.0F);
    EXPECT_EQ(descriptor.distance_range, 4.0F);
    EXPECT_EQ(view.header().source_font_hash, load_font("NotoSans.ttf").hash());

    // Every character, `.notdef`, and the report.
    EXPECT_EQ(view.character_count(), 95U);
    EXPECT_TRUE(result.report.missing_characters.empty());
    EXPECT_EQ(result.report.glyph_count, view.glyph_count());
    EXPECT_GE(view.glyph_count(), 96U);
    EXPECT_TRUE(view.find_glyph(GlyphIndex{0})->has_box());
    EXPECT_EQ(result.report.atlas_size, result.atlas.size());

    // A letter and a space.
    const AtlasGlyph a = *view.find_glyph(*view.find_character(U'A'));
    EXPECT_TRUE(a.has_box());
    EXPECT_GT(a.advance, 0.5F);
    EXPECT_LT(a.advance, 0.8F);
    EXPECT_GT(a.plane.top, 0.7F);
    EXPECT_LT(a.plane.bottom, 0.0F);
    const AtlasGlyph space = *view.find_glyph(*view.find_character(U' '));
    EXPECT_EQ(space.flags, AtlasGlyphFlag::WHITESPACE);
    EXPECT_FALSE(space.has_box());
    EXPECT_GT(space.advance, 0.2F);

    // Kerning (A then V comes closer) and the fi ligature, for the basic shaper.
    EXPECT_LT(view.find_kerning(*view.find_character(U'A'), *view.find_character(U'V')), 0.0F);
    EXPECT_EQ(result.report.kerning_pair_count, view.kerning_count());
    bool found = false;
    for (std::size_t index = 0; index < view.ligature_count(); ++index)
    {
        const Ligature ligature = view.ligature_at(index);
        found = found
                || (ligature.component_count == 2 && ligature.components[0] == *view.find_character(U'f')
                    && ligature.components[1] == *view.find_character(U'i'));
    }
    EXPECT_TRUE(found);

    // The face: names and the instance it was built at (the default one).
    const AtlasFaceMetrics face = view.face_metrics();
    EXPECT_EQ(face.units_per_em, 1000U);
    EXPECT_TRUE(face.family == "Noto Sans");
    EXPECT_EQ(face.axis_count, 2U);
    EXPECT_EQ(axis_value(view, "wdth"), 100.0F);
    EXPECT_EQ(axis_value(view, "wght"), 400.0F);

    // The shaping font: the subset, its glyphs those of the atlas.
    const std::vector<std::byte> shaping = gfx::tests::decompress(view, *view.shaping_font());
    EXPECT_EQ(shaping.size(), result.report.shaping_font_size);
    const std::unique_ptr<FontFace> subset = create_font_decoder(FontDecoderKind::FREETYPE)->open(FontSource::from_memory(shaping));
    EXPECT_EQ(subset->glyph_count(), view.glyph_count());
    EXPECT_TRUE(subset->variation_axes().empty());

    // The pixels: the letter's box is inside on some texels, outside on its border.
    const std::vector<std::byte> page = gfx::tests::decompress(view, view.page(0));
    const auto texel = [&](float x, float y)
    {
        return std::span{page}.subspan((static_cast<std::size_t>(y) * descriptor.page_width + static_cast<std::size_t>(x)) * 4, 4);
    };
    int inside = 0;
    for (float y = a.texels.top; y < a.texels.bottom; ++y)
    {
        for (float x = a.texels.left; x < a.texels.right; ++x)
        {
            inside += median(texel(x, y)) > 128 ? 1 : 0;
        }
    }
    EXPECT_GT(inside, 20);
    EXPECT_LT(median(texel(a.texels.left, a.texels.top)), 128);
    EXPECT_LT(std::to_integer<int>(texel(a.texels.left, a.texels.top)[3]), 128);
}

TEST(AtlasBuilderTest, AtlasesAreReproducible)
{
    // The same bytes, whatever the number of threads, build after build.
    const FontSource font = load_font("NotoSans.ttf");
    AtlasBuildDescription description = latin_description();
    description.characters.add(CharacterSet::LATIN_1_SUPPLEMENT);
    description.thread_count = 1;
    const std::vector<std::byte> single = build_atlas(font, description).atlas;
    description.thread_count = 8;
    EXPECT_EQ(build_atlas(font, description).atlas, single);
    EXPECT_EQ(build_atlas(font, description).atlas, single);

    // The built-in generator too.
    description.field.kind = SdfKind::SDF;
    description.generator = SdfGeneratorKind::BUILT_IN;
    const std::vector<std::byte> built_in = build_atlas(font, description).atlas;
    description.thread_count = 1;
    EXPECT_EQ(build_atlas(font, description).atlas, built_in);
}

TEST(AtlasBuilderTest, EveryFieldKindAndTexelFormatBuilds)
{
    const FontSource font = load_font("NotoSans.ttf");
    for (const SdfKind kind : {SdfKind::SDF, SdfKind::PSDF, SdfKind::MSDF, SdfKind::MTSDF})
    {
        const bool single = kind == SdfKind::SDF || kind == SdfKind::PSDF;
        for (const AtlasTexelFormat format : single ? std::array{AtlasTexelFormat::R8_UNORM, AtlasTexelFormat::R16_SFLOAT}
                                                    : std::array{AtlasTexelFormat::R8G8B8A8_UNORM, AtlasTexelFormat::R16G16B16A16_SFLOAT})
        {
            AtlasBuildDescription description = latin_description();
            description.field.kind = kind;
            description.texel_format = format;
            const std::vector<std::byte> atlas = build_atlas(font, description).atlas;
            const AtlasView view = view_of(atlas);
            EXPECT_EQ(view.descriptor().kind, kind);
            EXPECT_EQ(view.descriptor().texel_format, format);
        }
        // Formats that do not fit the kind are refused.
        AtlasBuildDescription mismatched = latin_description();
        mismatched.field.kind = kind;
        mismatched.texel_format = single ? AtlasTexelFormat::R8G8B8A8_UNORM : AtlasTexelFormat::R8_UNORM;
        EXPECT_THROW(static_cast<void>(build_atlas(font, mismatched)), gfx::InvalidUsageError) << to_string(kind);
    }

    // Default formats: one channel for SDF, four for MTSDF.
    AtlasBuildDescription description = latin_description();
    description.field.kind = SdfKind::SDF;
    const std::vector<std::byte> atlas = build_atlas(font, description).atlas;
    EXPECT_EQ(view_of(atlas).descriptor().texel_format, AtlasTexelFormat::R8_UNORM);
}

TEST(AtlasBuilderTest, EveryFontFormatBuilds)
{
    // FreeType: TrueType, CFF, CFF2, WOFF, WOFF2 and a face of a collection.
    for (const std::string_view name : {"SourceSans3-Regular.otf", "SourceSans3VF-Upright.otf", "SourceSans3-Regular.ttf.woff",
                                        "SourceSans3-Regular.ttf.woff2"})
    {
        const AtlasBuildResult result = build_atlas(load_font(name), latin_description());
        EXPECT_TRUE(result.report.missing_characters.empty()) << name;
        EXPECT_EQ(view_of(result.atlas).character_count(), 95U) << name;
    }
    const std::array fonts{load_font("NotoSans.ttf"), load_font("NotoSansHebrew.ttf")};
    const FontSource collection = gfx::tests::make_collection(fonts);
    AtlasBuildDescription hebrew{.face_index = 1, .characters = CharacterSelection::parse("U+05D0-U+05EA")};
    for (const FontDecoderKind decoder : {FontDecoderKind::FREETYPE, FontDecoderKind::STB_TRUETYPE})
    {
        hebrew.decoder = decoder;
        const std::vector<std::byte> atlas = build_atlas(collection, hebrew).atlas;
        const AtlasView view = view_of(atlas);
        EXPECT_EQ(view.character_count(), 27U) << to_string(decoder);
        EXPECT_EQ(view.descriptor().face_index, 1U);
        EXPECT_EQ(view.descriptor().decoder, decoder);
    }
}

TEST(AtlasBuilderTest, StbTruetypeAndFreetypeBuildTheSameGlyphs)
{
    // TrueType: the same records (boxes, advances, placement), and true distances (alpha) within 2 of 255.
    // The decoders do not start every contour at the same point, so msdfgen colors some edges differently:
    // the color channels may differ (each decoder gives the same bytes build after build).
    AtlasBuildDescription description = latin_description();
    const std::vector<std::byte> reference = build_atlas(load_font("NotoSans.ttf"), description).atlas;
    const AtlasView expected = view_of(reference);
    description.decoder = FontDecoderKind::STB_TRUETYPE;
    const std::vector<std::byte> atlas = build_atlas(load_font("NotoSans.ttf"), description).atlas;
    const AtlasView view = view_of(atlas);
    EXPECT_EQ(view.descriptor().decoder, FontDecoderKind::STB_TRUETYPE);
    EXPECT_TRUE(std::ranges::equal(glyphs_of(view), glyphs_of(expected), same_glyph));
    const std::vector<std::byte> texels = gfx::tests::decompress(view, view.page(0));
    const std::vector<std::byte> expected_texels = gfx::tests::decompress(expected, expected.page(0));
    ASSERT_EQ(texels.size(), expected_texels.size());
    int largest = 0;
    for (std::size_t alpha = 3; alpha < texels.size(); alpha += 4)
    {
        largest = std::max(largest, std::abs(std::to_integer<int>(texels[alpha]) - std::to_integer<int>(expected_texels[alpha])));
    }
    EXPECT_LE(largest, 2);

    // CFF: stb_truetype truncates fractional coordinates where FreeType rounds them, so boxes may differ
    // by a texel; advances are the same.
    AtlasBuildDescription cff = latin_description();
    const std::vector<std::byte> freetype_atlas = build_atlas(load_font("SourceSans3-Regular.otf"), cff).atlas;
    const AtlasView freetype = view_of(freetype_atlas);
    cff.decoder = FontDecoderKind::STB_TRUETYPE;
    const std::vector<std::byte> stb_atlas = build_atlas(load_font("SourceSans3-Regular.otf"), cff).atlas;
    const AtlasView stb = view_of(stb_atlas);
    ASSERT_EQ(stb.glyph_count(), freetype.glyph_count());
    for (std::size_t index = 0; index < stb.glyph_count(); ++index)
    {
        EXPECT_EQ(stb.glyph_at(index).advance, freetype.glyph_at(index).advance) << index;
        EXPECT_NEAR(stb.glyph_at(index).plane.top, freetype.glyph_at(index).plane.top, 0.04F) << index;
    }
}

TEST(AtlasBuilderTest, ClosureAddsTheFormsShapingNeeds)
{
    // Arabic letters bring their initial, medial and final forms; what HarfBuzz shapes is in the atlas.
    const AtlasBuildDescription description{.characters = CharacterSelection::parse("ARABIC")};
    const AtlasBuildResult result = build_atlas(load_font("NotoSansArabic.ttf"), description);
    const AtlasView view = view_of(result.atlas);
    EXPECT_GT(view.glyph_count(), view.character_count() + 100);
    const std::vector<std::byte> shaping = gfx::tests::decompress(view, *view.shaping_font());
    for (const std::string_view text : {"\xD8\xB3\xD9\x85", "\xD8\xA8\xD8\xA8\xD8\xA8", "\xD9\x84\xD8\xA7"})
    {
        const std::vector<std::uint32_t> glyphs = gfx::tests::shape_glyphs(shaping, text);
        ASSERT_FALSE(glyphs.empty());
        for (const std::uint32_t glyph : glyphs)
        {
            EXPECT_NE(glyph, 0U) << text;
            EXPECT_TRUE(view.find_glyph(GlyphIndex{glyph}).has_value()) << text << " " << glyph;
        }
    }
    // Arabic is not a script of the basic shaper: no pair tables for it.
    EXPECT_EQ(view.kerning_count(), 0U);
    EXPECT_EQ(view.ligature_count(), 0U);
}

TEST(AtlasBuilderTest, VariableFontsAreBuiltAtTheirInstance)
{
    const FontSource font = load_font("NotoSans.ttf");
    const auto advance_of_a = [](const AtlasView& view) { return view.find_glyph(*view.find_character(U'A'))->advance; };
    const std::vector<std::byte> regular_atlas = build_atlas(font, latin_description()).atlas;
    const AtlasView regular = view_of(regular_atlas);

    // Weight 700 and the named instance Bold give the same glyphs; the style names the instance.
    AtlasBuildDescription weight = latin_description();
    weight.variation = {{.tag = font_tag("wght"), .value = 700}};
    const std::vector<std::byte> weighted = build_atlas(font, weight).atlas;
    AtlasBuildDescription named = latin_description();
    named.named_instance = "Bold";
    const std::vector<std::byte> bold = build_atlas(font, named).atlas;
    const AtlasView weighted_view = view_of(weighted);
    const AtlasView bold_view = view_of(bold);
    EXPECT_GT(advance_of_a(bold_view), advance_of_a(regular));
    EXPECT_TRUE(std::ranges::equal(glyphs_of(bold_view), glyphs_of(weighted_view), same_glyph));
    EXPECT_TRUE(bold_view.face_metrics().style == "Bold");
    EXPECT_EQ(axis_value(bold_view, "wght"), 700.0F);
    EXPECT_EQ(axis_value(weighted_view, "wght"), 700.0F);
    EXPECT_EQ(axis_value(weighted_view, "wdth"), 100.0F);

    // CFF2 variable fonts too.
    AtlasBuildDescription cff2 = latin_description();
    cff2.variation = {{.tag = font_tag("wght"), .value = 700}};
    EXPECT_NO_THROW(static_cast<void>(build_atlas(load_font("SourceSans3VF-Upright.otf"), cff2)));

    // Instances the font lacks, variations of static fonts, and both at once are refused.
    AtlasBuildDescription unknown = latin_description();
    unknown.named_instance = "Hairline";
    try
    {
        static_cast<void>(build_atlas(font, unknown));
        FAIL() << "an unknown instance must throw";
    }
    catch (const gfx::InvalidUsageError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("'Bold'"), std::string_view::npos) << error.what();
    }
    EXPECT_THROW(static_cast<void>(build_atlas(load_font("SourceSans3-Regular.otf"), weight)), gfx::InvalidUsageError);
    AtlasBuildDescription both = weight;
    both.named_instance = "Bold";
    EXPECT_THROW(static_cast<void>(build_atlas(font, both)), gfx::InvalidUsageError);
    AtlasBuildDescription outside = latin_description();
    outside.variation = {{.tag = font_tag("wght"), .value = 2000}};
    EXPECT_THROW(static_cast<void>(build_atlas(font, outside)), gfx::InvalidUsageError);
}

TEST(AtlasBuilderTest, WithoutAShapingFontTheFaceItselfIsRead)
{
    // No subset: `.notdef` and the characters' glyphs only, no shaping font, no pair tables.
    const FontSource font = load_font("NotoSans.ttf");
    AtlasBuildDescription description = latin_description();
    description.shaping_font = false;
    description.kerning = false;
    description.ligatures = false;
    description.variation = {{.tag = font_tag("wght"), .value = 700}};
    const std::vector<std::byte> atlas = build_atlas(font, description).atlas;
    const AtlasView view = view_of(atlas);
    EXPECT_FALSE(view.shaping_font().has_value());
    EXPECT_EQ(view.kerning_count(), 0U);
    EXPECT_EQ(view.glyph_count(), 96U);

    // FreeType applies the variation itself: the same advances as the static instance hb-subset makes.
    AtlasBuildDescription subset = latin_description();
    subset.variation = description.variation;
    const std::vector<std::byte> instanced_atlas = build_atlas(font, subset).atlas;
    const AtlasView instanced = view_of(instanced_atlas);
    for (const char32_t code_point : {U'A', U'm', U'W', U'1'})
    {
        EXPECT_NEAR(view.find_glyph(*view.find_character(code_point))->advance, instanced.find_glyph(*instanced.find_character(code_point))->advance,
                    0.0015F)
            << static_cast<std::uint32_t>(code_point);
    }

    // stb_truetype applies no variation; kerning needs the shaping font.
    description.decoder = FontDecoderKind::STB_TRUETYPE;
    EXPECT_THROW(static_cast<void>(build_atlas(font, description)), gfx::BackendCapabilityError);
    AtlasBuildDescription kerning = latin_description();
    kerning.shaping_font = false;
    EXPECT_THROW(static_cast<void>(build_atlas(font, kerning)), gfx::InvalidUsageError);
}

TEST(AtlasBuilderTest, MissingCharactersAreReported)
{
    const AtlasBuildDescription description{.characters = CharacterSelection::parse("U+0041 U+4E2D U+1F600")};
    const AtlasBuildResult result = build_atlas(load_font("NotoSans.ttf"), description);
    EXPECT_EQ(result.report.missing_characters, (std::vector<char32_t>{U'中', U'\U0001F600'}));
    EXPECT_EQ(view_of(result.atlas).character_count(), 1U);
}

TEST(AtlasBuilderTest, LargeAtlasesUseSeveralPages)
{
    AtlasBuildDescription description = latin_description();
    description.field.em_size = 48;
    description.packing.maximum_dimension = 128;
    const AtlasBuildResult result = build_atlas(load_font("NotoSans.ttf"), description);
    const AtlasView view = view_of(result.atlas);
    EXPECT_GT(view.descriptor().page_count, 1U);
    EXPECT_EQ(view.descriptor().page_width, 128U);
    EXPECT_EQ(result.report.page_count, view.descriptor().page_count);
    std::set<std::uint16_t> pages;
    for (const AtlasGlyph& glyph : glyphs_of(view))
    {
        if (glyph.has_box())
        {
            pages.insert(glyph.page);
            EXPECT_LE(glyph.texels.right, 128.0F);
        }
    }
    EXPECT_EQ(pages.size(), view.descriptor().page_count);
}

TEST(AtlasBuilderTest, InvalidDescriptionsAreRefused)
{
    const FontSource font = load_font("NotoSans.ttf");
    EXPECT_THROW(static_cast<void>(build_atlas(font, {})), gfx::InvalidUsageError);
    AtlasBuildDescription size = latin_description();
    size.field.em_size = 0;
    EXPECT_THROW(static_cast<void>(build_atlas(font, size)), gfx::InvalidUsageError);
    AtlasBuildDescription built_in = latin_description();
    built_in.generator = SdfGeneratorKind::BUILT_IN;
    EXPECT_THROW(static_cast<void>(build_atlas(font, built_in)), gfx::BackendCapabilityError);
    AtlasBuildDescription custom = latin_description();
    custom.decoder = FontDecoderKind::CUSTOM;
    EXPECT_THROW(static_cast<void>(build_atlas(font, custom)), gfx::InvalidUsageError);
    AtlasBuildDescription level = latin_description();
    level.compression_level = 30;
    EXPECT_THROW(static_cast<void>(build_atlas(font, level)), gfx::InvalidUsageError);
    AtlasBuildDescription stb_woff = latin_description();
    stb_woff.decoder = FontDecoderKind::STB_TRUETYPE;
    EXPECT_THROW(static_cast<void>(build_atlas(load_font("SourceSans3-Regular.ttf.woff2"), stb_woff)), gfx::BackendCapabilityError);
}

/// A face of two squares: 'A' and, drawn in color by the font, 'B'.
class SquareFace final : public FontFace
{
public:
    [[nodiscard]] auto decoder() const noexcept -> FontDecoderKind override
    {
        return FontDecoderKind::CUSTOM;
    }

    [[nodiscard]] auto format() const noexcept -> FontFormat override
    {
        return FontFormat::CUSTOM;
    }

    [[nodiscard]] auto metrics() const -> FontFaceMetrics override
    {
        return {.units_per_em = 100, .ascender = 80, .descender = -20, .family = "Squares", .style = "Regular"};
    }

    [[nodiscard]] auto glyph_count() const noexcept -> std::uint32_t override
    {
        return 3;
    }

    [[nodiscard]] auto character_map() const -> std::vector<CharacterMapping> override
    {
        return {{.code_point = U'A', .glyph = GlyphIndex{1}}, {.code_point = U'B', .glyph = GlyphIndex{2}}};
    }

    [[nodiscard]] auto advance(GlyphIndex) const -> double override
    {
        return 60;
    }

    [[nodiscard]] auto outline(GlyphIndex glyph) const -> GlyphOutline override
    {
        if (glyph.is_notdef())
        {
            return {};
        }
        OutlineBuilder builder;
        builder.move_to({10, 0});
        builder.line_to({50, 0});
        builder.line_to({50, 40});
        builder.line_to({10, 40});
        return builder.finish();
    }

    [[nodiscard]] auto glyph_flags(GlyphIndex glyph) const -> AtlasGlyphFlag override
    {
        return glyph == GlyphIndex{2} ? AtlasGlyphFlag::COLOR_LAYERS : AtlasGlyphFlag::NONE;
    }
};

/// Opens SquareFace whatever the source.
class SquareDecoder final : public FontDecoder
{
public:
    [[nodiscard]] auto kind() const noexcept -> FontDecoderKind override
    {
        return FontDecoderKind::CUSTOM;
    }

    [[nodiscard]] auto face_count(const FontSource&) const -> std::uint32_t override
    {
        return 1;
    }

    [[nodiscard]] auto open(const FontSource&,
                            std::uint32_t) const -> std::unique_ptr<FontFace> override
    {
        return std::make_unique<SquareFace>();
    }
};

/// Places every box on one row, 64 texels apart, or (misbehaving) outside the page.
class RowPacker final : public AtlasPacker
{
public:
    explicit RowPacker(bool misbehave)
        : m_misbehave{misbehave}
    {
    }

    [[nodiscard]] auto kind() const noexcept -> AtlasPackerKind override
    {
        return AtlasPackerKind::CUSTOM;
    }

    [[nodiscard]] auto pack(std::span<const AtlasBoxSize> boxes,
                            const AtlasPackingDescription&) const -> AtlasPacking override
    {
        AtlasPacking packing{.page_width = 256, .page_height = 64, .page_count = 1};
        for (std::size_t index = 0; index < boxes.size(); ++index)
        {
            packing.placements.push_back({.x = m_misbehave ? 250U : static_cast<std::uint32_t>(64 * index)});
        }
        return packing;
    }

private:
    bool m_misbehave = false;
};

TEST(AtlasBuilderTest, CustomDecodersAndPackersAreUsed)
{
    AtlasBuildDescription description{.characters = CharacterSelection::parse("U+0041 U+0042"),
                                      .decoder = FontDecoderKind::CUSTOM,
                                      .packer = AtlasPackerKind::CUSTOM,
                                      .shaping_font = false,
                                      .kerning = false,
                                      .ligatures = false};
    AtlasBuilder builder{description};
    builder.use_decoder(std::make_unique<SquareDecoder>()).use_packer(std::make_unique<RowPacker>(false));
    const AtlasBuildResult result = builder.build(FontSource::from_memory(std::vector<std::byte>(16), "squares"));
    const AtlasView view = view_of(result.atlas);
    EXPECT_EQ(view.descriptor().decoder, FontDecoderKind::CUSTOM);
    EXPECT_EQ(view.descriptor().packer, AtlasPackerKind::CUSTOM);
    EXPECT_EQ(view.descriptor().page_width, 256U);
    EXPECT_EQ(view.glyph_count(), 3U);
    EXPECT_EQ(result.report.color_glyph_count, 1U);
    EXPECT_EQ(view.find_glyph(GlyphIndex{2})->flags, AtlasGlyphFlag::COLOR_LAYERS);
    EXPECT_EQ(view.find_glyph(GlyphIndex{2})->texels.left, 128.5F);
    EXPECT_EQ(view.find_glyph(GlyphIndex{1})->advance, 0.6F);
    EXPECT_TRUE(view.face_metrics().family == "Squares");

    // A packer that places boxes outside its page is caught.
    AtlasBuilder misbehaving{description};
    misbehaving.use_decoder(std::make_unique<SquareDecoder>()).use_packer(std::make_unique<RowPacker>(true));
    EXPECT_THROW(static_cast<void>(misbehaving.build(FontSource::from_memory(std::vector<std::byte>(16), "squares"))), gfx::InvalidUsageError);
}

} // namespace
