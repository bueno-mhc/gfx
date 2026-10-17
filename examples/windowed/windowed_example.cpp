// The windowed example of gfx: a platform window (SDL3 or GLFW) presenting Vulkan frames, driven by an
// input schema declared at compile time.
//
// A square moves with the "move" action (W A S D, the arrow keys or a gamepad's left stick) and hops
// on "jump" (Space or the gamepad's South button). "pause" (Escape or Start) opens a menu context that
// blocks gameplay: there "quit" (Return or South) closes the window, "resume" (Escape, Start or East)
// goes back. "reset_size" (Backspace or Back) gives the window its initial size again. In Debug builds,
// "toggle_metrics" (F3) shows or hides the frame metrics: frame rate, frame times, GPU time, commands
// and memory, drawn by the UI library (SDF text) when it is built.
//
// Usage: gfx_windowed_example [--backend sdl3|glfw] [--device <index>] [--frames <count>] [--validation] [--metrics]
//   --frames N     closes after N presented frames (automated runs; the window then does not take the focus)
//   --validation   enables the Vulkan validation layers; the exit code is 1 if they report an error
//   --metrics      shows the frame metrics from the start (Debug builds)
import std;
import gfx.platform.graphics; // gfx (graphics), gfx.platform (windows and input), and the bridge between them
#if GFX_EXAMPLE_WITH_UI
import gfx.ui;                // the metrics overlay: text laid out and drawn by the UI library
import windowed.fonts;        // the overlay's font atlas, built by gfx_add_ui_atlas()
#endif

namespace
{

using namespace gfx::platform;
using namespace gfx::platform::literals;
using namespace std::chrono_literals;

// ------------------------------------------------------------------------------------------------- input

/// The game's input: actions and two contexts, checked at compile time (a duplicate action, a binding of
/// an undeclared action or a 2D composite on a button action does not compile).
constexpr auto INPUT = make_input_schema(
    actions(action("move", ActionValueType::AXIS_2D),
            action("jump"),
            action("pause"),
            action("resume"),
            action("quit"),
            action("reset_size"),
            action("toggle_metrics")),
    context("gameplay",
            bind("move"_action, composite_2d(Key::W, Key::S, Key::A, Key::D)),
            bind("move"_action, composite_2d(Key::UP, Key::DOWN, Key::LEFT, Key::RIGHT)),
            bind("move"_action, stick(GamepadAxis::LEFT_X, GamepadAxis::LEFT_Y).with_deadzone(DeadzoneKind::SCALED_RADIAL, 0.15F)),
            bind("jump"_action, Key::SPACE),
            bind("jump"_action, GamepadButton::SOUTH),
            bind("pause"_action, Key::ESCAPE),
            bind("pause"_action, GamepadButton::START),
            bind("reset_size"_action, Key::BACKSPACE),
            bind("reset_size"_action, GamepadButton::BACK)),
    // The menu sits above gameplay and hides it while active. Escape both opens it (gameplay's "pause")
    // and closes it (the menu's "resume"): contexts ignore keys already held when they start reading
    // them (ContextDescription::ignore_held_inputs), so the press that opens the menu does not also close it.
    context("menu",
            {.priority = 10, .consumes_input = true, .blocks_lower_contexts = true},
            bind("resume"_action, Key::ESCAPE),
            bind("resume"_action, GamepadButton::START),
            bind("resume"_action, GamepadButton::EAST),
            bind("quit"_action, Key::RETURN),
            bind("quit"_action, GamepadButton::SOUTH)),
    // Debug keys, above everything and always active.
    context("debug",
            {.priority = 100},
            bind("toggle_metrics"_action, Key::F3)));

static_assert(INPUT.actions.size() == 7 && INPUT.contexts.size() == 3);

// ------------------------------------------------------------------------------------------------- settings

constexpr std::string_view TITLE = "gfx windowed example";
constexpr std::string_view PAUSED_TITLE = "gfx windowed example - paused (Return quits, Escape resumes)";
constexpr gfx::Extent INITIAL_SIZE{.width = 1280, .height = 720, .depth = 1};
constexpr std::uint32_t FRAMES_IN_FLIGHT = 2;
constexpr float SPEED = 0.6F;                         ///< Square speed, in window heights per second.
constexpr float SQUARE_SIZE = 0.1F;                   ///< Square side, in window heights.
constexpr std::chrono::milliseconds HOP_DURATION{400};
constexpr float HOP_HEIGHT = 0.15F;                   ///< In window heights.

/// What the command line asks for.
struct Options
{
    std::optional<PlatformBackendKind> backend{};
    std::optional<std::size_t> physical_device{};
    std::optional<std::uint64_t> frames{};
    bool validation = false;
    bool metrics = false;
};

/// The command line, or std::nullopt after printing the usage.
auto
parse_options(std::span<char* const> arguments) -> std::optional<Options>
{
    Options options;
    for (std::size_t index = 1; index < arguments.size(); ++index)
    {
        const std::string_view argument = arguments[index];
        const bool has_value = index + 1 < arguments.size();
        if (argument == "--backend" && has_value)
        {
            std::string name{arguments[++index]};
            std::ranges::transform(name, name.begin(), [](char character) { return static_cast<char>(std::toupper(static_cast<unsigned char>(character))); });
            options.backend = gfx::from_string<PlatformBackendKind>(name);
            if (!options.backend)
            {
                std::println(std::cerr, "unknown platform backend '{}'", arguments[index]);
                return std::nullopt;
            }
        }
        else if (argument == "--device" && has_value)
        {
            options.physical_device = std::stoull(arguments[++index]);
        }
        else if (argument == "--frames" && has_value)
        {
            options.frames = std::stoull(arguments[++index]);
        }
        else if (argument == "--validation")
        {
            options.validation = true;
        }
        else if (argument == "--metrics")
        {
            options.metrics = true;
        }
        else
        {
            std::println(std::cerr, "usage: gfx_windowed_example [--backend sdl3|glfw] [--device <index>] [--frames <count>] [--validation] "
                                    "[--metrics]");
            return std::nullopt;
        }
    }
    return options;
}

/// A square icon of @p side pixels: an orange square on a dark blue background.
auto
icon_pixels(std::uint32_t side) -> std::vector<std::byte>
{
    std::vector<std::byte> pixels(std::size_t{side} * side * 4);
    for (std::uint32_t y = 0; y < side; ++y)
    {
        for (std::uint32_t x = 0; x < side; ++x)
        {
            const bool inside = x >= side / 4 && x < side * 3 / 4 && y >= side / 4 && y < side * 3 / 4;
            const std::size_t texel = (std::size_t{y} * side + x) * 4;
            pixels[texel + 0] = std::byte{inside ? std::uint8_t{255} : std::uint8_t{16}};
            pixels[texel + 1] = std::byte{inside ? std::uint8_t{140} : std::uint8_t{24}};
            pixels[texel + 2] = std::byte{inside ? std::uint8_t{26} : std::uint8_t{48}};
            pixels[texel + 3] = std::byte{255};
        }
    }
    return pixels;
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

/// The game: a square that moves, hops, and a menu that pauses it.
struct Game
{
    gfx::Vector2 position{.x = 0.5F, .y = 0.5F}; ///< Square center, in window heights from the top-left corner.
    std::optional<std::chrono::steady_clock::time_point> hop_start{};
    std::optional<ContextActivation> menu{};     ///< Active while the menu is open.
    bool running = true;
};

/// Runs the example; returns the exit code.
auto
run(const Options& options) -> int
{
    // ---------------------------------------------------------------------------------- the window

    // The platform: the backend asked for, or the best one available here.
    const std::vector<PlatformBackendKind> backends = available_platform_backends();
    if (backends.empty())
    {
        std::println(std::cerr, "no platform backend is available: {}", platform_backend_availability(PlatformBackendKind::SDL3).reason);
        return 1;
    }
    const std::unique_ptr<Platform> platform = create_platform(options.backend.value_or(backends.front()), {.application_name = TITLE});

    // A resizable window prepared for Vulkan, with its icon in two resolutions when the backend sets icons.
    WindowDescription description;
    description.title = TITLE;
    description.size = INITIAL_SIZE;
    description.minimum_size = gfx::Extent{.width = 320, .height = 180, .depth = 1};
    description.surface_api = SurfaceApi::VULKAN;
    if (options.frames)
    {
        description.flags &= ~WindowFlag::FOCUS_ON_SHOW; // automated runs leave the focus where it is
    }
    const std::unique_ptr<Window> window = platform->create_window(description);
    if (platform->supports(PlatformFeature::WINDOW_ICON))
    {
        const std::vector<std::byte> small = icon_pixels(32);
        const std::vector<std::byte> large = icon_pixels(64);
        const std::array icons{
            ImageData::from_rgba(gfx::Extent{.width = 32, .height = 32, .depth = 1}, small),
            ImageData::from_rgba(gfx::Extent{.width = 64, .height = 64, .depth = 1}, large),
        };
        window->set_icon(icons);
    }

    // ---------------------------------------------------------------------------------- the GPU

    // Vulkan, with the validation layers on request.
    ValidationErrors validation_errors;
    gfx::InstanceDescription instance_description{
        .application_name = TITLE,
        .enable_validation = options.validation,
        .debug_message_severities = gfx::DebugMessageSeverity::WARNING | gfx::DebugMessageSeverity::CRITICAL,
        .debug_callback = [&validation_errors](const gfx::DebugMessage& message) { validation_errors.report(message); },
    };
    const std::unique_ptr<gfx::Instance> instance = gfx::create_instance(gfx::BackendKind::VULKAN, instance_description);
    gfx::PhysicalDevice& physical_device =
        options.physical_device ? instance->physical_device(*options.physical_device) : instance->select_physical_device();
    static constexpr std::array QUEUES{gfx::QueueRequest{.type = gfx::QueueType::GRAPHICS, .count = 1, .priority = 1.0F}};
    const std::unique_ptr<gfx::Device> device = physical_device.create_device({.queues = QUEUES, .debug_name = "example device"});
    gfx::Queue& queue = device->queue(gfx::QueueType::GRAPHICS);

    // The window's native handle is all the graphics library needs: the surface it supports, then the
    // swapchain (WindowSwapchain recreates it when the window is resized, skips minimized frames).
    const gfx::NativeWindowHandle native_window = window->native_handle();
    const gfx::SurfaceSupport support = physical_device.surface_support(native_window);
    WindowSwapchain presentation{*device, *window, {.format = choose_format(support), .debug_name = "example swapchain"}};
    std::println("{} window ({}) on '{}': {}x{} pixels, {}", platform->backend(), native_window.kind(), physical_device.properties().name,
                 presentation.extent().width, presentation.extent().height, presentation.format());

    // Frame pacing: frame N signals N on the timeline; each frame records into its own command pool.
    gfx::FrameTimeline timeline{*device, FRAMES_IN_FLIGHT};
    gfx::FrameCommandPools command_pools{*device, {.queue_type = gfx::QueueType::GRAPHICS, .debug_name = "example commands"},
                                         timeline.timeline(), FRAMES_IN_FLIGHT};

    // Debug builds: frame metrics in an overlay (F3), with the GPU time of each frame when timestamps work.
    gfx::FrameMetrics metrics{*device};
    std::optional<gfx::GpuProfiler> profiler;
    if (gfx::FrameMetrics::enabled() && physical_device.supports(gfx::DeviceFeature::TIMESTAMP_QUERY))
    {
        profiler.emplace(*device, FRAMES_IN_FLIGHT);
    }
    if (!gfx::FrameMetrics::enabled())
    {
        std::println("the frame metrics (F3) exist in Debug builds only");
    }
#if GFX_EXAMPLE_WITH_UI
    // The overlay: its font, the text engine, and the renderer drawing into the swapchain's images.
    gfx::ui::FontLibrary fonts;
    const gfx::ui::FontAtlasHandle overlay_atlas = fonts.add_atlas(gfx::ui::FontAtlas::load(overlay_font));
    const gfx::ui::FontHandle overlay_text_font = fonts.add_font({.atlas = overlay_atlas, .size = 15.0F * window->content_scale()});
    gfx::ui::TextLayoutEngine text{fonts};
    gfx::ui::UiRenderer ui_renderer{*device, fonts, {.color_format = presentation.format(), .frames_in_flight = FRAMES_IN_FLIGHT}};
    gfx::ui::DebugOverlay overlay{{.visible = options.metrics}};
    gfx::ui::DrawList overlay_list;
#else
    bool overlay_visible = options.metrics;
#endif

    // ---------------------------------------------------------------------------------- the loop

    InputSystem input{INPUT};
    const ContextActivation gameplay = input.activate_context("gameplay"_context);
    const ContextActivation debug = input.activate_context("debug"_context);
    Game game;
    std::uint64_t presented = 0;
    auto previous_time = std::chrono::steady_clock::now();
    while (game.running && (!options.frames || presented < *options.frames))
    {
        // Events: a minimized window waits for them instead of spinning.
        const EventSpan events = window->is_minimized() ? platform->wait_events_timeout(50ms) : platform->poll_events();
        for (const Event& event : events)
        {
            presentation.handle_event(event);
            if (event.window() != window->handle())
            {
                continue;
            }
            if (event.type() == EventType::WINDOW_CLOSE_REQUESTED)
            {
                game.running = false;
            }
            else if (event.type() == EventType::WINDOW_FRAMEBUFFER_RESIZED)
            {
                const gfx::Extent size = event.as<WindowEvent>().framebuffer_size;
                std::println("framebuffer resized to {}x{}", size.width, size.height);
            }
        }

        // Input: one frame of actions from this pump's events.
        const InputFrame& frame = input.update(events);
        const auto now = std::chrono::steady_clock::now();
        const float elapsed = std::chrono::duration<float>(now - previous_time).count();
        previous_time = now;
        if (frame.was_action_just_pressed("pause"_action))
        {
            // The menu opens above gameplay...
            game.menu = input.activate_context("menu"_context);
            window->set_title(PAUSED_TITLE);
        }
        if (frame.was_action_just_pressed("resume"_action))
        {
            // ...and closes again.
            game.menu.reset();
            window->set_title(TITLE);
        }
        if (frame.was_action_just_pressed("toggle_metrics"_action))
        {
#if GFX_EXAMPLE_WITH_UI
            overlay.toggle();
#else
            overlay_visible = !overlay_visible;
            if (overlay_visible)
            {
                std::println("the metrics overlay needs the UI library (GFX_BUILD_UI)");
            }
#endif
        }
        if (frame.was_action_just_pressed("quit"_action))
        {
            game.running = false;
        }
        if (frame.was_action_just_pressed("reset_size"_action))
        {
            window->set_size(INITIAL_SIZE);
        }
        if (frame.was_action_just_pressed("jump"_action) && !game.hop_start)
        {
            game.hop_start = now;
        }
        if (frame.is_action_pressed("move"_action))
        {
            // Up is positive; the window's y axis points down. The square stays inside the window.
            const gfx::Extent size = window->framebuffer_size();
            const float aspect = size.height == 0 ? 1.0F : static_cast<float>(size.width) / static_cast<float>(size.height);
            const gfx::Vector2 move = frame.get_action_value_2d("move"_action);
            game.position.x = std::clamp(game.position.x + move.x * SPEED * elapsed, SQUARE_SIZE / 2, aspect - SQUARE_SIZE / 2);
            game.position.y = std::clamp(game.position.y - move.y * SPEED * elapsed, SQUARE_SIZE / 2, 1.0F - SQUARE_SIZE / 2);
        }
        float hop = 0.0F;
        if (game.hop_start)
        {
            const float progress = std::chrono::duration<float>(now - *game.hop_start) / HOP_DURATION;
            hop = progress < 1.0F ? HOP_HEIGHT * std::sin(std::numbers::pi_v<float> * progress) : 0.0F;
            if (progress >= 1.0F)
            {
                game.hop_start.reset();
            }
        }

        // An image to draw into (none while the window is minimized).
        const std::optional<SwapchainFrame> image = presentation.acquire();
        if (!image)
        {
            continue;
        }
        const std::uint64_t frame_number = timeline.begin_frame();
        command_pools.begin_frame(frame_number);
        metrics.begin_frame();

        // The square, in pixels, inside the image.
        const gfx::Extent extent = presentation.extent();
        const float scale = static_cast<float>(extent.height);
        const auto side = static_cast<std::uint32_t>(SQUARE_SIZE * scale);
        const auto left = static_cast<std::int32_t>(std::clamp((game.position.x - SQUARE_SIZE / 2) * scale, 0.0F,
                                                               static_cast<float>(extent.width - std::min(side, extent.width))));
        const auto top = static_cast<std::int32_t>(std::clamp((game.position.y - hop - SQUARE_SIZE / 2) * scale, 0.0F,
                                                              static_cast<float>(extent.height - std::min(side, extent.height))));

#if GFX_EXAMPLE_WITH_UI
        // The overlay's draw list: the previous frame's metrics, when shown.
        text.new_frame();
        overlay_list.clear();
        overlay.draw(overlay_list, text, fonts, overlay_text_font, metrics.text());
        const std::array overlay_lists{&overlay_list};
        const gfx::ui::UiDrawData ui{.lists = overlay_lists, .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height)};
        ui_renderer.begin_frame(frame_number);
#endif

        // Record: clear the image, clear the square's rectangle, draw the metrics (when shown), hand the
        // image to presentation.
        gfx::CommandBuffer& commands = command_pools.command_buffer();
        {
            gfx::GraphicsRecorder recorder{commands};
            const auto record = [&]
            {
                const std::array to_attachment{
                    gfx::image_barrier(image->image.image, gfx::ResourceState::UNDEFINED, gfx::ResourceState::COLOR_ATTACHMENT_WRITE)};
                recorder.barrier({.image_barriers = to_attachment});
#if GFX_EXAMPLE_WITH_UI
                ui_renderer.prepare(recorder, ui);
#endif
                {
                    const std::array<float, 4> background = game.menu ? std::array{0.01F, 0.01F, 0.01F, 1.0F} : std::array{0.02F, 0.03F, 0.08F, 1.0F};
                    const std::array attachments{gfx::ColorAttachment{.view = image->image.view, .clear_color = background}};
                    const gfx::Rectangle render_area{.width = extent.width, .height = extent.height};
                    gfx::RenderingScope scope = recorder.begin_rendering({
                        .area = render_area,
                        .color_attachments = attachments,
                        .debug_name = "scene",
                    });
                    const std::array<float, 4> color = game.menu ? std::array{0.2F, 0.2F, 0.2F, 1.0F} : std::array{1.0F, 0.3F, 0.02F, 1.0F};
                    const std::array square{gfx::ClearAttachment{.color = color}};
                    const std::array area{gfx::ClearRectangle{.area = {.x = left, .y = top, .width = side, .height = side}}};
                    scope.clear_attachments(square, area);
#if GFX_EXAMPLE_WITH_UI
                    ui_renderer.record(scope, ui);
#endif
                }
                const std::array to_present{
                    gfx::image_barrier(image->image.image, gfx::ResourceState::COLOR_ATTACHMENT_WRITE, gfx::ResourceState::PRESENT)};
                recorder.barrier({.image_barriers = to_present});
            };

            // With the GPU time of the whole frame when the profiler exists (it reads the frame the GPU finished).
            if (profiler)
            {
                profiler->begin_frame(frame_number, recorder);
                metrics.set_gpu_timings(profiler->timings());
                const gfx::GpuProfilerScope timing = profiler->scope(recorder, "frame");
                record();
            }
            else
            {
                record();
            }
        }
        metrics.count(commands);

        // Submit: wait for the image, signal the presentation and the frame timeline; then present.
        const std::array command_buffers{commands.handle()};
        const std::array waits{gfx::SemaphoreWait{.semaphore = image->acquired}};
        const std::array signals{gfx::SemaphoreSignal{.semaphore = image->rendered}};
        const std::array timeline_signals{timeline.frame_signal()};
        const std::array batches{gfx::SubmitDescription{
            .command_buffers = command_buffers,
            .waits = waits,
            .signals = signals,
            .timeline_signals = timeline_signals,
        }};
        queue.submit(batches);
        (void)presentation.present(queue, *image);
        ++presented;
    }

    // The GPU finishes before anything is destroyed.
    device->wait_idle();
    std::println("{} frames presented, {} swapchain recreations, {} validation errors", presented, presentation.recreation_count(),
                 validation_errors.count());
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
