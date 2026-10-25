// The physics sandbox of gfx: every library at work in one window. A physics world (Box3D or the reference backend,
// gfx.physics) steps at 60 Hz on its own thread while the main thread reads the window's events (gfx.platform), turns
// them into actions (its input system), moves a camera (gfx.camera), builds a UI (gfx.ui) and draws the bodies as lit
// meshes with Vulkan (gfx), interpolated between the last two steps.
//
// Mouse: right drag orbits around the target, middle drag pans it, the wheel zooms. Escape quits.
//
// Usage: gfx_physics_sandbox [--backend sdl3|glfw] [--physics box3d|reference] [--device <index>] [--threads <count>]
//                            [--frames <count>] [--validation] [--capture <file.ppm>]
//   --frames N     closes after N frames, one physics step per frame (the same frames on any machine); the window then
//                  does not take the focus
//   --validation   enables the Vulkan validation layers; the exit code is 1 if they report an error
//   --capture F    writes the last frame to F (binary PPM)
import std;
import gfx.platform.graphics;   // gfx (graphics), gfx.platform (windows and input), WindowSwapchain
import gfx.ui;                  // the panels
import gfx.ui.platform;         // the UI's input and services from the window
import gfx.camera;              // the orbit camera
import physics_sandbox.font;    // the UI's font atlas, built by gfx_add_ui_atlas()
import physics_sandbox.renderer;
import physics_sandbox.scene;

namespace
{

using namespace physics_sandbox;
using namespace gfx::platform::literals;
using namespace std::chrono_literals;
namespace physics = gfx::physics;
namespace camera = gfx::camera;

// ------------------------------------------------------------------------------------------------- input

/// The sandbox's actions: the camera's (the mouse) and quitting, in one context.
constexpr auto INPUT = gfx::platform::make_input_schema(
    gfx::platform::actions(gfx::platform::action("orbit"),
                           gfx::platform::action("pan"),
                           gfx::platform::action("look_x", gfx::platform::ActionValueType::AXIS_1D),
                           gfx::platform::action("look_y", gfx::platform::ActionValueType::AXIS_1D),
                           gfx::platform::action("zoom", gfx::platform::ActionValueType::AXIS_1D),
                           gfx::platform::action("quit")),
    gfx::platform::context("sandbox",
                           gfx::platform::bind("orbit"_action, gfx::platform::MouseButton::RIGHT),
                           gfx::platform::bind("pan"_action, gfx::platform::MouseButton::MIDDLE),
                           gfx::platform::bind("look_x"_action, gfx::platform::MouseAxis::MOTION_X),
                           gfx::platform::bind("look_y"_action, gfx::platform::MouseAxis::MOTION_Y),
                           gfx::platform::bind("zoom"_action, gfx::platform::MouseAxis::WHEEL_Y),
                           gfx::platform::bind("quit"_action, gfx::platform::Key::ESCAPE)));

// ------------------------------------------------------------------------------------------------- settings

constexpr std::string_view TITLE = "gfx physics sandbox";
constexpr gfx::Extent INITIAL_SIZE{.width = 1600, .height = 900, .depth = 1};
constexpr std::uint32_t FRAMES_IN_FLIGHT = 2;
constexpr float ORBIT_RADIANS_PER_PIXEL = 0.006F;
constexpr float INTERFACE_FONT_SIZE = 15.0F; ///< UI units (points) per em.
constexpr std::size_t PLOTTED_STEPS = 180;   ///< Step times plotted.

/// What the command line asks for.
struct Options
{
    std::optional<gfx::platform::PlatformBackendKind> backend{};
    physics::PhysicsBackendKind physics = physics::PhysicsBackendKind::BOX3D;
    std::optional<std::size_t> physical_device{};
    std::uint32_t threads = std::max(1U, std::thread::hardware_concurrency());
    std::optional<std::uint64_t> frames{};
    bool validation = false;
    std::optional<std::string> capture{};
};

/// @p name in capitals (enumerator names).
auto
upper(std::string_view name) -> std::string
{
    std::string result{name};
    std::ranges::transform(result, result.begin(), [](char character) { return static_cast<char>(std::toupper(static_cast<unsigned char>(character))); });
    return result;
}

/// The command line, or std::nullopt after printing the usage.
auto
parse_options(std::span<char* const> arguments) -> std::optional<Options>
{
    Options options;
    if (!physics::physics_backend_availability(physics::PhysicsBackendKind::BOX3D).available)
    {
        options.physics = physics::PhysicsBackendKind::REFERENCE;
    }
    for (std::size_t index = 1; index < arguments.size(); ++index)
    {
        const std::string_view argument = arguments[index];
        const bool has_value = index + 1 < arguments.size();
        if (argument == "--backend" && has_value)
        {
            options.backend = gfx::from_string<gfx::platform::PlatformBackendKind>(upper(arguments[++index]));
            if (!options.backend)
            {
                std::println(std::cerr, "unknown platform backend '{}'", arguments[index]);
                return std::nullopt;
            }
        }
        else if (argument == "--physics" && has_value)
        {
            const std::optional<physics::PhysicsBackendKind> kind = gfx::from_string<physics::PhysicsBackendKind>(upper(arguments[++index]));
            if (!kind)
            {
                std::println(std::cerr, "unknown physics backend '{}'", arguments[index]);
                return std::nullopt;
            }
            options.physics = *kind;
        }
        else if (argument == "--device" && has_value)
        {
            options.physical_device = std::stoull(arguments[++index]);
        }
        else if (argument == "--threads" && has_value)
        {
            options.threads = std::max(1U, static_cast<std::uint32_t>(std::stoul(arguments[++index])));
        }
        else if (argument == "--frames" && has_value)
        {
            options.frames = std::stoull(arguments[++index]);
        }
        else if (argument == "--validation")
        {
            options.validation = true;
        }
        else if (argument == "--capture" && has_value)
        {
            options.capture = arguments[++index];
        }
        else
        {
            std::println(std::cerr, "usage: gfx_physics_sandbox [--backend sdl3|glfw] [--physics box3d|reference] [--device <index>] "
                                    "[--threads <count>] [--frames <count>] [--validation] [--capture <file.ppm>]");
            return std::nullopt;
        }
    }
    return options;
}

/// The swapchain format: B8G8R8A8_SRGB when the window's surface offers it, else its first sRGB format.
auto
choose_format(const gfx::SurfaceSupport& support) -> gfx::Format
{
    constexpr gfx::SurfaceFormat PREFERRED{.format = gfx::Format::B8G8R8A8_SRGB, .color_space = gfx::ColorSpace::SRGB_NONLINEAR};
    if (std::ranges::contains(support.formats, PREFERRED))
    {
        return PREFERRED.format;
    }
    const auto found = std::ranges::find(support.formats, gfx::ColorSpace::SRGB_NONLINEAR, &gfx::SurfaceFormat::color_space);
    return found != support.formats.end() ? found->format : gfx::Format::B8G8R8A8_SRGB;
}

/// Counts the errors the validation layers report (from any thread).
class ValidationErrors
{
public:
    void report(const gfx::DebugMessage& message)
    {
        std::println(std::cerr, "[{} {}] {}", message.severity, message.type, message.text);
        if (message.severity == gfx::DebugMessageSeverity::CRITICAL)
        {
            m_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] auto count() const noexcept -> std::uint32_t
    {
        return m_count.load(std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint32_t> m_count{0};
};

// ------------------------------------------------------------------------------------------------- drawing the scene

/// Every collider of every body, at the poses drawn this frame, in its body's color (darker while asleep).
void
add_scene(SceneRenderer& renderer,
          const Scene& scene)
{
    for (const SceneBody& body : scene.bodies())
    {
        const gfx::Transform pose = scene.drawn_pose(body);
        physics::DebugColor color = body.color;
        if (body.kind == physics::BodyKind::DYNAMIC && !body.awake)
        {
            color = {.red = static_cast<std::uint8_t>(color.red * 0.72F), .green = static_cast<std::uint8_t>(color.green * 0.72F),
                     .blue = static_cast<std::uint8_t>(color.blue * 0.72F)};
        }
        for (const SceneCollider& collider : body.colliders)
        {
            renderer.add(collider.shape, pose * collider.pose, color, body.flags);
        }
    }

    // The world's axes at the origin.
    renderer.lines().add_axes({.position = {.y = 0.01F}}, 1.0F);
}

// ------------------------------------------------------------------------------------------------- the panels

/// What the statistics panel shows besides the scene.
struct FrameReport
{
    double frame_seconds = 0.0;
    std::uint32_t threads = 0;
    std::string_view device{};
    gfx::SampleCount samples = gfx::SampleCount::ONE;
    const SceneRendererStatistics* drawn = nullptr;
    std::span<const float> step_milliseconds{};
};

void
statistics_panel(gfx::ui::UiFrame& frame,
                 const Scene& scene,
                 const FrameReport& report)
{
    frame.set_next_window_position({.x = 12, .y = 12}, gfx::ui::UiCondition::FIRST_USE_EVER);
    frame.set_next_window_size({.x = 380, .y = 0}, gfx::ui::UiCondition::FIRST_USE_EVER);
    if (frame.begin_window("Physics sandbox"))
    {
        const physics::StepStatistics step = scene.statistics();
        frame.text("{} physics, {} threads; step {}", scene.backend(), report.threads, step.step);
        frame.text("bodies {} ({} awake), joints {}, soft bodies {}", step.rigid_bodies, step.awake_bodies, step.joints, step.soft_bodies);
        frame.text("pairs {}, contact points {}", step.pairs, step.contacts);
        double total = 0.0;
        for (const double seconds : step.phase_seconds)
        {
            total += seconds;
        }
        frame.text("step {:.2f} ms", total * 1000.0);
        for (std::size_t phase = 0; phase < step.phase_seconds.size(); ++phase)
        {
            frame.text("  {:<12} {:6.3f} ms", static_cast<physics::StepPhase>(phase), step.phase_seconds[phase] * 1000.0);
        }
        frame.plot_lines("step ms", report.step_milliseconds, {.minimum = 0.0F, .size = {.x = 0, .y = 48}});
        frame.separator();
        frame.text_unformatted(report.device);
        frame.text("frame {:.2f} ms ({:.0f} fps)", report.frame_seconds * 1000.0, report.frame_seconds > 0 ? 1.0 / report.frame_seconds : 0.0);
        if (report.drawn != nullptr)
        {
            frame.text("{} instances in {} draws, {} lines, {}x MSAA", report.drawn->instances, report.drawn->draw_calls, report.drawn->lines,
                       std::to_underlying(report.samples));
            frame.text("{} meshes: {} vertices, {} indices", report.drawn->meshes, report.drawn->mesh_vertices, report.drawn->mesh_indices);
        }
        frame.separator();
        frame.text_unformatted("right drag: orbit   middle drag: pan   wheel: zoom   Esc: quit");
    }
    frame.end_window();
}

// ------------------------------------------------------------------------------------------------- capture

/// Writes @p pixels (B8G8R8A8 or R8G8B8A8 words of @p extent) to @p path as a binary PPM.
void
write_capture(const std::string& path,
              std::span<const std::uint32_t> pixels,
              gfx::Extent extent,
              bool blue_first)
{
    std::ofstream file{path, std::ios::binary};
    file << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
    std::vector<char> row(std::size_t{extent.width} * 3);
    for (std::uint32_t y = 0; y < extent.height; ++y)
    {
        for (std::uint32_t x = 0; x < extent.width; ++x)
        {
            const std::uint32_t pixel = pixels[std::size_t{y} * extent.width + x];
            const auto first = static_cast<char>(pixel & 0xFFU);
            const auto second = static_cast<char>((pixel >> 8U) & 0xFFU);
            const auto third = static_cast<char>((pixel >> 16U) & 0xFFU);
            row[std::size_t{x} * 3] = blue_first ? third : first;
            row[std::size_t{x} * 3 + 1] = second;
            row[std::size_t{x} * 3 + 2] = blue_first ? first : third;
        }
        file.write(row.data(), static_cast<std::streamsize>(row.size()));
    }
}

// ------------------------------------------------------------------------------------------------- the program

auto
run(const Options& options) -> int
{
    // ---------------------------------------------------------------------------------- the window

    const std::vector<gfx::platform::PlatformBackendKind> backends = gfx::platform::available_platform_backends();
    if (backends.empty())
    {
        std::println(std::cerr, "no platform backend is available: {}",
                     gfx::platform::platform_backend_availability(gfx::platform::PlatformBackendKind::SDL3).reason);
        return 1;
    }
    const physics::PhysicsBackendAvailability physics_availability = physics::physics_backend_availability(options.physics);
    if (!physics_availability.available)
    {
        std::println(std::cerr, "the {} physics backend is unavailable: {}", options.physics, physics_availability.reason);
        return 1;
    }
    const std::unique_ptr<gfx::platform::Platform> platform =
        gfx::platform::create_platform(options.backend.value_or(backends.front()), {.application_name = TITLE});
    gfx::platform::WindowDescription window_description;
    window_description.title = TITLE;
    window_description.size = INITIAL_SIZE;
    window_description.minimum_size = gfx::Extent{.width = 480, .height = 270, .depth = 1};
    window_description.surface_api = gfx::platform::SurfaceApi::VULKAN;
    if (options.frames)
    {
        window_description.flags &= ~gfx::platform::WindowFlag::FOCUS_ON_SHOW; // automated runs leave the focus where it is
    }
    const std::unique_ptr<gfx::platform::Window> window = platform->create_window(window_description);

    // ---------------------------------------------------------------------------------- the GPU

    ValidationErrors validation_errors;
    const std::unique_ptr<gfx::Instance> instance = gfx::create_instance(
        gfx::BackendKind::VULKAN, {.application_name = TITLE,
                                   .enable_validation = options.validation,
                                   .debug_message_severities = gfx::DebugMessageSeverity::WARNING | gfx::DebugMessageSeverity::CRITICAL,
                                   .debug_callback = [&validation_errors](const gfx::DebugMessage& message) { validation_errors.report(message); }});
    gfx::PhysicalDevice& physical_device =
        options.physical_device ? instance->physical_device(*options.physical_device) : instance->select_physical_device();
    static constexpr std::array QUEUES{gfx::QueueRequest{.type = gfx::QueueType::GRAPHICS, .count = 1, .priority = 1.0F}};
    const std::unique_ptr<gfx::Device> device = physical_device.create_device({.queues = QUEUES, .debug_name = "sandbox device"});
    gfx::Queue& queue = device->queue(gfx::QueueType::GRAPHICS);

    // The swapchain (copyable images when a capture is asked), 4x multisampling when the device has it.
    const gfx::SurfaceSupport support = physical_device.surface_support(window->native_handle());
    const bool capturing = options.capture.has_value() && gfx::has_all(support.supported_usage, gfx::ImageUsage::TRANSFER_SOURCE);
    if (options.capture && !capturing)
    {
        std::println(std::cerr, "the window's images cannot be copied: no capture");
    }
    gfx::platform::WindowSwapchain presentation{
        *device, *window,
        {.format = choose_format(support),
         .usage = capturing ? gfx::ImageUsage::COLOR_ATTACHMENT | gfx::ImageUsage::TRANSFER_SOURCE : gfx::ImageUsage::COLOR_ATTACHMENT,
         .debug_name = "sandbox swapchain"}};
    const gfx::DeviceLimits& limits = physical_device.properties().limits;
    const gfx::SampleCount samples = gfx::has_all(limits.color_sample_counts, gfx::SampleCount::FOUR) && gfx::has_all(limits.depth_sample_counts, gfx::SampleCount::FOUR)
                                         ? gfx::SampleCount::FOUR
                                         : gfx::SampleCount::ONE;
    gfx::FrameTimeline timeline{*device, FRAMES_IN_FLIGHT};
    gfx::FrameCommandPools command_pools{*device, {.queue_type = gfx::QueueType::GRAPHICS, .debug_name = "sandbox commands"}, timeline.timeline(),
                                         FRAMES_IN_FLIGHT};
    SceneRenderer renderer{*device, {.color_format = presentation.format(), .samples = samples, .frames_in_flight = FRAMES_IN_FLIGHT}};

    // The UI: the window's services and events, a font of 15 points, drawn over the scene.
    gfx::ui::UiWindowServices ui_services{*platform, *window};
    gfx::ui::UiContext ui{{.services = &ui_services}};
    ui.load_font(gfx::ui::FontAtlas::load(sandbox_font), INTERFACE_FONT_SIZE);
    gfx::ui::UiPlatformInput ui_input{*window};
    gfx::ui::UiRenderer ui_renderer{*device, ui.fonts(), {.color_format = presentation.format(), .frames_in_flight = FRAMES_IN_FLIGHT}};
    std::println("{} window on '{}': {}x{} pixels, {}, {}x MSAA; {} physics on {} threads", platform->backend(), physical_device.properties().name,
                 presentation.extent().width, presentation.extent().height, presentation.format(), std::to_underlying(samples), options.physics,
                 options.threads);

    // ---------------------------------------------------------------------------------- the scene

    physics::ThreadTaskScheduler scheduler{options.threads};
    Scene scene{options.physics, scheduler};
    build_showcase(scene);

    // A perspective camera with reversed depth and no far plane (Vulkan: +Y down in normalized device coordinates),
    // turned by an orbit controller.
    const std::unique_ptr<camera::Camera> view_camera = camera::create_camera(
        {.projection = camera::PerspectiveProjection{.vertical_field_of_view = 0.9F, .aspect_ratio = 16.0F / 9.0F, .near_plane = 0.05F,
                                                     .far_plane = camera::INFINITE_FAR_PLANE},
         .clip = {.reversed_depth = true, .y_axis = camera::NormalizedDeviceYAxis::DOWN}});
    const std::unique_ptr<camera::CameraController> orbit = camera::create_camera_controller(
        camera::OrbitControllerDescription{.target = {.x = -5.5F, .y = 0.8F, .z = -2.5F}, .distance = 17.0F, .yaw = 0.12F, .pitch = -0.36F,
                                           .maximum_distance = 300.0F});

    // ---------------------------------------------------------------------------------- the loop

    gfx::platform::InputSystem input{INPUT};
    const gfx::platform::ContextActivation sandbox_context = input.activate_context("sandbox"_context);
    std::vector<float> step_milliseconds(PLOTTED_STEPS, 0.0F);
    std::uint64_t plotted_step = 0;
    std::uint64_t presented = 0;
    double frame_seconds = 0.0;
    bool running = true;
    bool ui_wants_mouse = false;
    const auto start = std::chrono::steady_clock::now();
    auto previous_time = start;
    double ui_time = 0.0;
    while (running && (!options.frames || presented < *options.frames))
    {
        // Events, then a frame of actions.
        const gfx::platform::EventSpan events = window->is_minimized() ? platform->wait_events_timeout(50ms) : platform->poll_events();
        ui_input.begin_frame();
        ui_input.add_events(events);
        for (const gfx::platform::Event& event : events)
        {
            presentation.handle_event(event);
            if (event.window() == window->handle() && event.type() == gfx::platform::EventType::WINDOW_CLOSE_REQUESTED)
            {
                running = false;
            }
        }
        const gfx::platform::InputFrame& frame = input.update(events);
        if (frame.was_action_just_pressed("quit"_action))
        {
            running = false;
        }

        // Time: real, or one step per frame in automated runs.
        const auto now = std::chrono::steady_clock::now();
        frame_seconds = std::chrono::duration<double>(now - previous_time).count();
        previous_time = now;
        const double advanced = options.frames ? static_cast<double>(STEP) : frame_seconds;
        ui_time += advanced;

        // The camera: the mouse turns, pans and zooms it unless the UI has it.
        const gfx::Extent extent = presentation.extent();
        camera::CameraControllerInput control{.seconds = 1.0F};
        if (!ui_wants_mouse)
        {
            const float dx = frame.get_action_value("look_x"_action);
            const float dy = frame.get_action_value("look_y"_action);
            const float height = std::max(1.0F, static_cast<float>(extent.height));
            if (frame.is_action_pressed("orbit"_action))
            {
                control.look = {.x = dx * ORBIT_RADIANS_PER_PIXEL, .y = -dy * ORBIT_RADIANS_PER_PIXEL};
            }
            if (frame.is_action_pressed("pan"_action))
            {
                control.movement = {.x = -dx / height, .y = dy / height};
            }
            control.zoom = frame.get_action_value("zoom"_action);
        }
        orbit->update(*view_camera, control);
        if (extent.width > 0 && extent.height > 0)
        {
            view_camera->set_aspect_ratio(static_cast<float>(extent.width) / static_cast<float>(extent.height));
        }

        // The physics: the step begun last frame completes, the next one runs while this frame is drawn.
        const std::uint64_t steps_before = scene.step_count();
        scene.advance(advanced);
        if (scene.step_count() != steps_before)
        {
            const physics::StepStatistics step = scene.statistics();
            double total = 0.0;
            for (const double seconds : step.phase_seconds)
            {
                total += seconds;
            }
            step_milliseconds[plotted_step % PLOTTED_STEPS] = static_cast<float>(total * 1000.0);
            ++plotted_step;
        }

        // An image to draw into (none while the window is minimized).
        const std::optional<gfx::platform::SwapchainFrame> image = presentation.acquire();
        if (!image)
        {
            continue;
        }
        const std::uint64_t frame_number = timeline.begin_frame();
        command_pools.begin_frame(frame_number);
        renderer.begin_frame(frame_number);
        ui_renderer.begin_frame(frame_number);
        add_scene(renderer, scene);

        // The UI's frame: the statistics panel (the plot oldest first).
        std::array<float, PLOTTED_STEPS> plot{};
        for (std::size_t index = 0; index < PLOTTED_STEPS; ++index)
        {
            plot[index] = step_milliseconds[(plotted_step + index) % PLOTTED_STEPS];
        }
        gfx::ui::UiFrame& ui_frame = ui.begin_frame(ui_input.frame_input(ui_time));
        statistics_panel(ui_frame, scene,
                         {.frame_seconds = frame_seconds,
                          .threads = scheduler.concurrency(),
                          .device = physical_device.properties().name,
                          .samples = samples,
                          .drawn = &renderer.statistics(),
                          .step_milliseconds = plot});
        static_cast<void>(ui.end_frame());
        ui_wants_mouse = ui.wants_mouse();
        const gfx::ui::UiDrawData& ui_data = ui.draw_data();

        // Record: the scene's pass (resolved into the image), the UI's pass over it, the hand-over to presentation.
        const bool last_frame = options.frames && presented + 1 == *options.frames;
        std::unique_ptr<gfx::Buffer> readback{};
        gfx::CommandBuffer& commands = command_pools.command_buffer();
        {
            gfx::GraphicsRecorder recorder{commands};
            const std::array to_attachment{gfx::image_barrier(image->image.image, gfx::ResourceState::UNDEFINED, gfx::ResourceState::COLOR_ATTACHMENT_WRITE)};
            recorder.barrier({.image_barriers = to_attachment});
            renderer.prepare(recorder);
            ui_renderer.prepare(recorder, ui_data);
            const camera::Camera& eye = *view_camera;
            renderer.render(recorder, image->image.view, extent, {.view_projection = eye.view_projection()});
            const std::array to_interface{
                gfx::image_barrier(image->image.image, gfx::ResourceState::COLOR_ATTACHMENT_WRITE, gfx::ResourceState::COLOR_ATTACHMENT_READ_WRITE)};
            recorder.barrier({.image_barriers = to_interface});
            {
                const std::array attachments{gfx::ColorAttachment{.view = image->image.view, .load = gfx::LoadOperation::LOAD}};
                gfx::RenderingScope scope = recorder.begin_rendering(
                    {.area = {.width = extent.width, .height = extent.height}, .color_attachments = attachments, .debug_name = "sandbox interface"});
                ui_renderer.record(scope, ui_data);
            }
            if (last_frame && capturing)
            {
                // The last frame copied out for the capture.
                readback = device->create_buffer({.size = std::uint64_t{extent.width} * extent.height * 4,
                                                  .usage = gfx::BufferUsage::TRANSFER_DESTINATION,
                                                  .location = gfx::MemoryLocation::HOST_READBACK,
                                                  .debug_name = "sandbox capture"});
                const std::array to_copy{
                    gfx::image_barrier(image->image.image, gfx::ResourceState::COLOR_ATTACHMENT_READ_WRITE, gfx::ResourceState::TRANSFER_SOURCE)};
                recorder.barrier({.image_barriers = to_copy});
                const std::array regions{gfx::BufferImageCopyRegion{.image_extent = extent}};
                recorder.copy_image_to_buffer(image->image.image, gfx::ImageLayout::TRANSFER_SOURCE, readback->handle(), regions);
                const std::array to_present{gfx::image_barrier(image->image.image, gfx::ResourceState::TRANSFER_SOURCE, gfx::ResourceState::PRESENT)};
                const std::array to_host{gfx::buffer_barrier(readback->handle(), gfx::ResourceState::TRANSFER_DESTINATION, gfx::ResourceState::HOST_READ)};
                recorder.barrier({.buffer_barriers = to_host, .image_barriers = to_present});
            }
            else
            {
                const std::array to_present{
                    gfx::image_barrier(image->image.image, gfx::ResourceState::COLOR_ATTACHMENT_READ_WRITE, gfx::ResourceState::PRESENT)};
                recorder.barrier({.image_barriers = to_present});
            }
        }

        // Submit: wait for the image, signal the presentation and the frame timeline; then present.
        const std::array command_buffers{commands.handle()};
        const std::array waits{gfx::SemaphoreWait{.semaphore = image->acquired}};
        const std::array signals{gfx::SemaphoreSignal{.semaphore = image->rendered}};
        const std::array timeline_signals{timeline.frame_signal()};
        const std::array batches{gfx::SubmitDescription{.command_buffers = command_buffers, .waits = waits, .signals = signals, .timeline_signals = timeline_signals}};
        queue.submit(batches);
        static_cast<void>(presentation.present(queue, *image));
        ++presented;
        if (readback != nullptr)
        {
            device->wait_idle();
            readback->invalidate_mapped_range();
            write_capture(*options.capture, readback->mapped_elements<std::uint32_t>(), extent,
                          presentation.format() == gfx::Format::B8G8R8A8_SRGB || presentation.format() == gfx::Format::B8G8R8A8_UNORM);
            std::println("captured the last frame into {}", *options.capture);
        }
    }

    // The step in flight and the GPU finish before anything is destroyed.
    scene.finish();
    device->wait_idle();
    const physics::StepStatistics last = scene.statistics();
    std::println("{} frames presented, {} steps, {} bodies ({} awake), {} contact points, {} validation errors", presented, scene.step_count(),
                 last.rigid_bodies, last.awake_bodies, last.contacts, validation_errors.count());
    return validation_errors.count() == 0 ? 0 : 1;
}

} // namespace

int
main(int argument_count,
     char** arguments)
{
    // Every gfx error says which backend failed, in which operation, why, and what to do instead.
    try
    {
        const std::optional<Options> options = parse_options(std::span{arguments, static_cast<std::size_t>(argument_count)});
        return options ? run(*options) : 2;
    }
    catch (const gfx::Error& error)
    {
        std::println(std::cerr, "{}", error.what());
        return 1;
    }
}
