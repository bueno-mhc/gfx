// The UI editor example of gfx: an editor-like UI of the UI library over a scene, both drawn by a render thread.
//
// The main thread owns the platform and the UI: it pumps events, reads the input system (a gamepad navigates through
// the UI's navigation context), builds a UI frame — a main menu bar, a dock space whose default layout docks a text
// panel on the left, an inspector on the right, a table of the scene's objects and statistics at the bottom, and the
// scene in the middle — and hands the published draw data (a lease) with a snapshot of the scene to the render thread.
// The render thread draws the scene (its objects as rectangles) and the UI's draw data into the swapchain's images
// while the main thread builds the next frame. Every text is SDF: the text panel draws its text at any size, rotated,
// outlined and shadowed, from one atlas (and two fallback atlases for Hebrew and Arabic). Shortcuts (Control+N, Delete,
// Control+D, Control+S, Control+O, Control+Q) work from any window; screen readers see the UI through
// UiAccessibilityAdapter (Windows).
//
// Usage: gfx_ui_editor_example [--backend sdl3|glfw] [--device <index>] [--frames <count>] [--validation]
//                              [--sweep] [--check-allocations] [--warm-up <frames>] [--layout <file>]
//   --frames N             closes after N frames (automated runs; the window then does not take the focus)
//   --validation           enables the Vulkan validation layers; the exit code is 1 if they report an error
//   --sweep                moves a synthetic mouse across the window along a path closed every 120 frames (hovering
//                          items, tooltips and table rows); the time then advances 1/60 s per frame (the frame times
//                          shown too: every run draws the same frames) and the measured time is printed at the end
//   --check-allocations    counts the allocations of the UI's frames (main thread) and of the render thread's frames
//                          (acquisition, recording with the UI's renderer, submission, presentation) after the first
//                          frames; the exit code is 1 if there is one
//   --warm-up N            the frames before allocations are counted (default 60)
//   --layout <file>        loads the layout from the file (when it exists); File > Save layout writes it there
import std;
import gfx.platform.graphics; // gfx (graphics), gfx.platform (windows and input), and the bridge between them
import gfx.ui;                // the immediate-mode UI, its SDF text and its renderer
import gfx.ui.platform;       // the UI's input and services from the platform window, navigation, accessibility
import ui_editor.latin_font;  // the font atlases, built by gfx_add_ui_atlas()
import ui_editor.hebrew_font;
import ui_editor.arabic_font;

// ===================================================================================================== allocations

namespace
{

/// The allocations counted on the main thread (the UI's frames) and on the render thread (its frames: acquisition,
/// recording, submission, presentation).
std::atomic<std::uint64_t> g_ui_allocations{0};
std::atomic<std::uint64_t> g_render_allocations{0};

/// Allocations are counted on a thread while this points at its counter (the measured sections).
thread_local std::atomic<std::uint64_t>* t_counter = nullptr;

/// Called for each allocation counted: a breakpoint here shows where it comes from.
[[gnu::noinline]] void
counted_allocation(std::atomic<std::uint64_t>& counter) noexcept
{
    counter.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

// The global allocation functions of this program: counted in the measured sections.
auto
operator new(std::size_t size) -> void*
{
    if (t_counter != nullptr)
    {
        counted_allocation(*t_counter);
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size); memory != nullptr)
    {
        return memory;
    }
    throw std::bad_alloc{};
}

auto
operator new[](std::size_t size) -> void*
{
    return operator new(size);
}

void
operator delete(void* memory) noexcept
{
    std::free(memory);
}

void
operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void
operator delete(void* memory,
                std::size_t) noexcept
{
    std::free(memory);
}

void
operator delete[](void* memory,
                  std::size_t) noexcept
{
    std::free(memory);
}

namespace
{

using namespace std::chrono_literals;
using namespace gfx::ui::literals;
using gfx::ui::Color;
using gfx::ui::KeyChord;
using gfx::ui::UiFrame;
using gfx::ui::UiRectangle;
using gfx::Vector2;

/// Counts this thread's allocations in @p counter while it lives, when @p counted.
class AllocationScope
{
public:
    AllocationScope(std::atomic<std::uint64_t>& counter,
                    bool counted) noexcept
    {
        t_counter = counted ? &counter : nullptr;
    }

    AllocationScope(const AllocationScope&) = delete;
    auto operator=(const AllocationScope&) -> AllocationScope& = delete;

    ~AllocationScope()
    {
        t_counter = nullptr;
    }
};

// ------------------------------------------------------------------------------------------------- settings

constexpr std::string_view TITLE = "gfx UI editor example";
constexpr gfx::Extent INITIAL_SIZE{.width = 1600, .height = 900, .depth = 1};
constexpr std::uint32_t FRAMES_IN_FLIGHT = 2;
constexpr std::size_t MAXIMUM_OBJECTS = 32;
constexpr std::uint64_t WARM_UP_FRAMES = 60;  ///< Default frames before allocations are counted (caches, pools, atlas pages).
constexpr float INTERFACE_FONT_SIZE = 15.0F;  ///< UI units (points) per em.
constexpr float PREVIEW_FONT_SIZE = 64.0F;    ///< The text panel's font, scaled to the size asked for.
constexpr std::size_t FRAME_TIMES = 120;      ///< Frame times plotted.
constexpr std::uint64_t SWEEP_FRAMES = 120;   ///< Frames of the synthetic mouse's closed path (--sweep).
constexpr double SWEEP_TIME_STEP = 1.0 / 60.0; ///< Seconds per frame in a sweep: the same frames whatever the machine's speed.
constexpr std::uint64_t REPORTED_ALLOCATING_FRAMES = 10; ///< Frames that allocated after the warm-up printed (--check-allocations).

constexpr KeyChord ADD_OBJECT{.key = gfx::KeyCode::N, .modifiers = gfx::KeyModifier::CONTROL};
constexpr KeyChord DUPLICATE_OBJECT{.key = gfx::KeyCode::D, .modifiers = gfx::KeyModifier::CONTROL};
constexpr KeyChord REMOVE_OBJECT{.key = gfx::KeyCode::DELETE_FORWARD};
constexpr KeyChord SAVE_LAYOUT{.key = gfx::KeyCode::S, .modifiers = gfx::KeyModifier::CONTROL};
constexpr KeyChord LOAD_LAYOUT{.key = gfx::KeyCode::O, .modifiers = gfx::KeyModifier::CONTROL};
constexpr KeyChord QUIT{.key = gfx::KeyCode::Q, .modifiers = gfx::KeyModifier::CONTROL};

/// The input: the UI's navigation actions, a gamepad's bindings for them (the keyboard is the UI's own keymap).
constexpr auto INPUT = gfx::platform::make_input_schema(gfx::ui::ui_navigation_actions(), gfx::ui::ui_gamepad_navigation_context());

/// What the command line asks for.
struct Options
{
    std::optional<gfx::platform::PlatformBackendKind> backend{};
    std::optional<std::size_t> physical_device{};
    std::optional<std::uint64_t> frames{};
    bool validation = false;
    bool sweep = false;
    bool check_allocations = false;
    std::uint64_t warm_up = WARM_UP_FRAMES;
    std::optional<std::filesystem::path> layout{};
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
            options.backend = gfx::from_string<gfx::platform::PlatformBackendKind>(name);
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
        else if (argument == "--warm-up" && has_value)
        {
            options.warm_up = std::stoull(arguments[++index]);
        }
        else if (argument == "--layout" && has_value)
        {
            options.layout = std::filesystem::path{arguments[++index]};
        }
        else if (argument == "--validation")
        {
            options.validation = true;
        }
        else if (argument == "--sweep")
        {
            options.sweep = true;
        }
        else if (argument == "--check-allocations")
        {
            options.check_allocations = true;
        }
        else
        {
            std::println(std::cerr, "usage: gfx_ui_editor_example [--backend sdl3|glfw] [--device <index>] [--frames <count>] [--validation] "
                                    "[--sweep] [--check-allocations] [--warm-up <frames>] [--layout <file>]");
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

/// @p color (sRGB-encoded, as the UI edits colors) as the linear values an sRGB image is cleared with.
auto
linear(Color color) noexcept -> std::array<float, 4>
{
    const auto channel = [](float value)
    {
        return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
    };
    return {channel(color.red), channel(color.green), channel(color.blue), color.alpha};
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

// ===================================================================================================== the scene

/// An object of the scene: a square that moves and bounces in the scene's viewport.
struct SceneObject
{
    std::array<char, 32> name{};                       ///< NUL-terminated (edited in place: no allocation).
    std::array<float, 2> position{0.5F, 0.5F};         ///< Center, in viewport sizes from its top-left corner.
    std::array<float, 2> velocity{0.2F, 0.13F};        ///< Viewport sizes per second.
    float size = 0.08F;                                ///< Side, in viewport heights.
    Color color{.red = 1, .green = 0.55F, .blue = 0.1F, .alpha = 1};
    bool visible = true;
    bool animated = true;
};

/// The scene: its objects, the selected one, its background, its speed.
struct Scene
{
    std::array<SceneObject, MAXIMUM_OBJECTS> objects{};
    std::size_t count = 0;
    std::optional<std::size_t> selected{};
    Color background{.red = 0.05F, .green = 0.07F, .blue = 0.12F, .alpha = 1};
    float speed = 1.0F;
    std::uint32_t made = 0; ///< Objects made so far (their names).
};

/// The name of @p object.
auto
name_of(const SceneObject& object) noexcept -> std::string_view
{
    return {object.name.data(), static_cast<std::size_t>(std::ranges::find(object.name, '\0') - object.name.begin())};
}

/// Adds an object to @p scene (a copy of @p model when given), selected; nothing when the scene is full.
void
add_object(Scene& scene,
           const SceneObject* model = nullptr)
{
    if (scene.count == scene.objects.size())
    {
        return;
    }
    // A color and a direction of its own, from a small palette and the golden angle.
    constexpr std::array PALETTE{Color{.red = 1, .green = 0.55F, .blue = 0.1F, .alpha = 1}, Color{.red = 0.2F, .green = 0.7F, .blue = 1, .alpha = 1},
                                 Color{.red = 0.4F, .green = 0.9F, .blue = 0.35F, .alpha = 1}, Color{.red = 0.95F, .green = 0.3F, .blue = 0.5F, .alpha = 1},
                                 Color{.red = 0.95F, .green = 0.85F, .blue = 0.25F, .alpha = 1}};
    const std::uint32_t number = ++scene.made;
    SceneObject object = model != nullptr ? *model : SceneObject{};
    if (model == nullptr)
    {
        const float angle = static_cast<float>(number) * 2.39996F;
        object.color = PALETTE[number % PALETTE.size()];
        object.velocity = {0.25F * std::cos(angle), 0.25F * std::sin(angle)};
        object.position = {0.2F + 0.6F * static_cast<float>((number * 37) % 100) / 100.0F, 0.2F + 0.6F * static_cast<float>((number * 61) % 100) / 100.0F};
        object.size = 0.05F + 0.01F * static_cast<float>(number % 6);
    }
    object.name.fill('\0');
    const std::string_view base = model != nullptr ? "Copy" : "Object";
    std::format_to_n(object.name.data(), static_cast<std::ptrdiff_t>(object.name.size() - 1), "{} {}", base, number);
    scene.objects[scene.count] = object;
    scene.selected = scene.count;
    ++scene.count;
}

/// Removes the selected object of @p scene.
void
remove_selected(Scene& scene)
{
    if (!scene.selected || *scene.selected >= scene.count)
    {
        return;
    }
    std::ranges::move(scene.objects.begin() + static_cast<std::ptrdiff_t>(*scene.selected) + 1, scene.objects.begin() + static_cast<std::ptrdiff_t>(scene.count),
                      scene.objects.begin() + static_cast<std::ptrdiff_t>(*scene.selected));
    --scene.count;
    scene.selected = scene.count == 0 ? std::nullopt : std::optional{std::min(*scene.selected, scene.count - 1)};
}

/// Moves @p scene's animated objects by @p seconds, bouncing on the viewport's edges.
void
animate(Scene& scene,
        float seconds) noexcept
{
    for (SceneObject& object : std::span{scene.objects}.first(scene.count))
    {
        if (!object.animated)
        {
            continue;
        }
        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            object.position[axis] += object.velocity[axis] * scene.speed * seconds;
            const float margin = std::min(object.size * 0.5F, 0.5F);
            if (object.position[axis] < margin || object.position[axis] > 1.0F - margin)
            {
                object.velocity[axis] = -object.velocity[axis];
                object.position[axis] = std::clamp(object.position[axis], margin, 1.0F - margin);
            }
        }
    }
}

// ===================================================================================================== the render thread

/// An object of the scene as the render thread draws it: a rectangle of the image cleared to a color.
struct SceneSquare
{
    gfx::Rectangle area{};
    std::array<float, 4> color{};
};

/// What the main thread hands the render thread for a frame: the UI's published draw data (leased: the UI does not
/// reuse it until the lease is released) and a snapshot of the scene.
struct FramePacket
{
    gfx::ui::UiDrawDataLease draw_data{};
    std::array<float, 4> background{};
    std::array<SceneSquare, MAXIMUM_OBJECTS> squares{};
    std::size_t square_count = 0;
    bool recreate_swapchain = false;
};

/// One packet in flight between the threads: the main thread waits until the render thread took the previous one.
class Mailbox
{
public:
    /// Hands @p packet over once the previous one was taken (nothing once stopped).
    void put(FramePacket&& packet)
    {
        std::unique_lock lock{m_mutex};
        m_taken.wait(lock, [this] { return !m_packet.has_value() || m_stopped; });
        if (!m_stopped)
        {
            m_packet = std::move(packet);
            m_ready.notify_one();
        }
    }

    /// The next packet; nothing once stopped and empty.
    [[nodiscard]] auto take() -> std::optional<FramePacket>
    {
        std::unique_lock lock{m_mutex};
        m_ready.wait(lock, [this] { return m_packet.has_value() || m_stopped; });
        std::optional<FramePacket> packet = std::exchange(m_packet, std::nullopt);
        m_taken.notify_one();
        return packet;
    }

    /// No more packets: take() returns the one left, then nothing.
    void stop()
    {
        const std::scoped_lock lock{m_mutex};
        m_stopped = true;
        m_ready.notify_all();
        m_taken.notify_all();
    }

private:
    std::mutex m_mutex{};
    std::condition_variable m_ready{};
    std::condition_variable m_taken{};
    std::optional<FramePacket> m_packet{};
    bool m_stopped = false;
};

/// What the render thread uses: the device, its queue, the window's swapchain, the UI's renderer.
struct RenderTarget
{
    gfx::Device& device;
    gfx::Queue& queue;
    gfx::platform::WindowSwapchain& presentation;
    gfx::ui::UiRenderer& ui_renderer;
    bool check_allocations = false;
    std::uint64_t warm_up = WARM_UP_FRAMES;
    std::atomic<std::uint64_t> presented{0};
};

/// The render thread: each packet's scene and UI drawn into a swapchain image and presented, until the mailbox stops.
void
render(RenderTarget& target,
       Mailbox& mailbox)
{
    // Frame pacing: frame N signals N on the timeline; each frame records into its own command pool.
    gfx::FrameTimeline timeline{target.device, FRAMES_IN_FLIGHT};
    gfx::FrameCommandPools command_pools{target.device, {.queue_type = gfx::QueueType::GRAPHICS, .debug_name = "editor commands"},
                                         timeline.timeline(), FRAMES_IN_FLIGHT};
    while (std::optional<FramePacket> packet = mailbox.take())
    {
        // The whole frame's allocations are counted after the warm-up: acquisition, recording, submission, presentation.
        const bool counted = target.check_allocations && target.presented.load(std::memory_order_relaxed) >= target.warm_up;
        const AllocationScope counting{g_render_allocations, counted};

        // An image (none while the window is minimized: the frame is dropped, its lease released).
        if (packet->recreate_swapchain)
        {
            target.presentation.request_recreation();
        }
        const std::optional<gfx::platform::SwapchainFrame> image = target.presentation.acquire();
        if (!image)
        {
            continue;
        }
        const std::uint64_t frame_number = timeline.begin_frame();
        command_pools.begin_frame(frame_number);
        const gfx::Extent extent = target.presentation.extent();
        const gfx::ui::UiDrawData& ui_data = packet->draw_data.data();

        // Record: the background, the scene's squares (inside the image), the UI over them; to presentation.
        gfx::CommandBuffer& commands = command_pools.command_buffer();
        {
            gfx::GraphicsRecorder recorder{commands};
            const std::array to_attachment{gfx::image_barrier(image->image.image, gfx::ResourceState::UNDEFINED, gfx::ResourceState::COLOR_ATTACHMENT_WRITE)};
            recorder.barrier({.image_barriers = to_attachment});
            target.ui_renderer.begin_frame(frame_number);
            target.ui_renderer.prepare(recorder, ui_data);
            {
                const std::array attachments{gfx::ColorAttachment{.view = image->image.view, .clear_color = packet->background}};
                gfx::RenderingScope scope = recorder.begin_rendering({
                    .area = {.width = extent.width, .height = extent.height},
                    .color_attachments = attachments,
                    .debug_name = "editor",
                });
                for (const SceneSquare& square : std::span{packet->squares}.first(packet->square_count))
                {
                    // Clipped to the image (a rectangle outside it is invalid).
                    const std::int64_t left = std::clamp<std::int64_t>(square.area.x, 0, extent.width);
                    const std::int64_t top = std::clamp<std::int64_t>(square.area.y, 0, extent.height);
                    const std::int64_t right = std::clamp<std::int64_t>(std::int64_t{square.area.x} + square.area.width, 0, extent.width);
                    const std::int64_t bottom = std::clamp<std::int64_t>(std::int64_t{square.area.y} + square.area.height, 0, extent.height);
                    if (right <= left || bottom <= top)
                    {
                        continue;
                    }
                    const std::array clear{gfx::ClearAttachment{.color = square.color}};
                    const std::array area{gfx::ClearRectangle{.area = {.x = static_cast<std::int32_t>(left),
                                                                       .y = static_cast<std::int32_t>(top),
                                                                       .width = static_cast<std::uint32_t>(right - left),
                                                                       .height = static_cast<std::uint32_t>(bottom - top)}}};
                    scope.clear_attachments(clear, area);
                }
                target.ui_renderer.record(scope, ui_data);
            }
            const std::array to_present{gfx::image_barrier(image->image.image, gfx::ResourceState::COLOR_ATTACHMENT_WRITE, gfx::ResourceState::PRESENT)};
            recorder.barrier({.image_barriers = to_present});
        }

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
        target.queue.submit(batches);
        static_cast<void>(target.presentation.present(target.queue, *image));
        target.presented.fetch_add(1, std::memory_order_relaxed);
    }
    target.device.wait_idle();
}

// ===================================================================================================== the editor's UI

/// What the editor shows and edits, and what its frame asks for (applied after the frame).
struct Editor
{
    Scene scene{};
    std::string text{};
    float text_size = 72.0F;
    float rotation = 0.0F;    ///< Degrees, clockwise.
    bool spin = true;
    float outline = 1.0F;
    bool shadow = true;
    Color text_color{.red = 0.95F, .green = 0.95F, .blue = 1, .alpha = 1};
    Color outline_color{.red = 0.1F, .green = 0.3F, .blue = 0.8F, .alpha = 1};
    bool show_text = true;
    bool show_inspector = true;
    bool show_objects = true;
    bool show_statistics = true;
    bool show_diagnostics = false;
    bool about = false;
    std::array<std::uint8_t, MAXIMUM_OBJECTS> order{}; ///< The objects in the table's order.
    std::optional<gfx::ui::ThemeKind> theme{};
    gfx::ui::ThemeKind current_theme = gfx::ui::ThemeKind::DARK;
    bool save_layout = false;
    bool load_layout = false;
    bool reset_layout = false;
    bool quit = false;
    std::string layout{};                              ///< The layout saved last (or loaded from the file).
    std::string_view status = "Ready";
    std::array<float, FRAME_TIMES> frame_times{};      ///< Milliseconds, the oldest first.
    gfx::ui::UiFrameStatistics statistics{};
    std::uint64_t frames = 0;
    std::uint64_t presented = 0;
    std::uint64_t ui_allocations = 0;     ///< Counted on the main thread after the warm-up.
    std::uint64_t render_allocations = 0; ///< Counted on the render thread after the warm-up.
    std::uint64_t warm_up = 0;
    bool counting = false;
    bool accessibility = false;
};

/// What the text panel draws with: its layout engine and a big font of the same atlases.
struct TextPreview
{
    gfx::ui::TextLayoutEngine engine;
    gfx::ui::FontHandle font{};
};

/// A "(?)" whose tooltip is @p text.
void
help_marker(UiFrame& frame,
            std::string_view text)
{
    frame.same_line();
    frame.text_disabled_unformatted("(?)");
    frame.item_tooltip(text);
}

/// The main menu bar, and the editor's shortcuts (from any window: global routes; a menu's item has its chord while
/// the menu is open).
void
main_menu(UiFrame& frame,
          Editor& editor)
{
    Scene& scene = editor.scene;
    using gfx::ui::ShortcutFlag;
    const auto global = [&frame](KeyChord chord) { return frame.shortcut(chord, ShortcutFlag::ROUTE_GLOBAL); };
    if (global(ADD_OBJECT))
    {
        add_object(scene);
    }
    if (global(DUPLICATE_OBJECT) && scene.selected)
    {
        add_object(scene, &scene.objects[*scene.selected]);
    }
    if (global(REMOVE_OBJECT))
    {
        remove_selected(scene);
    }
    editor.save_layout = editor.save_layout || global(SAVE_LAYOUT);
    editor.load_layout = editor.load_layout || global(LOAD_LAYOUT);
    editor.quit = editor.quit || global(QUIT);
    if (!frame.begin_main_menu_bar())
    {
        return;
    }
    if (frame.begin_menu("File"))
    {
        frame.set_next_item_shortcut(SAVE_LAYOUT);
        editor.save_layout = frame.menu_item("Save layout") || editor.save_layout;
        frame.set_next_item_shortcut(LOAD_LAYOUT);
        editor.load_layout = frame.menu_item("Load layout", {}, false, !editor.layout.empty()) || editor.load_layout;
        editor.reset_layout = frame.menu_item("Reset layout") || editor.reset_layout;
        frame.separator();
        frame.set_next_item_shortcut(QUIT);
        editor.quit = frame.menu_item("Quit") || editor.quit;
        frame.end_menu();
    }
    if (frame.begin_menu("Edit"))
    {
        frame.set_next_item_shortcut(ADD_OBJECT);
        if (frame.menu_item("Add object", {}, false, scene.count < scene.objects.size()))
        {
            add_object(scene);
        }
        frame.set_next_item_shortcut(DUPLICATE_OBJECT);
        if (frame.menu_item("Duplicate object", {}, false, scene.selected.has_value()))
        {
            add_object(scene, &scene.objects[*scene.selected]);
        }
        frame.set_next_item_shortcut(REMOVE_OBJECT);
        if (frame.menu_item("Remove object", {}, false, scene.selected.has_value()))
        {
            remove_selected(scene);
        }
        frame.end_menu();
    }
    if (frame.begin_menu("View"))
    {
        static_cast<void>(frame.menu_item("Text", {}, &editor.show_text));
        static_cast<void>(frame.menu_item("Inspector", {}, &editor.show_inspector));
        static_cast<void>(frame.menu_item("Objects", {}, &editor.show_objects));
        static_cast<void>(frame.menu_item("Statistics", {}, &editor.show_statistics));
        frame.separator();
        static_cast<void>(frame.menu_item("UI diagnostics", {}, &editor.show_diagnostics));
        frame.end_menu();
    }
    if (frame.begin_menu("Theme"))
    {
        using gfx::ui::ThemeKind;
        for (const auto& [kind, label] : {std::pair{ThemeKind::DARK, "Dark"}, std::pair{ThemeKind::LIGHT, "Light"}, std::pair{ThemeKind::CLASSIC, "Classic"},
                                         std::pair{ThemeKind::HIGH_CONTRAST, "High contrast"}})
        {
            if (frame.menu_item(label, {}, editor.current_theme == kind))
            {
                editor.theme = kind;
            }
        }
        frame.end_menu();
    }
    if (frame.begin_menu("Help"))
    {
        editor.about = frame.menu_item("About") || editor.about;
        frame.end_menu();
    }
    frame.end_main_menu_bar();
}

/// The inspector: the selected object's properties, the scene's.
void
inspector(UiFrame& frame,
          Editor& editor)
{
    if (!editor.show_inspector)
    {
        return;
    }
    if (frame.begin_window("Inspector", editor.show_inspector))
    {
        Scene& scene = editor.scene;
        frame.separator_text("Object");
        if (scene.selected && *scene.selected < scene.count)
        {
            SceneObject& object = scene.objects[*scene.selected];
            static_cast<void>(frame.input_text("Name", std::span{object.name}));
            static_cast<void>(frame.checkbox("Visible", object.visible));
            frame.same_line();
            static_cast<void>(frame.checkbox("Animated", object.animated));
            help_marker(frame, "An animated object moves at its velocity and bounces on the viewport's edges.");
            frame.slider("Size", object.size, 0.02F, 0.3F, "{:.2f}");
            frame.slider("Position", object.position, 0.0F, 1.0F, "{:.2f}");
            frame.drag("Velocity", object.velocity, 0.005F, -1.0F, 1.0F, "{:.2f}");
            static_cast<void>(frame.color_edit("Color", object.color, gfx::ui::ColorEditFlag::NO_ALPHA));
        }
        else
        {
            frame.text_disabled_unformatted("No object selected: choose one in the Objects table.");
        }
        frame.separator_text("Scene");
        frame.slider("Speed", scene.speed, 0.0F, 3.0F, "{:.1f}x");
        help_marker(frame, "Multiplies every object's velocity.");
        static_cast<void>(frame.color_picker("Background", scene.background, gfx::ui::ColorEditFlag::NO_ALPHA | gfx::ui::ColorEditFlag::PICKER_HUE_WHEEL));
    }
    frame.end_window();
}

/// The objects' table: sortable by name, size and speed; a click selects, a right click duplicates or removes.
void
objects_table(UiFrame& frame,
              Editor& editor)
{
    if (!editor.show_objects)
    {
        return;
    }
    if (frame.begin_window("Objects", editor.show_objects))
    {
        using gfx::ui::TableColumnFlag;
        using gfx::ui::TableFlag;
        Scene& scene = editor.scene;
        constexpr TableFlag FLAGS = TableFlag::SORTABLE | TableFlag::ROW_BACKGROUND | TableFlag::BORDERS | TableFlag::RESIZABLE | TableFlag::REORDERABLE
                                    | TableFlag::HIDEABLE | TableFlag::SCROLL_Y;
        if (frame.begin_table("objects", 5, FLAGS))
        {
            frame.table_setup_scroll_freeze(0, 1);
            frame.table_setup_column("Name", TableColumnFlag::DEFAULT_SORT | TableColumnFlag::WIDTH_STRETCH);
            frame.table_setup_column("Visible", TableColumnFlag::NO_SORT | TableColumnFlag::WIDTH_FIXED);
            frame.table_setup_column("Size", TableColumnFlag::WIDTH_FIXED, 70);
            frame.table_setup_column("Speed", TableColumnFlag::WIDTH_FIXED, 70);
            frame.table_setup_column("Color", TableColumnFlag::NO_SORT | TableColumnFlag::WIDTH_FIXED);
            frame.table_headers_row();

            // The rows in the order the table sorts them (an insertion sort: a few objects, no allocation).
            const gfx::ui::TableSortSpecifications sort = frame.table_sort_specifications();
            const auto key = [&scene](std::size_t index, std::uint32_t column)
            {
                const SceneObject& object = scene.objects[index];
                const float speed = std::hypot(object.velocity[0], object.velocity[1]);
                return column == 2 ? object.size : speed;
            };
            const auto before = [&](std::uint8_t first, std::uint8_t second)
            {
                if (sort.columns.empty())
                {
                    return first < second;
                }
                const gfx::ui::TableColumnSortSpecification& by = sort.columns.front();
                const bool ascending = by.direction != gfx::ui::SortDirection::DESCENDING;
                if (by.column == 0)
                {
                    const std::string_view a = name_of(scene.objects[first]);
                    const std::string_view b = name_of(scene.objects[second]);
                    return ascending ? a < b : b < a;
                }
                return ascending ? key(first, by.column) < key(second, by.column) : key(second, by.column) < key(first, by.column);
            };
            for (std::size_t index = 0; index < scene.count; ++index)
            {
                editor.order[index] = static_cast<std::uint8_t>(index);
                for (std::size_t place = index; place > 0 && before(editor.order[place], editor.order[place - 1]); --place)
                {
                    std::swap(editor.order[place], editor.order[place - 1]);
                }
            }

            std::optional<std::size_t> remove{};
            std::optional<std::size_t> duplicate{};
            for (std::size_t row = 0; row < scene.count; ++row)
            {
                const std::size_t index = editor.order[row];
                SceneObject& object = scene.objects[index];
                frame.push_id(static_cast<std::int32_t>(index));
                frame.table_next_row();
                frame.table_next_column();
                if (frame.selectable(name_of(object), scene.selected == index, gfx::ui::SelectableFlag::SPAN_ALL_COLUMNS))
                {
                    scene.selected = index;
                }
                if (frame.begin_popup_context_item("row menu"))
                {
                    duplicate = frame.menu_item("Duplicate") ? std::optional{index} : duplicate;
                    remove = frame.menu_item("Remove") ? std::optional{index} : remove;
                    frame.end_popup();
                }
                frame.table_next_column();
                static_cast<void>(frame.checkbox("##visible", object.visible));
                frame.table_next_column();
                frame.text("{:.2f}", object.size);
                frame.table_next_column();
                frame.text("{:.2f}", key(index, 3));
                frame.table_next_column();
                static_cast<void>(frame.color_button("##color", object.color));
                frame.pop_id();
            }
            frame.end_table();
            if (duplicate)
            {
                add_object(scene, &scene.objects[*duplicate]);
            }
            if (remove)
            {
                scene.selected = remove;
                remove_selected(scene);
            }
        }
    }
    frame.end_window();
}

/// The text panel: a text field, how its text is drawn, and the text drawn from the SDF atlas at any size, rotated,
/// outlined and shadowed (one atlas: no glyph is rasterized again).
void
text_panel(UiFrame& frame,
           Editor& editor,
           TextPreview& preview,
           const gfx::ui::FontLibrary& fonts,
           float seconds)
{
    if (!editor.show_text)
    {
        return;
    }
    if (frame.begin_window("Text", editor.show_text))
    {
        static_cast<void>(frame.input_text_multiline("##text", editor.text, {.size = {.x = -1.0F, .y = 90.0F}}));
        frame.slider("Size", editor.text_size, 12.0F, 200.0F, "{:.0f} units");
        frame.slider("Rotation", editor.rotation, -180.0F, 180.0F, "{:.0f} degrees");
        frame.same_line();
        static_cast<void>(frame.checkbox("Spin", editor.spin));
        frame.slider("Outline", editor.outline, 0.0F, 3.0F, "{:.1f} pixels");
        static_cast<void>(frame.checkbox("Shadow", editor.shadow));
        static_cast<void>(frame.color_edit("Text color", editor.text_color, gfx::ui::ColorEditFlag::NO_ALPHA));
        static_cast<void>(frame.color_edit("Outline color", editor.outline_color, gfx::ui::ColorEditFlag::NO_ALPHA));
        if (editor.spin)
        {
            editor.rotation = std::fmod(seconds * 20.0F + 180.0F, 360.0F) - 180.0F;
        }

        // The preview: the text laid out with the big font, scaled to the size asked for and rotated about the
        // region's center, clipped to the region.
        if (frame.begin_child("preview", {}, gfx::ui::ChildFlag::BORDERS))
        {
            const Vector2 position = frame.window_position();
            const Vector2 size = frame.window_size();
            const UiRectangle area{.left = position.x, .top = position.y, .right = position.x + size.x, .bottom = position.y + size.y};
            const Vector2 center{.x = (area.left + area.right) * 0.5F, .y = (area.top + area.bottom) * 0.5F};
            const float scale = editor.text_size / PREVIEW_FONT_SIZE;
            const gfx::ui::TextLayout layout = preview.engine.layout(editor.text, preview.font,
                                                                     {.wrap_width = std::max(size.x * 0.9F / scale, 1.0F),
                                                                      .alignment = gfx::ui::TextAlignment::CENTER});
            const float radians = editor.rotation * std::numbers::pi_v<float> / 180.0F;
            gfx::ui::DrawList& list = frame.window_draw_list();
            list.push_clip_rectangle(area);
            list.push_transform(gfx::ui::Transform2D::about(center, gfx::ui::Transform2D::rotation(radians) * gfx::ui::Transform2D::scaling(scale, scale)));
            list.add_text(fonts, layout, {.x = center.x - layout.width * 0.5F, .y = center.y - layout.height * 0.5F},
                          {.color = editor.text_color,
                           .outline_color = editor.outline_color,
                           .outline_width = editor.outline,
                           .shadow_color = editor.shadow ? Color{.alpha = 0.6F} : Color{.alpha = 0},
                           .shadow_offset = {.x = 3, .y = 3},
                           .shadow_softness = 2});
            list.pop_transform();
            list.pop_clip_rectangle();
        }
        frame.end_child();
    }
    frame.end_window();
}

/// The statistics: frame times, the UI's work, the render thread, allocations, accessibility.
void
statistics(UiFrame& frame,
           Editor& editor)
{
    if (!editor.show_statistics)
    {
        return;
    }
    if (frame.begin_window("Statistics", editor.show_statistics))
    {
        const float last = editor.frame_times.back();
        frame.text("Frame {} ({:.2f} ms), {} presented by the render thread", editor.frames, last, editor.presented);
        frame.plot_lines("##frame times", editor.frame_times, {.overlay = "frame time (ms)", .minimum = 0.0F, .maximum = 50.0F, .size = {.x = -1.0F, .y = 50.0F}});
        frame.progress_bar(std::min(last / 16.67F, 1.0F), {.x = -1.0F, .y = 0.0F}, "of a 60 Hz frame");
        const gfx::ui::UiFrameStatistics& work = editor.statistics;
        frame.text("UI: {} windows, {} items, {} draw lists, {} vertices, {} indices", work.windows, work.items, work.draw_lists, work.vertices, work.indices);
        frame.text("Frame arena: {} bytes", work.arena_bytes);
        const gfx::ui::TextLayoutStatistics& texts = work.text_layouts;
        frame.text("Text layouts: {} cached, {} hits, {} misses, {} replaced", texts.cached, texts.hits, texts.misses, texts.evictions);
        if (editor.counting)
        {
            frame.text("Allocations after the first {} frames: {} in the UI, {} on the render thread", editor.warm_up, editor.ui_allocations,
                       editor.render_allocations);
        }
        frame.text("Screen reader: {}", editor.accessibility ? "connected (the accessibility tree is built)" : "none");
        frame.text("Status: {}", editor.status);
    }
    frame.end_window();
}

/// The scene's window, in the middle: transparent (the render thread draws the scene behind the UI); its area.
auto
scene_window(UiFrame& frame,
             Editor& editor) -> UiRectangle
{
    UiRectangle viewport{};
    using gfx::ui::UiWindowFlag;
    if (frame.begin_window("Scene", UiWindowFlag::NO_BACKGROUND | UiWindowFlag::NO_SCROLLBAR | UiWindowFlag::NO_SCROLL_WITH_MOUSE))
    {
        const Vector2 position = frame.window_position();
        const Vector2 size = frame.window_size();
        viewport = {.left = position.x, .top = position.y, .right = position.x + size.x, .bottom = position.y + size.y};
        frame.text("{} objects. Control+N adds one, Delete removes the selected one.", editor.scene.count);
    }
    frame.end_window();
    return viewport;
}

/// The About modal.
void
about(UiFrame& frame,
      Editor& editor)
{
    if (std::exchange(editor.about, false))
    {
        frame.open_popup("About");
    }
    if (frame.begin_modal("About", gfx::ui::UiWindowFlag::ALWAYS_AUTO_RESIZE))
    {
        frame.text_unformatted("gfx UI editor example");
        frame.separator();
        frame.text_unformatted("An immediate-mode UI with docking, tables and SDF text,");
        frame.text_unformatted("drawn by a render thread with the graphics library.");
        frame.text_disabled_unformatted("Third-party software: THIRD_PARTY_LICENSES.md");
        if (frame.button("Close", {.x = 120, .y = 0}))
        {
            frame.close_current_popup();
        }
        frame.end_popup();
    }
}

/// The editor's frame: the menu bar, the dock space and its windows; the scene's viewport (UI units).
auto
editor_frame(UiFrame& frame,
             Editor& editor,
             TextPreview& preview,
             const gfx::ui::FontLibrary& fonts,
             float seconds) -> UiRectangle
{
    main_menu(frame, editor);
    static_cast<void>(frame.dock_space("editor"_dock_space));
    text_panel(frame, editor, preview, fonts, seconds);
    inspector(frame, editor);
    objects_table(frame, editor);
    statistics(frame, editor);
    const UiRectangle viewport = scene_window(frame, editor);
    about(frame, editor);
    frame.show_diagnostics_window(editor.show_diagnostics);
    return viewport;
}

/// The default layout: Text on the left, Inspector on the right, Objects and Statistics under the Scene.
void
default_layout(gfx::ui::UiContext& ui)
{
    using gfx::ui::ArrowDirection;
    gfx::ui::UiDockBuilder builder = ui.dock_builder("editor"_dock_space);
    const auto [left, rest] = builder.split(builder.root(), ArrowDirection::LEFT, 0.24F);
    const auto [right, middle] = builder.split(rest, ArrowDirection::RIGHT, 0.32F);
    const auto [bottom, center] = builder.split(middle, ArrowDirection::DOWN, 0.34F);
    builder.dock_window("Text", left);
    builder.dock_window("Inspector", right);
    builder.dock_window("Objects", bottom);
    builder.dock_window("Statistics", bottom);
    builder.dock_window("Scene", center);
    builder.finish();
}

// ===================================================================================================== the program

/// Runs the example; returns the exit code.
auto
run(const Options& options) -> int
{
    using namespace gfx::platform;

    // ---------------------------------------------------------------------------------- the window and the GPU

    const std::vector<PlatformBackendKind> backends = available_platform_backends();
    if (backends.empty())
    {
        std::println(std::cerr, "no platform backend is available: {}", platform_backend_availability(PlatformBackendKind::SDL3).reason);
        return 1;
    }
    const std::unique_ptr<Platform> platform = create_platform(options.backend.value_or(backends.front()), {.application_name = TITLE});
    WindowDescription description;
    description.title = TITLE;
    description.size = INITIAL_SIZE;
    description.minimum_size = gfx::Extent{.width = 640, .height = 360, .depth = 1};
    description.surface_api = SurfaceApi::VULKAN;
    if (options.frames)
    {
        description.flags &= ~WindowFlag::FOCUS_ON_SHOW; // automated runs leave the focus where it is
    }
    const std::unique_ptr<Window> window = platform->create_window(description);

    ValidationErrors validation_errors;
    gfx::InstanceDescription instance_description{
        .application_name = TITLE,
        .enable_validation = options.validation,
        .debug_message_severities = gfx::DebugMessageSeverity::WARNING | gfx::DebugMessageSeverity::CRITICAL,
        .debug_callback = [&validation_errors](const gfx::DebugMessage& message) { validation_errors.report(message); },
    };
    const std::unique_ptr<gfx::Instance> instance = gfx::create_instance(gfx::BackendKind::VULKAN, instance_description);
    gfx::PhysicalDevice& physical_device = options.physical_device ? instance->physical_device(*options.physical_device) : instance->select_physical_device();
    static constexpr std::array QUEUES{gfx::QueueRequest{.type = gfx::QueueType::GRAPHICS, .count = 1, .priority = 1.0F}};
    const std::unique_ptr<gfx::Device> device = physical_device.create_device({.queues = QUEUES, .debug_name = "editor device"});
    const gfx::NativeWindowHandle native_window = window->native_handle();
    WindowSwapchain presentation{*device, *window, {.format = choose_format(physical_device.surface_support(native_window)), .debug_name = "editor swapchain"}};
    std::println("{} window ({}) on '{}': {}x{} pixels, {}", platform->backend(), native_window.kind(), physical_device.properties().name,
                 presentation.extent().width, presentation.extent().height, presentation.format());

    // ---------------------------------------------------------------------------------- the UI

    // Fonts: the interface's and the text panel's, from a Latin, Greek and Cyrillic atlas with Hebrew and Arabic
    // atlases after it for the characters it lacks (shaping and bidi choose per character).
    gfx::ui::UiWindowServices services{*platform, *window};
    gfx::ui::UiContext ui{{.services = &services}};
    gfx::ui::FontLibrary& fonts = ui.fonts();
    const gfx::ui::FontAtlasHandle latin = fonts.add_atlas(gfx::ui::FontAtlas::load(ui_editor::editor_latin));
    const gfx::ui::FontAtlasHandle hebrew = fonts.add_atlas(gfx::ui::FontAtlas::load(ui_editor::editor_hebrew));
    const gfx::ui::FontAtlasHandle arabic = fonts.add_atlas(gfx::ui::FontAtlas::load(ui_editor::editor_arabic));
    ui.set_default_font(fonts.add_font({.atlas = latin, .fallbacks = {hebrew, arabic}, .size = INTERFACE_FONT_SIZE}));
    TextPreview preview{.engine = gfx::ui::TextLayoutEngine{fonts},
                        .font = fonts.add_font({.atlas = latin, .fallbacks = {hebrew, arabic}, .size = PREVIEW_FONT_SIZE})};
    gfx::ui::UiPlatformInput ui_input{*window};
    gfx::ui::UiRenderer ui_renderer{*device, fonts, {.color_format = presentation.format(), .frames_in_flight = FRAMES_IN_FLIGHT}};

    // Screen readers, where an adapter exists (Windows).
    std::optional<gfx::ui::UiAccessibilityAdapter> accessibility{};
    try
    {
        accessibility.emplace(*window);
    }
    catch (const gfx::BackendCapabilityError& error)
    {
        std::println("no screen reader adapter: {}", error.what());
    }

    // The layout: the file's, else the default one.
    Editor editor;
    editor.text = "Signed distance fields keep text sharp at any size.\nΚείμενο · Текст · טקסט · نص";
    editor.text.reserve(1024);
    editor.layout.reserve(16 * 1024);
    for (std::uint32_t object = 0; object < 6; ++object)
    {
        add_object(editor.scene);
    }
    editor.scene.selected = 0;
    editor.counting = options.check_allocations;
    editor.warm_up = options.warm_up;
    bool layout_loaded = false;
    if (options.layout && std::filesystem::exists(*options.layout))
    {
        std::ifstream file{*options.layout, std::ios::binary};
        editor.layout.assign(std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{});
        const std::expected<void, std::string> loaded = ui.load_layout(editor.layout);
        layout_loaded = loaded.has_value();
        if (!loaded)
        {
            std::println(std::cerr, "the layout of {} was not loaded: {}", options.layout->string(), loaded.error());
        }
    }
    if (!layout_loaded)
    {
        default_layout(ui);
    }

    // ---------------------------------------------------------------------------------- the threads

    // The render thread takes each frame's packet; this thread builds the next frame meanwhile.
    Mailbox mailbox;
    RenderTarget target{.device = *device, .queue = device->queue(gfx::QueueType::GRAPHICS), .presentation = presentation, .ui_renderer = ui_renderer,
                        .check_allocations = options.check_allocations, .warm_up = options.warm_up};
    std::jthread render_thread{[&target, &mailbox] { render(target, mailbox); }};

    InputSystem input{INPUT};
    const ContextActivation navigation = input.activate_context(BindingContextIdentifier::from_name(gfx::ui::UI_NAVIGATION_CONTEXT));
    const auto start = std::chrono::steady_clock::now();
    auto previous = start;
    std::uint64_t built = 0;
    std::uint64_t allocating_frames = 0;
    bool running = true;
    while (running && !editor.quit && (!options.frames || built < *options.frames))
    {
        // Events: the window's (closing, resizing: the render thread recreates the swapchain), the UI's, the input's.
        const EventSpan events = window->is_minimized() ? platform->wait_events_timeout(50ms) : platform->poll_events();
        ui_input.begin_frame();
        ui_input.add_events(events);
        bool recreate = false;
        for (const Event& event : events)
        {
            running = running && !(event.window() == window->handle() && event.type() == EventType::WINDOW_CLOSE_REQUESTED);
            recreate = recreate || requires_swapchain_recreation(event, window->handle());
        }
        const InputFrame& actions = input.update(events);

        // Time: measured, or a fixed step in sweeps, frame times shown included (the UI then draws the same frames on
        // every run, whatever the machine's speed: what is counted after the warm-up does not depend on it).
        const auto now = std::chrono::steady_clock::now();
        const float measured = std::chrono::duration<float>(now - previous).count();
        const float elapsed = options.sweep ? static_cast<float>(SWEEP_TIME_STEP) : measured;
        const double time = options.sweep ? static_cast<double>(built) * SWEEP_TIME_STEP : std::chrono::duration<double>(now - start).count();
        previous = now;
        animate(editor.scene, std::min(elapsed, 0.1F));
        std::ranges::rotate(editor.frame_times, editor.frame_times.begin() + 1);
        editor.frame_times.back() = elapsed * 1000.0F;

        // The UI's input: the window's, the gamepad's navigation, the screen reader's requests; a synthetic mouse
        // sweeping the window in automated runs.
        gfx::ui::UiFrameInput frame_input = ui_input.frame_input(time);
        frame_input.navigation = gfx::ui::navigation_input(actions);
        if (accessibility)
        {
            frame_input.accessibility_requests = accessibility->take_requests();
        }
        if (options.sweep)
        {
            // A Lissajous curve (1:3) over the window, closed every SWEEP_FRAMES frames: each cycle hovers the same items.
            const Vector2 display = frame_input.display_size;
            const float phase = 2.0F * std::numbers::pi_v<float> * static_cast<float>(built % SWEEP_FRAMES) / static_cast<float>(SWEEP_FRAMES);
            frame_input.mouse_present = true;
            frame_input.mouse_position = {.x = display.x * (0.5F + 0.47F * std::sin(phase)), .y = display.y * (0.5F + 0.45F * std::sin(3.0F * phase + 0.5F))};
        }

        // The frame (its allocations counted after the warm-up).
        UiRectangle viewport{};
        gfx::ui::UiFrameReport report{};
        {
            const AllocationScope counting{g_ui_allocations, options.check_allocations && built >= options.warm_up};
            UiFrame& frame = ui.begin_frame(frame_input);
            preview.engine.new_frame();
            viewport = editor_frame(frame, editor, preview, fonts, static_cast<float>(time));
            report = ui.end_frame();
        }
        if (accessibility)
        {
            accessibility->update(ui);
            editor.accessibility = accessibility->active();
        }
        editor.statistics = report.statistics;
        editor.frames = ++built;
        editor.presented = target.presented.load(std::memory_order_relaxed);
        const std::uint64_t ui_allocations = g_ui_allocations.load(std::memory_order_relaxed);
        const std::uint64_t render_allocations = g_render_allocations.load(std::memory_order_relaxed);
        if (ui_allocations != editor.ui_allocations || render_allocations != editor.render_allocations)
        {
            // The frames that allocated (the first few; the render thread's are counted with the frame being built).
            if (++allocating_frames <= REPORTED_ALLOCATING_FRAMES)
            {
                std::println("frame {}: {} allocation(s) counted in the UI, {} on the render thread (break on counted_allocation() in a "
                             "debugger to see where)",
                             built - 1, ui_allocations - editor.ui_allocations, render_allocations - editor.render_allocations);
            }
            editor.ui_allocations = ui_allocations;
            editor.render_allocations = render_allocations;
        }

        // What the frame asked for, outside it: the theme, the layout, the window's title.
        if (editor.theme)
        {
            editor.current_theme = *std::exchange(editor.theme, std::nullopt);
            ui.set_theme(gfx::ui::builtin_theme(editor.current_theme));
        }
        if (std::exchange(editor.save_layout, false))
        {
            editor.layout = ui.save_layout();
            editor.status = "Layout saved";
            if (options.layout)
            {
                std::ofstream{*options.layout, std::ios::binary} << editor.layout;
                editor.status = "Layout saved to its file";
            }
        }
        if (std::exchange(editor.load_layout, false) && !editor.layout.empty())
        {
            editor.status = ui.load_layout(editor.layout) ? "Layout loaded" : "The layout could not be loaded";
        }
        if (std::exchange(editor.reset_layout, false))
        {
            default_layout(ui);
            editor.status = "Layout reset";
        }

        // The packet: the UI's draw data and the scene in pixels.
        const float pixels = ui_input.pixels_per_unit();
        FramePacket packet{.draw_data = ui.acquire_draw_data(), .background = linear(editor.scene.background), .recreate_swapchain = recreate};
        const float side = viewport.height() * pixels;
        for (const SceneObject& object : std::span{editor.scene.objects}.first(editor.scene.count))
        {
            if (!object.visible || viewport.empty())
            {
                continue;
            }
            const float size = object.size * side;
            const float center_x = (viewport.left + object.position[0] * viewport.width()) * pixels;
            const float center_y = (viewport.top + object.position[1] * viewport.height()) * pixels;
            packet.squares[packet.square_count++] = {.area = {.x = static_cast<std::int32_t>(center_x - size * 0.5F),
                                                               .y = static_cast<std::int32_t>(center_y - size * 0.5F),
                                                               .width = static_cast<std::uint32_t>(std::max(size, 1.0F)),
                                                               .height = static_cast<std::uint32_t>(std::max(size, 1.0F))},
                                                      .color = linear(object.color)};
        }
        mailbox.put(std::move(packet));
    }

    // The render thread finishes the frame it has, then the GPU is idle.
    mailbox.stop();
    render_thread.join();
    const std::uint64_t ui_allocations = g_ui_allocations.load();
    const std::uint64_t render_allocations = g_render_allocations.load();
    const gfx::ui::TextLayoutStatistics& texts = ui.last_report().statistics.text_layouts;
    std::println("text layouts: {} cached, {} hits, {} misses, {} replaced, {} overflows", texts.cached, texts.hits, texts.misses, texts.evictions,
                 texts.overflows);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::println("{} frames built in {:.1f} s ({:.2f} ms each), {} presented, {} swapchain recreations, {} validation errors{}", built, seconds,
                 built > 0 ? seconds * 1000.0 / static_cast<double>(built) : 0.0, target.presented.load(), presentation.recreation_count(),
                 validation_errors.count(),
                 options.check_allocations ? std::format(", {} allocations in the UI and {} on the render thread after the first {} frames",
                                                         ui_allocations, render_allocations, options.warm_up)
                                           : std::string{});
    const bool allocated = options.check_allocations && ui_allocations + render_allocations > 0;
    return validation_errors.count() == 0 && !allocated ? 0 : 1;
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
