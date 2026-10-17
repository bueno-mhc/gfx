// Atlases built at build time by gfx_add_ui_atlas() and embedded in generated modules (one with #embed,
// one as a byte list): read at compile time by AtlasView, and equal to the files the tool wrote.
#include <gtest/gtest.h>

import std;
import gfx.ui;
import gfx.tests.latin_atlas;
import gfx.tests.digits_atlas;

namespace
{

using namespace gfx::ui;
using gfx::tests::atlases::digits;
using gfx::tests::atlases::latin;

/// The value @p view was built at for axis @p tag (-1 when the font has no such axis).
constexpr auto
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
    return -1.0F;
}

// Checked when this file compiles: structure, coverage, build parameters (and every checksum of the small one).
constexpr std::expected<AtlasView, AtlasReadError> LATIN = AtlasView::parse(latin, AtlasVerification::STRUCTURE);
static_assert(LATIN.has_value(), "the embedded latin atlas is valid");
static_assert(LATIN->descriptor().kind == SdfKind::MTSDF && LATIN->descriptor().em_size == 32.0F);
static_assert(LATIN->find_character(U'A').has_value() && LATIN->find_character(U'~').has_value());
static_assert(!LATIN->find_character(U'é').has_value(), "only BASIC_LATIN was requested");
static_assert(LATIN->face_metrics().family == "Noto Sans");
static_assert(axis_value(*LATIN, "wght") == 500.0F && axis_value(*LATIN, "wdth") == 100.0F, "built at VARIATION wght=500");
static_assert(LATIN->find_kerning(*LATIN->find_character(U'A'), *LATIN->find_character(U'V')) < 0.0F);

constexpr std::expected<AtlasView, AtlasReadError> DIGITS = AtlasView::parse(digits, AtlasVerification::CHECKSUMS);
static_assert(DIGITS.has_value(), "the embedded digits atlas is valid, checksums included");
static_assert(DIGITS->descriptor().kind == SdfKind::SDF && DIGITS->descriptor().generator == SdfGeneratorKind::BUILT_IN);
static_assert(DIGITS->character_count() == 10 && !DIGITS->shaping_font().has_value());

/// The bytes of the file @p path.
auto
read_file(const char* path) -> std::vector<std::byte>
{
    std::ifstream file{path, std::ios::binary};
    std::vector<char> bytes{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    std::vector<std::byte> result(bytes.size());
    std::ranges::transform(bytes, result.begin(), [](char byte) { return static_cast<std::byte>(byte); });
    return result;
}

TEST(AtlasEmbedTest, EmbeddedAtlasesAreTheFilesTheToolWrote)
{
    EXPECT_TRUE(std::ranges::equal(latin, read_file(GFX_TEST_LATIN_ATLAS)));
    EXPECT_TRUE(std::ranges::equal(digits, read_file(GFX_TEST_DIGITS_ATLAS)));
}

TEST(AtlasEmbedTest, EmbeddedAtlasesPassEveryCheckAtRunTime)
{
    // The checksums of the larger atlas, which the compile-time check skipped.
    const auto view = AtlasView::parse(latin);
    ASSERT_TRUE(view.has_value()) << describe_atlas_error(view.error());
    EXPECT_EQ(view->character_count(), 95U);
    EXPECT_EQ(view->descriptor().page_count, 1U);
}

} // namespace
