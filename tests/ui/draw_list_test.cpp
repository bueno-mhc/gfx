// The draw list (`gfx::ui::DrawList`): the vertices, indices and commands of shapes, lines, polygons, images
// and text, under transforms and clips, and when a new command starts; colors, rectangles and transforms;
// the debug overlay drawn into a list.
#include <gtest/gtest.h>

import std;
import gfx.ui;
import gfx.tests.draw_latin;

namespace
{

using namespace gfx::ui;
using gfx::Vector2;

constexpr Color RED{.red = 1, .alpha = 1};
constexpr Color GREEN{.green = 1, .alpha = 1};

/// The shader mode of @p vertex.
auto
mode_of(const UiVertex& vertex) -> UiShaderMode
{
    return static_cast<UiShaderMode>(vertex.mode & 0xFFU);
}

/// The smallest rectangle holding every vertex of @p list.
auto
bounds_of(const DrawList& list) -> UiRectangle
{
    UiRectangle bounds{.left = 1.0e9F, .top = 1.0e9F, .right = -1.0e9F, .bottom = -1.0e9F};
    for (const UiVertex& vertex : list.vertices())
    {
        bounds.left = std::min(bounds.left, vertex.position.x);
        bounds.top = std::min(bounds.top, vertex.position.y);
        bounds.right = std::max(bounds.right, vertex.position.x);
        bounds.bottom = std::max(bounds.bottom, vertex.position.y);
    }
    return bounds;
}

/// Whether every index of @p list names one of its vertices, and its commands cover its indices in order.
auto
is_consistent(const DrawList& list) -> bool
{
    const bool indices_valid = std::ranges::all_of(list.indices(), [&](std::uint32_t index) { return index < list.vertices().size(); });
    std::uint32_t next = 0;
    for (const DrawCommand& command : list.commands())
    {
        if (command.first_index != next)
        {
            return false;
        }
        next += command.index_count;
    }
    return indices_valid && next == list.indices().size();
}

/// A font library with the Basic Latin atlas built for these tests, and a 32-pixel font of it.
struct DrawFonts
{
    DrawFonts()
        : atlas{library.add_atlas(FontAtlas::load(gfx::tests::draw_atlases::draw_latin))}
        , font{library.add_font({.atlas = atlas, .size = 32})}
    {
    }

    FontLibrary library;
    FontAtlasHandle atlas{};
    FontHandle font{};
};

TEST(ColorTest, PacksItsChannelsRedInTheLowestByte)
{
    EXPECT_EQ((Color{.red = 1, .green = 0, .blue = 0, .alpha = 0}.packed()), 0x000000FFU);
    EXPECT_EQ((Color{.red = 0, .green = 0, .blue = 1, .alpha = 1}.packed()), 0xFFFF0000U);
    EXPECT_EQ((Color{.red = 0.5F, .green = 2, .blue = -1, .alpha = 1}.packed()), 0xFF00FF80U) << "channels are rounded and clamped";
    EXPECT_EQ(Color::from_rgba8(0x12, 0x34, 0x56, 0x78).packed(), 0x78563412U);
    EXPECT_EQ(RED.with_alpha(0.5F).alpha, 0.5F);
    EXPECT_EQ(RED.with_alpha(0.5F).red, 1.0F);
}

TEST(UiRectangleTest, IntersectsAndMeasures)
{
    constexpr UiRectangle first{.left = 0, .top = 0, .right = 10, .bottom = 8};
    constexpr UiRectangle second{.left = 4, .top = -2, .right = 20, .bottom = 6};
    static_assert(first.width() == 10 && first.height() == 8);
    static_assert(first.intersection(second) == UiRectangle{.left = 4, .top = 0, .right = 10, .bottom = 6});
    static_assert(first.intersection({.left = 12, .top = 0, .right = 14, .bottom = 2}).empty());
    static_assert(UiRectangle{}.empty());
}

TEST(Transform2DTest, ComposesRightToLeft)
{
    // translation * scaling: scaled first, then moved.
    constexpr Transform2D transform = Transform2D::translation({.x = 10, .y = 20}) * Transform2D::scaling(2, 3);
    static_assert(transform.apply({.x = 1, .y = 1}) == Vector2{.x = 12, .y = 23});
    static_assert(transform.apply_to_vector({.x = 1, .y = 1}) == Vector2{.x = 2, .y = 3});

    // A quarter turn is clockwise on screen (y down): x goes to y; about a pivot, the pivot stays.
    const Transform2D quarter = Transform2D::rotation(std::numbers::pi_v<float> / 2);
    const Vector2 turned = quarter.apply({.x = 1, .y = 0});
    EXPECT_NEAR(turned.x, 0.0F, 1.0e-6F);
    EXPECT_NEAR(turned.y, 1.0F, 1.0e-6F);
    const Transform2D about = Transform2D::about({.x = 5, .y = 5}, quarter);
    const Vector2 pivot = about.apply({.x = 5, .y = 5});
    EXPECT_NEAR(pivot.x, 5.0F, 1.0e-5F);
    EXPECT_NEAR(pivot.y, 5.0F, 1.0e-5F);
}

TEST(DrawListTest, ARectangleIsOneQuadOfTheShapeShader)
{
    // A quad one pixel larger than the rectangle each side (its anti-aliased edge), evaluated by the shader
    // as a box of the rectangle's half size about its center.
    DrawList list;
    list.add_rectangle_filled({.left = 10, .top = 20, .right = 30, .bottom = 30}, RED, 3);
    ASSERT_EQ(list.vertices().size(), 4U);
    ASSERT_EQ(list.indices().size(), 6U);
    ASSERT_EQ(list.commands().size(), 1U);
    EXPECT_TRUE(is_consistent(list));
    EXPECT_EQ(bounds_of(list), (UiRectangle{.left = 9, .top = 19, .right = 31, .bottom = 31}));
    for (const UiVertex& vertex : list.vertices())
    {
        EXPECT_EQ(mode_of(vertex), UiShaderMode::SHAPE);
        EXPECT_EQ(vertex.shape, (std::array{10.0F, 5.0F, 3.0F, 0.0F})) << "half width, half height, corner radius, border";
        EXPECT_EQ(vertex.color, RED.packed());
        EXPECT_EQ(std::abs(vertex.local.x), 11.0F) << "pixels from the center, the margin included";
        EXPECT_EQ(std::abs(vertex.local.y), 6.0F);
    }
    EXPECT_EQ(list.commands().front().texture.kind, DrawTextureKind::NONE);
}

TEST(DrawListTest, CornerRadiiAndBordersStayInsideTheShape)
{
    // Radii and borders larger than half the shorter side are limited to it.
    DrawList list;
    list.add_rectangle({.left = 0, .top = 0, .right = 20, .bottom = 10}, RED, 50, 40);
    ASSERT_EQ(list.vertices().size(), 4U);
    EXPECT_EQ(list.vertices().front().shape, (std::array{10.0F, 5.0F, 5.0F, 5.0F}));

    // A circle is a box whose corners are rounded by its radius; a ring has a border.
    list.clear();
    list.add_circle({.x = 50, .y = 50}, 8, GREEN, 2);
    ASSERT_EQ(list.vertices().size(), 4U);
    EXPECT_EQ(list.vertices().front().shape, (std::array{8.0F, 8.0F, 8.0F, 2.0F}));
}

TEST(DrawListTest, NothingIsAddedForEmptyOrTransparentPrimitives)
{
    DrawList list;
    list.add_rectangle_filled({.left = 5, .top = 5, .right = 5, .bottom = 20}, RED);
    list.add_rectangle_filled({.left = 0, .top = 0, .right = 10, .bottom = 10}, RED.with_alpha(0));
    list.add_rectangle({.left = 0, .top = 0, .right = 10, .bottom = 10}, RED, 0);
    list.add_circle_filled({.x = 1, .y = 1}, 0, RED);
    list.add_line({.x = 3, .y = 3}, {.x = 3, .y = 3}, RED);
    list.add_line({.x = 0, .y = 0}, {.x = 9, .y = 9}, RED, 0);
    const std::array one_point{Vector2{.x = 1, .y = 1}};
    list.add_polyline(one_point, RED);
    const std::array two_points{Vector2{.x = 1, .y = 1}, Vector2{.x = 4, .y = 1}};
    list.add_convex_polygon_filled(two_points, RED);
    list.add_image(3, {.left = 0, .top = 0, .right = 0, .bottom = 0});
    EXPECT_TRUE(list.vertices().empty());
    EXPECT_TRUE(list.indices().empty());
    EXPECT_TRUE(list.commands().empty());
}

TEST(DrawListTest, LinesAreBoxesAlongTheirDirection)
{
    // A diagonal line 4 pixels thick: a box rotated by 45 degrees, its long axis from one end to the other.
    DrawList list;
    list.add_line({.x = 0, .y = 0}, {.x = 30, .y = 40}, RED, 4);
    ASSERT_EQ(list.vertices().size(), 4U);
    const UiVertex& vertex = list.vertices().front();
    EXPECT_EQ(mode_of(vertex), UiShaderMode::SHAPE);
    EXPECT_FLOAT_EQ(vertex.shape[0], 25.0F) << "half its length";
    EXPECT_FLOAT_EQ(vertex.shape[1], 2.0F) << "half its thickness";

    // Every corner lies at the half extents (plus the margin) from the middle, along the line's axes.
    const Vector2 axis{.x = 0.6F, .y = 0.8F};
    for (const UiVertex& corner : list.vertices())
    {
        const Vector2 offset{.x = corner.position.x - 15, .y = corner.position.y - 20};
        EXPECT_NEAR(std::abs(offset.x * axis.x + offset.y * axis.y), 26.0F, 1.0e-4F);
        EXPECT_NEAR(std::abs(-offset.x * axis.y + offset.y * axis.x), 3.0F, 1.0e-4F);
    }
}

TEST(DrawListTest, PolylinesHaveFourVerticesPerPointAndThreeQuadsPerSegment)
{
    const std::array points{Vector2{.x = 0, .y = 0}, Vector2{.x = 20, .y = 0}, Vector2{.x = 20, .y = 20}, Vector2{.x = 0, .y = 20}};
    DrawList list;
    list.add_polyline(points, RED, 2);
    EXPECT_EQ(list.vertices().size(), 16U);
    EXPECT_EQ(list.indices().size(), 3U * 18U);
    EXPECT_TRUE(is_consistent(list));

    // Closed: one segment more; the corners are mitered (the edges meet on the corner's diagonal). The
    // fringe, a pixel wide, is centered on the line's edge: the core ends half a pixel inside it.
    list.clear();
    list.add_polyline(points, RED, 2, true);
    EXPECT_EQ(list.indices().size(), 4U * 18U);
    EXPECT_TRUE(is_consistent(list));
    const UiVertex& outer = list.vertices()[0];
    const UiVertex& core = list.vertices()[1];
    EXPECT_NEAR(std::abs(outer.position.x), 1.5F, 1.0e-5F) << "the fringe's outer edge at the corner, from both sides";
    EXPECT_NEAR(std::abs(outer.position.y), 1.5F, 1.0e-5F);
    EXPECT_NEAR(std::abs(core.position.x), 0.5F, 1.0e-5F);
    EXPECT_NEAR(std::abs(core.position.y), 0.5F, 1.0e-5F);

    // A line thinner than a pixel has no core: it fades by its thickness.
    DrawList thin;
    thin.add_polyline(points, RED, 0.5F);
    EXPECT_EQ(thin.vertices()[1].position, thin.vertices()[2].position);
    EXPECT_EQ(thin.vertices()[1].color, RED.with_alpha(0.5F).packed());

    // The core's vertices are opaque, the fringe's transparent.
    for (std::size_t vertex = 0; vertex < list.vertices().size(); ++vertex)
    {
        const bool fringe = vertex % 4 == 0 || vertex % 4 == 3;
        EXPECT_EQ(list.vertices()[vertex].color, fringe ? RED.with_alpha(0).packed() : RED.packed()) << vertex;
        EXPECT_EQ(mode_of(list.vertices()[vertex]), UiShaderMode::SOLID);
    }
}

TEST(DrawListTest, ConvexPolygonsAreFansWithAFringeWhateverTheirWinding)
{
    const std::array clockwise{Vector2{.x = 0, .y = 0}, Vector2{.x = 30, .y = 0}, Vector2{.x = 30, .y = 30}, Vector2{.x = 0, .y = 30}};
    std::array counterclockwise = clockwise;
    std::ranges::reverse(counterclockwise);
    DrawList first;
    first.add_convex_polygon_filled(clockwise, GREEN);
    DrawList second;
    second.add_convex_polygon_filled(counterclockwise, GREEN);
    ASSERT_EQ(first.vertices().size(), 8U);
    EXPECT_EQ(first.indices().size(), 2U * 3U + 4U * 6U);
    EXPECT_TRUE(is_consistent(first));

    // Inner vertices half a pixel inside, outer ones half a pixel outside, either way round.
    for (const UiRectangle& bounds : {bounds_of(first), bounds_of(second)})
    {
        EXPECT_NEAR(bounds.left, -0.5F, 1.0e-5F);
        EXPECT_NEAR(bounds.top, -0.5F, 1.0e-5F);
        EXPECT_NEAR(bounds.right, 30.5F, 1.0e-5F);
        EXPECT_NEAR(bounds.bottom, 30.5F, 1.0e-5F);
    }
    const UiVertex& inner = first.vertices()[0];
    EXPECT_NEAR(inner.position.x, 0.5F, 1.0e-5F);
    EXPECT_NEAR(inner.position.y, 0.5F, 1.0e-5F);
    EXPECT_EQ(inner.color, GREEN.packed());
    EXPECT_EQ(first.vertices()[1].color, GREEN.with_alpha(0).packed());
}

TEST(DrawListTest, TransformsMoveVerticesButKeepTheEdgeOnePixelWide)
{
    // Scaled by 4: the rectangle covers 8 to 32 pixels, its margin is still one pixel (a quarter of a unit).
    DrawList list;
    list.push_transform(Transform2D::scaling(4, 4));
    list.add_rectangle_filled({.left = 2, .top = 2, .right = 8, .bottom = 6}, RED);
    EXPECT_EQ(bounds_of(list), (UiRectangle{.left = 7, .top = 7, .right = 33, .bottom = 25}));
    EXPECT_EQ(std::abs(list.vertices().front().local.x), 3.25F) << "units of the shape's own frame";

    // Nested transforms compose; popping restores the previous one (popping the base one does nothing).
    list.push_transform(Transform2D::translation({.x = 1, .y = 0}));
    list.add_rectangle_filled({.left = 0, .top = 0, .right = 1, .bottom = 1}, RED);
    EXPECT_FLOAT_EQ(list.vertices()[4].position.x, 4 * (1 + 0.5F - 0.75F)) << "translated, then scaled";
    list.pop_transform();
    list.pop_transform();
    list.pop_transform();
    list.add_rectangle_filled({.left = 0, .top = 0, .right = 2, .bottom = 2}, RED);
    EXPECT_FLOAT_EQ(list.vertices()[8].position.x, -1.0F);
}

TEST(DrawListTest, CommandsChangeWithTheClipRectangleOrTheTexture)
{
    DrawList list;
    list.add_rectangle_filled({.left = 0, .top = 0, .right = 10, .bottom = 10}, RED);
    list.add_circle_filled({.x = 20, .y = 20}, 4, GREEN);
    ASSERT_EQ(list.commands().size(), 1U) << "shapes of one clip share a command";

    // A clip within the current clip: a new command, clipped to the intersection.
    list.push_clip_rectangle({.left = 5, .top = 5, .right = 50, .bottom = 50});
    list.push_clip_rectangle({.left = 0, .top = 10, .right = 30, .bottom = 60});
    list.add_rectangle_filled({.left = 0, .top = 0, .right = 10, .bottom = 10}, RED);
    ASSERT_EQ(list.commands().size(), 2U);
    EXPECT_EQ(list.commands()[1].clip, (UiRectangle{.left = 5, .top = 10, .right = 30, .bottom = 50}));
    list.pop_clip_rectangle();
    list.pop_clip_rectangle();

    // An image: a new command sampling it; shapes join it (they sample nothing); another image: a new one.
    list.add_image(7, {.left = 0, .top = 0, .right = 16, .bottom = 16});
    list.add_rectangle_filled({.left = 0, .top = 0, .right = 10, .bottom = 10}, RED);
    list.add_image(8, {.left = 0, .top = 0, .right = 16, .bottom = 16});
    ASSERT_EQ(list.commands().size(), 4U);
    EXPECT_EQ(list.commands()[2].texture, DrawTexture::from_image(7));
    EXPECT_EQ(list.commands()[2].index_count, 12U);
    EXPECT_EQ(list.commands()[3].texture, DrawTexture::from_image(8));
    EXPECT_TRUE(is_consistent(list));

    // Back to the unclipped list: the clip of every primitive outside the pushed ones.
    list.add_rectangle_filled({.left = 0, .top = 0, .right = 10, .bottom = 10}, RED);
    EXPECT_EQ(list.commands().back().clip, list.commands().front().clip);
}

TEST(DrawListTest, ImagesAreQuadsWithTheirTextureCoordinates)
{
    DrawList list;
    list.add_image(5, {.left = 10, .top = 10, .right = 42, .bottom = 26}, GREEN, {.left = 0.25F, .top = 0, .right = 0.75F, .bottom = 1});
    ASSERT_EQ(list.vertices().size(), 4U);
    EXPECT_EQ(mode_of(list.vertices()[0]), UiShaderMode::IMAGE);
    EXPECT_EQ(list.vertices()[0].local, (Vector2{.x = 0.25F, .y = 0}));
    EXPECT_EQ(list.vertices()[2].local, (Vector2{.x = 0.75F, .y = 1}));
    EXPECT_EQ(list.vertices()[2].position, (Vector2{.x = 42, .y = 26}));
    EXPECT_EQ(list.vertices()[0].color, GREEN.packed()) << "the tint";
}

TEST(DrawListTest, TextIsAQuadPerVisibleGlyphOnItsAtlasPage)
{
    // "H i": two glyphs with boxes (the space has none), placed from the atlas's plane bounds at 32 pixels.
    DrawFonts fonts;
    TextLayoutEngine engine{fonts.library};
    const TextLayout layout = engine.layout("H i", fonts.font);
    ASSERT_EQ(layout.glyphs.size(), 3U);
    DrawList list;
    list.add_text(fonts.library, layout, {.x = 100, .y = 50});
    ASSERT_EQ(list.vertices().size(), 8U);
    ASSERT_EQ(list.commands().size(), 1U);
    EXPECT_EQ(list.commands().front().texture.kind, DrawTextureKind::FONT_PAGE);
    EXPECT_EQ(list.commands().front().texture.atlas, fonts.atlas);

    const std::shared_ptr<const FontAtlas> atlas = fonts.library.atlas(fonts.atlas);
    const AtlasDescriptor& descriptor = atlas->descriptor();
    const std::optional<AtlasGlyph> h = atlas->view().find_glyph(layout.glyphs[0].glyph);
    ASSERT_TRUE(h.has_value());
    const UiVertex& top_left = list.vertices()[0];
    EXPECT_FLOAT_EQ(top_left.position.x, 100 + layout.glyphs[0].x + h->plane.left * 32);
    EXPECT_FLOAT_EQ(top_left.position.y, 50 + layout.glyphs[0].y - h->plane.top * 32);
    EXPECT_FLOAT_EQ(top_left.local.x, h->texels.left / static_cast<float>(descriptor.page_width));
    EXPECT_FLOAT_EQ(top_left.local.y, h->texels.top / static_cast<float>(descriptor.page_height));
    EXPECT_EQ(mode_of(top_left), UiShaderMode::GLYPH);
    EXPECT_EQ((top_left.mode >> 8U) & 0xFFU, std::uint32_t{std::to_underlying(descriptor.kind)}) << "the atlas's field kind";
    EXPECT_EQ(top_left.shape[0], descriptor.distance_range);
    EXPECT_EQ(top_left.color, (Color{.red = 1, .green = 1, .blue = 1, .alpha = 1}.packed())) << "white by default";
}

TEST(DrawListTest, ShadowsAreDrawnUnderTheTextAndOutlinesWithIt)
{
    DrawFonts fonts;
    TextLayoutEngine engine{fonts.library};
    const TextLayout layout = engine.layout("Hi", fonts.font);
    DrawList list;
    const TextStyle style{.color = GREEN,
                          .outline_color = RED,
                          .outline_width = 2,
                          .shadow_color = Color{.alpha = 0.5F},
                          .shadow_offset = {.x = 3, .y = 4},
                          .shadow_softness = 1.5F};
    list.add_text(fonts.library, layout, {.x = 0, .y = 0}, style);
    ASSERT_EQ(list.vertices().size(), 16U);

    // The shadow's quads first, moved by the offset, then the text's, with the outline's color and width.
    for (std::size_t vertex = 0; vertex < 8; ++vertex)
    {
        const UiVertex& shadow = list.vertices()[vertex];
        const UiVertex& text = list.vertices()[vertex + 8];
        EXPECT_EQ(mode_of(shadow), UiShaderMode::GLYPH_SHADOW);
        EXPECT_EQ(mode_of(text), UiShaderMode::GLYPH);
        EXPECT_FLOAT_EQ(shadow.position.x, text.position.x + 3);
        EXPECT_FLOAT_EQ(shadow.position.y, text.position.y + 4);
        EXPECT_EQ(shadow.shape[2], 1.5F) << "softness";
        EXPECT_EQ(shadow.color, style.shadow_color.packed());
        EXPECT_EQ(text.shape[1], 2.0F) << "outline width";
        EXPECT_EQ(text.secondary_color, RED.packed());
    }
}

TEST(DrawListTest, GlyphsOfAnAtlasTheLibraryLacksAreSkipped)
{
    DrawFonts fonts;
    TextLayoutEngine engine{fonts.library};
    const TextLayout layout = engine.layout("Hi", fonts.font);
    const FontLibrary empty;
    DrawList list;
    list.add_text(empty, layout, {.x = 0, .y = 0});
    EXPECT_TRUE(list.vertices().empty());
}

TEST(DrawListTest, ClearResetsTheStacks)
{
    DrawList list;
    list.push_clip_rectangle({.left = 0, .top = 0, .right = 4, .bottom = 4});
    list.push_transform(Transform2D::translation({.x = 100, .y = 0}));
    list.add_rectangle_filled({.left = 0, .top = 0, .right = 2, .bottom = 2}, RED);
    list.clear();
    EXPECT_TRUE(list.vertices().empty());
    EXPECT_TRUE(list.commands().empty());
    list.add_rectangle_filled({.left = 0, .top = 0, .right = 2, .bottom = 2}, RED);
    EXPECT_EQ(bounds_of(list).left, -1.0F) << "no transform";
    EXPECT_GT(list.commands().front().clip.width(), 1.0e6F) << "no clip";
}

TEST(DebugOverlayTest, DrawsNothingWhenHidden)
{
    DrawFonts fonts;
    TextLayoutEngine engine{fonts.library};
    DebugOverlay overlay;
    DrawList list;
    const std::vector<std::string> lines{"60.0 fps"};
    EXPECT_TRUE(overlay.draw(list, engine, fonts.library, fonts.font, lines).empty());
    EXPECT_TRUE(list.vertices().empty());

    // Shown, but without lines: nothing either.
    overlay.toggle();
    EXPECT_TRUE(overlay.is_visible());
    EXPECT_TRUE(overlay.draw(list, engine, fonts.library, fonts.font, {}).empty());
    EXPECT_TRUE(list.vertices().empty());
}

TEST(DebugOverlayTest, DrawsAPanelUnderItsLines)
{
    DrawFonts fonts;
    TextLayoutEngine engine{fonts.library};
    DebugOverlay overlay{{.visible = true, .position = {.x = 10, .y = 20}, .padding = 5}};
    DrawList list;
    const std::vector<std::string> lines{"60.0 fps", "draws 12"};
    const UiRectangle panel = overlay.draw(list, engine, fonts.library, fonts.font, lines);

    // The panel holds both lines and its padding: a rounded shape, then a glyph quad per visible character.
    const TextLayout layout = engine.layout("60.0 fps\ndraws 12", fonts.font);
    ASSERT_EQ(layout.lines.size(), 2U);
    EXPECT_EQ(panel.left, 10.0F);
    EXPECT_EQ(panel.top, 20.0F);
    EXPECT_FLOAT_EQ(panel.width(), layout.width + 10);
    EXPECT_FLOAT_EQ(panel.height(), layout.height + 10);
    ASSERT_GE(list.vertices().size(), 4U);
    EXPECT_EQ(mode_of(list.vertices()[0]), UiShaderMode::SHAPE);
    const std::shared_ptr<const FontAtlas> atlas = fonts.library.atlas(fonts.atlas);
    const auto visible = static_cast<std::size_t>(std::ranges::count_if(layout.glyphs, [&](const LaidOutGlyph& glyph)
    {
        const std::optional<AtlasGlyph> record = atlas->view().find_glyph(glyph.glyph);
        return record.has_value() && record->has_box();
    }));
    EXPECT_EQ(visible, 14U) << "every character but the spaces and the line break";
    EXPECT_EQ(list.vertices().size(), 4U + 4U * visible);
    EXPECT_EQ(list.commands().size(), 1U) << "the panel joins the text's command";

    // Moved and hidden.
    overlay.set_position({.x = 0, .y = 0});
    EXPECT_EQ(overlay.draw(list, engine, fonts.library, fonts.font, lines).left, 0.0F);
    overlay.set_visible(false);
    EXPECT_FALSE(overlay.is_visible());
}

} // namespace
