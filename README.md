# gfx

A C++23 graphics rendering abstraction that models Vulkan's object model (instance, physical device,
device, queues, resources, commands, synchronization) above concrete backends. It is written with
C++ modules and strongly typed numeric handles, is **bindless by default**, and follows **AZDO**
(Approaching Zero Driver Overhead) principles throughout. A companion **platform library** provides
windows and input (actions instead of keys) above SDL3 and GLFW, and plugs into the graphics library
to present frames.

| Library | Import | What it provides |
|---|---|---|
| core | `import gfx.core;` (re-exported by the others) | enumeration machinery, errors, handles, identifiers, native window handles |
| graphics | `import gfx;` | the rendering abstraction and its backends ([below](#gfx)) |
| platform | `import gfx.platform;` | windows, monitors, cursors, events, input devices, action mapping ([Platform library](#platform-library-windows-and-input)) |
| bridge | `import gfx.platform.graphics;` | swapchains for platform windows ([Presenting](#presenting-to-a-window)) |
| ui | `import gfx.ui;` | immediate-mode UI over retained state (widgets, tables, docking, navigation, accessibility), every text through SDF/MSDF atlases built at build time ([design](docs/ui-design.md)) |

> **Status:** the graphics interface is complete: objects, typed recording, bindless resources,
> parallel rendering passes, ray tracing, mesh shading, GPU-driven drawing, pipeline caches, frame
> resources, profiling and crash diagnostics. The **Vulkan backend** implements all of it, presentation
> to windows included. The SPIR-V toolchain (build-time compilation, reflection, Debug-only run-time
> compilation and hot reload) is in place. The Direct3D 12 and Metal backends are stubs that throw
> "not implemented yet". The **platform library** is complete on its **SDL3 and GLFW** backends
> (Win32, XCB, Cocoa and Web are reported as not built), and the [windowed example](#examples) runs on
> both. The design and the decisions behind the platform library are in
> [docs/platform-design.md](docs/platform-design.md); [docs/architecture.md](docs/architecture.md)
> describes the code as built, with diagrams. The **UI library** is complete
> ([docs/ui-design.md](docs/ui-design.md)): [font atlases](#ui-font-atlases) (any font format turned into
> SDF/MSDF atlases at build time, checked at compile time), [text layout](#ui-text-layout) (bidi, fallback
> fonts, shaping, line breaking), [drawing](#ui-drawing), the [immediate-mode UI](#ui-immediate-mode) with
> Dear ImGui's widgets, tables and docking, keyboard and gamepad navigation, shortcuts and an accessibility
> tree for screen readers; the [UI editor example](#examples) draws it from a render thread.

```cpp
import std;
import gfx;

int main()
{
    // The backend factory is the only entry to the backends.
    std::unique_ptr<gfx::Instance> instance = gfx::create_instance(gfx::BackendKind::VULKAN, {.enable_validation = true});
    gfx::PhysicalDevice& gpu = instance->select_physical_device();   // has every core feature

    // A graphics queue and a dedicated compute queue when the GPU has one.
    const std::array queues{gfx::QueueRequest{.type = gfx::QueueType::GRAPHICS}, gfx::QueueRequest{.type = gfx::QueueType::COMPUTE}};
    std::unique_ptr<gfx::Device> device = gpu.create_device({.queues = queues});
}
```

Every texture, storage image, sampler, storage buffer and top-level acceleration structure joins the
device's **bindless table** when it is created; shaders index the table with 32-bit indices passed in
push constants. Every pipeline uses one pipeline layout by default, so the table is bound once per
command buffer (by the backend) and a draw needs no descriptor binding at all:

```cpp
import std;
import gfx;

// What the shader reads, in push constants: bindless indices and parameters.
struct BlurPush
{
    gfx::SampledImageIndex input;   // gfx_sampled_images[input]
    gfx::SamplerIndex sampler;      // gfx_samplers[sampler]
    gfx::StorageImageIndex output;  // gfx_storage_images[output]
    float radius;
};

void blur(gfx::Device& device, std::span<const std::uint32_t> blur_spirv, gfx::CommandBuffer& command_buffer,
          const gfx::ImageView& input, const gfx::Sampler& sampler, const gfx::ImageView& output)
{
    // The shader's interface is reflected from its SPIR-V and checked against the bindless layout.
    // (Create shaders and pipelines once, at load time, not per frame.)
    auto shader = device.create_shader_module({.stage = gfx::ShaderStage::COMPUTE, .spirv = blur_spirv, .name = "blur"});
    auto pipeline = device.create_compute_pipeline({.shader = shader->handle()});   // no layout: the bindless one

    gfx::ComputeRecorder recorder{command_buffer};                                   // the bindless table is bound
    recorder.bind_pipeline(pipeline->handle());
    recorder.push_constants(BlurPush{input.sampled_index(), sampler.bindless_index(), output.storage_index(), 4.0F});
    recorder.dispatch(240, 135);
    // recorder.begin_rendering({});   // does not compile: rendering needs a graphics queue
}
```

### Shaders

Shaders are written in **GLSL or HLSL** and reach the backends as **SPIR-V**:

- **Build time**: `gfx_add_shaders()` (CMake) compiles them with glslang and embeds the SPIR-V in a
  generated C++ module, as ready-to-use `ShaderModuleDescription` constants. Release builds accept
  only this compiled SPIR-V.

  ```cmake
  gfx_add_shaders(my_app
      MODULE my_app.shaders                # import my_app.shaders;
      SHADERS shaders/blur.comp shaders/mesh.vert shaders/tonemap.frag.hlsl
      INCLUDE_DIRECTORIES shaders/include)
  ```

  ```cpp
  auto blur = device.create_shader_module(my_app::shaders::blur_comp);   // SPIR-V, stage, entry point, name
  ```

  Shaders are rebuilt when they or a file they include change. Debug builds embed source-level debug
  information (for RenderDoc) and the source path, so the shader reloads when its file changes.
- **Debug builds** also accept GLSL and HLSL source, compile it at run time, and reload shaders whose
  file changed (`device.poll_shader_changes()`, once per frame). A reload is applied only if the new
  code still fits the pipelines using it; otherwise the old code stays and the report says why.
- The interface is **reflected from the SPIR-V** (bindings with names, push constants, inputs,
  outputs, specialization constants) and checked when pipelines are created: against the pipeline
  layout, vertex attributes, the previous stage's outputs and the color attachments.
- Classic descriptor sets remain available for special cases: `device.create_pipeline_layout_from()`
  derives a layout from shaders, `layout.find_binding("u_output")` locates a binding once, and
  `set.write(location, resource)` checks the resource type.
- The tools are public too: `gfx::reflect_spirv()` returns a module's `ShaderLayout`, and (Debug
  builds) `gfx::compile_shader()` compiles GLSL or HLSL with defines and include directories.
- HLSL is compiled by glslang's HLSL front end (DXC is not used yet). Push constants are declared
  as `[[vk::push_constant]] ConstantBuffer<Parameters> parameters;`.

### Vulkan backend

| Area | Implementation |
|---|---|
| Loading | [volk](https://github.com/zeux/volk) (Vulkan 1.3 loader required; a device function table per device) |
| Memory | Vulkan Memory Allocator: dedicated, placed (aliasing) and sparse resources; CPU-written memory is always host-coherent |
| Synchronization | synchronization2 barriers, timeline semaphores per queue, deferred destruction keyed on each queue's progress |
| Rendering | dynamic rendering, secondary command buffers with inheritance for parallel passes |
| Bindless | one update-after-bind, partially bound descriptor set for the whole device |
| Pipelines | pipeline cache, background compilation on the application's task scheduler, hot reload in Debug builds |
| Features | cost-free features enabled automatically; ray tracing, ray queries, mesh shaders, variable-rate shading, sparse resources and crash markers on request (`DeviceDescription::required_features`) |
| Diagnostics | validation layers and debug messenger, object names, debug labels, RenderDoc captures from code (`Device::begin_capture`), breadcrumbs (NVIDIA diagnostic checkpoints) and `VK_EXT_device_fault` reports when the device is lost |
| Presentation | surfaces of Win32 windows (Xlib, XCB, Wayland and Metal compiled on their platforms), swapchains recreated on resize (none while the window is minimized), `VK_KHR_present_wait` for `Swapchain::wait_for_frame_latency()`, `VK_EXT_hdr_metadata`, HDR and wide-gamut color spaces (`VK_EXT_swapchain_colorspace`) |

Every function the backend cannot provide on a given GPU throws `BackendCapabilityError`, naming the
missing feature and how to get it (e.g. `wait_for_frame_latency()` on a GPU without
`DeviceFeature::FRAME_LATENCY_WAIT`). Presenting to a platform window is described
[below](#presenting-to-a-window).

**Order of waits and signals.** A queue runs its submissions in order, so a submission must not wait
for a timeline value that only a later submission to the same hardware queue signals: that signal
would wait behind the wait forever. A GPU with fewer queues than requested runs several gfx queues on
one hardware queue (an Intel UHD runs graphics, compute and transfer on its single queue), and the
rule then spans them. `Queue::submit` refuses the certain case (a later submission signaling exactly
the value waited for) with an error naming both queues. Submitting the signaling work first is always
safe.

### AZDO in gfx

| Rule | How |
|---|---|
| No descriptor work per draw | Bindless table, bound once per command buffer; one pipeline layout for every pipeline |
| No allocation or mapping per draw | `TransientBufferAllocator`: lock-free bump allocation in persistently mapped memory; data reached through bindless indices or device addresses |
| Few, large draws | Multi-draw indirect, draw count read from a buffer (GPU culling), mesh shaders |
| Few pipelines | Dynamic state (`DynamicState`), specialization constants |
| No hitches | `PipelineCache` saved across runs, background compilation (`PipelineCreationMode::ASYNCHRONOUS` on your job system) |
| No per-command overhead in shipping builds | Validation levels: per-command checks only in Debug |
| Parallel CPU work | One command pool per thread and frame (`FrameCommandPools`), parallel rendering passes, ordered submission |
| Batched transfers | `Uploader` stages every upload of a frame into one command buffer |

`CommandBuffer::statistics()` counts draws, binds, push constants and barriers, to keep them low.

### Validation levels

| Level | Checks | Default |
|---|---|---|
| `NONE` | Nothing (invalid usage is undefined behavior) | |
| `CREATION` | Object creation (descriptions, shader and pipeline layouts), start of recordings | Release |
| `COMMANDS` | Also every command, descriptor write and push-constant write | Debug |

Set it with `InstanceDescription::validation_level`; `enable_validation` additionally enables the
native API's validation (Vulkan validation layers, Direct3D 12 debug layer).

### Frame metrics (Debug builds)

`FrameMetrics` measures each frame: the frame rate and CPU frame times (average, minimum and maximum
over half a second), the commands of the frame's command buffers (draws, indirect draws, dispatches,
ray traces, pipeline, descriptor-set and vertex-buffer binds, push constants, barriers, transfers,
passes), GPU timings from a `GpuProfiler`, and device memory. It exists only for **Debug builds**: in
other builds it does nothing (`enabled()` is false, `text()` is empty) and code using it compiles
unchanged. The UI library's `gfx::ui::DebugOverlay` shows its lines as distance-field text over a
translucent rounded panel (see [UI drawing](#ui-drawing)), shown or hidden with `toggle()`.

```cpp
gfx::FrameMetrics metrics{*device};
gfx::GpuProfiler profiler{*device, FRAMES_IN_FLIGHT};
gfx::ui::DebugOverlay overlay;                                    // hidden until toggle()
gfx::ui::DrawList overlay_list;

// Each frame:
metrics.begin_frame();                                            // closes the previous frame
profiler.begin_frame(frame_number, recorder);
metrics.set_gpu_timings(profiler.timings());
overlay_list.clear();
overlay.draw(overlay_list, text, fonts, overlay_font, metrics.text()); // the previous frame's numbers
// ... the frame, then overlay_list drawn by a gfx::ui::UiRenderer (UI drawing) ...
metrics.count(commands);                                          // after recording each command buffer
if (frame.was_action_just_pressed("toggle_metrics"_action))       // e.g. F3 in an always-active context
{
    overlay.toggle();
}
```

The overlay shows the previous frame's numbers, its own draw included.

### Recording in parallel

Command buffers are not thread-safe, like every native command buffer: threads record into their own
command buffers, from their own pools (`FrameCommandPools`: one pool per frame in flight, reused once
the GPU finished that frame), and the results run in a fixed order. One rendering pass can be split
across threads:

```cpp
void record_gbuffer(gfx::CommandBuffer& primary, std::span<gfx::FrameCommandPools> worker_pools,
                    const gfx::RenderingDescription& gbuffer)
{
    gfx::GraphicsRecorder recorder{primary};
    gfx::ParallelRenderingScope pass = recorder.begin_parallel_rendering(gbuffer);
    std::vector<gfx::CommandBufferHandle> parts(worker_pools.size());
    {
        std::vector<std::jthread> workers;
        for (std::size_t index = 0; index < worker_pools.size(); ++index)
        {
            workers.emplace_back([&, index]
            {
                gfx::CommandBuffer& part = worker_pools[index].command_buffer(gfx::CommandBufferLevel::SECONDARY);
                gfx::RenderingPartRecorder part_recorder{part, pass.inheritance()};
                part_recorder.draw({.vertex_count = 3});                  // this worker's share of the draws
                // part_recorder.barrier({});                             // does not compile: not inside a pass
                parts[index] = part.handle();
            });
        }
    }
    pass.execute(parts);                                                   // in this order, whatever finished first
}
```

## Platform library: windows and input

`import gfx.platform;` gives windows and input above a **platform backend** (SDL3 or GLFW), in two
layers that can be used separately:

- **Windowing**: windows (title, icon, size and size limits, aspect ratio, modes from windowed to
  exclusive fullscreen, minimize/maximize/restore, focus, DPI), monitors and video modes, cursors
  (standard shapes, custom images, hidden and captured modes), the clipboard, and the **native window
  handle** a graphics backend creates its surface from.
- **Input**: normalized events (one 64-byte value type, a `std::variant` payload, delivered as a span:
  no allocation while pumping), physical keys (`Key`, USB HID positions) and logical keys (`KeyCode`,
  the symbol of the keyboard layout), mouse, gamepads, joysticks and touch (the keyboard, mouse and cursor
  enumerations are core's, `gfx::Key`, shared with the UI library and spelled `gfx::platform::Key` as
  well); and an **action mapping**
  system modeled on Unreal's Enhanced Input and Godot's InputMap: the game asks for actions
  (`"jump"_action`), not keys.

As in the graphics library, objects are RAII (`std::unique_ptr`), handles are typed numbers
(`WindowHandle`, `GamepadHandle`, ...), every enumeration lives in one file (`source/platform/enums.cppm`),
and a backend that cannot do something throws `BackendCapabilityError` naming the backend, the
operation, the missing feature and a workaround (`platform->supports(PlatformFeature::...)` tells
beforehand).

### A window

```cpp
import std;
import gfx.platform;

using namespace gfx::platform;

int main()
{
    // The best backend available here (SDL3, then GLFW); create_platform() explains a missing one.
    std::unique_ptr<Platform> platform = create_platform(available_platform_backends().front(), {.application_name = "demo"});

    WindowDescription description;
    description.title = "demo";                                          // UTF-8
    description.size = gfx::Extent{.width = 1280, .height = 720, .depth = 1};
    description.minimum_size = gfx::Extent{.width = 320, .height = 180, .depth = 1};
    description.surface_api = SurfaceApi::VULKAN;                        // prepared for a Vulkan swapchain
    std::unique_ptr<Window> window = platform->create_window(description);
    window->set_icon(icons);                                             // RGBA or PNG, several resolutions

    bool running = true;
    while (running)
    {
        // One pump per frame, on the thread that created the platform.
        for (const Event& event : platform->poll_events())
        {
            if (event.type() == EventType::WINDOW_CLOSE_REQUESTED)
            {
                running = false;
            }
            else if (event.type() == EventType::WINDOW_FRAMEBUFFER_RESIZED)
            {
                const gfx::Extent pixels = event.as<WindowEvent>().framebuffer_size;   // swapchain size
            }
        }
    }
}
```

`framebuffer_size()` (pixels), `size()` (window coordinates), `content_scale()` (UI scale of the monitor)
and `is_minimized()` are thread-safe: a render thread reads them without touching the platform.

### Input: actions instead of keys

A game declares its actions and their default bindings in a **schema checked at compile time**, grouped
in **contexts** (layers such as gameplay, menu, vehicle) that are activated and deactivated at run time:

```cpp
using namespace gfx::platform;
using namespace gfx::platform::literals;

constexpr auto INPUT = make_input_schema(
    actions(action("move", ActionValueType::AXIS_2D),
            action("jump"),
            action("pause"),
            action("resume"),
            action("quit")),
    context("gameplay",
            bind("move"_action, composite_2d(Key::W, Key::S, Key::A, Key::D)),        // up, down, left, right
            bind("move"_action, stick(GamepadAxis::LEFT_X, GamepadAxis::LEFT_Y)
                                    .with_deadzone(DeadzoneKind::SCALED_RADIAL, 0.15F)
                                    .with_curve(ResponseCurve::QUADRATIC)),
            bind("jump"_action, Key::SPACE),
            bind("jump"_action, GamepadButton::SOUTH),
            bind("pause"_action, Key::ESCAPE),
            bind("pause"_action, GamepadButton::START)),
    context("menu", {.priority = 10, .consumes_input = true, .blocks_lower_contexts = true},
            bind("resume"_action, Key::ESCAPE),                                        // Escape closes the menu again
            bind("quit"_action, Key::RETURN),
            bind("quit"_action, GamepadButton::SOUTH)));
// A duplicate action, a binding of an undeclared action, a 2D composite on a button action or a hold
// trigger on an axis does not compile ("call to non-constexpr function 'input_schema_error_...()'").

InputSystem input{INPUT};
ContextActivation gameplay = input.activate_context("gameplay"_context);        // active until destroyed

// Once per frame: the events of the pump become a frame of actions.
const InputFrame& frame = input.update(platform->poll_events());
if (frame.was_action_just_pressed("jump"_action))
{
    jump();
}
const gfx::Vector2 move = frame.get_action_value_2d("move"_action);              // dead zone and curve applied
const bool sprinting = frame.is_action_pressed("sprint"_action);                 // InvalidUsageError: undeclared
```

Bindings can be single inputs, chords (`chord(Key::S, KeyModifier::CONTROL)` does not also press `S`),
gamepad chords, mouse axes, 1D and 2D composites and sticks, with dead zones (axial, radial, scaled
radial), response curves, inversion, scaling, swizzling, and hold or tap triggers
(`single(GamepadButton::WEST).held_for(500ms)`). The highest-priority active context sees an input
first; it may consume it or block every context below it. A context ignores keys and buttons that are
already held when it starts reading them (it is activated, a context above it goes away, or text input
ends) until they are released: the Escape that opens a menu does not also press the menu's "resume", and
the Escape that closes it does not press gameplay's "pause" (`ContextDescription::ignore_held_inputs`,
on by default, like Unreal's "ignore all pressed keys until release"; sticks and triggers are never
ignored).

### Presenting to a window

`import gfx.platform.graphics;` connects the two libraries. The window's native handle is all the
graphics library needs, and `WindowSwapchain` does the rest: it creates the swapchain, recreates it when
the framebuffer is resized or the swapchain reports itself out of date, skips frames while the window is
minimized, and owns the presentation semaphores:

```cpp
import gfx.platform.graphics;   // gfx and gfx.platform included

const gfx::SurfaceSupport support = physical_device.surface_support(window->native_handle()); // formats, modes
gfx::platform::WindowSwapchain presentation{*device, *window};                // FIFO, 3 images, B8G8R8A8_SRGB
for (const gfx::platform::Event& event : events)
{
    presentation.handle_event(event);                                         // framebuffer resizes
}
if (const std::optional<gfx::platform::SwapchainFrame> frame = presentation.acquire())
{
    // Record: frame->image.image from UNDEFINED to COLOR_ATTACHMENT_WRITE, render into
    // frame->image.view, then to PRESENT. The submission waits on frame->acquired and signals
    // frame->rendered.
    const std::array waits{gfx::SemaphoreWait{.semaphore = frame->acquired}};
    const std::array signals{gfx::SemaphoreSignal{.semaphore = frame->rendered}};
    const std::array batches{gfx::SubmitDescription{.command_buffers = commands, .waits = waits, .signals = signals}};
    queue.submit(batches);
    presentation.present(queue, *frame);
}
```

The [windowed example](#examples) shows the whole loop, with frame pacing (`FrameTimeline`,
`FrameCommandPools`).

### Use cases

**A pause menu above gameplay.** The menu context has a higher priority and blocks the contexts below
it, so gameplay actions read nothing while it is open. One key may open and close it: the press that
opens the menu is ignored by the menu until the key is released (`ignore_held_inputs`):

```cpp
std::optional<ContextActivation> menu;
if (frame.was_action_just_pressed("pause"_action))              // gameplay: Escape
{
    menu = input.activate_context("menu"_context);              // above gameplay until reset
}
if (frame.was_action_just_pressed("resume"_action))             // menu: Escape as well
{
    menu.reset();
}
```

**Couch multiplayer.** Each player has their own contexts, gamepad and action states; the keyboard
and mouse belong to one player:

```cpp
InputSystem input{INPUT, {.players = 2, .gamepad_assignment = GamepadAssignment::AUTOMATIC}};  // first gamepad used joins
ContextActivation first = input.activate_context("gameplay"_context, PlayerIdentifier{0});
ContextActivation second = input.activate_context("gameplay"_context, PlayerIdentifier{1});
input.assign_gamepad(gamepad, PlayerIdentifier{1});                                  // or route explicitly
const bool second_jumps = frame.is_action_pressed("jump"_action, PlayerIdentifier{1});
```

**A rebinding menu.** Capture the next input, find conflicts, rebind, save the result (JSON naming
keys, not numbers), and load it at the next start (all or nothing; errors give the JSON path):

```cpp
input.capture_next_binding();                                       // the next press does not reach the actions
if (const auto& captured = input.frame().captured_binding(); captured && !captured->canceled)
{
    const std::vector<BindingSlot> conflicts = input.find_bindings_using(captured->binding.sources[0]);
    input.rebind({.context = "gameplay"_context, .action = "jump"_action, .slot = 0}, captured->binding);
}
std::ofstream{"bindings.json"} << input.save_bindings();
```

**Typing in a chat box.** Text arrives as `TEXT_INPUT` events (UTF-8, after the input method's
composition), separate from key presses; while text input is active, keyboard bindings read nothing,
so typing "w" does not move the character:

```cpp
window->start_text_input({.area = chat_box_rectangle});
input.set_text_input_active(true);
for (const Event& event : events)
{
    if (event.type() == EventType::TEXT_INPUT)
    {
        chat_line += events.text(event);
    }
}
```

**A game thread separate from the main thread.** The main thread owns the platform and pumps it (the
operating system requires it on some platforms); an `EventChannel` hands each pump's events to the game
thread without locks:

```cpp
EventChannel channel;
// main thread, every pump (even an empty one):  channel.send(platform->poll_events());
// game thread, every frame:                     const InputFrame& frame = input.update(channel.receive());
```

**Jobs reading input while the next frame is prepared.** `InputFrame` is immutable once `update()`
returns and stays valid until the second `update()` after it, so job threads read it by reference
without locks while the game thread builds the next one (double buffering).

**Tests and replays.** `platform->push_event(event)` injects events from any thread (they arrive at the
next pump), and `input.update(events, time)` evaluates them at a given time, which makes hold and tap
triggers testable.

### Threads

| Who | May call |
|---|---|
| The thread that created the platform | everything: pumps, windows, cursors, the clipboard, input devices |
| Any thread | `Window::framebuffer_size()`, `size()`, `content_scale()`, `is_minimized()`, `native_handle()`; `Platform::push_event()`, `wake()` |
| The game thread (one) | `InputSystem` (`update()`, contexts, rebinding) |
| Any number of readers | an `InputFrame` returned by `update()`, until the second `update()` after it |

### Backends

| Feature | SDL3 | GLFW |
|---|---|---|
| Windows, modes, monitors, standard and custom cursors, text clipboard, keyboard, mouse, gamepads, joysticks | yes | yes |
| Window icons, positions | yes | yes (not on Wayland; no icons on macOS) |
| Occlusion state, IME composition, image clipboard, dropped text, touch | yes | no |
| Gamepad rumble, trigger rumble, LEDs, touchpads | yes | no |
| Gamepad sensors | not yet in gfx | no |

What a backend lacks throws `BackendCapabilityError` where it is used; `Platform::supports()` reports it
beforehand. One backend is active at a time (both pump the same thread's message queue).

## UI font atlases

Every text of the UI library is drawn from signed distance fields (SDF, PSDF, MSDF or MTSDF), built when
the program is built: `gfx_add_ui_atlas()` runs the atlas tool on a font (TrueType, OpenType CFF and CFF2,
collections, WOFF, WOFF2, variable fonts at any instance) and can embed the result in a module, which
`gfx::ui::AtlasView` reads at compile time and at run time.

```cmake
include(GfxUiAtlas)
gfx_add_ui_atlas(my_application
    NAME interface_font
    FONT fonts/NotoSans.ttf
    CHARACTERS BASIC_LATIN LATIN_1_SUPPLEMENT U+2013-U+2014 U+20AC   # sets, ranges, code points
    VARIATION wght=500                                        # or NAMED_INSTANCE Bold
    KIND MTSDF SIZE 40 RANGE 6
    EMBED MODULE my_application.fonts)
```

```cpp
import gfx.ui;
import my_application.fonts; // generated: inline constexpr std::array<std::byte, N> interface_font

constexpr auto INTERFACE_FONT = gfx::ui::AtlasView::parse(interface_font, gfx::ui::AtlasVerification::STRUCTURE);
static_assert(INTERFACE_FONT.has_value(), "the atlas is valid");
static_assert(INTERFACE_FONT->find_character(U'é').has_value(), "a label uses a character the atlas lacks");
```

An atlas holds its pages (zstd-compressed), glyph boxes and metrics, the character map, kerning and
ligatures for simple scripts, and a subset of the font for HarfBuzz (the requested characters and every
form shaping can substitute them with). The same inputs give the same bytes. `gfx_ui_atlas_builder --help`
lists the tool's options; the format is described in [docs/ui-atlas-format.md](docs/ui-atlas-format.md),
and `import gfx.ui.atlas_tools;` builds atlases from C++ (custom decoders, generators and packers included).
`gfx::ui::atlas_covers(interface_font, u8"Ångström")` checks in a `static_assert` that an embedded atlas
has every character of a label.

## UI text layout

Atlases are loaded into a `FontLibrary`; a font is an atlas, the atlases to fall back on for the
characters it lacks, a size and a language. A `TextLayoutEngine` turns UTF-8 text into positioned glyphs:
paragraphs, bidi levels (SheenBidi), scripts, the fallback atlas of every character, shaping (HarfBuzz with
the atlas's subset font, or a basic shaper for simple left-to-right runs), line breaking (libunibreak),
visual reordering and alignment. Layouts are cached: laying the same text out again allocates nothing.

```cpp
import gfx.ui;
import my_application.fonts;

gfx::ui::FontLibrary fonts;
const gfx::ui::FontAtlasHandle latin = fonts.add_atlas(gfx::ui::FontAtlas::load(interface_font));
const gfx::ui::FontHandle body = fonts.add_font({.atlas = latin, .size = 16.0F});
gfx::ui::TextLayoutEngine text{fonts};

// Every frame: laying the same text out again is a cache hit (no allocation).
text.new_frame();
const gfx::ui::TextLayout layout = text.layout("Hello, world: text wraps at word boundaries.", body, {.wrap_width = 200.0F});
for (const gfx::ui::LaidOutGlyph& glyph : layout.glyphs)
{
    // glyph.atlas and glyph.glyph name its box in an atlas page; glyph.x, glyph.y its pen position.
}
```

`caret_position()` and `hit_test()` map byte offsets to caret positions and back. Problems never throw
during a frame: they are reported in `TextLayout::problems` (unknown font, invalid UTF-8, missing glyphs
drawn as `.notdef`, a run that needed a shaping font, a full cache).

## UI drawing

A `DrawList` collects what to draw, in order, in pixels of the target (y down): rectangles (filled,
bordered, with rounded corners), circles and rings, lines, polylines and convex polygons, images (by
bindless index) and laid-out text, under a stack of transforms (`Transform2D`: translation, scale,
rotation) and a stack of clip rectangles. The fragment shader evaluates shapes as signed distances and
glyphs from their atlas's distance field, so edges stay one pixel wide at any scale or rotation; text can
have an outline and a soft shadow or glow (`TextStyle`). `UiRenderer` draws lists through the graphics
interfaces: one pipeline on the bindless layout, vertices pulled from a per-frame transient buffer, one
indexed draw per clip rectangle and texture, atlas pages uploaded the first time they are drawn.

```cpp
gfx::ui::UiRenderer renderer{*device, fonts, {.color_format = presentation.format(), .frames_in_flight = FRAMES_IN_FLIGHT}};
gfx::ui::DrawList list;

// Each frame:
list.clear();
list.add_rectangle_filled({.left = 20, .top = 20, .right = 220, .bottom = 76}, {.red = 0.1F, .green = 0.1F, .blue = 0.12F, .alpha = 0.9F}, 8);
list.add_text(fonts, text.layout("Settings", body), {.x = 32, .y = 36}, {.shadow_color = {.alpha = 0.6F}, .shadow_offset = {.x = 1, .y = 1}});
const std::array lists{&list};
const gfx::ui::UiDrawData ui{.lists = lists, .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height)};
renderer.begin_frame(frame_number);                               // the frame's region of the geometry buffer
renderer.prepare(recorder, ui);                                   // before the pass: uploads new atlas pages
{
    gfx::RenderingScope scope = recorder.begin_rendering({.area = area, .color_attachments = attachments});
    renderer.record(scope, ui);
}
```

Colors are sRGB-encoded and not premultiplied: on an sRGB target the shader converts them to linear, so
they read back as given. `UiDrawData::scale` is the pixels per unit of the lists' coordinates (a UI laid
out in points on a high-density display). `statistics()` reports draws, vertices, resident pages, uploaded
bytes and the commands skipped when a frame's geometry exceeds `UiRendererDescription::geometry_bytes_per_frame`.

## UI: immediate mode

`UiContext` is an immediate-mode UI in the manner of Dear ImGui: every frame, the application describes its
windows and widgets on the `UiFrame` that `begin_frame()` returns (`if (frame.button("Save")) save();`), and the
context keeps what must survive between frames (window places and sizes, the active and focused items). There
are no widget objects and no callbacks. `gfx.ui.platform` feeds a context from a gfx platform window (its
events, clipboard, cursors and text input); any other windowing fills a `UiFrameInput` and implements
`UiPlatformServices` itself.

```cpp
import gfx.ui;
import gfx.ui.platform;
import my_application.fonts;

gfx::ui::UiWindowServices services{*platform, *window};
gfx::ui::UiContext ui{{.services = &services}};
ui.load_font(gfx::ui::FontAtlas::load(interface_font), 16.0F);    // UI units are points
gfx::ui::UiPlatformInput input{*window};
gfx::ui::UiRenderer renderer{*device, ui.fonts(), {.color_format = presentation.format()}};

// Each frame, with the events of this pump:
input.begin_frame();
input.add_events(events);
gfx::ui::UiFrame& frame = ui.begin_frame(input.frame_input(seconds));
frame.set_next_window_position({.x = 20, .y = 20}, gfx::ui::UiCondition::FIRST_USE_EVER);
if (frame.begin_window("Inspector"))
{
    frame.text("{} objects", object_count);
    frame.checkbox("Visible", visible);
    if (frame.button("Reset"))
    {
        reset();
    }
}
frame.end_window();
const gfx::ui::UiFrameReport report = ui.end_frame();             // problems of the frame, if any
// renderer.prepare(recorder, ui.draw_data()) before the pass, renderer.record(scope, ui.draw_data()) in it
```

- **Windows** move (by their title bar or any empty space), resize (bottom-right grip), collapse, close,
  scroll (wheel, scrollbars, by code), fit their content, stack (a click brings one to the front), and hold
  child regions that scroll on their own. `set_next_window_*()` places, sizes and focuses the next window,
  with a `UiCondition` (always, once, first use, appearing).
- **Widgets**: text (formatted with `std::format`, colored, disabled, wrapped, bulleted, labeled), buttons
  (normal, small, invisible, arrow), checkboxes (also over bitmask flags), radio buttons, switches
  (`toggle()`), selectables, separators (with text), bullets. **Layout**: `same_line()`, groups, indentation,
  spacing, columns, the cursor, item widths, wrap positions; `begin_disabled()`.
- **Identifiers**: a widget's label hashed with the identifier stack (the window, `push_id()`); `"Save##bar"`
  shows `Save`, `"Saved 3###status"` keeps the identifier of `###status`. Two interactive items with one
  identifier in a frame are reported.
- **Keyboard**: Tab and Shift+Tab move the focus through a window's items, Space or Return activates the
  focused one, Escape drops the focus. `wants_mouse()` and `wants_keyboard()` tell the application what the
  UI used.
- **Styles and themes**: `Style` (every size, padding, rounding and alignment) and a color per `StyleColor`
  make a `Theme`; `builtin_theme()` makes the dark, light, classic and high-contrast themes at compile time;
  `push_style_color()`, `push_style_variable()` and `push_font()` change them for a part of a frame.
- **Nothing throws in a frame**: misuse (an end without a begin, a widget outside any window, unbalanced
  stacks, identifier conflicts, a missing font, an exceeded capacity) is reported in the `UiFrameReport` that
  `end_frame()` returns; `show_diagnostics_window()` shows the last one. After its first frames, a frame
  allocates nothing (a test counts `operator new`).
- **Threads**: frames run on one thread; `acquire_draw_data()` gives any thread the last published draw data,
  whose lists the context does not reuse while the lease lives ([below](#ui-a-render-thread)).

## UI: input widgets

Text fields, numbers, colors and lists edit the application's own values in place and return true when they
changed them:

```cpp
struct Settings
{
    std::string name = "Ada";
    std::array<char, 260> path{};
    float exposure = 1.0F;
    std::int32_t samples = 16;
    std::array<float, 3> position{};
    gfx::ui::Color tint = gfx::ui::Color::from_rgba8(255, 200, 120);
    std::size_t quality = 1;
};

void
edit(gfx::ui::UiFrame& frame,
     Settings& settings)
{
    using namespace gfx::ui;
    constexpr std::array<std::string_view, 3> QUALITIES{"Low", "Medium", "High"};
    if (frame.begin_window("Settings"))
    {
        frame.input_text("Name", settings.name, {.hint = "your name"});
        frame.input_text("Path", settings.path, {.flags = InputTextFlag::ELIDE_LEFT});
        frame.slider("Exposure", settings.exposure, 0.01F, 100.0F, "{:.2f} EV", SliderFlag::LOGARITHMIC);
        frame.input_number("Samples", settings.samples, 1, 8);
        frame.drag("Position", settings.position, 0.05F);
        frame.color_edit("Tint", settings.tint);
        frame.combo("Quality", settings.quality, QUALITIES);
    }
    frame.end_window();
}
```

- **Text fields** edit a `std::string` (grown as needed) or a fixed `char` buffer (NUL-terminated), on one line
  or several. A click places the cursor (Shift extends, a double click selects a word, a drag selects); Tab
  into a field selects its text. Arrows move by graphemes, Control+arrows by words (libunibreak's rules),
  Home and End, Up and Down between lines; Backspace and Delete erase (with Control: words);
  Control+A, C, X, V, Z and Y (and Shift+Delete, Control+Insert, Shift+Insert) follow the keyboard layout's
  symbols, and undo goes back word by word. Return confirms, Escape reverts, Tab moves on (or types a tab with
  `ALLOW_TAB_INPUT`). Flags filter characters (decimal, hexadecimal, scientific, uppercase, no blanks) and make
  fields read-only, passwords (`*` per character, never copied), overwriting, select-all-on-focus or
  elided on the left; empty fields show a hint. `TextEditor` (the editing model) and `TextBoundaries`
  (grapheme and word boundaries) are public and usable without a UI.
- **IME**: while a field is edited the context asks the platform for text input at its cursor
  (`UiPlatformServices::set_text_input()`, so candidate windows open there), and draws the composition in
  progress (`UiFrameInput::composition`) underlined in place of the selection until it is committed as typed
  text. `gfx.ui.platform` passes SDL3's compositions; GLFW reports none (the system shows them).
- **Numbers**: `input_number()` (with step buttons), `slider()` and `drag()` take any arithmetic type, and
  arrays, spans or vectors of it (one component each, side by side). Formats are `std::format` formats
  (`"{:.2f} m/s"`); values are rounded to the format's precision, and typed values are parsed and clamped to
  the type (an `std::uint8_t` field given 300 holds 255). Control+click, a double click or Return types a
  value into a slider or a drag; Shift drags ten times faster, Alt ten times slower; sliders may be
  logarithmic, drags may wrap around their range.
- **Colors**: `color_edit()` (red, green, blue and alpha drags as 0 to 255 or 0 to 1, or hue, saturation and
  value, or a hexadecimal field, and a preview that opens a picker), `color_picker()` (a saturation/value
  square and a hue bar, or a hue wheel around a triangle; an alpha bar; a side preview) and `color_button()`.
  `to_hsv()`, `from_hsv()`, `to_hexadecimal()` and `from_hexadecimal()` are `constexpr`.
- **Lists**: `combo()` (a popup lists the items, a choice closes it) and `list_box()` (a framed region that
  scrolls), over any random-access range of things convertible to `std::string_view`; `begin_combo()` and
  `begin_list_box()` take any items. Popups float over every window and close on a click outside, Escape or a
  choice.
- An idle frame of every input widget still allocates nothing (a test counts `operator new`).

## UI: menus, trees, tabs, popups, tooltips, drag and drop

```cpp
void
outliner(gfx::ui::UiFrame& frame,
         Scene& scene)
{
    using namespace gfx::ui;
    if (frame.begin_window("Outliner", UiWindowFlag::MENU_BAR))
    {
        if (frame.begin_menu_bar())
        {
            if (frame.begin_menu("File"))
            {
                if (frame.menu_item("Save", "Ctrl+S"))
                {
                    scene.save();
                }
                frame.menu_item("Autosave", {}, &scene.autosave);
                frame.end_menu();
            }
            frame.end_menu_bar();
        }
        if (frame.begin_tab_bar("Views", TabBarFlag::REORDERABLE))
        {
            if (frame.begin_tab_item("Objects"))
            {
                for (Object& object : scene.objects)
                {
                    frame.push_id(object.id);
                    const bool open = frame.tree_node(object.name, object.children.empty() ? TreeNodeFlag::LEAF : TreeNodeFlag::NONE);
                    frame.item_tooltip("Drag it onto another object to parent it");
                    if (frame.begin_drag_drop_source())
                    {
                        frame.set_drag_drop_payload("object", object.id);
                        frame.text_unformatted(object.name);
                        frame.end_drag_drop_source();
                    }
                    if (frame.begin_drag_drop_target())
                    {
                        if (const auto payload = frame.accept_drag_drop_payload("object"))
                        {
                            scene.parent(payload->data, object);
                        }
                        frame.end_drag_drop_target();
                    }
                    if (frame.begin_popup_context_item("object menu"))
                    {
                        if (frame.menu_item("Delete"))
                        {
                            frame.open_popup("Delete?");
                        }
                        frame.end_popup();
                    }
                    if (open)
                    {
                        frame.tree_pop();
                    }
                    frame.pop_id();
                }
                frame.end_tab_item();
            }
            frame.end_tab_bar();
        }
        if (frame.begin_modal("Delete?", UiWindowFlag::ALWAYS_AUTO_RESIZE))
        {
            frame.text_unformatted("Delete the object and its children?");
            if (frame.button("Delete"))
            {
                frame.close_current_popup();
            }
            frame.end_popup();
        }
    }
    frame.end_window();
}
```

- **Menus**: a window's menu bar (`UiWindowFlag::MENU_BAR`) or the display's (`begin_main_menu_bar()`); a press
  opens a menu, hovering another menu of the bar switches to it; rows of a menu open their submenus on hover
  (a move toward an open submenu keeps it open); `menu_item()` shows a shortcut's text and a check mark. From
  the keyboard: Tab to a bar menu, Down or Return opens it with its first item focused, Up and Down move,
  Right opens a submenu, Left closes it, Return chooses, Escape closes (the focus goes back to the menu).
- **Trees and headers**: `tree_node()` remembers its open state by identifier; flags open it on the arrow
  only or on a double click, make it a leaf or a bullet, span the width; Left and Right close and open it
  from the keyboard. `collapsing_header()` is a framed node, optionally with a close button;
  `set_next_item_open()` opens or closes the next one (always, once, when its window appears).
- **Tab bars**: the first tab is selected at once, a press selects another from the next frame; closable tabs
  (their close button or the middle button), reordering by dragging, leading and trailing tabs, tab buttons,
  a list of the tabs, tabs that shrink or scroll when they do not fit, tooltips for cut labels.
- **Popups**: `open_popup()` + `begin_popup()` at the mouse, stacked when opened from a popup, closed by a
  click outside, Escape, a choice or `close_current_popup()`; context popups on a right release over an item
  or a window; **modals** (`begin_modal()`): centered, the rest of the UI dimmed and unreachable until code
  (or their close button) closes them.
- **Tooltips**: `tooltip()` / `begin_tooltip()` at the mouse over everything; `item_tooltip()` once the mouse
  has stayed still over the item for `TOOLTIP_DELAY_SECONDS`.
- **Drag and drop**: an item held and moved past the drag threshold carries a typed payload (copied; any
  trivially copyable value) and a preview tooltip; targets accept payloads of their type, the smallest one
  under the mouse winning, and receive them when the button is released (or peek before). A payload held over
  a closed tree node or a tab opens it. Color buttons carry their color (`COLOR_PAYLOAD_TYPE`) onto color
  editors; a right click on a color editor opens its options.

## UI: tables, lists, splitters, plots

```cpp
void
file_browser(gfx::ui::UiFrame& frame,
             Folder& folder)
{
    using namespace gfx::ui;
    if (frame.begin_window("Files"))
    {
        constexpr TableFlag FLAGS = TableFlag::RESIZABLE | TableFlag::REORDERABLE | TableFlag::HIDEABLE | TableFlag::SORTABLE
                                    | TableFlag::SORT_MULTIPLE | TableFlag::ROW_BACKGROUND | TableFlag::BORDERS | TableFlag::SCROLL_Y;
        if (frame.begin_table("files", 3, FLAGS, {.x = 0, .y = 300}))
        {
            frame.table_setup_column("Name", TableColumnFlag::DEFAULT_SORT);
            frame.table_setup_column("Size", TableColumnFlag::WIDTH_FIXED, 80.0F);
            frame.table_setup_column("Kind");
            frame.table_setup_scroll_freeze(0, 1);
            frame.table_headers_row();
            if (const TableSortSpecifications sort = frame.table_sort_specifications(); sort.changed)
            {
                folder.sort(sort.columns);
            }

            // 100000 files: only the rows in view are submitted.
            UiListClipper clipper;
            clipper.begin(frame, static_cast<std::uint32_t>(folder.files.size()));
            while (clipper.step())
            {
                for (std::uint32_t row = clipper.display_start(); row < clipper.display_end(); ++row)
                {
                    File& file = folder.files[row];
                    frame.table_next_row();
                    frame.table_next_column();
                    frame.push_id(static_cast<std::int32_t>(row));
                    frame.selectable(file.name, &file.selected, SelectableFlag::SPAN_ALL_COLUMNS);
                    frame.pop_id();
                    frame.table_next_column();
                    frame.text("{} KB", file.size / 1024);
                    frame.table_next_column();
                    frame.text_unformatted(file.kind);
                }
            }
            frame.end_table();
        }
        frame.plot_lines("Read speed", folder.read_speeds, {.overlay = "MB/s", .size = {.x = 0, .y = 60}});
        frame.progress_bar(folder.indexing ? -1.0F : 1.0F, {}, folder.indexing ? "Indexing" : "Indexed");
    }
    frame.end_window();
}
```

- **Tables** (`begin_table()`): columns described every frame (`table_setup_column()`), fixed ones as wide as
  asked or as their content, stretched ones sharing the rest by weight (the `SIZING_*` policies choose the
  defaults). The mouse resizes a column by its right border (a double click fits it), moves one by dragging
  its header, sorts by a click on a header (Shift+click adds a column with `SORT_MULTIPLE`; `SORT_TRISTATE`
  allows no sort), and a right click on the header row opens the context menu: sizes, order, which columns are
  shown. `table_sort_specifications()` says how to sort and whether that changed. A scrolling table
  (`SCROLL_X`, `SCROLL_Y`) keeps its first rows and columns in view (`table_setup_scroll_freeze()`). Widths,
  order, visibility and sorting are kept between frames, also while the table is not submitted. Rows alternate
  their background (`ROW_BACKGROUND`), borders are drawn as asked, `table_set_background_color()` colors a row
  or a cell, selectables can span every column, and tree nodes indent the next rows' first column. Each column
  draws into its own channel (`DrawListSplitter`), so a table's cells merge into a few draw commands.
- **List clipper** (`UiListClipper`): of a long list of items of one height (a table's rows), only those in
  view (and those asked for with `include_item()`) are submitted; the cursor moves past the others, so the
  window still scrolls over the whole list. A table of 10000 rows scrolled every frame allocates nothing.
- **Splitters and resizable regions**: `splitter()` moves size between two areas; a child region with
  `ChildFlag::RESIZE_X` / `RESIZE_Y` is resized by its right or bottom edge (the size is kept).
- **Plots, progress bars, images**: `plot_lines()` and `plot_histogram()` (sampled to the pixels, the values
  under the mouse in a tooltip), `progress_bar()` (a negative fraction: indeterminate), `image()`,
  `image_rounded()` and `image_button()` (bindless sampled-image indices).

## UI: docking and layouts

```cpp
using namespace gfx::ui::literals;

// The editor's layout the first time (no saved layout): Outliner on the left, Console under the Scene.
void
default_layout(gfx::ui::UiContext& ui)
{
    gfx::ui::UiDockBuilder builder = ui.dock_builder("editor"_dock_space);
    const auto [left, rest] = builder.split(builder.root(), gfx::ui::ArrowDirection::LEFT, 0.2F);
    const auto [bottom, center] = builder.split(rest, gfx::ui::ArrowDirection::DOWN, 0.25F);
    builder.dock_window("Outliner", left);
    builder.dock_window("Console", bottom);
    builder.dock_window("Scene", center);
    builder.finish();
}

// Each frame: a dock space over the display (its empty center shows the 3D view behind), then the windows,
// docked or floating as the user arranged them.
void
editor_frame(gfx::ui::UiFrame& frame,
             Editor& editor)
{
    frame.dock_space("editor"_dock_space, {}, gfx::ui::DockNodeFlag::PASS_THROUGH_CENTRAL_NODE);
    if (frame.begin_window("Outliner"))
    {
        frame.text("{} objects", editor.objects.size());
    }
    frame.end_window();
    if (frame.begin_window("Console", editor.console_open))
    {
        frame.text_unformatted(editor.log);
    }
    frame.end_window();
    if (frame.begin_window("Scene"))
    {
        frame.text("{} draw calls", editor.draw_calls);
    }
    frame.end_window();
}

// At start and at exit: the layout as JSON (windows, dock trees, tables' columns).
void
restore_layout(gfx::ui::UiContext& ui,
               const std::optional<std::string>& saved)
{
    if (!saved || !ui.load_layout(*saved))
    {
        default_layout(ui);
    }
}

auto
layout_to_save(const gfx::ui::UiContext& ui) -> std::string
{
    return ui.save_layout();
}
```

- **Dock spaces** (`dock_space()`): in a window, or over the whole display when called outside any window. A dock
  space's layout is a tree of nodes split side by side or one above the other; each leaf holds docked windows as the
  tabs of its tab bar (the selected one is shown: the others' `begin_window()` returns false). The user drags a
  window over a node or another window: five squares offer to dock it as a tab or beside the node (a preview shows
  where); dragging a tab out of its bar undocks its window, dragging a separator resizes the nodes. Windows docked
  together outside a dock space form a floating tree with a host window that moves and resizes them.
- **By code**: `UiContext::dock_builder()` splits a dock space's root and docks windows by name;
  `set_next_window_dock()` docks the next window (by condition: always, once, the first time, when it appears);
  `is_window_docked()` and `window_dock_node()` tell where the current window is. `DockNodeFlag` keeps the central
  node free of docking or lets the mouse through it, forbids splits, undocking or resizing, hides a lone tab bar.
- **Layouts**: `save_layout()` writes the windows' places, every dock tree and the tables' columns (widths, order,
  visibility, sort) as JSON; `load_layout()` restores them, all or nothing, reporting each problem of a document with
  its JSON path. Windows and tables with `NO_SAVED_SETTINGS` are left out.

## UI: navigation, shortcuts and accessibility

```cpp
using namespace gfx::platform::literals;

// The game's schema with the UI's navigation actions and context: the keyboard's keymap and a gamepad's
// (A activates, B cancels, the d-pad and the left stick move the focus), rebindable and saved like the game's.
constexpr auto SCHEMA = gfx::platform::make_input_schema(
    gfx::ui::with_ui_navigation_actions(gfx::platform::actions(gfx::platform::action("jump"))),
    gfx::platform::context("gameplay", gfx::platform::bind("jump"_action, gfx::Key::SPACE)),
    gfx::ui::ui_navigation_context());

constexpr gfx::ui::KeyChord SAVE{.key = gfx::KeyCode::S, .modifiers = gfx::KeyModifier::CONTROL};

// Each frame: the navigation commands of the input system and the requests of screen readers go to the UI; the
// accessibility tree goes back to them.
void
editor_frame(gfx::ui::UiContext& ui,
             gfx::ui::UiPlatformInput& input,
             gfx::ui::UiAccessibilityAdapter& accessibility,
             const gfx::platform::InputFrame& actions,
             double seconds,
             Editor& editor)
{
    gfx::ui::UiFrameInput frame_input = input.frame_input(seconds);
    frame_input.navigation = gfx::ui::navigation_input(actions);
    frame_input.accessibility_requests = accessibility.take_requests();
    gfx::ui::UiFrame& frame = ui.begin_frame(frame_input);
    if (frame.begin_window("Editor", gfx::ui::UiWindowFlag::MENU_BAR))
    {
        if (frame.begin_menu_bar())
        {
            if (frame.begin_menu("File"))
            {
                // The menu item shows "Ctrl+S"; the chord chooses it while the menu is open.
                frame.set_next_item_shortcut(SAVE);
                editor.save_requested |= frame.menu_item("Save");
                frame.end_menu();
            }
            frame.end_menu_bar();
        }

        // Control+S while this window (or a window in it) has the keyboard focus.
        if (frame.shortcut(SAVE) || std::exchange(editor.save_requested, false))
        {
            editor.save();
            frame.announce("Saved");
        }
        frame.set_next_item_accessible_description("Seconds between automatic saves");
        frame.slider("Autosave", editor.autosave_seconds, 0, 600);
    }
    frame.end_window();
    static_cast<void>(ui.end_frame());
    accessibility.update(ui);
}
```

- **Navigation**: the keyboard keymap of `UiInputSettings::keyboard_navigation` turns keys into
  `UiNavigationCommand`s: the arrows move the focus to the nearest item in their direction (scored as Dear ImGui
  scores it), Tab and Shift+Tab step through the focus order, Space and Return activate, Return types a slider's
  value, Escape closes the top popup or drops the focus, Alt alone goes to the menu bar and back (Left and Right
  then go between its menus), Control+Tab cycles the windows, the Application key or Shift+F10 opens the focused
  item's context menu. Widgets that use an arrow keep it (a slider steps, a tree node opens or closes, a menu
  opens). The window scrolls to show the item the focus moves to. `UiFrameInput::navigation` brings the same
  commands from a gamepad (held moves repeat) and a stick's scrolling; `gfx.ui.platform` reads them from the
  input system's `ui_navigation` context (or `ui_gamepad_navigation_context()` to keep the built-in keymap).
- **Shortcuts**: `shortcut(chord, flags)` is true when its chord is pressed (by the layout's symbols, modifiers
  exactly) and routed to the call: by default from the window holding the keyboard focus, else the nearest
  window it is in; `ROUTE_GLOBAL` after those, `ROUTE_ACTIVE` for the active item, `ROUTE_ALWAYS` without routing.
  A text field being edited keeps the chords it edits with. `set_next_item_shortcut()` activates an item;
  `key_chord_text()` writes a chord as menus show it ("Ctrl+Shift+S").
- **Accessibility**: `UiContext::set_accessibility_enabled()` builds an accessibility tree each frame — windows,
  menu bars, tree nodes, tab bars, tables, lists and their items, with roles, names, values, numbers, states,
  bounds and actions — compared with the tree before, so that providers send only the changes.
  `set_next_item_accessible_label()`, `set_next_item_accessible_description()` and
  `set_next_item_accessibility_role()` override what a widget says; `announce()` reads a status message out.
  `UiAccessibilityAdapter` (gfx.ui.platform) gives the tree to screen readers through
  [AccessKit](https://accesskit.dev) (Windows UI Automation for now), enabling it when one connects, and
  their requests (focus, click, set a value, step, expand, open the context menu) back to the next frame.

## UI: a render thread

The thread that owns the platform builds UI frames; a render thread draws them. `end_frame()` publishes the
frame's draw data into one of three slots, in turn, skipping a slot that a `UiDrawDataLease` still holds;
`acquire_draw_data()` leases the slot just published, so the lists stay untouched while another thread reads
them. The renderer copies them into the frame's geometry buffer, after which the lease can go.

```cpp
// What the main thread hands over for a frame: a lease on the UI's draw data (and whatever else the frame draws).
struct FramePacket
{
    gfx::ui::UiDrawDataLease draw_data{};
    bool recreate_swapchain = false;
};

// The main thread, each frame: the UI frame, then its draw data to the render thread.
gfx::ui::UiFrame& frame = ui.begin_frame(input.frame_input(seconds));
editor_frame(frame);
static_cast<void>(ui.end_frame());
mailbox.put(FramePacket{.draw_data = ui.acquire_draw_data(), .recreate_swapchain = recreate});

// The render thread, each packet: the draw data into the image, read through the lease.
const gfx::ui::UiDrawData& data = packet.draw_data.data();
renderer.begin_frame(frame_number);
renderer.prepare(recorder, data);                                 // before the pass: atlas pages
{
    gfx::RenderingScope scope = recorder.begin_rendering({.area = area, .color_attachments = attachments});
    renderer.record(scope, data);                                 // the lists' geometry copied out
}
```

The render thread owns its frame pacing (`FrameTimeline`, `FrameCommandPools`) and the swapchain's
acquire, present and recreation; the main thread reads the window's events and only tells it to recreate
(`requires_swapchain_recreation()` → `WindowSwapchain::request_recreation()`), because a `WindowSwapchain`
is used by one thread at a time.

## Camera

`import gfx.camera;` (target `gfx::camera`) gives cameras that know nothing of a renderer, a window or an input
system: the application says which clip conventions its renderer expects — depth range `[0, 1]` (Vulkan,
Direct3D, Metal, WebGPU) or `[-1, 1]` (OpenGL), reversed depth, +Y up or down in normalized device
coordinates — and gets view, projection and inverse matrices, a world-space frustum, culling and screen rays.
The camera library depends on gfx::core and the physics foundations only (the build checks it).

```cpp
import std;
import gfx.camera;

using namespace gfx::camera;

// A camera 8 units back and 3 up, looking at the origin: reversed depth and an infinite far plane, the usual
// choice with a floating-point depth buffer.
const std::unique_ptr<Camera> camera = create_camera(
    {.projection = PerspectiveProjection{.vertical_field_of_view = 1.0F, .aspect_ratio = 16.0F / 9.0F,
                                         .near_plane = 0.1F, .far_plane = INFINITE_FAR_PLANE},
     .clip = {.reversed_depth = true},
     .position = {.y = 3, .z = 8}});
camera->look_at({});

// Matrices for the renderer, culling, and picking under the mouse.
const gfx::Matrix4 view_projection = camera->view_projection();
const bool visible = camera->classify(Aabb::from_center({}, {.x = 1, .y = 1, .z = 1})) != FrustumIntersection::OUTSIDE;
const Ray picking = camera->screen_to_world_ray({.x = 960, .y = 540}, {.width = 1920, .height = 1080});

// A first-person controller, fed with values the application computes from its input.
const std::unique_ptr<CameraController> walker = create_camera_controller(FirstPersonControllerDescription{.speed = 4});
walker->update(*camera, {.movement = {.z = 1}, .look = {.x = 0.01F}, .seconds = 1.0F / 60});

// The same functions at compile time, without a camera object: a map camera looking straight down.
constexpr CameraDescription map{.projection = orthographic({.width = 100, .height = 100, .near_plane = 0, .far_plane = 200}),
                                .position = {.y = 100},
                                .orientation = gfx::Quaternion::from_axis_angle({.x = 1}, -std::numbers::pi_v<float> / 2)};
static_assert(camera_frustum(map).contains({.x = 10, .z = -20}));
```

Conventions: right-handed world and view space, the camera looking along -Z with +Y up; column-major matrices
of column vectors; unit quaternions for orientations (no Euler angles are kept, so the camera itself has no
gimbal lock; `set_orientation_from()` and `yaw_pitch_roll()` convert). Screen points are pixels of a viewport,
origin at its top-left corner. Controllers (free fly, first person, orbit) clamp pitch and distance; an
application's own camera (off-axis stereo, jittered projections) derives from `Camera` and is constructed directly.

## Examples

`examples/windowed` is a complete program: a window (title, icon, size limits, resize handling), an
input schema with three contexts declared at compile time, and Vulkan frames presented through
`WindowSwapchain`. A square moves with W A S D, the arrow keys or a gamepad's left stick; Space or
the South button makes it hop; Escape or Start opens a pause menu where Return or South quits and
Escape, Start or East resumes; Backspace restores the initial size; in Debug builds, F3 shows or hides
the frame metrics in a window of the UI library that the mouse can move and close (when the UI library is
built with its atlas tool: the example builds its font atlas with `gfx_add_ui_atlas()`).

```bash
build/debug/examples/windowed/gfx_windowed_example.exe --backend glfw
```

`--backend sdl3|glfw` picks the platform backend, `--device <index>` the GPU, `--validation` enables
the Vulkan validation layers (the exit code is then 1 if they report an error), `--metrics` shows the
frame metrics from the start, and `--frames <count>` closes the window after that many frames. CTest
runs it with `--frames 120 --metrics --validation` on every platform backend
(`ctest --preset debug -L example`).

`examples/ui_editor` is an editor-like UI over a scene of colored squares, drawn by a render thread
([above](#ui-a-render-thread)) while the main thread builds the next frame. A main menu bar (File, Edit,
View, Theme, Help, with shortcuts shown) sits over a dock space whose default layout docks a text panel on
the left, an inspector on the right, the objects' table and the statistics at the bottom, and the scene in
the middle. Tabs can be dragged out, redocked and saved as a layout. In it:

- the **text panel**: a multiline text field whose text is drawn below at any size, rotated, outlined and
  shadowed, all from one SDF atlas (Hebrew and Arabic come from two fallback atlases);
- the **inspector**: the selected object's name, visibility, animation, size, position, velocity and color,
  the scene's speed and background (a color wheel), with "(?)" tooltips;
- the **objects' table**: sortable, resizable, reorderable and hideable columns, rows selected by a click,
  a context menu per row;
- the **statistics**: frame times (a plot and a progress bar), the UI's work (windows, items, draw lists,
  vertices, the frame arena, the text layout cache), allocations, the screen reader;
- **shortcuts** from any window (Control+N adds an object, Control+D duplicates it, Delete removes it,
  Control+S and Control+O save and load the layout, Control+Q quits), keyboard and gamepad
  **navigation**, the four built-in **themes**, and the **accessibility tree** for screen readers
  (through UI Automation on Windows).

```bash
build/debug/examples/ui_editor/gfx_ui_editor_example.exe --backend sdl3 --layout editor_layout.json
```

`--backend`, `--device`, `--frames` and `--validation` work as for the windowed example; `--layout <file>`
loads the layout from the file, and File > Save layout (Control+S) writes it there. `--sweep` moves a synthetic mouse along a path closed
every 120 frames, over items, tooltips and table rows, with the time advancing 1/60 s per frame, so every
run draws the same frames. `--check-allocations` counts `operator new` in the UI's frames (main thread)
and in the render thread's whole frames (acquisition, recording, submission, presentation) after
`--warm-up <frames>`, and prints the frames that allocated, with each thread's count. CTest runs it on every
platform backend with `--frames 600 --warm-up 360 --sweep --check-allocations --validation`: five cycles of
the path, the last two without any allocation on either thread and without any validation error.

## Requirements (Windows)

| Tool | Version verified | Notes |
|---|---|---|
| [MSYS2](https://www.msys2.org/) UCRT64 GCC | 16.1 | GCC 15 or newer is required for `import std` |
| CMake | 4.4.0-rc2 | 4.3 or newer; see the `import std` warning below |
| Ninja | 1.13.2 | The only generator that can build `import std` |
| Python | 3.14 | Only for `build.py` |

Install the MSYS2 packages from an MSYS2 shell:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,ninja,vulkan-headers,vulkan-loader,vulkan-validation-layers,glslang,spirv-tools,spirv-cross,vulkan-memory-allocator}
```

Dependencies that are not installed (or not usable, such as DLLs refused by Windows application
control) are downloaded and built with CMake's `FetchContent`. GoogleTest is always built from
source on Windows and linked statically. volk and RenderDoc's in-application API header are always
fetched. [RenderDoc](https://renderdoc.org/) itself is optional: when installed, the test
`VulkanCapture.RenderDoc` captures a test's work through `Device::begin_capture()`.
The UI library's font pipeline is fetched at pinned versions, so that atlases are byte-for-byte
reproducible: FreeType 2.14.3 (with Brotli 1.2.0 for WOFF2), msdfgen 1.13, msdf-atlas-gen 1.4,
HarfBuzz 14.2.1, zstd 1.5.7 and stb_truetype 1.26; SheenBidi 3.0.0 and libunibreak 7.0 are used
from the system when installed. The UI's screen reader adapter links AccessKit's C bindings 0.23.1 on Windows,
fetched as the AccessKit project publishes them (static libraries for each platform: a 67 MB archive). With the tests on, configuring downloads the OFL test fonts (Noto
Sans, Arabic, Hebrew; Source Sans 3 as OpenType CFF, CFF2, WOFF and WOFF2) into `<build>/ui-test-fonts`,
checked against their hashes. The core library's math (`gfx.core.math`) is computed at compile time by its own code
and at run time on SIMD through cglm 0.9.6 and SIMDe 0.8.2 (header-only, always fetched at those versions).
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) lists every dependency,
its version, its license and what of it ends up in a program.

| CMake option | Default | Backend |
|---|---|---|
| `GFX_BACKEND_VULKAN` | `ON` | Vulkan (implemented) |
| `GFX_BACKEND_NULL` | `ON` | Null: an instance without physical devices |
| `GFX_BACKEND_DIRECT3D12` | `ON` on Windows | Direct3D 12 (stub) |
| `GFX_BACKEND_METAL` | `ON` on Apple platforms | Metal (stub) |
| `GFX_PLATFORM_BACKEND_SDL3` | `ON` | SDL3 windowing and input (SDL 3.4 found, or 3.4.10 fetched and linked statically) |
| `GFX_PLATFORM_BACKEND_GLFW` | `ON` | GLFW windowing and input (GLFW 3.4 found, or fetched and linked statically) |
| `GFX_BUILD_PLATFORM` | `ON` | The platform library and its bridge to the graphics library |
| `GFX_BUILD_UI` | `ON` | The UI library |
| `GFX_BUILD_UI_ATLAS_TOOLS` | `ON` | The atlas tools (`gfx::ui_atlas_tools`, `gfx_ui_atlas_builder`, `gfx_add_ui_atlas()`) and their font pipeline |
| `GFX_BUILD_PHYSICS` | `ON` | The physics and camera libraries (in progress: foundations `gfx::physics_common`, `gfx::camera`, `gfx::physics_components` with collision shapes, the narrow phase, spatial structures, the broad phase, the world interface, integrators, XPBD soft bodies coupled both ways with any backend's rigid bodies, the sequential-impulse solver, and the framework backends are built on, and `gfx::physics` with the factory and its backends: Box3D, the reference backend made of the abstraction's replaceable components, and the null one), which depend on the core library only (and Box3D, for its backend) — the build checks it |
| `GFX_PHYSICS_BACKEND_BOX3D` | `ON` | The Box3D physics backend (Box3D fetched, pinned by SHA-256 and linked statically) |
| `GFX_PHYSICS_CUSTOM_BACKEND_TARGET` | (empty) | A target whose module `gfx.physics.backends.custom` becomes the factory's `PhysicsBackendKind::CUSTOM` (it links `gfx::physics_components` and exports `gfx::physics::custom_backend::unavailable_reason()` and `create_physics_world(description)`) |
| `GFX_SIMD_INSTRUCTION_SET` | `SSE2` | The instruction set of the SIMD paths: `SSE2`, `AVX2` (with FMA) or `NATIVE` (`-march=native`) |
| `GFX_BUILD_EXAMPLES` | `ON` | The examples (both need the platform library and Vulkan; the UI editor needs the UI library and its atlas tools) |
| `GFX_BUILD_TESTS` | `ON` | The tests, registered with CTest |

## Project layout

```
gfx/
├── CMakeLists.txt        entry point; only orchestrates the modules in cmake/
├── CMakePresets.json     configure/build/test/workflow presets (CMake reads them only from the root)
├── build.py              configure, build and test driver
├── cmake/
│   ├── GfxImportStandardLibrary.cmake   experimental `import std` gate (included before project())
│   ├── GfxToolchainRequirements.cmake   Ninja / GCC / `import std` checks and language level
│   ├── GfxProjectOptions.cmake          warnings, platform libraries and toolchain workarounds
│   ├── GfxRuntimeDeployment.cmake       copies required DLLs next to Windows executables
│   ├── GfxDependencies.cmake            system package or FetchContent for each dependency
│   ├── GfxShaders.cmake                 gfx_add_shaders(): GLSL/HLSL to SPIR-V at build time
│   ├── GfxTesting.cmake                 GoogleTest, compilation-failure and naming test helpers
│   ├── GfxDecoupling.cmake              gfx_require_decoupled(): a target's link closure avoids forbidden targets
│   └── scripts/                         CMake scripts run by builds and tests (SPIR-V embedding, ...)
├── THIRD_PARTY_LICENSES.md  every dependency: version, license, what a program contains of it
├── docs/                 architecture.md (the code as built, with diagrams), platform-design.md
│                         (windowing and input: decisions and plan), ui-design.md (the UI: decisions,
│                         phases, what each phase built), ui-atlas-format.md (the .uiatlas format),
│                         physics-design.md (physics and camera: decisions, phases, what each built)
├── examples/windowed/    a window, compile-time input contexts and Vulkan presentation
├── examples/ui_editor/   an editor-like UI (docking, menus, a table, a text panel) drawn by a render thread
├── source/
│   ├── core/             gfx.core — what the libraries share (`import gfx.core;`, re-exported by both)
│   │   ├── enums.cppm               enumeration machinery (traits, verification, bitmask operators,
│   │   │                            to_string/from_string, std::format) and the shared enumerations
│   │   ├── errors.cppm/.cpp         exception hierarchy (gfx::Error, BackendCapabilityError, ...) and
│   │   │                            BackendIdentity (which library's backend raised an error)
│   │   ├── handles.cppm             Handle<Tag> (issued by backends) and Identifier<Tag> (hashed names)
│   │   ├── types.cppm               Version, Extent, Offset, Rectangle, Vector2
│   │   ├── math.cppm, math_kernels.cpp   gfx.core.math (`import gfx.core.math;`): vectors, quaternions,
│   │   │                            matrices, transforms; constexpr, SIMD at run time (cglm, SIMDe)
│   │   ├── native_window.cppm       NativeWindowHandle: OS window handles, from the platform library
│   │   │                            to the graphics library
│   │   ├── slot_table.cppm, handle_table.cppm   thread-safe handle-to-object tables for backends
│   │   └── include/gfx/             headers only where a module cannot serve (toolchain workarounds)
│   ├── graphics/         the graphics library (`import gfx;`)
│   │   ├── gfx.cppm      `import gfx;` umbrella (gfx.core included)
│   │   ├── enums.cppm    every graphics enumeration and constant (the only graphics file that defines them)
│   │   ├── errors.cppm   graphics errors (DeviceLostError, ShaderCompilationError, ShaderLayoutMismatchError)
│   │   ├── handles.cppm  graphics handle tags (BufferHandle, ...) and their concepts
│   │   ├── interface/    module gfx.interface: one partition per object or concept
│   │   │   ├── interface.cppm             primary interface unit (re-exports every partition)
│   │   │   ├── bindless.cppm              bindless indices, table capacities, the universal pipeline layout
│   │   │   ├── shader_layout.cppm         ShaderLayout, the interface reflected from a shader's SPIR-V
│   │   │   ├── shader_validation.cppm     pipeline checks (stages, layouts, attributes, attachments,
│   │   │   │                              specialization constants, ray tracing groups)
│   │   │   ├── recorder.cppm              Recorder<QueueType>, rendering scopes, parallel rendering parts
│   │   │   ├── acceleration_structure.cppm, ray_tracing_pipeline.cppm   ray tracing
│   │   │   ├── frame_timeline.cppm, frame_command_pools.cppm, transient_buffer_allocator.cppm,
│   │   │   │   uploader.cppm, gpu_profiler.cppm   per-frame resources built on the interfaces
│   │   │   └── device.cppm, buffer.cppm, ...  the abstract objects backends implement
│   │   ├── backend_factory.cppm/.cpp  create_instance(BackendKind): the only public entry to backends
│   │   ├── shaders/      module gfx.shader_tools: SPIR-V reflection (SPIRV-Cross), Debug compiler (glslang)
│   │   └── backends/     private modules, reached only through the factory
│   │       ├── vulkan/   module gfx.backends.vulkan (partitions + one implementation unit per area)
│   │       └── null/, d3d12/, metal/
│   ├── platform/         the platform library: windowing and input (`import gfx.platform;`)
│   │   ├── enums.cppm    every platform enumeration and constant (keys as USB HID usages, key codes,
│   │   │                 buttons, axes, modifiers, cursors, window modes and flags, event types)
│   │   ├── handles.cppm  window, monitor, cursor and device handles; action and binding-context
│   │   │                 identifiers ("jump"_action); player slots
│   │   ├── interface/    module gfx.platform.interface: events and their queue, images, monitors,
│   │   │                 Cursor, Window, InputDevices, Platform (event pump, watchers, threads)
│   │   ├── input/        module gfx.platform.input: action mapping — compile-time schemas, bindings,
│   │   │                 contexts, players, InputSystem and InputFrame, EventChannel, saved bindings
│   │   ├── backends/     private modules, reached only through the factory
│   │   │   ├── sdl3/     module gfx.platform.backends.sdl3 (windows, events, cursors, monitors,
│   │   │   │             clipboard, gamepads, joysticks, touch)
│   │   │   └── glfw/     module gfx.platform.backends.glfw (the same contract; throws for what GLFW lacks)
│   │   └── backend_factory.cppm/.cpp  create_platform(PlatformBackendKind)
│   ├── platform_graphics/  gfx.platform.graphics: platform windows to graphics swapchains
│   │                       (swapchain_description_for(), WindowSwapchain)
│   ├── ui/               the UI library (`import gfx.ui;`): enums.cppm, handles.cppm, errors.cppm
│   │   ├── context/      gfx.ui.context: UiContext, UiFrame (windows, layout, widgets, tables, docking,
│   │   │                 navigation, shortcuts), UiListClipper, the frame report, the accessibility tree;
│   │   │                 internal partition :state, one implementation unit per area
│   │   ├── style/        gfx.ui.style: Style, Theme, builtin_theme()
│   │   ├── input/        gfx.ui.input: UiFrameInput, UiPlatformServices, UiNullPlatformServices
│   │   ├── platform/     gfx.ui.platform (target gfx::ui_platform): UiPlatformInput, UiWindowServices, the
│   │   │                 navigation context of the input system, UiAccessibilityAdapter (AccessKit)
│   │   ├── text/         gfx.ui.text (in gfx.ui): FontAtlas, FontLibrary, TextLayoutEngine (bidi,
│   │   │                 fallback, shaping, line breaking, cache), gfx.ui.unicode (UTF-8)
│   │   ├── draw/         gfx.ui.draw: DrawList, Color, Transform2D, TextStyle, UiDrawData;
│   │   │                 gfx.ui.debug_overlay: DebugOverlay (frame metrics as SDF text)
│   │   ├── render/       gfx.ui.renderer: UiRenderer; shaders/ui.vert, ui.frag (embedded by gfx_add_shaders())
│   │   ├── font/         atlas/atlas_format.cppm: the .uiatlas format and AtlasView (in gfx.ui);
│   │   │                 the atlas tools (gfx.ui.atlas_tools): source/ (fonts, characters), decoders/
│   │   │                 (FreeType, stb_truetype), sdf/ (msdfgen, built-in), atlas/ (packers, writer,
│   │   │                 builder), shaping/ (hb-subset, pair tables)
│   │   └── tools/atlas_builder/  gfx_ui_atlas_builder, the atlas tool gfx_add_ui_atlas() runs
│   └── physics/          the physics library (in progress; core only): enums.cppm (physics and camera),
│                         handles.cppm, errors.cppm; primitives/ (gfx.physics.primitives: planes, boxes,
│                         spheres, oriented boxes, rays, segments, hulls, frustums, intersection tests,
│                         SIMD frustum culling); camera/ (gfx.camera); shapes/ (gfx.physics.shapes: convex shapes,
│                         quickhull, mass properties, meshes, height fields, compounds); narrow_phase/ (GJK, EPA,
│                         separating axes, contact manifolds, the manifold cache); spatial/ (gfx.physics.spatial:
│                         BVH, loose octree and quadtree, BSP, uniform grid, sweep and prune, the broad phase);
│                         tasks/ (gfx.physics.tasks: task schedulers); interface/ (gfx.physics.interface: worlds,
│                         bodies, colliders, joints, triggers, soft bodies, queries, events, pipeline); dynamics/
│                         (gfx.physics.dynamics: integrators, cloth/rope/volume presets, the XPBD soft-body
│                         solver, the sequential-impulse solver and joint rows); world/ (gfx.physics.world: SteppedWorld, deferred edits, snapshots, stepping,
│                         queries, the soft bodies' step);
│                         backends/box3d/ (the Box3D backend), backends/reference/ (the reference backend:
│                         the abstraction's components, user constraints), backends/null/; physics.cppm (gfx.physics),
│                         physics_factory (create_physics_world())
└── tests/
    ├── toolchain/        toolchain conformance; defects/ proves each workaround is still needed
    ├── core/             gfx.core (imports gfx.core only), its math and the slot table
    ├── physics/          the physics library (primitives at compile time and against references, SIMD batches
    │                     against the scalar code, shapes and queries against exact answers, spatial structures
    │                     against brute force, integrators against analytic orbits, soft bodies with known
    │                     outcomes, worlds on a recording backend, Box3D) and the decoupling checks
    ├── graphics/<area>/  GoogleTest unit tests of the graphics library, one subfolder per area;
    │                     graphics/vulkan: the Vulkan backend on the machine's real GPUs
    ├── platform/         the platform library, through a test-only fake platform; platform/backends:
    │                     every backend with real windows (one parameterized suite), then sdl3/
    │                     (SDL-injected events, virtual gamepads) and glfw/ (Win32 messages)
    ├── platform_graphics/  presentation to visible windows: every platform backend x every GPU,
    │                     with the validation layers (cleared frames read back, the UI renderer's
    │                     shapes in the window, resize, minimize)
    ├── ui/               the UI library, the atlas tools (every format, decoder, generator, packer),
    │                     embedded atlases, text layout (and its allocation count), draw lists, the
    │                     renderer on every GPU (shapes and text read back, text compared with the field
    │                     evaluated on the CPU), the immediate-mode UI under synthetic input (and drawn on
    │                     every GPU), the platform adapter (and a UI in visible windows), and the font
    │                     pipeline's libraries (fetched OFL fonts)
    └── compilation/      code that must not compile, and the platform-macro naming check
```

## Building and testing

```bash
python build.py --test
```

or, with CMake presets:

```bash
cmake --workflow --preset debug
```

Tests run in parallel through CTest (`ctest --preset debug`). Each GoogleTest case is registered as
its own CTest test. Compilation tests (label `compilation`) build snippets that must be rejected and
run one at a time; run only them with `ctest --preset debug -L compilation`.

The Vulkan tests (`gfx_vulkan_tests`) run on a real GPU with the validation layers; any validation
error fails the test, and tests whose feature the GPU lacks are skipped. Environment variables:

| Variable | Effect |
|---|---|
| `GFX_TEST_PHYSICAL_DEVICE=<index>` | Run on that physical device instead of the best one |
| `GFX_TEST_VALIDATION=0` | Run without the validation layers |

## Debugging a test in VS Code

`.vscode/` provides build/test tasks and two debug configurations (gdb, Debug build). A debugged test
stops at its first failed assertion; select the test's frame in the call stack.

- **Testing panel** (CMake Tools extension, "debug" preset selected): every test case is listed;
  "Debug Test" on any of them starts **gfx: Debug Test**.
- **Run and Debug view**: **gfx: Debug Test** asks which CTest test to debug; **gfx: Debug Test
  Executable** (C/C++ extension only) asks for a test executable and a GoogleTest filter such as
  `HandlesTest.DefaultHandleIsNull`.
- **Tasks**: *gfx: Run One Test (Debug)* runs the tests matching a name pattern without the debugger
  (including the `CompilationFailure.*` tests, which cannot be debugged).

## Threading

| Object | Thread safety |
|---|---|
| Handles, bindless indices, descriptions | Plain values: any thread |
| `Device` creation functions, `PipelineCache` | Thread-safe |
| `Queue::submit`, `Queue::present` | Thread-safe (serialized per queue); different queues run in parallel |
| `CommandPool`, `CommandBuffer`, recorders, `FrameCommandPools` | One thread at a time: one pool per thread, per queue type and per frame in flight |
| `DescriptorSet::update` | One thread at a time per set (bindless needs none) |
| `TransientBufferAllocator::allocate`, `GpuProfiler::scope` | Thread-safe, lock-free; `begin_frame` must not run concurrently |
| `Device::poll_shader_changes` | Once per frame, while no thread records |
| `UiContext`, `UiFrame`, `UiRenderer` | One thread at a time (the context: the thread that builds frames) |
| `UiContext::acquire_draw_data`, `UiDrawDataLease` | Any thread: the leased lists are not reused until the lease is released |

## Using gfx together with `<windows.h>`

With GCC 16 on MinGW, a translation unit cannot include `<windows.h>` and import modules unless the
header comes first and through the workaround header:

```cpp
#include <gfx/windows_headers.hpp>   // instead of <windows.h>, before any import
import std;
import gfx;
```

## Warnings

- **`import std;` is experimental in CMake.** It is enabled through `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD`,
  whose UUID changes with every CMake release. After upgrading CMake, configuration stops with an
  explanation; update the UUID table in `cmake/GfxImportStandardLibrary.cmake` or pass
  `-DGFX_IMPORT_STD_GATE=<uuid>`.
- **GCC 16 on MinGW emits some standard library symbols as duplicate definitions with `import std`**
  (static locals of templates, e.g. inside `std::format`). Every executable that uses gfx therefore
  links with `-Wl,--allow-multiple-definition`, propagated by the `gfx::toolchain_workarounds`
  target. The test `CompilationFailure.ImportStdDuplicatesStaticLocals` starts failing once a GCC
  release fixes this; the workaround can then be removed.
- **Windows Smart App Control**, when enforced, judges every freshly compiled executable and unsigned
  DLL by cloud reputation, and may block any of them unpredictably (test executables included). A
  blocked test fails with `execute_process Abnormal exit` or exit code `0xC0E90002`; the
  `Microsoft-Windows-CodeIntegrity/Operational` event log names the blocked file.

## License

[MIT](LICENSE). The dependencies, and the fonts the tests and examples build atlases from, have their own
licenses: see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
