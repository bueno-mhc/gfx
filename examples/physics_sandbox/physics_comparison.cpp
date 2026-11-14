// The physics comparison of gfx: one large scene built into two worlds of two physics backends — Box3D on the left and
// the GPU backend (XPBD in compute shaders) on the right by default — stepped and drawn side by side with one camera, each
// step's wall time measured and plotted. By default the worlds step in lockstep, one after the other: each frame, the
// left world takes one step, then the right one, so both show the same simulated time and neither step shares the
// processor with the other (their times compare). With --real-time, each world advances by the real time a frame takes,
// both at once, as an application would run it: the slower one falls behind (it drops the steps it cannot catch up).
//
// Mouse: right drag turns the camera, middle drag pans it, the wheel zooms. Keys: Space shoots a ball into both worlds
// along the pointer's ray (from either half), R builds the scene again, P pauses, N takes a single step while paused,
// Escape quits. The panel picks the scene, its size and the stepping.
//
// Usage: gfx_physics_comparison [--backend sdl3|glfw] [--scene pyramid|pile|wrecking|chains|cloth] [--size <factor>]
//                               [--left box3d|reference|gpu_xpbd] [--right box3d|reference|gpu_xpbd] [--real-time]
//                               [--no-sleep] [--device <index>] [--threads <count>] [--frames <count>] [--validation]
//                               [--trace <file.json>]
//   --scene S      the scene (pyramid by default): a square pyramid of boxes, a pile poured into a bin, a brick wall hit
//                  by a wrecking ball, rows of swinging chains, a large cloth
//   --size F       scales every count of the scene (1 by default: about two thousand bodies, 6 400 cloth particles)
//   --left B       the left world's backend (Box3D by default); --right B the right one's (the GPU backend by default,
//                  on the program's own device)
//   --real-time    each world advances by the real time of a frame, both at once (default: one step each per frame)
//   --no-sleep     bodies never sleep (the panel switches it): resting bodies keep being simulated, so a scene at rest
//                  still measures each backend's work (with sleep, a world at rest costs almost nothing)
//   --frames N     closes after N frames and prints how the steps compared; the window then does not take the focus
//   --validation   enables the Vulkan validation layers; the exit code is 1 if they report an error
//   --trace F      records every frame with the profiler (each world's step, the worlds' own scopes, the GPU backend's
//                  GPU scopes) and writes them to F as a Chrome trace at the end (builds with the profiler)
#include <gfx/profiler.hpp>   // the profiler's macros (before the imports)
import std;
import gfx.profiler;            // --trace: the frames recorded
import gfx.platform.graphics;   // gfx (graphics), gfx.platform (windows and input), WindowSwapchain
import gfx.ui;                  // the panel and the labels
import gfx.ui.platform;         // the UI's input and services from the window
import gfx.camera;              // the camera
import physics_sandbox.font;    // the UI's font atlas, built by gfx_add_ui_atlas()
import physics_sandbox.renderer;
import physics_sandbox.scene;
import physics_sandbox.large_scenes;
import physics_sandbox.interaction;
#if GFX_SANDBOX_GPU_XPBD
import gfx.physics.gpu_xpbd;    // the GPU backend's direct entry: its worlds on the program's own device
#endif

namespace
{

using namespace physics_sandbox;
using namespace gfx::platform::literals;
using namespace std::chrono_literals;
namespace physics = gfx::physics;
namespace camera = gfx::camera;
namespace platform = gfx::platform;

// ------------------------------------------------------------------------------------------------- input

/// The comparison's actions, in one context.
constexpr auto INPUT = platform::make_input_schema(
    platform::actions(platform::action("turn"),
                      platform::action("pan"),
                      platform::action("look_x", platform::ActionValueType::AXIS_1D),
                      platform::action("look_y", platform::ActionValueType::AXIS_1D),
                      platform::action("wheel", platform::ActionValueType::AXIS_1D),
                      platform::action("shoot"),
                      platform::action("reset"),
                      platform::action("pause"),
                      platform::action("step"),
                      platform::action("quit")),
    platform::context("comparison",
                      platform::bind("turn"_action, platform::MouseButton::RIGHT),
                      platform::bind("pan"_action, platform::MouseButton::MIDDLE),
                      platform::bind("look_x"_action, platform::MouseAxis::MOTION_X),
                      platform::bind("look_y"_action, platform::MouseAxis::MOTION_Y),
                      platform::bind("wheel"_action, platform::MouseAxis::WHEEL_Y),
                      platform::bind("shoot"_action, platform::Key::SPACE),
                      platform::bind("reset"_action, platform::Key::R),
                      platform::bind("pause"_action, platform::Key::P),
                      platform::bind("step"_action, platform::Key::N),
                      platform::bind("quit"_action, platform::Key::ESCAPE)));

// ------------------------------------------------------------------------------------------------- settings

constexpr std::string_view TITLE = "gfx physics comparison";
constexpr gfx::Extent INITIAL_SIZE{.width = 1800, .height = 900, .depth = 1};
constexpr std::uint32_t FRAMES_IN_FLIGHT = 2;
constexpr float STEP = 1.0F / 60.0F;           ///< A step's simulated time.
constexpr float TURN_RADIANS_PER_PIXEL = 0.006F;
constexpr float SHOT_SPEED = 60.0F;            ///< Meters per second.
constexpr float INTERFACE_FONT_SIZE = 15.0F;   ///< UI units (points) per em.
constexpr std::size_t PLOTTED_STEPS = 240;     ///< Step times plotted (and averaged).
constexpr std::uint32_t DIVIDER = 2;           ///< Pixels between the halves.

/// How the worlds advance.
enum class Stepping : std::uint8_t
{
    LOCKSTEP,  ///< One step each per frame, one world after the other.
    REAL_TIME, ///< Each by the frame's real time, both at once.
};

/// The command line.
struct Options
{
    std::optional<platform::PlatformBackendKind> backend{};
    LargeScene scene = LargeScene::PYRAMID;
    float size = 1.0F;
    physics::PhysicsBackendKind left = physics::PhysicsBackendKind::BOX3D;
    physics::PhysicsBackendKind right = physics::PhysicsBackendKind::GPU_XPBD;
    Stepping stepping = Stepping::LOCKSTEP;
    bool sleep = true;
    std::optional<std::size_t> physical_device{};
    std::uint32_t threads = std::max(1U, std::thread::hardware_concurrency() / 2U);
    std::optional<std::uint64_t> frames{};
    bool validation = false;
    std::optional<std::string> trace{};
};

/// @p name in capitals (enumerator names).
auto
upper(std::string_view name) -> std::string
{
    std::string result{name};
    std::ranges::transform(result, result.begin(), [](char character) { return static_cast<char>(std::toupper(static_cast<unsigned char>(character))); });
    return result;
}

/// A physics backend as the program names it.
auto
backend_label(physics::PhysicsBackendKind backend) -> std::string_view
{
    switch (backend)
    {
        case physics::PhysicsBackendKind::BOX3D:
            return "Box3D";
        case physics::PhysicsBackendKind::GPU_XPBD:
            return "GPU XPBD";
        case physics::PhysicsBackendKind::REFERENCE:
            return "reference";
        default:
            return "other";
    }
}

/// The command line, or std::nullopt after printing the usage.
auto
parse_options(std::span<char* const> arguments) -> std::optional<Options>
{
    Options options;
    const auto backend = [](std::string_view name) -> std::optional<physics::PhysicsBackendKind> {
        const std::optional<physics::PhysicsBackendKind> kind = gfx::from_string<physics::PhysicsBackendKind>(upper(name));
        if (!kind)
        {
            std::println(std::cerr, "unknown physics backend '{}'", name);
        }
        return kind;
    };
    for (std::size_t index = 1; index < arguments.size(); ++index)
    {
        const std::string_view argument = arguments[index];
        const bool has_value = index + 1 < arguments.size();
        if (argument == "--backend" && has_value)
        {
            options.backend = gfx::from_string<platform::PlatformBackendKind>(upper(arguments[++index]));
            if (!options.backend)
            {
                std::println(std::cerr, "unknown platform backend '{}'", arguments[index]);
                return std::nullopt;
            }
        }
        else if (argument == "--scene" && has_value)
        {
            const std::optional<LargeScene> scene = find_large_scene(arguments[++index]);
            if (!scene)
            {
                std::println(std::cerr, "unknown scene '{}'", arguments[index]);
                return std::nullopt;
            }
            options.scene = *scene;
        }
        else if (argument == "--size" && has_value)
        {
            const std::string_view text = arguments[++index];
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), options.size);
            if (error != std::errc{} || end != text.data() + text.size() || !(options.size > 0.0F) || options.size > 64.0F)
            {
                std::println(std::cerr, "--size needs a factor in (0, 64]: '{}'", text);
                return std::nullopt;
            }
        }
        else if ((argument == "--left" || argument == "--right") && has_value)
        {
            const std::optional<physics::PhysicsBackendKind> kind = backend(arguments[++index]);
            if (!kind)
            {
                return std::nullopt;
            }
            (argument == "--left" ? options.left : options.right) = *kind;
        }
        else if (argument == "--real-time")
        {
            options.stepping = Stepping::REAL_TIME;
        }
        else if (argument == "--no-sleep")
        {
            options.sleep = false;
        }
        else if (argument == "--trace" && has_value)
        {
            options.trace = arguments[++index];
        }
        else if (argument == "--device" && has_value)
        {
            options.physical_device = std::stoul(arguments[++index]);
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
        else
        {
            std::println(std::cerr, "usage: gfx_physics_comparison [--backend sdl3|glfw] [--scene pyramid|pile|wrecking|chains|cloth] [--size <factor>] "
                                    "[--left <physics backend>] [--right <physics backend>] [--real-time] [--no-sleep] [--device <index>] "
                                    "[--threads <count>] [--frames <count>] [--validation] [--trace <file.json>]");
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

/// --trace: a profiler frame for the scope of a frame of the program (whatever way the frame ends).
class TracedFrame
{
public:
    TracedFrame(gfx::profiler::Profiler* recorder,
                gfx::profiler::SessionHandle session)
        : m_recorder{recorder}
        , m_session{session}
    {
        if (m_recorder != nullptr)
        {
            m_recorder->begin_frame(m_session);
        }
    }

    TracedFrame(const TracedFrame&) = delete;
    auto operator=(const TracedFrame&) -> TracedFrame& = delete;

    ~TracedFrame()
    {
        if (m_recorder != nullptr)
        {
            (void)m_recorder->end_frame(m_session);
        }
    }

private:
    gfx::profiler::Profiler* m_recorder = nullptr;
    gfx::profiler::SessionHandle m_session{};
};

// ------------------------------------------------------------------------------------------------- a side

/// One world, its renderer and its measurements.
struct Side
{
    physics::PhysicsBackendKind backend = physics::PhysicsBackendKind::BOX3D;
    std::unique_ptr<SceneRenderer> renderer{};
    std::unique_ptr<Scene> scene{};
    LargeSceneResult built{};
    std::vector<float> step_milliseconds = std::vector<float>(PLOTTED_STEPS, 0.0F); ///< The latest steps' wall times (a ring).
    std::uint64_t timed_steps = 0;                                                  ///< Steps measured since the scene was built.
    std::uint64_t counted_steps = 0;                                                ///< The world's step count when last looked at.
    std::vector<double> all_steps{};                                                ///< Every step's wall time (the summary).

    /// Takes the wall time of the step just completed, if one did.
    void measure()
    {
        if (scene->step_count() == counted_steps)
        {
            return;
        }
        counted_steps = scene->step_count();
        const double milliseconds = scene->statistics().wall_seconds * 1000.0;
        step_milliseconds[timed_steps % PLOTTED_STEPS] = static_cast<float>(milliseconds);
        all_steps.push_back(milliseconds);
        ++timed_steps;
    }

    /// The mean of the plotted steps (the latest PLOTTED_STEPS), in milliseconds.
    [[nodiscard]] auto recent_mean() const -> double
    {
        const std::size_t count = std::min<std::uint64_t>(timed_steps, PLOTTED_STEPS);
        if (count == 0)
        {
            return 0.0;
        }
        double total = 0.0;
        for (std::size_t index = 0; index < count; ++index)
        {
            total += step_milliseconds[index];
        }
        return total / static_cast<double>(count);
    }

    /// The plot's values, oldest first.
    void plot(std::array<float, PLOTTED_STEPS>& values) const
    {
        for (std::size_t index = 0; index < PLOTTED_STEPS; ++index)
        {
            values[index] = step_milliseconds[(timed_steps + index) % PLOTTED_STEPS];
        }
    }

    /// The highest point of the dynamic bodies (a pyramid or a wall standing keeps it), meters.
    [[nodiscard]] auto highest() const -> float
    {
        float top = 0.0F;
        for (const SceneBody& body : scene->bodies())
        {
            if (body.kind == physics::BodyKind::DYNAMIC)
            {
                top = std::max(top, body.current.position.y);
            }
        }
        return top;
    }
};

/// Turns sleeping on or off in @p scene's world (after its step in flight); turned off, every dynamic body is woken.
void
set_sleeping(Scene& scene,
             bool enabled)
{
    scene.finish();
    physics::SleepDescription sleep = scene.world().description().sleep;
    sleep.enabled = enabled;
    scene.world().set_sleep_description(sleep);
    if (!enabled)
    {
        for (const SceneBody& body : scene.bodies())
        {
            if (body.kind == physics::BodyKind::DYNAMIC)
            {
                body.body->wake_up();
            }
        }
    }
}

/// Adds @p scene's bodies and soft bodies to @p renderer at their drawn poses; distance joints as lines. @p positions is
/// scratch storage.
void
add_scene(SceneRenderer& renderer,
          const Scene& scene,
          std::vector<gfx::Vector3>& positions)
{
    for (const SceneBody& body : scene.bodies())
    {
        const gfx::Transform pose = scene.drawn_pose(body);
        for (const SceneCollider& collider : body.colliders)
        {
            renderer.add(collider.shape, pose * collider.pose, body.color, body.flags);
        }
    }
    for (const SceneSoftBody& soft_body : scene.soft_bodies())
    {
        scene.drawn_positions(soft_body, positions);
        if (!soft_body.surface.empty())
        {
            renderer.add_surface(&soft_body, soft_body.surface, positions, soft_body.color);
            continue;
        }
        for (const gfx::Vector3& position : positions)
        {
            renderer.add(soft_body.particle, {.position = position}, soft_body.color);
        }
    }
    for (const SceneJoint& joint : scene.joints())
    {
        // Cables only (the wrecking ball's rope), checked before the bodies are searched for (a search per joint over
        // the scene's bodies took 220 ms a frame for the chains' 7 684 joints).
        if (!joint.cable)
        {
            continue;
        }
        const SceneBody* first = scene.find(joint.first);
        const SceneBody* second = joint.second.is_valid() ? scene.find(joint.second) : nullptr;
        if (first == nullptr || (joint.second.is_valid() && second == nullptr))
        {
            continue;
        }
        const gfx::Vector3 from = gfx::transform_point(scene.drawn_pose(*first), joint.anchor_first);
        const gfx::Vector3 to = second != nullptr ? gfx::transform_point(scene.drawn_pose(*second), joint.anchor_second) : joint.anchor_second;
        renderer.lines().add_line(from, to, {.red = 210, .green = 210, .blue = 200});
    }
}

/// The halves of an image of @p extent: the left one and the right one, DIVIDER pixels apart.
auto
halves(gfx::Extent extent) -> std::array<gfx::Rectangle, 2>
{
    const std::uint32_t left = (extent.width - std::min(extent.width, DIVIDER)) / 2;
    const std::uint32_t right_x = std::min(extent.width, left + DIVIDER);
    return {gfx::Rectangle{.width = std::max(left, 1U), .height = extent.height},
            gfx::Rectangle{.x = static_cast<std::int32_t>(right_x), .width = std::max(extent.width - right_x, 1U), .height = extent.height}};
}

// ------------------------------------------------------------------------------------------------- the panel

/// What the panel asks for.
struct PanelRequest
{
    std::optional<LargeScene> scene{};
    std::optional<float> size{};
    bool reset = false;
};

/// The panel: the scene, its size, the stepping, and how each side's steps compare.
auto
comparison_panel(gfx::ui::UiFrame& frame,
                 std::span<Side, 2> sides,
                 LargeScene scene,
                 float size,
                 Stepping& stepping,
                 bool& sleeping,
                 bool& paused,
                 double frame_seconds) -> PanelRequest
{
    PanelRequest request;
    frame.set_next_window_position({.x = 12, .y = 40}, gfx::ui::UiCondition::FIRST_USE_EVER);
    if (frame.begin_window("Comparison", gfx::ui::UiWindowFlag::ALWAYS_AUTO_RESIZE))
    {
        std::array<std::string_view, LARGE_SCENE_COUNT> names{};
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            names[index] = large_scene_information(static_cast<LargeScene>(index)).name;
        }
        std::size_t selected = std::to_underlying(scene);
        if (frame.combo("scene", selected, names))
        {
            request.scene = static_cast<LargeScene>(selected);
        }
        std::array<std::string, LARGE_SCENE_SIZES.size()> size_names{};
        std::size_t size_index = LARGE_SCENE_SIZES.size();
        for (std::size_t index = 0; index < LARGE_SCENE_SIZES.size(); ++index)
        {
            size_names[index] = std::format("size {}", LARGE_SCENE_SIZES[index]);
            size_index = LARGE_SCENE_SIZES[index] == size ? index : size_index;
        }
        if (size_index == LARGE_SCENE_SIZES.size())
        {
            frame.text("size {} (command line)", size);
            size_index = 0;
        }
        else if (frame.combo("size", size_index, size_names))
        {
            request.size = LARGE_SCENE_SIZES[size_index];
        }
        if (frame.radio_button("lockstep", stepping == Stepping::LOCKSTEP))
        {
            stepping = Stepping::LOCKSTEP;
        }
        frame.same_line();
        if (frame.radio_button("real time", stepping == Stepping::REAL_TIME))
        {
            stepping = Stepping::REAL_TIME;
        }
        frame.same_line();
        static_cast<void>(frame.checkbox("sleeping", sleeping));
        frame.same_line();
        static_cast<void>(frame.checkbox("paused (P)", paused));
        frame.same_line();
        request.reset = frame.button("reset (R)");
        frame.text_unformatted(large_scene_information(scene).description);
        const LargeSceneResult& built = sides[0].built;
        frame.text("{} bodies, {} particles, {} joints in each world", built.bodies, built.particles, built.joints);
        frame.separator();

        // Each side: its backend, the last step and the latest steps' mean, their plot, the world's state.
        for (Side& side : sides)
        {
            const physics::StepStatistics step = side.scene->statistics();
            frame.text("{}: step {:.2f} ms (mean of the last {}: {:.2f} ms)", backend_label(side.backend), step.wall_seconds * 1000.0,
                       std::min<std::uint64_t>(side.timed_steps, PLOTTED_STEPS), side.recent_mean());
            std::array<float, PLOTTED_STEPS> values{};
            side.plot(values);
            const std::string overlay = std::format("slowest {:.2f} ms", std::ranges::max(values));
            frame.plot_lines(std::format("##{}", backend_label(side.backend)), values, {.overlay = overlay, .minimum = 0.0F, .size = {.x = 360, .y = 44}});
            frame.text("  {} steps ({:.1f} s simulated), {} awake, {} contact points, highest body {:.2f} m", side.scene->step_count(),
                       static_cast<double>(side.scene->step_count()) * STEP, step.awake_bodies, step.contacts, side.highest());
        }
        frame.separator();
        const double left = sides[0].recent_mean();
        const double right = sides[1].recent_mean();
        if (left > 0.0 && right > 0.0)
        {
            const bool right_faster = right < left;
            frame.text("{} steps {:.2f}x faster than {}", backend_label(right_faster ? sides[1].backend : sides[0].backend),
                       right_faster ? left / right : right / left, backend_label(right_faster ? sides[0].backend : sides[1].backend));
        }
        frame.text("frame {:.1f} ms; {}", frame_seconds * 1000.0,
                   stepping == Stepping::LOCKSTEP ? "a step each per frame, one world after the other" : "each world by the frame's time, both at once");
        frame.text_unformatted("right drag: turn   middle drag: pan   wheel: zoom");
        frame.text_unformatted("Space: shoot into both   N: a step while paused   Esc: quit");
    }
    frame.end_window();
    return request;
}

/// A label at the top of each half: the backend and its latest steps' mean.
void
side_labels(gfx::ui::UiFrame& frame,
            std::span<const Side, 2> sides,
            float width)
{
    for (std::size_t index = 0; index < sides.size(); ++index)
    {
        const float center = (static_cast<float>(index) + 0.5F) * 0.5F * width;
        frame.set_next_window_position({.x = center, .y = 8.0F}, gfx::ui::UiCondition::ALWAYS, {.x = 0.5F, .y = 0.0F});
        frame.set_next_window_background_alpha(0.55F);
        const std::string name = std::format("##side{}", index);
        if (frame.begin_window(name, gfx::ui::UiWindowFlag::NO_DECORATION | gfx::ui::UiWindowFlag::ALWAYS_AUTO_RESIZE | gfx::ui::UiWindowFlag::NO_INPUTS |
                                         gfx::ui::UiWindowFlag::NO_SAVED_SETTINGS | gfx::ui::UiWindowFlag::NO_FOCUS_ON_APPEARING))
        {
            frame.text("{}   {:.2f} ms a step", backend_label(sides[index].backend), sides[index].recent_mean());
        }
        frame.end_window();
    }
}

// ------------------------------------------------------------------------------------------------- the program

auto
run(const Options& options) -> int
{
    // The window.
    const std::vector<platform::PlatformBackendKind> backends = platform::available_platform_backends();
    if (backends.empty())
    {
        std::println(std::cerr, "no platform backend is available: {}", platform::platform_backend_availability(platform::PlatformBackendKind::SDL3).reason);
        return 1;
    }
    for (const physics::PhysicsBackendKind backend : {options.left, options.right})
    {
        const physics::PhysicsBackendAvailability availability = physics::physics_backend_availability(backend);
        if (!availability.available)
        {
            std::println(std::cerr, "the {} physics backend is unavailable: {}", backend, availability.reason);
            return 1;
        }
    }
    const std::unique_ptr<platform::Platform> host = platform::create_platform(options.backend.value_or(backends.front()), {.application_name = TITLE});
    platform::WindowDescription window_description;
    window_description.title = TITLE;
    window_description.size = INITIAL_SIZE;
    window_description.minimum_size = gfx::Extent{.width = 640, .height = 270, .depth = 1};
    window_description.surface_api = platform::SurfaceApi::VULKAN;
    if (options.frames)
    {
        window_description.flags &= ~platform::WindowFlag::FOCUS_ON_SHOW; // automated runs leave the focus where it is
    }
    const std::unique_ptr<platform::Window> window = host->create_window(window_description);

    // The GPU: a graphics queue for the frames, a compute queue for the GPU backend's steps.
    ValidationErrors validation_errors;
    const std::unique_ptr<gfx::Instance> instance = gfx::create_instance(
        gfx::BackendKind::VULKAN, {.application_name = TITLE,
                                   .enable_validation = options.validation,
                                   .debug_message_severities = gfx::DebugMessageSeverity::WARNING | gfx::DebugMessageSeverity::CRITICAL,
                                   .debug_callback = [&validation_errors](const gfx::DebugMessage& message) { validation_errors.report(message); }});
    gfx::PhysicalDevice& physical_device =
        options.physical_device ? instance->physical_device(*options.physical_device) : instance->select_physical_device();
    static constexpr std::array QUEUES{gfx::QueueRequest{.type = gfx::QueueType::GRAPHICS, .count = 1, .priority = 1.0F},
                                       gfx::QueueRequest{.type = gfx::QueueType::COMPUTE, .count = 1, .priority = 1.0F}};
    const std::unique_ptr<gfx::Device> device = physical_device.create_device({.queues = QUEUES, .debug_name = "comparison device"});
    gfx::Queue& queue = device->queue(gfx::QueueType::GRAPHICS);
    const gfx::SurfaceSupport support = physical_device.surface_support(window->native_handle());
    platform::WindowSwapchain presentation{*device, *window, {.format = choose_format(support), .usage = gfx::ImageUsage::COLOR_ATTACHMENT, .debug_name = "comparison swapchain"}};
    const gfx::DeviceLimits& limits = physical_device.properties().limits;
    const gfx::SampleCount samples = gfx::has_all(limits.color_sample_counts, gfx::SampleCount::FOUR) && gfx::has_all(limits.depth_sample_counts, gfx::SampleCount::FOUR)
                                         ? gfx::SampleCount::FOUR
                                         : gfx::SampleCount::ONE;
    gfx::FrameTimeline timeline{*device, FRAMES_IN_FLIGHT};
    gfx::FrameCommandPools command_pools{*device, {.queue_type = gfx::QueueType::GRAPHICS, .debug_name = "comparison commands"}, timeline.timeline(),
                                         FRAMES_IN_FLIGHT};

    // The UI.
    gfx::ui::UiWindowServices ui_services{*host, *window};
    gfx::ui::UiContext ui{{.services = &ui_services}};
    ui.load_font(gfx::ui::FontAtlas::load(sandbox_font), INTERFACE_FONT_SIZE);
    gfx::ui::UiPlatformInput ui_input{*window};
    gfx::ui::UiRenderer ui_renderer{*device, ui.fonts(), {.color_format = presentation.format(), .frames_in_flight = FRAMES_IN_FLIGHT}};

    // The camera both halves share, and the worlds: the GPU backend's on this device, the others through the factory.
    const std::unique_ptr<camera::Camera> view_camera = camera::create_camera(
        {.projection = camera::PerspectiveProjection{.vertical_field_of_view = 0.9F, .aspect_ratio = 1.0F, .near_plane = 0.05F,
                                                     .far_plane = camera::INFINITE_FAR_PLANE},
         .clip = {.reversed_depth = true, .y_axis = camera::NormalizedDeviceYAxis::DOWN}});
    CameraRig rig{camera::OrbitControllerDescription{.distance = 30.0F, .maximum_distance = 500.0F}};
    physics::ThreadTaskScheduler scheduler{options.threads};
    const WorldFactory make_world = [&device](const physics::PhysicsWorldDescription& description, physics::PhysicsBackendKind backend) {
#if GFX_SANDBOX_GPU_XPBD
        if (backend == physics::PhysicsBackendKind::GPU_XPBD)
        {
            return physics::gpu_xpbd::create_physics_world(description, *device);
        }
#else
        static_cast<void>(device);
#endif
        return physics::create_physics_world(description, backend);
    };
    std::array<Side, 2> sides{Side{.backend = options.left}, Side{.backend = options.right}};
    for (Side& side : sides)
    {
        side.renderer = std::make_unique<SceneRenderer>(*device, SceneRendererDescription{.color_format = presentation.format(),
                                                                                          .samples = samples,
                                                                                          .frames_in_flight = FRAMES_IN_FLIGHT,
                                                                                          .frame_bytes = 16ULL << 20U});
    }
    std::println("{} window on '{}': {} on the left, {} on the right, {} scheduler threads, sleeping {}", host->backend(),
                 physical_device.properties().name, backend_label(options.left), backend_label(options.right), options.threads,
                 options.sleep ? "on" : "off");

    // A scene: built again into both worlds (the old ones finished and their meshes forgotten first).
    LargeScene scene = options.scene;
    float size = options.size;
    bool sleeping = options.sleep;
    const auto build = [&](LargeScene next,
                           float next_size)
    {
        for (Side& side : sides)
        {
            if (side.scene != nullptr)
            {
                side.scene->finish();
            }
        }
        device->wait_idle();
        for (Side& side : sides)
        {
            side.scene.reset();
            side.renderer->clear_meshes();
            const auto started = std::chrono::steady_clock::now();
            side.scene = std::make_unique<Scene>(side.backend, scheduler, make_world);
            side.built = build_large_scene(next, *side.scene, next_size);
            if (!sleeping)
            {
                set_sleeping(*side.scene, false);
            }
            side.step_milliseconds.assign(PLOTTED_STEPS, 0.0F);
            side.timed_steps = 0;
            side.counted_steps = side.scene->step_count();
            side.all_steps.clear();
            std::println("{} at size {} on {}: {} bodies, {} particles, {} joints, built in {:.0f} ms", large_scene_information(next).name, next_size,
                         backend_label(side.backend), side.built.bodies, side.built.particles, side.built.joints,
                         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        }
        const PresetView& view = sides[0].built.view;
        rig.aim(view.target, view.distance, view.yaw, view.pitch);
        scene = next;
        size = next_size;
    };
    build(scene, size);

    // --trace: every frame recorded from here (the worlds' threads and the GPU backend's GPU scopes too).
    std::unique_ptr<gfx::profiler::Profiler> trace_recorder;
    gfx::profiler::SessionHandle trace_session{};
    if (options.trace)
    {
        if constexpr (!gfx::profiler::PROFILER_ENABLED)
        {
            std::println(std::cerr, "--trace needs a build with the profiler (GFX_PROFILER on): nothing is recorded");
        }
        trace_recorder = gfx::profiler::create_profiler({.ring_capacity = 1U << 20U});
        gfx::profiler::set_current_profiler(trace_recorder.get());
        gfx::profiler::set_thread_name("comparison main");
        trace_session = trace_recorder->create_session({.name = "physics comparison"});
    }

    // The loop.
    bool sleeping_applied = sleeping;
    platform::InputSystem input{INPUT};
    const platform::ContextActivation context = input.activate_context("comparison"_context);
    std::vector<gfx::Vector3> positions;
    Stepping stepping = options.stepping;
    bool paused = false;
    bool running = true;
    bool ui_wants_mouse = false;
    bool ui_wants_keyboard = false;
    std::uint64_t presented = 0;
    double frame_seconds = 0.0;
    double ui_time = 0.0;
    PanelRequest request;
    auto previous_time = std::chrono::steady_clock::now();
    while (running && (!options.frames || presented < *options.frames))
    {
        const TracedFrame traced{trace_recorder.get(), trace_session};

        // Events, then a frame of actions.
        const platform::EventSpan events = window->is_minimized() ? host->wait_events_timeout(50ms) : host->poll_events();
        ui_input.begin_frame();
        ui_input.add_events(events);
        for (const platform::Event& event : events)
        {
            presentation.handle_event(event);
            if (event.window() == window->handle() && event.type() == platform::EventType::WINDOW_CLOSE_REQUESTED)
            {
                running = false;
            }
        }
        const platform::InputFrame& frame = input.update(events);
        running = running && !frame.was_action_just_pressed("quit"_action);
        const auto now = std::chrono::steady_clock::now();
        frame_seconds = std::chrono::duration<double>(now - previous_time).count();
        previous_time = now;
        ui_time += frame_seconds;

        // A new scene (the panel or R), pausing, a single step.
        if (!ui_wants_keyboard)
        {
            request.reset = request.reset || frame.was_action_just_pressed("reset"_action);
            paused = paused != frame.was_action_just_pressed("pause"_action);
        }
        const bool single_step = !ui_wants_keyboard && paused && frame.was_action_just_pressed("step"_action);
        if (request.scene || request.size || request.reset)
        {
            build(request.scene.value_or(scene), request.size.value_or(size));
        }
        request = {};
        if (sleeping != sleeping_applied)
        {
            for (Side& side : sides)
            {
                set_sleeping(*side.scene, sleeping);
            }
            sleeping_applied = sleeping;
        }

        // The camera (one for both halves, each half's aspect), turned and moved by the mouse outside the panel.
        const gfx::Extent extent = presentation.extent();
        const std::array<gfx::Rectangle, 2> areas = halves(extent);
        FrameCommands commands{};
        const gfx::Extent window_size = window->size();
        const gfx::Extent pixels = window->framebuffer_size();
        const float scale = window_size.width > 0 ? static_cast<float>(pixels.width) / static_cast<float>(window_size.width) : 1.0F;
        commands.pointer = {.x = frame.mouse_position().x * scale, .y = frame.mouse_position().y * scale};
        if (!ui_wants_mouse)
        {
            const float dx = frame.get_action_value("look_x"_action);
            const float dy = frame.get_action_value("look_y"_action);
            if (frame.is_action_pressed("turn"_action))
            {
                commands.look = {.x = dx * TURN_RADIANS_PER_PIXEL, .y = -dy * TURN_RADIANS_PER_PIXEL};
            }
            if (frame.is_action_pressed("pan"_action))
            {
                const float height = std::max(1.0F, static_cast<float>(window_size.height));
                commands.pan = {.x = -dx / height, .y = dy / height};
            }
            commands.zoom = frame.get_action_value("wheel"_action);
        }
        rig.update(*view_camera, commands, static_cast<float>(frame_seconds));
        view_camera->set_aspect_ratio(static_cast<float>(areas[0].width) / static_cast<float>(std::max(areas[0].height, 1U)));

        // A shot into both worlds, along the ray through the pointer in whichever half it is.
        if (!ui_wants_keyboard && frame.was_action_just_pressed("shoot"_action))
        {
            const gfx::Rectangle& area = commands.pointer.x >= static_cast<float>(areas[1].x) ? areas[1] : areas[0];
            const physics::Ray ray = view_camera->screen_to_world_ray(commands.pointer, {.x = static_cast<float>(area.x),
                                                                                          .y = static_cast<float>(area.y),
                                                                                          .width = static_cast<float>(area.width),
                                                                                          .height = static_cast<float>(area.height)});
            for (Side& side : sides)
            {
                side.scene->finish();
                static_cast<void>(side.scene->shoot(ray.origin + ray.direction * 0.6F, ray.direction * SHOT_SPEED));
            }
        }

        // The worlds: one step each, one after the other (lockstep), or each by the frame's time (real time); none while
        // paused but a single step.
        const auto step = [&](Side& side) {
            if (paused && !single_step)
            {
                side.scene->hold();
            }
            else if (stepping == Stepping::LOCKSTEP || single_step)
            {
                side.scene->step_once();
                side.scene->finish();
            }
            else
            {
                side.scene->advance(frame_seconds);
            }
            side.measure();
            side.scene->clear_events(); // the scene keeps the contact events until cleared (nothing here reads them)
        };
        {
            PROFILE_SCOPE("Comparison: the left world");
            step(sides[0]);
        }
        {
            PROFILE_SCOPE("Comparison: the right world");
            step(sides[1]);
        }

        // An image to draw into (none while the window is minimized).
        PROFILE_NAMED_SCOPE(acquire_scope, "Comparison: acquire an image");
        const std::optional<platform::SwapchainFrame> image = presentation.acquire();
        acquire_scope.end();
        if (!image)
        {
            continue;
        }
        PROFILE_NAMED_SCOPE(draw_scope, "Comparison: UI, drawing and submission");
        const std::uint64_t frame_number = timeline.begin_frame();
        command_pools.begin_frame(frame_number);
        ui_renderer.begin_frame(frame_number);
        for (Side& side : sides)
        {
            side.renderer->begin_frame(frame_number);
            add_scene(*side.renderer, *side.scene, positions);
        }

        // The UI: the panel and the labels.
        gfx::ui::UiFrame& ui_frame = ui.begin_frame(ui_input.frame_input(ui_time));
        request = comparison_panel(ui_frame, sides, scene, size, stepping, sleeping, paused, frame_seconds);
        side_labels(ui_frame, sides, static_cast<float>(extent.width) / std::max(ui_input.pixels_per_unit(), 0.01F));
        static_cast<void>(ui.end_frame());
        ui_wants_mouse = ui.wants_mouse();
        ui_wants_keyboard = ui.wants_keyboard();
        const gfx::ui::UiDrawData& ui_data = ui.draw_data();

        // Record: the image cleared (the divider), each side's pass into its half, the UI over both, the hand-over.
        gfx::CommandBuffer& commands_buffer = command_pools.command_buffer();
        {
            gfx::GraphicsRecorder recorder{commands_buffer};
            const std::array to_attachment{gfx::image_barrier(image->image.image, gfx::ResourceState::UNDEFINED, gfx::ResourceState::COLOR_ATTACHMENT_WRITE)};
            recorder.barrier({.image_barriers = to_attachment});
            for (Side& side : sides)
            {
                side.renderer->prepare(recorder);
            }
            ui_renderer.prepare(recorder, ui_data);
            {
                const std::array attachments{gfx::ColorAttachment{.view = image->image.view,
                                                                  .load = gfx::LoadOperation::CLEAR,
                                                                  .clear_color = {0.02F, 0.02F, 0.03F, 1.0F}}};
                gfx::RenderingScope clear = recorder.begin_rendering(
                    {.area = {.width = extent.width, .height = extent.height}, .color_attachments = attachments, .debug_name = "comparison divider"});
            }
            const std::array between{
                gfx::image_barrier(image->image.image, gfx::ResourceState::COLOR_ATTACHMENT_WRITE, gfx::ResourceState::COLOR_ATTACHMENT_WRITE)};
            for (std::size_t index = 0; index < sides.size(); ++index)
            {
                recorder.barrier({.image_barriers = between});
                sides[index].renderer->render(recorder, image->image.view, extent, {.view_projection = view_camera->view_projection()}, areas[index]);
            }
            const std::array to_interface{
                gfx::image_barrier(image->image.image, gfx::ResourceState::COLOR_ATTACHMENT_WRITE, gfx::ResourceState::COLOR_ATTACHMENT_READ_WRITE)};
            recorder.barrier({.image_barriers = to_interface});
            {
                const std::array attachments{gfx::ColorAttachment{.view = image->image.view, .load = gfx::LoadOperation::LOAD}};
                gfx::RenderingScope scope = recorder.begin_rendering(
                    {.area = {.width = extent.width, .height = extent.height}, .color_attachments = attachments, .debug_name = "comparison interface"});
                ui_renderer.record(scope, ui_data);
            }
            const std::array to_present{gfx::image_barrier(image->image.image, gfx::ResourceState::COLOR_ATTACHMENT_READ_WRITE, gfx::ResourceState::PRESENT)};
            recorder.barrier({.image_barriers = to_present});
        }
        const std::array command_buffers{commands_buffer.handle()};
        const std::array waits{gfx::SemaphoreWait{.semaphore = image->acquired}};
        const std::array signals{gfx::SemaphoreSignal{.semaphore = image->rendered}};
        const std::array timeline_signals{timeline.frame_signal()};
        const std::array batches{gfx::SubmitDescription{.command_buffers = command_buffers, .waits = waits, .signals = signals, .timeline_signals = timeline_signals}};
        queue.submit(batches);
        draw_scope.end();
        {
            PROFILE_SCOPE("Comparison: present");
            static_cast<void>(presentation.present(queue, *image));
        }
        ++presented;
    }

    // The steps' summary: each side's wall times (after the first ten steps), and how they compare.
    for (Side& side : sides)
    {
        side.scene->finish();
    }
    device->wait_idle();
    if (trace_recorder != nullptr)
    {
        const gfx::profiler::Trace trace = trace_recorder->finalize_session(trace_session);
        gfx::profiler::set_current_profiler(nullptr);
        gfx::profiler::export_trace(trace, gfx::profiler::TraceFormat::CHROME_JSON, *options.trace);
        std::println("{} frames traced into {}", trace.frames().size(), *options.trace);
    }
    std::array<double, 2> medians{};
    for (std::size_t index = 0; index < sides.size(); ++index)
    {
        std::vector<double> steps{sides[index].all_steps.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(10, sides[index].all_steps.size())),
                                  sides[index].all_steps.end()};
        std::ranges::sort(steps);
        const double mean = steps.empty() ? 0.0 : std::ranges::fold_left(steps, 0.0, std::plus{}) / static_cast<double>(steps.size());
        medians[index] = steps.empty() ? 0.0 : steps[steps.size() / 2];
        const physics::StepStatistics last = sides[index].scene->statistics();
        std::println("{:>10}: {} steps, mean {:.2f} ms, median {:.2f} ms, slowest {:.2f} ms; {} awake, {} contact points, highest body {:.2f} m",
                     backend_label(sides[index].backend), sides[index].scene->step_count(), mean, medians[index], steps.empty() ? 0.0 : steps.back(),
                     last.awake_bodies, last.contacts, sides[index].highest());
    }
    if (medians[0] > 0.0 && medians[1] > 0.0)
    {
        std::println("median step: {} / {} = {:.2f}", backend_label(sides[0].backend), backend_label(sides[1].backend), medians[0] / medians[1]);
    }
    std::println("{} frames presented, {} validation errors", presented, validation_errors.count());
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
