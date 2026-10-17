# gfx ui: hybrid immediate/retained UI with an SDF/MSDF font pipeline — architecture, decisions and plan

Status: **approved 2026-10-17 with the answers of §D (audio out of scope, everything else as recommended); U0 committed;
U1 built, awaiting validation (§B, "U1 as built").** This document answers the
request "C++23 Commercial-Grade UI & Font System (SDF/MSDF)". It follows the platform design's form:
§0 lists where the request meets the existing project (each with a recommendation and a question in §D),
§A gives the architecture and decisions, §B the implementation plan, §C the API as proposed, §D the questions.

Facts checked on this machine (2026-10-16): GCC 16.1 provides `<flat_map>`, `<flat_set>`, `<mdspan>`,
`std::pmr`, `std::expected`, `<print>`; it has no constexpr `<cmath>` (C++26). `#embed` works inside a module
(a 200 KB blob compiled in 0.13 s and was inspected by `static_assert`), but `-Wpedantic` reports it as a C++26
extension. MSYS2 packages exist for FreeType 2.14.3, HarfBuzz 14.2.1 (with hb-subset), libunibreak 7.0,
woff2 1.0.2, Brotli 1.2.0 and zstd 1.5.7 (installed); none exists for msdfgen (latest v1.13, Nov 2025),
msdf-atlas-gen (v1.4, Mar 2026) or SheenBidi (v3.0.0, Jan 2026). There is **no audio abstraction** in the
repository.

---

## 0. Where the request meets the existing project

| # | Request | Situation | Recommendation |
|---|---|---|---|
| 1 | "sits on top of the previously defined graphics **and audio** abstractions", `UiAudioBridge` over `Emitter`/`Sound`/`Bus` handles, `create_ui_context(..., AudioDevice*)` | No audio abstraction exists | **Out of scope (you, 2026-10-17)**: the UI has no audio bridge and no audio events; it builds on the graphics abstraction only. |
| 2 | `platform/{win32, cocoa, x11, wayland, android, web, null}` with Win32 input + IME | The **platform library** already provides windows, events, keyboard, mouse, gamepads, IME composition, clipboard and cursors over SDL3 and GLFW | The UI **consumes `gfx::platform`** (events → `UiFrameInput`, clipboard, cursors, IME placement) through a small adapter; no second set of OS backends. "Null" = a context fed with `UiFrameInput` directly (tests, offscreen tools), whose platform services report what they cannot do. |
| 3 | A separate GitHub repository (`commercial-ui-font-abstraction`), git commands to push | The project rule: you commit and push; there is no `gh` here | **`source/ui/` in this repository** (target `gfx::ui`, `import gfx.ui;`), sharing core (handles, errors, enumeration machinery), the toolchain workarounds, CMake modules and the test infrastructure; the requested layout is kept inside `source/ui/`. A separate repository would duplicate all of it and fetch gfx as a dependency. If you want one anyway, I prepare it and give you the commands. |
| 4 | `template<class Tag> struct Handle { std::uint32_t value; };` | Core has `class Handle<Tag>` (private value, issued by backends through `HandleKey`) and `Identifier<Tag>` (hashed names) | Keep the core types (your earlier decision: a class, never `struct Handle`). Retained objects (fonts, atlases, dock nodes, themes) get `Handle`s; immediate-mode widget IDs are `Identifier`s (Dear ImGui's ID stack: label hash combined with the parent's ID, `consteval` for literals). |
| 5 | `UiCapabilityError : std::runtime_error` naming subsystem, operation, capability, suggestion | One error hierarchy in core (your decision for the platform library) | `UiCapabilityError : BackendCapabilityError` (hence `std::runtime_error`), adding `UiSubsystem` (GRAPHICS, FONT, INPUT, PLATFORM, ACCESSIBILITY); message origin `[gfx::ui::<subsystem>]`. **As built (U0)**: core's `BackendCapabilityError` is `final` and core stays unchanged (answer 4), so `UiCapabilityError` is that class thrown with a `UiSubsystem` backend identity (`using UiCapabilityError = BackendCapabilityError;`): messages start `[gfx::ui::FONT]`, and `error.backend() == UiSubsystem::FONT` tells the subsystem. |
| 6 | "Every text primitive in the entire codebase must render via SDF/MSDF; no bitmap path anywhere" | `DebugOverlay` (added today) draws a 5 × 7 **bitmap** font with cleared rectangles | When UI text exists (phase U3), the metrics move to a UI window drawn with SDF text and the bitmap font is **deleted** from the graphics library; `FrameMetrics` stays. |
| 7 | Atlases `consteval`-generated as `constexpr` artifacts | msdfgen is not constexpr; `consteval` MSDF generation would need a constexpr TrueType parser, constexpr math (`<cmath>` is not constexpr before C++26) and would hit the compiler's constant-evaluation limits | **Generation at build time** (the atlas tool, run by CMake), **embedding at compile time** (`#embed` of the `.uiatlas` into a generated module) and **validation at compile time** (`consteval` blob reader: `static_assert` on version, dimensions, SDF range, glyph coverage of given strings). |
| 8 | Runtime "zero glyph processing" | Shaping (HarfBuzz) needs the font's OpenType tables (GSUB/GPOS) at run time | The blob carries a **subset of the font** (hb-subset: only the glyphs of the atlas and their layout tables); no outline is decoded and no glyph is rasterized at run time. |
| 9 | Full Dear ImGui + docking parity, tables, plots, drag and drop, accessibility | Dear ImGui's docking branch is tens of thousands of lines, refined over a decade | **Phased plan** (§B, U0–U10), each phase built, tested, reported and validated like P0–P6. The interface for the whole surface is declared early (U4); widgets arrive by phase. |
| 10 | CI on Linux/Windows/macOS, backends for every platform | Windows + MinGW only here | Portable code; Windows verified; CI is your step 6 (MSVC/Clang/Linux). |
| 11 | Pinned third-party versions | FreeType/HarfBuzz/libunibreak are packaged; msdfgen/msdf-atlas-gen/SheenBidi are not | FetchContent with pinned versions for msdfgen v1.13, msdf-atlas-gen v1.4, SheenBidi v3.0.0; **FreeType also always fetched and pinned**, because byte-for-byte reproducible atlases need the same outline decoder everywhere. HarfBuzz, libunibreak, zstd: `find_package` first, FetchContent otherwise (as for SDL3/GLFW). |
| 12 | glm, cereal/flatbuffers | Core has its own value types; the blob must be readable by `consteval` code | No glm (2D affine transforms and rectangles are a few small types in `gfx.ui`); the `.uiatlas` is a **custom documented binary format** (cereal/flatbuffers cannot be read at compile time); dock layouts and themes are saved as JSON (nlohmann, as input bindings). |

---

## A. Architecture and design decisions

### A1. Immediate mode, retained state: the hybrid

**Choice: an immediate-mode API over retained state, with retained docking.** The application describes its UI
every frame (`if (ui.button("Save")) save();`), as in Dear ImGui: no widget objects to create, bind or keep in
sync with application data, no callbacks, and the code that draws a widget is the code that reacts to it. What
must survive between frames is retained, keyed by widget identifiers:

| Retained (by `UiContext`) | Lifetime |
|---|---|
| Windows: position, size, collapsed, scroll, z-order, focus | until the window is not submitted for a configurable time, then compacted |
| Dock tree: nodes, splits, ratios, tab order, selected tab, floating dock windows | until changed; saved and loaded as layouts |
| Per-widget state: tree node open, text edit state (cursor, selection, undo), drag value in progress, color picker mode | while the widget is submitted |
| Tables: column widths, order, visibility, sort specs | saved with the layout |
| Popups and modals stack, tooltips, drag-and-drop payload | while open |
| Shaped text runs (cache keyed by text hash, font, size, features) | least-recently used, fixed capacity |

Retained-mode **objects** exist where commercial tools need them: fonts, atlases, themes and styles, dock
layouts, keymaps and accessibility trees are long-lived objects with handles. This is Dear ImGui's docking
branch plus the retained resources of Slate and Qt: immediate for the widgets, retained for the structure that
users arrange and expect to find again.

### A2. Object model

```
UiContext (control thread, owns all mutable UI state)
├── fonts: FontLibrary ── FontAtlas (immutable, loaded from .uiatlas) ── Font (face × size × features)
├── themes: Theme (style vars + colors), switchable
├── dock: DockSpace ── DockNode tree ── TabBar ── TabItem → Window
├── windows: Window (floating or docked) ── child regions, tables, trees, menus, popups, tooltips
├── input: InputContext (keymap, focus, navigation, text input/IME, drag and drop)
├── accessibility: AccessibilityProvider (tree built from the frame's widgets)
└── services: UiPlatformServices (clipboard, cursors, IME rectangle, open URL), UiProfiler

per frame:  UiFrameInput ─▶ UiContext::begin_frame() ─▶ UiFrame (widgets, layout, DrawLists)
            ─▶ end_frame() ─▶ UiDrawData (immutable, double-buffered) ─▶ UiRenderer (any thread) ─▶ CommandBuffer
```

`UiFrame` is the per-frame recording object (Dear ImGui's global functions become its methods): it exists between
`begin_frame()` and `end_frame()` and holds the ID stack, layout cursors and the frame's DrawLists.

### A3. Handles and identifiers

- **Handles** (`Handle<Tag>`, core): `FontHandle`, `FontAtlasHandle`, `GlyphHandle` (an atlas glyph index),
  `ThemeHandle`, `StyleHandle`, `DockNodeHandle`, `WindowHandle` (UI window, distinct from the platform's),
  `LayerHandle`, `DrawListHandle`, `InputContextHandle`, `TextureHandle` (an image the UI shows), `SoundHandle`
  (an application sound, opaque to the UI). Issued by `UiContext`, resolved in its tables; a stale handle is an
  `InvalidHandleError` in construction-time APIs and a reported, ignored use in the frame.
- **Identifiers** (`Identifier<Tag>`, core): `WidgetIdentifier` — the hash of a label (or `"##id"` part) combined
  with the ID stack, as in Dear ImGui; `consteval` for literal labels, run time for formatted ones. Also
  `PanelIdentifier`, `TabIdentifier`, `MenuIdentifier`, `PopupIdentifier`, `TooltipIdentifier`,
  `SplitterIdentifier`, `TableIdentifier` — one tag each, so they cannot be mixed.
- Neither carries a pointer or a lookup structure; Vulkan, FreeType, msdfgen, HarfBuzz and SheenBidi objects stay
  in implementation units (private file sets and plain translation units, as for the other backends).

### A4. Widget inventory (Dear ImGui + docking branch parity)

| Group | Widgets (request names) | Phase |
|---|---|---|
| Windows | windows (floating, resizable, collapsible, closeable, scroll), `begin_child`, hierarchical windows | U4 |
| Basic | `text`, `text_wrapped`, `label`, `button`, `small_button`, `invisible_button`, `checkbox`, `radio_button`, `switch`, `selectable`, `bullet`, `bullet_text` | U4 |
| Layout | `begin_group`, `same_line`, `spacing`, `dummy`, `indent`, `separator`, `separator_text`, `columns` | U4 |
| Text helpers | `calc_text_size`, `get_text_line_height`, `push_text_wrap_pos`, `push_font`, `push_style_color`, `push_style_var`, `pop_*` | U4 |
| Style | the full style-var and color surface, themes (dark, light, classic, high contrast) | U4 |
| Input | `input_text` (single/multi-line, password, hint, filters and callbacks, IME), `input_int`, `input_float`, `input_double`, `slider_int`, `slider_float`, `drag_int`, `drag_float`, `color_edit` (RGB/HSV/hex, picker, alpha), `combo`, `list_box`, `selectable_list` | U5 |
| Hierarchy | `tree_node`, `collapsing_header`, `collapsing_header_ex`, `tab_bar`, `tab_item` | U6 |
| Menus and popups | `begin_menu_bar`, `begin_menu`, `menu_item`, `begin_popup`, `begin_popup_context_item`, `open_popup`, `begin_tooltip`, `begin_modal` | U6 |
| Drag and drop | `begin_drag_drop_source`, `set_drag_drop_payload`, `begin_drag_drop_target`, `accept_drag_drop_payload` | U6 |
| Tables | `table` (frozen rows/columns, sorting, resizing, reordering, per-column context menu), `splitter` | U7 |
| Visualization | `plot_lines`, `plot_histogram`, `progress_bar`, `image`, `image_button`, `image_rounded` | U7 |
| Docking | `dock_space`, `dock_builder`, `set_next_window_dock_id`, `set_next_window_dock_id_override`, `is_window_docked`, `set_window_dock_tab_bar`, tab reordering and tearing, layout save/load | U8 |
| Input mapping | `shortcut`, `is_key_pressed`, `is_key_released`, `is_mouse_*`, `get_io`, rebindable keymap, gamepad navigation | U4 (keys, mouse), U9 (keymap, gamepad) |
| Accessibility | roles, labels, values, states, live regions, focus order, screen-reader provider | U9 |

Widgets are member functions of `UiFrame` (`frame.button(...)`); the request's per-widget **concepts**
(`Button`, `Slider`, `InputText`, ...) constrain the *value types* widgets accept — e.g. `slider<Value>` requires
`ArithmeticValue<Value>`, `combo` accepts any `std::ranges::random_access_range` of `TextLike` items,
`input_text` accepts a `TextBuffer` (fixed capacity, or a user callback that grows it outside the frame).

### A5. The font pipeline

```
font file ─▶ FontSource ─▶ FontDecoder ─▶ outlines (+ metrics, kerning, GSUB/GPOS) ─▶ SdfGenerator
(any format)  (bytes)      (FreeType;      (glyph set closed over         (msdfgen: SDF, PSDF,
                            stb_truetype;   ligatures by hb-subset)        MSDF, MTSDF)
                            custom)
   ─▶ AtlasPacker ─▶ serializer ─▶ .uiatlas ─▶ (#embed into a module | file) ─▶ FontAtlas::load (no glyph work)
      (msdf-atlas-gen's         (zstd)                                        ─▶ TextShaper + TextBidi + line breaking
       tight packer; custom)                                                   ─▶ glyph quads ─▶ MSDF shader
```

| Role | Choice | Why |
|---|---|---|
| Font decoding (build time) | **FreeType** (FTL), pinned | The only widely used decoder that reads TTF, OTF/CFF/CFF2, TTC, WOFF (zlib), WOFF2 (Brotli) and variable fonts (`FT_Set_Var_Design_Coordinates`); msdfgen-ext loads outlines through it. `stb_truetype` is the second `FontDecoder` (TTF/OTF/TTC, no WOFF): small, dependency-free, used by the SDF fallback. |
| SDF/MSDF generation | **msdfgen v1.13 + msdf-atlas-gen v1.4** (MIT) | The reference implementation of multi-channel SDFs (sharp corners at any scale), SDF/PSDF/MSDF/MTSDF, overlap handling, error correction, multithreaded generation, a tight packer. Built without vcpkg, Skia, SVG, PNG and Artery Font. |
| Fallback SDF | `stb_truetype` outlines + a gfx SDF kernel (exact distance to quadratic segments) | Satisfies "stb_truetype-based SDF as fallback" and builds without msdfgen. |
| Default atlas kind | **MTSDF** (MSDF in RGB, true SDF in alpha) | MSDF for crisp corners; the true SDF channel gives soft effects (shadows, glows) and thick outlines, where MSDF alone artifacts. |
| Shaping | **HarfBuzz** (MIT) behind `TextShaper`, plus a built-in `BasicShaper` | HarfBuzz is the industry standard (Chrome, Android, Qt, Godot, Unreal). `BasicShaper` (cmap + kerning + standard ligatures from the blob) shapes Latin/Cyrillic/Greek runs without HarfBuzz: the fast path, and the shaper when HarfBuzz is not built. |
| Bidi | **SheenBidi v3.0.0** (Apache-2.0) behind `TextBidi` | A compact UAX #9 implementation in C (CMake, FetchContent-ready); it also locates script runs (UAX #24) for font fallback and shaping. |
| Line breaking, graphemes | **libunibreak 7.0** (zlib license) | UAX #14 line breaking and UAX #29 grapheme/word boundaries (cursor movement, double-click selection) without ICU's size. |
| Compression | **zstd** (BSD) | Fast decompression at load; deterministic output for a given level. |

### A6. What happens at compile time, build time and run time

| When | What |
|---|---|
| **Compile time** (`consteval`, `static_assert`) | The `.uiatlas` embedded by `#embed` into a generated module is a `constexpr std::array<std::byte, N>`; a `consteval` reader parses its header and section table and checks magic, version, endianness tag, dimensions, SDF range and kind, and **coverage**: `static_assert(gfx::ui::atlas_covers(EDITOR_FONT, u8"Ångström — ±∞"))`. Atlas descriptors in CMake are checked when the build is configured. Widget IDs of literal labels are hashed at compile time. Style defaults and theme tables are `constexpr`. |
| **Build time** (`gfx_add_ui_atlas()`, `cmake/atlas.cmake`) | The atlas tool (`gfx_ui_atlas_builder`, a host executable) decodes the font, closes the glyph set over ligatures and alternates (hb-subset), generates the distance fields with msdfgen, packs, extracts metrics/kerning/ligatures, subsets the font for run-time shaping, compresses and writes the `.uiatlas` (deterministic: same inputs and tool version → same bytes; a test regenerates and compares). Depfiles rebuild it when the font or descriptor changes. |
| **Run time, start-up** | `FontAtlas::load(blob)`: validates the header and checksums, decompresses the pixels once, uploads one `Image` per atlas page, builds no glyph. Loading a file at run time throws `UiCapabilityError`/`InvalidUsageError` for a bad or unsupported blob (the only run-time font error path). |
| **Run time, per frame** | Shaping of text not in the shaped-run cache, layout, quads. No outline, no rasterization, no SDF work. |

Embedding uses `#embed` (C++26, a GCC 15+ and Clang 19+ extension in C++23): only the generated atlas module is
compiled with `-Wno-c++26-extensions`; for compilers without `#embed` (MSVC today) the generator writes a hex
array instead (slower to compile, same result).

### A7. The `.uiatlas` format (version 1)

Little-endian throughout, every structure aligned to 8 bytes, offsets from the start of the blob:

```
Header (64 bytes)    magic "GFXUIATL", format version (major, minor), endianness tag 0x01020304, flags,
                     section count, total size, xxHash64 of the rest, generator version, source font hash
Section table        per section: kind (SectionKind), compression (NONE | ZSTD), offset, stored size,
                     uncompressed size, xxHash64
Sections             FACE_METRICS      units per em, ascender, descender, line gap, x-height, cap height,
                                       underline position/thickness, font name, family, style, axis values
                     GLYPHS            per glyph: glyph id, advance, plane bounds (em units), atlas bounds
                                       (pixels), page, flags (whitespace, color glyph unsupported)
                     CHARACTER_MAP     sorted (code point, glyph id) pairs — what coverage checks read
                     KERNING           sorted (left glyph, right glyph, adjustment) triples
                     LIGATURES         standard ligatures for BasicShaper (glyph sequence → glyph)
                     SHAPING_FONT      the hb-subset font (OpenType tables for HarfBuzz)
                     PIXELS            per page: width, height, channel layout (SdfKind), texel format
                                       (R8, RGB8, RGBA8, RGBA16F), distance range, then texels (zstd)
                     DESCRIPTOR        the build parameters (size, range, kind, padding, angle threshold,
                                       miter limit, packer), for diagnostics and reproducibility
```

Readers accept every minor version of their major version and skip unknown sections; a major version change
is refused with an error that names both versions. The format is documented in `docs/ui-atlas-format.md`.

### A8. Shaping, bidi, line breaking, fallback

1. **Itemize** a UTF-8 string: bidi levels and runs (SheenBidi), script runs (SheenBidi's script locator),
   then font runs: for each run, the first font of the run's **fallback chain** (per script: e.g. Latin → Inter,
   Arabic → Noto Sans Arabic, CJK → Noto Sans CJK subset, then a symbols font, then `.notdef`) that covers its
   characters.
2. **Shape** each run (HarfBuzz with the atlas's subset face, or BasicShaper when the run is simple) into glyph
   ids, advances, offsets and cluster indices.
3. **Break lines** at UAX #14 opportunities (libunibreak) when the width is exceeded; **reorder** each line
   visually (UAX #9 L1–L2).
4. **Cache** the shaped result keyed by (text hash, font, size, features, wrap width) in a fixed-capacity LRU;
   steady-state frames shape nothing.

Variable fonts: an atlas is built for one named instance or axis coordinates; several instances are several
atlases. Missing glyphs show as `.notdef` and are counted in the frame report.

### A9. Rendering: `UiDrawData` and `UiRenderer`

- Every DrawList holds vertices `{position, uv, color, packed SDF parameters}` and 16-bit or 32-bit indices, and
  commands `{clip rectangle, texture (bindless index), shader mode, index range, vertex offset}`; `UiDrawData` is
  the frame's ordered DrawLists plus the display size and scale — a plain, immutable value the render thread
  reads.
- **Shapes are SDF too**: rounded rectangles, borders, circles and frames are single quads whose fragment shader
  evaluates an analytic SDF (crisp at any DPI, few vertices); polylines and plots are triangle strips with an
  anti-aliased fringe.
- **Text**: one quad per glyph; the fragment shader samples the MTSDF atlas, computes the screen-pixel range with
  `fwidth` (correct under scaling and rotation) and applies the median; outline, shadow and glow use the true-SDF
  channel with per-command parameters.
- `UiRenderer` (in the UI library, graphics interfaces only): one graphics pipeline on the bindless layout (shader
  modes selected by push constants), atlas pages as `Image`s registered in the bindless table, vertices and
  indices written into a `TransientBufferAllocator` (persistently mapped, per frame in flight, lock-free), and
  `record(RenderingScope&, const UiDrawData&)`: per command a scissor, push constants and `draw_indexed`. The
  engine owns the pass, the swapchain and the submission; shaders are compiled at build time by
  `gfx_add_shaders()`.

### A10. Audio

Out of scope (you, 2026-10-17): no audio bridge, no audio events.

### A11. Input and platform

- `UiFrameInput` is a plain value: time, display size and scale, mouse, keys (gfx `Key`/`KeyCode`), text and
  IME composition, gamepad navigation values, focus. `gfx.ui.platform` builds it from a pump's `EventSpan` and an
  `InputFrame` of the input system (thread-safe snapshot), so the UI sees the same keys as the game.
- **Keymap and gamepad navigation** are an input schema context (`ui_navigation`: activate, cancel, move focus,
  tab, menu...), so they are rebindable and saved like any binding; a focused text field activates text input,
  which already silences gameplay keyboard bindings.
- **Input capture**: `UiFrame::wants_keyboard()` / `wants_mouse()` (Dear ImGui's `WantCaptureKeyboard/Mouse`),
  and a high-priority `ui` input context activated while the UI has focus, consuming what it uses.
- `UiPlatformServices` (clipboard, cursor shapes, IME candidate rectangle, open URL, multi-viewport windows
  later) is implemented over `gfx::platform`; the null services report each request they cannot honor.

### A12. Threads

| Thread | Does |
|---|---|
| Control (UI) thread — usually the game thread | `begin_frame(input)` → widgets → `end_frame()`; owns `UiContext` and everything mutable |
| Render thread | `UiRenderer::record()` from the latest published `UiDrawData` (two buffers swapped at `end_frame()`; the render thread holds one while the control thread fills the other) |
| Platform thread (main) | pumps events; `UiFrameInput` reaches the control thread through the input system's frame or an `EventChannel` |
| Build machine / any thread | the atlas tool |
| Any thread | read-only, shared: `FontAtlas`, `Theme`, `Style` (immutable after construction) |

### A13. No allocation per frame

- Each frame's transient data (layout stacks, ID stack, DrawList vertices/indices, text scratch, popup and
  tooltip stacks) lives in **arenas** (`std::pmr::monotonic_buffer_resource` over buffers sized at construction
  from `UiContextDescription` limits) reset at `begin_frame()`. DrawList arrays reuse their capacity.
- Retained state lives in `std::flat_map`s and slot tables with reserved capacity; a widget seen for the first
  time takes a free slot. Growth beyond the reserved capacity is the only allocation, recorded in the frame
  report (so the limits can be raised) — never an exception.
- HarfBuzz runs with a reused, pre-allocated `hb_buffer_t` and cached shape plans; new text may allocate inside
  HarfBuzz on its first shaping (the shaped-run cache keeps it to the first frame), simple runs take BasicShaper.
- **Verified by a test**: a test executable replaces `operator new` with a counting one and asserts zero
  allocations over frames 2…N of a demo UI covering every widget.

### A14. Errors and the non-throwing frame

- Construction and tooling APIs (contexts, atlases, themes, layouts, the atlas tool) throw
  (`UiCapabilityError`, `InvalidUsageError`) or return `std::expected` where failure is a normal outcome (loading
  a saved layout, parsing a theme).
- Every per-frame function is `noexcept`. Misuse inside a frame (unbalanced begin/end, an ID conflict, a
  capacity exceeded, a missing glyph, a stale handle) is recorded and returned by `end_frame()` as a
  `UiFrameReport`; Debug builds also show it in a diagnostics window and break into the debugger when asked.
- Font errors: compile time where the blob is embedded (`static_assert`), run time only when a blob is loaded
  from disk.

### A15. Accessibility

Each frame produces an **accessibility tree** from the widgets submitted (role `AccessibilityRole`, name from
the label or an explicit `accessible_label`, value, states, bounds, actions, relations), diffed against the
previous frame so providers receive only changes. Focus order is the navigation order; notifications and
status texts can be marked as live regions (polite/assertive). `AccessibilityProvider` receives the updates and
sends actions back as `UiFrameInput` events. Platform providers: **Windows UI Automation** (a COM provider in a
plain translation unit, as `<windows.h>` requires), and AccessKit (C bindings; Windows/macOS/Linux) as an
alternative to evaluate in U9.

### A16. Localization

UTF-8 everywhere; bidi and shaping make right-to-left and complex scripts correct; per-script fallback chains
and atlases built per language (glyph sets from the application's string tables); layout mirroring for
right-to-left (`LayoutDirection`); number and date formatting stay the application's (gfx formats only what it
displays itself, with `std::format`). String tables and plural rules are out of scope.

### A17. Modules and layout

```
source/ui/                       gfx::ui (gfx_ui), import gfx.ui;  (re-exports gfx.core)
  enums.cppm                     every UI enumeration and constant (the only file that defines them)
  handles.cppm                   UI handle and identifier tags, concepts UiHandle, FontHandleType, ...
  errors.cppm                    UiCapabilityError, UiSubsystem
  interface/                     module gfx.ui.interface: ui_context, frame (widgets), window, dock_node,
                                 draw_list, font, font_atlas, text_shaper, input, accessibility, profiler
  font/source/                   FontSource implementations: files, memory, TTC faces, custom (private)
  font/decoders/                 FreeType and stb_truetype FontDecoders (plain translation units)
  font/sdf/                      msdfgen SdfGenerator, stb SDF kernel
  font/atlas/                    packer, serializer (writer for the tool), reader (also consteval)
  font/shaping/                  HarfBuzz and BasicShaper TextShapers, SheenBidi TextBidi, libunibreak breaking
  render/                        UiRenderer (graphics interfaces), shaders (gfx_add_shaders)
  platform/                      gfx.ui.platform: UiFrameInput from gfx::platform, UiPlatformServices
  tools/atlas_builder/           the atlas tool (host executable)
  ui_factory.cppm/.cpp           create_ui_context(), the only public entry to the implementations
cmake/atlas.cmake                gfx_add_ui_atlas()
tests/ui/                        unit tests (null services, synthetic input), font pipeline tests (fetched OFL
                                 fonts), GPU tests (rendered text read back), allocation-counting test
examples/ui_editor/              the requested sample (dockable editor)
```

`gfx.ui.platform` is built when the platform library is; `gfx::ui` itself depends on graphics and core only.

### A18. RAII and deferred deletion

Contexts, atlases, fonts, themes, renderers are `std::unique_ptr`-owned; destroying an atlas while draw data
still refers to it defers the release of its images to the graphics deleter (the GPU may still read them).
Windows and widgets have no objects: their retained state is compacted when they stop being submitted.

### A19. Naming

As the project: snake_case functions, PascalCase types, `m_` members, `SCREAMING_SNAKE_CASE` enumerators and
constants in `enums.cppm`, full words (`InputTextFlag`, not `InputTextFlags`; bitmask enumerations are singular
as everywhere in gfx: `WindowFlag::NO_TITLE_BAR | WindowFlag::NO_RESIZE`). Dear ImGui names are kept where the
request uses them (`same_line`, `begin_child`, `dock_builder`).

### A20. Prior art

| Subsystem | Mirrors |
|---|---|
| Immediate widgets, ID stack, layout cursor | Dear ImGui (and its docking branch for docking, tables, multi-select) |
| Retained docking, tabs, tearing, saved layouts | Dear ImGui docking; Unreal Slate's tab manager; Qt dock widgets; Blender areas |
| Styles and themes | Dear ImGui style vars/colors; Qt style sheets; Unity UI Toolkit USS variables |
| Font pipeline | msdf-atlas-gen; Unity TextMesh Pro (SDF font assets generated in the editor); Godot 4 (MSDF fonts through msdfgen) |
| Shaping, bidi, breaking | HarfBuzz + ICU in Chrome/Android/Qt; Godot's TextServer (HarfBuzz + ICU) |
| Draw data and renderer | Dear ImGui `ImDrawData` + backends; Slate's element batcher |
| Accessibility | AccessKit (egui), Qt accessibility, Slate accessibility |

---

## B. Implementation plan

Each phase ends with everything built and tested in Debug and Release, Doxygen on every public declaration, a
report, and your validation (you commit).

| Phase | Content | Tests |
|---|---|---|
| **U0 Foundations** | `source/ui` library skeleton; `enums.cppm`, `handles.cppm`, `errors.cppm`; dependency helpers (FreeType, msdfgen, msdf-atlas-gen, HarfBuzz, SheenBidi, libunibreak, zstd: pinned FetchContent / find_package); test fonts fetched by URL + SHA-256 (OFL: Inter, Noto Sans, Noto Sans Arabic, Noto Sans Hebrew, a CJK subset) | enumeration traits, handle/identifier kinds that do not mix (compile-failure), error messages, dependency smoke tests, macro-collision scan |
| **U1 Atlas build** | `FontSource`/`FontDecoder` (FreeType, stb_truetype), glyph sets and closure, `SdfGenerator` (msdfgen; stb fallback), `AtlasPacker`, the `.uiatlas` writer, the atlas tool, `cmake/atlas.cmake` (`gfx_add_ui_atlas`, depfiles, `#embed` module), the format document | every decoder on TTF/OTF/WOFF/WOFF2/TTC/variable fonts (or its documented error), byte-for-byte reproducibility, unsupported formats throw `UiCapabilityError` |
| **U2 Atlas load + text** | the reader (run time and `consteval`), `FontAtlas`, `Font`, fallback chains, BasicShaper, HarfBuzz shaper, SheenBidi, libunibreak breaking, text layout and the shaped-run cache | `static_assert` coverage tests; shaping (ligatures, kerning, Arabic joining, Hebrew bidi), line breaking, cluster mapping, cache hits allocate nothing |
| **U3 Rendering** | `DrawList`, `UiDrawData`, SDF shapes, MTSDF text shader, effects, `UiRenderer` on the graphics interfaces; the metrics overlay moves onto it and the bitmap font is deleted | GPU tests: text and shapes rendered and read back on both GPUs (edges, rotation, scale), validation layers clean |
| **U4 Core immediate mode** | `UiContext`, `UiFrame`, ID stack, windows, layout, basic widgets, text helpers, styles and themes, `gfx.ui.platform` (input from gfx.platform, clipboard, cursors), focus, keyboard and mouse, the whole widget interface declared, the frame report, arenas | synthetic-input tests per widget; the allocation-counting test; a visible-window smoke test |
| **U5 Input widgets** | text input (IME, multi-line, password, filters, undo), numbers, sliders, drags, color editing and picker, combo, list boxes | editing semantics (graphemes, words, selection, clipboard), IME through SDL3, numeric parsing |
| **U6 Structure** | trees, collapsing headers, tab bars, menus, popups, context menus, tooltips, modals, drag and drop | open/close stacks, keyboard navigation of menus, payload types |
| **U7 Tables and visualization** | tables (frozen rows/columns, sorting, resize, reorder, hide, context menus), columns, splitters, plots, progress bars, images | table state round trip, sort specs, clipping of large tables without allocation |
| **U8 Docking** | dock space, dock nodes, splits, tab bars of docked windows, tearing and re-docking, dock builder, layout save/load (JSON) | dock tree operations, layout round trip, persistence of table state |
| **U9 Mapping, navigation, accessibility** | keymap as an input context, gamepad navigation, shortcuts, the accessibility tree and a Windows UI Automation provider (or AccessKit) | navigation order, shortcut routing, accessibility tree diffs, UIA queried by a test client |
| **U10 Sample and documentation** | `examples/ui_editor` (the requested sample: docked panels, menu bar, table, checkboxes, sliders, color picker, SDF text box, tooltip, draw data rendered by the render thread), README section, `THIRD_PARTY_LICENSES.md`, architecture overview update with diagrams | the sample runs N frames under CTest with validation, zero allocations after warm-up |

### U0 as built (2026-10-17)

- **Library**: `source/ui/`, target `gfx::ui` (`gfx_ui`, option `GFX_BUILD_UI`, ON), namespace `gfx::ui`,
  `import gfx.ui;` (re-exports `gfx.core`). Modules `gfx.ui.enums` (UiSubsystem with `BACKEND_LIBRARY`
  "gfx::ui", FontFormat, FontDecoderKind, SdfKind, AtlasTexelFormat, AtlasCompression, AtlasSectionKind,
  AtlasPackerKind, TextDirection; constants `UI_ATLAS_MAGIC` "GFXUIATL", `UI_ATLAS_VERSION_MAJOR`/`MINOR`,
  `UI_ATLAS_ENDIANNESS_TAG`, `UI_ATLAS_ALIGNMENT`, `UI_ATLAS_HEADER_SIZE`; every enumeration verified at
  compile time like the others), `gfx.ui.handles` (8 handle kinds: Font, FontAtlas, Theme, Style, DockNode,
  DrawList, Layer, InputContext; 11 identifier kinds: Widget, Window, Panel, DockSpace, Tab, Menu, MenuItem,
  Popup, Tooltip, Splitter, Table; `GlyphIndex` (0 = `.notdef`, explicit, formatted "Glyph(36)"); concepts
  `UiHandle`, `FontResourceHandle`, `UiIdentifier`; consteval literals `_window`, `_panel`, `_dock_space`,
  `_tab`, `_menu`, `_popup`, `_table` in `gfx::ui::literals`), `gfx.ui.errors` (`UiCapabilityError`, see
  §0-5, and `throw_unsupported(UiSubsystem, operation, capability, reason, suggestion)`).
- **Dependencies** (`cmake/GfxDependencies.cmake`; each function provides a `gfx::` target, fetched archives
  checked against their SHA-256, built static, excluded from module scanning):

  | Function | Library, version, license | Source |
  |---|---|---|
  | `gfx_require_freetype()` | FreeType 2.14.3 (FreeType License) — TrueType, CFF/CFF2, collections, WOFF (FreeType's own zlib), WOFF2, variable fonts; no PNG, BZip2 or HarfBuzz | always fetched (savannah) |
  | `gfx_require_brotli_decoder()` | Brotli 1.2.0 decoder (MIT), for WOFF2 | always fetched (FreeType's dependency) |
  | `gfx_require_msdfgen()` | msdfgen 1.13 + msdf-atlas-gen 1.4 (MIT); no Skia, SVG, PNG, vcpkg or Artery Font | always fetched |
  | `gfx_require_harfbuzz()` | HarfBuzz 14.2.1 (MIT) with hb-subset; no GLib, ICU, rasterizer, vector, GPU or utilities | always fetched |
  | `gfx_require_zstd()` | zstd 1.5.7 (BSD) | always fetched |
  | `gfx_require_sheenbidi()` | SheenBidi 3.0.0 (Apache-2.0), unity build | installed package first, else fetched |
  | `gfx_require_libunibreak()` | libunibreak 7.0 (zlib) | installed library first, else fetched |
  | `gfx_require_stb_truetype()` | stb_truetype 1.26 (MIT / public domain), the stb commit of stb_image | always fetched |
  | `gfx_require_ui_test_fonts()` | Noto Sans, Noto Sans Arabic, Noto Sans Hebrew (OFL 1.1, variable TrueType, axes wdth + wght) with their licenses, from google/fonts commit 47d54ca1 | downloaded at configure time into `<build>/ui-test-fonts`, never committed |

- **Deviations from the plan and the answers, for your validation**:
  1. *Answer 10 refined*: **HarfBuzz and zstd are always fetched and pinned too** (the answer had them from
     MSYS2 when installed). Both change atlas bytes: hb-subset writes the shaping font stored in each atlas,
     and zstd's output depends on its version. Only the libraries used at run time alone (SheenBidi,
     libunibreak) look for an installed copy first. Neither is installed here, so those branches are untested.
  2. HarfBuzz builds its FreeType helpers (hb-ft) whenever a `freetype` target exists, whatever
     `HB_HAVE_FREETYPE` says; `gfx_require_harfbuzz()` therefore requires FreeType first so the build does not
     depend on the order of the calls. gfx shapes with HarfBuzz's own font functions. HarfBuzz's configure
     step warns that its CMake build is "community-maintained" (the Meson build is the official one); the
     CMake build is kept because the project has no Meson. Optimized GCC builds warn (`-Wstringop-overread`) about
     the `memcmp()` in HarfBuzz's `hb_memcmp()`, a length range GCC cannot exclude; that warning is turned off
     for the HarfBuzz targets only.
  3. Test fonts: Inter and a CJK subset are not fetched yet — Noto Sans covers the Latin tests of U0–U1; the
     CJK font (tens of MB) arrives with the fallback tests of U2, subset by gfx's own atlas tool.
  4. The CMake option is `GFX_BUILD_UI` (the library needs no backend option: it renders through the graphics
     interfaces).
- **Facts found**: Noto Sans Arabic shapes dotted letters as a dotless body plus a separate dot glyph (beh =
  `uni066E` + `dotbelowar`), so tests of Arabic joining use undotted letters (seen, meem); msdf-atlas-gen
  produces identical atlases with 1 and 4 threads (the reproducibility U1 relies on).
- **Tests**: `gfx_ui_tests` (7: enumerations spelled and parsed, atlas-format values stable, handle and
  identifier kinds, glyph indices, capability errors), `gfx_ui_font_library_tests` (9, a plain translation
  unit: FreeType reads variable fonts and their axes, HarfBuzz joins Arabic and subsets, SheenBidi finds the
  runs of mixed Latin/Hebrew text, libunibreak breaks lines and graphemes, zstd round trip, msdfgen
  generates a glyph's MSDF, msdf-atlas-gen packs ASCII deterministically, stb_truetype reads outlines),
  compile-failure tests `UiHandlesOfDifferentKindsDoNotCompare` and `UiIdentifiersOfDifferentKindsDoNotMix`;
  the platform-macro collision scan covers `source/ui`.


### U1 as built (2026-10-17)

- **Libraries**: the runtime part of the format joins `gfx::ui` (module `gfx.ui.atlas_format`, re-exported by
  `gfx.ui`); the build-time pipeline is a library of its own, **`gfx::ui_atlas_tools`** (`import gfx.ui.atlas_tools;`,
  option `GFX_BUILD_UI_ATLAS_TOOLS`, ON with `GFX_BUILD_UI`), so applications that only load atlases never link
  FreeType, msdfgen or HarfBuzz's subsetter. The atlas tool `gfx_ui_atlas_builder` links it.
- **`gfx.ui.atlas_format`** (`source/ui/font/atlas/atlas_format.cppm`): `atlas_layout` (record offsets as
  `static constexpr` members of `Header`, `SectionEntry`, `Descriptor`, `FaceMetrics`, `AxisRecord`, `Table`,
  `GlyphRecord`, `CharacterRecord`, `KerningRecord`, `LigatureRecord`; constexpr little-endian `load`/`store`;
  `texel_size`, `channel_count`, `texel_format_fits`), constexpr `xxhash64` (checked against zstd's xxHash for every
  length up to 400 bytes), `font_tag`/`font_tag_name`, the decoded records (`AtlasHeader`, `AtlasSection`,
  `AtlasDescriptor`, `AtlasFaceMetrics` with `AtlasText` names, `AtlasGlyph` with `GlyphPlaneBounds` (em, y up)
  and `GlyphTexelBounds` (texels, y down), `CharacterMapping`, `KerningPair`, `Ligature`), and **`AtlasView`**:
  `parse(blob, AtlasVerification)` validates header, table, required sections, descriptor, table sizes, ordering,
  references and (by default) every checksum, returning `std::expected<AtlasView, AtlasReadError>`
  (`describe_atlas_error()`); lookups by binary search (`find_glyph`, `find_character`, `find_kerning`), pages,
  shaping font. Usable in constant expressions; `parse()` of a temporary `std::vector` is deleted (the view would
  dangle). The format is documented in [ui-atlas-format.md](ui-atlas-format.md).
- **`gfx.ui.atlas_tools`** (`source/ui/font/`): `FontSource` (file or memory, shared bytes, xxHash64) and constexpr
  `detect_font_format()`; `CharacterSelection` (CharacterSet names, `U+XXXX`, `U+XXXX-U+YYYY`, UTF-8 text without
  controls; `format_code_point()`); `GlyphOutline`/`OutlineBuilder` (font units, closed contours, lines, quadratic
  and cubic curves); `FontDecoder`/`FontFace` with **FreeType** (TrueType, CFF, CFF2, collections, WOFF, WOFF2,
  variable fonts; through the plain translation unit `freetype_library.cpp`, FreeType's headers include
  `<stdio.h>`) and **stb_truetype** (TrueType, CFF, collections; reads fvar for axes and instances, COLR for color
  glyphs, OS/2 and post for metrics); `SdfGenerator` with **msdfgen** (SDF, PSDF, MSDF, MTSDF; plain unit
  `msdfgen_kernel.cpp`) and the **built-in** kernel (SDF; curves flattened within 0.01 texel, only the pieces of
  lines on the outline of the filled area kept, so overlapping contours have no inner edges; within 0.0025 of
  msdfgen's values); `AtlasPacker` with **TIGHT** (msdf-atlas-gen's rectangle packer, plain unit
  `rectangle_packer.cpp`; smallest power-of-two or multiple-of-4 square, then pages of the maximum dimension) and
  **GRID**; `write_atlas(AtlasContent)` (zstd level 19 single-threaded for pixels and the shaping font, every
  checksum, parsed back before it returns); **`AtlasBuilder`** (`use_decoder`, `use_generator`, `use_packer` for
  custom implementations) and `build_atlas()`, with `AtlasBuildReport` (missing characters, color glyphs, pages,
  pair tables, sizes).
- **The build pipeline**: open the face → resolve the variation (axis values or a named instance, checked against
  the axes) → **hb-subset** the font: requested characters, closure over hb-subset's default layout features, the
  `.notdef` outline, no hinting, every axis pinned (a static instance; HarfBuzz's instancer gives the advances
  FreeType gives) → reopen the subset with the same decoder, so glyph indices are those of the shaping font → read
  every glyph on the calling thread → boxes and fields on worker threads (`std::jthread`, an atomic index; the bytes
  do not depend on the thread count) → pack → quantize into pages (8-bit `round(clamp(v) × 255)`, or binary16) →
  pair kerning and standard ligatures (2 to 4 characters) for the basic shaper, found by shaping every pair of
  Latin, Greek, Cyrillic and common characters with HarfBuzz (no locale: the default language system) → write.
  Without a shaping font (`shaping_font = false`, then no kerning and no ligatures), the face itself is read (its
  variation applied by the decoder: FreeType only).
- **Tool and CMake**: `gfx_ui_atlas_builder --font --output [--characters] [--text-file] [--variation]
  [--named-instance] [--kind --size --range --padding ...] [--depfile] [--embed-module --module-name --constant
  --namespace --embedding] [--require-all-characters]` (`--help`); **`gfx_add_ui_atlas()`** in
  `cmake/GfxUiAtlas.cmake`: checks the description when CMake configures, runs the tool with a depfile (the font and
  text files), and with `EMBED` adds a generated module exporting `inline constexpr std::array<std::byte, N>
  <NAME>` — `#embed` on GCC 15 and Clang 19 or later (`-Wno-c++26-extensions` on that file only), byte literals
  otherwise (`EMBEDDING BYTE_LIST`).
- **Deviations from the design, for your validation**:
  1. The CMake module is `cmake/GfxUiAtlas.cmake` (not `cmake/atlas.cmake`): the project's modules are named
     `Gfx*`, which also keeps a generic name out of consumers' `CMAKE_MODULE_PATH`.
  2. The atlas tools are a separate library and option (above), not part of `gfx::ui`.
  3. The reader of the format (`AtlasView`, compile-time validation included) came in U1: the writer checks its
     output with it and the tests read every section. U2 keeps `FontAtlas` (pages uploaded as images), the
     coverage helpers (`atlas_covers()`), fallback chains and the shapers.
  4. Format details refined from A7: the page geometry is in DESCRIPTOR (readable without decompressing), with one
     PIXELS section per page; FACE_METRICS keeps font units; every table starts with its count and record size;
     the header gives the section table's offset; glyph texel bounds are y-down (Vulkan's image origin).
  5. msdf-atlas-gen is used for packing only: its `GlyphGeometry` loads glyphs through msdfgen's FreeType handle,
     which would tie generation to one decoder, so gfx builds the msdfgen shape from its own outline and applies
     msdf-atlas-gen's steps itself (normalize, orientation fix, box with half the range, miters and padding, sign
     correction by scanline, error correction after it).
  6. Test fonts: Source Sans 3 is fetched too (OpenType CFF, variable CFF2, WOFF, WOFF2; Adobe's release branch,
     OFL); collections are assembled by the tests from two Noto fonts. Inter and the CJK font still arrive in U2.
- **Facts found**: hb-subset removes the `.notdef` outline unless `HB_SUBSET_FLAGS_NOTDEF_OUTLINE` is set;
  `hb_subset_input_pin_all_axes_to_default()` resets axes pinned before it (call it first); FreeType's
  `FT_Load_Sfnt_Table(face, 0, ...)` returns the tables it decoded from WOFF and WOFF2; HarfBuzz's static
  `harfbuzz-subset` repeats symbols of `harfbuzz` (linked with `--allow-multiple-definition`, which gfx already
  uses); stb_truetype truncates fractional CFF coordinates where FreeType rounds them, and the two may start
  TrueType contours at other points (same boxes; true distances within 2/255, MSDF colors may differ);
  `GfxRuntimeDeployment.cmake` kept its runtime list in a directory variable behind a global include guard (now a
  cache variable).
- **Tests**: `gfx_ui_atlas_tools_tests` (48: xxHash64 and layout, writer round trip, compression, refused content,
  every kind of damaged blob, format detection, character selections, outline builder, every decoder on every
  format and their agreement, variable fonts, generators (msdfgen fields, every kind, built-in against msdfgen,
  refusals), packers (no overlap, spacing, pages, grid), the builder (content, reproducibility with 1 and 8 threads,
  every kind and texel format, every font format, closure for Arabic shaping, instances, no shaping font, missing
  characters, several pages, refused descriptions, custom decoder and packer)), `gfx_ui_atlas_embed_tests` (2, plus
  `static_assert`s on two embedded atlases, one validated with every checksum at compile time), and CTest runs of
  the tool (`UiAtlasTool.*`: help, unknown option, required characters, same bytes with 1 and 8 threads).

---

## C. The API as proposed (for validation, not final code)

### C1. Atlas at build time, checked at compile time

As built in U1 (`cmake/GfxUiAtlas.cmake`; `atlas_covers()` arrives in U2):

```cmake
include(GfxUiAtlas)
gfx_add_ui_atlas(editor
    NAME editor_font                                   # editor_font.uiatlas, constant editor_font
    FONT ${fonts_SOURCE_DIR}/Inter-Regular.ttf
    CHARACTERS BASIC_LATIN LATIN_1_SUPPLEMENT GENERAL_PUNCTUATION ARROWS   # sets, U+XXXX, U+XXXX-U+YYYY
    TEXT_FILES strings/labels.txt                      # and every character of these UTF-8 files
    KIND MTSDF SIZE 40 RANGE 6 PADDING 2
    EMBED MODULE editor.fonts)                         # also embed it in a generated module
```

```cpp
import gfx.ui;
import editor.fonts;   // generated: inline constexpr std::array<std::byte, N> editor_font (#embed)

constexpr auto EDITOR_FONT = gfx::ui::AtlasView::parse(editor_font, gfx::ui::AtlasVerification::STRUCTURE);
static_assert(EDITOR_FONT.has_value() && EDITOR_FONT->descriptor().kind == gfx::ui::SdfKind::MTSDF);
static_assert(EDITOR_FONT->find_character(U'→').has_value(), "a label uses a character the atlas lacks");
```

### C2. Context, fonts and a frame

```cpp
std::unique_ptr<gfx::ui::UiContext> ui = gfx::ui::create_ui_context({
    .device = *device,                       // the graphics abstraction: images, buffers, pipelines
    .platform = gfx::ui::platform_services(*platform, *window),
});
const gfx::ui::FontHandle body = ui->load_font(gfx::ui::FontAtlas::load(editor_font), {.size = 16.0F});

// Control thread, each frame:
gfx::ui::UiFrame& frame = ui->begin_frame(gfx::ui::frame_input(*window, events, input.frame()));
frame.dock_space("editor"_dock);
if (frame.begin_window("Inspector"))
{
    if (frame.begin_menu_bar())
    {
        if (frame.begin_menu("File"))
        {
            if (frame.menu_item("Save", "Ctrl+S")) { save(); }
            frame.end_menu();
        }
        frame.end_menu_bar();
    }
    frame.checkbox("Visible", object.visible);
    frame.slider("Intensity", light.intensity, 0.0F, 10.0F);
    frame.color_edit("Color", light.color, gfx::ui::ColorEditFlag::ALPHA | gfx::ui::ColorEditFlag::PICKER);
    frame.input_text("Name", name_buffer, {.hint = "unnamed"});
    if (frame.is_item_hovered()) { frame.tooltip("Shown in the outliner"); }
    if (frame.begin_table("Objects", 3, gfx::ui::TableFlag::SORTABLE | gfx::ui::TableFlag::RESIZABLE))
    {
        // rows ...
        frame.end_table();
    }
}
frame.end_window();
const gfx::ui::UiFrameReport report = ui->end_frame();      // noexcept; problems of the frame, if any

// Render thread:
renderer.record(scope, ui->draw_data());                     // the published, immutable draw data
```

### C3. Enumerations (excerpt of `source/ui/enums.cppm`)

```cpp
enum class SdfKind : std::uint8_t { SDF, PSDF, MSDF, MTSDF };
enum class FontFormat : std::uint8_t { TRUETYPE, OPENTYPE_CFF, OPENTYPE_CFF2, COLLECTION, WOFF, WOFF2, CUSTOM };
enum class UiSubsystem : std::uint8_t { GRAPHICS, FONT, INPUT, PLATFORM, ACCESSIBILITY };
enum class WindowFlag : std::uint32_t { NONE = 0, NO_TITLE_BAR = 1U << 0, NO_RESIZE = 1U << 1, /* ... */ };
enum class AccessibilityRole : std::uint8_t { WINDOW, BUTTON, CHECK_BOX, RADIO_BUTTON, SLIDER, TEXT_FIELD, /* ... */ };
```

---

## D. Answers (2026-10-17)

| # | Question | Answer |
|---|---|---|
| 1 | Audio | **Out of scope**: focus on the GUI and font rendering only |
| 2 | Build on the platform library | **Yes** |
| 3 | Repository | **The same repository** (`source/ui/`) |
| 4 | Handles | **Core unchanged**, previous rules respected (`Handle`/`Identifier` classes) |
| 5 | `UiCapabilityError` | **Yes** (as built: an alias, since the core class is `final` and core stays unchanged; see §0-5) |
| 6 | Delete `DebugOverlay`'s bitmap font in U3 | **Yes** |
| 7 | Build-time atlases, `#embed`, `consteval` validation | **Yes** |
| 8 | `.notdef` and a report now, an optional run-time SDF cache later | **Yes** |
| 9 | Phase order U0–U10 | **Yes** |
| 10 | FreeType always fetched and pinned | **Yes** |
| 11 | UI Automation or AccessKit | **As recommended**: decided in U9 after a trial |
| 12 | Fetched OFL test fonts | **Yes** |

The questions as asked:

1. **Audio** (§0-1): `UiAudioBridge` as an interface the application implements, no audio library — or should an
   audio abstraction be designed first?
2. **Platform** (§0-2): the UI consumes the platform library (no Win32/Cocoa/X11/... backends of its own)?
3. **Repository** (§0-3): `source/ui/` in this repository, or a separate repository (which you create and push)?
4. **Handles** (§0-4): core `Handle`/`Identifier` instead of `struct Handle { value; }`?
5. **Errors** (§0-5): `UiCapabilityError : BackendCapabilityError` with a `UiSubsystem`?
6. **Bitmap font** (§0-6): delete `DebugOverlay`'s bitmap font in U3 and show the metrics through the UI?
7. **Compile-time atlases** (§0-7, A6): build-time generation + `#embed` + `consteval` validation, instead of
   `consteval` generation?
8. **Glyphs outside the atlases** (§0-8, A8): `.notdef` and a report (the request's "zero glyph processing"), or
   also an optional run-time MSDF cache for user text (CJK chat, file names) — still SDF, off by default?
9. **Phases** (§B): this order, or another priority (e.g. docking before tables)?
10. **Dependencies** (§0-11): FreeType always fetched and pinned for reproducible atlases (HarfBuzz, libunibreak,
    zstd from MSYS2 when installed)? Installing the MSYS2 packages `freetype harfbuzz libunibreak` is yours to do
    if you prefer system packages.
11. **Accessibility provider** (A15): Windows UI Automation written in gfx, or AccessKit's C bindings?
12. **Test fonts**: fetched at configure time (OFL fonts: Inter, Noto Sans, Noto Sans Arabic/Hebrew, a CJK subset),
    never committed — acceptable?
