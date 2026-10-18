# gfx ui: hybrid immediate/retained UI with an SDF/MSDF font pipeline — architecture, decisions and plan

Status: **approved 2026-10-17 with the answers of §D (audio out of scope, everything else as recommended); U0 to U4
committed; U5 built, awaiting validation (§B, "U5 as built").** This document answers the
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


### U2 as built (2026-10-17)

- **Modules** (all in `gfx::ui`, re-exported by `gfx.ui`): `gfx.ui.unicode` (constexpr UTF-8: `decode_utf8`,
  `Utf8CodePoints`, `is_valid_utf8`, `is_drawn`), `gfx.ui.atlas_format` gains **`atlas_covers(atlas, text)`** and
  `first_missing_character()` (constexpr: `static_assert(atlas_covers(editor_font, u8"Ångström"))`), and
  **`gfx.ui.text`** (`source/ui/text/`): `FontAtlas`, `FontLibrary`, `TextLayoutEngine` (partitions `:font_atlas`,
  `:font_library`, `:text_layout`; internal `:engine`; units `font_atlas.cpp` (zstd), `font_library.cpp` (handle
  tables), `bidi.cpp` (SheenBidi), `line_breaking.cpp` (libunibreak), `shaping.cpp` (HarfBuzz and the basic shaper),
  `text_layout.cpp`). `gfx_ui` links HarfBuzz, zstd, SheenBidi and libunibreak privately.
- **`FontAtlas`**: `load(span)` reads an embedded atlas in place, `load(std::vector&&)` keeps the bytes,
  `load_file(path)`; validated (checksums by default), pages and shaping font decompressed once; immutable,
  shared through `std::shared_ptr<const FontAtlas>`. A damaged atlas → `InvalidUsageError` with the reason; another
  major format version → `UiCapabilityError`.
- **`FontLibrary`**: `add_atlas` → `FontAtlasHandle`, `add_font(FontDescription{atlas, fallbacks, size, language,
  shaping})` → `FontHandle` (unknown atlases and non-positive sizes refused), `remove_font`, `remove_atlas` (refused
  while a font uses it); lock-free lookups (core `HandleTable`, kept in the implementation unit).
- **`TextLayoutEngine::layout(text, font, TextLayoutDescription{wrap_width, alignment, direction, line_spacing})`**
  → `TextLayout` (glyphs: atlas, glyph, x, y on the baseline, advance, byte cluster, direction; lines: glyph and text
  ranges, left, width without trailing whitespace, top, baseline, height, direction; width, height, missing glyphs,
  `TextLayoutProblem` flags), `noexcept`. The pipeline, on code points (clusters mapped back to byte offsets, so
  invalid bytes never shift them): paragraphs at line and paragraph separators (CR LF one) → SheenBidi levels (base
  direction automatic, or forced) and scripts → the atlas of every character: the first of the chain that has it;
  characters of the Common and Inherited scripts (spaces, punctuation, marks) stay in the previous character's atlas
  when it has them; none → the first atlas's `.notdef`, counted → runs of one level, script and atlas → shaping:
  `TextShaping::AUTOMATIC` uses the basic shaper (character map, longest standard ligature, pair kerning from the
  atlas) for left-to-right Latin, Greek, Cyrillic and common runs without combining marks, HarfBuzz otherwise (the
  whole paragraph as context, monotone grapheme clusters, the font's language) → greedy line breaking at UAX #14
  opportunities, never inside a cluster, a word wider than the line broken at a grapheme boundary, trailing whitespace
  hanging → SheenBidi visual runs per line → alignment (START, END, LEFT, CENTER, RIGHT in the wrap width, or the widest
  line) → line metrics of the font's first atlas.
- **Cache**: fixed capacity (`TextLayoutEngineDescription::cache_capacity`, 1024), hashed (xxHash64 of the text, the
  font, the description) with chained buckets and an intrusive least-recently-used list; entries reuse their storage.
  `new_frame()` starts a frame; layouts returned before stay valid until then; an entry used this frame is never
  replaced — when all are, the layout lives in a per-frame overflow (`CACHE_FULL`). A hit allocates nothing (counted
  by a test that replaces `operator new`). `statistics()`: hits, misses, evictions, overflows, cached.
- **Carets**: `caret_position(layout, byte offset)` (inside a ligature, the ligature's width is shared; right-to-left
  clusters start on their right) and `hit_test(layout, x, y)` (the nearest cluster edge).
- **Deviations from the design, for your validation**:
  1. Pages are decompressed into CPU memory by `FontAtlas`; the renderer of U3 creates the images, so `gfx::ui` still
     depends on core only.
  2. HarfBuzz objects belong to the layout engine (one HarfBuzz font per atlas, a reused buffer), not to `FontAtlas`:
     atlases stay plain immutable data that any thread shares.
  3. Fallback chains are an ordered list of atlases per font (with the stickiness above), not per-script tables: the
     first atlas that has a character draws it. The language is per font.
  4. HarfBuzz is always built; the design's "basic shaper when HarfBuzz is not built" is not an option yet (the basic
     shaper is the fast path, and the shaper of atlases without a shaping font). Tell me if you want the option.
  5. A paragraph is shaped once and broken afterwards (no reshaping across a line break); tabs lay out as spaces (tab
     stops later); control characters are not drawn.
  6. `gfx_require_freetype()` now configures HarfBuzz first, so HarfBuzz never builds its FreeType helpers: before,
     linking HarfBuzz into an application would have linked FreeType too.
- **Facts found**: SheenBidi's lines give their runs in visual order; HarfBuzz gives right-to-left glyphs in visual
  order; SheenBidi and libunibreak headers work next to `import std` in a global module fragment (as HarfBuzz and zstd
  do); libunibreak has UTF-32 entry points.
- **Verified by eye**: a temporary test rendered a layout (Latin with kerning and ligatures, a Hebrew word inside
  Latin text, joined Arabic, wrapping) on the CPU from the MTSDF pages; the image was checked and the test removed.
- **Tests**: `gfx_ui_text_tests` (19: embedded, owned and file atlases, damaged and newer atlases, the font library,
  `static_assert` coverage checks; baselines, kerning and ligatures, the basic shaper against HarfBuzz on Latin text,
  Arabic joining, Hebrew reordered inside Latin, right-to-left paragraphs, fallback and missing glyphs, line breaking
  within the width, long words between graphemes, paragraphs and empty lines, alignment, problems, the cache (hits,
  overflow, eviction, clear), carets and hit tests) with atlases built by `gfx_add_ui_atlas()`;
  `gfx_ui_text_allocation_tests` (cached layouts allocate nothing over 100 frames).

### U3 as built (2026-10-17)

- **Modules** (in `gfx::ui`, re-exported by `gfx.ui`): **`gfx.ui.draw`** (`source/ui/draw/`: `Color` (sRGB-encoded,
  not premultiplied; `packed()` RGBA8, red in the low byte), `UiRectangle`, `Transform2D` (translation, scaling,
  rotation clockwise on screen, `about(pivot, transform)`, composed right to left), `DrawTexture`, `UiVertex`,
  `DrawCommand`, `TextStyle`, `DrawList`, `UiDrawData`; `draw_list.cpp`), **`gfx.ui.debug_overlay`** (`DebugOverlay`)
  and **`gfx.ui.renderer`** (`source/ui/render/`: `UiRenderer`, `UiRendererDescription`, `UiRendererStatistics`;
  shaders `ui.vert`/`ui.frag` compiled by `gfx_add_shaders()` into module `gfx.ui.shaders`). Enumerations added:
  `DrawTextureKind`, `UiShaderMode` (SOLID, SHAPE, GLYPH, GLYPH_SHADOW, IMAGE).
- **`DrawList`**: `add_rectangle_filled` / `add_rectangle` (border inside the edge; corner radius), `add_circle_filled`
  / `add_circle` (ring), `add_line` (square ends), `add_polyline` (mitered joins, open or closed), `add_convex_polygon_filled`
  (either winding), `add_image(bindless sampled-image index, rectangle, tint, texture coordinates)`, `add_text(fonts,
  layout, origin, TextStyle{color, outline color and width, shadow color, offset, softness})`; `push/pop_clip_rectangle`
  (intersected with the current clip), `push/pop_transform` (composed with the current transform). Rectangles, borders,
  circles, rings and lines are **one quad each**, evaluated by the fragment shader as a rounded box's signed distance (a
  quad a screen pixel larger than the shape, whatever the transform); polylines (four vertices per point) and polygons
  (a fan and a fringe) are triangles with an anti-aliased fringe a pixel wide **centered on the edge**, so they cover
  their exact area (ImGui's convention; a line thinner than a pixel fades instead); text is a quad per glyph with a box,
  its shadow's quads first. A new command starts only when the **clip rectangle or the texture** changes: shapes sample
  nothing, so they join any command (the whole metrics overlay, panel and text, is one draw). `clear()` keeps the
  arrays: a list refilled every frame allocates nothing once grown (counted by a test).
- **Shaders**: vertices are **pulled from a bindless storage buffer** (12 words per 48-byte `UiVertex`; no vertex input
  state); the shader mode is **per vertex** (`UiVertex::mode`: `UiShaderMode` in bits 0–7, the atlas's `SdfKind` in bits
  8–15), so one pipeline and one draw serve every primitive of a command. Shapes: rounded-box distance, the edge one
  screen pixel wide from the distance's **analytic gradient mapped to the screen through the derivatives of the shape's
  frame coordinates** (exact under any affine transform). Glyphs: median of the MSDF channels (the single channel of an
  SDF), the field's range in screen pixels from `fwidth` of the texture coordinates (correct under scale and rotation),
  outline from the MTSDF true distance under the fill, shadows and glows from the true distance softened by
  `shadow_softness`. On an sRGB target the vertex colors are converted to linear (flag in the push constants), so colors
  read back as given; blending is non-premultiplied alpha.
- **`UiRenderer(device, fonts, UiRendererDescription{color_format, samples, frames_in_flight, geometry_bytes_per_frame
  (8 MiB), debug_name})`**: one graphics pipeline on the bindless layout, one linear clamp-to-edge sampler, geometry in a
  `TransientBufferAllocator` (STORAGE | INDEX, persistently mapped). Per frame: `begin_frame(frame)` (the frame's region;
  staging buffers of finished frames released), `prepare(GraphicsRecorder&, data)` **before the rendering pass** (atlas
  pages a command samples uploaded once: image, view, staging buffer, barriers), `record(RenderingCommands&, data)` in
  the pass (every list's vertices and indices copied, pipeline, viewport, index buffer; per command its scissor clamped
  to the viewport, push constants, `draw_indexed` with the list's vertex offset). A frame whose geometry does not fit its
  region draws nothing and reports its commands as skipped; a command whose page is not resident is skipped.
  `statistics()`: draw calls, vertices, indices, skipped commands, resident pages, uploaded bytes.
- **The metrics overlay**: `gfx::ui::DebugOverlay(DebugOverlayDescription{visible, position, padding, corner_radius,
  text style, background})`: `draw(list, text engine, fonts, font, lines)` joins the lines (a reused buffer), lays them out
  (a cache hit while they are unchanged), adds a rounded translucent panel and the text, and returns the panel; `toggle()`,
  `set_visible()`, `set_position()`. **The graphics library's `DebugOverlay` and its bitmap font are deleted**
  (`debug_overlay.cppm` and its test); `FrameMetrics` stays (its tests now in `frame_metrics_test.cpp`).
- **Deviations from the design, for your validation**:
  1. `gfx_ui` now links the graphics library (PUBLIC) for the renderer, so `import gfx.ui;` brings the renderer.
     A separate `gfx_ui_renderer` target would keep the rest of the UI library graphics-free; tell me if you want it.
  2. A9 listed a shader mode and a vertex offset per command: the mode is per vertex (fewer commands and draws) and the
     vertex offset is the list's, applied by the renderer. Indices are 32-bit only (A9: 16 or 32).
  3. `UiDrawData` holds the lists and the target size, not the display scale (A9): the scale arrives with `UiContext`
     in U4; until then the application sizes its fonts (the example multiplies the overlay font by the window's content
     scale).
  4. `record()` takes `RenderingCommands&` (A9: `RenderingScope&`), so a parallel rendering part can record the UI too;
     uploads need `prepare()` before the pass, because copies and barriers cannot be recorded inside one.
  5. The overlay is a panel and text in a `DrawList`, not yet a UI window (§0-6 said "a UI window": windows arrive in U4).
     It is not Debug-only itself: it draws the lines it is given, and `FrameMetrics::text()` is empty outside Debug.
     The application provides its font (an atlas it builds with `gfx_add_ui_atlas()`).
  6. One sampler (linear, clamp to edge) for atlas pages and images; per-image samplers when images get their own API.
- **Other changes**: the presentation test `TheDebugOverlayIsDrawnIntoTheWindow` became `TheUiIsDrawnIntoTheWindow`
  (shapes of the UI renderer read back from the swapchain image; built when the UI library is); the windowed example
  draws its overlay with the UI library when the UI library and its atlas tool are built (Noto Sans, Basic Latin, MTSDF
  32 px, range 4, embedded), and F3 prints a note otherwise; the Vulkan test fixture's `physical_device()` is virtual,
  so a fixture can be parameterized over the machine's GPUs. CMake: file-scope variables of modules with a global
  include guard are only visible to the directory that included them first — the atlas option lists of `GfxUiAtlas`,
  `GfxShaders`' script path and stage list and `GfxDependencies`' Vulkan SDK tag are now inside the functions or cache
  variables (found when the example's directory called `gfx_add_ui_atlas()`). `UiShaderMode::TEXT` was renamed `GLYPH`:
  `TEXT` is a `<windows.h>` macro (found by the macro-collision check).
- **Facts found**: finite differences of the distance itself are wrong at a box's corners, where the distance changes
  direction: on the Intel GPU the gradient came out √2 long there, leaving the corner pixels of an axis-aligned rectangle
  at 218 instead of 255 — hence the analytic gradient. Coverage sampled at pixel centers along a 45° edge is a few percent
  off the exact area depending on where the edge falls (2.1% for the test's line, the same on the CPU and both GPUs).
- **Verified by eye**: the GPU tests can write every picture they read back (`GFX_TEST_PICTURE_DIRECTORY`); text at
  15 px (the overlay), 40 px, scaled, rotated, outlined and shadowed, and the shapes, were checked on both GPUs.
- **Tests**: `gfx_ui_draw_tests`: 18 on the CPU (colors, rectangles, transforms; every primitive's vertices and indices,
  margins under scale, radii and borders limited, empty and transparent primitives, polyline miters and thin lines,
  polygons of either winding, commands split on clip and texture, images, glyph quads from the atlas's plane bounds and
  texels, shadows and outlines, unknown atlases, clear; the overlay) and 17 on each GPU with the validation layers (sharp
  edges on pixel boundaries and half-covered pixels between, rounded shapes' areas and symmetry, borders and rings, lines
  and rotated rectangles, polygons and polylines, edges one pixel wide under scale, clips, several lists, images through
  their bindless index, sRGB targets, text compared with ui.frag's computation done on the CPU from the draw list and the
  atlas page — plain, scaled, rotated, outlined and shadowed — glyph stems full with one-pixel edges standing on the
  baseline, the overlay at 15 px, pages uploaded once over three frames, geometry beyond the budget skipped);
  `gfx_ui_text_allocation_tests` + a draw list refilled every frame (every primitive, text, the overlay) allocates
  nothing; the presentation test on 2 platform backends × 2 GPUs; the windowed example with `--metrics --validation`.

### U4 as built (2026-10-18)

- **Your decision (2026-10-18)**: the keyboard, mouse and cursor enumerations moved to core — `Key`, `KeyCode` (with
  `KEY_CODE_NON_PRINTABLE_FLAG`), `KeyModifier`, `MouseButton`, `MouseButtonMask` and, for the same reason (the UI asks
  for cursor shapes), **`CursorShape`** — in `gfx.core.enums`, namespace `gfx`; `gfx.platform.enums` names them in
  `gfx::platform` with using-declarations, so the platform library and its tests are unchanged. `gfx::ui` still depends
  on core and graphics only.
- **Modules** (in `gfx::ui`, re-exported by `gfx.ui`): **`gfx.ui.style`** (`Style`: every size, padding, rounding and
  alignment, `scaled()`; `Theme` {name, style, a `Color` per `StyleColor`}; constexpr `builtin_theme(ThemeKind)`: DARK,
  LIGHT and CLASSIC with Dear ImGui's colors, HIGH_CONTRAST of gfx's; `style_float()`/`style_vector()` map a
  `StyleVariable` to its member), **`gfx.ui.input`** (`UiFrameInput`: time, display size in UI units, pixels per unit,
  focus, mouse position and button events and buttons held, wheel, key events, modifiers, typed text;
  `UiPlatformServices` (clipboard, cursor, text input, links) and `UiNullPlatformServices`), **`gfx.ui.context`**
  (partitions `:types` (descriptions, `UiFrameReport`, `UiFrameIssue`, `UiFrameStatistics`, `KeyChord`, the value types
  of the declared widgets, concepts `ArithmeticValue` and `TextLike`), `:frame` (`UiFrame`), `:context` (`UiContext`,
  `UiDrawDataLease`, `UiDockBuilder`); internal partition `:state`; units `context.cpp`, `windows.cpp`, `layout.cpp`,
  `widgets.cpp`, `style.cpp`, `queries.cpp`, `later.cpp`), and **`gfx.ui.platform`** (target `gfx::ui_platform`, built with
  the platform library: `UiPlatformInput` turns a window's events into a `UiFrameInput`; `UiWindowServices` implements
  the services with the platform's clipboard and cursors and the window's text input). Enumerations added (29):
  StyleColor, StyleVariable, ThemeKind, UiWindowFlag, ChildFlag, ButtonFlag, SelectableFlag, UiCondition, ArrowDirection,
  HoveredFlag, FocusedFlag, UiFrameProblem, and those of the declared widgets (InputTextFlag, SliderFlag, ColorEditFlag,
  ComboFlag, TreeNodeFlag, TabBarFlag, TabItemFlag, PopupFlag, TableFlag, TableColumnFlag, TableRowFlag, SortDirection,
  DragDropFlag, DockNodeFlag, ShortcutFlag, AccessibilityRole, AccessibilityLiveRegion); `UiSubsystem::WIDGETS`; constants
  MAXIMUM_FRAME_ISSUES, MAXIMUM_ISSUE_NAME_SIZE, UI_FORMAT_BUFFER_CAPACITY, the double-click, drag and repeat defaults,
  DEFAULT_ITEM_WIDTH_FRACTION, MAXIMUM_COLUMNS.
- **Implemented**: windows (fit to their content on a first frame that is measured and not drawn; move by the title bar
  or any empty space; resize by the bottom-right grip, which shows the resize cursor, and fit on its double click;
  collapse by the arrow or a double click on the title; close button; kept reachable on the display; z-order by click;
  wheel scrolling of the window under the mouse or its first parent that scrolls; vertical and horizontal scrollbars,
  dragged or clicked; scrolling by code; `ALWAYS_AUTO_RESIZE`; a window begun twice appends; size constraints, content
  size, background alpha, conditions ALWAYS / ONCE / FIRST_USE_EVER / APPEARING), child regions (sized, filling, auto
  sized, bordered or frame style, clipped, scrolling on their own), layout (`same_line`, `new_line`, `spacing`,
  `dummy`, indentation, groups, legacy columns, cursor positions, `content_region_available`, item widths, wrap
  positions, `align_text_to_frame_padding`), widgets (text: plain, formatted with `std::format` into a 1 KiB buffer then
  the frame arena, colored, disabled, wrapped, bulleted, `label`; separators with and without text; `button`,
  `small_button`, `invisible_button`, `arrow_button`; `checkbox`, `checkbox_flags` over any bitmask enumeration;
  `radio_button` (bool and grouped); `toggle` (the request's `switch`); `selectable`; `bullet`; `begin_disabled`), the
  identifier stack, item and window queries, keys and mouse (presses, repeats, releases, double clicks, drags), the
  style stacks (colors, variables, fonts) and themes, the keyboard focus (Tab, Shift+Tab, Space, Return, Escape), the
  frame report, the diagnostics window, the published draw data and its leases.
- **Deviations from the design, for your validation**:
  1. `UiContext` is constructed directly (`UiContext ui{description};`), like the input system: there is one
     implementation, so no `create_ui_context()` factory or `ui_factory.cppm` (A17, C2).
  2. The context owns its `FontLibrary` (`fonts()`, `load_font(atlas, size)`, the first font is the default); a
     renderer takes `ui.fonts()`. C2 passed `.device` to the context: the context needs no device.
  3. **UI units are points**: positions and sizes are pixels divided by the window's content scale; `UiDrawData::scale`
     (pixels per unit) is applied by the renderer and `DrawList::set_pixels_per_unit()` keeps anti-aliased edges a pixel
     wide — U3's deviation 3 resolved this way, MSDF text staying sharp at any scale.
  4. **Three sets of draw lists** rotate (A12 said two buffers swapped): `end_frame()` swaps each window's list into a
     set that is neither the published one nor leased, so a render thread holding the published draw data
     (`acquire_draw_data()`) never waits for the next frame and the next frame never waits for it, unless two published
     frames are leased at once. Single-threaded applications read `draw_data()`.
  5. Themes are values (`set_theme(const Theme&)`, `builtin_theme()` constexpr), not objects behind `ThemeHandle`s
     (A3); `ThemeHandle` and `StyleHandle` stay declared, unused.
  6. Names: `UiWindowFlag` (the platform library has a `WindowFlag`), `StyleColor` / `StyleVariable` (ImGuiCol /
     ImGuiStyleVar), **`StyleColor::TEXT_NORMAL`** (`TEXT` is a `<windows.h>` macro: found by the macro-collision check),
     `UiCondition`, `toggle()` for `switch` (a keyword), `calculate_text_size()` / `calculate_item_width()` (full words),
     `label()` for ImGui's LabelText. `selectable(label, bool*)` and `menu_item(label, shortcut, bool*)` take a pointer:
     with `bool&` an lvalue would make them ambiguous with the `bool selected` overloads.
  7. The widgets of U5 to U9 are declared (A4's surface, with `slider<ArithmeticValue>`, `combo` over a random-access
     range of `TextLike` items, `input_text` over `std::string` or a fixed buffer, tables, plots, docking, shortcuts,
     accessibility); each reports `UiFrameProblem::NOT_IMPLEMENTED` once per frame and returns false (or an empty value).
     Outside a frame, `dock_builder()`, `save_layout()` and `load_layout()` throw `UiCapabilityError`
     (`UiSubsystem::WIDGETS`). `UiWindowFlag::MENU_BAR` and `ChildFlag::RESIZE_X/Y` are reported likewise.
  8. No `item_identifier()`: a core `Identifier` is made from a name only (`from_name()`), not from a stored hash;
     `identifier(label)` gives the identifier a label gets (FNV-1a of the parent identifier's bytes and the label: what
     `WidgetIdentifier::from_name()` of those bytes gives, so no core change was needed).
  9. Keyboard navigation in U4 is Tab, Shift+Tab, Space, Return and Escape; the focus highlight and `wants_keyboard()`
     follow keyboard navigation only (a click focuses an item without them, as Dear ImGui does). Arrow keys and gamepads
     arrive with the keymap in U9, as does the high-priority `ui` input context of A11.
  10. Windows resize by their grip only (no edge resizing yet), and the scrollbar tracks end before the grip — at the
      bottom of its track a grab under the grip could not be grabbed (found by a test).
  11. The frame arena (a `std::pmr::monotonic_buffer_resource` over a buffer of `UiContextLimits::frame_arena_bytes`, reset
      every frame) holds long formatted texts and long composed identifiers; the per-frame stacks are vectors reserved at
      construction. Growth beyond a limit is counted (`UiFrameStatistics::allocations`) and reported.
  12. The windowed example's metrics are now a UI window (movable, closable, auto-sized) fed by `gfx.ui.platform`;
      `DebugOverlay` stays in the library.
- **Facts found**: an lvalue `bool` is ambiguous between `f(bool)` and `f(bool&)` overloads; the test-discovery step of
  a GPU test executable failed once in a parallel Release build (rerun: fine); `GlfwEventsTest.KeysCarryThePhysicalKey…`
  read a key code `~` instead of `w` once under load (a dead-key state of the layout seen by `glfwGetKeyName()`;
  unrelated to U4, passes alone).
- **Verified by eye**: a demo window with every U4 widget, rendered on both GPUs at 1 and 1.5 pixels per unit.
- **Tests**: `gfx_ui_context_tests` (37 under synthetic input — themes and styles, windows: fitting, buttons pressed
  only when released over them, collapse, close, move, resize with the cursor, z-order, wheel and scrollbar scrolling,
  child regions, auto-resize and appending; layout: lines, groups, columns, wrapping; widgets; identifiers and
  conflicts; keyboard focus; keys; item queries; double clicks and drags; style stacks, unbalanced and mismatched;
  themes; the report: misuse, missing fonts, dropped issues, later widgets, the diagnostics window; draw data scale and
  order; a render thread leasing draw data during 300 frames — and the demo window drawn on each GPU), the allocation
  test (two windows with every U4 widget, hovered and clicked, allocate nothing over 100 frames after 30),
  `gfx_ui_platform_tests` (the adapter on each platform backend, cursors, and a UI in a visible window on SDL3 and GLFW
  clicked through events pushed to the platform and read back from the swapchain image, validation layers on), and the
  compile-failure tests `UiSlidersRejectBooleans` and `UiCombosNeedTextItems`.

### U5 as built (2026-10-18)

- **Text model** (in `gfx.ui.text`, public, usable without a context): **`TextBoundaries`** (`analyze(text)`; grapheme
  and word boundaries of UTF-8 text by byte offset from libunibreak's `set_graphemebreaks_utf8` /
  `set_wordbreaks_utf8`; `next_grapheme`, `previous_grapheme`, `next_word`, `previous_word` (Control+Right skips the
  current segment then the spaces, Control+Left the spaces then the segment), `word_at`, `grapheme_count`) and
  **`TextEditor`** (`TextEditorDescription{capacity, undo_levels = DEFAULT_UNDO_LEVELS, single_line}`; text, cursor,
  anchor, selection, `move(TextMotion, extend)`, `select_all`, `select_word`, `insert` (clean UTF-8: control characters
  dropped, CR LF one break, line breaks become spaces on one line; capacity cut at a character boundary; overwrite
  mode), `erase(TextMotion)`, `erase_selection`, `undo`/`redo` (typing merges into one step until a word starts after a
  blank, consecutive erasing merges), `revision()`). Enumeration `TextMotion`.
- **Text fields** (`text_input.cpp`; the core `text_field()` is shared with numbers): one field is edited at a time
  (`TextEditState`, holding a `TextEditor`). Editing starts on a click (the cursor at the click, Shift extends, a double
  click selects a word, a drag selects; the I-beam cursor over fields), on Tab into the field or Return on it (everything
  selected); it ends on Return or Tab (committed), Escape (reverted; with ESCAPE_CLEARS_ALL a text is cleared first), a click
  elsewhere (committed) or when the field is no longer submitted. Keys: arrows by graphemes, Control+arrows by words,
  Home/End (Control: the whole text), Up/Down/Page Up/Page Down between lines at a kept column; Backspace/Delete
  (Control: words); Control+A, C, X, V, Z, Y and Shift+Delete, Control+Insert, Shift+Insert. Every `InputTextFlag` works:
  the character filters, ALLOW_TAB_INPUT, ENTER_RETURNS_TRUE, ESCAPE_CLEARS_ALL, CONTROL_ENTER_FOR_NEW_LINE, READ_ONLY,
  PASSWORD (`*` per grapheme, never copied), ALWAYS_OVERWRITE, AUTO_SELECT_ALL, NO_HORIZONTAL_SCROLL, NO_UNDO_REDO,
  ELIDE_LEFT, WORD_WRAP. Single-line fields scroll horizontally to the cursor; `input_text_multiline()` is a framed
  child region that scrolls to it. A `std::string` grows as needed; a `std::span<char>` holds its size minus one bytes,
  NUL-terminated. The cursor blinks (TEXT_CURSOR_BLINK_SECONDS, shown TEXT_CURSOR_VISIBLE_SECONDS after a change).
- **IME**: `UiFrameInput` gained `composition` and `composition_cursor`; while a field is edited, `end_frame()` asks
  `UiPlatformServices::set_text_input(true, cursor rectangle)` (again when the cursor moves) and stops it afterwards;
  `UiContext::wants_text_input()` is true. The field draws the composition underlined **in place of the selection** (where
  committing it puts it), with the cursor inside it. `gfx.ui.platform` turns TEXT_COMPOSITION events into the
  composition and asks for composition events only where the platform supports `PlatformFeature::TEXT_COMPOSITION`
  (GLFW throws `BackendCapabilityError` when asked: it reports none, the system shows them).
- **Numbers** (`numbers.cpp`): `input_number()` (a text field plus - and + buttons repeating while held, Control: the
  fast step), `slider()` and `drag()` over any `ArithmeticValue`, and the multi-component overloads over any contiguous
  range of them (`EditableNumbers`: arrays, spans, vectors, C arrays). Sliders: a grab one step wide for small integer
  ranges, the value follows the mouse, Left/Right step, LOGARITHMIC. Drags: horizontal motion past the drag threshold
  times the speed (Shift ×DRAG_FAST_FACTOR, Alt ×DRAG_SLOW_FACTOR unless NO_SPEED_TWEAKS), the rounding's remainder kept
  for the next motion, clamped (or WRAP_AROUND) when the minimum is below the maximum. Control+click, a double click or
  Return turns a slider or drag into a text field (unless NO_INPUT); its value applies when editing ends by Return,
  Tab or a click (CLAMP_ON_INPUT clamps it). Values are rounded to the format's precision (unless NO_ROUND_TO_FORMAT).
- **Colors** (`colors.cpp`): `color_edit()` (four drags or three without alpha, RGB or HSV, integers 0 to 255 or
  FLOAT_CHANNELS 0 to 1, HDR without an upper bound; or DISPLAY_HEXADECIMAL; the preview opens a picker popup unless
  NO_PICKER), `color_picker()` (PICKER_HUE_BAR: a saturation/value square and a hue bar; PICKER_HUE_WHEEL: a hue ring
  around a hue/black/white triangle, Dear ImGui's barycentric formulas; ALPHA_BAR over a checkerboard; a side preview;
  the inputs under it), `color_button()` (a swatch, translucent ones over a checkerboard, ALPHA_PREVIEW_HALF, a border
  unless NO_BORDER). The hue and saturation are remembered while the color is gray or black. INPUT_HSV colors hold
  hue, saturation and value. In `gfx.ui.draw`: `HsvColor`, constexpr `to_hsv()`, `from_hsv()`, `HexadecimalColor`,
  `to_hexadecimal()`, `from_hexadecimal()`, and `DrawList::add_rectangle_filled_gradient()` / `add_triangle_filled()`
  (a color per corner, no fringe: the insides of widgets).
- **Lists** (`combos.cpp`, `popups.cpp`): `begin_combo()`/`end_combo()` and `combo()` (pressed on the mouse going down;
  the popup is at least as wide as the combo, HEIGHT_SMALL/REGULAR/LARGE/LARGEST show COMBO_SMALL/REGULAR/LARGE_ITEMS or
  all; NO_ARROW_BUTTON, NO_PREVIEW, WIDTH_FIT_PREVIEW; it scrolls to the selected item when it opens),
  `begin_list_box()`/`end_list_box()` and `list_box()` (a framed child region about seven items tall, the label on its
  right). Popups: a stack of windows drawn over every window and hovered first, placed below their opener (above when
  they do not fit), closed by a click outside them (a click on the opener does not reopen it), by Escape, by a choice
  (a selectable in a popup closes it unless NO_AUTO_CLOSE_POPUPS) or when no longer submitted; the window that opened
  the focused popup keeps its title active.
- **Deviations from the design, for your validation**:
  1. The popup machinery is internal (`ContextState::open_popup`, `begin_popup`, `end_popup`, `update_popups`) and
     serves combos and the color picker; the public popup API (`open_popup()`, `begin_popup()`, modals, context menus)
     stays declared for U6, which will build on it.
  2. One template per kind instead of A4's `input_int`, `input_float`, `slider_int`, ...: `input_number()`, `slider()`
     and `drag()` over any arithmetic type. The templates erase the type into a `NumberField`
     (`std::variant<std::int64_t, std::uint64_t, double>` with the bounds and the type's limits) handled by non-template
     cores; arithmetic is done in `long double`, exact for 64-bit integers with GCC (MSVC's `long double` is a `double`:
     integers beyond 2^53 would lose precision there; to revisit in step 6).
  3. **Formats are `std::format` formats** (`"{:.2f} m/s"`), not printf's: the value is shown with the whole format and
     edited as its replacement field alone; the precision (`.N`) rounds the value; a format the type cannot use is
     reported (INVALID_ARGUMENT) and the type's default used (DEFAULT_INTEGER_FORMAT, DEFAULT_FLOAT_FORMAT,
     DEFAULT_DOUBLE_FORMAT). Typed text is parsed with `std::from_chars` (scientific notation accepted, no expressions)
     and clamped to the type.
  4. The multi-component overloads take contiguous ranges (concept `EditableNumbers`), not `std::span<Value>` as
     declared in U4: a span parameter does not deduce from arrays or fixed-extent spans. New compile-failure test
     `UiComponentsNeedWritableNumbers`.
  5. Shortcuts follow the keyboard layout's symbols (`UiKeyEvent::key_code`: Control+Z is the key labeled Z on AZERTY);
     typed text is ignored while Control alone is held (Control+Alt is AltGr).
  6. A frame's input has no order between keys and text: a field applies motions and erasing first, then the typed
     text, then Return, Tab and Escape. Several keys of one frame apply in a fixed order (horizontal motions, then
     vertical ones): one event per frame is the usual case, and the tests send one key group per frame.
  7. Text fields keep `FRAME_BACKGROUND` whether hovered or edited (Dear ImGui's choice); the selection and the cursor
     show the editing.
  8. Logarithmic sliders use the ratio of logarithms when both ends have one sign and are at least 10^-precision from
     zero (Dear ImGui's formula: the middle of 1..1000 is 31.6); ranges that touch or span zero use
     sign(x)·log(1 + |x|/ε) instead of Dear ImGui's zero dead zone.
  9. Not in U5: input-text callbacks (A4: there is no callback API; filters are flags), `selectable_list` (A4; the
     list box takes any items), and the flags that need later phases are accepted and ignored: ColorEditFlag::NO_OPTIONS
     (the options menu needs U6's context menus), NO_TOOLTIP (tooltips, U6), NO_DRAG_DROP (drag and drop, U6).
  10. Two fixes to U4's code found by U5's tests: an item focused by a click after its `item_add()` lost the focus at
      the end of the frame (Tab after a click then wrapped to the first item) — `take_focus()` keeps it alive; and
      while a field is edited, other items stay hoverable and `wants_mouse()` is false unless the field is selecting
      with the mouse, so a click outside commits the edit and reaches the application or the item under it.
  11. After `end_combo()` the combo is the last item again (item queries refer to it, not to its popup's items).
- **Facts found**: a frame's typed text and keys being unordered matters only when one frame carries several (the
  tests now send one group per frame); a click on the same spot within the double-click time is a double click (tests
  wait a second between clicks on one field); GLFW refuses composition events; twice in a full build, a transient
  "Permission denied" (a link, then ninja's log recompaction) did not reproduce on the next build — another process
  probably held a file.
- **Verified by eye** (pictures read back from both GPUs): the input widgets with an open combo popup over the window
  (the picker's square, hue bar and marker, the title active while the popup has the focus), and an edited field with
  its selection, then a composition underlined in its place.
- **Tests**: `gfx_ui_text_tests` +9 (`TextEditor`, `TextBoundaries`: graphemes, words, selection, undo merging, capacity,
  single line, overwrite); `gfx_ui_context_tests` +26 under synthetic input (text fields: typing at the cursor, words,
  selection, clipboard and undo, double click and drag, Return/Escape/click elsewhere, Tab between fields, filters,
  passwords, read-only fields and buffers, multi-line fields, IME composition and text input area, hints and overwrite,
  long texts scrolled, fixed or elided; numbers: parsing and type limits and formats, step buttons, sliders (linear,
  integer, logarithmic, dragged), typed input (Control+click, double click, CLAMP_ON_INPUT, Escape), drags (speed, Shift,
  clamp, wrap), keyboard steps, components; colors: conversions (`static_assert`s and round trips), channel drags and
  the hexadecimal field, the square and hue bar, INPUT_HSV, the wheel and triangle, the picker popup; lists: a combo's
  choice, closing its popup three ways, list boxes) and +2 × each GPU (the widgets with an open combo popup; an edited
  field's selection and composition); `gfx_ui_draw_tests` +1 (gradients and triangles); the allocation test +1 (every
  input widget idle under a moving mouse: no allocation over 100 frames); `gfx_ui_platform_tests` +1 × each backend
  (text input started and stopped on the window, compositions and typed text through `UiPlatformInput`); compile-failure
  `UiComponentsNeedWritableNumbers`. The later-phase report test now uses U6's widgets.

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
