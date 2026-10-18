// The immediate-mode UI drawn by the renderer on every GPU of the machine (validation layers on): a demo window
// with every widget of phase U4, read back at one and at one and a half pixels per unit.
//
// Environment (besides vulkan_test.hpp's):
//   GFX_TEST_PICTURE_DIRECTORY=<directory>  writes each picture read back there, as <test>.<n>.ppm
#include "vulkan_test.hpp"

import std;
import gfx;
import gfx.ui;
import gfx.tests.ui_context_support;

namespace
{

using namespace gfx::ui;
using gfx::Vector2;
using gfx::tests::UiHarness;

/// Every physical device of the machine that runs gfx.
auto
physical_devices() -> std::vector<std::size_t>
{
    std::vector<std::size_t> devices;
    if (!gfx::backend_availability(gfx::BackendKind::VULKAN).available)
    {
        return devices;
    }
    const std::unique_ptr<gfx::Instance> instance = gfx::create_instance(gfx::BackendKind::VULKAN, {.application_name = "gfx UI context tests (enumeration)"});
    for (std::size_t index = 0; index < instance->physical_device_count(); ++index)
    {
        if (instance->physical_device(index).missing_core_features().empty())
        {
            devices.push_back(index);
        }
    }
    return devices;
}

/// A picture read back.
struct Picture
{
    gfx::Extent extent{};
    std::vector<std::uint32_t> pixels{};

    /// The pixel at (@p x, @p y), RGBA8 in a word (red in the low byte).
    [[nodiscard]] auto at(float x,
                          float y) const -> std::uint32_t
    {
        return pixels.at(static_cast<std::size_t>(y) * extent.width + static_cast<std::size_t>(x));
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
    file << std::format("P6\n{} {}\n255\n", picture.extent.width, picture.extent.height);
    for (const std::uint32_t pixel : picture.pixels)
    {
        const std::array channels{static_cast<char>(pixel & 0xFFU), static_cast<char>((pixel >> 8U) & 0xFFU), static_cast<char>((pixel >> 16U) & 0xFFU)};
        file.write(channels.data(), channels.size());
    }
}

/// A demo window: every widget of phase U4.
void
demo(UiFrame& frame)
{
    frame.set_next_window_position({.x = 16, .y = 16}, UiCondition::FIRST_USE_EVER);
    frame.set_next_window_size({.x = 300, .y = 260}, UiCondition::FIRST_USE_EVER);
    if (frame.begin_window("Inspector"))
    {
        static bool visible = true;
        static bool switched = true;
        static int quality = 1;
        frame.text("{} objects selected", 3);
        frame.checkbox("Visible", visible);
        frame.same_line();
        frame.toggle("Shadows", switched);
        frame.radio_button("Low", quality, 0);
        frame.same_line();
        frame.radio_button("High", quality, 1);
        frame.separator_text("Actions");
        frame.button("Apply");
        frame.same_line();
        frame.small_button("Reset");
        frame.same_line();
        static_cast<void>(frame.arrow_button("next", ArrowDirection::RIGHT));
        frame.selectable("Selected line", true);
        frame.bullet_text("Bullet {}", 1);
        frame.label("Speed", "{:.1f} m/s", 12.5);
        if (frame.begin_child("log", {.x = 0, .y = 50}, ChildFlag::BORDERS))
        {
            for (int line = 0; line < 8; ++line)
            {
                frame.text_disabled("log line {}", line);
            }
        }
        frame.end_child();
    }
    frame.end_window();
}

class UiContextRenderTest : public gfx::tests::VulkanDeviceTest,
                            public testing::WithParamInterface<std::size_t>
{
protected:
    [[nodiscard]] auto physical_device() -> gfx::PhysicalDevice& override
    {
        return m_instance->physical_device(GetParam());
    }

    /// Draws @p ui's published draw data into a target of its size, reads it back.
    auto render(UiContext& ui,
                UiRenderer& renderer) -> Picture
    {
        const UiDrawData& data = ui.draw_data();
        const gfx::Extent extent{.width = static_cast<std::uint32_t>(data.width), .height = static_cast<std::uint32_t>(data.height)};
        const std::unique_ptr<gfx::Image> target = m_device->create_image({.format = gfx::Format::R8G8B8A8_UNORM,
                                                                           .extent = extent,
                                                                           .usage = gfx::ImageUsage::COLOR_ATTACHMENT | gfx::ImageUsage::TRANSFER_SOURCE,
                                                                           .debug_name = "ui context target"});
        const std::unique_ptr<gfx::ImageView> view = m_device->create_image_view({.image = target->handle()});
        renderer.begin_frame(++m_frame);
        run([&](gfx::GraphicsRecorder& recorder)
        {
            const std::array to_target{gfx::image_barrier(target->handle(), gfx::ResourceState::UNDEFINED, gfx::ResourceState::COLOR_ATTACHMENT_WRITE)};
            recorder.barrier({.image_barriers = to_target});
            renderer.prepare(recorder, data);
            const std::array attachments{gfx::ColorAttachment{.view = view->handle(), .clear_color = {0.2F, 0.2F, 0.2F, 1.0F}}};
            gfx::RenderingScope scope = recorder.begin_rendering({.area = {.width = extent.width, .height = extent.height}, .color_attachments = attachments});
            renderer.record(scope, data);
        });
        Picture picture{.extent = extent, .pixels = read_image(*target, gfx::ResourceState::COLOR_ATTACHMENT_WRITE)};
        write_picture(picture);
        return picture;
    }

    std::uint64_t m_frame = 0;
};

TEST_P(UiContextRenderTest, TheDemoWindowIsDrawnAtEveryScale)
{
    for (const float scale : {1.0F, 1.5F})
    {
        UiHarness ui;
        ui.scale(scale);
        UiRenderer renderer{*m_device, ui.context().fonts(), {.color_format = gfx::Format::R8G8B8A8_UNORM}};
        const UiFrameReport report = ui.frames(3, demo);
        EXPECT_TRUE(report.ok()) << (report.issues.empty() ? "" : report.issues.front().detail);
        const Picture picture = render(ui.context(), renderer);

        // The dark theme's window background inside the window, the clear color outside, the active title bar
        // (the window has the focus) at its top.
        const Color background = builtin_theme(ThemeKind::DARK).color(StyleColor::WINDOW_BACKGROUND);
        const auto channel = [](std::uint32_t pixel, std::uint32_t index) { return (pixel >> (8U * index)) & 0xFFU; };
        const std::uint32_t inside = picture.at(300 * scale, 250 * scale);
        EXPECT_NEAR(channel(inside, 0), 0.2 * 255 * (1 - background.alpha) + background.red * 255 * background.alpha, 2.0) << scale;
        EXPECT_EQ(picture.at(5 * scale, 5 * scale), gfx::tests::rgba(51, 51, 51, 255)) << scale;
        const Color title = builtin_theme(ThemeKind::DARK).color(StyleColor::TITLE_BACKGROUND_ACTIVE);
        EXPECT_NEAR(channel(picture.at(250 * scale, 20 * scale), 2), title.blue * 255, 2.0) << scale;
        EXPECT_GT(renderer.statistics().draw_calls, 0U);
    }
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         UiContextRenderTest,
                         testing::ValuesIn(physical_devices()),
                         [](const testing::TestParamInfo<std::size_t>& parameter) { return std::format("Device{}", parameter.param); });
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(UiContextRenderTest);

} // namespace
