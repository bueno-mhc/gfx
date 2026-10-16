# gfx platform: windowing and input — architecture, design decisions and plan

Status: **approved 2026-10-15; P0 to P3 committed; P4 (GLFW backend) done, awaiting validation; next P5 (graphics bridge and Vulkan presentation)**. Revisions after the user's answers:
events and `NativeWindowHandle` use `std::variant` (user preference over a tagged union), backends SDL3 and
GLFW only, threading model of commercial engines (A9).

This document answers the request "C++23 Backend-Agnostic Windowing & Input Abstraction": two layers
(windowing/surface and input/action mapping) above platform backends (GLFW, SDL3, Win32, XCB, Cocoa,
Web, headless), following the rules of the graphics abstraction that already lives in this repository.

Contents: [A. Architecture and design decisions](#a-architecture-and-design-decisions) ·
[B. Implementation plan](#b-implementation-plan) · [C. Code snippets](#c-code-snippets-the-api-as-proposed) ·
[D. Questions](#d-questions-for-the-user)

---

## A. Architecture and design decisions

### A1. Object model

```mermaid
flowchart TD
    factory["create_platform(PlatformBackendKind)"] --> platform[Platform]
    platform -->|creates, owns natively| window["Window (RAII)"]
    platform -->|enumerates| monitor["MonitorHandle → MonitorProperties, VideoMode"]
    platform -->|creates| cursor["Cursor (RAII)"]
    platform -->|pumps| queue["EventQueue: span&lt;const Event&gt; + text arena"]
    platform -->|exposes| devices["InputDevices: gamepads, joysticks, touch, rumble"]
    window -->|native_handle()| native["NativeWindowHandle (core)"]
    native -->|SwapchainDescription::window| graphics["gfx graphics: Swapchain"]
    queue -->|update(events)| input["InputSystem (backend-independent)"]
    devices --> input
    schema["InputSchema (constexpr): actions + binding contexts"] --> input
    input -->|"once per game frame"| frames["InputFrame (immutable for the frame, double-buffered)"]
    frames -->|"by reference"| game["game thread and the frame's jobs"]
```

| Object | Owns | Created by | Prior art |
|---|---|---|---|
| `Platform` | backend connection (glfwInit / SDL_Init), event pump, monitors, clipboard, handle tables | `create_platform()` | SDL_Init + video subsystem, Godot `DisplayServer`, Unreal `FGenericApplication` |
| `Window` | one native window | `Platform::create_window()` | `SDL_Window`, `GLFWwindow`, Unreal `FGenericWindow` |
| `Cursor` | one native cursor | `Platform::create_cursor()` | `SDL_Cursor`, `GLFWcursor` |
| Monitors | owned by the platform; the user holds `MonitorHandle`s | `Platform::monitors()` | `SDL_DisplayID`, `GLFWmonitor` |
| `InputDevices` | gamepads, joysticks, touch devices | `Platform::input_devices()` | SDL gamepad API, GLFW joystick API |
| `InputSystem` | action states, contexts, bindings, snapshots | `create_input_system()` (library code, not a backend) | Unreal Enhanced Input subsystem, Godot `Input` + `InputMap` |
| `InputFrame` | the input state of one game frame, immutable until the next update | `InputSystem::update()` | Unreal's per-tick `FInputActionValue` reads, Godot's per-frame `is_action_just_pressed` |

### A2. Why windowing and input are split

- **Input is pure library code**: `InputSystem::update(std::span<const Event>)` consumes normalized
  events and device information; it never calls a backend. It is therefore tested with synthetic events
  (no window, no display, no gamepad), and it runs identically on every backend.
- **Windowing is the backend's job**: windows, monitors, cursors, clipboard and the event pump talk to
  GLFW / SDL3 / the OS.
- They meet at two narrow points: the **event stream** (backend → input) and **`InputDevices`**
  (backend services input needs: gamepad enumeration, names, rumble, player LEDs, text-input control).
- A headless or offscreen application uses `InputSystem` with injected events (replays, tests, servers
  receiving remote input) and no window at all.

Prior art: Godot separates `DisplayServer` (windows) from `Input`/`InputMap` (actions); Unreal separates
`FGenericApplication`/`FGenericWindow` from Enhanced Input; SDL keeps video and events/gamepad as
distinct subsystems (`SDL_INIT_VIDEO`, `SDL_INIT_GAMEPAD`).

### A3. Shared core and repository restructure (reused parts of the graphics abstraction)

Several parts of the graphics library are generic and are needed by the platform library. They move to
a **core** directory and library that both depend on; graphics and platform never depend on each other.

| Moves to `source/core/` (module `gfx.core`, namespace `gfx`) | From |
|---|---|
| Enumeration machinery: `EnumerationTraits`, `EnumeratorName`, the concepts, bitmask operators, `to_string`, `std::formatter`, `ENUMERATOR_COUNT`, the `__PRETTY_FUNCTION__` verification; **new** `from_string` (needed to serialize key bindings) | `source/enums.cppm` |
| Error hierarchy (`Error`, `BackendCapabilityError`, `InvalidUsageError`, `InvalidHandleError`, `OutOfMemoryError`, …) with a backend identity usable by both libraries (§A12) | `source/errors.cppm/.cpp` |
| `Handle<Tag>`, `HandleKey`, `HandleIssuer`, handle constants; **new** `Identifier<Tag>` (§A4) | `source/handles.cppm` |
| `SlotTable`, `HandleTable` | `source/backends/common/` |
| Value types `Version`, `Extent`, `Offset`, `Rectangle`; **new** `Vector2` | `source/interface/common_types.cppm` |
| **`NativeWindowHandle`** (the contract between platform and graphics, §A5) — replaces graphics' `NativeWindow` | new |
| `ValidationLevel`, `DEBUG_BUILD`, `DEFAULT_VALIDATION_LEVEL` (both libraries validate) | `source/enums.cppm` |
| `windows_headers.hpp` workaround | `source/include/gfx/` |

Proposed tree (new folders under `C:\Users\mathe\Desktop\Coding\gfx`):

```
gfx/
├── cmake/                      shared (unchanged; + GfxPlatformDependencies for GLFW/SDL3)
├── docs/                       this document; later the architecture overview with diagrams
├── source/
│   ├── core/                   gfx.core      — enums.cppm (machinery + core constants), errors, handles,
│   │                                           identifiers, slot/handle tables, value types,
│   │                                           native_window_handle.cppm, include/gfx/windows_headers.hpp
│   ├── graphics/               gfx (graphics) — everything that is in source/ today, moved as is:
│   │                                           enums.cppm (graphics enums only), handles.cppm (graphics
│   │                                           tags + concepts), interface/, backends/, shaders/, backend_factory
│   └── platform/               gfx.platform  — enums.cppm, handles.cppm, interface/, backends/{glfw,sdl3},
│                                               input/, backend_factory
├── tests/{core,graphics,platform,toolchain,compilation}/
└── samples/                    later (step 5): headless and windowed samples
```

- **Graphics keeps its namespace and module names** (`gfx`, `import gfx;`, `gfx.interface`, …): no change
  for existing users; only files move. (Validated, answer 1.)
- **Platform lives in `gfx::platform`** with modules `gfx.platform`, `gfx.platform.enums`, … and the
  umbrella `import gfx.platform;`.
- **One enumerations file per library** (`core/enums.cppm`, `graphics/enums.cppm`, `platform/enums.cppm`):
  each library's enums and constants live only in its own file, and the core file holds the shared
  machinery. The platform-macro collision test scans all three.
- CMake targets: `gfx::core`, `gfx::graphics` (with the alias `gfx::gfx` kept), `gfx::platform`;
  options `GFX_BUILD_GRAPHICS`, `GFX_BUILD_PLATFORM`, `GFX_PLATFORM_BACKEND_{GLFW,SDL3,HEADLESS}`.

### A4. Handles and identifiers

Two kinds of strongly typed numbers, both from core, both 32 bits, both one tag per kind:

| | `Handle<Tag>` (existing) | `Identifier<Tag>` (new) |
|---|---|---|
| Names | an object a backend created | a name the application declared |
| Value | slot index + generation, issued by a `HandleIssuer` (backends) | FNV-1a hash of the UTF-8 name, computed by `consteval` |
| Stale detection | generation | not applicable (names never die) |
| Created by | the platform (windows, monitors, cursors, gamepads…) | `"jump"_action`, `"gameplay"_context` literals, or `ActionIdentifier{"jump"}` at compile time |
| Collisions | impossible | checked when a schema is built (`static_assert` for `constexpr` schemas, `InvalidUsageError` at run time) |

Platform kinds:

| Handles (issued by the platform) | Identifiers (declared by the application) |
|---|---|
| `WindowHandle`, `MonitorHandle`, `CursorHandle`, `GamepadHandle`, `JoystickHandle`, `TouchDeviceHandle` | `ActionIdentifier`, `BindingContextIdentifier`, `PlayerIdentifier` (a player slot index, `0 … MAXIMUM_PLAYERS-1`, not a hash) |

Concepts: `PlatformHandle`, `InputDeviceHandle` (gamepad, joystick, touch device), `WindowHandleType`,
`InputIdentifier`. The handle never contains a pointer, a map, or anything but its 32-bit value; native
objects (`GLFWwindow*`, `SDL_Window*`, `SDL_Gamepad*`, `HWND`) live in the backend's handle table.

Prior art: SDL3 uses numeric `SDL_WindowID`, `SDL_DisplayID`, `SDL_JoystickID` (never pointers in events);
Unreal identifies actions by asset, Godot by `StringName` (interned, compared by pointer) — the
compile-time hash gives Godot's cheap comparison without a string table at run time.

### A5. `NativeWindowHandle` and the graphics bridge

`NativeWindowHandle` is a **tagged union of operating-system handles stored as integers** (no `void*`,
no platform header in any public module), in **core**, because it is the contract between the two
libraries:

| Alternative | Contents | Graphics use |
|---|---|---|
| `Win32WindowHandle` | `instance` (HINSTANCE), `window` (HWND) | `vkCreateWin32SurfaceKHR`, D3D12 `CreateSwapChainForHwnd` |
| `XlibWindowHandle` | `display`, `window` | `vkCreateXlibSurfaceKHR` |
| `XcbWindowHandle` | `connection`, `window` | `vkCreateXcbSurfaceKHR` |
| `WaylandWindowHandle` | `display`, `surface` | `vkCreateWaylandSurfaceKHR` |
| `CocoaWindowHandle` | `window` (NSWindow), `metal_layer` (CAMetalLayer, created by the backend) | `vkCreateMetalSurfaceEXT`, Metal |
| `WebCanvasHandle` | canvas CSS selector (fixed inline buffer) | WebGPU |

Representation: a `std::variant` of the alternatives (user preference, 2026-10-15). `kind()` returns
`NativeWindowKind` (from the variant index through a `consteval` table), `get_if<Win32WindowHandle>()`
reads one alternative, `visit(overloads)` dispatches.

**Deviation from the request (validated, §D-3):** the request lists `GLFWwindow*` and `SDL_Window*`
as alternatives. They are left out: every graphics API creates its surface from the operating-system
handle, so the graphics library never needs GLFW or SDL (no `glfwCreateWindowSurface`, no link
dependency, no backend pointer leaving the backend). The Cocoa alternative carries a `CAMetalLayer` the
backend attaches (`SDL_Metal_CreateView`), which is the one thing a graphics backend cannot do alone.

The graphics side changes `SwapchainDescription::window` from `NativeWindow{connection, window}` (which
cannot say which windowing system it belongs to) to `NativeWindowHandle`. The **bridge** is then a
small header-free helper module (`gfx.platform.graphics`, built only when both libraries are) with
`swapchain_description_for(const Window&, …)` (extent from `framebuffer_size()`) and the resize rule
(recreate on `WINDOW_FRAMEBUFFER_RESIZED` or `SwapchainStatus::OUT_OF_DATE`). Implementing Vulkan
presentation (surface, swapchain, acquire, present) is part of this work (answer 7) — without it
the windowed example cannot run.

### A6. Event model: one value type, `std::variant` payload, no allocation

Choice: **`Event` is a trivially copyable value type with a `std::variant` payload**, not a class
hierarchy (the user preferred the variant to a C++ `union`, 2026-10-15).

- A closed set of event kinds suits a variant: exhaustive `std::visit` with overloaded lambdas (the
  compiler reports a missing case), no virtual dispatch, no `dynamic_cast`, events stored contiguously.
- Type and payload cannot disagree: events are created with
  `Event::make<EventType::KEY_DOWN>(window, timestamp, KeyboardEvent{…})`, whose payload type follows from
  the event type at compile time (`EventPayload<EventType::KEY_DOWN>` is `KeyboardEvent`).
- `sizeof(Event) <= 64` and `std::is_trivially_copyable_v<Event>` are `static_assert`ed.
- **Variable-length data never lives in the event**: committed text, IME composition strings and dropped
  file paths are written to the queue's **text arena** (a buffer reused every pump, grown only when a
  pump exceeds its capacity — amortized zero allocations) and the event holds a `TextReference{offset,
  length}`; `events.text(reference)` returns a `std::u32string_view` (text) or `std::string_view` (paths).
- The pump appends to a `std::vector<Event>` that is cleared, never shrunk: steady state allocates nothing.
- `EventType` (in `enums.cppm`) is the discriminator; each type maps to exactly one payload alternative
  through a `consteval` table, verified by `static_assert` (no runtime switch to keep in sync).

Payloads (one struct per category; the `EventType` distinguishes the edge, e.g. `KEY_DOWN`/`KEY_UP`):

| Payload | Event types |
|---|---|
| `WindowEvent` (size / position / scale data) | `WINDOW_SHOWN`, `WINDOW_HIDDEN`, `WINDOW_EXPOSED`, `WINDOW_MOVED`, `WINDOW_RESIZED`, `WINDOW_FRAMEBUFFER_RESIZED`, `WINDOW_CONTENT_SCALE_CHANGED`, `WINDOW_MINIMIZED`, `WINDOW_MAXIMIZED`, `WINDOW_RESTORED`, `WINDOW_FOCUS_GAINED`, `WINDOW_FOCUS_LOST`, `WINDOW_OCCLUDED`, `WINDOW_MOUSE_ENTERED`, `WINDOW_MOUSE_LEFT`, `WINDOW_CLOSE_REQUESTED` |
| `KeyboardEvent` (`Key`, `KeyCode`, `KeyModifier`, native scancode, `repeat`) | `KEY_DOWN`, `KEY_UP` |
| `TextInputEvent` (text reference) | `TEXT_INPUT` |
| `TextCompositionEvent` (text reference, cursor, selection length) | `TEXT_COMPOSITION` |
| `MouseMotionEvent` (position, delta, held buttons) | `MOUSE_MOTION` |
| `MouseButtonEvent` (button, position, click count) | `MOUSE_BUTTON_DOWN`, `MOUSE_BUTTON_UP` |
| `MouseWheelEvent` (precise `Vector2`, accumulated integer steps, flipped flag) | `MOUSE_WHEEL` |
| `GamepadDeviceEvent` | `GAMEPAD_CONNECTED`, `GAMEPAD_DISCONNECTED`, `GAMEPAD_REMAPPED` |
| `GamepadButtonEvent` / `GamepadAxisEvent` | `GAMEPAD_BUTTON_DOWN`, `GAMEPAD_BUTTON_UP` / `GAMEPAD_AXIS_MOTION` |
| `GamepadTouchpadEvent` | `GAMEPAD_TOUCHPAD_DOWN`, `GAMEPAD_TOUCHPAD_MOTION`, `GAMEPAD_TOUCHPAD_UP` |
| `JoystickDeviceEvent`, `JoystickButtonEvent`, `JoystickAxisEvent`, `JoystickHatEvent` | `JOYSTICK_CONNECTED`, `JOYSTICK_DISCONNECTED`, `JOYSTICK_BUTTON_DOWN`, `JOYSTICK_BUTTON_UP`, `JOYSTICK_AXIS_MOTION`, `JOYSTICK_HAT_MOTION` |
| `TouchEvent` (device, finger, normalized position, delta, pressure) | `TOUCH_DOWN`, `TOUCH_MOTION`, `TOUCH_UP`, `TOUCH_CANCELED` |
| `DropEvent` (path or text reference, position) | `DROP_FILE`, `DROP_TEXT` |
| `MonitorEvent` | `MONITOR_CONNECTED`, `MONITOR_DISCONNECTED` |
| `ApplicationEvent` | `QUIT_REQUESTED`, `KEYBOARD_LAYOUT_CHANGED`, `CLIPBOARD_UPDATED` |

Every event carries `WindowHandle window` (null for device-level events) and `timestamp` (nanoseconds,
steady clock; SDL3 provides it, GLFW events are stamped when its callback runs).

Delivery: `Platform::poll_events()`, `wait_events()`, `wait_events_timeout(duration)` fill the queue and
return `EventSpan` (the events plus the arena). **Event watchers** (`Platform::add_event_watcher`, RAII
registration) are called synchronously during the pump — needed because Win32 runs a modal loop while a
window is resized or moved, during which the application's own loop does not run (SDL's
`SDL_AddEventWatch` and GLFW's callbacks exist for the same reason). `Platform::push_event()` injects
events (replays, tests, cross-thread wake-ups), like `SDL_PushEvent`.

Prior art: SDL's `SDL_Event` union (one value type, type discriminator, no allocation, text in fixed
buffers — SDL3 moved text to an internal string pool, which is the arena idea), Godot's `InputEvent`
class hierarchy (rejected: reference counted heap objects per event).

### A7. Key model: physical `Key` and logical `KeyCode`

| | `Key` (physical) | `KeyCode` (logical) |
|---|---|---|
| Meaning | position on the keyboard, layout independent | symbol the current layout produces |
| Values | **USB HID usage IDs** (page 0x07), like `SDL_Scancode` — stable, serializable, documented | Unicode code point for printable keys (`KeyCode{U'a'}`); named enumerators for non-printable keys |
| Use | gameplay (WASD stays in place on AZERTY/Dvorak) | shortcuts and UI labels (Ctrl+Z is the key labeled Z) |
| Binding default | yes | opt-in (`bind(action, KeyCode{U'z'} \| KeyModifier::CONTROL)`) |

Per backend:
- **SDL3** gives both (`scancode`, `key`); `SDL_GetKeyFromScancode` labels keys in rebinding UIs.
- **GLFW** key tokens (`GLFW_KEY_*`) are physical positions named after the US layout → `Key` through a
  `consteval` table; `KeyCode` comes from `glfwGetKeyName` for printable keys (layout aware) and from
  the same table for the others.
- **Win32 (optional)**: scan code from `lParam` → HID table; `ToUnicodeEx` for `KeyCode`.

`KeyModifier` is a bitmask (`LEFT_SHIFT`, `RIGHT_SHIFT`, `LEFT_CONTROL`, `RIGHT_CONTROL`, `LEFT_ALT`,
`RIGHT_ALT`, `LEFT_SUPER`, `RIGHT_SUPER`, `CAPS_LOCK`, `NUM_LOCK`, `SCROLL_LOCK`) with combined
constants `SHIFT`, `CONTROL`, `ALT`, `SUPER` (either side). A key name that a platform header defines as
a macro gets the HID name instead (e.g. `DELETE` → `DELETE_FORWARD`, the HID usage name); the existing
platform-macro collision test enforces it.

Prior art: SDL's `SDL_Scancode`/`SDL_Keycode` split (HID values), Godot's `physical_keycode` vs `keycode`,
Unreal's `FKey` (logical) with physical binding options in Enhanced Input.

### A8. Action mapping subsystem

Model: **actions** (what the game asks), **binding contexts** (sets of bindings, layered with
priorities), **bindings** (a source plus modifiers), **players** (each with a context stack and devices).

- **Action**: `ActionIdentifier` + name + `ActionValueType` (`BUTTON`, `AXIS_1D`, `AXIS_2D`).
- **Binding kinds** (`BindingKind`): `KEY`, `KEY_CODE`, `KEY_CHORD` (key + required modifiers, exact or
  at least), `MOUSE_BUTTON`, `MOUSE_AXIS` (motion X/Y, wheel X/Y), `GAMEPAD_BUTTON`, `GAMEPAD_AXIS`,
  `GAMEPAD_CHORD` (buttons held together), `COMPOSITE_AXIS` (negative + positive digital sources → 1D),
  `COMPOSITE_2D` (up/down/left/right → 2D, e.g. WASD), `STICK` (two gamepad axes → 2D).
- **Modifiers** applied in order (Unreal's modifier pipeline, Godot's per-action deadzone):
  deadzone (`DeadzoneKind::AXIAL`, `RADIAL`, `SCALED_RADIAL`, lower and upper threshold), response
  curve (`ResponseCurve::LINEAR`, `QUADRATIC`, `CUBIC`, `POWER` with exponent), inversion per axis,
  scale, swizzle (a 1D source driving Y), clamp of 2D length to 1 (diagonals).
- **Value types and conversions** checked when the schema is built: a digital source on an axis action
  yields 0/1; an axis source on a button action is pressed above an actuation threshold (default 0.5).
- **Several bindings of one action**: the value with the largest magnitude wins (Godot's
  `get_action_strength`, Enhanced Input's highest-magnitude resolution).
- **Contexts and layers**: a context has a priority; each player has a stack of active contexts
  (`push_context`, `pop_context`, `set_contexts`). A context may **consume** its inputs: lower-priority
  contexts do not see a source a higher one used (Enhanced Input's input consumption; Godot's
  `set_input_as_handled`). E.g. `menu` (priority 10, consuming) over `gameplay` (priority 0).
- **Compile time**: the schema (actions, contexts, bindings) is a `constexpr` value built by `consteval`
  functions; `static_assert`s check unique names (hash collisions), that every binding names a declared
  action, and source/value-type compatibility. Lookup tables (source → bindings) for each context are
  computed at compile time too.
- **Run time**: `rebind`, `add_binding`, `remove_binding`, `reset_bindings`; a **listen mode**
  (`capture_next_binding(kinds)`) for "press a key" menus; contexts can also be registered at run time
  (mods, data-driven games).
- **Persistence**: only overrides relative to the schema are saved, as JSON with enumerator **names**
  (`"Key": "SPACE"`) via the enumeration traits (`to_string`/`from_string`), so files survive enum
  reordering; loading reports every unknown name at once. Round-trip tested for every `Key`, `KeyCode`,
  `MouseButton`, `GamepadButton`, `GamepadAxis` enumerator.
- **Players and devices**: `PlayerIdentifier` slots (`MAXIMUM_PLAYERS` = 8). Keyboard and mouse belong to
  player 0 by default; gamepads are assigned automatically on connection to the first player without
  one (`GamepadAssignment::AUTOMATIC`) or explicitly (`assign_device`). Bindings evaluate only events
  of the player's devices — couch multiplayer with one schema.
- **Text input** is separate: while a window's text input is active (`Window::start_text_input`), key
  events still arrive, `TEXT_INPUT`/`TEXT_COMPOSITION` carry the text, and contexts may declare
  `blocks_during_text_input` so typing "w" in a chat box does not move the character.

Prior art: Unreal Enhanced Input (`UInputAction` value types, `UInputMappingContext` with priorities,
modifiers: dead zone, negate, swizzle, response curve; triggers; input consumption; player-mappable
keys), Godot `InputMap` (named actions, per-action deadzone, `is_action_just_pressed`, `get_vector`),
SDL3 gamepad mappings and player indices.

**As built (P2, 2026-10-15)** — module `gfx.platform.input` (`source/platform/input/`), re-exported by
`gfx.platform`. Differences from the plan above and from snippet C6, decided while implementing:

- **No factory**: `InputSystem` is library code, so it is constructed directly
  (`InputSystem input{SCHEMA, {.players = 2}}`) instead of `create_input_system()`.
- **Contexts**: `activate_context(context, player)` returns a `ContextActivation` (RAII layer; `reset()`
  pops it early). No `push_context`/`pop_context`/`set_contexts`: one way to do it, and a layer cannot be
  popped by code that did not push it. `ContextDescription` = `priority`, `consumes_input` (default
  true), **`blocks_lower_contexts`** (modal menus; new) and **`reads_keyboard_during_text_input`**
  (default false; replaces `blocks_during_text_input`, same default behavior). Consumption is per
  source: a vehicle context binding W hides W from gameplay's WASD composite, S/A/D still work.
- **Bindings**: `InputSource {kind, code}` (`InputSourceKind`) + `BindingKind` {BUTTON, CHORD,
  GAMEPAD_CHORD, MOUSE_AXIS, COMPOSITE_AXIS, COMPOSITE_2D, STICK} instead of one kind per device;
  builders `single()`, `chord()`, `gamepad_chord()`, `composite_axis()`, `composite_2d()`, `stick()`,
  `bind(action, binding|input)`. Fluent modifiers are named `with_deadzone()`, `with_curve()`,
  `inverted()`, `scaled()`, `swizzled()`, `normalized()`, `held_for()`, `tapped_within()` (a member
  function cannot share the name of the data member `deadzone`). Held chords hide their key from
  single-key bindings (Ctrl+S does not also press S).
- **Schema**: `actions(action("jump"), ...)` and `context("gameplay", {...}, bind(...)...)` take names
  (the identifiers are hashed from them; names are kept for messages and saved bindings).
  `make_input_schema()` is `consteval`; a problem is a compilation error **naming it** (a call to a
  non-`constexpr` function such as `input_schema_error_two_actions_have_the_same_name()`), since a
  `static_assert` cannot inspect function arguments. Run-time schemas: `InputSchemaView` +
  `check_input_schema()` / `validate_input_schema()` (names in the message). Problems:
  `InputSchemaProblem` (+ `MISSING_NAME`, `INVALID_TRIGGER_TIME`, `INVALID_RESPONSE_CURVE`).
  No compile-time source → binding lookup tables: evaluation walks the active contexts' bindings each
  frame (tens of bindings per player; a lookup table can come later without an API change).
- **Devices**: `update(events)` only (no `InputDevices` argument): gamepads are tracked from their
  events (connected, or first button/axis event), assigned automatically or with `assign_gamepad()`;
  keyboard and mouse go to `assign_keyboard_and_mouse(player)` (player 0 by default).
- **Frames**: `InputFrame` (partition `:frame`, not `:snapshot`) answers `is_action_pressed`,
  `was_action_just_pressed`, `was_action_just_released`, `get_action_value`, `get_action_value_2d`,
  `action_state` (value, edges, pressed duration), with the player **last** (default player 0), plus
  raw keyboard, mouse and per-player gamepad state and `captured_binding()`. A press and release
  within one frame press the action for that frame and release it the next.
- **Triggers** `HOLD`/`TAP` use the update times (`update(events, now)` for tests and replays).
- **Rebinding**: `bindings()`, `rebind(BindingSlot, binding)`, `add_binding()`, `remove_binding()`,
  `reset_bindings()`, `find_bindings_using(source)` (conflicts), `capture_next_binding(player, sources,
  cancel key)` (the captured press does not reach the actions), `add_context()` at run time.
- **Persistence**: `save_bindings()` writes only lists that differ from the defaults
  (`{"format": "gfx.input.bindings", "version": 1, "players": [...]}`, inputs as
  `{"KEY": "SPACE"}`, unnamed key codes as `"U+20AC"`, fields equal to their default omitted).
  `load_bindings()` is all or nothing and lists every problem with its JSON path. Parsing uses
  nlohmann/json's SAX parser in a plain translation unit (`json_reader.cpp` behind the std-free header
  `json_reader.hpp`): its standard headers cannot share a module unit with `import std`.
- **EventChannel** `{slot_count}` (default 4): batches move through slots with one atomic flag each;
  when the consumer lags, events wait on the producer side (never blocks, never drops); call `send()`
  after every pump.

### A9. Threading model (revised: the model of commercial engines)

Revised 2026-10-15 at the user's request ("the threading model most commonly used in commercial game
engines"). What the engines and the two backends do:

| Engine / library | Who pumps OS events | Where input is processed | Rendering | Source |
|---|---|---|---|---|
| Unreal Engine | the game thread (the main thread on Windows) pumps messages (`PumpMessages`) | game thread, once per tick (Slate, then the player controller and Enhanced Input) | render thread one frame behind the game thread (game N+1 while render N), RHI thread further behind; the render thread receives game data through enqueued render commands, it does not read input | Epic threading overview, `PumpMessages` reference |
| Unity | main thread (player loop); native backends may queue events from background threads into a thread-safe buffer | main thread: the buffer is drained once per dynamic or fixed update (`InputSystem.Update`), then scripts read the state | render thread consumes the main thread's command stream | Unity Input System manual (Events) |
| Godot 4 | main thread (`DisplayServer::process_events`) | main thread, propagated through the scene tree, which is not thread-safe | optional separate render thread (project setting "thread model") | Godot "Thread-safe APIs" |
| SDL3 | the thread that initialized video (in practice the main thread); `SDL_PushEvent` from any thread | the application's loop | — | SDL3 `SDL_PollEvent` reference |
| GLFW | main thread only; callbacks only on the main thread; `glfwPostEmptyEvent` from any thread | the application's loop | — | GLFW thread safety guide |
| Job-system engines (e.g. Naughty Dog, GDC 2015 "frame-centric design") | main thread | per frame, at its start; the frame's data is shared with the frame's jobs | frames pipelined: game frame N, render N-1, GPU N-2 | GDC talk abstract (slides not consulted) |

On platforms where the operating system requires its own event loop on the main thread (macOS, iOS,
Android), Unreal runs the game thread on another thread and forwards events to it — stated from the
engine source's structure (`FCocoaGameThread`), not confirmed by the documentation searched.

The common model, adopted here:

1. **The main thread owns the platform.** It creates the `Platform`, windows and cursors and pumps
   events (`poll_events`, `wait_events`, `wait_events_timeout`). Calls from another thread throw
   `InvalidUsageError` at `ValidationLevel::CREATION` and above. `Platform::push_event()` and
   `Platform::wake()` (ends a `wait_events`) may be called from any thread (`SDL_PushEvent`,
   `glfwPostEmptyEvent`).
2. **Input is processed once per game frame, at its start, on the game thread** (the main thread by
   default): `const InputFrame& frame = input.update(events);`. The frame's input state is immutable
   until the next `update()`, so the game thread and every job of that frame read it by reference — no
   locks, no atomics, no pinning.
3. **Double buffering**: `update()` builds the next frame while the published one stays intact; "just
   pressed/released" compare with the previous game frame; several presses and releases between two
   frames all register (taps are never lost).
4. **A separate game thread is supported** (Unreal's arrangement on macOS and mobile): an `EventChannel`
   — a single-producer, single-consumer lock-free ring of event batches, text included — carries the
   main thread's events to the game thread, which calls `input.update(channel.receive())`. The same
   `InputSystem`, no other change.
5. **The render thread never touches the platform or the `InputSystem`.** It reads the window values it
   needs for its swapchain (`framebuffer_size()`, `content_scale()`, `is_minimized()`) through
   thread-safe cached values the pump updates (atomics). Input a render frame needs (rare, e.g. a
   latency-critical camera) is copied into its frame packet (`InputFrame` is copyable).

This replaces the earlier proposal of a ring of pinned snapshots readable from any thread at any time,
which none of the surveyed engines uses.

### A10. Windowing details

- **Creation**: `WindowDescription { title (UTF-8), size, position (optional; centered by default),
  minimum_size, maximum_size, aspect_ratio (optional), mode (WindowMode), flags (WindowFlag bitmask),
  monitor (fullscreen), video_mode (exclusive fullscreen), surface_api (SurfaceApi: NONE, VULKAN,
  DIRECT3D12, METAL, WEBGPU), icon images, debug_name }`.
- **Flags** (`WindowFlag`, a bitmask enum): `RESIZABLE`, `HIDDEN`, `ALWAYS_ON_TOP`,
  `TRANSPARENT_FRAMEBUFFER`, `HIGH_PIXEL_DENSITY`, `FOCUS_ON_SHOW`, `MOUSE_PASSTHROUGH`, `UTILITY`.
  Each backend declares a `consteval` mask of the flags it honors; creation with an unsupported flag throws
  one `BackendCapabilityError` listing every offending flag.
- **Modes** (`WindowMode`): `WINDOWED`, `BORDERLESS`, `BORDERLESS_FULLSCREEN` (desktop resolution),
  `EXCLUSIVE_FULLSCREEN` (with a `VideoMode`). **States** are separate operations: `show`, `hide`,
  `minimize`, `maximize`, `restore`, `focus`.
- **Queries**: `is_focused`, `is_hovered`, `is_minimized`, `is_maximized`, `is_visible`, `is_occluded`
  (throws where the backend cannot know, e.g. GLFW).
- **Monitors**: `Platform::monitors()`, `primary_monitor()`, `monitor_properties(handle)` (name, position,
  size, work area, content scale, current video mode with refresh rate), `video_modes(handle)`. Monitors
  stay owned by the platform; a disconnected monitor's handle becomes stale (generation) and a
  `MONITOR_DISCONNECTED` event says so.
- **Cursors**: `Platform::create_cursor(CursorShape)` (`ARROW`, `I_BEAM`, `WAIT`, `PROGRESS`, `CROSSHAIR`,
  `POINTER`, `RESIZE_EAST_WEST`, `RESIZE_NORTH_SOUTH`, `RESIZE_NORTHWEST_SOUTHEAST`,
  `RESIZE_NORTHEAST_SOUTHWEST`, `MOVE`, `NOT_ALLOWED`) or from RGBA with a hot spot;
  `Window::set_cursor(handle)`, `set_cursor_mode(CursorMode::NORMAL | HIDDEN | CONFINED | CAPTURED)`
  (captured = hidden, locked, relative motion), `cursor_position()`/`set_cursor_position()`.
- **Icons**: `Window::set_icon(std::span<const IconImage>)` with several resolutions, each RGBA8 or
  encoded PNG (decoded once by the library, so every backend accepts PNG).
- **Clipboard**: `clipboard_text()`/`set_clipboard_text()` everywhere; images where the backend supports
  them (SDL3), `BackendCapabilityError` elsewhere.
- **Closing**: `WINDOW_CLOSE_REQUESTED` is only a request; the application destroys the `Window`.
- **Deferred destruction**: destroying a `Window` (or `Cursor`) invalidates its handle at once (later
  events of that window are dropped from the queue), hides it, and releases the native window at the
  start of the next pump — never inside a backend callback. Transparent to the application.
- **`supports(PlatformFeature)` and `supports(CursorShape)`** let applications test before calling (the
  graphics library's `supports(DeviceFeature)` pattern); the call itself still throws when unsupported.

Prior art: SDL3 window properties and flags, GLFW window hints validated at creation, Godot's
`DisplayServer::window_set_mode` modes, Unreal's `EWindowMode` (fullscreen, windowed fullscreen, windowed).

### A11. DPI and high pixel density

- Three quantities, always named: **size** (window coordinates, what the OS calls points or logical
  units), **framebuffer size** (pixels — what swapchains use), **content scale** (the UI scale the user
  chose for the monitor, e.g. 1.5).
- Mouse and touch positions are in window coordinates; `Window::pixel_density()` (= framebuffer / size)
  converts to pixels.
- Events: `WINDOW_RESIZED` (size), `WINDOW_FRAMEBUFFER_RESIZED` (pixels; recreate the swapchain),
  `WINDOW_CONTENT_SCALE_CHANGED` (moving to another monitor; rescale the UI).
- Per backend: GLFW `glfwGetFramebufferSize` / `glfwGetWindowContentScale` / `GLFW_SCALE_TO_MONITOR`;
  SDL3 `SDL_GetWindowSizeInPixels` / `SDL_GetWindowDisplayScale` / `SDL_WINDOW_HIGH_PIXEL_DENSITY`;
  Win32 per-monitor DPI awareness v2 with `WM_DPICHANGED`'s suggested rectangle; Cocoa
  `backingScaleFactor` (on macOS, size ≠ framebuffer size; on Windows, size = framebuffer size and
  content scale carries the DPI).

### A12. Error strategy

Same contract as the graphics library, **one hierarchy shared through core**:
`gfx::BackendCapabilityError` thrown by the platform names the platform backend, the operation, the
missing capability, the reason and a suggestion:

```
[gfx::platform::GLFW] Platform::create_cursor: cursor shape WAIT is not available
  Reason: GLFW 3.4 has arrow, text, crosshair, pointer, resize, move and not-allowed cursors, but no wait cursor
  Suggestion: create a custom cursor from RGBA (Platform::create_cursor(CursorImageDescription)), or use the SDL3 backend
  Location: …/glfw_platform.cpp:212 (create_cursor)
```

To share the hierarchy, `Error` stores a **`BackendIdentity`** (library + enumerator name, built from any
backend enumeration with traits) instead of the graphics `BackendKind`; `error.backend() ==
gfx::BackendKind::VULKAN` and `error.backend() == platform::PlatformBackendKind::SDL3` both keep
working, so existing graphics code and tests compile unchanged. Other errors: `InvalidUsageError`
(wrong thread, invalid description, unknown action), `InvalidHandleError` (stale window, disconnected
gamepad), `BackendApiError` (a backend call failed: SDL_GetError / GLFW error text in the message).
Nothing unsupported is ignored silently.

### A13. Module strategy

- `gfx.platform` (umbrella) re-exports `gfx.core`, `gfx.platform.enums`, `gfx.platform.handles`,
  `gfx.platform.interface` (partitions: `:event`, `:window`, `:monitor`, `:cursor`, `:input_devices`,
  `:platform`), `gfx.platform.input` (partitions `:binding`, `:schema`, `:frame`, `:event_channel`, `:input_system`),
  `gfx.platform.backend_factory`.
- Backends are modules in **PRIVATE CMake file sets** (`gfx.platform.backends.sdl3`, `.glfw`),
  imported only by the factory's implementation unit — as in the graphics library.
- Backend headers (`SDL3/SDL.h`, `GLFW/glfw3.h`) appear only in backend global module fragments; code
  that needs `<windows.h>` (GLFW's native access `glfw3native.h`, Win32 HWND/HINSTANCE queries) lives in
  plain translation units (toolchain defect 2, the RenderDoc precedent).

### A14. RAII

`Platform`, `Window`, `Cursor`, `InputSystem`, `EventChannel`, event watcher registrations and
context activations (`ContextActivation` returned by `activate_context`, pops on destruction or
`reset()`) are RAII, non-copyable, movable where meaningful (
registrations). Destructors never throw. Deferred native destruction (A10) is internal.

### A15. Naming conventions (project rules) and deviations from the request

The project's rules apply: snake_case functions, PascalCase types, `m_` class members (description
structs without prefix), SCREAMING_SNAKE_CASE enumerators and constants, **full words**, Allman braces,
Doxygen on every public declaration. They lead to these deviations from the request (validated, answer 4):

| Request | Proposed | Why |
|---|---|---|
| handle named `Window`, object `Window` | `WindowHandle` (handle) and `Window` (RAII object); same for monitors, cursors, gamepads | the graphics convention (`BufferHandle` / `Buffer`); one name cannot be both |
| `KeyMod` with `CTRL` | `KeyModifier` with `CONTROL` | no abbreviations |
| `ActionId`, `BindingContextId`, `PlayerId` | `ActionIdentifier`, `BindingContextIdentifier`, `PlayerIdentifier` | no abbreviations |
| `BackendKind` (platform) | `PlatformBackendKind` | `gfx::BackendKind` already names graphics backends; both are used together |
| `f32` | `float` | standard type |
| `DeadzoneType`, `InputActionValueType` | `DeadzoneKind`, `ActionValueType` | the request itself uses both spellings; one each |
| `KeyCode` enumerators like `DELETE` | `DELETE_FORWARD` (HID name) | `DELETE` is a `<windows.h>` macro |

### A16. Backends

| Capability | SDL3 3.4 | GLFW 3.4 |
|---|---|---|
| Windows (create, title, size, position, min/max, aspect) | yes | yes (position: not on Wayland) |
| Icons | yes (alternate images for HiDPI) | yes on Win32/X11, not on Wayland/macOS |
| Modes: borderless, borderless fullscreen, exclusive fullscreen | yes | yes |
| Occlusion state | yes | throws |
| Monitors, video modes, work area, content scale | yes | yes |
| Standard cursors | all 12 | 10 (no `WAIT`, `PROGRESS`) |
| Custom cursors, cursor modes | yes | yes |
| Clipboard text / image | yes / yes | yes / throws |
| Keyboard, mouse, wheel (precise + integer) | yes | yes (integer steps accumulated; click count always 1; modifier sides read from the key states) |
| Text input / IME composition | yes / yes | yes (the IME commits characters; the text area is not used) / throws |
| Window shown / hidden events | from the system | from `show()` / `hide()` only (GLFW has no such callbacks) |
| Gamepads: buttons, axes, connect | yes (SDL mappings) | yes (GLFW's gamepad mappings); input polled at each pump: it does not end a wait |
| Gamepad rumble, LEDs, touchpads, sensors | yes | throws |
| Raw joysticks, hats | yes | yes |
| Touch | yes | throws |
| Drop files / text | yes / yes | yes (at the cursor: GLFW gives no drop position) / throws |
| Window flags | all but `MOUSE_PASSTHROUGH` | all but `UTILITY` |
| Native handle | Win32, X11, Wayland, Cocoa (+ Metal layer) | Win32, X11, Wayland, Cocoa |

Win32, XCB, Cocoa and Web native backends: ignored for now (answer 5; XCB, Cocoa and Web cannot be compiled or tested on
this machine).

**SDL3 backend as built (P3, 2026-10-16)** — module `gfx.platform.backends.sdl3`
(`source/platform/backends/sdl3/`, PRIVATE file set, CMake option `GFX_PLATFORM_BACKEND_SDL3`, ON).
`gfx_require_sdl3()`: an installed SDL3 3.4+ (static library preferred), else release 3.4.10 fetched by
URL + SHA-256 and built static with video, events, joysticks/gamepads, HIDAPI, haptics, sensors and
virtual joysticks only (no audio, renderer, GPU API, camera, dialog, tray, OpenGL); SDL's targets stay
out of module scanning and of the `import std` build. Decisions taken while implementing:

- **One SDL3 platform per process** (SDL's event queue and subsystems are global): a second
  `create_platform(SDL3)` throws `InvalidUsageError`; a new one may follow a destroyed one.
- **Real-time event watchers**: SDL's event watch (`SDL_AddEventWatch`) translates the events SDL
  raises on the platform thread during a pump at once, so `Platform` watchers run inside Windows'
  modal resize/move loop; their copies in SDL's queue are recognized and skipped when polled (in this
  pump or the next: SDL's poll stops at a sentinel). Events of other threads are translated when
  polled. This made `Platform`'s watcher dispatch **re-entrant** (a watcher's action may emit events at
  once): dispatches nest, and watchers added or removed meanwhile take effect when the outermost
  dispatch ends (P1 code fixed; it could reallocate the list a running callback lives in).
- **Timestamps** on the steady clock (SDL's ticks plus the offset measured at start-up).
- **Windows** are created hidden, set up (limits, aspect, icon, fullscreen) and then shown;
  `FOCUS_ON_SHOW` maps to SDL's activate-when-shown hint, and is now in the **default flags** of
  `WindowDescription` (a game window should take the keyboard when it appears). `MOUSE_PASSTHROUGH` is
  not supported (SDL has no such window): creation names it in a `BackendCapabilityError`. Content scale =
  the display's content scale (`SDL_GetDisplayContentScale`); framebuffer size = `SDL_GetWindowSizeInPixels`.
  Window positions throw on Wayland. A window or cursor may outlive its platform: it then refuses to
  work (`InvalidUsageError`) and its destruction is harmless.
- **Cursors**: SDL has one cursor, so a window's cursor (and `CursorMode::HIDDEN`) is applied while the
  mouse is over it; `CAPTURED` = relative mouse mode, `CONFINED` = mouse grab.
- **Clipboard images**: set as PNG (`SDL_SavePNG_IO`), read as PNG (decoded like every image) or BMP
  (SDL's loader). `GamepadType::NINTENDO_GAMECUBE` added (SDL 3.4 reports it).
  `PlatformFeature::GAMEPAD_SENSORS` is false (gfx has no sensor interface yet).
- **Devices**: gamepads and joysticks present at start-up are opened at once and reported once;
  gamepads are reported only as gamepads. SDL ignores joystick input while windows exist and none has
  the focus (its default).
- **Tests** (`tests/platform/backends/sdl3`, 16, besides the shared suite of P4): its feature set, deferred
  window destruction, events injected with `SDL_PushEvent`, the watch path (an event raised by a watcher
  reaches watchers at once and the queue once), virtual gamepads and a flight stick
  (`SDL_AttachVirtualJoystick`) with rumble and LED requests recorded, a gamepad connected before the
  platform (reported once), and an InputSystem reading a virtual gamepad. The clipboard test changes the
  system clipboard, so it runs only with `GFX_TEST_CLIPBOARD=1` (and only when the clipboard holds plain
  text, which it restores).

**GLFW backend as built (P4, 2026-10-16)** — module `gfx.platform.backends.glfw`
(`source/platform/backends/glfw/`, option `GFX_PLATFORM_BACKEND_GLFW`, ON). `gfx_require_glfw()`: an
installed GLFW 3.4, else release 3.4 fetched by URL + SHA-256 and built static (no examples, tests, docs).
Windows have no graphics context (`GLFW_NO_API`). Decisions taken while implementing:

- **Key table at compile time** (C7): every GLFW key token maps to a `Key`, checked by `static_assert`s
  (sorted, no duplicate, only F25 unmapped). GLFW names the ISO key next to left Shift `WORLD_2` on
  Win32/Wayland and `WORLD_1` on X11: both map to `Key::NON_US_BACKSLASH`; `MENU` maps to `APPLICATION`.
  Logical keys: the character of the layout (`glfwGetKeyName`) for the main block, code points for
  Space/Return/Escape/Tab/Backspace/Delete, `KEY_CODE_NON_PRINTABLE_FLAG | HID usage` for the others (as SDL).
- **Callbacks** emit at once during a pump (watchers run in real time, also inside Windows' resize loop);
  callbacks GLFW runs outside a pump (e.g. `glfwSetWindowSize` reports the resize synchronously) are kept
  for the next pump. Exceptions in callbacks wait for the pump (GLFW is C).
- **Gamepads and joysticks** are polled at each pump (GLFW reports connections only) and reported as the
  differences; triggers rescaled from GLFW's [-1, 1] to [0, 1]; USB identifiers parsed from SDL-style GUIDs.
- **Errors**: `glfwGetError()` after each call: `GLFW_FEATURE_UNAVAILABLE`/`UNIMPLEMENTED`/`CURSOR_UNAVAILABLE`
  → `BackendCapabilityError`, others → `BackendApiError` with GLFW's description.
- **Native handles** through a plain translation unit (`glfw_native.cpp`: `glfw3native.h` includes
  `<windows.h>`, toolchain defect 2) behind a std-free header, one query per windowing system.
- **One backend active at a time** (new factory rule): SDL3 and GLFW both pump the thread's message queue
  and would dispatch each other's window messages; `create_platform` refuses a second backend with
  `InvalidUsageError` (each backend also allows one platform of its own).
- **Waits end on an event, a wake or the deadline** (both backends, fixed while testing): GLFW's wait ends
  on any system message and SDL's on any SDL event, also those that become no gfx event, so the pumps wait
  again until an event is emitted, `wake()` is requested, or the timeout passes.
- **Tests**: the backend-independent tests moved into one parameterized suite run on every available
  backend (`tests/platform/backends/platform_backend_test.cpp`, 12 tests × 2 backends: factory, the
  documented exception of each missing feature, windows, resize, states, lifetime, threads, monitors,
  cursors, icons and modes, waits). GLFW-specific (`backends/glfw`, 9): its feature set and messages, and
  input through **Win32 messages posted to the window** (GLFW cannot inject events; its own window
  procedure handles them as real input): keys (repeat, extended keys), characters (surrogate pairs),
  mouse motion deltas, buttons, wheel steps, close requests, watchers, destroyed windows. The test
  modules are compiled into each executable (`gfx_add_test(... MODULE_BASE_DIRECTORY ...)`): a module
  library shared between test targets made GCC report "import 'std' has CRC mismatch".

### A17. Prior art per subsystem (summary)

| Subsystem | Mirrors |
|---|---|
| Platform object, pump, watchers | SDL `SDL_PollEvent`/`SDL_WaitEventTimeout`/`SDL_AddEventWatch`; GLFW `glfwPollEvents`/`glfwWaitEventsTimeout`; Godot `DisplayServer::process_events` |
| Window modes, flags, states | SDL3 window flags and fullscreen modes; GLFW window hints and attributes; Unreal `EWindowMode` |
| DPI | SDL3 pixel density / display scale; GLFW content scale; Win32 per-monitor v2; Cocoa backing scale |
| Native handle | SDL3 window properties (`SDL_PROP_WINDOW_WIN32_HWND_POINTER`, …); GLFW `glfw3native.h`; wgpu/`raw-window-handle` (Rust) — a tagged union of OS handles |
| Events | SDL `SDL_Event` (value type, no allocation, numeric IDs) |
| Keys | SDL scancode (HID) / keycode; Godot physical keycode |
| Actions, contexts, modifiers, consumption, rebinding | Unreal Enhanced Input; Godot `InputMap`; Unity Input System action maps and composites (WASD 2D vector) |
| Players / devices | SDL3 gamepad player index; Unreal local players; Godot device ids |
| Snapshots | engine game-thread → render-thread handoff (triple buffering) |
| Errors | the graphics library's `BackendCapabilityError` |

---

## B. Implementation plan

Each phase ends green in Debug and Release, with Doxygen on every public declaration, then a report and
your validation (project rule: step by step, you commit).

| Phase | Content | Tests |
|---|---|---|
| **P0 Restructure** | `source/core`, `source/graphics`, `tests/{core,graphics}`; enumeration machinery + `from_string`, errors with `BackendIdentity`, `Handle`/`Identifier`, slot tables, value types, `NativeWindowHandle` (tagged union) move to core; graphics `SwapchainDescription::window` becomes `NativeWindowHandle`; CMake targets `gfx::core`, `gfx::graphics` (`gfx::gfx` alias) | all 264 existing tests pass unchanged; new core tests (identifiers, `from_string`, backend identity, native handle) |
| **P1 Platform foundations** | `platform/enums.cppm` (keys, buttons, axes, modifiers, cursor shapes, modes, flags, event types, binding kinds, …), handles, `Event` + `EventQueue` + text arena, interfaces (`Platform`, `Window`, `Cursor`, `InputDevices`), `backend_factory` (no headless backend: tests use a test-only fake platform, as the graphics tests do) | event layout (`static_assert` size, trivially copyable, type ↔ payload table), error contract through the fake platform, thread check, deferred destruction, macro collision scan of the new enums file, compile-failure tests (handle kinds do not mix, identifiers from different tags do not mix) |
| **P2 Input** | `InputSchema` builders (`consteval`), bindings, modifiers, triggers (hold, tap), contexts, players, devices, `InputSystem::update` with double-buffered `InputFrame`s, `EventChannel` (main thread → game thread), rebinding, listen mode, JSON persistence | synthetic event streams: presses/edges, taps between frames, hold/tap timing, chords, composites, deadzones and curves (numeric tables), context priority and consumption, multi-player routing, text-input blocking, round trip of every key name, one frame read by several job threads, `EventChannel` with a real producer and consumer thread; compile-failure tests (duplicate action names, binding to an undeclared action, 2D composite on a button action) |
| **P3 SDL3 backend** | full: windows, modes, monitors, cursors, icons, clipboard, events → normalized, keyboard/text/IME, mouse, gamepads (+ rumble, LEDs, touchpad, sensors), joysticks, touch, drop, native handles | real windows (hidden where possible), event translation via SDL's own event injection, **virtual gamepads** (`SDL_AttachVirtualJoystick`) for the gamepad path without hardware |
| **P4 GLFW backend** | same contract; `consteval` `GLFW_KEY_*` → `Key` table; throws for what GLFW lacks (A16) | same suite as SDL3 parameterized over backends; capability tests expect the documented exceptions |
| **P5 Graphics bridge + Vulkan presentation** (approved) | instance surface extensions (Win32 here; Xlib/XCB/Wayland/Metal compiled on their platforms), `surface_support`, `create_swapchain`, acquire, present, recreation on resize/out-of-date, `gfx.platform.graphics` helper | a visible window presenting cleared frames on both GPUs, resize and minimize handling, validation layers on |
| **P6 Usage example + docs** | the example of the request (window, title/icon/size, resize, two contexts at compile time, `is_action_pressed("jump"_action)`, `get_action_value_2d("move"_action)`, native handle → swapchain); README (platform section, examples, use cases); CLAUDE.md; **architecture overview with diagrams** of the code as built (`docs/`) | example built and run in CI-like mode (closes itself after N frames) |

---

## C. Code snippets (the API as proposed)

Illustrations for validation, not final code.

### C1. `platform/enums.cppm` (excerpt)

```cpp
export namespace gfx::platform
{

/// @brief Physical key: a position on the keyboard, independent of the layout. Values are USB HID
///        usage IDs (keyboard page 0x07), like SDL scancodes, so they are stable when serialized.
enum class Key : std::uint16_t
{
    UNKNOWN = 0,
    A = 4,
    B = 5,
    // ...
    NUMBER_1 = 30,
    // ...
    RETURN = 40,
    ESCAPE = 41,
    BACKSPACE = 42,
    TAB = 43,
    SPACE = 44,
    // ...
    DELETE_FORWARD = 76, ///< HID "Delete Forward" (DELETE is a <windows.h> macro).
    // ...
    LEFT_CONTROL = 224,
    LEFT_SHIFT = 225,
    LEFT_ALT = 226,
    LEFT_SUPER = 227,
    RIGHT_CONTROL = 228,
    RIGHT_SHIFT = 229,
    RIGHT_ALT = 230,
    RIGHT_SUPER = 231,
};

/// @brief Keyboard modifiers held during an event.
enum class KeyModifier : std::uint16_t
{
    NONE = 0,
    LEFT_SHIFT = 1U << 0,
    RIGHT_SHIFT = 1U << 1,
    LEFT_CONTROL = 1U << 2,
    RIGHT_CONTROL = 1U << 3,
    LEFT_ALT = 1U << 4,
    RIGHT_ALT = 1U << 5,
    LEFT_SUPER = 1U << 6,
    RIGHT_SUPER = 1U << 7,
    CAPS_LOCK = 1U << 8,
    NUM_LOCK = 1U << 9,
    SCROLL_LOCK = 1U << 10,
    SHIFT = LEFT_SHIFT | RIGHT_SHIFT,       ///< Either shift key.
    CONTROL = LEFT_CONTROL | RIGHT_CONTROL, ///< Either control key.
    ALT = LEFT_ALT | RIGHT_ALT,             ///< Either alt key.
    SUPER = LEFT_SUPER | RIGHT_SUPER,       ///< Either super (Windows, Command) key.
};

/// @brief Gamepad buttons by position (south = A on Xbox, Cross on PlayStation, B on Nintendo).
enum class GamepadButton : std::uint8_t
{
    SOUTH, EAST, WEST, NORTH, BACK, GUIDE, START, LEFT_STICK, RIGHT_STICK, LEFT_SHOULDER,
    RIGHT_SHOULDER, DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT, MISCELLANEOUS_1, RIGHT_PADDLE_1,
    LEFT_PADDLE_1, RIGHT_PADDLE_2, LEFT_PADDLE_2, TOUCHPAD,
};

inline constexpr std::uint32_t MAXIMUM_PLAYERS = 8;
inline constexpr float DEFAULT_ACTUATION_THRESHOLD = 0.5F;

} // namespace gfx::platform
```

(Formatted one enumerator per line with Doxygen in the real file, as in the graphics enums.)

### C2. Identifiers and literals (core + platform)

```cpp
// core: a name hashed at compile time; one tag per kind, kinds do not mix.
template<class Tag>
class Identifier
{
public:
    consteval explicit Identifier(std::string_view name)
        : m_value{fnv1a_32(name)}
    {
    }

    [[nodiscard]] constexpr auto value() const noexcept -> std::uint32_t
    {
        return m_value;
    }

    [[nodiscard]] constexpr auto operator<=>(const Identifier&) const noexcept = default;

private:
    std::uint32_t m_value = 0;
};

// platform
using ActionIdentifier = Identifier<ActionTag>;
using BindingContextIdentifier = Identifier<BindingContextTag>;

namespace literals
{

consteval auto
operator""_action(const char* name,
                  std::size_t length) -> ActionIdentifier
{
    return ActionIdentifier{std::string_view{name, length}};
}

} // namespace literals
```

### C3. `NativeWindowHandle` (core)

```cpp
/// @brief A Win32 window: what vkCreateWin32SurfaceKHR and DXGI need.
struct Win32WindowHandle
{
    std::uintptr_t instance = 0; ///< HINSTANCE of the module that registered the window class.
    std::uintptr_t window = 0;   ///< HWND.
};

/// @brief The operating-system handles of a window, for graphics backends to create surfaces.
class NativeWindowHandle
{
public:
    using Alternative = std::variant<std::monostate, Win32WindowHandle, XlibWindowHandle, XcbWindowHandle,
                                     WaylandWindowHandle, CocoaWindowHandle, WebCanvasHandle>;

    constexpr NativeWindowHandle() noexcept = default;

    template<NativeWindowAlternative Handle>
    constexpr explicit NativeWindowHandle(const Handle& handle) noexcept
        : m_handle{handle}
    {
    }

    [[nodiscard]] constexpr auto kind() const noexcept -> NativeWindowKind;

    template<NativeWindowAlternative Handle>
    [[nodiscard]] constexpr auto get_if() const noexcept -> const Handle*
    {
        return std::get_if<Handle>(&m_handle);
    }

    template<class Visitor>
    constexpr auto visit(Visitor&& visitor) const -> decltype(auto)
    {
        return std::visit(std::forward<Visitor>(visitor), m_handle);
    }

private:
    Alternative m_handle;
};
```

### C4. Events

```cpp
struct KeyboardEvent
{
    Key key = Key::UNKNOWN;                    ///< Physical key.
    KeyCode key_code = KeyCode::UNKNOWN;       ///< Symbol of the current layout.
    KeyModifier modifiers = KeyModifier::NONE; ///< Modifiers held.
    std::uint32_t native_scancode = 0;         ///< Platform scan code (diagnostics).
    bool repeat = false;                       ///< Generated by key repeat.
};

class Event
{
public:
    /// @brief Creates an event; the payload type follows from the event type, so they cannot disagree.
    template<EventType TYPE>
    [[nodiscard]] static constexpr auto make(WindowHandle window,
                                             std::chrono::nanoseconds timestamp,
                                             const EventPayload<TYPE>& payload) noexcept -> Event;

    [[nodiscard]] auto type() const noexcept -> EventType;
    [[nodiscard]] auto window() const noexcept -> WindowHandle;           ///< Null for device events.
    [[nodiscard]] auto timestamp() const noexcept -> std::chrono::nanoseconds;

    template<class Payload>
    [[nodiscard]] auto get_if() const noexcept -> const Payload*;          ///< Null if another payload.

    template<class Visitor>
    auto visit(Visitor&& visitor) const -> decltype(auto);

private:
    EventType m_type = EventType::NONE;
    WindowHandle m_window{};
    std::uint64_t m_timestamp = 0;
    EventPayloadVariant m_payload{};                                       ///< std::variant of the payloads.
};

static_assert(sizeof(Event) <= 64);
static_assert(std::is_trivially_copyable_v<Event>);

// The pump: no allocation in steady state.
for (const Event& event : platform->poll_events())
{
    event.visit(overloaded{
        [](const KeyboardEvent& key) { on_key(key); },
        [](const WindowEvent& window) { on_window(window); },
        [](const auto&) {},
    });
}
```

### C5. Platform and window

```cpp
std::unique_ptr<Platform> platform = create_platform(PlatformBackendKind::SDL3, {.application_name = "demo"});

std::unique_ptr<Window> window = platform->create_window({
    .title = "gfx demo",
    .size = {.width = 1280, .height = 720},
    .flags = WindowFlag::RESIZABLE | WindowFlag::HIGH_PIXEL_DENSITY,
    .surface_api = SurfaceApi::VULKAN,
});
const std::array icons{IconImage::png(icon_64_png), IconImage::png(icon_256_png)};
window->set_icon(icons);

const Extent pixels = window->framebuffer_size();        // swapchain size (thread-safe)
const float scale = window->content_scale();             // UI scale
const NativeWindowHandle native = window->native_handle();
```

### C6. Compile-time input schema and queries (as built in P2)

```cpp
using namespace gfx::platform;
using namespace gfx::platform::literals;
using namespace std::chrono_literals;

constexpr auto GAME_INPUT = make_input_schema(
    actions(action("jump"),
            action("move", ActionValueType::AXIS_2D),
            action("charge"),
            action("confirm"),
            action("back")),
    context("gameplay",
            bind("jump"_action, Key::SPACE),
            bind("jump"_action, GamepadButton::SOUTH),
            bind("move"_action, composite_2d(Key::W, Key::S, Key::A, Key::D)),
            bind("move"_action, stick(GamepadAxis::LEFT_X, GamepadAxis::LEFT_Y)
                                    .with_deadzone(DeadzoneKind::SCALED_RADIAL, 0.15F)
                                    .with_curve(ResponseCurve::QUADRATIC)),
            bind("charge"_action, single(GamepadButton::WEST).held_for(500ms))),
    context("menu", {.priority = 10, .consumes_input = true, .blocks_lower_contexts = true},
            bind("confirm"_action, Key::RETURN),
            bind("confirm"_action, GamepadButton::SOUTH),
            bind("back"_action, Key::ESCAPE)));

static_assert(GAME_INPUT.actions.size() == 5);
// Does not compile: a duplicate name, a binding of an undeclared action, composite_2d on a BUTTON
// action, a HOLD trigger on an axis ("call to non-'constexpr' function
// 'input_schema_error_two_actions_have_the_same_name()'").

InputSystem input{GAME_INPUT, {.players = 2}};
ContextActivation gameplay = input.activate_context("gameplay"_context);                  // player 0
ContextActivation gameplay_two = input.activate_context("gameplay"_context, PlayerIdentifier{1});

// The default arrangement: the main thread is the game thread.
while (running)
{
    // Once per game frame, at its start: the frame stays unchanged until the second update() after it.
    const InputFrame& frame = input.update(platform->poll_events());
    if (frame.was_action_just_pressed("jump"_action))                    // player 0 by default
    {
        jump();
    }
    const Vector2 move = frame.get_action_value_2d("move"_action);
    const bool player_two_jumps = frame.is_action_pressed("jump"_action, PlayerIdentifier{1});
    jobs.run(frame);                                                     // the frame's jobs read it by reference
}

// A separate game thread (Unreal's arrangement where the OS owns the main thread's loop):
EventChannel channel;
// main thread:  channel.send(platform->poll_events());   // every pump, even an empty one
// game thread:  const InputFrame& frame = input.update(channel.receive());

// Rebinding menu: capture, check conflicts, rebind, save.
input.capture_next_binding();                                       // next frame: frame.captured_binding()
input.rebind({.context = "gameplay"_context, .action = "jump"_action, .slot = 0}, single(Key::J));
const std::vector<BindingSlot> conflicts = input.find_bindings_using(InputSource::of(Key::J));
const std::string saved = input.save_bindings();                    // JSON, names only
input.load_bindings(saved);                                         // all or nothing
```

### C7. GLFW key table at compile time

```cpp
// backends/glfw: GLFW key tokens are physical positions named after the US layout.
constexpr auto GLFW_KEYS = []
{
    std::array<Key, GLFW_KEY_LAST + 1> keys{};   // Key::UNKNOWN by default
    keys[GLFW_KEY_SPACE] = Key::SPACE;
    keys[GLFW_KEY_A] = Key::A;
    // ... every GLFW_KEY_* token
    keys[GLFW_KEY_DELETE] = Key::DELETE_FORWARD;
    return keys;
}();
static_assert(std::ranges::count(GLFW_KEYS, Key::UNKNOWN) == GLFW_KEYS.size() - GLFW_KEY_TOKEN_COUNT,
              "every GLFW key token maps to a Key");

constexpr auto
to_key(int glfw_key) noexcept -> Key
{
    return glfw_key >= 0 && glfw_key <= GLFW_KEY_LAST ? GLFW_KEYS[static_cast<std::size_t>(glfw_key)] : Key::UNKNOWN;
}
```

### C8. Graphics side

```cpp
// graphics: SwapchainDescription::window becomes the core NativeWindowHandle.
std::unique_ptr<Swapchain> swapchain = device->create_swapchain({
    .window = window->native_handle(),
    .extent = window->framebuffer_size(),
});

// or through the bridge module gfx.platform.graphics:
std::unique_ptr<Swapchain> swapchain = device->create_swapchain(swapchain_description_for(*window));
```

---

## D. Decisions and open points

### Answers of 2026-10-15

| # | Question | Answer |
|---|---|---|
| 1 | Restructure into `core` / `graphics` / `platform` | **Yes** |
| 2 | One error hierarchy in core | **Yes** |
| 3 | `NativeWindowHandle` in core, operating-system handles only | **As recommended** |
| 4 | Naming deviations (A15) | **As recommended** |
| 5 | Backends | **SDL3 and GLFW only** (follow-up answer); others ignored, reported as not built |
| 6 | Dependencies | **As recommended**: `find_package` first, FetchContent fallback (GLFW 3.4, SDL3 3.4.10, static); nlohmann-json; stb_image |
| 7 | Vulkan presentation | **Yes — the windowed example must really run** |
| 8 | Input extras | **As recommended**: hold and tap triggers, rumble and LEDs now; sensors and 3D axes later |
| 9 | Snapshot model | Superseded by the commercial-engine threading model the user asked for (A9), **confirmed** |
| 10 | Testing | **Window tests allowed; no gamepad available** (gamepad paths tested with injected events and SDL's virtual gamepads) |
| — | `std::variant` | First excluded, then **kept** for events and `NativeWindowHandle` (user preference over a union) |

### Follow-up answers (2026-10-15)

- **Events** (and `NativeWindowHandle`): `std::variant` — preferred to a tagged union. The graphics
  `DescriptorResource` stays a variant.
- **Backends**: SDL3 and GLFW only (no headless backend); Win32, XCB, Cocoa and Web ignored — their
  `PlatformBackendKind` enumerators exist and the factory reports them as not built.
- **Threading**: the commercial-engine model of A9.
