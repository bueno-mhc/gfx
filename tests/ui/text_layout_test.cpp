// Text layout: glyphs on baselines, kerning and ligatures, the basic shaper against HarfBuzz, Arabic
// joining, bidi reordering, fallback chains, line breaking, paragraphs, alignment, problems, the cache
// and caret positions.
#include <gtest/gtest.h>

import std;
import gfx.tests.text_support;

namespace
{

using namespace gfx::ui;
using gfx::tests::glyph_indices;

class TextLayoutTest : public ::testing::Test
{
protected:
    gfx::tests::TextFonts fonts{};
    TextLayoutEngine engine{fonts.library};

    /// The advance of @p code_point in the Latin atlas at 16 pixels per em.
    [[nodiscard]] auto advance_of(char32_t code_point) const -> float
    {
        const std::shared_ptr<const FontAtlas> atlas = fonts.library.atlas(fonts.latin_atlas);
        return atlas->view().find_glyph(*atlas->find_character(code_point))->advance * 16.0F;
    }

    /// The glyph of @p layout drawing the character at byte @p cluster.
    [[nodiscard]] static auto glyph_at(const TextLayout& layout,
                                       std::uint32_t cluster) -> const LaidOutGlyph&
    {
        return *std::ranges::find(layout.glyphs, cluster, &LaidOutGlyph::cluster);
    }
};

TEST_F(TextLayoutTest, LatinTextSitsOnOneBaseline)
{
    const TextLayout layout = engine.layout("Hello", fonts.latin);
    ASSERT_EQ(layout.lines.size(), 1U);
    ASSERT_EQ(layout.glyphs.size(), 5U);
    EXPECT_EQ(layout.problems, TextLayoutProblem::NONE);
    const LaidOutLine& line = layout.lines[0];
    EXPECT_EQ(line.text_begin, 0U);
    EXPECT_EQ(line.text_end, 5U);
    EXPECT_FALSE(line.right_to_left);
    EXPECT_GT(line.height, 16.0F);
    EXPECT_GT(line.baseline, 10.0F);
    float pen = 0;
    for (std::uint32_t index = 0; index < 5; ++index)
    {
        const LaidOutGlyph& glyph = layout.glyphs[index];
        EXPECT_EQ(glyph.cluster, index);
        EXPECT_EQ(glyph.atlas, fonts.latin_atlas);
        EXPECT_EQ(glyph.y, line.baseline);
        EXPECT_NEAR(glyph.x, pen, 1e-4F);
        EXPECT_FALSE(glyph.right_to_left);
        pen += glyph.advance;
    }
    EXPECT_NEAR(layout.width, pen, 1e-4F);
    EXPECT_EQ(layout.height, line.height);
}

TEST_F(TextLayoutTest, KerningAndLigaturesApply)
{
    // A then V comes closer than their advances.
    const TextLayout kerned = engine.layout("AV", fonts.latin);
    ASSERT_EQ(kerned.glyphs.size(), 2U);
    EXPECT_LT(kerned.glyphs[0].advance, advance_of(U'A'));

    // f then i is one ligature glyph, with the basic shaper as with HarfBuzz.
    const TextLayout basic = engine.layout("fi", fonts.latin);
    const TextLayout harfbuzz = engine.layout("fi", fonts.with_shaping(fonts.latin_atlas, TextShaping::HARFBUZZ));
    ASSERT_EQ(basic.glyphs.size(), 1U);
    ASSERT_EQ(harfbuzz.glyphs.size(), 1U);
    EXPECT_EQ(basic.glyphs[0].glyph, harfbuzz.glyphs[0].glyph);
    EXPECT_NE(basic.glyphs[0].glyph, *fonts.library.atlas(fonts.latin_atlas)->find_character(U'f'));
}

TEST_F(TextLayoutTest, BasicShaperMatchesHarfBuzzOnSimpleText)
{
    const std::string_view text = "The quick brown fox jumps over the lazy dog. AV To Wa fi fl ffi 0123 (\"x\")";
    const TextLayout basic = engine.layout(text, fonts.with_shaping(fonts.latin_atlas, TextShaping::BASIC));
    const TextLayout harfbuzz = engine.layout(text, fonts.with_shaping(fonts.latin_atlas, TextShaping::HARFBUZZ));
    EXPECT_EQ(glyph_indices(basic), glyph_indices(harfbuzz));
    ASSERT_EQ(basic.glyphs.size(), harfbuzz.glyphs.size());
    for (std::size_t index = 0; index < basic.glyphs.size(); ++index)
    {
        EXPECT_NEAR(basic.glyphs[index].x, harfbuzz.glyphs[index].x, 0.01F) << index;
        EXPECT_EQ(basic.glyphs[index].cluster, harfbuzz.glyphs[index].cluster) << index;
    }
}

TEST_F(TextLayoutTest, ArabicJoinsRightToLeft)
{
    // Seen, lam, alef, meem: joined forms, the first letter on the right.
    const std::string_view salam = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
    const TextLayout layout = engine.layout(salam, fonts.arabic);
    ASSERT_EQ(layout.lines.size(), 1U);
    EXPECT_TRUE(layout.lines[0].right_to_left);
    EXPECT_EQ(layout.missing_glyph_count, 0U);
    ASSERT_GE(layout.glyphs.size(), 3U);
    for (const LaidOutGlyph& glyph : layout.glyphs)
    {
        EXPECT_EQ(glyph.atlas, fonts.arabic_atlas);
        EXPECT_TRUE(glyph.right_to_left);
        EXPECT_LE(glyph.x, glyph_at(layout, 0).x);
    }
    const TextLayout isolated = engine.layout("\xD8\xB3", fonts.arabic);
    ASSERT_EQ(isolated.glyphs.size(), 1U);
    EXPECT_NE(glyph_at(layout, 0).glyph, isolated.glyphs[0].glyph);
}

TEST_F(TextLayoutTest, HebrewInLatinTextIsReordered)
{
    // "abc אבג def": the Hebrew word reads right to left between the Latin ones.
    const std::string_view text = "abc \xD7\x90\xD7\x91\xD7\x92 def";
    const TextLayout layout = engine.layout(text, fonts.latin);
    ASSERT_EQ(layout.lines.size(), 1U);
    EXPECT_FALSE(layout.lines[0].right_to_left);
    const float a = glyph_at(layout, 0).x;
    const float c = glyph_at(layout, 2).x;
    const float alef = glyph_at(layout, 4).x;
    const float bet = glyph_at(layout, 6).x;
    const float gimel = glyph_at(layout, 8).x;
    const float d = glyph_at(layout, 11).x;
    EXPECT_LT(a, c);
    EXPECT_LT(c, gimel);
    EXPECT_LT(gimel, bet);
    EXPECT_LT(bet, alef);
    EXPECT_LT(alef, d);
    EXPECT_EQ(glyph_at(layout, 4).atlas, fonts.hebrew_atlas);
    EXPECT_TRUE(glyph_at(layout, 4).right_to_left);
    EXPECT_EQ(glyph_at(layout, 0).atlas, fonts.latin_atlas);
}

TEST_F(TextLayoutTest, RightToLeftParagraphsStartOnTheRight)
{
    // "אב abc": a right-to-left paragraph (its first strong letter), its Latin word still left to right.
    const std::string_view text = "\xD7\x90\xD7\x91 abc";
    const TextLayout layout = engine.layout(text, fonts.hebrew, {.wrap_width = 300});
    ASSERT_EQ(layout.lines.size(), 1U);
    const LaidOutLine& line = layout.lines[0];
    EXPECT_TRUE(line.right_to_left);
    EXPECT_NEAR(line.left + line.width, 300.0F, 1e-3F);
    EXPECT_LT(glyph_at(layout, 5).x, glyph_at(layout, 6).x);
    EXPECT_GT(glyph_at(layout, 0).x, glyph_at(layout, 7).x);

    // Forced directions.
    EXPECT_FALSE(engine.layout(text, fonts.hebrew, {.direction = ParagraphDirection::LEFT_TO_RIGHT}).lines[0].right_to_left);
    EXPECT_TRUE(engine.layout("abc", fonts.latin, {.direction = ParagraphDirection::RIGHT_TO_LEFT}).lines[0].right_to_left);
}

TEST_F(TextLayoutTest, FallbacksFillWhatTheAtlasLacks)
{
    // Hebrew from the Hebrew atlas; a space between Hebrew words stays in it.
    const TextLayout mixed = engine.layout("a\xD7\x90 \xD7\x91", fonts.latin);
    EXPECT_EQ(glyph_at(mixed, 0).atlas, fonts.latin_atlas);
    EXPECT_EQ(glyph_at(mixed, 1).atlas, fonts.hebrew_atlas);
    EXPECT_EQ(glyph_at(mixed, 3).atlas, fonts.hebrew_atlas);
    EXPECT_EQ(mixed.missing_glyph_count, 0U);

    // What no atlas has is the first atlas's `.notdef`, counted.
    const TextLayout missing = engine.layout("a\xE4\xB8\xAD", fonts.latin);
    EXPECT_EQ(missing.missing_glyph_count, 1U);
    EXPECT_TRUE(has_all(missing.problems, TextLayoutProblem::MISSING_GLYPHS));
    EXPECT_TRUE(glyph_at(missing, 1).glyph.is_notdef());
    EXPECT_EQ(glyph_at(missing, 1).atlas, fonts.latin_atlas);
}

TEST_F(TextLayoutTest, LinesBreakAtSpacesWithinTheWidth)
{
    const std::string_view text = "The quick brown fox jumps over the lazy dog and keeps on running far away";
    const TextLayout layout = engine.layout(text, fonts.latin, {.wrap_width = 120});
    ASSERT_GT(layout.lines.size(), 2U);
    for (std::size_t index = 0; index < layout.lines.size(); ++index)
    {
        const LaidOutLine& line = layout.lines[index];
        EXPECT_LE(line.width, 120.0F) << index;
        EXPECT_NEAR(line.top, static_cast<float>(index) * line.height, 1e-3F);
        if (index + 1 < layout.lines.size())
        {
            EXPECT_EQ(text[line.text_end - 1], ' ') << index;
            EXPECT_EQ(layout.lines[index + 1].text_begin, line.text_end);
        }
        for (std::uint32_t glyph = line.first_glyph; glyph < line.first_glyph + line.glyph_count; ++glyph)
        {
            EXPECT_EQ(layout.glyphs[glyph].y, line.baseline);
        }
    }
    EXPECT_EQ(layout.lines.back().text_end, text.size());
    EXPECT_LE(layout.width, 120.0F);
}

TEST_F(TextLayoutTest, LongWordsBreakBetweenGraphemes)
{
    const std::string_view word = "Supercalifragilisticexpialidocious";
    const TextLayout layout = engine.layout(word, fonts.latin, {.wrap_width = 60});
    ASSERT_GT(layout.lines.size(), 2U);
    std::uint32_t expected_begin = 0;
    for (const LaidOutLine& line : layout.lines)
    {
        EXPECT_EQ(line.text_begin, expected_begin);
        EXPECT_GT(line.glyph_count, 0U);
        EXPECT_LE(line.width, 60.0F);
        expected_begin = line.text_end;
    }
    EXPECT_EQ(expected_begin, word.size());
}

TEST_F(TextLayoutTest, LineSeparatorsStartParagraphs)
{
    const TextLayout layout = engine.layout("one\ntwo\r\nthree\n", fonts.latin);
    ASSERT_EQ(layout.lines.size(), 4U);
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 4> ranges{{{0, 3}, {4, 7}, {9, 14}, {15, 15}}};
    for (std::size_t index = 0; index < ranges.size(); ++index)
    {
        EXPECT_EQ(layout.lines[index].text_begin, ranges[index].first) << index;
        EXPECT_EQ(layout.lines[index].text_end, ranges[index].second) << index;
    }
    EXPECT_EQ(layout.lines[3].glyph_count, 0U);
    EXPECT_EQ(layout.glyphs.size(), 11U);
    EXPECT_NEAR(layout.height, 4 * layout.lines[0].height, 1e-3F);

    // An empty text is one empty line.
    const TextLayout empty = engine.layout("", fonts.latin);
    ASSERT_EQ(empty.lines.size(), 1U);
    EXPECT_TRUE(empty.glyphs.empty());
    EXPECT_GT(empty.height, 0.0F);
}

TEST_F(TextLayoutTest, AlignmentPlacesEveryLine)
{
    const std::string_view text = "short\nmuch longer line";
    const TextLayout left = engine.layout(text, fonts.latin, {.alignment = TextAlignment::LEFT});
    const float narrow = left.lines[0].width;
    const float wide = left.lines[1].width;
    EXPECT_EQ(left.width, wide);
    EXPECT_EQ(left.lines[0].left, 0.0F);
    EXPECT_NEAR(engine.layout(text, fonts.latin, {.alignment = TextAlignment::CENTER}).lines[0].left, (wide - narrow) / 2, 1e-3F);
    EXPECT_NEAR(engine.layout(text, fonts.latin, {.alignment = TextAlignment::RIGHT}).lines[0].left, wide - narrow, 1e-3F);
    EXPECT_NEAR(engine.layout(text, fonts.latin, {.alignment = TextAlignment::END}).lines[0].left, wide - narrow, 1e-3F);
    const TextLayout boxed = engine.layout(text, fonts.latin, {.wrap_width = 400, .alignment = TextAlignment::RIGHT});
    EXPECT_NEAR(boxed.lines[1].left, 400 - wide, 1e-3F);
    EXPECT_NEAR(boxed.glyphs[boxed.lines[1].first_glyph].x, 400 - wide, 1e-3F);
}

TEST_F(TextLayoutTest, ProblemsAreReported)
{
    const TextLayout unknown = engine.layout("text", FontHandle{});
    EXPECT_EQ(unknown.problems, TextLayoutProblem::UNKNOWN_FONT);
    EXPECT_TRUE(unknown.lines.empty());

    const TextLayout invalid = engine.layout("a\xFF" "b", fonts.latin);
    EXPECT_TRUE(has_all(invalid.problems, TextLayoutProblem::INVALID_UTF8));
    ASSERT_EQ(invalid.glyphs.size(), 3U);
    EXPECT_EQ(invalid.glyphs[1].cluster, 1U);
    EXPECT_EQ(invalid.glyphs[2].cluster, 2U);
    EXPECT_FALSE(invalid.glyphs[1].glyph.is_notdef());

    const FontHandle harfbuzz_without_font = fonts.with_shaping(fonts.latin_basic_atlas, TextShaping::HARFBUZZ);
    EXPECT_TRUE(has_all(engine.layout("abc", harfbuzz_without_font).problems, TextLayoutProblem::NO_SHAPING_FONT));
    EXPECT_EQ(engine.layout("abc", fonts.basic).problems, TextLayoutProblem::NONE);
}

TEST(TextLayoutCacheTest, CachedLayoutsAreReused)
{
    gfx::tests::TextFonts fonts;
    TextLayoutEngine engine{fonts.library, {.cache_capacity = 2}};

    // The same text, font and description: the same layout.
    const TextLayout first = engine.layout("one", fonts.latin);
    const TextLayout again = engine.layout("one", fonts.latin);
    EXPECT_EQ(first.glyphs.data(), again.glyphs.data());
    EXPECT_EQ(engine.statistics().hits, 1U);
    EXPECT_EQ(engine.statistics().misses, 1U);
    static_cast<void>(engine.layout("one", fonts.latin, {.wrap_width = 50}));
    EXPECT_EQ(engine.statistics().misses, 2U);

    // Every entry in use this frame: the next layout lives until the next frame.
    const TextLayout full = engine.layout("two", fonts.latin);
    EXPECT_TRUE(has_all(full.problems, TextLayoutProblem::CACHE_FULL));
    EXPECT_EQ(full.glyphs.size(), 3U);
    EXPECT_EQ(engine.statistics().overflows, 1U);

    // Next frame: the least recently used entry is replaced.
    engine.new_frame();
    static_cast<void>(engine.layout("two", fonts.latin));
    EXPECT_EQ(engine.statistics().evictions, 1U);
    EXPECT_EQ(engine.statistics().cached, 2U);
    static_cast<void>(engine.layout("one", fonts.latin, {.wrap_width = 50}));
    EXPECT_EQ(engine.statistics().hits, 2U);

    engine.clear();
    EXPECT_EQ(engine.statistics().cached, 0U);
    static_cast<void>(engine.layout("one", fonts.latin));
    EXPECT_EQ(engine.statistics().hits, 2U);
}

TEST(TextLayoutCacheTest, LayoutsUsedInOneFrameOnlyAreReplacedFirst)
{
    gfx::tests::TextFonts fonts;
    TextLayoutEngine engine{fonts.library, {.cache_capacity = 8}};

    // "kept" every frame, "counter N" in frame N only: from the third frame, each counter replaces the one of two
    // frames before instead of taking a free entry.
    std::string counter = "counter 0";
    for (char digit = '0'; digit <= '9'; ++digit)
    {
        engine.new_frame();
        counter.back() = digit;
        static_cast<void>(engine.layout("kept", fonts.latin));
        static_cast<void>(engine.layout(counter, fonts.latin));
    }
    EXPECT_EQ(engine.statistics().cached, 3U) << "kept and the last two counters";
    EXPECT_EQ(engine.statistics().evictions, 8U);
    EXPECT_EQ(engine.statistics().hits, 9U);

    // A text used in two consecutive frames stays while the counters go on; the last counters are still there.
    engine.new_frame();
    static_cast<void>(engine.layout("shown twice", fonts.latin));
    engine.new_frame();
    static_cast<void>(engine.layout("shown twice", fonts.latin));
    for (char digit = 'a'; digit <= 'j'; ++digit)
    {
        engine.new_frame();
        counter.back() = digit;
        static_cast<void>(engine.layout(counter, fonts.latin));
    }
    const std::uint64_t hits = engine.statistics().hits;
    static_cast<void>(engine.layout("shown twice", fonts.latin));
    static_cast<void>(engine.layout("kept", fonts.latin));
    static_cast<void>(engine.layout("counter j", fonts.latin));
    EXPECT_EQ(engine.statistics().hits, hits + 3);

    // A counter of an earlier frame is laid out again.
    const std::uint64_t misses = engine.statistics().misses;
    static_cast<void>(engine.layout("counter 5", fonts.latin));
    EXPECT_EQ(engine.statistics().misses, misses + 1);
    EXPECT_LE(engine.statistics().cached, 6U);
}

TEST(TextLayoutCacheTest, TheLeastRecentlyUsedLayoutIsReplacedWhenTheCacheIsFull)
{
    gfx::tests::TextFonts fonts;
    TextLayoutEngine engine{fonts.library, {.cache_capacity = 2}};

    // "a" and "b" used in two frames, then "b" before "a": "b" is the least recently used.
    for (int frame = 0; frame < 2; ++frame)
    {
        engine.new_frame();
        static_cast<void>(engine.layout("a", fonts.latin));
        static_cast<void>(engine.layout("b", fonts.latin));
    }
    engine.new_frame();
    static_cast<void>(engine.layout("b", fonts.latin));
    static_cast<void>(engine.layout("a", fonts.latin));

    // A new text replaces "b".
    engine.new_frame();
    static_cast<void>(engine.layout("c", fonts.latin));
    EXPECT_EQ(engine.statistics().evictions, 1U);
    const std::uint64_t hits = engine.statistics().hits;
    static_cast<void>(engine.layout("a", fonts.latin));
    EXPECT_EQ(engine.statistics().hits, hits + 1);
    EXPECT_TRUE(has_all(engine.layout("b", fonts.latin).problems, TextLayoutProblem::CACHE_FULL)) << "a and c are used this frame";
}

TEST_F(TextLayoutTest, CaretsAndHitTestsFollowClusters)
{
    const TextLayout layout = engine.layout("Hello", fonts.latin);
    EXPECT_NEAR(caret_position(layout, 0).x, layout.glyphs[0].x, 1e-4F);
    EXPECT_NEAR(caret_position(layout, 2).x, layout.glyphs[2].x, 1e-4F);
    EXPECT_NEAR(caret_position(layout, 5).x, layout.width, 1e-4F);
    EXPECT_EQ(caret_position(layout, 3).height, layout.lines[0].height);
    EXPECT_EQ(hit_test(layout, layout.glyphs[1].x + 0.9F * layout.glyphs[1].advance, 5), 2U);
    EXPECT_EQ(hit_test(layout, layout.glyphs[1].x + 0.1F * layout.glyphs[1].advance, 5), 1U);
    EXPECT_EQ(hit_test(layout, -10, 5), 0U);
    EXPECT_EQ(hit_test(layout, 1000, 5), 5U);

    // A ligature shares its width between its characters.
    const TextLayout ligature = engine.layout("fi", fonts.latin);
    ASSERT_EQ(ligature.glyphs.size(), 1U);
    EXPECT_NEAR(caret_position(ligature, 1).x, ligature.glyphs[0].advance / 2, 1e-4F);

    // Right to left: the first caret on the right, the last on the left.
    const TextLayout hebrew = engine.layout("\xD7\x90\xD7\x91", fonts.hebrew);
    const LaidOutGlyph& alef = glyph_at(hebrew, 0);
    EXPECT_NEAR(caret_position(hebrew, 0).x, alef.x + alef.advance, 1e-4F);
    EXPECT_NEAR(caret_position(hebrew, 4).x, hebrew.lines[0].left, 1e-4F);
    EXPECT_EQ(hit_test(hebrew, alef.x + alef.advance - 0.5F, 5), 0U);

    // Lines: a caret of the second line, a hit test on it.
    const TextLayout lines = engine.layout("ab\ncd", fonts.latin);
    EXPECT_EQ(caret_position(lines, 4).line, 1U);
    EXPECT_GE(hit_test(lines, 1000, lines.lines[1].top + 1), 4U);
}

/// The caret before byte @p offset of @p layout, found the straightforward way (for each glyph, its cluster's end
/// and extent over the whole line): the reference of caret_position().
auto
reference_caret_position(const TextLayout& layout,
                         std::uint32_t offset) -> CaretPosition
{
    if (layout.lines.empty())
    {
        return {};
    }

    // The last line starting at or before the offset.
    std::size_t line_index = 0;
    for (std::size_t index = 1; index < layout.lines.size(); ++index)
    {
        line_index = layout.lines[index].text_begin <= offset ? index : line_index;
    }
    const LaidOutLine& line = layout.lines[line_index];
    const std::span<const LaidOutGlyph> glyphs = layout.glyphs.subspan(line.first_glyph, line.glyph_count);
    const auto end_of = [&](std::uint32_t cluster)
    {
        std::uint32_t end = line.text_end;
        for (const LaidOutGlyph& other : glyphs)
        {
            end = other.cluster > cluster && other.cluster < end ? other.cluster : end;
        }
        return end;
    };

    // The first glyph in visual order whose cluster holds the offset, the cluster's share of its glyphs' width.
    CaretPosition caret{.line = static_cast<std::uint32_t>(line_index), .top = line.top, .height = line.height};
    for (const LaidOutGlyph& glyph : glyphs)
    {
        const std::uint32_t end = end_of(glyph.cluster);
        if (offset >= glyph.cluster && offset < end)
        {
            float left = std::numeric_limits<float>::max();
            float right = std::numeric_limits<float>::lowest();
            for (const LaidOutGlyph& other : glyphs)
            {
                if (other.cluster == glyph.cluster)
                {
                    left = std::min(left, other.x);
                    right = std::max(right, other.x + other.advance);
                }
            }
            const float fraction = static_cast<float>(offset - glyph.cluster) / static_cast<float>(end - glyph.cluster);
            caret.x = glyph.right_to_left ? right - fraction * (right - left) : left + fraction * (right - left);
            return caret;
        }
    }
    caret.x = line.right_to_left ? line.left : line.left + line.width;
    return caret;
}

/// The offset nearest (@p x, @p y) in @p layout, found the straightforward way (both edges of every glyph's whole
/// cluster): the reference of hit_test().
auto
reference_hit_test(const TextLayout& layout,
                   float x,
                   float y) -> std::uint32_t
{
    if (layout.lines.empty())
    {
        return 0;
    }

    // The line under y.
    std::size_t line_index = 0;
    while (line_index + 1 < layout.lines.size() && y >= layout.lines[line_index + 1].top)
    {
        ++line_index;
    }
    const LaidOutLine& line = layout.lines[line_index];
    const std::span<const LaidOutGlyph> glyphs = layout.glyphs.subspan(line.first_glyph, line.glyph_count);

    // For each glyph, its cluster's edges; the nearest one's offset.
    std::uint32_t nearest = line.right_to_left ? line.text_end : line.text_begin;
    float nearest_distance = std::numeric_limits<float>::max();
    for (const LaidOutGlyph& glyph : glyphs)
    {
        float left = std::numeric_limits<float>::max();
        float right = std::numeric_limits<float>::lowest();
        std::uint32_t end = line.text_end;
        for (const LaidOutGlyph& other : glyphs)
        {
            if (other.cluster == glyph.cluster)
            {
                left = std::min(left, other.x);
                right = std::max(right, other.x + other.advance);
            }
            end = other.cluster > glyph.cluster && other.cluster < end ? other.cluster : end;
        }
        const std::uint32_t at_left = glyph.right_to_left ? end : glyph.cluster;
        const std::uint32_t at_right = glyph.right_to_left ? glyph.cluster : end;
        for (const auto& [edge, offset] : {std::pair{left, at_left}, std::pair{right, at_right}})
        {
            if (std::abs(edge - x) < nearest_distance)
            {
                nearest_distance = std::abs(edge - x);
                nearest = offset;
            }
        }
    }
    return nearest;
}

TEST_F(TextLayoutTest, CaretsAndHitTestsMatchTheStraightforwardSearch)
{
    // Carets at every offset and hit tests across every line, in one pass over a line's glyphs (or two), as found by
    // comparing every glyph with every other: Latin with ligatures and combining marks, pointed Hebrew, vowelled
    // Arabic, bidi mixes, line breaks, empty lines, wrapping, right-to-left paragraphs, the basic shaper.
    const std::array<std::string_view, 9> texts{
        "",
        "Hello world, fine office affinity, waffle",
        "\xD7\xA9\xD6\xB8\xD7\x81\xD7\x9C\xD7\x95\xD6\xB9\xD7\x9D \xD7\xA2\xD7\x95\xD6\xB9\xD7\x9C\xD6\xB8\xD7\x9D",
        "\xD9\x85\xD9\x8E\xD8\xB1\xD9\x92\xD8\xAD\xD9\x8E\xD8\xA8\xD9\x8B\xD8\xA7 \xD8\xA8\xD8\xA7\xD9\x84\xD8\xB9\xD8\xA7\xD9\x84\xD9\x85",
        "abc \xD7\x90\xD7\x91\xD7\x92 def 123 \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7! end",
        "first line\nsecond \xD7\x90\xD7\x91 line\n\nfourth after an empty one",
        "The quick brown fox jumps over the lazy dog and keeps running far away",
        "e\xCC\x81t\xC3\xA9 ffi \xEF\xAC\x81 tab\tend  ",
        "123 \xD7\x90\xD7\x91\xD7\x92 456 \xD7\x93\xD7\x94 (\xD7\x95)",
    };
    const std::array<FontHandle, 4> handles{fonts.latin, fonts.basic, fonts.hebrew, fonts.arabic};
    const std::array<TextLayoutDescription, 4> descriptions{TextLayoutDescription{}, TextLayoutDescription{.wrap_width = 60},
                                                            TextLayoutDescription{.direction = ParagraphDirection::RIGHT_TO_LEFT},
                                                            TextLayoutDescription{.wrap_width = 90, .alignment = TextAlignment::CENTER}};
    for (const std::string_view text : texts)
    {
        for (const FontHandle font : handles)
        {
            for (const TextLayoutDescription& description : descriptions)
            {
                engine.new_frame();
                const TextLayout layout = engine.layout(text, font, description);
                for (std::uint32_t offset = 0; offset <= text.size() + 2; ++offset)
                {
                    const CaretPosition expected = reference_caret_position(layout, offset);
                    const CaretPosition caret = caret_position(layout, offset);
                    ASSERT_EQ(caret.line, expected.line) << text << " at " << offset;
                    ASSERT_EQ(caret.x, expected.x) << text << " at " << offset;
                    ASSERT_EQ(caret.top, expected.top) << text << " at " << offset;
                    ASSERT_EQ(caret.height, expected.height) << text << " at " << offset;
                }
                for (const LaidOutLine& line : layout.lines)
                {
                    for (float x = -10; x <= layout.width + 10; x += 0.25F)
                    {
                        ASSERT_EQ(hit_test(layout, x, line.top + 1), reference_hit_test(layout, x, line.top + 1)) << text << " at x " << x;
                    }
                }
            }
        }
    }
}

} // namespace
