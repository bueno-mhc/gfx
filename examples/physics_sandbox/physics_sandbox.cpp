// The physics sandbox of gfx: every library at work in one window. A physics world (Box3D or the reference backend,
// gfx.physics) steps at 60 Hz on its own thread while the main thread reads the window's events (gfx.platform), turns
// them into actions (its input system), moves a camera (gfx.camera), lets the mouse pick, drag, shoot, spawn and remove
// bodies, builds a UI (gfx.ui) and draws the bodies as lit meshes with Vulkan (gfx), interpolated between the last two
// steps.
//
// Mouse: left drag drags a body by the point grabbed (the wheel pushes it away or pulls it closer), right drag turns the
// camera (orbit: around its target; fly: in place), middle drag pans the orbit's target, the wheel zooms. Keys: Space
// shoots a ball at the pointer, 1-7 spawn a sphere, capsule, cylinder, cone, crate, plinth or dumbbell there, Delete or X
// removes the body under the pointer, F switches between orbiting and flying (W A S D move, E and Q rise and sink),
// R builds the scene again, P pauses, N takes a single step while paused, Escape quits. The panel's sections pick the
// scene and the backend, change the simulation (slow motion, gravity, the solver, sleeping, the reference backend's broad
// phase and integrator) and turn on debug views (wireframe, bounding boxes, contacts, the broad phase's structure,
// joints, bodies colored by sleep or island).
//
// Usage: gfx_physics_sandbox [--backend sdl3|glfw] [--physics box3d|reference] [--preset <name>] [--views <list>]
//                            [--device <index>] [--threads <count>] [--frames <count>] [--script | --tour] [--validation]
//                            [--capture <file.ppm>] [--capture-frame <index>]
//   --preset P     starts with scene P (showcase, stacks, dominoes, chains, cloth, soft_volumes, friction, bouncing,
//                  triggers)
//   --views L      starts with the debug views of the comma-separated list L on (wireframe, boxes, contacts, broad_phase,
//                  joints, sleep, islands; the last two color the bodies)
//   --frames N     closes after N frames, one physics step per frame (the same frames on any machine); the window then
//                  does not take the focus
//   --script       drives the sandbox by a script instead of the mouse and keyboard (a grab and a drag, a shot, a spawn,
//                  a removal, a flight; then pausing, a single step, slow motion, gravity, the solver, the broad phase
//                  and the integrator, every view), then checks what happened: the exit code is 1 if a check fails
//   --tour         builds every preset on every physics backend in turn and checks each scene
//   --validation   enables the Vulkan validation layers; the exit code is 1 if they report an error
//   --capture F    writes the last frame (or the one --capture-frame counts from 0) to F (binary PPM)
import std;
import gfx.platform.graphics;   // gfx (graphics), gfx.platform (windows and input), WindowSwapchain
import gfx.ui;                  // the panels
import gfx.ui.platform;         // the UI's input and services from the window
import gfx.camera;              // the camera
import physics_sandbox.font;    // the UI's font atlas, built by gfx_add_ui_atlas()
import physics_sandbox.renderer;
import physics_sandbox.scene;
import physics_sandbox.presets;
import physics_sandbox.interaction;
import physics_sandbox.controls;

namespace
{

using namespace physics_sandbox;
using namespace gfx::platform::literals;
using namespace std::chrono_literals;
namespace physics = gfx::physics;
namespace camera = gfx::camera;
namespace platform = gfx::platform;

// ------------------------------------------------------------------------------------------------- input

/// The sandbox's actions, in one context.
constexpr auto INPUT = platform::make_input_schema(
    platform::actions(platform::action("grab"),
                      platform::action("turn"),
                      platform::action("pan"),
                      platform::action("look_x", platform::ActionValueType::AXIS_1D),
                      platform::action("look_y", platform::ActionValueType::AXIS_1D),
                      platform::action("wheel", platform::ActionValueType::AXIS_1D),
                      platform::action("shoot"),
                      platform::action("remove"),
                      platform::action("toggle_fly"),
                      platform::action("move", platform::ActionValueType::AXIS_2D),
                      platform::action("rise", platform::ActionValueType::AXIS_1D),
                      platform::action("spawn_sphere"),
                      platform::action("spawn_capsule"),
                      platform::action("spawn_cylinder"),
                      platform::action("spawn_cone"),
                      platform::action("spawn_crate"),
                      platform::action("spawn_plinth"),
                      platform::action("spawn_dumbbell"),
                      platform::action("reset"),
                      platform::action("pause"),
                      platform::action("step"),
                      platform::action("quit")),
    platform::context("sandbox",
                      platform::bind("grab"_action, platform::MouseButton::LEFT),
                      platform::bind("turn"_action, platform::MouseButton::RIGHT),
                      platform::bind("pan"_action, platform::MouseButton::MIDDLE),
                      platform::bind("look_x"_action, platform::MouseAxis::MOTION_X),
                      platform::bind("look_y"_action, platform::MouseAxis::MOTION_Y),
                      platform::bind("wheel"_action, platform::MouseAxis::WHEEL_Y),
                      platform::bind("shoot"_action, platform::Key::SPACE),
                      platform::bind("remove"_action, platform::Key::DELETE_FORWARD),
                      platform::bind("remove"_action, platform::Key::X),
                      platform::bind("toggle_fly"_action, platform::Key::F),
                      platform::bind("move"_action, platform::composite_2d(platform::Key::W, platform::Key::S, platform::Key::A, platform::Key::D)),
                      platform::bind("rise"_action, platform::composite_axis(platform::Key::Q, platform::Key::E)),
                      platform::bind("spawn_sphere"_action, platform::Key::NUMBER_1),
                      platform::bind("spawn_capsule"_action, platform::Key::NUMBER_2),
                      platform::bind("spawn_cylinder"_action, platform::Key::NUMBER_3),
                      platform::bind("spawn_cone"_action, platform::Key::NUMBER_4),
                      platform::bind("spawn_crate"_action, platform::Key::NUMBER_5),
                      platform::bind("spawn_plinth"_action, platform::Key::NUMBER_6),
                      platform::bind("spawn_dumbbell"_action, platform::Key::NUMBER_7),
                      platform::bind("reset"_action, platform::Key::R),
                      platform::bind("pause"_action, platform::Key::P),
                      platform::bind("step"_action, platform::Key::N),
                      platform::bind("quit"_action, platform::Key::ESCAPE)));

/// The spawn actions, in SpawnKind's order.
constexpr std::array<platform::ActionIdentifier, SPAWN_KIND_COUNT> SPAWN_ACTIONS{
    "spawn_sphere"_action, "spawn_capsule"_action, "spawn_cylinder"_action, "spawn_cone"_action,
    "spawn_crate"_action,  "spawn_plinth"_action,  "spawn_dumbbell"_action};

// ------------------------------------------------------------------------------------------------- settings

constexpr std::string_view TITLE = "gfx physics sandbox";
constexpr gfx::Extent INITIAL_SIZE{.width = 1600, .height = 900, .depth = 1};
constexpr std::uint32_t FRAMES_IN_FLIGHT = 2;
constexpr float TURN_RADIANS_PER_PIXEL = 0.006F;
constexpr float PUSH_METERS_PER_WHEEL_STEP = 0.5F;
constexpr float INTERFACE_FONT_SIZE = 15.0F; ///< UI units (points) per em.
constexpr std::size_t PLOTTED_STEPS = 180;   ///< Step times plotted.
constexpr float PANEL_WRAP_WIDTH = 400.0F;   ///< Where the panel's long lines wrap (UI units).
constexpr float PANEL_OFFSET = 0.3F;         ///< A scene's view turns around a point this fraction of its distance left of its target (the panel is on the left).
constexpr std::uint64_t TOUR_FRAMES = 90;    ///< Frames (steps) the tour spends in each scene.
constexpr float TOUR_LOWEST = -1.0F;         ///< A body or particle lower than this fell through the floor (its top is at 0).
constexpr float TOUR_FASTEST = 30.0F;        ///< Meters per second no body or particle of the presets reaches.

/// What the command line asks for.
struct Options
{
    std::optional<platform::PlatformBackendKind> backend{};
    physics::PhysicsBackendKind physics = physics::PhysicsBackendKind::BOX3D;
    Preset preset = Preset::SHOWCASE;
    std::optional<std::size_t> physical_device{};
    std::uint32_t threads = std::max(1U, std::thread::hardware_concurrency());
    std::optional<std::uint64_t> frames{};
    bool script = false;
    bool tour = false;
    ViewSettings views{};
    bool validation = false;
    std::optional<std::string> capture{};
    std::optional<std::uint64_t> capture_frame{};
};

/// The physics backends built.
auto
available_physics_backends() -> std::vector<physics::PhysicsBackendKind>
{
    std::vector<physics::PhysicsBackendKind> backends;
    for (const physics::PhysicsBackendKind kind : {physics::PhysicsBackendKind::BOX3D, physics::PhysicsBackendKind::REFERENCE})
    {
        if (physics::physics_backend_availability(kind).available)
        {
            backends.push_back(kind);
        }
    }
    return backends;
}

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
            options.backend = gfx::from_string<platform::PlatformBackendKind>(upper(arguments[++index]));
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
        else if (argument == "--preset" && has_value)
        {
            const std::optional<Preset> preset = find_preset(arguments[++index]);
            if (!preset)
            {
                std::println(std::cerr, "unknown preset '{}'", arguments[index]);
                return std::nullopt;
            }
            options.preset = *preset;
        }
        else if (argument == "--views" && has_value)
        {
            // Names separated by commas.
            const std::string_view list = arguments[++index];
            for (const auto part : list | std::views::split(','))
            {
                const std::string_view name{part.begin(), part.end()};
                if (name == "wireframe")
                {
                    options.views.wireframe = true;
                }
                else if (name == "boxes")
                {
                    options.views.bounding_boxes = true;
                }
                else if (name == "contacts")
                {
                    options.views.contacts = true;
                }
                else if (name == "broad_phase")
                {
                    options.views.broad_phase = true;
                }
                else if (name == "joints")
                {
                    options.views.joints = true;
                }
                else if (name == "sleep")
                {
                    options.views.coloring = BodyColoring::SLEEP;
                }
                else if (name == "islands")
                {
                    options.views.coloring = BodyColoring::ISLAND;
                }
                else
                {
                    std::println(std::cerr, "unknown view '{}' (wireframe, boxes, contacts, broad_phase, joints, sleep, islands)", name);
                    return std::nullopt;
                }
            }
        }
        else if (argument == "--tour")
        {
            options.tour = true;
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
        else if (argument == "--script")
        {
            options.script = true;
        }
        else if (argument == "--validation")
        {
            options.validation = true;
        }
        else if (argument == "--capture" && has_value)
        {
            options.capture = arguments[++index];
        }
        else if (argument == "--capture-frame" && has_value)
        {
            options.capture_frame = std::stoull(arguments[++index]);
        }
        else
        {
            std::println(std::cerr, "usage: gfx_physics_sandbox [--backend sdl3|glfw] [--physics box3d|reference] [--preset <name>] [--views <list>] [--device <index>] "
                                    "[--threads <count>] [--frames <count>] [--script | --tour] [--validation] [--capture <file.ppm>] [--capture-frame <index>]");
            std::print(std::cerr, "presets:");
            for (std::size_t preset = 0; preset < PRESET_COUNT; ++preset)
            {
                std::print(std::cerr, " {}", preset_information(static_cast<Preset>(preset)).name);
            }
            std::println(std::cerr, "");
            return std::nullopt;
        }
    }
    if (options.script && (options.tour || options.preset != Preset::SHOWCASE))
    {
        std::println(std::cerr, "--script runs in the showcase, without --tour");
        return std::nullopt;
    }
    if (options.script)
    {
        // The scripts (the interaction's, then the controls') run to their end at least.
        options.frames = std::max(options.frames.value_or(0), Script::FRAMES + ControlScript::FRAMES);
    }
    if (options.tour)
    {
        // Every preset on every physics backend, from the first preset.
        options.preset = Preset::SHOWCASE;
        options.frames = PRESET_COUNT * available_physics_backends().size() * TOUR_FRAMES;
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

// ------------------------------------------------------------------------------------------------- the mouse and keyboard

/// What the mouse and keyboard ask this frame: the pointer in framebuffer pixels (the window's coordinates scaled),
/// nothing the UI has (the mouse over a panel, the keyboard in a text field); the wheel pushes a dragged body, or zooms.
auto
commands_from(const platform::InputFrame& frame,
              const platform::Window& window,
              bool ui_wants_mouse,
              bool ui_wants_keyboard,
              bool dragging) -> FrameCommands
{
    const gfx::Extent size = window.size();
    const gfx::Extent pixels = window.framebuffer_size();
    const float scale_x = size.width > 0 ? static_cast<float>(pixels.width) / static_cast<float>(size.width) : 1.0F;
    const float scale_y = size.height > 0 ? static_cast<float>(pixels.height) / static_cast<float>(size.height) : 1.0F;
    const gfx::Vector2 mouse = frame.mouse_position();
    FrameCommands commands{.pointer = {.x = mouse.x * scale_x, .y = mouse.y * scale_y}, .pointer_in_scene = !ui_wants_mouse};
    if (!ui_wants_mouse)
    {
        const float dx = frame.get_action_value("look_x"_action);
        const float dy = frame.get_action_value("look_y"_action);
        const float wheel = frame.get_action_value("wheel"_action);
        commands.grab_pressed = frame.was_action_just_pressed("grab"_action);
        commands.grab_held = frame.is_action_pressed("grab"_action);
        commands.grab_released = frame.was_action_just_released("grab"_action);
        if (frame.is_action_pressed("turn"_action))
        {
            commands.look = {.x = dx * TURN_RADIANS_PER_PIXEL, .y = -dy * TURN_RADIANS_PER_PIXEL};
        }
        if (frame.is_action_pressed("pan"_action))
        {
            const float height = std::max(1.0F, static_cast<float>(size.height));
            commands.pan = {.x = -dx / height, .y = dy / height};
        }
        commands.push = dragging ? wheel * PUSH_METERS_PER_WHEEL_STEP : 0.0F;
        commands.zoom = dragging ? 0.0F : wheel;
    }
    else
    {
        commands.grab_released = true;
    }
    if (!ui_wants_keyboard)
    {
        commands.shoot = frame.was_action_just_pressed("shoot"_action);
        commands.remove = frame.was_action_just_pressed("remove"_action);
        commands.toggle_fly = frame.was_action_just_pressed("toggle_fly"_action);
        const gfx::Vector2 move = frame.get_action_value_2d("move"_action);
        commands.fly = {.x = move.x, .y = frame.get_action_value("rise"_action), .z = move.y};
        for (std::size_t kind = 0; kind < SPAWN_KIND_COUNT; ++kind)
        {
            if (frame.was_action_just_pressed(SPAWN_ACTIONS[kind]))
            {
                commands.spawn = static_cast<SpawnKind>(kind);
            }
        }
    }
    return commands;
}

// ------------------------------------------------------------------------------------------------- drawing the scene

/// @p color brighter by @p fraction (toward white).
auto
brighter(physics::DebugColor color,
         float fraction) -> physics::DebugColor
{
    const auto channel = [fraction](std::uint8_t value) { return static_cast<std::uint8_t>(value + (255.0F - value) * fraction); };
    return {.red = channel(color.red), .green = channel(color.green), .blue = channel(color.blue), .alpha = color.alpha};
}

/// Every collider of every body, at the poses drawn this frame, colored as @p views asks (their own colors darker while
/// asleep, by sleep, by island; the body under the pointer and the one dragged highlighted); the soft bodies (surfaces,
/// or particles); distance joints and triggers as lines (a trigger brighter while something is in it); the debug views'
/// lines; the interaction's lines and the world's axes. @p positions is scratch storage.
void
add_scene(SceneRenderer& renderer,
          const Scene& scene,
          const Interaction& interaction,
          const ViewSettings& views,
          const DebugViews& debug,
          std::vector<gfx::Vector3>& positions)
{
    for (std::size_t index = 0; index < scene.bodies().size(); ++index)
    {
        const SceneBody& body = scene.bodies()[index];
        const gfx::Transform pose = scene.drawn_pose(body);
        const std::span<const std::uint32_t> islands = debug.islands();
        const physics::DebugColor color = body_color(body, views.coloring, index < islands.size() ? islands[index] : NO_ISLAND);
        const physics::RigidBodyHandle handle = body.body->handle();
        const std::uint32_t flags = body.flags | (handle == interaction.hovered() || handle == interaction.dragged() ? INSTANCE_HIGHLIGHT : 0U);
        for (const SceneCollider& collider : body.colliders)
        {
            renderer.add(collider.shape, pose * collider.pose, color, flags);
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
        // A distance joint's cable, while both bodies are there.
        const SceneBody* first = scene.find(joint.first);
        const SceneBody* second = joint.second.is_valid() ? scene.find(joint.second) : nullptr;
        if (!joint.cable || first == nullptr || (joint.second.is_valid() && second == nullptr))
        {
            continue;
        }
        const gfx::Vector3 from = gfx::transform_point(scene.drawn_pose(*first), joint.anchor_first);
        const gfx::Vector3 to = second != nullptr ? gfx::transform_point(scene.drawn_pose(*second), joint.anchor_second) : joint.anchor_second;
        renderer.lines().add_line(from, to, {.red = 210, .green = 210, .blue = 200});
    }
    for (const SceneTrigger& trigger : scene.triggers())
    {
        const physics::DebugColor color = trigger.inside.empty() ? trigger.color : brighter(trigger.color, 0.6F);
        if (const auto* sphere = trigger.shape->get_if<physics::SphereShape>())
        {
            for (const gfx::Vector3 axis : {gfx::Vector3{.x = 1.0F}, gfx::Vector3{.y = 1.0F}, gfx::Vector3{.z = 1.0F}})
            {
                renderer.lines().add_circle(trigger.pose.position, axis, sphere->radius, color, 48);
            }
        }
        else if (const auto* box = trigger.shape->get_if<physics::BoxShape>())
        {
            renderer.lines().add_box(physics::Obb{.center = trigger.pose.position, .half_extents = box->half_extents, .orientation = trigger.pose.orientation}, color);
        }
    }
    const std::span<const physics::DebugLineVertex> lines = debug.lines().vertices();
    for (std::size_t index = 0; index + 1 < lines.size(); index += 2)
    {
        renderer.lines().add_line(lines[index].position, lines[index + 1].position, lines[index].color);
    }
    interaction.add_lines(renderer.lines());
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
    std::span<const std::array<float, PLOTTED_STEPS>> phase_milliseconds{};
    CameraMode camera = CameraMode::ORBIT;
    const Interaction* interaction = nullptr;
    bool scripted = false;
    Preset preset = Preset::SHOWCASE;
    std::span<const std::string_view> preset_names{};
    std::span<const physics::PhysicsBackendKind> backends{};
    const SimulationControl* control = nullptr;
    const DebugViews* debug = nullptr;
};

/// The integrators' names, in INTEGRATOR_CHOICES's order.
constexpr std::array<std::string_view, INTEGRATOR_CHOICES.size()> INTEGRATOR_NAMES{"semi-implicit Euler", "Verlet", "Runge-Kutta 4"};

/// The simulation section: time (pause, single steps, slow motion), the world (gravity, the solver, sleeping), the
/// reference backend's components; what the backend refused.
void
simulation_section(gfx::ui::UiFrame& frame,
                   Scene& scene,
                   SimulationSettings& settings,
                   const SimulationControl& control)
{
    static_cast<void>(frame.checkbox("paused (P)", settings.paused));
    frame.same_line();
    if (frame.button("step (N)") && settings.paused)
    {
        settings.step_once = true;
    }
    static_cast<void>(frame.slider("time scale", settings.time_scale, 0.05F, 4.0F, "{:.2f}x", gfx::ui::SliderFlag::LOGARITHMIC));
    std::array<float, 3> gravity{settings.gravity.x, settings.gravity.y, settings.gravity.z};
    if (frame.slider("gravity", gravity, -20.0F, 20.0F, "{:.1f}"))
    {
        settings.gravity = {.x = gravity[0], .y = gravity[1], .z = gravity[2]};
    }
    static_cast<void>(frame.slider("sub-steps", settings.solver.sub_steps, 1U, 16U));
    const bool iterations = scene.world().solver().is_external();
    static_cast<void>(frame.slider(iterations ? "velocity iterations" : "velocity iterations (unused)", settings.solver.velocity_iterations, 1U, 32U));
    static_cast<void>(frame.slider(iterations ? "position iterations" : "position iterations (unused)", settings.solver.position_iterations, 0U, 10U));
    static_cast<void>(frame.checkbox("warm starting", settings.solver.warm_starting));
    frame.same_line();
    static_cast<void>(frame.checkbox("sleeping", settings.sleep));

    // The components a backend that takes them can be given.
    physics::PhysicsWorld& world = scene.world();
    if (world.broad_phase().is_external())
    {
        std::array<std::string_view, BROAD_PHASE_CHOICE_COUNT> names{};
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            names[index] = broad_phase_choice_name(static_cast<BroadPhaseChoice>(index));
        }
        std::size_t selected = std::to_underlying(settings.broad_phase);
        if (frame.combo("broad phase", selected, names))
        {
            settings.broad_phase = static_cast<BroadPhaseChoice>(selected);
        }
    }
    else
    {
        frame.text("broad phase: {}'s own", scene.backend());
    }
    if (world.integrator().is_external())
    {
        std::size_t selected = static_cast<std::size_t>(std::ranges::find(INTEGRATOR_CHOICES, settings.integrator) - INTEGRATOR_CHOICES.begin());
        if (frame.combo("integrator", selected, INTEGRATOR_NAMES) && selected < INTEGRATOR_CHOICES.size())
        {
            settings.integrator = INTEGRATOR_CHOICES[selected];
        }
    }
    else
    {
        frame.text("integrator: {}'s own", scene.backend());
    }
    frame.push_text_wrap_position(PANEL_WRAP_WIDTH);
    for (const std::string& refusal : control.refusals())
    {
        frame.text_wrapped("{} refused {}", scene.backend(), refusal);
    }
    frame.pop_text_wrap_position();
}

/// The views section: what is drawn over the scene and how bodies are colored, with what the views found.
void
views_section(gfx::ui::UiFrame& frame,
              ViewSettings& views,
              const DebugViews& debug)
{
    static_cast<void>(frame.checkbox("wireframe", views.wireframe));
    frame.same_line();
    static_cast<void>(frame.checkbox("bounding boxes", views.bounding_boxes));
    frame.same_line();
    static_cast<void>(frame.checkbox("contacts", views.contacts));
    static_cast<void>(frame.checkbox("broad phase", views.broad_phase));
    frame.same_line();
    static_cast<void>(frame.checkbox("joints", views.joints));
    frame.text_unformatted("colors:");
    frame.same_line();
    static_cast<void>(frame.radio_button("body", views.coloring, BodyColoring::BODY));
    frame.same_line();
    static_cast<void>(frame.radio_button("sleep", views.coloring, BodyColoring::SLEEP));
    frame.same_line();
    static_cast<void>(frame.radio_button("island", views.coloring, BodyColoring::ISLAND));
    const DebugStatistics& found = debug.statistics();
    frame.text("{} boxes, {} contact points, {} nodes, {} joints, {} islands", found.boxes, found.contact_points, found.nodes, found.joints, found.islands);
    if (!found.structure_note.empty())
    {
        frame.push_text_wrap_position(PANEL_WRAP_WIDTH);
        frame.text_wrapped("broad phase not shown: {}", found.structure_note);
        frame.pop_text_wrap_position();
    }
}

/// What the panel asks of the scene (applied at the start of the next frame).
struct SceneRequest
{
    std::optional<Preset> preset{};                      ///< Another preset.
    std::optional<physics::PhysicsBackendKind> backend{}; ///< Another backend.
    bool reset = false;                                  ///< The preset built again.

    [[nodiscard]] auto any() const noexcept -> bool
    {
        return preset || backend || reset;
    }
};

auto
statistics_panel(gfx::ui::UiFrame& frame,
                 Scene& scene,
                 const FrameReport& report,
                 SimulationSettings& settings,
                 ViewSettings& views) -> SceneRequest
{
    // A summary always shown; the scene, the simulation, the views, the step's phases, the rendering and the controls in
    // sections that open and close.
    SceneRequest request;
    frame.set_next_window_position({.x = 12, .y = 12}, gfx::ui::UiCondition::FIRST_USE_EVER);
    if (frame.begin_window("Physics sandbox", gfx::ui::UiWindowFlag::ALWAYS_AUTO_RESIZE))
    {
        const physics::StepStatistics step = scene.statistics();
        double total = 0.0;
        for (const double seconds : step.phase_seconds)
        {
            total += seconds;
        }
        frame.text("{} physics, {} threads; step {}", scene.backend(), report.threads, step.step);
        frame.text("bodies {} ({} awake), pairs {}, contact points {}", step.rigid_bodies, step.awake_bodies, step.pairs, step.contacts);
        frame.text("step {:.2f} ms, frame {:.2f} ms ({:.0f} fps)", total * 1000.0, report.frame_seconds * 1000.0,
                   report.frame_seconds > 0 ? 1.0 / report.frame_seconds : 0.0);
        const float slowest = report.step_milliseconds.empty() ? 0.0F : std::ranges::max(report.step_milliseconds);
        const std::string overlay = std::format("slowest {:.2f} ms", slowest);
        frame.plot_lines("step ms", report.step_milliseconds, {.overlay = overlay, .minimum = 0.0F, .size = {.x = 0, .y = 40}});
        if (report.interaction != nullptr)
        {
            const InteractionStatistics& done = report.interaction->statistics();
            const SceneBody* hovered = scene.find(report.interaction->hovered());
            frame.text("camera: {}{}; pointing at {}", report.camera == CameraMode::ORBIT ? "orbit" : "fly", report.scripted ? " (scripted)" : "",
                       hovered != nullptr && !hovered->colliders.empty() ? std::format("a {}", hovered->colliders.front().shape->kind()) : std::string{"nothing to grab"});
            frame.text("grabs {}, shots {}, spawned {}, removed {}", done.grabs, done.shots, done.spawned, done.removed);
        }
        if (frame.collapsing_header("Scene", gfx::ui::TreeNodeFlag::DEFAULT_OPEN))
        {
            // The preset and the backend (either change builds the scene again), what is in the triggers, what the
            // backend refused.
            std::size_t selected = std::to_underlying(report.preset);
            if (frame.combo("preset", selected, report.preset_names))
            {
                request.preset = static_cast<Preset>(selected);
            }
            for (std::size_t index = 0; index < report.backends.size(); ++index)
            {
                const physics::PhysicsBackendKind backend = report.backends[index];
                if (index > 0)
                {
                    frame.same_line();
                }
                if (frame.radio_button(backend == physics::PhysicsBackendKind::BOX3D ? "Box3D" : "reference", backend == scene.backend())
                    && backend != scene.backend())
                {
                    request.backend = backend;
                }
            }
            frame.same_line();
            request.reset = frame.button("reset (R)");
            frame.push_text_wrap_position(PANEL_WRAP_WIDTH);
            frame.text_wrapped_unformatted(preset_information(report.preset).description);
            frame.text("{} soft bodies, {} joints, {} triggers", scene.soft_bodies().size(), scene.joints().size(), scene.triggers().size());
            for (const SceneTrigger& trigger : scene.triggers())
            {
                frame.bullet_text("{}: {} inside, {} entered", trigger.name, trigger.inside.size(), trigger.entered);
            }
            for (const std::string& note : scene.notes())
            {
                frame.text_wrapped("{} refused {}", scene.backend(), note);
            }
            frame.pop_text_wrap_position();
        }
        if (frame.collapsing_header("Simulation") && report.control != nullptr)
        {
            simulation_section(frame, scene, settings, *report.control);
        }
        if (frame.collapsing_header("Views") && report.debug != nullptr)
        {
            views_section(frame, views, *report.debug);
        }
        if (frame.collapsing_header("Step phases"))
        {
            // Each phase's time in the last step, and its plot.
            for (std::size_t phase = 0; phase < step.phase_seconds.size(); ++phase)
            {
                const std::string label = std::format("{:<12} {:6.3f} ms", static_cast<physics::StepPhase>(phase), step.phase_seconds[phase] * 1000.0);
                if (phase < report.phase_milliseconds.size())
                {
                    frame.plot_lines(label, report.phase_milliseconds[phase], {.minimum = 0.0F, .size = {.x = 160, .y = 22}});
                }
            }
            frame.text("joints {}, soft bodies {}", step.joints, step.soft_bodies);
        }
        if (frame.collapsing_header("Rendering") && report.drawn != nullptr)
        {
            frame.text_unformatted(report.device);
            frame.text("{} instances and {} surfaces in {} draws, {} lines, {}x MSAA", report.drawn->instances, report.drawn->surfaces, report.drawn->draw_calls,
                       report.drawn->lines, std::to_underlying(report.samples));
            frame.text("{} meshes: {} vertices, {} indices", report.drawn->meshes, report.drawn->mesh_vertices, report.drawn->mesh_indices);
        }
        if (frame.collapsing_header("Controls"))
        {
            frame.text_unformatted("left drag: drag a body (wheel: push, pull)");
            frame.text_unformatted("right drag: turn   middle drag: pan   wheel: zoom");
            frame.text_unformatted("F: orbit / fly (W A S D, E Q)   Space: shoot");
            frame.text_unformatted("1-7: spawn   Delete, X: remove   R: reset");
            frame.text_unformatted("P: pause   N: single step   Esc: quit");
        }
    }
    frame.end_window();
    return request;
}

// ------------------------------------------------------------------------------------------------- the tour

/// Whether @p scene (between steps) looks sane after its time in the tour: everything finite, nothing under the floor,
/// nothing flying away, the steps all taken.
auto
check_tour_stop(const Scene& scene,
                Preset preset,
                std::uint64_t frames) -> ScriptCheck
{
    bool finite = true;
    float lowest = std::numeric_limits<float>::infinity();
    std::string lowest_what = "nothing";
    float fastest = 0.0F;
    std::size_t moving = 0;
    std::size_t particles = 0;
    const auto add = [&](gfx::Vector3 position, gfx::Vector3 velocity, auto&& what)
    {
        finite = finite && std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) && std::isfinite(gfx::length(velocity));
        if (position.y < lowest)
        {
            lowest = position.y;
            lowest_what = std::format("{} at ({:.2f}, {:.2f}, {:.2f})", what(), position.x, position.y, position.z);
        }
        fastest = std::max(fastest, gfx::length(velocity));
    };
    for (const SceneBody& body : scene.bodies())
    {
        if (body.kind == physics::BodyKind::DYNAMIC)
        {
            const physics::RigidBodyState state = body.body->state();
            add(state.pose.position, state.linear_velocity,
                [&body] { return std::format("{} ({})", body.body->handle(), body.colliders.empty() ? std::string{"no collider"} : std::format("{}", body.colliders.front().shape->kind())); });
            ++moving;
        }
    }
    std::vector<gfx::Vector3> positions;
    std::vector<gfx::Vector3> velocities;
    for (const SceneSoftBody& soft_body : scene.soft_bodies())
    {
        positions.resize(soft_body.body->particle_count());
        velocities.resize(positions.size());
        static_cast<void>(soft_body.body->positions(positions));
        static_cast<void>(soft_body.body->velocities(velocities));
        for (std::size_t index = 0; index < positions.size(); ++index)
        {
            add(positions[index], velocities[index], [&soft_body, index] { return std::format("{} particle {}", soft_body.body->handle(), index); });
        }
        particles += positions.size();
    }
    const bool stepped = scene.step_count() + 1 >= frames;
    return {.description = std::format("{} on {}", preset_information(preset).name, scene.backend()),
            .passed = finite && lowest > TOUR_LOWEST && fastest < TOUR_FASTEST && stepped,
            .details = std::format("{} bodies, {} particles, {}finite, lowest {}, fastest {:.1f} m/s, {} steps, {} refusals", moving, particles,
                                   finite ? "" : "not ", lowest_what, fastest, scene.step_count(), scene.notes().size())};
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

    const std::vector<platform::PlatformBackendKind> backends = platform::available_platform_backends();
    if (backends.empty())
    {
        std::println(std::cerr, "no platform backend is available: {}", platform::platform_backend_availability(platform::PlatformBackendKind::SDL3).reason);
        return 1;
    }
    const physics::PhysicsBackendAvailability physics_availability = physics::physics_backend_availability(options.physics);
    if (!physics_availability.available)
    {
        std::println(std::cerr, "the {} physics backend is unavailable: {}", options.physics, physics_availability.reason);
        return 1;
    }
    const std::unique_ptr<platform::Platform> host = platform::create_platform(options.backend.value_or(backends.front()), {.application_name = TITLE});
    platform::WindowDescription window_description;
    window_description.title = TITLE;
    window_description.size = INITIAL_SIZE;
    window_description.minimum_size = gfx::Extent{.width = 480, .height = 270, .depth = 1};
    window_description.surface_api = platform::SurfaceApi::VULKAN;
    if (options.frames)
    {
        window_description.flags &= ~platform::WindowFlag::FOCUS_ON_SHOW; // automated runs leave the focus where it is
    }
    const std::unique_ptr<platform::Window> window = host->create_window(window_description);

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
    platform::WindowSwapchain presentation{
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
    gfx::ui::UiWindowServices ui_services{*host, *window};
    gfx::ui::UiContext ui{{.services = &ui_services}};
    ui.load_font(gfx::ui::FontAtlas::load(sandbox_font), INTERFACE_FONT_SIZE);
    gfx::ui::UiPlatformInput ui_input{*window};
    gfx::ui::UiRenderer ui_renderer{*device, ui.fonts(), {.color_format = presentation.format(), .frames_in_flight = FRAMES_IN_FLIGHT}};
    std::println("{} window on '{}': {}x{} pixels, {}, {}x MSAA; {} physics on {} threads", host->backend(), physical_device.properties().name,
                 presentation.extent().width, presentation.extent().height, presentation.format(), std::to_underlying(samples), options.physics,
                 options.threads);

    // ---------------------------------------------------------------------------------- the scene

    // A perspective camera with reversed depth and no far plane (Vulkan: +Y down in normalized device coordinates),
    // moved by the rig (orbit or fly), aimed by each scene.
    const std::unique_ptr<camera::Camera> view_camera = camera::create_camera(
        {.projection = camera::PerspectiveProjection{.vertical_field_of_view = 0.9F, .aspect_ratio = 16.0F / 9.0F, .near_plane = 0.05F,
                                                     .far_plane = camera::INFINITE_FAR_PLANE},
         .clip = {.reversed_depth = true, .y_axis = camera::NormalizedDeviceYAxis::DOWN}});
    CameraRig rig{camera::OrbitControllerDescription{.distance = 17.0F, .maximum_distance = 300.0F}};

    // The scene: a preset built into a world of a backend, built again when either changes (the world cannot move to
    // another backend). Nothing may step the old world or draw its meshes then: the step in flight and the GPU finish.
    physics::ThreadTaskScheduler scheduler{options.threads};
    const std::vector<physics::PhysicsBackendKind> physics_backends = available_physics_backends();
    std::array<std::string_view, PRESET_COUNT> preset_names{};
    for (std::size_t index = 0; index < PRESET_COUNT; ++index)
    {
        preset_names[index] = preset_information(static_cast<Preset>(index)).name;
    }
    std::unique_ptr<Scene> scene{};
    std::unique_ptr<Interaction> interaction{};
    Preset preset = options.preset;
    std::vector<float> step_milliseconds(PLOTTED_STEPS, 0.0F);
    std::array<std::vector<float>, gfx::ENUMERATOR_COUNT<physics::StepPhase>> phase_milliseconds{};
    phase_milliseconds.fill(std::vector<float>(PLOTTED_STEPS, 0.0F));

    // The simulation's settings and the views, kept across scenes (applied to each new world).
    SimulationSettings settings;
    ViewSettings views = options.views;
    SimulationControl control;
    DebugViews debug;
    const auto build = [&](Preset next,
                           physics::PhysicsBackendKind backend) -> PresetResult
    {
        if (scene != nullptr)
        {
            scene->finish();
        }
        device->wait_idle();
        interaction.reset();
        scene.reset();
        renderer.clear_meshes();
        scene = std::make_unique<Scene>(backend, scheduler);
        PresetResult built = build_preset(next, *scene);
        interaction = std::make_unique<Interaction>(*scene);
        control.adopt(*scene);
        control.apply(*scene, settings);
        const gfx::Vector3 right{.x = std::cos(built.view.yaw), .z = -std::sin(built.view.yaw)};
        rig.aim(built.view.target - right * (PANEL_OFFSET * built.view.distance), built.view.distance, built.view.yaw, built.view.pitch);
        preset = next;
        std::ranges::fill(step_milliseconds, 0.0F);
        for (std::vector<float>& series : phase_milliseconds)
        {
            std::ranges::fill(series, 0.0F);
        }
        std::println("{} on {}: {} bodies, {} soft bodies, {} joints, {} triggers", preset_information(next).name, backend, scene->bodies().size(),
                     scene->soft_bodies().size(), scene->joints().size(), scene->triggers().size());
        for (const std::string& note : scene->notes())
        {
            std::println("  {} refused {}", backend, note);
        }
        return built;
    };
    const PresetResult first = build(options.preset, options.physics);
    std::optional<Script> script{};
    std::optional<ControlScript> control_script{};
    if (options.script)
    {
        script.emplace(*first.showcase);
        control_script.emplace(Script::FRAMES);
    }

    // The tour: every preset on every physics backend, each for TOUR_FRAMES frames, through the same rebuilding.
    std::vector<std::pair<Preset, physics::PhysicsBackendKind>> tour_stops;
    if (options.tour)
    {
        for (const physics::PhysicsBackendKind backend : physics_backends)
        {
            for (std::size_t index = 0; index < PRESET_COUNT; ++index)
            {
                tour_stops.emplace_back(static_cast<Preset>(index), backend);
            }
        }
        if (tour_stops.front().second != options.physics)
        {
            static_cast<void>(build(tour_stops.front().first, tour_stops.front().second));
        }
    }
    std::size_t tour_stop = 0;
    std::uint64_t stop_start_frame = 0;
    std::vector<ScriptCheck> tour_checks;

    // ---------------------------------------------------------------------------------- the loop

    platform::InputSystem input{INPUT};
    const platform::ContextActivation sandbox_context = input.activate_context("sandbox"_context);
    std::vector<gfx::Vector3> drawn_positions;
    SceneRequest request;
    std::uint64_t plotted_step = 0;
    std::uint64_t presented = 0;
    std::uint64_t frame_index = 0;
    double frame_seconds = 0.0;
    bool running = true;
    bool ui_wants_mouse = false;
    bool ui_wants_keyboard = false;
    auto previous_time = std::chrono::steady_clock::now();
    double ui_time = 0.0;
    while (running && (!options.frames || presented < *options.frames))
    {
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

        // A new scene: asked by the panel or R (not while a script or the tour drives the sandbox), or the tour's next stop
        // (the scene left checked first).
        request.reset = request.reset || (!ui_wants_keyboard && frame.was_action_just_pressed("reset"_action));
        if (!ui_wants_keyboard && !script && frame.was_action_just_pressed("pause"_action))
        {
            settings.paused = !settings.paused;
        }
        if (!ui_wants_keyboard && !script && frame.was_action_just_pressed("step"_action) && settings.paused)
        {
            settings.step_once = true;
        }
        if (request.any() && !script && !options.tour)
        {
            static_cast<void>(build(request.preset.value_or(preset), request.backend.value_or(scene->backend())));
        }
        request = {};
        if (options.tour && frame_index - stop_start_frame >= TOUR_FRAMES && tour_stop + 1 < tour_stops.size())
        {
            scene->finish();
            tour_checks.push_back(check_tour_stop(*scene, preset, frame_index - stop_start_frame));
            ++tour_stop;
            static_cast<void>(build(tour_stops[tour_stop].first, tour_stops[tour_stop].second));
            stop_start_frame = frame_index;
        }

        // This frame's commands: from the script, or from the mouse and keyboard.
        const gfx::Extent extent = presentation.extent();
        const FrameCommands commands = script ? script->commands(frame_index, *scene, *view_camera, extent)
                                              : commands_from(frame, *window, ui_wants_mouse, ui_wants_keyboard, interaction->dragged().is_valid());

        // The camera, then the world: the step begun last frame completes, the interaction queries and edits the world, the
        // settings are applied and the views gathered, the next step begins (it runs while this frame is drawn) — unless
        // paused (one step when asked), and slower in slow motion.
        rig.update(*view_camera, commands, static_cast<float>(advanced));
        if (extent.width > 0 && extent.height > 0)
        {
            view_camera->set_aspect_ratio(static_cast<float>(extent.width) / static_cast<float>(extent.height));
        }
        const std::uint64_t steps_before = scene->step_count();
        scene->finish();
        interaction->update(commands, *view_camera, extent);
        if (script)
        {
            script->observe(frame_index, *scene, *interaction, *view_camera, rig);
        }
        if (control_script)
        {
            control_script->control(frame_index, *scene, settings, views);
        }
        control.apply(*scene, settings);
        debug.capture(*scene, views);
        if (control_script)
        {
            control_script->observe(frame_index, *scene, settings, control, debug, renderer.statistics());
        }
        scene->clear_events();
        if (!settings.paused)
        {
            settings.step_once = false;
            scene->advance(advanced * static_cast<double>(settings.time_scale));
        }
        else if (settings.step_once)
        {
            settings.step_once = false;
            scene->step_once();
        }
        else
        {
            scene->hold();
        }
        ++frame_index;
        if (scene->step_count() != steps_before)
        {
            const physics::StepStatistics step = scene->statistics();
            double total = 0.0;
            for (std::size_t phase = 0; phase < step.phase_seconds.size(); ++phase)
            {
                total += step.phase_seconds[phase];
                phase_milliseconds[phase][plotted_step % PLOTTED_STEPS] = static_cast<float>(step.phase_seconds[phase] * 1000.0);
            }
            step_milliseconds[plotted_step % PLOTTED_STEPS] = static_cast<float>(total * 1000.0);
            ++plotted_step;
        }

        // An image to draw into (none while the window is minimized).
        const std::optional<platform::SwapchainFrame> image = presentation.acquire();
        if (!image)
        {
            continue;
        }
        const std::uint64_t frame_number = timeline.begin_frame();
        command_pools.begin_frame(frame_number);
        renderer.begin_frame(frame_number);
        ui_renderer.begin_frame(frame_number);
        add_scene(renderer, *scene, *interaction, views, debug, drawn_positions);

        // The UI's frame: the statistics panel (the plots oldest first).
        std::array<float, PLOTTED_STEPS> plot{};
        std::array<std::array<float, PLOTTED_STEPS>, gfx::ENUMERATOR_COUNT<physics::StepPhase>> phase_plots{};
        for (std::size_t index = 0; index < PLOTTED_STEPS; ++index)
        {
            plot[index] = step_milliseconds[(plotted_step + index) % PLOTTED_STEPS];
            for (std::size_t phase = 0; phase < phase_plots.size(); ++phase)
            {
                phase_plots[phase][index] = phase_milliseconds[phase][(plotted_step + index) % PLOTTED_STEPS];
            }
        }
        gfx::ui::UiFrame& ui_frame = ui.begin_frame(ui_input.frame_input(ui_time));
        request = statistics_panel(ui_frame, *scene,
                                   {.frame_seconds = frame_seconds,
                                    .threads = scheduler.concurrency(),
                                    .device = physical_device.properties().name,
                                    .samples = samples,
                                    .drawn = &renderer.statistics(),
                                    .step_milliseconds = plot,
                                    .phase_milliseconds = phase_plots,
                                    .camera = rig.mode(),
                                    .interaction = interaction.get(),
                                    .scripted = script.has_value(),
                                    .preset = preset,
                                    .preset_names = preset_names,
                                    .backends = physics_backends,
                                    .control = &control,
                                    .debug = &debug},
                                   settings, views);
        static_cast<void>(ui.end_frame());
        ui_wants_mouse = ui.wants_mouse();
        ui_wants_keyboard = ui.wants_keyboard();
        const gfx::ui::UiDrawData& ui_data = ui.draw_data();

        // Record: the scene's pass (resolved into the image), the UI's pass over it, the hand-over to presentation.
        const bool capture_now = options.capture_frame ? presented == *options.capture_frame : options.frames && presented + 1 == *options.frames;
        std::unique_ptr<gfx::Buffer> readback{};
        gfx::CommandBuffer& commands_buffer = command_pools.command_buffer();
        {
            gfx::GraphicsRecorder recorder{commands_buffer};
            const std::array to_attachment{gfx::image_barrier(image->image.image, gfx::ResourceState::UNDEFINED, gfx::ResourceState::COLOR_ATTACHMENT_WRITE)};
            recorder.barrier({.image_barriers = to_attachment});
            renderer.prepare(recorder);
            ui_renderer.prepare(recorder, ui_data);
            const camera::Camera& eye = *view_camera;
            renderer.render(recorder, image->image.view, extent, {.view_projection = eye.view_projection(), .wireframe = views.wireframe});
            const std::array to_interface{
                gfx::image_barrier(image->image.image, gfx::ResourceState::COLOR_ATTACHMENT_WRITE, gfx::ResourceState::COLOR_ATTACHMENT_READ_WRITE)};
            recorder.barrier({.image_barriers = to_interface});
            {
                const std::array attachments{gfx::ColorAttachment{.view = image->image.view, .load = gfx::LoadOperation::LOAD}};
                gfx::RenderingScope scope = recorder.begin_rendering(
                    {.area = {.width = extent.width, .height = extent.height}, .color_attachments = attachments, .debug_name = "sandbox interface"});
                ui_renderer.record(scope, ui_data);
            }
            if (capture_now && capturing)
            {
                // The frame copied out for the capture (the last one unless --capture-frame names another).
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
        const std::array command_buffers{commands_buffer.handle()};
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
            std::println("captured frame {} into {}", presented - 1, *options.capture);
        }
    }

    // The step in flight and the GPU finish before anything is destroyed.
    scene->finish();
    device->wait_idle();
    const physics::StepStatistics last = scene->statistics();
    std::println("{} frames presented, {} steps, {} bodies ({} awake), {} contact points, {} validation errors", presented, scene->step_count(),
                 last.rigid_bodies, last.awake_bodies, last.contacts, validation_errors.count());

    // The script's checks, or the tour's (the last stop's too).
    std::vector<ScriptCheck> checks;
    if (script)
    {
        checks = script->checks(*scene);
        std::ranges::move(control_script->checks(), std::back_inserter(checks));
    }
    if (options.tour)
    {
        tour_checks.push_back(check_tour_stop(*scene, preset, frame_index - stop_start_frame));
        if (tour_checks.size() != tour_stops.size())
        {
            tour_checks.push_back({.description = "every stop of the tour", .passed = false,
                                   .details = std::format("{} of {} stops reached", tour_checks.size(), tour_stops.size())});
        }
        checks = std::move(tour_checks);
    }
    std::uint32_t failed = 0;
    for (const ScriptCheck& check : checks)
    {
        std::println("  {}  {}{}{}", check.passed ? "ok    " : "FAILED", check.description, check.details.empty() ? "" : ": ", check.details);
        failed += check.passed ? 0U : 1U;
    }
    if (!checks.empty())
    {
        std::println("{} checks failed", failed);
    }
    return validation_errors.count() == 0 && failed == 0 ? 0 : 1;
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
