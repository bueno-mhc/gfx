# gfx — architecture overview (the code as built)

This document describes the code as it is built, on 2026-10-16: the libraries, their modules, the
objects and how they relate, the paths a frame and an event take, the threads, and how everything is
built and tested. The decisions behind the platform library, with the alternatives considered, are in
[platform-design.md](platform-design.md); how to use the libraries is in the [README](../README.md).

Diagrams are [Mermaid](https://mermaid.js.org/) (GitHub renders them). Arrows read "uses" or "depends
on" unless a diagram says otherwise.

## 1. Libraries, modules and dependencies

Four libraries, each a CMake target whose public API is a set of C++ modules. Backends are modules of a
**private** file set: only the factory of their library imports them, so no consumer can name a
backend type or include a native header through gfx.

```mermaid
flowchart TB
    subgraph app["applications"]
        example["gfx_windowed_example<br/>examples/windowed"]
        tests["tests/*"]
    end
    subgraph bridge_lib["gfx::platform_graphics"]
        bridge["gfx.platform.graphics<br/>swapchain_description_for, WindowSwapchain"]
    end
    subgraph graphics_lib["gfx::graphics (target gfx)"]
        gfx_module["gfx<br/>enums, errors, handles, interface,<br/>backend_factory, shader_tools"]
        vulkan["gfx.backends.vulkan<br/>(private)"]
        graphics_stubs["gfx.backends.null / direct3d12 / metal<br/>(private stubs)"]
    end
    subgraph platform_lib["gfx::platform"]
        platform_module["gfx.platform<br/>enums, handles, interface, input,<br/>backend_factory"]
        sdl3["gfx.platform.backends.sdl3<br/>(private)"]
        glfw["gfx.platform.backends.glfw<br/>(private)"]
    end
    subgraph core_lib["gfx::core"]
        core["gfx.core<br/>enums machinery, errors, handles,<br/>identifiers, types, native_window"]
        tables["gfx.core.slot_table, gfx.core.handle_table<br/>(backend utilities)"]
    end
    example --> bridge
    tests --> bridge
    bridge --> gfx_module
    bridge --> platform_module
    gfx_module --> core
    platform_module --> core
    gfx_module -.->|factory imports| vulkan
    gfx_module -.->|factory imports| graphics_stubs
    platform_module -.->|factory imports| sdl3
    platform_module -.->|factory imports| glfw
    vulkan --> tables
    sdl3 --> tables
    glfw --> tables
    vulkan --> third_graphics["volk, Vulkan Memory Allocator,<br/>SPIRV-Cross, glslang (Debug), RenderDoc API"]
    sdl3 --> third_sdl["SDL 3.4 (static)"]
    glfw --> third_glfw["GLFW 3.4 (static)"]
    platform_module --> third_platform["stb_image (icons, cursors),<br/>nlohmann/json (saved bindings)"]
```

| Library | Source | Modules (public) | Namespace |
|---|---|---|---|
| core | `source/core/` | `gfx.core` = `gfx.core.enums`, `.errors`, `.handles`, `.types`, `.native_window`; plus `gfx.core.slot_table`, `gfx.core.handle_table` for backends | `gfx` |
| graphics | `source/graphics/` | `gfx` = `gfx.enums`, `gfx.errors`, `gfx.handles`, `gfx.interface` (one partition per object or concept), `gfx.backend_factory`, `gfx.shader_tools` | `gfx` |
| platform | `source/platform/` | `gfx.platform` = `gfx.platform.enums`, `.handles`, `.interface` (partitions), `.input` (partitions), `.backend_factory` | `gfx::platform` |
| bridge | `source/platform_graphics/` | `gfx.platform.graphics` (re-exports `gfx` and `gfx.platform`) | `gfx::platform` |

Rules every library follows:

- **One enumerations file per library** (`enums.cppm`): every enumeration and named constant, each with
  `EnumerationTraits` (names, bitmask or not) verified at compile time against the compiler's own
  spelling of each enumerator. `to_string`, `from_string`, `std::formatter` and the bitmask operators
  come from `gfx.core.enums`.
- **Typed handles** (`Handle<Tag>`, 32 bits: 24-bit slot, 8-bit generation) name objects in commands
  and events; only backends can create them (`HandleKey`, constructible by `HandleIssuer` only).
- **RAII**: objects are owned through `std::unique_ptr`; destruction releases (or defers the release
  until the GPU or the event pump no longer needs it).
- **Errors** name the backend, the operation, the problem and a suggestion; backends never degrade
  silently.
- **Native headers stay out of module purviews**: C headers in global module fragments, and headers
  that need `<windows.h>` or that conflict with `import std` in plain translation units behind small
  std-free headers (`vulkan_surface.hpp`, `glfw_native.hpp`, `json_reader.hpp`, `renderdoc_capture.hpp`).

## 2. Core: what the libraries share

```mermaid
classDiagram
    class HandleKey {
        <<constructible by HandleIssuer only>>
    }
    class HandleIssuer {
        #handle_key() HandleKey
    }
    class Handle~Tag~ {
        -uint32 m_value
        +is_valid() bool
        +create(HandleKey, index, generation)$ Handle
        +index(HandleKey) uint32
        +generation(HandleKey) uint32
    }
    class Identifier~Tag~ {
        +value uint32
        +from_name(name)$ Identifier
    }
    class SlotTable {
        lock-free lookups
        mutex on insert and remove
        chunks of 4096 slots, FIFO reuse
    }
    class HandleTable {
        one per Tag and Record
        +insert(Record) Handle
        +find(Handle) Record*
        +get(Handle, operation) Record&
        +remove(Handle) optional Record
    }
    class NativeWindowHandle {
        -m_handle variant of OS window handles
        +kind() NativeWindowKind
        +get_if() alternative pointer
    }
    HandleIssuer ..> HandleKey : creates
    HandleIssuer <|-- HandleTable
    HandleTable *-- SlotTable
    HandleTable ..> Handle : issues
    Handle ..> HandleKey : needs for create/index
```

- **Handles vs identifiers**: a `Handle` is issued by a backend for an object that exists (a stale
  handle is detected by its generation); an `Identifier` is a compile-time hash of a name
  (`"jump"_action`, `"menu"_context`) chosen by the application.
- **`NativeWindowHandle`** carries operating-system handles only, as integers: the platform library
  produces it (`Window::native_handle()`), the graphics library consumes it (surfaces). Neither library
  depends on the other.
- **Handle tables** map a handle to a backend record (the native object and what commands need to know
  about it) with lock-free lookups, so any thread can resolve handles while recording.

Errors:

```mermaid
classDiagram
    class StandardRuntimeError {
        std::runtime_error
    }
    class Error {
        +backend() optional BackendIdentity
        +operation() string_view
        +location() source_location
    }
    StandardRuntimeError <|-- Error
    Error <|-- BackendCapabilityError
    Error <|-- InvalidUsageError
    Error <|-- InvalidHandleError
    Error <|-- OutOfMemoryError
    Error <|-- BackendApiError
    Error <|-- DeviceLostError
    Error <|-- ShaderCompilationError
    Error <|-- ShaderLayoutMismatchError
    note for DeviceLostError "graphics: active GPU markers,\ncrash report path"
    note for BackendCapabilityError "a backend cannot provide a feature:\ncapability, reason, suggestion"
```

`BackendIdentity` is `{library, backend}`: messages start with `[gfx::VULKAN]` or `[gfx::platform::GLFW]`,
followed by the operation, the problem, a suggestion and the source location.

## 3. Graphics library

### 3.1 Object model

```mermaid
classDiagram
    direction LR
    class Instance {
        +physical_device(index)
        +select_physical_device()
    }
    class PhysicalDevice {
        +properties() limits
        +supports(DeviceFeature)
        +format_support(Format)
        +surface_support(NativeWindowHandle)
        +create_device(DeviceDescription)
    }
    class Device {
        +queue(QueueType, index)
        +create_object(description)
        +deleter()
        +bindless_pipeline_layout()
        +poll_shader_changes()
    }
    class Queue {
        +submit(batches, fence)
        +present(PresentDescription)
        +bind_sparse()
    }
    class Swapchain {
        +acquire_next_image()
        +resize()
        +wait_for_frame_latency()
    }
    class CommandPool
    class CommandBuffer
    class Recorder~QueueType~
    Instance "1" *-- "n" PhysicalDevice
    PhysicalDevice ..> Device : creates
    Device "1" *-- "n" Queue
    Device ..> Swapchain : creates
    Device ..> CommandPool : creates
    CommandPool "1" *-- "n" CommandBuffer
    Recorder ..> CommandBuffer : records into
    Device ..> Resources : creates
    Device ..> Pipelines : creates
    Device ..> Synchronization : creates
    class Resources {
        Buffer, Image, ImageView, Sampler
        BufferView, DeviceMemory, AccelerationStructure
    }
    class Pipelines {
        ShaderModule, PipelineLayout, PipelineCache
        Compute, Graphics and RayTracing pipelines
        DescriptorSetLayout, DescriptorPool, DescriptorSet
    }
    class Synchronization {
        Fence, Semaphore, TimelineSemaphore
        Event, QueryPool
    }
```

Objects are created by their parent and owned by the application (`std::unique_ptr`); commands and
descriptors name them by handle. Every interface operation is **pure virtual with a throwing default
definition**: a backend must override it, and may forward to the default, which throws a descriptive
`BackendCapabilityError`. The Null backend (an instance without devices) and the Direct3D 12 and
Metal stubs rely on that.

### 3.2 Recording

`Recorder<QueueType>` is a zero-cost typed wrapper over a `CommandBuffer`: methods are constrained by
the queue type, so `draw()` does not compile on a compute recorder. `begin_rendering()` returns a
`RenderingScope` (dynamic rendering, ended by its destructor); `begin_parallel_rendering()` returns a
`ParallelRenderingScope` whose parts are recorded on secondary command buffers by other threads
(`RenderingPartRecorder`) and executed in a fixed order. Barriers are synchronization2-style, derived
from `ResourceState` pairs (`image_barrier(image, before, after)`).

Per-frame helpers built on the interfaces (backend-independent): `FrameTimeline` (frame N signals N on a
timeline semaphore; the CPU runs at most `frames_in_flight` frames ahead), `FrameCommandPools` (one pool
per thread and frame in flight), `TransientBufferAllocator` (lock-free bump allocation in persistently
mapped memory), `Uploader` (staged uploads with barriers), `GpuProfiler` (timestamp scopes), and, in Debug
builds only (no-ops elsewhere), `FrameMetrics` (frame rate, CPU frame times over a period, the frame's
`CommandStatistics` summed over its command buffers, GPU timings, device memory) as lines of text. The
UI library's `gfx::ui::DebugOverlay` draws those lines (distance-field text over a translucent panel,
through `gfx::ui::UiRenderer`); the graphics library has no text of its own.

### 3.3 Vulkan backend

```mermaid
flowchart LR
    factory["backend_factory.cpp"] --> backend["vulkan_backend.cppm<br/>create_instance, unavailable_reason"]
    subgraph partitions["internal partitions"]
        common[":common<br/>check, result names, loader, timeouts"]
        translation[":translation<br/>to_vulkan / from_vulkan"]
        instance[":instance<br/>VulkanInstance, VulkanPhysicalDevice,<br/>WindowSurface"]
        device[":device<br/>records, HandleTables, NativeQueue,<br/>VulkanQueue, VulkanDeleter, BindlessTable,<br/>VulkanDevice"]
        pipelines[":pipelines<br/>set and pipeline layouts"]
    end
    subgraph units["implementation units (classes only one unit needs are declared there)"]
        u_device["vulkan_device.cpp"]
        u_queue["vulkan_queue.cpp"]
        u_resources["vulkan_resources.cpp"]
        u_pipelines["vulkan_pipelines.cpp, vulkan_layouts.cpp"]
        u_commands["vulkan_commands.cpp"]
        u_sync["vulkan_synchronization.cpp"]
        u_swapchain["vulkan_swapchain.cpp"]
        u_misc["vulkan_bindless.cpp, vulkan_diagnostics.cpp"]
    end
    subgraph plain["plain translation units (native headers)"]
        surface["vulkan_surface.cpp<br/>Win32 / Xlib / XCB / Wayland / Metal surfaces"]
        vma["vma_implementation.cpp"]
        renderdoc["renderdoc_capture.cpp"]
    end
    backend --> instance
    instance --> device
    device --> pipelines
    units --> partitions
    instance --> surface
    device --> vma
    u_misc --> renderdoc
```

- **Records**: each handle table maps a handle to a record holding the native object and what commands
  need (an image's format, aspects, extent; a pipeline's atomic `VkPipeline` for asynchronous builds and
  hot reload). Records exist because commands receive only handles, which by design carry no pointer;
  the objects themselves belong to the application.
- **Queues**: each gfx queue maps to a `NativeQueue` (several gfx queues may share one when a family has
  fewer queues than requested). A native queue has a mutex and a **progress timeline**: every submission
  signals it, and the deleter runs a deferred destruction once every native queue has passed the value
  it had when the destruction was requested. Submissions that would deadlock a shared hardware queue
  (waiting for a value only a later submission to it signals) are refused.
- **Bindless table**: one update-after-bind descriptor set per device; resources register themselves
  when created; every command buffer binds it once.
- **Presentation**: surfaces are created from the `NativeWindowHandle` in `vulkan_surface.cpp`; a
  swapchain's images are registered in the image tables (not owned, not bindless) so barriers and
  rendering use their handles; a window without area has no native swapchain.

### 3.4 Shaders

```mermaid
flowchart LR
    source["GLSL / HLSL source"] -- "build time: gfx_add_shaders()<br/>glslangValidator, depfiles" --> spirv["SPIR-V embedded in a<br/>generated C++ module"]
    source -- "Debug builds only:<br/>compile_shader() (glslang)" --> spirv_runtime["SPIR-V at run time"]
    spirv --> module["Device::create_shader_module"]
    spirv_runtime --> module
    module -- "reflect_spirv() (SPIRV-Cross)" --> layout["ShaderLayout<br/>bindings, push constants,<br/>inputs, outputs"]
    layout -- "validate_*_pipeline()" --> pipeline["pipeline creation<br/>(ShaderLayoutMismatchError)"]
    watch["Device::poll_shader_changes()<br/>(Debug)"] -- "changed file" --> module
```

## 4. Platform library

### 4.1 Object model

```mermaid
classDiagram
    direction LR
    class Platform {
        +poll_events() EventSpan
        +wait_events() EventSpan
        +wait_events_timeout(duration) EventSpan
        +push_event(Event) any thread
        +wake() any thread
        +add_event_watcher(function) EventWatcher
        +create_window(WindowDescription)
        +create_cursor(shape or image)
        +monitors() / monitor_properties() / video_modes()
        +clipboard_text() / clipboard_image()
        +input_devices() InputDevices&
        +supports(PlatformFeature) bool
        #pump(mode, timeout)*
        #emit(Event, text)
    }
    class Window {
        +framebuffer_size() any thread
        +size() / content_scale() / is_minimized() any thread
        +native_handle() NativeWindowHandle
        +set_title / set_icon / set_size / set_mode()
        +minimize / maximize / restore / show / hide()
        +set_cursor / set_cursor_mode()
        +start_text_input / stop_text_input()
    }
    class InputDevices {
        +gamepads() / joysticks() / touch_devices()
        +gamepad_properties(GamepadHandle)
        +is_gamepad_button_down() / gamepad_axis()
        +set_gamepad_rumble / set_gamepad_led()
    }
    class Cursor
    class EventWatcher {
        RAII registration
    }
    class Event {
        +type() EventType
        +window() WindowHandle
        +timestamp()
        +get_if() / as() / visit()
    }
    class EventSpan {
        +events
        +text(Event) string_view
    }
    Platform ..> Window : creates
    Platform ..> Cursor : creates
    Platform *-- InputDevices
    Platform ..> EventWatcher : creates
    Platform ..> EventSpan : returns per pump
    EventSpan o-- Event
    Window ..> NativeWindowHandle : exports
    class BackendPlatform {
        Sdl3Platform or GlfwPlatform
        with their windows, cursors and devices
    }
    BackendPlatform --|> Platform
```

Monitors, gamepads, joysticks and touch devices are **handles** queried through the platform (they come
and go with the hardware); windows and cursors are objects the application owns. Destroying a window
closes it; its native resources are released at the next pump, never inside a backend callback.

### 4.2 Events

`Event` is a **value type of at most 64 bytes, trivially copyable**: an `EventType`, the window it concerns,
a timestamp, and a `std::variant` of payload structs (`WindowEvent`, `KeyboardEvent`, `MouseButtonEvent`,
`GamepadAxisEvent`, ...). Each payload lists the event types it serves (`TYPES`), so
`Event::make<EventType::KEY_DOWN>(...)` only accepts the matching payload, and `get_if` / `as` / `visit`
read it. Text (typed text, dropped file paths, IME compositions) lives in the span's text buffer, not in
the event. The `EventQueue` behind each pump is reused: **no allocation while pumping** in steady state.

### 4.3 The pump

```mermaid
sequenceDiagram
    participant App as application (platform thread)
    participant P as Platform
    participant B as backend (SDL3 / GLFW)
    participant W as watchers
    App->>P: poll_events() / wait_events_timeout(t)
    P->>P: check the calling thread, refuse re-entrance
    P->>P: release what destroyed windows and cursors left (deferred)
    P->>P: take the events other threads pushed (push_event)
    P->>B: pump(mode, timeout)
    loop each native event or callback
        B->>B: translate (consteval key tables, scaled axes, UTF-8 text)
        B->>P: emit(event, text)
        P->>W: call watchers at once (real time, e.g. inside a resize loop)
    end
    B-->>P: an event was emitted, a wake was requested, or the deadline passed
    P->>P: take events pushed while waiting, drop events of windows already destroyed
    P-->>App: EventSpan (valid until the next pump)
```

**SDL3 backend**: an SDL event watch translates the events SDL raises while it pumps (so watchers run in
real time, also inside Windows' modal resize loop); keys come as USB HID scancodes, as gfx's `Key`;
gamepads (rumble, LEDs, touchpads) through SDL's gamepad API; sizes are events only when they changed.
**GLFW backend**: callbacks deliver events during the pump and queue those GLFW raises outside it; keys
go through a `consteval` table from `GLFW_KEY_*` to `Key`, checked by `static_assert`s; gamepads and
joysticks are polled each pump and reported as differences. What a backend lacks (GLFW: touch, IME
composition, rumble, ...) throws `BackendCapabilityError`. One backend is active at a time.

### 4.4 Input: from events to actions

```mermaid
flowchart LR
    events["EventSpan<br/>(one or more pumps)"] --> devices["device state<br/>keys (held and pressed this frame),<br/>mouse, gamepads per player,<br/>text input, focus"]
    schema["InputSchema<br/>(consteval: actions, contexts,<br/>default bindings)"] --> system
    devices --> system["InputSystem::update()"]
    subgraph evaluation["per player"]
        order["active contexts:<br/>priority, then latest activation"] --> bindings["bindings: read sources<br/>(consumed sources and blocked<br/>contexts read nothing;<br/>held chords hide their key;<br/>inputs held when a context<br/>starts reading them wait<br/>for a release)"]
        bindings --> shape["dead zone, curve, inversion,<br/>scale, swizzle, normalize"]
        shape --> triggers["triggers: down, hold, tap"]
        triggers --> states["action states:<br/>largest magnitude wins;<br/>pressed, just pressed/released,<br/>value, duration"]
    end
    system --> order
    states --> frame["InputFrame<br/>(double-buffered,<br/>read-only for every thread)"]
```

- The **schema** is built by `consteval` functions: actions (`BUTTON`, `AXIS_1D`, `AXIS_2D`), contexts
  (priority, consumes input, blocks lower contexts) and bindings. A wrong schema is a compile error whose
  message names the problem (`input_schema_error_two_actions_have_the_same_name()`).
- **Contexts** are activated per player and stay active while the returned `ContextActivation` lives.
- **Held inputs** (`ContextDescription::ignore_held_inputs`, on by default): each binding remembers which of
  its digital sources it could read in the previous frame; a source it starts to read (its context was
  activated, a context above stopped blocking or consuming it, text input ended) while held since an
  earlier frame reads as released until it is released. Presses that begin in the frame count; sticks,
  triggers and mouse motion are never ignored.
- **Players** own contexts, a gamepad (assigned automatically on first use or explicitly) and their action
  states; the keyboard and mouse belong to one player.
- **Rebinding**: `rebind`, `add_binding`, `remove_binding`, `capture_next_binding` (the captured press
  does not reach the actions), `find_bindings_using`; `save_bindings` / `load_bindings` write and read
  JSON naming keys and buttons (only bindings that differ from the defaults; loading is all or nothing).

### 4.5 Threads

```mermaid
flowchart TB
    subgraph main["platform thread (the one that created the Platform)"]
        pump["poll_events()"] --> send["EventChannel::send()<br/>(when the game runs on another thread)"]
    end
    subgraph game["game thread (often the same thread)"]
        receive["EventChannel::receive()"] --> update["InputSystem::update()"] --> logic["game logic"]
    end
    subgraph jobs["job threads"]
        read["read InputFrame by reference<br/>(no lock: immutable until the second update after it)"]
    end
    subgraph render["render thread"]
        sizes["Window::framebuffer_size(), is_minimized()<br/>(atomics)"] --> present["acquire, record, submit, present"]
    end
    any["any thread"] -- "push_event(), wake()" --> pump
    send -- "SPSC slots, one atomic flag each" --> receive
    update --> read
    logic --> render
```

This is the arrangement of commercial engines (Unreal, Unity, Godot, SDL's rules): the operating system
requires window events on one thread; input is evaluated once per game frame into an immutable frame
that any number of job threads read; the render thread never touches the platform or the input system.

## 5. Bridge: presenting to a window

`gfx.platform.graphics` holds the only code that knows both libraries:
`swapchain_description_for(window)` (native handle and framebuffer size), the recreation rules
(`requires_swapchain_recreation(event or status)`), and `WindowSwapchain`.

```mermaid
stateDiagram-v2
    [*] --> Presenting: create_swapchain(window)
    Presenting --> Presenting: acquire / render / present (OPTIMAL)
    Presenting --> RecreationRequested: resized, SUBOPTIMAL or OUT_OF_DATE
    RecreationRequested --> Presenting: next acquire() resizes, resets semaphores
    Presenting --> Minimized: window minimized
    Minimized --> Minimized: acquire() returns nothing
    Minimized --> RecreationRequested: window restored
    Presenting --> SurfaceLost: SURFACE_LOST
    SurfaceLost --> Presenting: next acquire() creates a new Swapchain
```

Semaphores: one **acquisition semaphore** per image plus one. Each acquisition takes a free one; when an
image comes back, the semaphore of its previous acquisition is free again (the image's presentation, and
therefore the rendering that waited on that semaphore, is over). One **rendering semaphore** per image:
the presentation engine may still hold the previous frame's semaphore, so reusing one per frame in flight
would be invalid.

One frame of the windowed example:

```mermaid
sequenceDiagram
    participant G as game loop
    participant WS as WindowSwapchain
    participant FT as FrameTimeline / FrameCommandPools
    participant Q as graphics Queue
    participant PE as presentation engine
    G->>G: events = poll_events(), frame = input.update(events)
    G->>WS: handle_event(each event)
    G->>WS: acquire()
    WS->>PE: acquire_next_image(signal = acquisition semaphore)
    PE-->>WS: image index
    WS-->>G: SwapchainFrame (image, view, acquired, rendered)
    G->>FT: begin_frame(): wait for frame N - frames in flight, reset its pool
    G->>G: record: UNDEFINED to COLOR_ATTACHMENT_WRITE, clear, draw, to PRESENT
    G->>Q: submit(wait acquired, signal rendered, signal timeline = N)
    G->>WS: present(queue, frame)
    WS->>Q: present(wait rendered)
    Q->>PE: queue the image
    PE-->>WS: OPTIMAL / SUBOPTIMAL / OUT_OF_DATE (recreation noted)
```

## 6. Build and tests

```mermaid
flowchart LR
    presets["CMakePresets.json<br/>debug / release (Ninja, MSYS2 GCC 16)"] --> configure["CMakeLists.txt<br/>options GFX_BACKEND_*, GFX_PLATFORM_BACKEND_*,<br/>GFX_BUILD_PLATFORM, GFX_BUILD_EXAMPLES, GFX_BUILD_TESTS"]
    configure --> modules["cmake/: import std gate, toolchain checks,<br/>project options and toolchain workarounds,<br/>dependencies (find_package, else FetchContent),<br/>gfx_add_shaders(), test helpers"]
    modules --> targets["gfx_core, gfx, gfx_platform,<br/>gfx_platform_graphics, examples, tests"]
    targets --> ctest["CTest: one test per GoogleTest case<br/>(POST_BUILD discovery), compilation-failure tests,<br/>naming check, example runs"]
```

| Suite | What it covers |
|---|---|
| `tests/toolchain` | C++23 and `import std` conformance; **defect detectors** that pass while a GCC 16 / MinGW defect exists (each workaround stays only as long as its defect) |
| `tests/core` | enumeration machinery, errors, handles, identifiers, native window handles, slot tables |
| `tests/graphics/*` | enumerations, errors, handles, the interfaces through a fake backend, shader layouts and validation, shader tools; `vulkan`: the Vulkan backend on every GPU of the machine with the validation layers (any message fails a test) |
| `tests/platform` | the platform library through a fake platform: events, windows, the pump, watchers, threads, the input system (schemas, contexts, players, triggers, rebinding, saved bindings, `EventChannel`) |
| `tests/platform/backends` | the contract every platform backend honors (one parameterized suite run on SDL3 and GLFW, real windows), then SDL3 (injected events, virtual gamepads) and GLFW (Win32 messages) |
| `tests/platform_graphics` | presentation to visible windows on every platform backend and GPU: cleared frames read back, the UI renderer drawing into the window (with the UI library), resize, minimize, frame latency, unsupported settings |
| `tests/compilation` | code that must not compile (handle kinds that do not mix, draws on compute recorders, invalid input schemas, ...) and a check that no enumerator collides with a platform macro |
| `WindowedExample.*` | the windowed example, 120 frames on each platform backend with the validation layers (label `example`) |

Toolchain workarounds the build applies (each with a detector test): `--allow-multiple-definition`
for template static locals `import std` emits twice; `<windows.h>`, glfw3native.h, nlohmann/json and
Vulkan platform headers in plain translation units; handle-table lookups only in implementation units;
brace-initialized members of classes declared in partitions; `STBI_NO_STDIO` before stb_image; no
capturing lambdas in in-class constructors of classes declared across partitions. Details are in
`.claude/CLAUDE.md` §6.
