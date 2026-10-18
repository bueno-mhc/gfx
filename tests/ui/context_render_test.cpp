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

TEST_P(UiContextRenderTest, InputWidgetsAndAnOpenComboPopupAreDrawn)
{
    // A window of phase U5's widgets: a picker's square at the top, a combo whose popup is opened.
    UiHarness ui;
    UiRenderer renderer{*m_device, ui.context().fonts(), {.color_format = gfx::Format::R8G8B8A8_UNORM}};
    std::string name = "Ada";
    std::int32_t count = 4;
    float ratio = 0.25F;
    std::array<float, 3> position{1, 2, 3};
    Color color = Color::from_rgba8(40, 160, 220);
    std::size_t fruit = 0;
    const std::array<std::string_view, 3> fruits{"apple", "banana", "cherry"};
    gfx::Vector2 picker{};
    UiRectangle combo{};
    float frame_size = 0;
    float line = 0;
    const auto body = [&](UiFrame& frame)
    {
        frame.set_next_window_position({.x = 16, .y = 16}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size({.x = 420, .y = 560}, UiCondition::FIRST_USE_EVER);
        if (frame.begin_window("Inputs"))
        {
            picker = frame.cursor_screen_position();
            frame_size = frame.frame_height();
            line = frame.text_line_height();
            frame.set_next_item_width(200);
            frame.color_picker("Picker", color, ColorEditFlag::NO_INPUTS | ColorEditFlag::NO_SIDE_PREVIEW | ColorEditFlag::NO_ALPHA);
            frame.input_text("Name", name);
            frame.input_number("Count", count, 1, 10);
            frame.slider("Ratio", ratio, 0.0F, 1.0F);
            frame.drag("Position", position, 0.1F);
            frame.color_edit("Color", color);
            frame.combo("Fruit", fruit, fruits);
            if (!frame.is_window_appearing())
            {
                combo = frame.item_rectangle();
            }
        }
        frame.end_window();
    };
    ui.frames(3, body);
    ui.click(gfx::tests::center(combo).x, gfx::tests::center(combo).y, body);
    const UiFrameReport report = ui.frames(2, body);
    EXPECT_TRUE(report.ok()) << (report.issues.empty() ? "" : report.issues.front().detail);
    ASSERT_EQ(ui.context().draw_data().lists.size(), 2U) << "the window and the popup";
    const Picture picture = render(ui.context(), renderer);
    const auto channel = [](std::uint32_t pixel, std::uint32_t index) { return static_cast<int>((pixel >> (8U * index)) & 0xFFU); };

    // The square: white at its top left, the hue (sky blue) at its top right, black at its bottom.
    const float square = std::floor(200 - (frame_size + 4));
    const std::uint32_t white = picture.at(picker.x + 2, picker.y + 2);
    const std::uint32_t hue = picture.at(picker.x + square - 3, picker.y + 2);
    const std::uint32_t black = picture.at(picker.x + 2, picker.y + square - 3);
    EXPECT_GT(std::min({channel(white, 0), channel(white, 1), channel(white, 2)}), 235);
    EXPECT_GT(channel(hue, 2), 230);
    EXPECT_LT(channel(hue, 0), 30);
    EXPECT_LT(std::max({channel(black, 0), channel(black, 1), channel(black, 2)}), 20);

    // The popup over the window: its selected first item highlighted (the header blue).
    const std::uint32_t selected = picture.at(combo.left + 12, combo.bottom + 8 + line * 0.5F);
    EXPECT_GT(channel(selected, 2), channel(selected, 0) + 25);
}

TEST_P(UiContextRenderTest, AnEditedFieldShowsItsSelectionAndAComposition)
{
    // A field whose text is selected (Shift+Home from the end), a multi-line field, and a composition.
    UiHarness ui;
    UiRenderer renderer{*m_device, ui.context().fonts(), {.color_format = gfx::Format::R8G8B8A8_UNORM}};
    std::string name = "Selected text";
    std::string notes = "First line\nSecond line";
    UiRectangle field{};
    const auto body = [&](UiFrame& frame)
    {
        frame.set_next_window_position({.x = 16, .y = 16}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size({.x = 420, .y = 260}, UiCondition::FIRST_USE_EVER);
        if (frame.begin_window("Editing"))
        {
            frame.input_text("Name", name);
            field = frame.item_rectangle();
            frame.input_text_multiline("Notes", notes, {.size = {.x = 300, .y = 80}});
        }
        frame.end_window();
    };
    ui.frames(3, body);
    ui.click(field.right - 2, gfx::tests::center(field).y, body);
    ui.modifiers(gfx::KeyModifier::SHIFT);
    ui.tap(gfx::Key::HOME);
    ui.frame(body);
    ui.modifiers(gfx::KeyModifier::NONE);
    UiFrameReport report = ui.frame(body);
    EXPECT_TRUE(report.ok()) << (report.issues.empty() ? "" : report.issues.front().detail);
    const Picture selected = render(ui.context(), renderer);
    const auto blue = [](std::uint32_t pixel) { return static_cast<int>((pixel >> 16U) & 0xFFU); };

    // The selection's blue behind the text (near the frame's top, between glyphs), not after it.
    const gfx::Vector2 inside{.x = field.left + 5, .y = field.top + 4};
    const gfx::Vector2 after{.x = field.right - 6, .y = field.top + 4};
    EXPECT_GT(blue(selected.at(inside.x, inside.y)), blue(selected.at(after.x, after.y)) + 30);

    // A composition takes the selection's place (as committing it will) and is not typed.
    ui.compose("abc", 3);
    report = ui.frames(2, body);
    EXPECT_TRUE(report.ok()) << (report.issues.empty() ? "" : report.issues.front().detail);
    const Picture composing = render(ui.context(), renderer);
    EXPECT_LT(std::abs(blue(composing.at(inside.x, inside.y)) - blue(composing.at(after.x, after.y))), 10);
    EXPECT_EQ(name, "Selected text");
    ui.compose("");
    ui.type("abc");
    ui.frame(body);
    EXPECT_EQ(name, "abc");
}

TEST_P(UiContextRenderTest, MenusTreesTabsAndTooltipsAreDrawn)
{
    // A window with a menu bar whose File menu is open, a tree, a collapsing header, a tab bar, and a tooltip.
    UiHarness ui;
    UiRenderer renderer{*m_device, ui.context().fonts(), {.color_format = gfx::Format::R8G8B8A8_UNORM}};
    UiRectangle file{};
    UiRectangle tab{};
    UiRectangle bar{};
    bool checked = true;
    const auto body = [&](UiFrame& frame)
    {
        frame.set_next_window_position({.x = 16, .y = 16}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size({.x = 420, .y = 360}, UiCondition::FIRST_USE_EVER);
        if (frame.begin_window("Structure", UiWindowFlag::MENU_BAR))
        {
            if (frame.begin_menu_bar())
            {
                if (frame.begin_menu("File"))
                {
                    frame.menu_item("Open", "Ctrl+O");
                    frame.menu_item("Save", "Ctrl+S");
                    frame.menu_item("Autosave", {}, &checked);
                    if (frame.begin_menu("Recent"))
                    {
                        frame.menu_item("scene.json");
                        frame.end_menu();
                    }
                    frame.end_menu();
                }
                else
                {
                    file = frame.item_rectangle();
                }
                if (frame.begin_menu("Edit"))
                {
                    frame.end_menu();
                }
                frame.end_menu_bar();
            }
            const gfx::Vector2 start = frame.cursor_screen_position();
            if (frame.begin_tab_bar("Panels"))
            {
                if (frame.begin_tab_item("Scene"))
                {
                    tab = frame.item_rectangle();
                    frame.set_next_item_open(true, UiCondition::ONCE);
                    if (frame.tree_node("Root"))
                    {
                        frame.tree_node("Leaf", TreeNodeFlag::LEAF | TreeNodeFlag::NO_TREE_PUSH_ON_OPEN);
                        frame.tree_node("Bullet", TreeNodeFlag::BULLET | TreeNodeFlag::LEAF | TreeNodeFlag::NO_TREE_PUSH_ON_OPEN);
                        frame.tree_pop();
                    }
                    frame.collapsing_header("Properties");
                    frame.end_tab_item();
                }
                if (frame.begin_tab_item("Assets"))
                {
                    frame.end_tab_item();
                }
                frame.end_tab_bar();
            }
            bar = {.left = start.x, .top = start.y, .right = start.x + frame.content_region_available().x, .bottom = start.y + frame.frame_height()};
            frame.button("Hover me");
            frame.item_tooltip("A tooltip over everything");
        }
        frame.end_window();
    };
    const UiFrameReport closed = ui.frames(3, body);
    EXPECT_TRUE(closed.ok()) << (closed.issues.empty() ? "" : closed.issues.front().detail);
    const Picture bar_picture = render(ui.context(), renderer);
    const auto channel = [](std::uint32_t pixel, std::uint32_t index) { return static_cast<int>((pixel >> (8U * index)) & 0xFFU); };
    const auto brightest = [&](const Picture& picture, const UiRectangle& area)
    {
        int value = 0;
        for (float y = area.top; y < area.bottom; y += 1.0F)
        {
            for (float x = area.left; x < area.right; x += 1.0F)
            {
                value = std::max(value, channel(picture.at(x, y), 0));
            }
        }
        return value;
    };

    // Closed: the menu bar's labels (white text on the bar's color), the selected tab's blue, the tabs under the bar.
    const Theme theme = builtin_theme(ThemeKind::DARK);
    EXPECT_GT(brightest(bar_picture, file), 200) << "the File label is drawn in the menu bar";
    const std::uint32_t menu_bar = bar_picture.at(file.right + 200, gfx::tests::center(file).y);
    EXPECT_NEAR(channel(menu_bar, 0), theme.color(StyleColor::MENU_BAR_BACKGROUND).red * 255, 3.0);
    const std::uint32_t selected = bar_picture.at(tab.left + 3, tab.top + tab.height() * 0.5F);
    EXPECT_GT(channel(selected, 2), channel(selected, 0) + 40);
    EXPECT_GT(tab.top + 1, file.bottom);
    EXPECT_LT(bar.top, tab.top + 1);

    // File open: a second list, its rows under File.
    ui.click(gfx::tests::center(file).x, gfx::tests::center(file).y, body);
    ui.frames(2, body);
    const UiFrameReport report = ui.frame(body);
    EXPECT_TRUE(report.ok()) << (report.issues.empty() ? "" : report.issues.front().detail);
    ASSERT_EQ(ui.context().draw_data().lists.size(), 2U) << "the window and the menu";
    const Picture menu_picture = render(ui.context(), renderer);
    const UiRectangle first_row{.left = file.left, .top = file.bottom + 4, .right = file.left + 60, .bottom = file.bottom + 4 + file.height()};
    EXPECT_GT(brightest(menu_picture, first_row), 200) << "the menu's first item";
}

TEST_P(UiContextRenderTest, TablesPlotsAndProgressBarsAreDrawn)
{
    // A table (a header row, alternating rows, borders), a histogram and a half-filled progress bar.
    UiHarness ui;
    UiRenderer renderer{*m_device, ui.context().fonts(), {.color_format = gfx::Format::R8G8B8A8_UNORM}};
    UiRectangle table{};
    std::array<float, 4> row_tops{};
    float column_border = 0;
    float row_height = 0;
    UiRectangle histogram{};
    UiRectangle progress{};
    const std::array values{0.2F, 0.9F, 0.4F, 1.0F, 0.6F};
    const float padding = builtin_theme(ThemeKind::DARK).style.cell_padding.y;
    const float padding_x = builtin_theme(ThemeKind::DARK).style.cell_padding.x;
    const auto body = [&](UiFrame& frame)
    {
        frame.set_next_window_position({.x = 16, .y = 16}, UiCondition::FIRST_USE_EVER);
        frame.set_next_window_size({.x = 420, .y = 360}, UiCondition::FIRST_USE_EVER);
        if (frame.begin_window("Tables"))
        {
            if (frame.begin_table("files", 3, TableFlag::BORDERS | TableFlag::ROW_BACKGROUND | TableFlag::RESIZABLE))
            {
                frame.table_setup_column("Name");
                frame.table_setup_column("Size");
                frame.table_setup_column("Kind");
                frame.table_headers_row();
                for (std::size_t row = 0; row < 3; ++row)
                {
                    frame.table_next_row();
                    for (int column = 0; column < 3; ++column)
                    {
                        frame.table_next_column();
                        if (column == 0)
                        {
                            row_tops[row + 1] = frame.cursor_screen_position().y - padding;
                        }
                        if (column == 1 && row == 0)
                        {
                            column_border = frame.cursor_screen_position().x - padding_x;
                        }
                        frame.text("{}", row * 10 + static_cast<std::size_t>(column));
                    }
                }
                frame.end_table();
                table = frame.item_rectangle();
            }
            row_height = frame.text_line_height() + padding * 2;
            row_tops[0] = table.top;
            frame.plot_histogram("Bars", values, {.minimum = 0.0F, .maximum = 1.0F, .size = {.x = 200, .y = 50}});
            histogram = frame.item_rectangle();
            frame.progress_bar(0.5F, {.x = 200, .y = 0});
            progress = frame.item_rectangle();
        }
        frame.end_window();
    };
    const UiFrameReport report = ui.frames(3, body);
    EXPECT_TRUE(report.ok()) << (report.issues.empty() ? "" : report.issues.front().detail);
    const Picture picture = render(ui.context(), renderer);
    const auto red = [&](float x, float y) { return static_cast<double>(picture.at(x, y) & 0xFFU); };
    const auto blue = [&](float x, float y) { return static_cast<double>((picture.at(x, y) >> 16U) & 0xFFU); };

    // What a color gives over the window's background (itself over the clear color 0.2).
    const Theme theme = builtin_theme(ThemeKind::DARK);
    const Color window = theme.color(StyleColor::WINDOW_BACKGROUND);
    const double behind = (0.2 * (1 - window.alpha) + window.red * window.alpha) * 255;
    const auto over = [behind](Color color) { return color.red * 255 * color.alpha + behind * (1 - color.alpha); };

    // The header row's color; rows alternating (the second body row lighter); the borders between the columns
    // and around the table.
    const float empty_x = table.right - 20;
    EXPECT_NEAR(red(empty_x, row_tops[0] + row_height * 0.5F), over(theme.color(StyleColor::TABLE_HEADER_BACKGROUND)), 3.0);
    EXPECT_NEAR(red(empty_x, row_tops[1] + row_height * 0.5F), behind, 3.0);
    EXPECT_NEAR(red(empty_x, row_tops[2] + row_height * 0.5F), over(theme.color(StyleColor::TABLE_ROW_BACKGROUND_ALTERNATE)), 3.0);
    EXPECT_NEAR(red(std::floor(column_border), row_tops[3] + row_height * 0.5F), over(theme.color(StyleColor::TABLE_BORDER_LIGHT)), 3.0);
    EXPECT_NEAR(red(table.left, row_tops[2] + row_height * 0.5F), over(theme.color(StyleColor::TABLE_BORDER_STRONG)), 3.0);

    // The histogram's tallest bar (the fourth value) and the progress bar's filled half in the histogram color, its
    // other half the frame's.
    const Color bars = theme.color(StyleColor::PLOT_HISTOGRAM);
    const float bar_x = histogram.left + builtin_theme(ThemeKind::DARK).style.frame_padding.x + (200 - 8) * 3.5F / 5;
    EXPECT_NEAR(red(bar_x, histogram.top + 10), bars.red * 255, 3.0);
    EXPECT_NEAR(blue(bar_x, histogram.top + 10), 0.0, 3.0);
    EXPECT_NEAR(red(progress.left + 40, gfx::tests::center(progress).y), bars.red * 255, 3.0);
    EXPECT_NEAR(red(progress.right - 30, gfx::tests::center(progress).y), over(theme.color(StyleColor::FRAME_BACKGROUND)), 3.0);
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         UiContextRenderTest,
                         testing::ValuesIn(physical_devices()),
                         [](const testing::TestParamInfo<std::size_t>& parameter) { return std::format("Device{}", parameter.param); });
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(UiContextRenderTest);

} // namespace
