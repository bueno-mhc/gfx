// Distance-field generators (msdfgen and the built-in kernel, which must agree on true distances) and
// atlas packers (tight and grid: every box inside its page, no overlap, deterministic, several pages).
#include <gtest/gtest.h>

import std;
import gfx.tests.ui_atlas_support;

namespace
{

using namespace gfx::ui;

/// The outline of @p code_point in Noto Sans, and the face's units per em.
auto
outline_of(char32_t code_point) -> std::pair<GlyphOutline, std::uint32_t>
{
    const std::unique_ptr<FontFace> face = create_font_decoder(FontDecoderKind::FREETYPE)->open(gfx::tests::load_font("NotoSans.ttf"));
    const std::vector<CharacterMapping> map = face->character_map();
    const GlyphIndex glyph = std::ranges::find(map, code_point, &CharacterMapping::code_point)->glyph;
    return {face->outline(glyph), face->metrics().units_per_em};
}

/// A description of @p kind at 32 texels per em and a range of 4 texels.
auto
field_description(SdfKind kind) -> SdfGenerationDescription
{
    return {.kind = kind, .em_size = 32, .distance_range = 4};
}

/// The median of the first three channels of texel (@p x, @p y).
auto
median(const GlyphField& field,
       std::uint32_t x,
       std::uint32_t y) -> float
{
    const float red = field.at(x, y, 0);
    const float green = field.at(x, y, 1);
    const float blue = field.at(x, y, 2);
    return std::max(std::min(red, green), std::min(std::max(red, green), blue));
}

/// Runs of texels inside (value above 0.5) along row @p y of a field read by @p value.
template<class Value>
auto
inside_runs(const GlyphField& field,
            std::uint32_t y,
            Value value) -> int
{
    int runs = 0;
    bool inside = false;
    for (std::uint32_t x = 0; x < field.width; ++x)
    {
        const bool now = value(x, y) > 0.5F;
        runs += now && !inside ? 1 : 0;
        inside = now;
    }
    return runs;
}

TEST(SdfGeneratorTest, MsdfgenFieldsFollowTheOutline)
{
    // An O: outside, the ring, its hole, the ring, outside again, along its middle row.
    const auto [outline, units_per_em] = outline_of(U'O');
    const std::unique_ptr<SdfGenerator> generator = create_sdf_generator(SdfGeneratorKind::MSDFGEN);
    const SdfGenerationDescription description = field_description(SdfKind::MTSDF);
    const GlyphBox box = generator->layout(outline, units_per_em, description);
    ASSERT_FALSE(box.empty());
    const GlyphField field = generator->generate(outline, box, description);
    ASSERT_EQ(field.channel_count, 4U);
    ASSERT_EQ(field.values.size(), std::size_t{box.width} * box.height * 4);
    const std::uint32_t middle = box.height / 2;
    EXPECT_EQ(inside_runs(field, middle, [&](std::uint32_t x, std::uint32_t y) { return median(field, x, y); }), 2);
    EXPECT_EQ(inside_runs(field, middle, [&](std::uint32_t x, std::uint32_t y) { return field.at(x, y, 3); }), 2);
    EXPECT_LT(median(field, box.width / 2, middle), 0.5F);
    EXPECT_LT(field.at(0, 0, 3), 0.5F);

    // The box holds the outline, half the range and the padding: its quad is wider than the glyph.
    const GlyphPlaneBounds plane = box.plane_bounds(units_per_em);
    const OutlineBounds bounds = outline.bounds();
    EXPECT_LT(plane.left, bounds.left / units_per_em);
    EXPECT_GT(plane.top, bounds.top / units_per_em);
}

TEST(SdfGeneratorTest, EveryKindHasItsChannels)
{
    const auto [outline, units_per_em] = outline_of(U'A');
    const std::unique_ptr<SdfGenerator> generator = create_sdf_generator(SdfGeneratorKind::MSDFGEN);
    for (const auto& [kind, channels] : {std::pair{SdfKind::SDF, 1U}, std::pair{SdfKind::PSDF, 1U}, std::pair{SdfKind::MSDF, 3U}, std::pair{SdfKind::MTSDF, 4U}})
    {
        EXPECT_TRUE(generator->supports(kind));
        const SdfGenerationDescription description = field_description(kind);
        const GlyphField field = generator->generate(outline, generator->layout(outline, units_per_em, description), description);
        EXPECT_EQ(field.channel_count, channels) << to_string(kind);
        EXPECT_TRUE(std::ranges::any_of(field.values, [](float value) { return value > 0.5F; })) << to_string(kind);
        EXPECT_TRUE(std::ranges::any_of(field.values, [](float value) { return value < 0.5F; })) << to_string(kind);
    }
}

TEST(SdfGeneratorTest, BuiltInDistancesMatchMsdfgen)
{
    // True signed distances of the same boxes: within a hundredth of a texel (a quarter of an 8-bit step).
    const std::unique_ptr<SdfGenerator> msdfgen = create_sdf_generator(SdfGeneratorKind::MSDFGEN);
    const std::unique_ptr<SdfGenerator> built_in = create_sdf_generator(SdfGeneratorKind::BUILT_IN);
    const SdfGenerationDescription description = field_description(SdfKind::SDF);
    for (const char32_t code_point : {U'A', U'g', U'O', U'&', U'@', U'S'})
    {
        const auto [outline, units_per_em] = outline_of(code_point);
        const GlyphBox box = msdfgen->layout(outline, units_per_em, description);
        const GlyphField expected = msdfgen->generate(outline, box, description);
        const GlyphField field = built_in->generate(outline, box, description);
        ASSERT_EQ(field.values.size(), expected.values.size());
        float largest = 0;
        for (std::size_t index = 0; index < field.values.size(); ++index)
        {
            largest = std::max(largest, std::abs(field.values[index] - expected.values[index]));
        }
        EXPECT_LT(largest, 0.0025F + 1e-4F) << static_cast<std::uint32_t>(code_point);

        // Its own boxes hold the glyph too.
        const GlyphBox own = built_in->layout(outline, units_per_em, description);
        EXPECT_NEAR(own.width, box.width, 1U) << static_cast<std::uint32_t>(code_point);
        EXPECT_NEAR(own.height, box.height, 1U) << static_cast<std::uint32_t>(code_point);
    }
}

TEST(SdfGeneratorTest, BuiltInGeneratesSdfOnly)
{
    const std::unique_ptr<SdfGenerator> generator = create_sdf_generator(SdfGeneratorKind::BUILT_IN);
    EXPECT_EQ(generator->kind(), SdfGeneratorKind::BUILT_IN);
    EXPECT_TRUE(generator->supports(SdfKind::SDF));
    EXPECT_FALSE(generator->supports(SdfKind::MTSDF));
    const auto [outline, units_per_em] = outline_of(U'A');
    EXPECT_THROW(static_cast<void>(generator->layout(outline, units_per_em, field_description(SdfKind::MTSDF))), gfx::BackendCapabilityError);
    EXPECT_THROW(static_cast<void>(create_sdf_generator(SdfGeneratorKind::CUSTOM)), gfx::InvalidUsageError);
}

TEST(SdfGeneratorTest, EmptyOutlinesHaveNoBox)
{
    for (const SdfGeneratorKind kind : {SdfGeneratorKind::MSDFGEN, SdfGeneratorKind::BUILT_IN})
    {
        const std::unique_ptr<SdfGenerator> generator = create_sdf_generator(kind);
        const SdfGenerationDescription description = field_description(SdfKind::SDF);
        const GlyphBox box = generator->layout({}, 1000, description);
        EXPECT_TRUE(box.empty());
        EXPECT_TRUE(generator->generate({}, box, description).values.empty());
    }
}

/// Boxes of random sizes (seeded), and a few spaces.
auto
random_boxes(std::size_t count,
             std::uint32_t largest) -> std::vector<AtlasBoxSize>
{
    std::mt19937 random{11};
    std::uniform_int_distribution<std::uint32_t> size{4, largest};
    std::vector<AtlasBoxSize> boxes;
    for (std::size_t index = 0; index < count; ++index)
    {
        boxes.push_back(index % 17 == 0 ? AtlasBoxSize{} : AtlasBoxSize{.width = size(random), .height = size(random)});
    }
    return boxes;
}

/// Checks that every placed box is inside its page and apart from the others by @p spacing.
void
expect_valid_packing(std::span<const AtlasBoxSize> boxes,
                     const AtlasPacking& packing,
                     std::uint32_t spacing)
{
    ASSERT_EQ(packing.placements.size(), boxes.size());
    for (std::size_t first = 0; first < boxes.size(); ++first)
    {
        const AtlasPlacement& a = packing.placements[first];
        if (boxes[first].width == 0)
        {
            continue;
        }
        EXPECT_LT(a.page, packing.page_count);
        EXPECT_LE(a.x + boxes[first].width, packing.page_width);
        EXPECT_LE(a.y + boxes[first].height, packing.page_height);
        for (std::size_t second = first + 1; second < boxes.size(); ++second)
        {
            const AtlasPlacement& b = packing.placements[second];
            if (boxes[second].width == 0 || a.page != b.page)
            {
                continue;
            }
            const bool apart = a.x + boxes[first].width + spacing <= b.x || b.x + boxes[second].width + spacing <= a.x
                               || a.y + boxes[first].height + spacing <= b.y || b.y + boxes[second].height + spacing <= a.y;
            EXPECT_TRUE(apart) << "boxes " << first << " and " << second << " overlap";
        }
    }
}

TEST(AtlasPackerTest, TightPackingPlacesEveryBoxApart)
{
    const std::vector<AtlasBoxSize> boxes = random_boxes(300, 40);
    const std::unique_ptr<AtlasPacker> packer = create_atlas_packer(AtlasPackerKind::TIGHT);
    const AtlasPacking packing = packer->pack(boxes, {});
    EXPECT_EQ(packing.page_count, 1U);
    EXPECT_EQ(packing.page_width, packing.page_height);
    EXPECT_TRUE(std::has_single_bit(packing.page_width));
    expect_valid_packing(boxes, packing, 0);

    // Deterministic, and with spacing the boxes stay apart.
    const AtlasPacking again = packer->pack(boxes, {});
    EXPECT_TRUE(std::ranges::equal(packing.placements, again.placements, [](const AtlasPlacement& a, const AtlasPlacement& b)
    {
        return a.page == b.page && a.x == b.x && a.y == b.y;
    }));
    expect_valid_packing(boxes, packer->pack(boxes, {.spacing = 2}), 2);

    // Sides that are not powers of two are multiples of 4.
    const AtlasPacking compact = packer->pack(boxes, {.power_of_two = false});
    EXPECT_EQ(compact.page_width % 4, 0U);
    EXPECT_LE(compact.page_width, packing.page_width);
    expect_valid_packing(boxes, compact, 0);
}

TEST(AtlasPackerTest, BoxesBeyondOnePageGoToMorePages)
{
    const std::vector<AtlasBoxSize> boxes = random_boxes(200, 40);
    for (const AtlasPackerKind kind : {AtlasPackerKind::TIGHT, AtlasPackerKind::GRID})
    {
        const AtlasPacking packing = create_atlas_packer(kind)->pack(boxes, {.maximum_dimension = 128});
        EXPECT_GT(packing.page_count, 1U) << to_string(kind);
        EXPECT_EQ(packing.page_width, 128U) << to_string(kind);
        expect_valid_packing(boxes, packing, 0);
    }
}

TEST(AtlasPackerTest, GridPackingUsesCellsOfTheLargestBox)
{
    const std::vector<AtlasBoxSize> boxes = random_boxes(100, 30);
    const std::unique_ptr<AtlasPacker> packer = create_atlas_packer(AtlasPackerKind::GRID);
    EXPECT_EQ(packer->kind(), AtlasPackerKind::GRID);
    const AtlasPacking packing = packer->pack(boxes, {});
    expect_valid_packing(boxes, packing, 0);
    const std::uint32_t cell = std::ranges::max(boxes, {}, &AtlasBoxSize::width).width;
    for (std::size_t index = 0; index < boxes.size(); ++index)
    {
        if (boxes[index].width != 0)
        {
            EXPECT_EQ(packing.placements[index].x % cell, 0U);
        }
    }
}

TEST(AtlasPackerTest, BoxesLargerThanAPageAreRefused)
{
    const std::array boxes{AtlasBoxSize{.width = 5000, .height = 10}};
    for (const AtlasPackerKind kind : {AtlasPackerKind::TIGHT, AtlasPackerKind::GRID})
    {
        EXPECT_THROW(static_cast<void>(create_atlas_packer(kind)->pack(boxes, {})), gfx::InvalidUsageError);
    }
    EXPECT_THROW(static_cast<void>(create_atlas_packer(AtlasPackerKind::CUSTOM)), gfx::InvalidUsageError);
}

TEST(AtlasPackerTest, WhitespaceNeedsNoPage)
{
    const std::array boxes{AtlasBoxSize{}, AtlasBoxSize{}};
    for (const AtlasPackerKind kind : {AtlasPackerKind::TIGHT, AtlasPackerKind::GRID})
    {
        const AtlasPacking packing = create_atlas_packer(kind)->pack(boxes, {});
        EXPECT_EQ(packing.page_count, 0U);
        EXPECT_EQ(packing.placements.size(), 2U);
    }
}

} // namespace
