// Presentation to platform windows (gfx.platform.graphics), on every platform backend built here and
// every GPU of the machine, with the Vulkan validation layers (any validation message fails the test):
// cleared frames reach a visible window (read back from the swapchain image), resizing recreates the
// swapchain, a minimized window presents nothing and is presented to again once restored, and settings
// the surface lacks are named.
#include "vulkan_test.hpp"

import std;
import gfx.platform.graphics;
#if GFX_TEST_WITH_UI
import gfx.ui;
#endif

namespace
{

using namespace std::chrono_literals;
using gfx::BackendCapabilityError;
using gfx::Extent;
using gfx::InvalidUsageError;
using gfx::platform::PlatformBackendKind;
using gfx::platform::SwapchainFrame;
using gfx::platform::WindowSwapchain;

/// One platform backend and one physical device.
struct PresentationParameter
{
    PlatformBackendKind backend = PlatformBackendKind::SDL3;
    std::size_t physical_device = 0;
};

/// Every available platform backend with every physical device that can run gfx and present.
auto
presentation_parameters() -> std::vector<PresentationParameter>
{
    std::vector<PresentationParameter> parameters;
    if (!gfx::backend_availability(gfx::BackendKind::VULKAN).available)
    {
        return parameters;
    }
    const std::unique_ptr<gfx::Instance> instance =
        gfx::create_instance(gfx::BackendKind::VULKAN, {.application_name = "gfx presentation tests (enumeration)"});
    for (const PlatformBackendKind backend : gfx::platform::available_platform_backends())
    {
        for (std::size_t index = 0; index < instance->physical_device_count(); ++index)
        {
            const gfx::PhysicalDevice& device = instance->physical_device(index);
            if (device.missing_core_features().empty() && device.supports(gfx::DeviceFeature::PRESENTATION))
            {
                parameters.push_back({.backend = backend, .physical_device = index});
            }
        }
    }
    return parameters;
}

/// What a test draws in a frame: before its rendering pass, and in it.
struct FrameDrawing
{
    std::function<void(gfx::GraphicsRecorder&)> before_pass{};
    std::function<void(gfx::RenderingCommands&)> in_pass{};
};

/// A clear color, and the 32-bit word a readback of the cleared image holds.
struct ClearColor
{
    std::array<float, 4> value{};
    std::uint32_t red = 0;
    std::uint32_t green = 0;
    std::uint32_t blue = 0;
};

constexpr ClearColor RED{.value = {1.0F, 0.0F, 0.0F, 1.0F}, .red = 255};
constexpr ClearColor GREEN{.value = {0.0F, 1.0F, 0.0F, 1.0F}, .green = 255};
constexpr ClearColor BLUE{.value = {0.0F, 0.0F, 1.0F, 1.0F}, .blue = 255};

/// What a presented frame showed.
struct PresentedFrame
{
    gfx::SwapchainStatus status = gfx::SwapchainStatus::OPTIMAL;
    Extent extent{};
    std::uint32_t first_pixel = 0;
    std::uint32_t last_pixel = 0;
    std::vector<std::uint32_t> pixels{};

    /// The pixel at (@p x, @p y).
    [[nodiscard]] auto pixel(std::uint32_t x,
                             std::uint32_t y) const -> std::uint32_t
    {
        return pixels.at(std::size_t{y} * extent.width + x);
    }
};

/// A visible window of the backend under test on the device under test.
class PresentationTest : public testing::TestWithParam<PresentationParameter>
{
protected:
    void SetUp() override
    {
        // The instance, with the validation layers when installed.
        gfx::tests::use_configured_validation_layers();
        gfx::InstanceDescription description{
            .application_name = "gfx presentation tests",
            .validation_level = gfx::ValidationLevel::COMMANDS,
            .enable_validation = gfx::tests::validation_requested(),
            .debug_message_severities = gfx::DebugMessageSeverity::WARNING | gfx::DebugMessageSeverity::CRITICAL,
            .debug_callback = [this](const gfx::DebugMessage& message) { m_validation_messages.add(message); },
        };
        try
        {
            m_instance = gfx::create_instance(gfx::BackendKind::VULKAN, description);
        }
        catch (const BackendCapabilityError& error)
        {
            std::cout << "note: running without the Vulkan validation layers: " << error.reason() << '\n';
            description.enable_validation = false;
            m_instance = gfx::create_instance(gfx::BackendKind::VULKAN, description);
        }
        m_validation_enabled = description.enable_validation;

        // The device, with one graphics queue.
        m_physical_device = &m_instance->physical_device(GetParam().physical_device);
        std::cout << "note: " << to_string(GetParam().backend) << " window on '" << m_physical_device->properties().name << "'\n";
        static constexpr std::array QUEUES{gfx::QueueRequest{.type = gfx::QueueType::GRAPHICS, .count = 1, .priority = 1.0F}};
        m_device = m_physical_device->create_device({.queues = QUEUES, .debug_name = "presentation test device"});
        m_pool = m_device->create_command_pool({.queue_type = gfx::QueueType::GRAPHICS, .debug_name = "presentation test pool"});

        // A visible window prepared for Vulkan, which does not take the focus.
        m_platform = gfx::platform::create_platform(GetParam().backend, {.application_name = "gfx presentation tests"});
        gfx::platform::WindowDescription window;
        window.title = "gfx presentation test";
        window.size = Extent{.width = 320, .height = 240, .depth = 1};
        window.flags = gfx::platform::WindowFlag::RESIZABLE | gfx::platform::WindowFlag::HIGH_PIXEL_DENSITY;
        window.surface_api = gfx::platform::SurfaceApi::VULKAN;
        m_window = m_platform->create_window(window);
        (void)m_platform->poll_events();
    }

    void TearDown() override
    {
        // The device and the instance go first (their surfaces refer to the window), then the window.
        m_pool.reset();
        m_device.reset();
        m_instance.reset();
        m_window.reset();
        m_platform.reset();
        EXPECT_TRUE(m_validation_enabled || !gfx::tests::validation_requested()) << "the validation layers were requested";
        for (const std::string& message : m_validation_messages.take())
        {
            ADD_FAILURE() << "validation message: " << message;
        }
    }

    /// The swapchain settings of the tests: a UNORM format whose readback is exact, copyable images.
    [[nodiscard]] auto swapchain_description() -> gfx::SwapchainDescription
    {
        const gfx::SurfaceSupport support = m_physical_device->surface_support(m_window->native_handle());
        for (const gfx::Format format : {gfx::Format::B8G8R8A8_UNORM, gfx::Format::R8G8B8A8_UNORM})
        {
            if (std::ranges::contains(support.formats, gfx::SurfaceFormat{.format = format, .color_space = gfx::ColorSpace::SRGB_NONLINEAR}))
            {
                return {
                    .format = format,
                    .usage = gfx::ImageUsage::COLOR_ATTACHMENT | gfx::ImageUsage::TRANSFER_SOURCE,
                    .debug_name = "presentation test swapchain",
                };
            }
        }
        ADD_FAILURE() << "the surface offers neither B8G8R8A8_UNORM nor R8G8B8A8_UNORM";
        return {};
    }

    /// The word a readback of an image of @p format cleared to @p color holds.
    [[nodiscard]] static auto expected_pixel(gfx::Format format,
                                             const ClearColor& color) -> std::uint32_t
    {
        return format == gfx::Format::B8G8R8A8_UNORM ? gfx::tests::rgba(color.blue, color.green, color.red, 255)
                                                     : gfx::tests::rgba(color.red, color.green, color.blue, 255);
    }

    /// Pumps events (handed to @p presentation) until @p condition holds or @p timeout passes.
    template<class Condition>
    auto pump_until(WindowSwapchain& presentation,
                    Condition condition,
                    std::chrono::milliseconds timeout = 3000ms) -> bool
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            for (const gfx::platform::Event& event : m_platform->wait_events_timeout(10ms))
            {
                presentation.handle_event(event);
            }
            if (condition())
            {
                return true;
            }
        }
        return condition();
    }

    /// Acquires an image, clears it to @p color (then records @p drawing), copies it for reading, presents
    /// it and waits for the GPU. std::nullopt when no image was acquired (minimized window).
    auto present_cleared_frame(WindowSwapchain& presentation,
                               const ClearColor& color,
                               const FrameDrawing& drawing = {}) -> std::optional<PresentedFrame>
    {
        const std::optional<SwapchainFrame> frame = presentation.acquire(5s);
        if (!frame)
        {
            return std::nullopt;
        }
        const Extent extent = presentation.extent();
        const std::uint64_t size = std::uint64_t{extent.width} * extent.height * 4;
        const std::unique_ptr<gfx::Buffer> readback = m_device->create_buffer({
            .size = size,
            .usage = gfx::BufferUsage::TRANSFER_DESTINATION,
            .location = gfx::MemoryLocation::HOST_READBACK,
        });

        // Clear, copy for reading, hand over to presentation.
        const std::unique_ptr<gfx::CommandBuffer> commands = m_pool->allocate(gfx::CommandBufferLevel::PRIMARY, "frame");
        {
            gfx::GraphicsRecorder recorder{*commands};
            const std::array to_attachment{gfx::image_barrier(frame->image.image, gfx::ResourceState::UNDEFINED,
                                                              gfx::ResourceState::COLOR_ATTACHMENT_WRITE)};
            recorder.barrier({.image_barriers = to_attachment});
            if (drawing.before_pass)
            {
                drawing.before_pass(recorder);
            }
            {
                const std::array attachments{gfx::ColorAttachment{.view = frame->image.view, .clear_color = color.value}};
                const gfx::Rectangle area{.width = extent.width, .height = extent.height};
                gfx::RenderingScope scope = recorder.begin_rendering({.area = area, .color_attachments = attachments});
                if (drawing.in_pass)
                {
                    drawing.in_pass(scope);
                }
            }
            const std::array to_copy{gfx::image_barrier(frame->image.image, gfx::ResourceState::COLOR_ATTACHMENT_WRITE,
                                                        gfx::ResourceState::TRANSFER_SOURCE)};
            recorder.barrier({.image_barriers = to_copy});
            const std::array regions{gfx::BufferImageCopyRegion{.image_extent = extent}};
            recorder.copy_image_to_buffer(frame->image.image, gfx::ImageLayout::TRANSFER_SOURCE, readback->handle(), regions);
            const std::array to_present{gfx::image_barrier(frame->image.image, gfx::ResourceState::TRANSFER_SOURCE,
                                                           gfx::ResourceState::PRESENT)};
            const std::array to_host{gfx::buffer_barrier(readback->handle(), gfx::ResourceState::TRANSFER_DESTINATION,
                                                         gfx::ResourceState::HOST_READ)};
            recorder.barrier({.buffer_barriers = to_host, .image_barriers = to_present});
        }

        // Rendering waits for the acquisition and signals the presentation.
        const std::unique_ptr<gfx::Fence> fence = m_device->create_fence({.debug_name = "frame fence"});
        gfx::Queue& queue = m_device->queue(gfx::QueueType::GRAPHICS);
        const std::array command_buffers{commands->handle()};
        const std::array waits{gfx::SemaphoreWait{.semaphore = frame->acquired}};
        const std::array signals{gfx::SemaphoreSignal{.semaphore = frame->rendered}};
        const std::array batches{gfx::SubmitDescription{.command_buffers = command_buffers, .waits = waits, .signals = signals}};
        queue.submit(batches, fence->handle());
        const gfx::SwapchainStatus status = presentation.present(queue, *frame);
        EXPECT_EQ(fence->wait(10s), gfx::WaitStatus::SIGNALED);

        // What the window shows.
        readback->invalidate_mapped_range();
        const std::span<std::uint32_t> pixels = readback->mapped_elements<std::uint32_t>();
        return PresentedFrame{
            .status = status,
            .extent = extent,
            .first_pixel = pixels.front(),
            .last_pixel = pixels.back(),
            .pixels = {pixels.begin(), pixels.begin() + static_cast<std::ptrdiff_t>(std::size_t{extent.width} * extent.height)},
        };
    }

    /// Presents frames cleared to @p color (then drawn by @p drawing) until one is acquired (pumping events
    /// in between), for at most @p timeout.
    auto present_when_possible(WindowSwapchain& presentation,
                               const ClearColor& color,
                               const FrameDrawing& drawing = {},
                               std::chrono::milliseconds timeout = 3000ms) -> std::optional<PresentedFrame>
    {
        std::optional<PresentedFrame> frame;
        (void)pump_until(presentation, [&] { return (frame = present_cleared_frame(presentation, color, drawing)).has_value(); }, timeout);
        return frame;
    }

    std::unique_ptr<gfx::Instance> m_instance;
    gfx::PhysicalDevice* m_physical_device = nullptr;
    std::unique_ptr<gfx::Device> m_device;
    std::unique_ptr<gfx::CommandPool> m_pool;
    std::unique_ptr<gfx::platform::Platform> m_platform;
    std::unique_ptr<gfx::platform::Window> m_window;

private:
    gfx::tests::ValidationMessages m_validation_messages;
    bool m_validation_enabled = false;
};

TEST_P(PresentationTest, SurfaceSupportDescribesTheWindow)
{
    const gfx::SurfaceSupport support = m_physical_device->surface_support(m_window->native_handle());
    EXPECT_FALSE(support.formats.empty());
    EXPECT_TRUE(std::ranges::contains(support.present_modes, gfx::PresentMode::FIFO));
    EXPECT_GE(support.minimum_image_count, 1U);
    EXPECT_TRUE(support.maximum_image_count == 0 || support.maximum_image_count >= support.minimum_image_count);
    EXPECT_EQ(support.current_extent.width, m_window->framebuffer_size().width);
    EXPECT_EQ(support.current_extent.height, m_window->framebuffer_size().height);
    EXPECT_TRUE(has_all(support.supported_usage, gfx::ImageUsage::COLOR_ATTACHMENT));
}

TEST_P(PresentationTest, ClearedFramesReachTheWindow)
{
    WindowSwapchain presentation{*m_device, *m_window, swapchain_description()};
    EXPECT_EQ(presentation.extent().width, m_window->framebuffer_size().width);
    EXPECT_EQ(presentation.extent().height, m_window->framebuffer_size().height);
    EXPECT_GE(presentation.swapchain().image_count(), 2U);

    // More frames than images: the semaphores are recycled as the images come back.
    for (const ClearColor& color : {RED, GREEN, BLUE, RED, GREEN, BLUE, RED, GREEN})
    {
        const std::optional<PresentedFrame> frame = present_when_possible(presentation, color);
        ASSERT_TRUE(frame.has_value());
        EXPECT_NE(frame->status, gfx::SwapchainStatus::SURFACE_LOST);
        EXPECT_EQ(frame->first_pixel, expected_pixel(presentation.format(), color));
        EXPECT_EQ(frame->last_pixel, expected_pixel(presentation.format(), color));
    }
}

TEST_P(PresentationTest, ResizingRecreatesTheSwapchain)
{
    WindowSwapchain presentation{*m_device, *m_window, swapchain_description()};
    ASSERT_TRUE(present_when_possible(presentation, RED).has_value());
    const Extent before = presentation.extent();

    // The framebuffer grows; the next frames have its new size.
    m_window->set_size(Extent{.width = 480, .height = 300, .depth = 1});
    ASSERT_TRUE(pump_until(presentation, [&] { return m_window->framebuffer_size().width != before.width; }));
    std::optional<PresentedFrame> frame;
    for (int attempt = 0; attempt < 3 && (!frame || frame->extent.width != m_window->framebuffer_size().width); ++attempt)
    {
        frame = present_when_possible(presentation, GREEN);
    }
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->extent.width, m_window->framebuffer_size().width);
    EXPECT_EQ(frame->extent.height, m_window->framebuffer_size().height);
    EXPECT_EQ(frame->first_pixel, expected_pixel(presentation.format(), GREEN));
    EXPECT_EQ(frame->last_pixel, expected_pixel(presentation.format(), GREEN));
    EXPECT_GE(presentation.recreation_count(), 1U);
}

TEST_P(PresentationTest, MinimizedWindowsPresentNothing)
{
    WindowSwapchain presentation{*m_device, *m_window, swapchain_description()};
    ASSERT_TRUE(present_when_possible(presentation, RED).has_value());

    // Minimized: no image.
    m_window->minimize();
    ASSERT_TRUE(pump_until(presentation, [&] { return m_window->is_minimized(); }));
    EXPECT_FALSE(presentation.acquire(100ms).has_value());

    // Restored: the swapchain follows the window back.
    m_window->restore();
    ASSERT_TRUE(pump_until(presentation, [&] { return !m_window->is_minimized(); }));
    const std::optional<PresentedFrame> frame = present_when_possible(presentation, BLUE);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->extent.width, m_window->framebuffer_size().width);
    EXPECT_EQ(frame->extent.height, m_window->framebuffer_size().height);
    EXPECT_EQ(frame->first_pixel, expected_pixel(presentation.format(), BLUE));
}

TEST_P(PresentationTest, FrameLatencyWaitsForPresentation)
{
    WindowSwapchain presentation{*m_device, *m_window, swapchain_description()};
    if (!m_physical_device->supports(gfx::DeviceFeature::FRAME_LATENCY_WAIT))
    {
        std::cout << "note: no FRAME_LATENCY_WAIT: the documented error is expected" << std::endl;
        EXPECT_THROW((void)presentation.swapchain().wait_for_frame_latency(1s), BackendCapabilityError);
        return;
    }

    // Before any presentation, then after more presentations than the latency allows.
    EXPECT_EQ(presentation.swapchain().wait_for_frame_latency(0ns), gfx::WaitStatus::SIGNALED);
    for (const ClearColor& color : {RED, GREEN, BLUE, RED})
    {
        ASSERT_TRUE(present_when_possible(presentation, color).has_value());
    }
    EXPECT_EQ(presentation.swapchain().wait_for_frame_latency(5s), gfx::WaitStatus::SIGNALED);
}

#if GFX_TEST_WITH_UI
TEST_P(PresentationTest, TheUiIsDrawnIntoTheWindow)
{
    // A blue panel 44 pixels wide from the corner, a green bar inside it: shapes of the UI renderer, in the
    // window's format (sRGB or not, the colors read back as given).
    WindowSwapchain presentation{*m_device, *m_window, swapchain_description()};
    const gfx::ui::FontLibrary fonts;
    gfx::ui::UiRenderer renderer{*m_device, fonts, {.color_format = presentation.format()}};
    gfx::ui::DrawList list;
    const auto color = [](const ClearColor& clear) { return gfx::ui::Color{clear.value[0], clear.value[1], clear.value[2], clear.value[3]}; };
    list.add_rectangle_filled({.left = 0, .top = 0, .right = 44, .bottom = 60}, color(BLUE));
    list.add_rectangle_filled({.left = 20, .top = 12, .right = 24, .bottom = 60}, color(GREEN));
    const std::array lists{&list};
    const gfx::Extent extent = presentation.extent();
    const gfx::ui::UiDrawData ui{.lists = lists, .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height)};
    std::uint64_t frame_number = 0;
    const FrameDrawing drawing{.before_pass = [&](gfx::GraphicsRecorder& recorder)
                               {
                                   renderer.begin_frame(++frame_number);
                                   renderer.prepare(recorder, ui);
                               },
                               .in_pass = [&](gfx::RenderingCommands& commands) { renderer.record(commands, ui); }};
    const std::optional<PresentedFrame> frame = present_when_possible(presentation, RED, drawing);
    ASSERT_TRUE(frame.has_value());
    const gfx::Format format = presentation.format();
    EXPECT_EQ(frame->pixel(1, 1), expected_pixel(format, BLUE));
    EXPECT_EQ(frame->pixel(21, 30), expected_pixel(format, GREEN));
    EXPECT_EQ(frame->pixel(10, 30), expected_pixel(format, BLUE));
    EXPECT_EQ(frame->pixel(60, 5), expected_pixel(format, RED));
    EXPECT_EQ(renderer.statistics().draw_calls, 1U);
}
#endif

TEST_P(PresentationTest, UnsupportedSettingsAreNamed)
{
    // A format no surface presents.
    gfx::SwapchainDescription description = gfx::platform::swapchain_description_for(*m_window);
    description.format = gfx::Format::D32_SFLOAT;
    try
    {
        (void)m_device->create_swapchain(description);
        FAIL() << "a D32_SFLOAT swapchain was created";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("D32_SFLOAT"), std::string_view::npos) << error.what();
        EXPECT_NE(std::string_view{error.what()}.find("supports"), std::string_view::npos) << error.what();
    }

    // No window, and no way to know when an acquired image may be written.
    EXPECT_THROW((void)m_device->create_swapchain({}), InvalidUsageError);
    const std::unique_ptr<gfx::Swapchain> swapchain = m_device->create_swapchain(gfx::platform::swapchain_description_for(*m_window));
    EXPECT_THROW((void)swapchain->acquire_next_image({}, {}), InvalidUsageError);
}

INSTANTIATE_TEST_SUITE_P(Presentation,
                         PresentationTest,
                         testing::ValuesIn(presentation_parameters()),
                         [](const testing::TestParamInfo<PresentationParameter>& parameter)
                         {
                             return std::format("{}_Device{}", parameter.param.backend, parameter.param.physical_device);
                         });
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(PresentationTest);

} // namespace
