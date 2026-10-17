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
| ui (in progress) | `import gfx.ui;` | immediate-mode UI over retained state, every text through SDF/MSDF atlases built at build time ([design](docs/ui-design.md)) |

> **Status:** the graphics interface is complete: objects, typed recording, bindless resources,
> parallel rendering passes, ray tracing, mesh shading, GPU-driven drawing, pipeline caches, frame
> resources, profiling and crash diagnostics. The **Vulkan backend** implements all of it, presentation
> to windows included. The SPIR-V toolchain (build-time compilation, reflection, Debug-only run-time
> compilation and hot reload) is in place. The Direct3D 12 and Metal backends are stubs that throw
> "not implemented yet". The **platform library** is complete on its **SDL3 and GLFW** backends
> (Win32, XCB, Cocoa and Web are reported as not built), and the [windowed example](#examples) runs on
> both. The design and the decisions behind the platform library are in
> [docs/platform-design.md](docs/platform-design.md); [docs/architecture.md](docs/architecture.md)
> describes the code as built, with diagrams. The **UI library** is being built in phases
> ([docs/ui-design.md](docs/ui-design.md)): its foundations, its [font atlases](#ui-font-atlases)
> (any font format turned into SDF/MSDF atlases at build time, checked at compile time) and its
> [text layout](#ui-text-layout) (bidi, fallback fonts, shaping, line breaking) are in place; rendering
> and widgets arrive in the next phases.

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
  the symbol of the keyboard layout), mouse, gamepads, joysticks and touch; and an **action mapping**
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
they read back as given. `statistics()` reports draws, vertices, resident pages, uploaded bytes and the
commands skipped when a frame's geometry exceeds `UiRendererDescription::geometry_bytes_per_frame`.

## Examples

`examples/windowed` is a complete program: a window (title, icon, size limits, resize handling), an
input schema with three contexts declared at compile time, and Vulkan frames presented through
`WindowSwapchain`. A square moves with W A S D, the arrow keys or a gamepad's left stick; Space or
the South button makes it hop; Escape or Start opens a pause menu where Return or South quits and
Escape, Start or East resumes; Backspace restores the initial size; in Debug builds, F3 shows or hides
the frame metrics (drawn by the UI library when it is built with its atlas tool: the example builds its
overlay's font atlas with `gfx_add_ui_atlas()`).

```bash
build/debug/examples/windowed/gfx_windowed_example.exe --backend glfw
```

`--backend sdl3|glfw` picks the platform backend, `--device <index>` the GPU, `--validation` enables
the Vulkan validation layers (the exit code is then 1 if they report an error), `--metrics` shows the
frame metrics from the start, and `--frames <count>` closes the window after that many frames. CTest
runs it with `--frames 120 --metrics --validation` on every platform backend
(`ctest --preset debug -L example`).

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
from the system when installed. With the tests on, configuring downloads the OFL test fonts (Noto
Sans, Arabic, Hebrew; Source Sans 3 as OpenType CFF, CFF2, WOFF and WOFF2) into `<build>/ui-test-fonts`,
checked against their hashes.

| CMake option | Default | Backend |
|---|---|---|
| `GFX_BACKEND_VULKAN` | `ON` | Vulkan (implemented) |
| `GFX_BACKEND_NULL` | `ON` | Null: an instance without physical devices |
| `GFX_BACKEND_DIRECT3D12` | `ON` on Windows | Direct3D 12 (stub) |
| `GFX_BACKEND_METAL` | `ON` on Apple platforms | Metal (stub) |
| `GFX_PLATFORM_BACKEND_SDL3` | `ON` | SDL3 windowing and input (SDL 3.4 found, or 3.4.10 fetched and linked statically) |
| `GFX_PLATFORM_BACKEND_GLFW` | `ON` | GLFW windowing and input (GLFW 3.4 found, or fetched and linked statically) |
| `GFX_BUILD_PLATFORM` | `ON` | The platform library and its bridge to the graphics library |
| `GFX_BUILD_UI` | `ON` | The UI library (in progress) |
| `GFX_BUILD_UI_ATLAS_TOOLS` | `ON` | The atlas tools (`gfx::ui_atlas_tools`, `gfx_ui_atlas_builder`, `gfx_add_ui_atlas()`) and their font pipeline |
| `GFX_BUILD_EXAMPLES` | `ON` | The examples (the windowed one needs the platform library and Vulkan) |
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
│   └── scripts/                         CMake scripts run by builds and tests (SPIR-V embedding, ...)
├── docs/                 platform-design.md (windowing and input: decisions and plan),
│                         architecture.md (the code as built, with diagrams)
├── examples/windowed/    a window, compile-time input contexts and Vulkan presentation
├── source/
│   ├── core/             gfx.core — what the libraries share (`import gfx.core;`, re-exported by both)
│   │   ├── enums.cppm               enumeration machinery (traits, verification, bitmask operators,
│   │   │                            to_string/from_string, std::format) and the shared enumerations
│   │   ├── errors.cppm/.cpp         exception hierarchy (gfx::Error, BackendCapabilityError, ...) and
│   │   │                            BackendIdentity (which library's backend raised an error)
│   │   ├── handles.cppm             Handle<Tag> (issued by backends) and Identifier<Tag> (hashed names)
│   │   ├── types.cppm               Version, Extent, Offset, Rectangle, Vector2
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
│   └── ui/               the UI library (`import gfx.ui;`, in progress): enums.cppm, handles.cppm,
│       │                 errors.cppm
│       ├── text/         gfx.ui.text (in gfx.ui): FontAtlas, FontLibrary, TextLayoutEngine (bidi,
│       │                 fallback, shaping, line breaking, cache), gfx.ui.unicode (UTF-8)
│       ├── draw/         gfx.ui.draw: DrawList, Color, Transform2D, TextStyle, UiDrawData;
│       │                 gfx.ui.debug_overlay: DebugOverlay (frame metrics as SDF text)
│       ├── render/       gfx.ui.renderer: UiRenderer; shaders/ui.vert, ui.frag (embedded by gfx_add_shaders())
│       ├── font/         atlas/atlas_format.cppm: the .uiatlas format and AtlasView (in gfx.ui);
│       │                 the atlas tools (gfx.ui.atlas_tools): source/ (fonts, characters), decoders/
│       │                 (FreeType, stb_truetype), sdf/ (msdfgen, built-in), atlas/ (packers, writer,
│       │                 builder), shaping/ (hb-subset, pair tables)
│       └── tools/atlas_builder/  gfx_ui_atlas_builder, the atlas tool gfx_add_ui_atlas() runs
└── tests/
    ├── toolchain/        toolchain conformance; defects/ proves each workaround is still needed
    ├── core/             gfx.core (imports gfx.core only) and the slot table
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
    │                     evaluated on the CPU), and the font pipeline's libraries (fetched OFL fonts)
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

[MIT](LICENSE)
