// The UI renderer (`gfx::ui::UiRenderer`) on every GPU of the machine that runs gfx, with the Vulkan
// validation layers (any message fails the test): shapes, lines, polygons, images and text drawn into a
// target and read back. Shapes are checked by their edges and the area they cover (under rotation and
// scale); text is compared with what ui.frag computes, evaluated on the CPU from the draw list and the atlas
// page, and its stems checked for full interiors and one-pixel edges.
//
// Environment (besides vulkan_test.hpp's):
//   GFX_TEST_PICTURE_DIRECTORY=<directory>  writes every picture read back there, as <test>.<n>.ppm
#include "vulkan_test.hpp"

import std;
import gfx;
import gfx.ui;
import gfx.tests.draw_latin;

namespace
{

using namespace gfx::ui;
using gfx::Vector2;

constexpr std::uint32_t WIDTH = 192;
constexpr std::uint32_t HEIGHT = 96;

constexpr Color RED{.red = 1, .alpha = 1};
constexpr Color GREEN{.green = 1, .alpha = 1};
constexpr Color BLUE{.blue = 1, .alpha = 1};
constexpr Color WHITE{.red = 1, .green = 1, .blue = 1, .alpha = 1};

/// A color, channels 0 to 1.
using Rgba = std::array<double, 4>;

/// Every physical device of the machine that runs gfx.
auto
physical_devices() -> std::vector<std::size_t>
{
    std::vector<std::size_t> devices;
    if (!gfx::backend_availability(gfx::BackendKind::VULKAN).available)
    {
        return devices;
    }
    const std::unique_ptr<gfx::Instance> instance =
        gfx::create_instance(gfx::BackendKind::VULKAN, {.application_name = "gfx UI renderer tests (enumeration)"});
    for (std::size_t index = 0; index < instance->physical_device_count(); ++index)
    {
        if (instance->physical_device(index).missing_core_features().empty())
        {
            devices.push_back(index);
        }
    }
    return devices;
}

/// A target read back: RGBA8 pixels, row by row from the top.
struct Picture
{
    std::vector<std::uint32_t> pixels{};

    /// Channel @p channel (0: red to 3: alpha) of the pixel at (@p x, @p y), 0 to 255.
    [[nodiscard]] auto at(std::uint32_t x,
                          std::uint32_t y,
                          std::uint32_t channel = 0) const -> std::uint32_t
    {
        return (pixels.at(std::size_t{y} * WIDTH + x) >> (8U * channel)) & 0xFFU;
    }

    /// The pixels channel @p channel covers: its sum, 255 being one pixel.
    [[nodiscard]] auto coverage(std::uint32_t channel) const -> double
    {
        double sum = 0;
        for (const std::uint32_t pixel : pixels)
        {
            sum += static_cast<double>((pixel >> (8U * channel)) & 0xFFU);
        }
        return sum / 255.0;
    }
};

/// Writes @p picture into GFX_TEST_PICTURE_DIRECTORY (when set) as a binary PPM named after the running test.
void
write_picture(const Picture& picture)
{
    const char* directory = std::getenv("GFX_TEST_PICTURE_DIRECTORY");
    if (directory == nullptr)
    {
        return;
    }
    static std::uint32_t count = 0;
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
    std::string name = std::format("{}.{}.{}.ppm", test->test_suite_name(), test->name(), count++);
    std::ranges::replace(name, '/', '_');
    std::ofstream file{std::filesystem::path{directory} / name, std::ios::binary};
    file << std::format("P6\n{} {}\n255\n", WIDTH, HEIGHT);
    for (const std::uint32_t pixel : picture.pixels)
    {
        const std::array channels{static_cast<char>(pixel & 0xFFU), static_cast<char>((pixel >> 8U) & 0xFFU), static_cast<char>((pixel >> 16U) & 0xFFU)};
        file.write(channels.data(), channels.size());
    }
}

/// Texel (@p x, @p y) of an RGBA8 atlas page, clamped to the page.
auto
texel(const FontAtlasPage& page,
      std::int64_t x,
      std::int64_t y) -> Rgba
{
    const auto column = static_cast<std::size_t>(std::clamp<std::int64_t>(x, 0, std::int64_t{page.width} - 1));
    const auto row = static_cast<std::size_t>(std::clamp<std::int64_t>(y, 0, std::int64_t{page.height} - 1));
    const std::size_t offset = (row * page.width + column) * 4;
    Rgba value{};
    for (std::size_t channel = 0; channel < 4; ++channel)
    {
        value[channel] = std::to_integer<int>(page.texels[offset + channel]) / 255.0;
    }
    return value;
}

/// The page sampled at @p u, @p v as the renderer's sampler does: linear filtering, clamped to the edge.
auto
sample(const FontAtlasPage& page,
       double u,
       double v) -> Rgba
{
    const double x = u * page.width - 0.5;
    const double y = v * page.height - 0.5;
    const double left = std::floor(x);
    const double top = std::floor(y);
    const double across = x - left;
    const double down = y - top;
    const auto column = static_cast<std::int64_t>(left);
    const auto row = static_cast<std::int64_t>(top);
    const Rgba top_left = texel(page, column, row);
    const Rgba top_right = texel(page, column + 1, row);
    const Rgba bottom_left = texel(page, column, row + 1);
    const Rgba bottom_right = texel(page, column + 1, row + 1);
    Rgba value{};
    for (std::size_t channel = 0; channel < 4; ++channel)
    {
        const double upper = top_left[channel] + (top_right[channel] - top_left[channel]) * across;
        const double lower = bottom_left[channel] + (bottom_right[channel] - bottom_left[channel]) * across;
        value[channel] = upper + (lower - upper) * down;
    }
    return value;
}

/// A Color::packed() word as channels.
auto
unpacked(std::uint32_t word) -> Rgba
{
    return {(word & 0xFFU) / 255.0, ((word >> 8U) & 0xFFU) / 255.0, ((word >> 16U) & 0xFFU) / 255.0, ((word >> 24U) & 0xFFU) / 255.0};
}

/// What ui.frag draws for the glyph quads of @p list (GLYPH and GLYPH_SHADOW vertices, four per quad) sampling
/// @p page, blended in order over opaque black: the reference pictures of text are compared with.
auto
reference_text(const DrawList& list,
               const FontAtlasPage& page) -> std::vector<Rgba>
{
    std::vector<Rgba> picture(std::size_t{WIDTH} * HEIGHT, Rgba{0, 0, 0, 1});
    const std::span<const UiVertex> vertices = list.vertices();
    for (std::size_t first = 0; first + 3 < vertices.size(); first += 4)
    {
        // The quad as a parallelogram: origin + s * across + t * down, s and t from 0 to 1, and how s and t
        // change from pixel to pixel (the inverse of its edges).
        const UiVertex& origin = vertices[first];
        const UiVertex& right = vertices[first + 1];
        const UiVertex& below = vertices[first + 3];
        const double across_x = right.position.x - origin.position.x;
        const double across_y = right.position.y - origin.position.y;
        const double down_x = below.position.x - origin.position.x;
        const double down_y = below.position.y - origin.position.y;
        const double determinant = across_x * down_y - across_y * down_x;
        const double s_per_x = down_y / determinant;
        const double s_per_y = -down_x / determinant;
        const double t_per_x = -across_y / determinant;
        const double t_per_y = across_x / determinant;

        // Texture coordinates along the edges, their screen derivatives, the field's range in pixels.
        const double u_per_s = right.local.x - origin.local.x;
        const double v_per_s = right.local.y - origin.local.y;
        const double u_per_t = below.local.x - origin.local.x;
        const double v_per_t = below.local.y - origin.local.y;
        const double u_width = std::abs(u_per_s * s_per_x + u_per_t * t_per_x) + std::abs(u_per_s * s_per_y + u_per_t * t_per_y);
        const double v_width = std::abs(v_per_s * s_per_x + v_per_t * t_per_x) + std::abs(v_per_s * s_per_y + v_per_t * t_per_y);
        const double range = origin.shape[0];
        const double screen_range = std::max(0.5 * (range / page.width / u_width + range / page.height / v_width), 1.0);
        const auto mode = static_cast<UiShaderMode>(origin.mode & 0xFFU);
        const Rgba color = unpacked(origin.color);
        const Rgba outline_color = unpacked(origin.secondary_color);

        // Every pixel whose center is inside the quad.
        double left = 1.0e9;
        double top = 1.0e9;
        double right_edge = -1.0e9;
        double bottom = -1.0e9;
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
            left = std::min(left, double{vertices[first + corner].position.x});
            top = std::min(top, double{vertices[first + corner].position.y});
            right_edge = std::max(right_edge, double{vertices[first + corner].position.x});
            bottom = std::max(bottom, double{vertices[first + corner].position.y});
        }
        const auto first_column = static_cast<std::uint32_t>(std::clamp(std::floor(left), 0.0, double{WIDTH}));
        const auto end_column = static_cast<std::uint32_t>(std::clamp(std::ceil(right_edge), 0.0, double{WIDTH}));
        const auto first_row = static_cast<std::uint32_t>(std::clamp(std::floor(top), 0.0, double{HEIGHT}));
        const auto end_row = static_cast<std::uint32_t>(std::clamp(std::ceil(bottom), 0.0, double{HEIGHT}));
        for (std::uint32_t y = first_row; y < end_row; ++y)
        {
            for (std::uint32_t x = first_column; x < end_column; ++x)
            {
                const double offset_x = x + 0.5 - origin.position.x;
                const double offset_y = y + 0.5 - origin.position.y;
                const double s = offset_x * s_per_x + offset_y * s_per_y;
                const double t = offset_x * t_per_x + offset_y * t_per_y;
                if (s < 0 || s > 1 || t < 0 || t > 1)
                {
                    continue;
                }

                // The field: its median (fill) and true distance (effects), as ui.frag reads them.
                const Rgba field = sample(page, origin.local.x + s * u_per_s + t * u_per_t, origin.local.y + s * v_per_s + t * v_per_t);
                const double fill = std::max(std::min(field[0], field[1]), std::min(std::max(field[0], field[1]), field[2]));
                const double true_distance = field[3];
                Rgba source{};
                if (mode == UiShaderMode::GLYPH_SHADOW)
                {
                    const double softness = origin.shape[2] + 0.5;
                    const double pixels = screen_range * (true_distance - 0.5);
                    source = {color[0], color[1], color[2], color[3] * std::clamp((pixels + softness) / (2 * softness), 0.0, 1.0)};
                }
                else
                {
                    const double fill_alpha = std::clamp(screen_range * (fill - 0.5) + 0.5, 0.0, 1.0);
                    source = {color[0], color[1], color[2], color[3] * fill_alpha};
                    if (origin.shape[1] > 0)
                    {
                        const double outline_alpha = std::clamp(screen_range * (true_distance - 0.5) + origin.shape[1] + 0.5, 0.0, 1.0);
                        for (std::size_t channel = 0; channel < 3; ++channel)
                        {
                            source[channel] = outline_color[channel] + (color[channel] - outline_color[channel]) * fill_alpha;
                        }
                        source[3] = outline_color[3] * outline_alpha + (color[3] - outline_color[3] * outline_alpha) * fill_alpha;
                    }
                }

                // Blended as the renderer's pipeline does (colors not premultiplied).
                Rgba& target = picture[std::size_t{y} * WIDTH + x];
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    target[channel] = source[channel] * source[3] + target[channel] * (1 - source[3]);
                }
                target[3] = source[3] + target[3] * (1 - source[3]);
            }
        }
    }
    return picture;
}

/// How a picture differs from a reference.
struct Difference
{
    std::uint32_t largest = 0; ///< Largest channel difference, 0 to 255.
    std::size_t different = 0; ///< Pixels with a channel more than 3 apart.
    std::size_t drawn = 0;     ///< Pixels the reference does not leave black.
};

auto
compare(const Picture& picture,
        const std::vector<Rgba>& reference) -> Difference
{
    Difference difference;
    for (std::uint32_t y = 0; y < HEIGHT; ++y)
    {
        for (std::uint32_t x = 0; x < WIDTH; ++x)
        {
            const Rgba& expected = reference[std::size_t{y} * WIDTH + x];
            std::uint32_t largest = 0;
            bool drawn = false;
            for (std::uint32_t channel = 0; channel < 4; ++channel)
            {
                const auto value = static_cast<std::int64_t>(std::lround(expected[channel] * 255));
                largest = std::max(largest, static_cast<std::uint32_t>(std::abs(value - std::int64_t{picture.at(x, y, channel)})));
                drawn = drawn || (channel < 3 && value != 0);
            }
            difference.largest = std::max(difference.largest, largest);
            difference.different += largest > 3 ? 1U : 0U;
            difference.drawn += drawn ? 1U : 0U;
        }
    }
    return difference;
}

/// A UI renderer drawing into a WIDTH x HEIGHT target on the GPU of the test's parameter.
class UiRendererTest : public gfx::tests::VulkanDeviceTest,
                       public testing::WithParamInterface<std::size_t>
{
protected:
    UiRendererTest()
        : m_atlas{m_fonts.add_atlas(FontAtlas::load(gfx::tests::draw_atlases::draw_latin))}
    {
    }

    [[nodiscard]] auto physical_device() -> gfx::PhysicalDevice& override
    {
        return m_instance->physical_device(GetParam());
    }

    void release_device_objects() override
    {
        m_renderer.reset();
    }

    /// The renderer for targets of @p format (made again when the format or the geometry budget changes).
    auto renderer(gfx::Format format = gfx::Format::R8G8B8A8_UNORM,
                  std::uint64_t geometry_bytes_per_frame = std::uint64_t{1} << 20U) -> UiRenderer&
    {
        if (m_renderer == nullptr || m_renderer_format != format || m_geometry_bytes_per_frame != geometry_bytes_per_frame)
        {
            m_renderer.reset();
            m_renderer = std::make_unique<UiRenderer>(*m_device, m_fonts,
                                                      UiRendererDescription{.color_format = format,
                                                                            .geometry_bytes_per_frame = geometry_bytes_per_frame,
                                                                            .debug_name = "test ui"});
            m_renderer_format = format;
            m_geometry_bytes_per_frame = geometry_bytes_per_frame;
        }
        return *m_renderer;
    }

    /// Draws @p lists with renderer(@p format) into a target cleared to opaque black, and reads it back.
    auto render(std::span<const DrawList* const> lists,
                gfx::Format format = gfx::Format::R8G8B8A8_UNORM) -> Picture
    {
        UiRenderer& ui = renderer(format, m_geometry_bytes_per_frame);
        const std::unique_ptr<gfx::Image> target = m_device->create_image({.format = format,
                                                                           .extent = {.width = WIDTH, .height = HEIGHT},
                                                                           .usage = gfx::ImageUsage::COLOR_ATTACHMENT | gfx::ImageUsage::TRANSFER_SOURCE,
                                                                           .debug_name = "ui target"});
        const std::unique_ptr<gfx::ImageView> view = m_device->create_image_view({.image = target->handle(), .debug_name = "ui target view"});
        const UiDrawData data{.lists = lists, .width = static_cast<float>(WIDTH), .height = static_cast<float>(HEIGHT)};

        // The frame: pages uploaded before the pass, the lists drawn in it.
        ui.begin_frame(++m_frame);
        run([&](gfx::GraphicsRecorder& recorder)
        {
            const std::array to_target{gfx::image_barrier(target->handle(), gfx::ResourceState::UNDEFINED, gfx::ResourceState::COLOR_ATTACHMENT_WRITE)};
            recorder.barrier({.image_barriers = to_target});
            ui.prepare(recorder, data);
            const std::array attachments{gfx::ColorAttachment{.view = view->handle(), .clear_color = {0.0F, 0.0F, 0.0F, 1.0F}}};
            gfx::RenderingScope scope = recorder.begin_rendering({.area = {.width = WIDTH, .height = HEIGHT}, .color_attachments = attachments});
            ui.record(scope, data);
        });
        Picture picture{.pixels = read_image(*target, gfx::ResourceState::COLOR_ATTACHMENT_WRITE)};
        write_picture(picture);
        return picture;
    }

    /// Draws @p list (see the overload of lists).
    auto render(const DrawList& list,
                gfx::Format format = gfx::Format::R8G8B8A8_UNORM) -> Picture
    {
        const std::array lists{&list};
        return render(lists, format);
    }

    /// A font of the test atlas, @p size pixels per em.
    auto font(float size) -> FontHandle
    {
        return m_fonts.add_font({.atlas = m_atlas, .size = size});
    }

    /// The test atlas's page 0.
    [[nodiscard]] auto page() const -> FontAtlasPage
    {
        return m_fonts.atlas(m_atlas)->page(0);
    }

    FontLibrary m_fonts;
    FontAtlasHandle m_atlas{};
    std::unique_ptr<UiRenderer> m_renderer{};
    gfx::Format m_renderer_format = gfx::Format::R8G8B8A8_UNORM;
    std::uint64_t m_geometry_bytes_per_frame = std::uint64_t{1} << 20U;
    std::uint64_t m_frame = 0;
};

TEST_P(UiRendererTest, RectanglesHaveSharpEdgesOnPixelBoundariesAndHalfCoveredPixelsBetween)
{
    // Red on integer edges: exactly its 20 x 10 pixels. Green from x 40.5 to 60.5: its edge pixels half covered.
    DrawList list;
    list.add_rectangle_filled({.left = 10, .top = 10, .right = 30, .bottom = 20}, RED);
    list.add_rectangle_filled({.left = 40.5F, .top = 10, .right = 60.5F, .bottom = 20}, GREEN);
    const Picture picture = render(list);
    EXPECT_EQ(picture.coverage(0), 200.0);
    for (std::uint32_t y = 8; y < 22; ++y)
    {
        for (std::uint32_t x = 8; x < 32; ++x)
        {
            const bool inside = x >= 10 && x < 30 && y >= 10 && y < 20;
            ASSERT_EQ(picture.at(x, y), inside ? 255U : 0U) << x << ", " << y;
            ASSERT_EQ(picture.at(x, y, 3), 255U) << "opaque target alpha stays opaque";
        }
    }
    EXPECT_NEAR(picture.at(40, 15, 1), 128.0, 2.0);
    EXPECT_NEAR(picture.at(60, 15, 1), 128.0, 2.0);
    EXPECT_EQ(picture.at(41, 15, 1), 255U);
    EXPECT_EQ(picture.at(59, 15, 1), 255U);
    EXPECT_EQ(picture.at(39, 15, 1), 0U);
    EXPECT_EQ(picture.at(61, 15, 1), 0U);
    EXPECT_EQ(m_renderer->statistics().draw_calls, 1U);
}

TEST_P(UiRendererTest, RoundedShapesCoverTheirAreaSymmetrically)
{
    // A red circle of radius 12, a green rectangle with corners of radius 10: their exact areas, within 1%.
    DrawList list;
    list.add_circle_filled({.x = 32, .y = 32}, 12, RED);
    list.add_rectangle_filled({.left = 60, .top = 8, .right = 120, .bottom = 48}, GREEN, 10);
    const Picture picture = render(list);
    const double circle = std::numbers::pi * 12 * 12;
    const double rounded = 60.0 * 40.0 - (4 - std::numbers::pi) * 10 * 10;
    EXPECT_NEAR(picture.coverage(0), circle, circle * 0.01);
    EXPECT_NEAR(picture.coverage(1), rounded, rounded * 0.01);

    // Anti-aliased: partly covered pixels around the circle, mirrored about its center (within what finite
    // differences of a curved distance change).
    std::size_t partial = 0;
    for (std::uint32_t y = 16; y < 48; ++y)
    {
        for (std::uint32_t x = 16; x < 32; ++x)
        {
            const std::uint32_t value = picture.at(x, y);
            EXPECT_NEAR(value, picture.at(63 - x, y), 8.0) << x << ", " << y;
            EXPECT_NEAR(value, picture.at(x, 63 - y), 8.0) << x << ", " << y;
            partial += value > 0 && value < 255 ? 1U : 0U;
        }
    }
    EXPECT_GT(partial, 20U);
    EXPECT_LT(partial, 60U) << "an edge about a pixel wide";
    EXPECT_EQ(picture.at(32, 32), 255U);
    EXPECT_EQ(picture.at(60, 8, 1), 0U) << "the rounded corner is not drawn";
    EXPECT_EQ(picture.at(90, 8, 1), 255U);
}

TEST_P(UiRendererTest, BordersAndRingsLeaveTheirInsideUntouched)
{
    // A 3-pixel border inside the rectangle's edge, a 2-pixel ring inside the circle's radius.
    DrawList list;
    list.add_rectangle({.left = 10, .top = 10, .right = 50, .bottom = 40}, GREEN, 3);
    list.add_circle({.x = 80, .y = 30}, 15, RED, 2);
    const Picture picture = render(list);
    EXPECT_EQ(picture.at(10, 25, 1), 255U);
    EXPECT_EQ(picture.at(12, 25, 1), 255U);
    EXPECT_EQ(picture.at(13, 25, 1), 0U);
    EXPECT_EQ(picture.at(30, 25, 1), 0U);
    EXPECT_EQ(picture.at(9, 25, 1), 0U);
    EXPECT_NEAR(picture.coverage(1), 40.0 * 30.0 - 34.0 * 24.0, 2.0);
    EXPECT_EQ(picture.at(93, 30), 255U);
    EXPECT_EQ(picture.at(91, 30), 0U);
    EXPECT_EQ(picture.at(80, 30), 0U);
    const double ring = std::numbers::pi * (15.0 * 15.0 - 13.0 * 13.0);
    EXPECT_NEAR(picture.coverage(0), ring, ring * 0.02);
}

TEST_P(UiRendererTest, LinesAndRotatedShapesKeepTheirArea)
{
    // A diagonal line 4 pixels thick; a 20 x 10 rectangle turned by 30 degrees about its center.
    DrawList list;
    list.add_line({.x = 10, .y = 10}, {.x = 50, .y = 50}, RED, 4);
    list.push_transform(Transform2D::about({.x = 90, .y = 40}, Transform2D::rotation(std::numbers::pi_v<float> / 6)));
    list.add_rectangle_filled({.left = 80, .top = 35, .right = 100, .bottom = 45}, GREEN);
    list.pop_transform();
    const Picture picture = render(list);
    // Coverage sampled at pixel centers along a 45-degree edge is a few percent off the exact area,
    // depending on where the edge falls (2.1% here, the same when evaluated on the CPU).
    const double line = std::hypot(40.0, 40.0) * 4;
    EXPECT_NEAR(picture.coverage(0), line, line * 0.03);
    EXPECT_NEAR(picture.coverage(1), 200.0, 4.0);
    EXPECT_EQ(picture.at(30, 30), 255U);
    EXPECT_EQ(picture.at(40, 20), 0U);
    EXPECT_EQ(picture.at(90, 40, 1), 255U);
    EXPECT_LT(picture.at(99, 44, 1), 16U) << "a corner of the rectangle before it turned";
    EXPECT_GT(picture.at(94, 47, 1), 200U) << "inside the turned rectangle, below the corner it had";
}

TEST_P(UiRendererTest, PolygonsAndPolylinesCoverTheirArea)
{
    // A blue triangle (750 pixels); a red closed square path 2 pixels thick (an 80 x 2 ring).
    DrawList list;
    const std::array triangle{Vector2{.x = 10, .y = 60}, Vector2{.x = 60, .y = 60}, Vector2{.x = 35, .y = 90}};
    list.add_convex_polygon_filled(triangle, BLUE);
    const std::array square{Vector2{.x = 100, .y = 20}, Vector2{.x = 120, .y = 20}, Vector2{.x = 120, .y = 40}, Vector2{.x = 100, .y = 40}};
    list.add_polyline(square, RED, 2, true);
    const Picture picture = render(list);
    EXPECT_NEAR(picture.coverage(2), 750.0, 750.0 * 0.02);
    EXPECT_NEAR(picture.coverage(0), 160.0, 160.0 * 0.03);
    EXPECT_EQ(picture.at(35, 70, 2), 255U);
    EXPECT_EQ(picture.at(110, 30), 0U) << "inside the path";
    EXPECT_GT(picture.at(110, 20), 120U) << "on the path";
}

TEST_P(UiRendererTest, ScaledShapesKeepEdgesOnePixelWide)
{
    // Scaled by 4: 8 to 32 by 8 to 24 pixels, exactly; an edge at 8.5 pixels covers half its pixel, not 4.
    DrawList list;
    list.push_transform(Transform2D::scaling(4, 4));
    list.add_rectangle_filled({.left = 2, .top = 2, .right = 8, .bottom = 6}, RED);
    list.add_rectangle_filled({.left = 2.125F, .top = 8, .right = 6, .bottom = 12}, GREEN);
    const Picture picture = render(list);
    EXPECT_EQ(picture.coverage(0), 24.0 * 16.0);
    EXPECT_EQ(picture.at(8, 8), 255U);
    EXPECT_EQ(picture.at(31, 23), 255U);
    EXPECT_EQ(picture.at(32, 16), 0U);
    EXPECT_EQ(picture.at(7, 16), 0U);
    EXPECT_EQ(picture.at(7, 40, 1), 0U);
    EXPECT_NEAR(picture.at(8, 40, 1), 128.0, 2.0);
    EXPECT_EQ(picture.at(9, 40, 1), 255U);
}

TEST_P(UiRendererTest, ClipRectanglesCutPrimitives)
{
    // Clipped to the left 20 columns; then a clip larger than the target (clamped by the renderer).
    DrawList list;
    list.push_clip_rectangle({.left = 0, .top = 0, .right = 20, .bottom = 40});
    list.add_rectangle_filled({.left = 0, .top = 0, .right = 100, .bottom = 40}, RED);
    list.pop_clip_rectangle();
    list.push_clip_rectangle({.left = -100, .top = 50, .right = 1000, .bottom = 1000});
    list.add_rectangle_filled({.left = 0, .top = 0, .right = 100, .bottom = 60}, GREEN);
    list.pop_clip_rectangle();
    const Picture picture = render(list);
    EXPECT_EQ(picture.at(19, 10), 255U);
    EXPECT_EQ(picture.at(20, 10), 0U);
    EXPECT_EQ(picture.coverage(0), 20.0 * 40.0);
    EXPECT_EQ(picture.at(50, 45, 1), 0U) << "above the second clip";
    EXPECT_EQ(picture.at(50, 55, 1), 255U);
    EXPECT_EQ(picture.coverage(1), 100.0 * 10.0);
    EXPECT_EQ(m_renderer->statistics().draw_calls, 2U);
}

TEST_P(UiRendererTest, ListsAreDrawnInOrder)
{
    // The second list's indices start at its own vertices (vertex offset); it draws over the first.
    DrawList first;
    first.add_rectangle_filled({.left = 10, .top = 10, .right = 40, .bottom = 40}, RED);
    DrawList second;
    second.add_circle_filled({.x = 60, .y = 20}, 6, BLUE);
    second.add_rectangle_filled({.left = 30, .top = 30, .right = 60, .bottom = 60}, GREEN);
    const std::array lists{&first, &second};
    const Picture picture = render(lists);
    EXPECT_EQ(picture.at(15, 15), 255U);
    EXPECT_EQ(picture.at(35, 35, 1), 255U);
    EXPECT_EQ(picture.at(35, 35), 0U) << "green over red";
    EXPECT_EQ(picture.at(60, 20, 2), 255U);
    EXPECT_EQ(m_renderer->statistics().draw_calls, 2U);
    EXPECT_EQ(m_renderer->statistics().vertices, 12U);
}

TEST_P(UiRendererTest, ImagesAreSampledThroughTheirBindlessIndex)
{
    // A 2 x 2 texture (red, green / blue, white) stretched over 32 x 32 pixels, its corners exact; tinted.
    const std::unique_ptr<gfx::Image> texture = m_device->create_image({.format = gfx::Format::R8G8B8A8_UNORM,
                                                                        .extent = {.width = 2, .height = 2},
                                                                        .usage = gfx::ImageUsage::SAMPLED | gfx::ImageUsage::TRANSFER_DESTINATION,
                                                                        .debug_name = "ui texture"});
    const std::unique_ptr<gfx::ImageView> texture_view = m_device->create_image_view({.image = texture->handle(), .debug_name = "ui texture view"});
    const std::unique_ptr<gfx::Buffer> staging = m_device->create_buffer(
        {.size = 16, .usage = gfx::BufferUsage::TRANSFER_SOURCE, .location = gfx::MemoryLocation::HOST_UPLOAD, .debug_name = "ui texture staging"});
    const std::array texels{gfx::tests::rgba(255, 0, 0, 255), gfx::tests::rgba(0, 255, 0, 255), gfx::tests::rgba(0, 0, 255, 255),
                            gfx::tests::rgba(255, 255, 255, 255)};
    std::ranges::copy(texels, staging->mapped_elements<std::uint32_t>().begin());
    run([&](gfx::GraphicsRecorder& recorder)
    {
        const std::array to_copy{gfx::image_barrier(texture->handle(), gfx::ResourceState::UNDEFINED, gfx::ResourceState::TRANSFER_DESTINATION)};
        recorder.barrier({.image_barriers = to_copy});
        const std::array regions{gfx::BufferImageCopyRegion{.image_extent = {.width = 2, .height = 2}}};
        recorder.copy_buffer_to_image(staging->handle(), texture->handle(), gfx::ImageLayout::TRANSFER_DESTINATION, regions);
        const std::array to_sampling{
            gfx::image_barrier(texture->handle(), gfx::ResourceState::TRANSFER_DESTINATION, gfx::ResourceState::SHADER_SAMPLED_READ)};
        recorder.barrier({.image_barriers = to_sampling});
    });

    DrawList list;
    list.add_image(texture_view->sampled_index().value(), {.left = 0, .top = 0, .right = 32, .bottom = 32});
    list.add_image(texture_view->sampled_index().value(), {.left = 40, .top = 0, .right = 72, .bottom = 32}, Color{.red = 0.5F, .green = 0.5F, .blue = 0.5F, .alpha = 1});
    const Picture picture = render(list);
    EXPECT_EQ(picture.pixels.at(2 * WIDTH + 2), gfx::tests::rgba(255, 0, 0, 255));
    EXPECT_EQ(picture.pixels.at(2 * WIDTH + 29), gfx::tests::rgba(0, 255, 0, 255));
    EXPECT_EQ(picture.pixels.at(29 * WIDTH + 2), gfx::tests::rgba(0, 0, 255, 255));
    EXPECT_EQ(picture.pixels.at(29 * WIDTH + 29), gfx::tests::rgba(255, 255, 255, 255));
    EXPECT_NEAR(picture.at(69, 29), 128.0, 1.0) << "white tinted by half";
    EXPECT_GT(picture.at(16, 16, 1), 0U) << "filtered between the texels";
}

TEST_P(UiRendererTest, RoundedImagesLeaveTheirCornersOutAndHaveAnAntiAliasedEdge)
{
    // A white texture over 32 x 32 pixels, its corners rounded by 10: the corners stay black, the straight edges are
    // sharp on their pixel boundaries, the arcs half cover the pixels they cross.
    const std::unique_ptr<gfx::Image> texture = m_device->create_image({.format = gfx::Format::R8G8B8A8_UNORM,
                                                                        .extent = {.width = 2, .height = 2},
                                                                        .usage = gfx::ImageUsage::SAMPLED | gfx::ImageUsage::TRANSFER_DESTINATION,
                                                                        .debug_name = "ui rounded texture"});
    const std::unique_ptr<gfx::ImageView> texture_view = m_device->create_image_view({.image = texture->handle(), .debug_name = "ui rounded texture view"});
    const std::unique_ptr<gfx::Buffer> staging = m_device->create_buffer(
        {.size = 16, .usage = gfx::BufferUsage::TRANSFER_SOURCE, .location = gfx::MemoryLocation::HOST_UPLOAD, .debug_name = "ui rounded texture staging"});
    std::ranges::fill(staging->mapped_elements<std::uint32_t>(), gfx::tests::rgba(255, 255, 255, 255));
    run([&](gfx::GraphicsRecorder& recorder)
    {
        const std::array to_copy{gfx::image_barrier(texture->handle(), gfx::ResourceState::UNDEFINED, gfx::ResourceState::TRANSFER_DESTINATION)};
        recorder.barrier({.image_barriers = to_copy});
        const std::array regions{gfx::BufferImageCopyRegion{.image_extent = {.width = 2, .height = 2}}};
        recorder.copy_buffer_to_image(staging->handle(), texture->handle(), gfx::ImageLayout::TRANSFER_DESTINATION, regions);
        const std::array to_sampling{
            gfx::image_barrier(texture->handle(), gfx::ResourceState::TRANSFER_DESTINATION, gfx::ResourceState::SHADER_SAMPLED_READ)};
        recorder.barrier({.image_barriers = to_sampling});
    });

    DrawList list;
    list.add_image_rounded(texture_view->sampled_index().value(), {.left = 8, .top = 8, .right = 40, .bottom = 40}, 10.0F);
    const Picture picture = render(list);
    EXPECT_EQ(picture.at(24, 24), 255U) << "inside";
    EXPECT_EQ(picture.at(8, 8), 0U) << "the corner is cut";
    EXPECT_EQ(picture.at(39, 39), 0U);
    EXPECT_GE(picture.at(8, 24), 250U) << "the straight edge covers its pixels";
    EXPECT_LE(picture.at(7, 24), 5U);
    EXPECT_GE(picture.at(11, 11), 200U) << "inside the arc";
    EXPECT_LE(picture.at(10, 10), 128U) << "on the arc";
}

TEST_P(UiRendererTest, SrgbTargetsReadBackTheColorsAsGiven)
{
    // Colors are sRGB-encoded: an sRGB target converts them to linear and back, an UNORM one stores them.
    const Color color{.red = 0.5F, .green = 0.25F, .blue = 0.75F, .alpha = 1};
    DrawList list;
    list.add_rectangle_filled({.left = 10, .top = 10, .right = 40, .bottom = 40}, color);
    const Picture unorm = render(list, gfx::Format::R8G8B8A8_UNORM);
    const Picture srgb = render(list, gfx::Format::R8G8B8A8_SRGB);
    for (std::uint32_t channel = 0; channel < 3; ++channel)
    {
        EXPECT_EQ(unorm.at(20, 20, channel), (color.packed() >> (8U * channel)) & 0xFFU);
        EXPECT_NEAR(srgb.at(20, 20, channel), unorm.at(20, 20, channel), 1.0) << channel;
    }
}

TEST_P(UiRendererTest, TextMatchesTheFieldEvaluatedOnTheCpu)
{
    // Text at 40 pixels per em; the GPU's picture and the reference within a few levels per channel.
    TextLayoutEngine engine{m_fonts};
    const TextLayout layout = engine.layout("Text 42&g", font(40));
    DrawList list;
    list.add_text(m_fonts, layout, {.x = 8, .y = 20});
    const Picture picture = render(list);
    const Difference difference = compare(picture, reference_text(list, page()));
    EXPECT_GT(difference.drawn, 1000U);
    EXPECT_LE(difference.largest, 12U);
    EXPECT_LE(difference.different, difference.drawn / 100) << "pixels more than 3 levels apart";
    EXPECT_EQ(m_renderer->statistics().resident_pages, 1U);
}

TEST_P(UiRendererTest, ScaledAndRotatedTextMatchesTheFieldEvaluatedOnTheCpu)
{
    // Text laid out at 16 pixels drawn 2.5 times larger, then turned by 20 degrees: the field's range in
    // pixels follows the transform (ui.frag measures it with derivatives).
    TextLayoutEngine engine{m_fonts};
    const TextLayout layout = engine.layout("Scale", font(16));
    DrawList scaled;
    scaled.push_transform(Transform2D::translation({.x = 10, .y = 10}) * Transform2D::scaling(2.5F, 2.5F));
    scaled.add_text(m_fonts, layout, {.x = 0, .y = 0});
    const Picture scaled_picture = render(scaled);
    const Difference scaled_difference = compare(scaled_picture, reference_text(scaled, page()));
    EXPECT_GT(scaled_difference.drawn, 1000U);
    EXPECT_LE(scaled_difference.largest, 12U);
    EXPECT_LE(scaled_difference.different, scaled_difference.drawn / 100);

    DrawList rotated;
    rotated.push_transform(Transform2D::about({.x = 96, .y = 48}, Transform2D::rotation(std::numbers::pi_v<float> / 9)) *
                           Transform2D::translation({.x = 50, .y = 25}) * Transform2D::scaling(2, 2));
    rotated.add_text(m_fonts, layout, {.x = 0, .y = 0});
    const Picture rotated_picture = render(rotated);
    const Difference rotated_difference = compare(rotated_picture, reference_text(rotated, page()));
    EXPECT_GT(rotated_difference.drawn, 400U);
    EXPECT_LE(rotated_difference.largest, 12U);
    EXPECT_LE(rotated_difference.different, rotated_difference.drawn / 100);
}

TEST_P(UiRendererTest, OutlinesAndShadowsMatchTheFieldEvaluatedOnTheCpu)
{
    // White text, a red outline 1.5 pixels wide, a soft blue shadow 4 pixels down and right.
    TextLayoutEngine engine{m_fonts};
    const TextLayout layout = engine.layout("Glow", font(44));
    DrawList list;
    const TextStyle style{.color = WHITE,
                          .outline_color = RED,
                          .outline_width = 1.5F,
                          .shadow_color = BLUE,
                          .shadow_offset = {.x = 4, .y = 4},
                          .shadow_softness = 2};
    list.add_text(m_fonts, layout, {.x = 10, .y = 10}, style);
    const Picture picture = render(list);
    const Difference difference = compare(picture, reference_text(list, page()));
    EXPECT_LE(difference.largest, 12U);
    EXPECT_LE(difference.different, difference.drawn / 100);

    // Each effect is there: pure outline pixels (red, no green), shadow pixels (blue only).
    std::size_t outline = 0;
    std::size_t shadow = 0;
    for (std::uint32_t y = 0; y < HEIGHT; ++y)
    {
        for (std::uint32_t x = 0; x < WIDTH; ++x)
        {
            outline += picture.at(x, y, 0) > 200 && picture.at(x, y, 1) < 40 ? 1U : 0U;
            shadow += picture.at(x, y, 2) > 100 && picture.at(x, y, 0) < 20 ? 1U : 0U;
        }
    }
    EXPECT_GT(outline, 100U);
    EXPECT_GT(shadow, 50U);
}

TEST_P(UiRendererTest, GlyphStemsAreFilledWithEdgesOnePixelWide)
{
    // "I" at 64 pixels: a vertical stem. Across it, at mid-height: black, at most two partly covered pixels,
    // full coverage, at most two partly covered pixels, black. Its ink stands on the baseline.
    TextLayoutEngine engine{m_fonts};
    const TextLayout layout = engine.layout("I", font(64));
    ASSERT_EQ(layout.glyphs.size(), 1U);
    DrawList list;
    list.add_text(m_fonts, layout, {.x = 20, .y = 10});
    const Picture picture = render(list);
    std::uint32_t top = HEIGHT;
    std::uint32_t bottom = 0;
    for (std::uint32_t y = 0; y < HEIGHT; ++y)
    {
        for (std::uint32_t x = 0; x < WIDTH; ++x)
        {
            if (picture.at(x, y) > 127)
            {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }
    ASSERT_LT(top, bottom);
    const float baseline = 10 + layout.glyphs[0].y;
    EXPECT_NEAR(static_cast<float>(bottom) + 1, baseline, 1.0F) << "the stem ends on the baseline";
    EXPECT_GT(bottom - top, 40U) << "the height of a capital at 64 pixels";

    const std::uint32_t row = (top + bottom) / 2;
    std::vector<std::uint32_t> profile;
    for (std::uint32_t x = 0; x < WIDTH; ++x)
    {
        profile.push_back(picture.at(x, row));
    }
    std::uint32_t first_ink = WIDTH;
    std::uint32_t last_ink = 0;
    for (std::uint32_t x = 0; x < WIDTH; ++x)
    {
        if (profile[x] > 0)
        {
            first_ink = std::min(first_ink, x);
            last_ink = x;
        }
    }
    ASSERT_LT(first_ink, last_ink);
    std::uint32_t full = 0;
    std::uint32_t partial = 0;
    for (std::uint32_t x = first_ink; x <= last_ink; ++x)
    {
        full += profile[x] == 255 ? 1U : 0U;
        partial += profile[x] > 0 && profile[x] < 255 ? 1U : 0U;
    }
    EXPECT_GE(full, 4U) << "a stem several pixels wide";
    EXPECT_LE(partial, 4U) << "two edges, at most two pixels each";
    EXPECT_EQ(full + partial, last_ink - first_ink + 1) << "one stem: no gap";
}

TEST_P(UiRendererTest, TheDebugOverlayDrawsItsLinesOverATranslucentPanel)
{
    // The frame metrics' overlay at the windowed example's size (15 pixels), over a white rectangle: the
    // panel darkens it to a quarter, the lines are drawn in white inside the panel's padding.
    TextLayoutEngine engine{m_fonts};
    DebugOverlay overlay{{.visible = true, .position = {.x = 4, .y = 4}}};
    const std::vector<std::string> lines{"59.9 fps   16.69 ms   min 16.12   max 17.30", "gpu frame: 0.412 ms",
                                         "draws 3   indirect draws 0   dispatches 0"};
    DrawList list;
    list.add_rectangle_filled({.left = 0, .top = 0, .right = static_cast<float>(WIDTH), .bottom = static_cast<float>(HEIGHT)}, WHITE);
    const UiRectangle panel = overlay.draw(list, engine, m_fonts, font(15), lines);
    const Picture picture = render(list);
    ASSERT_FALSE(panel.empty());
    EXPECT_GT(panel.bottom, 50.0F) << "three lines";
    EXPECT_NEAR(picture.at(5, static_cast<std::uint32_t>(panel.bottom) - 4), 64.0, 2.0) << "the panel, without text";
    EXPECT_EQ(picture.at(5, static_cast<std::uint32_t>(panel.bottom) + 2), 255U) << "below the panel";

    // Text inside the panel: bright pixels, some fully white (stems), none in the padding's corner.
    std::size_t bright = 0;
    std::size_t white = 0;
    for (auto y = static_cast<std::uint32_t>(panel.top) + 7; y < static_cast<std::uint32_t>(panel.bottom) - 6; ++y)
    {
        for (auto x = static_cast<std::uint32_t>(panel.left) + 7; x < std::min(static_cast<std::uint32_t>(panel.right) - 6, WIDTH); ++x)
        {
            bright += picture.at(x, y) > 160 ? 1U : 0U;
            white += picture.at(x, y) == 255 ? 1U : 0U;
        }
    }
    EXPECT_GT(bright, 300U);
    EXPECT_GT(white, 30U);
    EXPECT_NEAR(picture.at(static_cast<std::uint32_t>(panel.left) + 5, static_cast<std::uint32_t>(panel.top) + 3), 64.0, 2.0);
}

TEST_P(UiRendererTest, PagesAreUploadedOnceAndFramesReuseTheirGeometry)
{
    // Three frames (two in flight): the page is uploaded in the first, the geometry regions alternate.
    TextLayoutEngine engine{m_fonts};
    const TextLayout layout = engine.layout("Frames", font(30));
    std::uint64_t uploaded = 0;
    for (std::uint32_t frame = 0; frame < 3; ++frame)
    {
        DrawList list;
        list.add_rectangle_filled({.left = 0, .top = 60, .right = 20 + 10.0F * static_cast<float>(frame), .bottom = 70}, GREEN);
        list.add_text(m_fonts, layout, {.x = 4, .y = 4});
        const Picture picture = render(list);
        EXPECT_EQ(picture.at(19 + 10 * frame, 65, 1), 255U) << frame;
        EXPECT_EQ(picture.at(20 + 10 * frame, 65, 1), 0U) << frame;
        const UiRendererStatistics& statistics = m_renderer->statistics();
        EXPECT_EQ(statistics.resident_pages, 1U);
        EXPECT_EQ(statistics.draw_calls, 1U) << "the shape joins the text's command";
        if (frame == 0)
        {
            uploaded = statistics.uploaded_bytes;
            EXPECT_EQ(uploaded, page().texels.size());
        }
        EXPECT_EQ(statistics.uploaded_bytes, uploaded) << "no upload after the first frame";
    }
}

TEST_P(UiRendererTest, GeometryBeyondTheFrameBudgetIsSkipped)
{
    // 4 KiB per frame: 200 rectangles (38 400 bytes of vertices) do not fit, so nothing is drawn.
    m_geometry_bytes_per_frame = 4096;
    DrawList list;
    for (std::uint32_t index = 0; index < 200; ++index)
    {
        list.add_rectangle_filled({.left = 0, .top = 0, .right = 10, .bottom = 10}, RED);
    }
    const Picture picture = render(list);
    EXPECT_EQ(picture.coverage(0), 0.0);
    EXPECT_EQ(m_renderer->statistics().draw_calls, 0U);
    EXPECT_EQ(m_renderer->statistics().skipped_commands, 1U);
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         UiRendererTest,
                         testing::ValuesIn(physical_devices()),
                         [](const testing::TestParamInfo<std::size_t>& parameter) { return std::format("Device{}", parameter.param); });
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(UiRendererTest);

} // namespace
