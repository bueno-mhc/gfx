// The .uiatlas format: xxHash64 against the reference implementation, the little-endian layout, the
// writer (content read back, compression, refused content, reproducible bytes) and AtlasView's
// validation of damaged blobs.
#include <gtest/gtest.h>

#include "xxhash_reference.hpp"

import std;
import gfx.tests.ui_atlas_support;

namespace
{

using namespace gfx::ui;
namespace layout = gfx::ui::atlas_layout;

/// Entry of the section table of the small atlas, in the writer's order.
enum SectionIndex : std::size_t
{
    DESCRIPTOR_SECTION = 0,
    FACE_METRICS_SECTION = 1,
    GLYPHS_SECTION = 2,
    CHARACTER_MAP_SECTION = 3,
    KERNING_SECTION = 4,
    LIGATURES_SECTION = 5,
    SHAPING_FONT_SECTION = 6,
    PIXELS_SECTION = 7,
};

/// A small atlas: `.notdef`, a ligature and two glyphs, one page of 4 × 4 R8 texels.
auto
small_content(AtlasCompression compression = AtlasCompression::NONE) -> AtlasContent
{
    AtlasContent content{.descriptor = {.kind = SdfKind::SDF,
                                        .texel_format = AtlasTexelFormat::R8_UNORM,
                                        .page_width = 4,
                                        .page_height = 4,
                                        .em_size = 16,
                                        .distance_range = 2},
                         .face = {.units_per_em = 1000,
                                  .ascender = 800,
                                  .descender = -200,
                                  .line_gap = 90,
                                  .family = "Test Sans",
                                  .style = "Regular",
                                  .axes = {{.tag = font_tag("wght"), .value = 400}}},
                         .source_font_hash = 42,
                         .compression = compression};
    content.glyphs = {{.glyph = GlyphIndex{0}, .advance = 0.5F},
                      {.glyph = GlyphIndex{3},
                       .advance = 0.625F,
                       .plane = {.left = -0.1F, .bottom = -0.1F, .right = 0.7F, .top = 0.8F},
                       .texels = {.left = 0.5F, .top = 0.5F, .right = 3.5F, .bottom = 3.5F}},
                      {.glyph = GlyphIndex{4}, .advance = 0.25F},
                      {.glyph = GlyphIndex{7}, .flags = AtlasGlyphFlag::WHITESPACE, .advance = 0.25F}};
    content.characters = {{.code_point = U' ', .glyph = GlyphIndex{7}}, {.code_point = U'f', .glyph = GlyphIndex{3}},
                          {.code_point = U'i', .glyph = GlyphIndex{4}}};
    content.kerning = {{.left = GlyphIndex{3}, .right = GlyphIndex{4}, .adjustment = -0.05F}};
    content.ligatures = {{.components = {GlyphIndex{3}, GlyphIndex{4}}, .component_count = 2, .glyph = GlyphIndex{0}}};
    content.shaping_font = {std::byte{'f'}, std::byte{'o'}, std::byte{'n'}, std::byte{'t'}};
    content.pages = {std::vector<std::byte>(16, std::byte{0x80})};
    return content;
}

/// Offset of field @p field of section table entry @p section.
constexpr auto
entry(std::size_t section,
      std::size_t field) noexcept -> std::size_t
{
    return layout::Header::SIZE + section * layout::SectionEntry::SIZE + field;
}

/// The problem parse() reports for @p blob, or nullopt when it is valid.
auto
problem_of(std::span<const std::byte> blob,
           AtlasVerification verification = AtlasVerification::CHECKSUMS) -> std::optional<AtlasProblem>
{
    const auto view = AtlasView::parse(blob, verification);
    return view.has_value() ? std::nullopt : std::optional{view.error().problem};
}

TEST(AtlasFormatTest, XxHash64MatchesTheReference)
{
    // Known values, and every length up to 400 bytes with several seeds (stripes, then 8, 4 and 1 byte tails).
    static_assert(xxhash64({}) == 0xEF46DB3751D8E999ULL);
    std::vector<std::byte> bytes(400);
    std::mt19937 random{7};
    for (std::byte& byte : bytes)
    {
        byte = static_cast<std::byte>(random() & 0xFFU);
    }
    for (const std::uint64_t seed : {0ULL, 1ULL, 0x9E3779B185EBCA87ULL})
    {
        for (std::size_t size = 0; size <= bytes.size(); ++size)
        {
            const std::span<const std::byte> prefix{bytes.data(), size};
            ASSERT_EQ(xxhash64(prefix, seed), gfx::tests::reference_xxhash64(reinterpret_cast<const unsigned char*>(bytes.data()), size, seed))
                << size << " bytes, seed " << seed;
        }
    }
}

TEST(AtlasFormatTest, ValuesAreStoredLittleEndian)
{
    // Least significant byte first, floats and signed values through their bits.
    static_assert([]
    {
        std::array<std::byte, 16> bytes{};
        layout::store(std::span{bytes}, 0, std::uint32_t{0x01020304U});
        layout::store(std::span{bytes}, 4, -2.5F);
        layout::store(std::span{bytes}, 8, std::int32_t{-7});
        return bytes[0] == std::byte{0x04} && bytes[3] == std::byte{0x01} && layout::load<float>(bytes, 4) == -2.5F
               && layout::load<std::int32_t>(bytes, 8) == -7 && layout::load<std::uint16_t>(bytes, 0) == 0x0304U;
    }());
    static_assert(layout::aligned(0) == 0 && layout::aligned(1) == 8 && layout::aligned(64) == 64 && layout::aligned(65) == 72);
    static_assert(font_tag("wght") == 0x77676874U && font_tag_name(0x77676874U) == std::array{'w', 'g', 'h', 't'});
    SUCCEED();
}

TEST(AtlasWriterTest, WrittenContentReadsBack)
{
    const std::vector<std::byte> blob = write_atlas(small_content());
    const AtlasView view = gfx::tests::view_of(blob);

    // Header and layout: every section 8-byte aligned, the blob a multiple of 8.
    EXPECT_EQ(view.header().version_major, UI_ATLAS_VERSION_MAJOR);
    EXPECT_EQ(view.header().generator_version, UI_ATLAS_GENERATOR_VERSION);
    EXPECT_EQ(view.header().source_font_hash, 42U);
    EXPECT_EQ(view.header().section_count, 8U);
    EXPECT_EQ(blob.size() % UI_ATLAS_ALIGNMENT, 0U);
    for (std::size_t index = 0; index < view.header().section_count; ++index)
    {
        EXPECT_EQ(view.section(index).offset % UI_ATLAS_ALIGNMENT, 0U) << index;
    }

    // Descriptor and face.
    EXPECT_EQ(view.descriptor().kind, SdfKind::SDF);
    EXPECT_EQ(view.descriptor().page_count, 1U);
    EXPECT_EQ(view.descriptor().glyph_count, 4U);
    EXPECT_EQ(view.descriptor().em_size, 16.0F);
    const AtlasFaceMetrics face = view.face_metrics();
    EXPECT_EQ(face.units_per_em, 1000U);
    EXPECT_EQ(face.descender, -200);
    EXPECT_TRUE(face.family == "Test Sans");
    EXPECT_EQ(face.style.to_string(), "Regular");
    ASSERT_EQ(face.axis_count, 1U);
    EXPECT_EQ(face.axis(0).tag, font_tag("wght"));
    EXPECT_EQ(face.axis(0).value, 400.0F);

    // Tables.
    const std::optional<AtlasGlyph> glyph = view.find_glyph(GlyphIndex{3});
    ASSERT_TRUE(glyph.has_value());
    EXPECT_EQ(glyph->advance, 0.625F);
    EXPECT_EQ(glyph->plane.top, 0.8F);
    EXPECT_EQ(glyph->texels.right, 3.5F);
    EXPECT_TRUE(glyph->has_box());
    EXPECT_FALSE(view.find_glyph(GlyphIndex{5}).has_value());
    EXPECT_EQ(view.find_glyph(GlyphIndex{7})->flags, AtlasGlyphFlag::WHITESPACE);
    EXPECT_EQ(view.find_character(U'f'), GlyphIndex{3});
    EXPECT_EQ(view.find_character(U'g'), std::nullopt);
    EXPECT_EQ(view.find_kerning(GlyphIndex{3}, GlyphIndex{4}), -0.05F);
    EXPECT_EQ(view.find_kerning(GlyphIndex{4}, GlyphIndex{3}), 0.0F);
    ASSERT_EQ(view.ligature_count(), 1U);
    EXPECT_EQ(view.ligature_at(0).component_count, 2U);
    EXPECT_EQ(view.ligature_at(0).components[1], GlyphIndex{4});

    // Shaping font and page.
    const std::optional<AtlasSection> shaping = view.shaping_font();
    ASSERT_TRUE(shaping.has_value());
    EXPECT_EQ(gfx::tests::decompress(view, *shaping), small_content().shaping_font);
    EXPECT_EQ(gfx::tests::decompress(view, view.page(0)), small_content().pages[0]);
}

TEST(AtlasWriterTest, CompressedSectionsDecompressToTheirContent)
{
    // zstd for the shaping font and the pages, never for the tables.
    const std::vector<std::byte> blob = write_atlas(small_content(AtlasCompression::ZSTD));
    const AtlasView view = gfx::tests::view_of(blob);
    EXPECT_EQ(view.page(0).compression, AtlasCompression::ZSTD);
    EXPECT_EQ(view.shaping_font()->compression, AtlasCompression::ZSTD);
    EXPECT_EQ(view.find_section(AtlasSectionKind::GLYPHS)->compression, AtlasCompression::NONE);
    EXPECT_EQ(gfx::tests::decompress(view, view.page(0)), small_content().pages[0]);
    EXPECT_EQ(gfx::tests::decompress(view, *view.shaping_font()), small_content().shaping_font);
}

TEST(AtlasWriterTest, SameContentGivesSameBytes)
{
    EXPECT_EQ(write_atlas(small_content(AtlasCompression::ZSTD)), write_atlas(small_content(AtlasCompression::ZSTD)));
}

TEST(AtlasWriterTest, InconsistentContentIsRefused)
{
    AtlasContent unsorted = small_content();
    std::swap(unsorted.glyphs[1], unsorted.glyphs[2]);
    EXPECT_THROW(static_cast<void>(write_atlas(unsorted)), gfx::InvalidUsageError);

    AtlasContent dangling = small_content();
    dangling.characters.push_back({.code_point = U'z', .glyph = GlyphIndex{99}});
    EXPECT_THROW(static_cast<void>(write_atlas(dangling)), gfx::InvalidUsageError);

    AtlasContent short_page = small_content();
    short_page.pages[0].pop_back();
    EXPECT_THROW(static_cast<void>(write_atlas(short_page)), gfx::InvalidUsageError);

    AtlasContent wrong_format = small_content();
    wrong_format.descriptor.kind = SdfKind::MTSDF;
    EXPECT_THROW(static_cast<void>(write_atlas(wrong_format)), gfx::InvalidUsageError);

    AtlasContent long_ligature = small_content();
    long_ligature.ligatures[0].component_count = 5;
    EXPECT_THROW(static_cast<void>(write_atlas(long_ligature)), gfx::InvalidUsageError);
}

TEST(AtlasViewTest, DamagedHeadersAreRejected)
{
    const std::vector<std::byte> blob = write_atlas(small_content());
    EXPECT_EQ(problem_of(std::span{blob}.first(32)), AtlasProblem::TRUNCATED);
    EXPECT_EQ(problem_of(std::span{blob}.first(blob.size() - 8)), AtlasProblem::TRUNCATED);

    std::vector<std::byte> magic = blob;
    magic[7] = std::byte{'X'};
    EXPECT_EQ(problem_of(magic), AtlasProblem::BAD_MAGIC);

    std::vector<std::byte> version = blob;
    layout::store(std::span{version}, layout::Header::VERSION_MAJOR, std::uint16_t{2});
    EXPECT_EQ(problem_of(version), AtlasProblem::UNSUPPORTED_VERSION);

    std::vector<std::byte> endianness = blob;
    std::reverse(endianness.begin() + layout::Header::ENDIANNESS, endianness.begin() + layout::Header::ENDIANNESS + 4);
    EXPECT_EQ(problem_of(endianness), AtlasProblem::WRONG_ENDIANNESS);

    std::vector<std::byte> longer = blob;
    longer.resize(blob.size() + 8);
    EXPECT_EQ(problem_of(longer), AtlasProblem::SIZE_MISMATCH);
}

TEST(AtlasViewTest, ChecksumsDetectDamagedBytes)
{
    // A changed texel: the header's checksum, or with structure checks only, nothing.
    const std::vector<std::byte> blob = write_atlas(small_content());
    std::vector<std::byte> damaged = blob;
    const std::size_t texel = static_cast<std::size_t>(gfx::tests::view_of(blob).page(0).offset);
    damaged[texel] = std::byte{0x00};
    EXPECT_EQ(problem_of(damaged), AtlasProblem::CHECKSUM_MISMATCH);
    EXPECT_EQ(problem_of(damaged, AtlasVerification::STRUCTURE), std::nullopt);

    // With the header's checksum updated, the section's checksum still disagrees.
    layout::store(std::span{damaged}, layout::Header::CHECKSUM, xxhash64(std::span<const std::byte>{damaged}.subspan(layout::Header::SIZE)));
    EXPECT_EQ(problem_of(damaged), AtlasProblem::SECTION_CHECKSUM_MISMATCH);
    const auto error = AtlasView::parse(damaged).error();
    EXPECT_EQ(error.section, AtlasSectionKind::PIXELS);
    EXPECT_NE(describe_atlas_error(error).find("PIXELS"), std::string::npos) << describe_atlas_error(error);
}

TEST(AtlasViewTest, DamagedSectionTablesAreRejected)
{
    // Structure checks only: these blobs are damaged on purpose, their checksums are not updated.
    const std::vector<std::byte> blob = write_atlas(small_content());
    const auto patched = [&](std::size_t offset, auto value)
    {
        std::vector<std::byte> copy = blob;
        layout::store(std::span{copy}, offset, value);
        return problem_of(copy, AtlasVerification::STRUCTURE);
    };
    const std::uint64_t glyphs = gfx::tests::view_of(blob).find_section(AtlasSectionKind::GLYPHS)->offset;
    EXPECT_EQ(patched(entry(GLYPHS_SECTION, layout::SectionEntry::OFFSET), glyphs + 4), AtlasProblem::MISALIGNED_SECTION);
    EXPECT_EQ(patched(entry(GLYPHS_SECTION, layout::SectionEntry::OFFSET), std::uint64_t{blob.size()}), AtlasProblem::SECTION_OUT_OF_BOUNDS);
    EXPECT_EQ(patched(entry(GLYPHS_SECTION, layout::SectionEntry::STORED_SIZE), std::uint64_t{1} << 40), AtlasProblem::SECTION_OUT_OF_BOUNDS);
    EXPECT_EQ(patched(entry(PIXELS_SECTION, layout::SectionEntry::COMPRESSION), std::uint32_t{9}), AtlasProblem::UNKNOWN_COMPRESSION);
    EXPECT_EQ(patched(entry(GLYPHS_SECTION, layout::SectionEntry::KIND), std::uint32_t{200}), AtlasProblem::MISSING_SECTION);
    EXPECT_EQ(patched(entry(KERNING_SECTION, layout::SectionEntry::KIND), static_cast<std::uint32_t>(AtlasSectionKind::GLYPHS)),
              AtlasProblem::DUPLICATE_SECTION);
    EXPECT_EQ(patched(entry(GLYPHS_SECTION, layout::SectionEntry::COMPRESSION), static_cast<std::uint32_t>(AtlasCompression::ZSTD)),
              AtlasProblem::MALFORMED_SECTION);

    // A section of a kind this reader does not know is skipped.
    std::vector<std::byte> unknown = blob;
    layout::store(std::span{unknown}, entry(KERNING_SECTION, layout::SectionEntry::KIND), std::uint32_t{200});
    const auto view = AtlasView::parse(unknown, AtlasVerification::STRUCTURE);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->kerning_count(), 0U);
}

TEST(AtlasViewTest, InconsistentSectionsAreRejected)
{
    const std::vector<std::byte> blob = write_atlas(small_content());
    const AtlasView view = gfx::tests::view_of(blob);
    const auto patched = [&](AtlasSectionKind kind, std::size_t field, auto value)
    {
        std::vector<std::byte> copy = blob;
        layout::store(std::span{copy}, static_cast<std::size_t>(view.find_section(kind)->offset) + field, value);
        return problem_of(copy, AtlasVerification::STRUCTURE);
    };
    using layout::Table;
    EXPECT_EQ(patched(AtlasSectionKind::DESCRIPTOR, layout::Descriptor::KIND, std::uint8_t{9}), AtlasProblem::MALFORMED_SECTION);
    EXPECT_EQ(patched(AtlasSectionKind::DESCRIPTOR, layout::Descriptor::TEXEL_FORMAT, std::uint8_t{1}), AtlasProblem::MALFORMED_SECTION);
    EXPECT_EQ(patched(AtlasSectionKind::DESCRIPTOR, layout::Descriptor::PAGE_COUNT, std::uint32_t{2}), AtlasProblem::MALFORMED_SECTION);
    EXPECT_EQ(patched(AtlasSectionKind::DESCRIPTOR, layout::Descriptor::PAGE_WIDTH, std::uint32_t{8}), AtlasProblem::MALFORMED_SECTION);
    EXPECT_EQ(patched(AtlasSectionKind::GLYPHS, Table::COUNT, std::uint32_t{1000}), AtlasProblem::MALFORMED_SECTION);
    EXPECT_EQ(patched(AtlasSectionKind::GLYPHS, Table::RECORD_SIZE, std::uint32_t{8}), AtlasProblem::MALFORMED_SECTION);
    EXPECT_EQ(patched(AtlasSectionKind::GLYPHS, Table::RECORDS + layout::GlyphRecord::GLYPH, std::uint32_t{5}), AtlasProblem::MALFORMED_SECTION);
    EXPECT_EQ(patched(AtlasSectionKind::CHARACTER_MAP, Table::RECORDS + layout::CharacterRecord::GLYPH, std::uint32_t{99}),
              AtlasProblem::MALFORMED_SECTION);
    EXPECT_EQ(patched(AtlasSectionKind::KERNING, Table::RECORDS + layout::KerningRecord::RIGHT, std::uint32_t{99}), AtlasProblem::MALFORMED_SECTION);
    EXPECT_EQ(patched(AtlasSectionKind::LIGATURES, Table::RECORDS + layout::LigatureRecord::COMPONENT_COUNT, std::uint32_t{1}),
              AtlasProblem::MALFORMED_SECTION);
    EXPECT_EQ(patched(AtlasSectionKind::FACE_METRICS, layout::FaceMetrics::FAMILY_SIZE, std::uint32_t{1000}), AtlasProblem::MALFORMED_SECTION);
}

TEST(AtlasViewTest, ErrorsAreDescribed)
{
    // Every problem has its own explanation.
    std::set<std::string> descriptions;
    for (const auto& entry : gfx::EnumerationTraits<AtlasProblem>::NAMES)
    {
        const std::string text = describe_atlas_error({.problem = entry.value, .section = AtlasSectionKind::GLYPHS, .offset = 8});
        EXPECT_FALSE(text.empty());
        descriptions.insert(text);
    }
    EXPECT_EQ(descriptions.size(), gfx::ENUMERATOR_COUNT<AtlasProblem>);
}

} // namespace
