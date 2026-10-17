// The third-party libraries of the UI's font pipeline, as gfx's dependency helpers build them on this
// toolchain: each does a small real job with the fetched test fonts. A plain translation unit (no
// modules): these are C and C++ headers.
#include <gtest/gtest.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include <hb-subset.h>
#include <hb.h>

#include <SheenBidi/SheenBidi.h>

#include <graphemebreak.h>
#include <linebreak.h>
#include <unibreakbase.h>

#include <zstd.h>

#include <msdf-atlas-gen/msdf-atlas-gen.h>
#include <msdfgen-ext.h>
#include <msdfgen.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace
{

/// The bytes of the test font @p name.
auto
read_font(const std::string& name) -> std::vector<unsigned char>
{
    std::ifstream file{std::string{GFX_UI_TEST_FONT_DIRECTORY} + "/" + name, std::ios::binary};
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

/// The glyphs HarfBuzz shapes @p text into with @p font.
auto
shape(hb_font_t* font,
      const char* text) -> std::vector<hb_codepoint_t>
{
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, text, -1, 0, -1);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font, buffer, nullptr, 0);
    unsigned int count = 0;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
    std::vector<hb_codepoint_t> glyphs;
    for (unsigned int index = 0; index < count; ++index)
    {
        glyphs.push_back(infos[index].codepoint);
    }
    hb_buffer_destroy(buffer);
    return glyphs;
}

TEST(FreeTypeTest, ReadsVariableFontsAndTheirAxes)
{
    // The pinned version, a variable TrueType font with its two axes.
    const std::vector<unsigned char> data = read_font("NotoSans.ttf");
    ASSERT_GT(data.size(), 100000U);
    FT_Library library = nullptr;
    ASSERT_EQ(FT_Init_FreeType(&library), 0);
    FT_Int major = 0;
    FT_Int minor = 0;
    FT_Int patch = 0;
    FT_Library_Version(library, &major, &minor, &patch);
    EXPECT_EQ(major * 10000 + minor * 100 + patch, 21403);
    FT_Face face = nullptr;
    ASSERT_EQ(FT_New_Memory_Face(library, data.data(), static_cast<FT_Long>(data.size()), 0, &face), 0);
    EXPECT_STREQ(face->family_name, "Noto Sans");
    EXPECT_GT(face->num_glyphs, 1000);
    EXPECT_NE(FT_Get_Char_Index(face, 'A'), 0U);
    ASSERT_TRUE(FT_HAS_MULTIPLE_MASTERS(face));
    FT_MM_Var* variations = nullptr;
    ASSERT_EQ(FT_Get_MM_Var(face, &variations), 0);
    EXPECT_EQ(variations->num_axis, 2U);
    FT_Done_MM_Var(library, variations);
    FT_Done_Face(face);
    FT_Done_FreeType(library);
}

TEST(HarfBuzzTest, ShapesArabicIntoJoiningForms)
{
    // Seen and meem alone are their isolated forms; "seen meem" joins them: an initial seen and a final meem,
    // in visual order (right to left: meem first). Letters without dots: Noto Sans Arabic shapes dotted
    // letters as a dotless body plus a dot glyph.
    EXPECT_STREQ(hb_version_string(), "14.2.1");
    std::vector<unsigned char> data = read_font("NotoSansArabic.ttf");
    hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(data.data()), static_cast<unsigned int>(data.size()),
                                     HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t* face = hb_face_create(blob, 0);
    hb_font_t* font = hb_font_create(face);
    const std::vector<hb_codepoint_t> seen = shape(font, "\xD8\xB3");
    const std::vector<hb_codepoint_t> meem = shape(font, "\xD9\x85");
    const std::vector<hb_codepoint_t> joined = shape(font, "\xD8\xB3\xD9\x85");
    ASSERT_EQ(seen.size(), 1U);
    ASSERT_EQ(meem.size(), 1U);
    ASSERT_EQ(joined.size(), 2U);
    EXPECT_NE(seen[0], 0U);
    EXPECT_NE(meem[0], 0U);
    EXPECT_NE(joined[0], meem[0]);
    EXPECT_NE(joined[1], seen[0]);
    hb_font_destroy(font);
    hb_face_destroy(face);
    hb_blob_destroy(blob);
}

TEST(HarfBuzzTest, SubsetsFontsToTheirCharacters)
{
    // "AB" keeps a handful of glyphs (.notdef, A, B and what they need) of more than a thousand.
    std::vector<unsigned char> data = read_font("NotoSans.ttf");
    hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(data.data()), static_cast<unsigned int>(data.size()),
                                     HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t* face = hb_face_create(blob, 0);
    hb_subset_input_t* input = hb_subset_input_create_or_fail();
    ASSERT_NE(input, nullptr);
    hb_set_add(hb_subset_input_unicode_set(input), 'A');
    hb_set_add(hb_subset_input_unicode_set(input), 'B');
    hb_face_t* subset = hb_subset_or_fail(face, input);
    ASSERT_NE(subset, nullptr);
    EXPECT_GT(hb_face_get_glyph_count(face), 1000U);
    EXPECT_GE(hb_face_get_glyph_count(subset), 3U);
    EXPECT_LE(hb_face_get_glyph_count(subset), 8U);
    hb_face_destroy(subset);
    hb_subset_input_destroy(input);
    hb_face_destroy(face);
    hb_blob_destroy(blob);
}

TEST(SheenBidiTest, FindsTheRunsOfMixedDirectionText)
{
    // "abc " then three Hebrew letters: a left-to-right paragraph, a level-1 run for the Hebrew.
    EXPECT_STREQ(SBVersionGetString(), "3.0.0");
    const char text[] = "abc \xD7\x90\xD7\x91\xD7\x92";
    const SBCodepointSequence sequence{SBStringEncodingUTF8, text, std::strlen(text)};
    SBAlgorithmRef algorithm = SBAlgorithmCreate(&sequence);
    SBParagraphRef paragraph = SBAlgorithmCreateParagraph(algorithm, 0, std::numeric_limits<SBUInteger>::max(), SBLevelDefaultLTR);
    ASSERT_NE(paragraph, nullptr);
    EXPECT_EQ(SBParagraphGetBaseLevel(paragraph), 0);
    SBLineRef line = SBParagraphCreateLine(paragraph, 0, SBParagraphGetLength(paragraph));
    ASSERT_EQ(SBLineGetRunCount(line), 2U);
    const SBRun* runs = SBLineGetRunsPtr(line);
    EXPECT_EQ(runs[0].level, 0);
    EXPECT_EQ(runs[0].length, 4U);
    EXPECT_EQ(runs[1].level, 1);
    EXPECT_EQ(runs[1].length, 6U);
    SBLineRelease(line);
    SBParagraphRelease(paragraph);
    SBAlgorithmRelease(algorithm);
}

TEST(LibunibreakTest, FindsLineBreakOpportunitiesAndGraphemes)
{
    // A line may break after "gfx " but not inside "draws".
    EXPECT_EQ(unibreak_version, 0x0700);
    init_linebreak();
    const char text[] = "gfx draws text";
    std::vector<char> breaks(std::strlen(text));
    set_linebreaks_utf8(reinterpret_cast<const utf8_t*>(text), breaks.size(), "en", breaks.data());
    EXPECT_EQ(breaks[2], LINEBREAK_NOBREAK);
    EXPECT_EQ(breaks[3], LINEBREAK_ALLOWBREAK);
    EXPECT_EQ(breaks[5], LINEBREAK_NOBREAK);

    // "e" and a combining acute accent are one grapheme.
    init_graphemebreak();
    const char accented[] = "e\xCC\x81x";
    std::vector<char> graphemes(std::strlen(accented));
    set_graphemebreaks_utf8(reinterpret_cast<const utf8_t*>(accented), graphemes.size(), "en", graphemes.data());
    EXPECT_EQ(graphemes[0], GRAPHEMEBREAK_NOBREAK);
    EXPECT_EQ(graphemes[2], GRAPHEMEBREAK_BREAK);
}

TEST(ZstdTest, CompressedDataRoundTrips)
{
    EXPECT_EQ(ZSTD_versionNumber(), 10507U);
    std::string text;
    for (int index = 0; index < 1000; ++index)
    {
        text += "MTSDF ";
    }
    std::vector<char> compressed(ZSTD_compressBound(text.size()));
    const std::size_t compressed_size = ZSTD_compress(compressed.data(), compressed.size(), text.data(), text.size(), 19);
    ASSERT_FALSE(ZSTD_isError(compressed_size));
    EXPECT_LT(compressed_size, text.size() / 20);
    std::string restored(text.size(), '\0');
    const std::size_t restored_size = ZSTD_decompress(restored.data(), restored.size(), compressed.data(), compressed_size);
    ASSERT_FALSE(ZSTD_isError(restored_size));
    EXPECT_EQ(restored, text);
}

TEST(MsdfgenTest, GeneratesTheDistanceFieldOfAGlyph)
{
    // The glyph 'I', centered in a 32 x 32 field: the center is inside it, the corners outside.
    std::vector<unsigned char> data = read_font("NotoSans.ttf");
    msdfgen::FreetypeHandle* freetype = msdfgen::initializeFreetype();
    ASSERT_NE(freetype, nullptr);
    msdfgen::FontHandle* font = msdfgen::loadFontData(freetype, data.data(), static_cast<int>(data.size()));
    ASSERT_NE(font, nullptr);
    msdfgen::Shape shape;
    ASSERT_TRUE(msdfgen::loadGlyph(shape, font, msdfgen::unicode_t{'I'}, msdfgen::FONT_SCALING_EM_NORMALIZED));
    shape.normalize();
    msdfgen::edgeColoringSimple(shape, 3.0);
    const msdfgen::Shape::Bounds bounds = shape.getBounds();
    const double scale = 24.0 / std::max(bounds.r - bounds.l, bounds.t - bounds.b);
    const msdfgen::Vector2 translate{16.0 / scale - (bounds.l + bounds.r) / 2.0, 16.0 / scale - (bounds.b + bounds.t) / 2.0};
    msdfgen::Bitmap<float, 3> field(32, 32);
    msdfgen::generateMSDF(field, shape, msdfgen::Projection{msdfgen::Vector2{scale}, translate}, msdfgen::Range{4.0 / scale});
    const auto median = [&field](int x, int y)
    {
        const float* texel = field(x, y);
        return std::max(std::min(texel[0], texel[1]), std::min(std::max(texel[0], texel[1]), texel[2]));
    };
    EXPECT_GT(median(16, 16), 0.5F);
    EXPECT_LT(median(0, 0), 0.5F);
    EXPECT_LT(median(31, 31), 0.5F);
    msdfgen::destroyFont(font);
    msdfgen::deinitializeFreetype(freetype);
}

/// The texels of an MTSDF atlas of printable ASCII, generated with @p threads threads.
auto
generate_ascii_atlas(int threads,
                     int& width,
                     int& height,
                     std::size_t& glyph_count) -> std::vector<msdfgen::byte>
{
    std::vector<unsigned char> data = read_font("NotoSans.ttf");
    msdfgen::FreetypeHandle* freetype = msdfgen::initializeFreetype();
    msdfgen::FontHandle* font = msdfgen::loadFontData(freetype, data.data(), static_cast<int>(data.size()));
    std::vector<msdf_atlas::GlyphGeometry> glyphs;
    msdf_atlas::FontGeometry geometry(&glyphs);
    geometry.loadCharset(font, 1.0, msdf_atlas::Charset::ASCII);
    for (msdf_atlas::GlyphGeometry& glyph : glyphs)
    {
        glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, 3.0, 0);
    }
    msdf_atlas::TightAtlasPacker packer;
    packer.setDimensionsConstraint(msdf_atlas::DimensionsConstraint::SQUARE);
    packer.setMinimumScale(24.0);
    packer.setPixelRange(msdfgen::Range{4.0});
    packer.setMiterLimit(1.0);
    packer.pack(glyphs.data(), static_cast<int>(glyphs.size()));
    packer.getDimensions(width, height);
    msdf_atlas::ImmediateAtlasGenerator<float, 4, msdf_atlas::mtsdfGenerator, msdf_atlas::BitmapAtlasStorage<msdfgen::byte, 4>> generator(width, height);
    generator.setThreadCount(threads);
    generator.generate(glyphs.data(), static_cast<int>(glyphs.size()));
    const msdfgen::BitmapConstRef<msdfgen::byte, 4> texels = generator.atlasStorage();
    std::vector<msdfgen::byte> result(texels.pixels, texels.pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    glyph_count = glyphs.size();
    msdfgen::destroyFont(font);
    msdfgen::deinitializeFreetype(freetype);
    return result;
}

TEST(MsdfAtlasGenTest, PacksAsciiIntoAnMtsdfAtlasDeterministically)
{
    // 95 printable characters in a square atlas, with distance data; one thread or four, the same texels.
    int width = 0;
    int height = 0;
    std::size_t glyphs = 0;
    const std::vector<msdfgen::byte> single = generate_ascii_atlas(1, width, height, glyphs);
    EXPECT_EQ(glyphs, 95U);
    EXPECT_GT(width, 64);
    EXPECT_EQ(width, height);
    EXPECT_TRUE(std::ranges::any_of(single, [](msdfgen::byte texel) { return texel > 128; }));
    int other_width = 0;
    int other_height = 0;
    const std::vector<msdfgen::byte> threaded = generate_ascii_atlas(4, other_width, other_height, glyphs);
    EXPECT_EQ(other_width, width);
    EXPECT_TRUE(threaded == single) << "atlas generation must not depend on the thread count (reproducible atlases)";
}

TEST(StbTrueTypeTest, ReadsOutlinesOfTrueTypeFonts)
{
    // The second decoder finds the same font and the outline of 'A'.
    const std::vector<unsigned char> data = read_font("NotoSans.ttf");
    stbtt_fontinfo font{};
    ASSERT_NE(stbtt_InitFont(&font, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0)), 0);
    const int glyph = stbtt_FindGlyphIndex(&font, 'A');
    EXPECT_GT(glyph, 0);
    stbtt_vertex* vertices = nullptr;
    const int vertex_count = stbtt_GetGlyphShape(&font, glyph, &vertices);
    EXPECT_GT(vertex_count, 3);
    stbtt_FreeShape(&font, vertices);
}

} // namespace
