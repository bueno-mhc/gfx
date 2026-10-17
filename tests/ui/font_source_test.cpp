// Font sources (format detection, files), character selections (named sets, ranges, code points, UTF-8)
// and the outline builder.
#include <gtest/gtest.h>

import std;
import gfx.tests.ui_atlas_support;

namespace
{

using namespace gfx::ui;
using gfx::tests::load_font;

/// The bytes of @p text.
auto
bytes_of(std::string_view text) -> std::vector<std::byte>
{
    std::vector<std::byte> bytes;
    for (const char character : text)
    {
        bytes.push_back(static_cast<std::byte>(character));
    }
    bytes.resize(std::max<std::size_t>(bytes.size(), 16));
    return bytes;
}

TEST(FontSourceTest, FormatsAreDetectedFromTheirFirstBytes)
{
    // Every test font, and a collection made of two of them.
    EXPECT_EQ(load_font("NotoSans.ttf").format(), FontFormat::TRUETYPE);
    EXPECT_EQ(load_font("SourceSans3-Regular.otf").format(), FontFormat::OPENTYPE_CFF);
    EXPECT_EQ(load_font("SourceSans3VF-Upright.otf").format(), FontFormat::OPENTYPE_CFF2);
    EXPECT_EQ(load_font("SourceSans3-Regular.ttf.woff").format(), FontFormat::WOFF);
    EXPECT_EQ(load_font("SourceSans3-Regular.ttf.woff2").format(), FontFormat::WOFF2);
    const std::array fonts{load_font("NotoSans.ttf"), load_font("NotoSansHebrew.ttf")};
    EXPECT_EQ(gfx::tests::make_collection(fonts).format(), FontFormat::COLLECTION);

    // Anything else is no font format.
    EXPECT_EQ(detect_font_format(bytes_of("typ1 a Type 1 font")), std::nullopt);
    EXPECT_EQ(detect_font_format(bytes_of("\x89PNG")), std::nullopt);
    EXPECT_EQ(detect_font_format({}), std::nullopt);
    static_assert(detect_font_format(std::array{std::byte{'w'}, std::byte{'O'}, std::byte{'F'}, std::byte{'2'}, std::byte{}, std::byte{},
                                                std::byte{}, std::byte{}, std::byte{}, std::byte{}, std::byte{}, std::byte{}})
                  == FontFormat::WOFF2);
}

TEST(FontSourceTest, SourcesKnowTheirNameAndHash)
{
    const FontSource font = load_font("NotoSans.ttf");
    EXPECT_EQ(font.name(), "NotoSans.ttf");
    EXPECT_EQ(font.hash(), xxhash64(font.bytes()));
    const FontSource copy = FontSource::from_memory(font.bytes(), "copy");
    EXPECT_EQ(copy.name(), "copy");
    EXPECT_EQ(copy.hash(), font.hash());
}

TEST(FontSourceTest, MissingFilesAreReported)
{
    try
    {
        static_cast<void>(FontSource::from_file(gfx::tests::font_path("missing.ttf")));
        FAIL() << "a missing file must throw";
    }
    catch (const gfx::InvalidUsageError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("missing.ttf"), std::string_view::npos) << error.what();
    }
}

TEST(CharacterSelectionTest, SpecificationsNameSetsCodePointsAndRanges)
{
    const CharacterSelection selection = CharacterSelection::parse("BASIC_LATIN, U+00B1 U+2190-U+2192\tu+20ac");
    EXPECT_EQ(selection.size(), 95U + 1U + 3U + 1U);
    EXPECT_TRUE(selection.contains(U' '));
    EXPECT_TRUE(selection.contains(U'~'));
    EXPECT_FALSE(selection.contains(U'\u007F'));
    EXPECT_TRUE(selection.contains(U'±'));
    EXPECT_TRUE(selection.contains(U'↑'));
    EXPECT_TRUE(selection.contains(U'€'));
    EXPECT_TRUE(std::ranges::is_sorted(selection.code_points()));

    // Every named set is its Unicode block.
    for (const auto& entry : gfx::EnumerationTraits<CharacterSet>::NAMES)
    {
        const CharacterRange range = CHARACTER_SET_RANGES[std::to_underlying(entry.value)];
        const CharacterSelection set = CharacterSelection::parse(entry.name);
        EXPECT_EQ(set.code_points().front(), range.first) << entry.name;
        EXPECT_EQ(set.code_points().back(), range.last) << entry.name;
    }
}

TEST(CharacterSelectionTest, MalformedSpecificationsAreRefused)
{
    for (const std::string_view specification : {"LATIN", "U+", "U+XYZ", "U+2192-U+2190", "U+110000", "U+D800"})
    {
        EXPECT_THROW(static_cast<void>(CharacterSelection::parse(specification)), gfx::InvalidUsageError) << specification;
    }
    try
    {
        static_cast<void>(CharacterSelection::parse("BASIC_LATIN LATN"));
        FAIL() << "an unknown set must throw";
    }
    catch (const gfx::InvalidUsageError& error)
    {
        // The message names the token and lists the sets.
        const std::string_view message = error.what();
        EXPECT_NE(message.find("'LATN'"), std::string_view::npos) << message;
        EXPECT_NE(message.find("GEOMETRIC_SHAPES"), std::string_view::npos) << message;
    }
}

TEST(CharacterSelectionTest, TextAddsItsCharactersButControls)
{
    // One to four bytes per character; line breaks and tabs have no glyph.
    CharacterSelection selection;
    selection.add_text("Aé€\U0001F600\n\tA");
    EXPECT_EQ(std::vector(selection.code_points().begin(), selection.code_points().end()),
              (std::vector<char32_t>{U'A', U'é', U'€', U'\U0001F600'}));

    // Invalid UTF-8: a stray continuation byte, an overlong form, a surrogate, a truncated sequence.
    for (const std::string_view text : {"\x80", "\xC0\xAF", "\xED\xA0\x80", "\xE2\x82"})
    {
        EXPECT_THROW(selection.add_text(text), gfx::InvalidUsageError);
    }
}

TEST(OutlineBuilderTest, ContoursAreClosedAndEmptySegmentsDropped)
{
    // An open triangle with a repeated point, then a contour of a single point.
    OutlineBuilder builder;
    builder.move_to({0, 0});
    builder.line_to({10, 0});
    builder.line_to({10, 0});
    builder.quadratic_to({10, 10}, {0, 10});
    builder.move_to({5, 5});
    builder.move_to({20, 20});
    builder.cubic_to({30, 20}, {30, 30}, {20, 30});
    const GlyphOutline outline = builder.finish();
    ASSERT_EQ(outline.contours.size(), 2U);
    ASSERT_EQ(outline.contours[0].segments.size(), 3U);
    EXPECT_EQ(outline.contours[0].segments[1].kind, OutlineSegmentKind::QUADRATIC);
    EXPECT_EQ(outline.contours[0].segments[2].end(), (OutlinePoint{0, 0}));
    EXPECT_EQ(outline.contours[1].segments.size(), 2U);
    EXPECT_EQ(outline.segment_count(), 5U);
    const OutlineBounds bounds = outline.bounds();
    EXPECT_EQ(bounds.left, 0.0);
    EXPECT_EQ(bounds.right, 30.0);
    EXPECT_EQ(bounds.top, 30.0);
    EXPECT_TRUE(builder.finish().empty());
}

} // namespace
