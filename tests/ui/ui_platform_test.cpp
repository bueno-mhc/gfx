// The UI on the gfx platform library (gfx.ui.platform): window events turned into frame input (UI units,
// buttons, keys, text, focus, other windows' events skipped), the window services (cursors), and a smoke test on
// every platform backend: a UI in a visible window presented with Vulkan (validation layers on), clicked through
// events pushed to the platform, and read back from the swapchain image.
#include "vulkan_test.hpp"

import std;
import gfx.platform.graphics;
import gfx.ui;
import gfx.ui.platform;
import gfx.tests.platform_latin;

namespace
{

using namespace std::chrono_literals;
using gfx::platform::Event;
using gfx::platform::EventType;
using gfx::platform::PlatformBackendKind;

/// A platform of @p backend and a window of it, hidden unless @p visible.
struct PlatformWindow
{
    std::unique_ptr<gfx::platform::Platform> platform{};
    std::unique_ptr<gfx::platform::Window> window{};
};

auto
make_window(PlatformBackendKind backend,
            bool visible) -> PlatformWindow
{
    PlatformWindow result;
    result.platform = gfx::platform::create_platform(backend, {.application_name = "gfx UI platform tests"});
    gfx::platform::WindowDescription description;
    description.title = "gfx UI platform test";
    description.size = gfx::Extent{.width = 480, .height = 320, .depth = 1};
    description.flags = gfx::platform::WindowFlag::HIGH_PIXEL_DENSITY | (visible ? gfx::platform::WindowFlag::NONE : gfx::platform::WindowFlag::HIDDEN);
    description.surface_api = gfx::platform::SurfaceApi::VULKAN;
    result.window = result.platform->create_window(description);
    static_cast<void>(result.platform->poll_events());
    return result;
}

/// Every platform backend built here.
auto
backends() -> std::vector<PlatformBackendKind>
{
    return gfx::platform::available_platform_backends();
}

class UiPlatformInputTest : public testing::TestWithParam<PlatformBackendKind>
{
};

TEST_P(UiPlatformInputTest, TheWindowsEventsBecomeTheFramesInput)
{
    const PlatformWindow platform = make_window(GetParam(), false);
    const gfx::platform::WindowHandle window = platform.window->handle();
    gfx::ui::UiPlatformInput input{*platform.window};
    const float units = platform.window->pixel_density() / platform.window->content_scale();

    // Motion, a click, the wheel, a key, typed text; an event of no window is skipped.
    const std::string text = "hé";
    const std::array events{
        Event::make<EventType::MOUSE_MOTION>(window, 0ns, {.position = {.x = 40, .y = 30}}),
        Event::make<EventType::MOUSE_BUTTON_DOWN>(window, 0ns, {.button = gfx::MouseButton::LEFT, .position = {.x = 50, .y = 30}}),
        Event::make<EventType::MOUSE_WHEEL>(window, 0ns, {.delta = {.x = 0, .y = -2}}),
        Event::make<EventType::KEY_DOWN>(window, 0ns, {.key = gfx::Key::A, .key_code = gfx::KeyCode::A, .modifiers = gfx::KeyModifier::LEFT_SHIFT}),
        Event::make<EventType::TEXT_INPUT>(window, 0ns, {.text = {.offset = 0, .length = static_cast<std::uint32_t>(text.size())}}),
        Event::make<EventType::MOUSE_MOTION>(gfx::platform::WindowHandle{}, 0ns, {.position = {.x = 400, .y = 300}}),
    };
    input.begin_frame();
    input.add_events({events, text});
    const gfx::ui::UiFrameInput frame = input.frame_input(2.5);
    EXPECT_EQ(frame.time, 2.5);
    EXPECT_TRUE(frame.mouse_present);
    EXPECT_FLOAT_EQ(frame.mouse_position.x, 50 * units);
    EXPECT_FLOAT_EQ(frame.mouse_position.y, 30 * units);
    ASSERT_EQ(frame.mouse_buttons.size(), 1U);
    EXPECT_TRUE(frame.mouse_buttons[0].down);
    EXPECT_EQ(frame.mouse_buttons_held, gfx::MouseButtonMask::LEFT);
    EXPECT_EQ(frame.mouse_wheel.y, -2.0F);
    ASSERT_EQ(frame.keys.size(), 1U);
    EXPECT_EQ(frame.keys[0].key, gfx::Key::A);
    EXPECT_EQ(frame.modifiers, gfx::KeyModifier::LEFT_SHIFT);
    EXPECT_EQ(frame.text, "hé");
    EXPECT_FLOAT_EQ(frame.pixels_per_unit, platform.window->content_scale());
    EXPECT_FLOAT_EQ(frame.display_size.x, static_cast<float>(platform.window->framebuffer_size().width) / platform.window->content_scale());

    // The next frame keeps what is held; losing the focus releases it.
    input.begin_frame();
    EXPECT_TRUE(input.frame_input(2.6).mouse_buttons.empty());
    EXPECT_EQ(input.frame_input(2.6).mouse_buttons_held, gfx::MouseButtonMask::LEFT);
    const std::array focus{Event::make<EventType::WINDOW_FOCUS_LOST>(window, 0ns, {})};
    input.add_events({focus, {}});
    EXPECT_FALSE(input.frame_input(2.7).focused);
    EXPECT_EQ(input.frame_input(2.7).mouse_buttons_held, gfx::MouseButtonMask::NONE);
}

TEST_P(UiPlatformInputTest, TheWindowServicesShowCursors)
{
    PlatformWindow platform = make_window(GetParam(), false);
    gfx::ui::UiWindowServices services{*platform.platform, *platform.window};
    for (const gfx::CursorShape shape : {gfx::CursorShape::ARROW, gfx::CursorShape::I_BEAM, gfx::CursorShape::RESIZE_NORTHWEST_SOUTHEAST})
    {
        EXPECT_NO_THROW(services.set_cursor(shape)) << gfx::to_string(shape);
    }
    EXPECT_FALSE(services.open_url("https://example.com"));
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         UiPlatformInputTest,
                         testing::ValuesIn(backends()),
                         [](const testing::TestParamInfo<PlatformBackendKind>& parameter) { return std::format("{}", parameter.param); });
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(UiPlatformInputTest);

// ===================================================================================================== smoke test

class UiWindowTest : public testing::TestWithParam<PlatformBackendKind>
{
protected:
    void SetUp() override
    {
        if (!gfx::backend_availability(gfx::BackendKind::VULKAN).available)
        {
            GTEST_SKIP() << "no Vulkan";
        }
        gfx::tests::use_configured_validation_layers();
        gfx::InstanceDescription description{
            .application_name = "gfx UI window test",
            .validation_level = gfx::ValidationLevel::COMMANDS,
            .enable_validation = gfx::tests::validation_requested(),
            .debug_message_severities = gfx::DebugMessageSeverity::WARNING | gfx::DebugMessageSeverity::CRITICAL,
            .debug_callback = [this](const gfx::DebugMessage& message) { m_validation_messages.add(message); },
        };
        try
        {
            m_instance = gfx::create_instance(gfx::BackendKind::VULKAN, description);
        }
        catch (const gfx::BackendCapabilityError&)
        {
            description.enable_validation = false;
            m_instance = gfx::create_instance(gfx::BackendKind::VULKAN, description);
        }
        static constexpr std::array QUEUES{gfx::QueueRequest{.type = gfx::QueueType::GRAPHICS, .count = 1, .priority = 1.0F}};
        m_device = m_instance->select_physical_device().create_device({.queues = QUEUES, .debug_name = "UI window test device"});
        m_pool = m_device->create_command_pool({.queue_type = gfx::QueueType::GRAPHICS, .debug_name = "UI window test pool"});
    }

    void TearDown() override
    {
        if (m_device != nullptr)
        {
            m_device->wait_idle();
        }
        m_pool.reset();
        m_device.reset();
        m_instance.reset();
        for (const std::string& message : m_validation_messages.take())
        {
            ADD_FAILURE() << "validation message: " << message;
        }
    }

    std::unique_ptr<gfx::Instance> m_instance{};
    std::unique_ptr<gfx::Device> m_device{};
    std::unique_ptr<gfx::CommandPool> m_pool{};
    gfx::tests::ValidationMessages m_validation_messages{};
};

TEST_P(UiWindowTest, AUiInAVisibleWindowIsDrawnAndClicked)
{
    PlatformWindow platform = make_window(GetParam(), true);
    gfx::platform::Window& window = *platform.window;

    // A UNORM swapchain whose images can be read back.
    const gfx::SurfaceSupport support = m_device->physical_device().surface_support(window.native_handle());
    gfx::Format format = gfx::Format::B8G8R8A8_UNORM;
    if (!std::ranges::contains(support.formats, gfx::SurfaceFormat{.format = format, .color_space = gfx::ColorSpace::SRGB_NONLINEAR}))
    {
        format = gfx::Format::R8G8B8A8_UNORM;
    }
    gfx::platform::WindowSwapchain presentation{*m_device, window,
                                                {.format = format,
                                                 .usage = gfx::ImageUsage::COLOR_ATTACHMENT | gfx::ImageUsage::TRANSFER_SOURCE,
                                                 .debug_name = "UI window test swapchain"}};

    // The UI: services and input of the window, the test font, the renderer.
    gfx::ui::UiWindowServices services{*platform.platform, window};
    gfx::ui::UiContext ui{{.services = &services}};
    ui.load_font(gfx::ui::FontAtlas::load(gfx::tests::platform_atlases::platform_latin), 16.0F);
    gfx::ui::UiPlatformInput input{window};
    gfx::ui::UiRenderer renderer{*m_device, ui.fonts(), {.color_format = format, .frames_in_flight = 2}};

    // Frames: the button is clicked through events pushed to the platform after a few frames.
    std::uint32_t clicks = 0;
    gfx::ui::UiRectangle button{};
    gfx::ui::UiFrameReport report{};
    std::vector<std::uint32_t> pixels;
    gfx::Extent extent{};
    const float to_window = window.content_scale() / window.pixel_density();
    for (std::uint64_t frame_number = 1; frame_number <= 40; ++frame_number)
    {
        input.begin_frame();
        const gfx::platform::EventSpan events = platform.platform->poll_events();
        for (const Event& event : events)
        {
            presentation.handle_event(event);
        }
        input.add_events(events);
        const gfx::ui::UiFrameInput frame_input = input.frame_input(static_cast<double>(frame_number) / 60.0);
        gfx::ui::UiFrame& frame = ui.begin_frame(frame_input);
        frame.set_next_window_position({.x = 20, .y = 20}, gfx::ui::UiCondition::FIRST_USE_EVER);
        if (frame.begin_window("Smoke test"))
        {
            frame.text("Frame {}", frame_number);
            clicks += frame.button("Click me") ? 1U : 0U;
            button = frame.item_rectangle();
            bool checked = true;
            frame.checkbox("Checked", checked);
        }
        frame.end_window();
        report = ui.end_frame();

        // Mouse events at the button (window coordinates), pushed for the next pumps.
        const gfx::Vector2 target{.x = (button.left + button.right) * 0.5F * to_window, .y = (button.top + button.bottom) * 0.5F * to_window};
        if (frame_number == 10)
        {
            platform.platform->push_event(Event::make<EventType::MOUSE_MOTION>(window.handle(), 0ns, {.position = target}));
        }
        if (frame_number == 12)
        {
            platform.platform->push_event(
                Event::make<EventType::MOUSE_BUTTON_DOWN>(window.handle(), 0ns, {.button = gfx::MouseButton::LEFT, .position = target}));
        }
        if (frame_number == 14)
        {
            platform.platform->push_event(
                Event::make<EventType::MOUSE_BUTTON_UP>(window.handle(), 0ns, {.button = gfx::MouseButton::LEFT, .position = target}));
        }

        // Rendering: the UI over a clear color; the last frame is read back.
        const std::optional<gfx::platform::SwapchainFrame> image = presentation.acquire(5s);
        if (!image)
        {
            continue;
        }
        extent = presentation.extent();
        const bool last = frame_number == 40;
        const std::unique_ptr<gfx::Buffer> readback =
            m_device->create_buffer({.size = std::uint64_t{extent.width} * extent.height * 4,
                                     .usage = gfx::BufferUsage::TRANSFER_DESTINATION,
                                     .location = gfx::MemoryLocation::HOST_READBACK});
        const std::unique_ptr<gfx::CommandBuffer> commands = m_pool->allocate(gfx::CommandBufferLevel::PRIMARY, "UI frame");
        const gfx::ui::UiDrawData& data = ui.draw_data();
        renderer.begin_frame(frame_number);
        {
            gfx::GraphicsRecorder recorder{*commands};
            const std::array to_attachment{gfx::image_barrier(image->image.image, gfx::ResourceState::UNDEFINED, gfx::ResourceState::COLOR_ATTACHMENT_WRITE)};
            recorder.barrier({.image_barriers = to_attachment});
            renderer.prepare(recorder, data);
            {
                const std::array attachments{gfx::ColorAttachment{.view = image->image.view, .clear_color = {0.0F, 0.0F, 0.0F, 1.0F}}};
                gfx::RenderingScope scope = recorder.begin_rendering({.area = {.width = extent.width, .height = extent.height}, .color_attachments = attachments});
                renderer.record(scope, data);
            }
            const std::array to_copy{gfx::image_barrier(image->image.image, gfx::ResourceState::COLOR_ATTACHMENT_WRITE, gfx::ResourceState::TRANSFER_SOURCE)};
            recorder.barrier({.image_barriers = to_copy});
            if (last)
            {
                const std::array regions{gfx::BufferImageCopyRegion{.image_extent = extent}};
                recorder.copy_image_to_buffer(image->image.image, gfx::ImageLayout::TRANSFER_SOURCE, readback->handle(), regions);
            }
            const std::array to_present{gfx::image_barrier(image->image.image, gfx::ResourceState::TRANSFER_SOURCE, gfx::ResourceState::PRESENT)};
            const std::array to_host{gfx::buffer_barrier(readback->handle(), gfx::ResourceState::TRANSFER_DESTINATION, gfx::ResourceState::HOST_READ)};
            recorder.barrier({.buffer_barriers = to_host, .image_barriers = to_present});
        }
        const std::unique_ptr<gfx::Fence> fence = m_device->create_fence({.debug_name = "UI frame fence"});
        gfx::Queue& queue = m_device->queue(gfx::QueueType::GRAPHICS);
        const std::array command_buffers{commands->handle()};
        const std::array waits{gfx::SemaphoreWait{.semaphore = image->acquired}};
        const std::array signals{gfx::SemaphoreSignal{.semaphore = image->rendered}};
        const std::array batches{gfx::SubmitDescription{.command_buffers = command_buffers, .waits = waits, .signals = signals}};
        queue.submit(batches, fence->handle());
        static_cast<void>(presentation.present(queue, *image));
        ASSERT_EQ(fence->wait(10s), gfx::WaitStatus::SIGNALED);
        if (last)
        {
            readback->invalidate_mapped_range();
            const std::span<std::uint32_t> words = readback->mapped_elements<std::uint32_t>();
            pixels.assign(words.begin(), words.begin() + static_cast<std::ptrdiff_t>(std::size_t{extent.width} * extent.height));
        }
    }

    // One click; a frame without problems; the window drawn where it is, the clear color elsewhere.
    EXPECT_EQ(clicks, 1U);
    EXPECT_TRUE(report.ok());
    EXPECT_GT(renderer.statistics().draw_calls, 0U);
    ASSERT_FALSE(pixels.empty());
    const float scale = window.content_scale();
    const auto pixel = [&](float x, float y) { return pixels.at(static_cast<std::size_t>(y * scale) * extent.width + static_cast<std::size_t>(x * scale)); };
    EXPECT_NE(pixel(button.left + 2, (button.top + button.bottom) * 0.5F), gfx::tests::rgba(0, 0, 0, 255)) << "the button";
    EXPECT_EQ(pixel(5, 5), gfx::tests::rgba(0, 0, 0, 255)) << "outside the window";
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         UiWindowTest,
                         testing::ValuesIn(backends()),
                         [](const testing::TestParamInfo<PlatformBackendKind>& parameter) { return std::format("{}", parameter.param); });
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(UiWindowTest);

} // namespace
