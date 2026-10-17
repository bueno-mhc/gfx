// Font decoders: FreeType on every format (TrueType, CFF, CFF2, WOFF, WOFF2, collections, variable
// fonts), stb_truetype on what it reads (and the descriptive errors for what it does not), and the two
// agreeing on metrics, character maps, advances and outlines.
#include <gtest/gtest.h>

import std;
import gfx.tests.ui_atlas_support;

namespace
{

using namespace gfx::ui;
using gfx::tests::load_font;

/// The glyph of @p code_point in @p face.
auto
glyph_of(const FontFace& face,
         char32_t code_point) -> GlyphIndex
{
    const std::vector<CharacterMapping> map = face.character_map();
    const auto mapping = std::ranges::find(map, code_point, &CharacterMapping::code_point);
    return mapping == map.end() ? GlyphIndex{} : mapping->glyph;
}

/// The end points of every segment of @p outline, sorted (independent of where contours start).
auto
end_points(const GlyphOutline& outline) -> std::vector<std::pair<double, double>>
{
    std::vector<std::pair<double, double>> points;
    for (const OutlineContour& contour : outline.contours)
    {
        for (const OutlineSegment& segment : contour.segments)
        {
            points.emplace_back(segment.end().x, segment.end().y);
        }
    }
    std::ranges::sort(points);
    return points;
}

/// Whether @p left and @p right have the same points, within @p tolerance font units.
auto
same_points(const GlyphOutline& left,
            const GlyphOutline& right,
            double tolerance) -> bool
{
    const auto first = end_points(left);
    const auto second = end_points(right);
    return first.size() == second.size() && std::ranges::equal(first, second, [&](const auto& a, const auto& b)
    {
        return std::abs(a.first - b.first) <= tolerance && std::abs(a.second - b.second) <= tolerance;
    });
}

/// The expected message parts of @p call's capability error.
template<class Call>
auto
capability_message(Call call) -> std::string
{
    try
    {
        call();
    }
    catch (const gfx::BackendCapabilityError& error)
    {
        return error.what();
    }
    return "no BackendCapabilityError";
}

TEST(FontDecoderTest, FreetypeReadsEveryFormat)
{
    const std::unique_ptr<FontDecoder> decoder = create_font_decoder(FontDecoderKind::FREETYPE);
    EXPECT_EQ(decoder->kind(), FontDecoderKind::FREETYPE);
    for (const std::string_view name : {"NotoSans.ttf", "SourceSans3-Regular.otf", "SourceSans3VF-Upright.otf", "SourceSans3-Regular.ttf.woff",
                                        "SourceSans3-Regular.ttf.woff2"})
    {
        const FontSource font = load_font(name);
        EXPECT_EQ(decoder->face_count(font), 1U) << name;
        const std::unique_ptr<FontFace> face = decoder->open(font);
        EXPECT_EQ(face->format(), font.format()) << name;
        EXPECT_GT(face->glyph_count(), 1000U) << name;
        EXPECT_GT(face->metrics().units_per_em, 0U) << name;
        EXPECT_FALSE(face->metrics().family.empty()) << name;
        const GlyphIndex a = glyph_of(*face, U'A');
        ASSERT_FALSE(a.is_notdef()) << name;
        EXPECT_GT(face->advance(a), 0.0) << name;
        EXPECT_GT(face->outline(a).segment_count(), 3U) << name;
        EXPECT_TRUE(face->outline(glyph_of(*face, U' ')).empty()) << name;
        EXPECT_EQ(face->glyph_flags(a), AtlasGlyphFlag::NONE) << name;
    }
}

TEST(FontDecoderTest, CollectionsHaveSeveralFaces)
{
    const std::array fonts{load_font("NotoSans.ttf"), load_font("NotoSansHebrew.ttf")};
    const FontSource collection = gfx::tests::make_collection(fonts);
    for (const FontDecoderKind kind : {FontDecoderKind::FREETYPE, FontDecoderKind::STB_TRUETYPE})
    {
        const std::unique_ptr<FontDecoder> decoder = create_font_decoder(kind);
        ASSERT_EQ(decoder->face_count(collection), 2U) << to_string(kind);
        const std::unique_ptr<FontFace> latin = decoder->open(collection, 0);
        const std::unique_ptr<FontFace> hebrew = decoder->open(collection, 1);
        EXPECT_FALSE(glyph_of(*latin, U'A').is_notdef()) << to_string(kind);
        EXPECT_FALSE(glyph_of(*hebrew, U'א').is_notdef()) << to_string(kind);
        EXPECT_TRUE(glyph_of(*latin, U'א').is_notdef()) << to_string(kind);
        EXPECT_EQ(hebrew->sfnt_face_index(), 1U) << to_string(kind);
        EXPECT_THROW(static_cast<void>(decoder->open(collection, 2)), gfx::InvalidUsageError) << to_string(kind);
    }
}

TEST(FontDecoderTest, WoffAndWoff2DecodeToTheSameGlyphs)
{
    // Both compress the same TrueType font: their faces hold the same glyphs.
    const std::unique_ptr<FontDecoder> decoder = create_font_decoder(FontDecoderKind::FREETYPE);
    const std::unique_ptr<FontFace> woff = decoder->open(load_font("SourceSans3-Regular.ttf.woff"));
    const std::unique_ptr<FontFace> woff2 = decoder->open(load_font("SourceSans3-Regular.ttf.woff2"));
    ASSERT_EQ(woff->glyph_count(), woff2->glyph_count());
    EXPECT_EQ(woff->character_map().size(), woff2->character_map().size());
    for (const char32_t code_point : {U'A', U'g', U'&', U'é', U'€'})
    {
        const GlyphIndex glyph = glyph_of(*woff, code_point);
        EXPECT_EQ(glyph, glyph_of(*woff2, code_point));
        EXPECT_EQ(woff->advance(glyph), woff2->advance(glyph));
        EXPECT_TRUE(same_points(woff->outline(glyph), woff2->outline(glyph), 0.0)) << static_cast<std::uint32_t>(code_point);
    }

    // Their OpenType data is the decoded TrueType font, which HarfBuzz can read.
    for (const FontFace* face : {woff.get(), woff2.get()})
    {
        const std::span<const std::byte> sfnt = face->sfnt();
        EXPECT_EQ(detect_font_format(sfnt), FontFormat::TRUETYPE);
    }
}

TEST(FontDecoderTest, StbTruetypeAgreesWithFreetype)
{
    // Metrics, character maps, advances and outlines of TrueType and CFF fonts.
    const std::unique_ptr<FontDecoder> freetype = create_font_decoder(FontDecoderKind::FREETYPE);
    const std::unique_ptr<FontDecoder> stb = create_font_decoder(FontDecoderKind::STB_TRUETYPE);
    for (const std::string_view name : {"NotoSans.ttf", "SourceSans3-Regular.otf", "NotoSansArabic.ttf"})
    {
        const FontSource font = load_font(name);
        const std::unique_ptr<FontFace> reference = freetype->open(font);
        const std::unique_ptr<FontFace> face = stb->open(font);
        EXPECT_EQ(face->decoder(), FontDecoderKind::STB_TRUETYPE);
        EXPECT_EQ(face->glyph_count(), reference->glyph_count()) << name;
        const FontFaceMetrics expected = reference->metrics();
        const FontFaceMetrics metrics = face->metrics();
        EXPECT_EQ(metrics.units_per_em, expected.units_per_em) << name;
        EXPECT_EQ(metrics.ascender, expected.ascender) << name;
        EXPECT_EQ(metrics.descender, expected.descender) << name;
        EXPECT_EQ(metrics.line_gap, expected.line_gap) << name;
        EXPECT_EQ(metrics.x_height, expected.x_height) << name;
        EXPECT_EQ(metrics.cap_height, expected.cap_height) << name;
        EXPECT_EQ(metrics.underline_position, expected.underline_position) << name;
        EXPECT_EQ(metrics.underline_thickness, expected.underline_thickness) << name;
        EXPECT_EQ(metrics.family, expected.family) << name;

        const std::vector<CharacterMapping> map = reference->character_map();
        EXPECT_EQ(face->character_map(), map) << name;
        for (const CharacterMapping& mapping : map | std::views::take(200))
        {
            EXPECT_EQ(face->advance(mapping.glyph), reference->advance(mapping.glyph)) << name << " " << mapping.glyph.value();
            EXPECT_TRUE(same_points(face->outline(mapping.glyph), reference->outline(mapping.glyph), 1.0)) << name << " " << mapping.glyph.value();
        }
    }
}

TEST(FontDecoderTest, StbTruetypeRefusesWhatItCannotRead)
{
    const std::unique_ptr<FontDecoder> stb = create_font_decoder(FontDecoderKind::STB_TRUETYPE);
    const std::string woff = capability_message([&] { static_cast<void>(stb->open(load_font("SourceSans3-Regular.ttf.woff"))); });
    EXPECT_NE(woff.find("[gfx::ui::FONT]"), std::string::npos) << woff;
    EXPECT_NE(woff.find("WOFF"), std::string::npos) << woff;
    EXPECT_NE(woff.find("FREETYPE"), std::string::npos) << woff;
    const std::string woff2 = capability_message([&] { static_cast<void>(stb->open(load_font("SourceSans3-Regular.ttf.woff2"))); });
    EXPECT_NE(woff2.find("WOFF2"), std::string::npos) << woff2;
    const std::string cff2 = capability_message([&] { static_cast<void>(stb->open(load_font("SourceSans3VF-Upright.otf"))); });
    EXPECT_NE(cff2.find("CFF2"), std::string::npos) << cff2;

    // Variable fonts: their axes and instances are read, but only the default instance is decoded.
    const std::unique_ptr<FontFace> face = stb->open(load_font("NotoSans.ttf"));
    EXPECT_EQ(face->variation_axes().size(), 2U);
    EXPECT_FALSE(face->named_instances().empty());
    const std::array bold{FontAxisValue{.tag = font_tag("wght"), .value = 700}};
    const std::string variation = capability_message([&] { face->set_variation(bold); });
    EXPECT_NE(variation.find("default instance"), std::string::npos) << variation;
    face->set_variation({});
}

TEST(FontDecoderTest, VariableFontsHaveAxesAndInstances)
{
    for (const std::string_view name : {"NotoSans.ttf", "SourceSans3VF-Upright.otf"})
    {
        const std::unique_ptr<FontFace> face = create_font_decoder(FontDecoderKind::FREETYPE)->open(load_font(name));
        const std::vector<VariationAxis> axes = face->variation_axes();
        const auto weight = std::ranges::find(axes, font_tag("wght"), &VariationAxis::tag);
        ASSERT_NE(weight, axes.end()) << name;
        EXPECT_LE(weight->minimum, 300.0) << name;
        EXPECT_GE(weight->maximum, 800.0) << name;
        EXPECT_EQ(weight->name, "Weight") << name;

        // A named instance per style; Bold at weight 700.
        const std::vector<NamedInstance> instances = face->named_instances();
        const auto bold = std::ranges::find(instances, "Bold", &NamedInstance::name);
        ASSERT_NE(bold, instances.end()) << name;
        EXPECT_EQ(bold->coordinates.size(), axes.size()) << name;
        EXPECT_EQ(std::ranges::find(bold->coordinates, font_tag("wght"), &FontAxisValue::tag)->value, 700.0F) << name;

        // Heavier letters are wider.
        const GlyphIndex a = glyph_of(*face, U'A');
        const double regular = face->advance(a);
        face->set_variation(bold->coordinates);
        EXPECT_GT(face->advance(a), regular) << name;
        face->set_variation({});
        EXPECT_EQ(face->advance(a), regular) << name;

        // Axes the font lacks and values outside an axis are refused.
        const std::array unknown{FontAxisValue{.tag = font_tag("slnt"), .value = -10}};
        EXPECT_THROW(face->set_variation(unknown), gfx::InvalidUsageError) << name;
        const std::array heavy{FontAxisValue{.tag = font_tag("wght"), .value = 5000}};
        EXPECT_THROW(face->set_variation(heavy), gfx::InvalidUsageError) << name;
    }

    // A static font has no variation.
    const std::unique_ptr<FontFace> face = create_font_decoder(FontDecoderKind::FREETYPE)->open(load_font("SourceSans3-Regular.otf"));
    EXPECT_TRUE(face->variation_axes().empty());
    const std::array bold{FontAxisValue{.tag = font_tag("wght"), .value = 700}};
    EXPECT_THROW(face->set_variation(bold), gfx::InvalidUsageError);
}

TEST(FontDecoderTest, UnreadableFontsAreReported)
{
    const std::unique_ptr<FontDecoder> decoder = create_font_decoder(FontDecoderKind::FREETYPE);

    // Not a font at all: a capability error naming the formats.
    const FontSource text = FontSource::from_memory(std::vector<std::byte>(64, std::byte{'x'}), "notes.txt");
    const std::string message = capability_message([&] { static_cast<void>(decoder->open(text)); });
    EXPECT_NE(message.find("notes.txt"), std::string::npos) << message;

    // A font cut short, and a face it does not have.
    const FontSource font = load_font("NotoSans.ttf");
    const FontSource truncated = FontSource::from_memory(font.bytes().first(200), "truncated.ttf");
    EXPECT_THROW(static_cast<void>(decoder->open(truncated)), gfx::InvalidUsageError);
    EXPECT_THROW(static_cast<void>(decoder->open(font, 3)), gfx::InvalidUsageError);
    EXPECT_THROW(static_cast<void>(create_font_decoder(FontDecoderKind::CUSTOM)), gfx::InvalidUsageError);
}

TEST(FontDecoderTest, MetricsDescribeTheFace)
{
    const std::unique_ptr<FontFace> face = create_font_decoder(FontDecoderKind::FREETYPE)->open(load_font("NotoSans.ttf"));
    const FontFaceMetrics metrics = face->metrics();
    EXPECT_EQ(metrics.units_per_em, 1000U);
    EXPECT_GT(metrics.ascender, metrics.cap_height);
    EXPECT_GT(metrics.cap_height, metrics.x_height);
    EXPECT_GT(metrics.x_height, 0);
    EXPECT_LT(metrics.descender, 0);
    EXPECT_LT(metrics.underline_position, 0);
    EXPECT_GT(metrics.underline_thickness, 0);
    EXPECT_EQ(metrics.family, "Noto Sans");
}

} // namespace
