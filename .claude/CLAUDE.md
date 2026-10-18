# gfx — project memory

> **Autonomous task COMPLETE (2026-10-15): Vulkan backend + SPIR-V infrastructure.** What was built,
> the decisions taken without the user, deviations and test results are in
> [vulkan-backend-report.md](vulkan-backend-report.md); the plan with its log is
> [vulkan-backend-plan.md](vulkan-backend-plan.md). **User validation (2026-10-15):** every decision
> accepted as recommended; the shared-hardware-queue limitation to be documented and detected
> (done: `Queue::submit`/`bind_sparse` throw); defect 7 got its detector. Committed by the user (e839407).

This file restores the working context of the gfx project for a new session (Claude Code loads it
automatically). The chronological record, with the evidence behind each decision, is in
[session-history.md](session-history.md).

## 1. Goal

A **C++23 graphics rendering abstraction library** that models Vulkan's object model (instance →
physical device → device → queues → resources → commands → synchronization) and sits *above* concrete
backends (Vulkan, Direct3D 12, Metal, WebGPU). Every backend must implement the whole interface or
throw a highly descriptive error. The API must be expressive enough to build a **render graph** and to
prepare and submit **compute and graphics work in parallel** (it does not ship a render graph).

Original hard requirements (from the user's initial specification):
- C++23 only; concepts, consteval, deducing this, std::expected, std::span, std::mdspan where useful;
  **compile time over run time** everywhere possible; C++ modules preferred.
- **Typed numeric handles**, one tag per resource kind; no opaque pointers, no `void*`, no lookup
  structure inside the handle; backend-native handles never leave the backend.
- snake_case functions, PascalCase types, `m_` member prefix, enums and constants in **one file**.
- Backends that cannot implement something **throw** (`BackendCapabilityError`), never degrade silently.
- **RAII** everywhere (destructor is the release path); deferred destruction allowed (device deleter).
- Multiple queues first-class; per-thread command recording; fences, semaphores, timeline semaphores, events.
- Shaders **compiled from source** by the abstraction; **shader binding layout known at compile time**
  (part of the shader's type), validated at ShaderModule/Pipeline creation with descriptive errors.
- Layout: `<lib_root>/enums.cppm`, `handles.cppm`, `interface/`, `backends/{vulkan,d3d12,metal,null}/`,
  `backend_factory.cppm` (the only public entry to backends).
- Deliverables: architecture & design decisions (done), GitHub repo, CMake with backend options,
  README, LICENSE, code, sample (headless + windowed), and **at the end an architecture overview with
  diagrams describing the code as built**.

## 2. Working process (user rules — follow strictly)

- Work **step by step**. After each step: stop, report, request validation. **The user commits**; never
  commit or push. Provide a suggested commit message.
- **Ask when in doubt**; do not assume. Stop at each step to clarify important questions.
- **Doxygen** documentation on every public interface the user interacts with.
- All code must be **compiled and tested locally** before reporting.
- Use **FetchContent** whenever a dependency is not available (or not usable) on the system.

## 3. Conventions (decided)

| Topic | Decision |
|---|---|
| Names | **Full words, no abbreviations**: `DeviceDescription` (not `DeviceDesc`), `MAXIMUM_` (not `MAX_`), `TRANSFER_SOURCE`/`TRANSFER_DESTINATION`, `minimum_level_of_detail`, no `Info`. **Allowed exceptions** (user approved): project namespace `gfx`, Vulkan format names (`R8G8B8A8_UNORM`), acronyms (GPU, CPU, GLSL, SPIR-V, HDR10, …), GLSL keywords `std140`/`std430`, the `std` module name. File names mandated by the spec stay (`enums.cppm`). |
| Functions | `snake_case` |
| Types, concepts, template parameters | `PascalCase` |
| Enumerators and constants | `SCREAMING_SNAKE_CASE` for both; avoid names platform headers define as macros (`NULL`, `ERROR`, `DELETE`, `OPAQUE`, `MIN`, `MAX`, `INFINITE`, `DOMAIN`, `MemoryBarrier` …). `BackendKind::NULL_BACKEND`, `DebugMessageSeverity::CRITICAL`, `BlendOperation::MINIMUM`. |
| Where enums/constants live | **only** `source/enums.cppm`. "Constants" = standalone named values; per-type traits (`Tag::USABLE_ON`), user shader data (`Shader::LAYOUT`) and backend translation tables are not constants. |
| Member data | `m_` prefix for **class** members only; **plain description structs** (aggregates used with designated initializers) have **no** prefix. |
| Creation structs | `<Thing>Description` (e.g. `BufferDescription`, `UnsupportedCapabilityDescription`). |
| Handles | `template<class Tag> class Handle` (a **class**, never `struct Handle`); aliases `using BufferHandle = Handle<BufferTag>`. Tags are empty `struct`s with `NAME`/`USABLE_ON` traits (open question Q2). |
| `std::variant` | **Allowed and preferred to a C++ `union`** for closed sets of alternatives (user, 2026-10-15: first asked to remove it, then preferred it to the tagged-union alternative). Platform events, `NativeWindowHandle` and the graphics `DescriptorResource` use it. |
| Code style (user's reformat) | Allman braces everywhere (namespace, class, function, if/for/try/catch, even one-line bodies); out-of-class definitions put `auto` on its own line (`auto\nError::backend() const noexcept -> ...`); one parameter per line when several; leading-comma member-initializer lists; short comments describing each step inside function bodies; friends in their own `private:` section before data members; **`case` labels indented one level inside the `switch` braces** (user's reformat of vulkan_commands/resources.cpp and the platform backend_factory.cpp, 2026-10-15). Tests: `TEST(Suite, Name)` then `{` on the next line. |

## 4. Environment (verified)

- Windows 11 25H2; **GCC 16.1** MSYS2 UCRT64 (`C:\msys64\ucrt64\bin`, on the user PATH); **CMake
  4.4.0-rc2** (`C:\Program Files\CMake`); **Ninja 1.13.2**; Python 3.14; git 2.54; no `gh`, no MSVC/clang.
- MSYS2 packages installed (step 0): ninja, vulkan-headers, vulkan-loader, vulkan-validation-layers,
  glslang, spirv-tools, spirv-cross, vulkan-memory-allocator, gtest. vulkaninfo is not packaged.
- GPUs: NVIDIA RTX 5060 Laptop (Vulkan 1.4.341; dedicated compute family 2 and transfer families) and
  Intel UHD (Vulkan 1.4.323; one universal family). Both support timeline semaphores, synchronization2,
  dynamic rendering, descriptor indexing, buffer device address. Validation layer found with
  `VK_ADD_LAYER_PATH=C:\msys64\ucrt64\bin`.
- **Smart App Control** was enforcing (blocked fresh binaries unpredictably, always in %TEMP%); the user
  **turned it off**. Never run built executables from %TEMP%; the scratchpad is fine for sources only.
- Repository: `C:\Users\mathe\Desktop\Coding\gfx`, remote `origin` = https://github.com/bueno-mhc/gfx.git,
  branch `main`. LICENSE: MIT, © 2026 Matheus Henrique Couto Bueno.
- VS Code with ms-vscode.cpptools 1.34 and ms-vscode.cmake-tools 1.24 (no test-adapter extension).

## 5. Architecture and design decisions

- **Object model**: Instance → PhysicalDevice → Device → Queue / resources (DeviceMemory, Buffer, Image,
  ImageView, Sampler) / binding model (ShaderModule, DescriptorSetLayout, PipelineLayout,
  Graphics/ComputePipeline, DescriptorPool, DescriptorSet) / commands (CommandPool, CommandBuffer,
  Recorder<Q>) / sync (Fence, Semaphore, TimelineSemaphore, Event) / QueryPool / Swapchain / Deleter.
  Added beyond the spec list (approved): **DescriptorPool, DeviceMemory**. The Vulkan surface is folded
  into Swapchain creation (native window passed as integers). **No RenderPass/Framebuffer** (legacy; user
  said do not implement them; dynamic rendering + barriers cover render graphs — ask the user if
  something a render graph needs turns out to be missing).
- **Interfaces**: abstract classes owned through `std::unique_ptr<Interface>`; every backend operation is
  **pure virtual with a throwing out-of-class definition** (forced override; the default body throws a
  descriptive BackendCapabilityError; backends can forward to it or throw their own reason).
- **Factory boundary**: backends are separate modules in PRIVATE CMake file sets; only the factory's
  implementation unit imports them; native headers only in backend global module fragments.
- **Backends** (2026-10-15): **Vulkan implemented** (presentation since P5, 2026-10-16); Null = an instance
  without physical devices; D3D12 and Metal are stubs throwing "not implemented yet". **Vulkan presentation
  (P5)**: instance enables VK_KHR_surface + every offered window-system surface extension +
  VK_EXT_swapchain_colorspace; surfaces created in plain TU `vulkan_surface.cpp` (std-free
  `vulkan_surface.hpp`, one function per window system, via vkGetInstanceProcAddr); `WindowSurface` RAII
  in `:instance`; PRESENTATION and new **`DeviceFeature::FRAME_LATENCY_WAIT`** (present_id + present_wait;
  NVIDIA yes, Intel no → wait_for_frame_latency throws) automatic; VK_EXT_hdr_metadata when offered;
  `VulkanSwapchain` declared in `vulkan_swapchain.cpp` (validates family/format/color space/mode/usage
  against the surface listing what it supports; images + views registered in the image tables, not owned,
  not bindless; resize waits device idle, recreates with oldSwapchain; **zero-area window = no
  VkSwapchainKHR**, acquire → OUT_OF_DATE; new `SwapchainStatus::TIMEOUT`); `SwapchainState`/`SwapchainRecord`
  + `HandleTables::swapchains` in `:device`; `VulkanQueue::present` checks family + index, chains
  VkPresentIdKHR. `timeout_nanoseconds()` moved to `:common`.
- **Windowed example + docs (P6, 2026-10-16)**: `examples/windowed/windowed_example.cpp` (option
  `GFX_BUILD_EXAMPLES`; `examples/` added after `tests/` so `GFX_VULKAN_VALIDATION_LAYER_DIRECTORY` is known;
  CTest `WindowedExample.{SDL3,GLFW}` label `example`: `--frames 120 --validation`); square drawn with
  `clear_attachments`; Escape opens the menu (gameplay "pause") and closes it (menu "resume"); F3
  "toggle_metrics" in an always-active "debug" context (priority 100); `--metrics`. SDL3 windows
  emit WINDOW_RESIZED / WINDOW_FRAMEBUFFER_RESIZED only when the size changed (`report_size`,
  `report_framebuffer_size`; SDL reports unchanged sizes when a window is first shown); WindowSwapchain
  ignores framebuffer events equal to its extent. `docs/architecture.md`: 13 Mermaid diagrams (checked by
  rendering with Mermaid 10.9.1 and 11.4.1 from a local http.server page — local files are static snapshots
  in the browser pane). README: platform section + use cases + Examples; snippets compiled in a temporary
  target. `.vscode/launch.json`: "gfx: Windowed example" (asks for the backend).
- **Follow-up after P6 (user, 2026-10-16)**: **`ContextDescription::ignore_held_inputs`** (default true,
  Unreal's "ignore all pressed keys until release"): per binding per source (`PlayerBinding::readable_sources`
  / `ignored_sources` / `readable_frame`; `InputSystemState::update_ignored_sources`, `held_since_earlier_frame`
  = down && !pressed this frame, `can_read_digital`): a digital source that becomes readable (activation,
  unblocked, no longer consumed, text input ended) while held since an earlier frame reads as released until
  released; analog never ignored. **Frame metrics, Debug only** (no-ops in Release, `enabled()`):
  `gfx.interface:frame_metrics` (`FrameMetrics`, `FrameMetricsDescription{averaging_period 500ms}`,
  `FrameMetricsSnapshot`; begin_frame(now)/count(command buffer)/set_gpu_timings/snapshot/text).
  `CommandStatistics::operator+=`. Fake backend: `FakeCommandBuffer::clears()`, `FakeDevice::set_memory_statistics()`.
  The graphics `:debug_overlay` (5x7 bitmap font drawn with clear_attachments) was **deleted in U3**: the overlay is
  now `gfx::ui::DebugOverlay` (SDF text); FrameMetrics tests in tests/graphics/interface/frame_metrics_test.cpp.
- **UI library (U0 as built, 2026-10-17)**: `source/ui/`, target `gfx::ui` (`gfx_ui`, option `GFX_BUILD_UI` ON),
  namespace `gfx::ui`, `import gfx.ui;` (re-exports gfx.core). `gfx.ui.enums` (UiSubsystem {GRAPHICS, FONT,
  INPUT, PLATFORM, ACCESSIBILITY}, BACKEND_LIBRARY "gfx::ui"; FontFormat, FontDecoderKind, SdfKind, AtlasTexelFormat,
  AtlasCompression, AtlasSectionKind (values stored in .uiatlas: stable), AtlasPackerKind, TextDirection;
  UI_ATLAS_MAGIC "GFXUIATL", UI_ATLAS_VERSION_MAJOR/MINOR, UI_ATLAS_ENDIANNESS_TAG, UI_ATLAS_ALIGNMENT 8,
  UI_ATLAS_HEADER_SIZE 64), `gfx.ui.handles` (8 Handle kinds, 11 Identifier kinds, `GlyphIndex`, concepts UiHandle/
  FontResourceHandle/UiIdentifier, literals `_window` `_panel` `_dock_space` `_tab` `_menu` `_popup` `_table`),
  `gfx.ui.errors`: **`using UiCapabilityError = BackendCapabilityError;`** (core class is `final`, core unchanged)
  + `throw_unsupported(UiSubsystem, operation, capability, reason, suggestion)` → "[gfx::ui::FONT] ...".
  Font pipeline helpers in GfxDependencies.cmake (all static, `gfx::` targets): brotli decoder 1.2.0, FreeType
  2.14.3 (WOFF via its own zlib — `CMAKE_DISABLE_FIND_PACKAGE_ZLIB`; WOFF2 via preset `BROTLIDEC_*` cache vars;
  alias `Freetype::Freetype` for msdfgen), msdfgen 1.13 + msdf-atlas-gen 1.4, HarfBuzz 14.2.1 + subset (requires
  FreeType first: HarfBuzz enables hb-ft whenever a `freetype` target exists; CMake build warns "community-
  maintained"; `-Wno-stringop-overread` on its targets for GCC: hb_memcmp false positive in Release), zstd 1.5.7 (`SOURCE_SUBDIR build/cmake`), stb_truetype 1.26 — **always fetched** (atlas bytes;
  HarfBuzz/zstd pinning is a refinement of answer 10 to validate); SheenBidi 3.0.0 and libunibreak 7.0 found
  first, else fetched (none installed here: find branches untested). `gfx_require_ui_test_fonts()`: Noto Sans,
  Arabic, Hebrew (variable, google/fonts 47d54ca1) into `<build>/ui-test-fonts`. Facts: Noto Sans Arabic shapes
  dotted letters as dotless body + dot glyph (test joining with seen/meem); msdf-atlas-gen output is identical
  with 1 and 4 threads.
- **UI atlases (U1 as built, 2026-10-17)** — details in docs/ui-design.md §B "U1 as built", format in
  docs/ui-atlas-format.md. `gfx.ui.atlas_format` (in gfx.ui): `atlas_layout` (record offsets as static constexpr members,
  constexpr little-endian load/store), constexpr `xxhash64`, `font_tag`, records, **`AtlasView::parse(blob,
  AtlasVerification)`** → `std::expected<AtlasView, AtlasReadError>` (validates everything, usable in constant
  expressions; `parse(std::vector&&)` deleted: views of temporaries dangle — a test bug found that way). Build-time
  **`gfx::ui_atlas_tools`** (option `GFX_BUILD_UI_ATLAS_TOOLS`): FontSource, CharacterSelection, GlyphOutline/
  OutlineBuilder, FontDecoder/FontFace (FreeType, stb_truetype), SdfGenerator (msdfgen; BUILT_IN = SDF only, curves
  flattened at 0.01 texel, union-boundary pieces only so overlapping contours have no inner edges), AtlasPacker
  (TIGHT = msdf-atlas-gen RectanglePacker, GRID), `write_atlas(AtlasContent)`, `AtlasBuilder` (use_decoder/
  use_generator/use_packer) / `build_atlas()`. Pipeline: hb-subset (characters + closure, `.notdef` outline, no
  hinting, every axis pinned → static instance) → reopen the subset with the same decoder (glyph ids = shaping font's)
  → outlines on the calling thread → boxes/fields on jthreads → pack → quantize → pair kerning/ligatures by shaping
  every pair of Latin/Greek/Cyrillic/Common characters (no locale) → write. Tool `gfx_ui_atlas_builder` (options table,
  depfile, `--embed-module`); **`gfx_add_ui_atlas()`** in `cmake/GfxUiAtlas.cmake` (configure-time checks, EMBED →
  module exporting `inline constexpr std::array<std::byte, N> <NAME>` via `#embed` + `-Wno-c++26-extensions` on that
  file, or BYTE_LIST). Test fonts + Source Sans 3 (adobe-fonts/source-sans release 87b37a2d: OTF, VF CFF2, WOFF,
  WOFF2). Gotchas: CMake regexes have no `{m,n}`; hb-subset drops the `.notdef` outline without
  HB_SUBSET_FLAGS_NOTDEF_OUTLINE; `pin_all_axes_to_default` resets earlier pins (call it first); harfbuzz-subset.a
  repeats harfbuzz.a symbols (allow-multiple-definition); stb truncates CFF coordinates, FreeType rounds; decoders may
  start contours elsewhere (MSDF colors differ, true distances within 2/255); `GfxRuntimeDeployment` list is now a
  cache variable (include guard is global). Preview helper (scratchpad, not in repo): stdlib Python 3.14 script with
  `compression.zstd` decoding pages to PNG.
- **UI text (U2 as built, 2026-10-17)** — docs/ui-design.md §B "U2 as built". `gfx.ui.unicode` (constexpr UTF-8),
  `atlas_covers()`/`first_missing_character()` (constexpr, in atlas_format), `gfx.ui.text` (source/ui/text/):
  `FontAtlas` (load(span) in place / load(vector&&) / load_file; pages + shaping font decompressed once, CPU only —
  images are U3's), `FontLibrary` (FontAtlasHandle/FontHandle via core HandleTable in the .cpp; FontDescription{atlas,
  fallbacks, size, language, shaping}), `TextLayoutEngine::layout(text, font, TextLayoutDescription) noexcept` →
  TextLayout view (valid until new_frame()); `caret_position`, `hit_test`. Internal partition `:engine`: BidiParagraph
  (SheenBidi, UTF-32, levels + scripts + visual line runs), find_line_breaks/find_grapheme_breaks (libunibreak UTF-32,
  call_once init), HarfBuzzShaper (one hb_font per atlas keeps the atlas alive; reused buffer; monotone graphemes),
  shape_basic (cmap, longest ligature, pair kerning). Engine works on uint32 code points + byte offset table. Atlas per
  char: first in chain; Common/Inherited stick to the previous atlas. AUTOMATIC shaping: basic for LTR
  Latin/Greek/Cyrillic/Common runs without marks. Cache: hash buckets + intrusive probation/retained lists (follow-up
  after U6), entries used this frame never evicted (overflow deque, storage kept → CACHE_FULL). HarfBuzz is now configured before FreeType (no hb-ft in apps). Tests use atlases
  from gfx_add_ui_atlas (text_latin, text_latin_basic NO_SHAPING_FONT, text_hebrew, text_arabic; module
  gfx.tests.text_support); allocation test replaces operator new. Visual check: CPU MTSDF render of a layout (temporary
  test, PGM → PNG with a stdlib script in the scratchpad).
- **UI rendering (U3 as built, 2026-10-17)** — docs/ui-design.md §B "U3 as built" (6 deviations for validation, incl.
  gfx_ui now links graphics PUBLIC). `gfx.ui.draw` (source/ui/draw/: Color (sRGB-encoded, not premultiplied, packed()
  red low byte), UiRectangle, Transform2D (rotation clockwise on screen, about(), right-to-left composition),
  DrawTexture, UiVertex (48 bytes, static_assert), DrawCommand, TextStyle, DrawList, UiDrawData{lists span of const
  DrawList* const, width, height}), `gfx.ui.debug_overlay` (DebugOverlay: draw(list, engine, fonts, font, lines) →
  panel rect; app provides the font), `gfx.ui.renderer` (source/ui/render/: UiRenderer(device, fonts,
  UiRendererDescription{color_format, samples, frames_in_flight, geometry_bytes_per_frame 8 MiB, debug_name});
  begin_frame(frame) / prepare(GraphicsRecorder&, data) before the pass (lazy page upload, staging released after
  frames_in_flight) / record(RenderingCommands&, data) / statistics(); pimpl State). Shaders ui.vert/ui.frag via
  gfx_add_shaders (module gfx.ui.shaders): vertex pulling from a bindless storage buffer (12 uint words per vertex),
  shader mode per vertex (UiShaderMode SOLID/SHAPE/GLYPH/GLYPH_SHADOW/IMAGE in bits 0-7, SdfKind bits 8-15), commands
  split only on clip or texture change (shapes join any command), 32-bit indices, push constants {scale, translation,
  vertex buffer, vertex word, texture, sampler, flags (bit 0 sRGB → colors converted to linear)}. Shape edges: analytic
  rounded-box gradient mapped through dFdx/dFdy(local) (finite differences of the distance gave √2 at corners on
  Intel → 218 instead of 255). Polyline fringe 1 px centered on the edge (ImGui convention; thinner fades). Enumerations
  DrawTextureKind, UiShaderMode (`TEXT` is a windows.h macro → GLYPH). Tests: gfx_ui_draw_tests (draw_list_test.cpp
  18 CPU; renderer_test.cpp 17 × every GPU: fixture derives VulkanDeviceTest + WithParamInterface<size_t>, whose
  `physical_device()` is now virtual; text compared with ui.frag's math evaluated on the CPU from the list's vertices +
  bilinear page sampling; `GFX_TEST_PICTURE_DIRECTORY` dumps every picture as PPM — ppm_to_png.py in the scratchpad);
  allocation test for a refilled DrawList; presentation `TheUiIsDrawnIntoTheWindow` (GFX_TEST_WITH_UI); windowed example
  overlay via UI (GFX_EXAMPLE_WITH_UI, atlas `windowed.fonts` overlay_font, Noto Sans MTSDF 32/4). Coverage sampled
  at pixel centers along a 45° edge overestimates by ~2% (CPU = GPU). **CMake gotcha**: file-scope `set()` in a module
  with `include_guard(GLOBAL)` is visible only to the first including directory → put lists inside functions or use
  CACHE INTERNAL (fixed GfxUiAtlas, GfxShaders, GfxDependencies' GFX_VULKAN_SDK_TAG, GfxRuntimeDeployment).
- **UI immediate mode (U4 as built, 2026-10-18)** — docs/ui-design.md §B "U4 as built" (12 deviations for
  validation). Modules `gfx.ui.style` (Style + scaled(), Theme {name, style, colors per StyleColor}, constexpr
  builtin_theme(DARK/LIGHT/CLASSIC = Dear ImGui colors, HIGH_CONTRAST), style_float/style_vector),
  `gfx.ui.input` (UiFrameInput: time, display_size (UI units), pixels_per_unit, focused, mouse position/button
  events/held mask/wheel, key events, modifiers, text; UiPlatformServices + UiNullPlatformServices),
  `gfx.ui.context` (partitions :types, :frame (UiFrame: every widget; later phases declared → NOT_IMPLEMENTED once per
  frame per operation), :context (UiContext constructed directly, owns FontLibrary, load_font(atlas, size),
  begin_frame/end_frame → UiFrameReport, draw_data(), acquire_draw_data() → UiDrawDataLease; dock_builder/save/load
  throw UiCapabilityError WIDGETS); internal :state (ContextState, WindowState, IdentifierMap/IdentifierSet open
  addressing, CountingResource arena upstream; every member brace-initialized — defect 7); units context.cpp,
  windows.cpp, layout.cpp, widgets.cpp, style.cpp, queries.cpp, later.cpp), `gfx.ui.platform` (target
  gfx::ui_platform in source/ui/platform: UiPlatformInput (window coords × density / content scale = units),
  UiWindowServices). **UI units = points**: UiDrawData::scale + DrawList::set_pixels_per_unit; renderer multiplies.
  Three DrawDataSlots rotate (window lists swapped in, never reallocated after warm-up; leases = atomic counters with
  wait/notify). Windows: first frame measured not drawn (auto-fit), hover from last frame's rects (`hit` = rect in
  parent clip), move by any empty space, grip resize (scrollbar tracks end before the grip), wheel to the hovered window
  or first scrollable parent. Identifiers: FNV-1a(seed bytes + label) == WidgetIdentifier::from_name(bytes); "###"
  restarts. Focus: Tab/Shift+Tab/Space/Return/Escape; highlight + wants_keyboard only after keyboard navigation.
  Enumerations: StyleColor (TEXT → **TEXT_NORMAL**, windows.h macro), StyleVariable, ThemeKind, UiWindowFlag, ChildFlag,
  ButtonFlag, SelectableFlag, UiCondition, ArrowDirection, Hovered/FocusedFlag, UiFrameProblem + flags of the declared
  widgets; UiSubsystem::WIDGETS; generated by a spec script (scratchpad ui_enums_u4.py). Overload gotcha: f(bool) vs
  f(bool&) is ambiguous for lvalues → selectable/menu_item take bool*. Tests: gfx_ui_context_tests (UiHarness in module
  gfx.tests.ui_context_support: synthetic input, frame(body), click(); 37 CPU + demo drawn on each GPU, PPM dump via
  GFX_TEST_PICTURE_DIRECTORY), allocation test (0 allocations, 100 frames after 30), gfx_ui_platform_tests (adapter
  ×2 backends, cursors, visible-window smoke test: click through Platform::push_event, swapchain readback),
  compile-failure UiSlidersRejectBooleans, UiCombosNeedTextItems. Windowed example: metrics in a UI window
  ("Frame metrics", closable, ALWAYS_AUTO_RESIZE), example links gfx::ui_platform.
- **UI input widgets (U5 as built, 2026-10-18)** — docs/ui-design.md §B "U5 as built". Public text model in gfx.ui.text:
  `TextBoundaries` (libunibreak grapheme/word breaks per byte, `next/previous_grapheme/word`, `word_at`) and `TextEditor`
  (TextEditorDescription{capacity, undo_levels, single_line}; motions (enum TextMotion), insert (clean UTF-8, capacity,
  overwrite), erase, undo/redo merged by words; a paste is its own step via `set_cursor(cursor, true)`). Context units
  added: `text_input.cpp` (`text_field()` core declared in `:state` with TextFieldRequest/Result, shared with numbers;
  one TextEditState; DisplayMap for password masks and the IME composition shown **in place of the selection**;
  per frame: motions/erasing, then typed text, then Return/Tab/Escape), `numbers.cpp` (NumberField =
  variant<int64, uint64, double> + bounds + type limits, `long double` arithmetic; std::format formats, replacement field
  edited, `.N` precision rounds, from_chars parsing; sliders (log = ratio of logs for one-sign ranges, symlog otherwise),
  drags (threshold, remainder, Shift/Alt, clamp/wrap), typed input via `temp_input_id`), `colors.cpp` (color_edit /
  color_picker hue bar or wheel+triangle / color_button; saved hue+saturation for grays; INPUT_HSV via edit_hsv()),
  `combos.cpp` (combo popup, list box = group + FRAME_STYLE child; label kept in `list_box_labels`), `popups.cpp`
  (internal popup stack: hovered first, published after windows, closed by click outside / Escape / choice / not
  submitted; `popups_closed_by_click` stops a re-open; `title_focus_window()`). `UiFrameInput::composition(_cursor)`;
  `UiWindowServices` asks composition events only if `PlatformFeature::TEXT_COMPOSITION` (GLFW throws otherwise);
  `ContextState::take_focus()` keeps a click-focused item alive (fixed a U4 bug: Tab after a click wrapped); items stay
  hoverable and `wants_mouse()` false while a field is edited. Concept `EditableNumbers` (multi-component overloads take
  contiguous ranges). Draw: HsvColor/to_hsv/from_hsv/to_hexadecimal/from_hexadecimal constexpr,
  add_rectangle_filled_gradient, add_triangle_filled. Test harness: `UiHarness::shortcut(letter)`, `compose(text, cursor)`;
  tests must send one key group per frame and wait 1 s between clicks on one spot (double click). Tests:
  tests/ui/input_widgets_test.cpp (in gfx_ui_context_tests), text_editor_test.cpp, render tests, allocation test, IME
  platform test, compile-failure UiComponentsNeedWritableNumbers.
- **UI structure widgets (U6 as built, 2026-10-18)** — docs/ui-design.md §B "U6 as built". Units trees.cpp (open states in
  `IdentifierMap open_states`, remember_open/remembered_open; tree_stack of window indices; body toggles on release, arrow
  on press; drag_hold_opens()), tabs.cpp (TabBarState/TabState in `tab_bars` + `tab_bar_lookup`; layout from last frame's
  widths, leading/trailing sections, shrink by bisection to a common cap ≥ MINIMUM_TAB_WIDTH or scroll with arrows; press
  → next_selected (applied next frame); bar id pushed on the id stack between begin/end_tab_bar so tab ids = widget_id(label);
  pruning by `last_frame < previous_frame`; `appearing` true on a new bar's first frame), menus.cpp (MenuBarState: bar items
  laid out horizontally with item_add only; window clip swapped to the bar and drawn with `push_clip_rectangle(clip, false)`;
  WindowState menu_bar_height + menu_columns/next_menu_columns; main menu bar = window "##main_menu_bar" found by id;
  submenu hover with Dear ImGui's moving-toward triangle; PopupState menu/submenu/bar_window/opener_item/focus_first),
  popups.cpp (public API; current_popup_level(), top_modal_level(), close_current_popup() chain, begin_modal_popup()
  centered on its first two frames; update_popups() returns whether Escape closed one, never closes the top modal or below,
  Left closes a focused submenu, focus back to opener_item), tooltips.cpp (window "##tooltip", `WindowState::tooltip`,
  published after popups; item tooltips keyed by id or rectangle), drag_drop.cpp (DragDropState; smallest target wins;
  delivery to last frame's winner on release; drag cleared in begin_frame after delivery or elapsed). Frame loop: hover
  ignores everything under the top modal; clicks outside a modal keep focus; Up/Down = focus steps in non-modal popups;
  modal dim list published before the top modal; wants_mouse while popups/drags. Colors: swatches drag COLOR_PAYLOAD_TYPE
  and show tooltips; editors accept drops; right-click options (`ContextState::color_options`). **MouseButton order is LEFT,
  MIDDLE, RIGHT** (ContextState::mouse index = to_underlying). (A text changing every frame no longer allocates: next item.)
- **Follow-up after U6: cache misses without allocations (2026-10-18; probation/retained policy accepted by the user)** — docs/ui-design.md §B
  "Follow-up after U6". Cause measured with gdb: a miss filled a **free** cache entry (empty text/glyph/line storage);
  1024 entries → a counter text took a new entry each frame. text_layout.cpp: lists `probation` (used in one frame only)
  and `retained` (used again in a later frame, `Entry::retained`); acquire() = oldest probation entry last used
  ≥ `PROBATION_FRAMES` (2) frames ago, else a free entry, else LRU of both lists not used this frame, else overflow
  (`overflow_used`, storage kept; clear() keeps storage). **Bug fixed**: unlink() swapped the list heads (hit on the newest
  → self-loop, wrong evictions, spurious CACHE_FULL). bidi.cpp: no R/AL/AN/format character and not forced RTL → levels 0,
  no SBAlgorithm/SBParagraph/SBLine (one level-0 run per line; verified identical to SheenBidi on 99 002 paragraphs, check
  removed); SBScriptLocator created once. Remaining allocations only when a text is longer than earlier ones (scratch
  vectors grow by exact size; draw lists of 3 slots). U6 structure test now `"Popup at frame {:03}"`.
- **UI tables and visualization (U7 as built, 2026-10-18)** — docs/ui-design.md §B "U7 as built". Units tables.cpp
  (TableState/TableColumnState in `tables` + `table_lookup`, `table_stack`; lazy `layout_table()` at the first row/cell:
  defaults, widths (fixed: request or last frame's content; stretched: weights; floors + remainder), places, the borders'
  mouse (before the cells), the draw channels; a cell sets the window's cursor, indent, clip, item width and
  `WindowState::table` (so `content_right()` is the column's); `end_row()` draws backgrounds + top border in channel 0;
  end_table() merges the splitter, measures `auto_width`, restores the host (non-scrolling) or ends the child "##table"
  (scrolling), draws the outer borders and the context menu popup), clipper.cpp (UiListClipper over
  ContextState::clipper_position/seek/visible), visuals.cpp; child RESIZE_X/Y in windows.cpp (`WindowState::resized_size`);
  `LastItem::clip` (is_item_hovered() uses the item's clip); `DrawListSplitter` (channel 0 = the list's own buffers; merge
  joins continuing commands). A table's first frame is measured, not drawn (cell clips empty). API added beyond the design:
  UiListClipper, table queries, TableBackgroundTarget/TableColumnStatus. Gotchas: GCC -O3 false -Warray-bounds on
  ranges::sort of a small std::array span; text cache entries recycled for longer texts allocate (follow-up task proposed;
  the U7 allocation test keeps the mouse off the plots).
- **Bridge `gfx.platform.graphics`** (P5; `source/platform_graphics/`, target `gfx::platform_graphics`,
  re-exports gfx + gfx.platform, built with the platform library): `swapchain_description_for(window, base)`,
  constexpr `requires_swapchain_recreation(event, window)` / `(status)`, **`WindowSwapchain`** (owns swapchain
  + semaphores: acquisition semaphores image_count + 1 recycled when an image comes back, rendering
  semaphores one per image; acquire() → nullopt when minimized/zero area/timeout; recreates on framebuffer
  resize, SUBOPTIMAL/OUT_OF_DATE (retry once), new swapchain on SURFACE_LOST; `recreation_count()`).
- **Backend factory** (`gfx.backend_factory`, re-exported by `gfx`): `backend_availability()`,
  `available_backends()`, `create_instance(BackendKind, InstanceDescription)`; each backend module
  exports `unavailable_reason()` + `create_instance()`; CMake options `GFX_BACKEND_{VULKAN,NULL,
  DIRECT3D12,METAL}` → `GFX_WITH_<NAME>_BACKEND`.
- **Handle tables** (in core since P0): module `gfx.core.slot_table` (std only): `SlotTable` lock-free
  lookups, mutex on insert/remove, chunks of 4096, FIFO reuse past a threshold; `gfx.core.handle_table`:
  `HandleTable<Tag, Record>` (private HandleIssuer, constructed with a `BackendIdentity`) find/get
  (InvalidHandleError)/insert/remove/for_each. Not in the `gfx.core` umbrella (backend utilities).
- **Core library (P0 of the platform work, 2026-10-15)**: `source/core/`, target `gfx::core` (`gfx_core`),
  namespace `gfx`, modules `gfx.core` (umbrella) = `gfx.core.enums` (machinery: EnumeratorName,
  EnumerationTraits, Described/Bitmask/Sequential/**Backend**Enumeration concepts, ENUMERATOR_COUNT,
  bitmask operators, to_string, **from_string** (sequential + bitmask "A | B"/NONE/0x..), formatter,
  exported `gfx::enumeration_verification::verify` used by every library's enums file; shared
  enumerations ValidationLevel, MemoryDomain, NativeWindowKind, and since U4 (user decision 2026-10-18)
  **Key, KeyCode (+ KEY_CODE_NON_PRINTABLE_FLAG), KeyModifier, MouseButton(Mask), CursorShape** (namespace gfx;
  gfx.platform.enums re-names them with using-declarations); constants HANDLE_*, NAME_HASH_*,
  DEBUG_BUILD, DEFAULT_VALIDATION_LEVEL, WAIT_INDEFINITELY; `GFX_DEBUG_BUILD` is a PUBLIC definition of
  gfx_core), `gfx.core.errors` (Error with **BackendIdentity** {library, name} — implicit from any
  BackendEnumeration (traits give `BACKEND_LIBRARY`: graphics "gfx"), `== BackendKind::X` works, message
  origin "[gfx::VULKAN]"/"[gfx]"; BackendCapabilityError, InvalidUsageError, InvalidHandleError,
  OutOfMemoryError, BackendApiError; protected helpers `suggestion_line`, `indented`), `gfx.core.handles`
  (HandleKey, HandleIssuer, Handle<Tag> (Tag needs NAME), concept TaggedHandle, **Identifier<Tag>**:
  consteval FNV-1a `name_hash`, `from_name` at run time, formatter "Action(0x1A2B3C4D)", concept
  TaggedIdentifier), `gfx.core.types` (Version, Extent, Offset, Rectangle, Vector2),
  `gfx.core.native_window` (**NativeWindowHandle**: std::variant<monostate, Win32/Xlib/Xcb/Wayland/
  Cocoa(+metal_layer)/WebCanvas handles>, integers only, kind() from the index). Graphics keeps its
  module names: `gfx.enums` / `gfx.handles` / `gfx.errors` re-export the core modules (graphics errors
  left: DeviceLostError, ShaderCompilationError, ShaderLayoutMismatchError); `gfx` re-exports
  `gfx.core`; `SwapchainDescription::window` and `surface_support()` take NativeWindowHandle (graphics
  `NativeWindow` removed); target alias `gfx::graphics`.
- **Vulkan backend as built** (`source/graphics/backends/vulkan/`, module `gfx.backends.vulkan`): partitions
  `:common` (result names, check/throw), `:translation` (to_vulkan/from_vulkan for every enum),
  `:instance` (VulkanInstance, VulkanPhysicalDevice, PhysicalDeviceCapabilities), `:device` (records,
  HandleTables, NativeQueue, VulkanQueue, VulkanDeleter, BindlessTable, VulkanDevice), `:pipelines`
  (set/pipeline layouts); implementation units per area (device, queue, bindless, layouts,
  synchronization, resources, pipelines, commands, diagnostics); classes only one unit needs are
  declared in that unit (defect 7). volk (fetched, `vulkan-sdk-1.4.350.0`; per-device VolkDeviceTable;
  debug-utils through the global pointers); VMA 3.4 (plain TU `vma_implementation.cpp`); Vulkan 1.3
  required; cost-free features enabled automatically, RT/ray query/mesh/VRS/conservative/sparse/
  GPU_CRASH_MARKERS (NV checkpoints only) on request. Several gfx queues may share one native queue
  (mutex + progress timeline per NativeQueue; deleter keyed on each native queue's submitted value).
  Barriers: stages masked to the device and the queue type, accesses filtered to their stages.
  Records hold `PipelineState` (atomic VkPipeline + status: async compile, hot reload) and
  `CommandBufferStatus` (a stale handle during recording poisons the buffer; submit refuses it).
  CPU-written memory (HOST_UPLOAD, DEVICE_LOCAL_HOST_VISIBLE) is required HOST_COHERENT.
  RenderDoc in-application API in a plain TU (`renderdoc_capture.cpp`, `<windows.h>`); device loss →
  DeviceLostError with the breadcrumb label chain + `VK_EXT_device_fault` report file.
  **Shared hardware queues** (user: document + detect + throw): on a GPU with one queue (Intel UHD)
  every queue type shares it, so a submission waiting for a value only a later submission to the same
  hardware queue signals would deadlock. Detection (≥ ValidationLevel::CREATION): `TimelineSignals`
  (owned by each timeline semaphore, pointer in its SemaphoreRecord: largest value submitted batches
  promise) + `NativeQueue::unresolved_waits` (waits nothing had signaled or promised when submitted;
  pruned when reached). `VulkanQueue::order_timeline_batches()` simulates the batches (waits before
  signals) and refuses a signal of **exactly** the value of an unresolved wait (the certain case: a
  greater value is accepted, a smaller one from elsewhere may release the wait) — InvalidUsageError
  for the same gfx queue, BackendCapabilityError across queues sharing hardware;
  `record_timeline_batches()` after the submission. Documented on `Queue` (Doxygen) and in README.
- **SPIR-V tools** (`gfx.shader_tools`, `source/shaders/`): `reflect_spirv()` (SPIRV-Cross C API),
  `compile_shader()` / `shader_dependencies()` (glslang C API, Debug only; Release does not link
  glslang). `gfx_add_shaders()` (`cmake/GfxShaders.cmake` + `scripts/GfxEmbedSpirv.cmake`):
  glslangValidator, depfiles, `-g` in Debug, generated module with `<name>_spirv` arrays and
  `ShaderModuleDescription` constants (Debug adds source path, defines, include directories). HLSL
  through glslang (DXC deferred).
- **Platform library (P1 as built, 2026-10-15)**: `source/platform/`, target `gfx::platform`
  (`gfx_platform`, option `GFX_BUILD_PLATFORM` ON), namespace `gfx::platform`, `import gfx.platform;`
  (re-exports gfx.core). Modules: `gfx.platform.enums` (generated tables, moved to core in U4: `Key` = USB HID usages, sparse
  enumeration; `KeyCode` = code points / `KEY_CODE_NON_PRINTABLE_FLAG | HID`; PlatformBackendKind {SDL3,
  GLFW, WINDOWS, XCB, COCOA, WEB} with BACKEND_LIBRARY "gfx::platform"; PlatformFeature, WindowMode,
  WindowFlag, SurfaceApi, CursorShape (`I_BEAM`: TEXT is a windows.h macro), CursorMode, MouseButton(+Mask),
  KeyModifier (SHIFT/CONTROL/ALT/SUPER combined), GamepadButton/Axis/Type, HatPosition, TouchDeviceType,
  EventType, EventWaitMode; constants; `using gfx::operator|` etc. so ADL finds the bitmask operators),
  `gfx.platform.handles` (Window/Monitor/Cursor/Gamepad/Joystick/TouchDevice handles,
  ActionIdentifier/BindingContextIdentifier, PlayerIdentifier slot, literals `_action`/`_context`),
  `gfx.platform.interface` partitions `:platform_object` (PlatformObject : HandleIssuer, mirrors
  BackendObject), `:event` (payload structs with static `TYPES`, `EventPayloadVariant` = std::variant,
  consteval `event_payload_index`, `EventPayload<TYPE>`, `Event::make<TYPE>` / get_if / as / visit /
  text / with_text, ≤ 64 bytes trivially copyable; `CodePoints`/`code_points()`/`is_valid_utf8()`;
  `EventSpan`, `EventQueue` reused per pump with a text buffer), `:image` (ImageData RGBA/PNG,
  `decode_image()` with stb_image 2.30 fetched by URL + SHA-256, PNG only, `STBI_NO_STDIO` in the GMF),
  `:monitor`, `:cursor`, `:window` (thread-safe size/framebuffer/scale/minimized/native handle via atomics;
  backends call update_*; `validate_window_description()`), `:input_devices`, `:platform` (Platform:
  pump = thread check → deferred releases → pushed events → backend `pump(mode, timeout)` → events pushed
  while waiting → drop events of dead windows; watchers called during the pump (RAII `EventWatcher`, weak
  lifetime token, removal during dispatch deferred, re-entrant pump refused); `push_event`/`wake` from any
  thread; `defer_release(HandleKey, fn)` for backends; protected `emit`, `require_platform_thread`,
  `release_deferred`), `gfx.platform.backend_factory` (no backend built yet: every kind reports why).
  Tests: tests/platform (fake platform module + implementation unit; 31 tests) and 4 compile-failure tests.
- **Input system (P2 as built, 2026-10-15)**: module `gfx.platform.input` (`source/platform/input/`,
  re-exported by `gfx.platform`), partitions `:binding` (InputSource {InputSourceKind, code}; `Binding`
  {BindingKind BUTTON/CHORD/GAMEPAD_CHORD/MOUSE_AXIS/COMPOSITE_AXIS/COMPOSITE_2D/STICK, 4 sources,
  modifiers + exact, dead zone, curve, invert, scale, swizzle, normalize, trigger DOWN/HOLD/TAP + time};
  constexpr builders `single`, `chord`, `gamepad_chord`, `composite_axis`, `composite_2d` (normalized),
  `stick` (y flipped: up positive), fluent `with_deadzone`/`with_curve`/`inverted`/`scaled`/`swizzled`/
  `normalized`/`held_for`/`tapped_within`; `bind(action, binding|input)` → ContextBinding), `:schema`
  (ActionDefinition/ContextDefinition hold **names**, identifier() hashes them; ContextDescription
  {priority, consumes_input = true, blocks_lower_contexts, reads_keyboard_during_text_input};
  consteval `action()`, `actions()`, `context()`, `make_input_schema()` → `InputSchema<A, C, B>` (converts
  to `InputSchemaView` of spans); `check_binding()`, `check_input_schema()` → InputSchemaCheck (indices),
  `describe_input_schema_problem()`, `validate_input_schema()`; compile errors are calls to non-constexpr
  `input_schema_error_<problem>()` functions — GCC prints the function name and the schema line, not
  argument values), `:frame` (InputFrame: is_action_pressed / was_action_just_pressed /
  was_action_just_released / get_action_value / get_action_value_2d / action_state (pressed_duration),
  player last (default 0), raw keys/key codes/modifiers/mouse/per-player GamepadState,
  captured_binding(); unknown action or player → InvalidUsageError; non-exported ActionTable shared by
  frames), `:event_channel` (SPSC slots with one atomic flag each, overflow kept on the producer side,
  `send()` every pump), `:input_system` (InputSystem: constructed directly (no factory); `update(events[,
  now])` → double-buffered frame; `activate_context(context, player)` → RAII ContextActivation (weak
  lifetime token); add_context; assign_keyboard_and_mouse / assign_gamepad (AUTOMATIC on first event of
  a gamepad) ; set_text_input_active; bindings / rebind(BindingSlot) / add_binding / remove_binding /
  reset_bindings / find_bindings_using; capture_next_binding (captured press does not reach actions);
  save_bindings / load_bindings (all or nothing, every problem with its JSON path); pimpl
  InputSystemState in input_system.cpp). Evaluation: sticky presses (down or pressed during the frame),
  contexts by priority then latest activation, per-source consumption, blocking contexts, held chords
  hide their key from single-key bindings (Ctrl+S ≠ S), largest magnitude wins, BUTTON pressed from
  actuation_threshold, axes pressed when non-zero, focus lost releases keys and buttons. Saved format
  `{"format": "gfx.input.bindings", "version": 1, "players": [{"player", "bindings": [{"context",
  "action", "bindings": [{"kind", "sources": [{"KEY": "SPACE"}], ...non-default fields}]}]}]}`, only lists
  differing from defaults, unnamed key codes "U+20AC", unknown fields are errors. JSON parsing: nlohmann
  3.12 (`gfx_require_nlohmann_json()`: find_package else single header by URL + SHA-256) SAX parser in
  plain TU `json_reader.cpp` behind std-free `json_reader.hpp` (its headers cannot share a module unit
  with `import std`); writer in binding_storage.cpp. Enums added: ActionValueType, InputSourceKind,
  BindingKind, DeadzoneKind, ResponseCurve, TriggerKind, MouseAxis, GamepadAssignment, InputSchemaProblem
  (incl. MISSING_NAME, INVALID_TRIGGER_TIME, INVALID_RESPONSE_CURVE), BindingCaptureSource; constants
  DEFAULT_ACTUATION_THRESHOLD, MAXIMUM_BINDING_SOURCES, BINDING_STORAGE_VERSION. Deviations from the
  approved snippet C6 are listed in docs/platform-design.md A8 "As built". Tests: input_schema_test,
  input_system_test, binding_storage_test (every input name round trips), event_channel_test (real
  threads), test module `gfx.tests.input_support` (Events builder, GamepadIssuer : HandleIssuer);
  compile-failure tests InputSchemaRejects{DuplicateActions, UndeclaredActions,
  TwoDimensionalBindingsOnButtons, TriggersOnAxes}, CompositesNeedDigitalInputs.
  Gotcha: a helper named `quoted(std::string)` lost to `std::quoted` by argument-dependent lookup —
  named `json_string` instead.
- **SDL3 backend (P3 as built, 2026-10-16)**: module `gfx.platform.backends.sdl3` in
  `source/platform/backends/sdl3/` (PRIVATE file set; option `GFX_PLATFORM_BACKEND_SDL3` ON →
  `GFX_WITH_PLATFORM_SDL3_BACKEND`; the factory imports it under `#if`). Units: `sdl3_backend.cppm`
  (exports `sdl3::unavailable_reason()`, `sdl3::create_platform()`), internal partition `:internal`
  (Sdl3Platform, Sdl3Window, Sdl3Cursor, Sdl3InputDevices, records, translation declarations; every
  member `{}`-initialized), implementation units `sdl3_translation.cpp` (keys = HID scancodes, key codes
  pass through, modifiers, buttons, axes `/32767`, hats, cursors, `throw_sdl_error` → BackendApiError with
  SDL_GetError text), `sdl3_platform.cpp` (SDL_Init VIDEO|GAMEPAD, one platform per process via an atomic
  flag, monitors, clipboard text + PNG/BMP images, cursors, registries; table lookups only here and in
  the other .cpp units — defect 10), `sdl3_events.cpp` (pump; SDL event watch translates events SDL raises
  on the platform thread during a pump at once, copies skipped when polled in this pump or the next —
  SDL's poll stops at a sentinel; `SDL_WaitEventTimeout` in ms, rounded up; wake = registered user
  event), `sdl3_window.cpp` (created hidden then shown, FOCUS_ON_SHOW via SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN,
  modes, icons with alternate images, per-window cursor applied on mouse enter, cursor modes, text input
  + IME hint; windows/cursors hold a weak lifetime token and refuse to work after the platform),
  `sdl3_input_devices.cpp` (gamepads/joysticks opened at start-up and on ADDED, reported once;
  rumble/LED capability checked → BackendCapabilityError). SDL headers come through `sdl3_headers.hpp`
  in each unit's global module fragment (C headers only: fine with `import std`).
  `gfx_require_sdl3()` (find_package SDL3 3.4, else URL release-3.4.10 + SHA-256
  12b34280...2785, SYSTEM, static, subsystems trimmed, `_gfx_exclude_from_modules(SDL3-static
  SDL_uclibc)` — SDL's C++ files otherwise rebuilt the std module with SDL's -Wundef).
  Interface changes: `WindowDescription::flags` default += FOCUS_ON_SHOW; `GamepadType::NINTENDO_GAMECUBE`;
  `Platform` watcher dispatch re-entrant (`m_dispatch_depth`, `m_added_watchers`, `settle_watchers()`);
  FakePlatform::emit_now. Tests: tests/platform/backends/sdl3 (gfx_platform_sdl3_tests, module
  `gfx.tests.sdl3_support`: sdl_window_identifier via HWND match, re-exports gfx.tests.backend_support), events injected with
  SDL_PushEvent, virtual gamepads via SDL_AttachVirtualJoystick (no windows in gamepad tests: SDL drops
  joystick input while windows exist without focus). **Clipboard**: test opt-in (`GFX_TEST_CLIPBOARD=1`);
  in Claude's sandboxed shell OpenClipboard fails with ERROR_ACCESS_DENIED (process in a job without UI
  restrictions; cause not identified) — the user must run it once.
- **GLFW backend (P4 as built, 2026-10-16)**: module `gfx.platform.backends.glfw` in
  `source/platform/backends/glfw/` (option `GFX_PLATFORM_BACKEND_GLFW` ON → `GFX_WITH_PLATFORM_GLFW_BACKEND`;
  `gfx_require_glfw()`: find_package glfw3 3.4, else URL glfw-3.4.zip + SHA-256 b5ec004b...a3e9, SYSTEM,
  static). Units: `glfw_backend.cppm` (unavailable_reason, is_active, create_platform), `:internal`
  (GlfwPlatform, GlfwWindow, GlfwCursor, GlfwInputDevices, records), `glfw_translation.cpp` (consteval
  KEY_TOKENS → GLFW_KEYS table with static_asserts; key codes via glfwGetKeyName; modifier sides from
  glfwGetKey; `check_glfw()` after calls: FEATURE_UNAVAILABLE/UNIMPLEMENTED/CURSOR_UNAVAILABLE →
  BackendCapabilityError, else BackendApiError; `clear_glfw_error()`), `glfw_platform.cpp` (glfwInit with
  hat buttons off, one per process, monitors by GLFWmonitor* list, clipboard text only),
  `glfw_events.cpp` (callbacks → `deliver()`: emit during a pump, `m_pending` EventQueue otherwise;
  `with_window` catches exceptions → `record_failure` → rethrown by the pump; process-wide monitor and
  joystick callbacks through `g_callback_platform`; pump loops until an event, `m_wake_requested` or the
  deadline), `glfw_window.cpp` (GLFW_NO_API, created hidden, hints; SHOWN/HIDDEN synthesized by
  show()/hide(); fullscreen through glfwSetWindowMonitor with the windowed place remembered; monitor() =
  largest overlap), `glfw_input_devices.cpp` (polled diff per pump; triggers (v+1)/2; GUID → USB ids),
  `glfw_native.cpp/.hpp` (plain TU: glfw3native.h + windows.h; win32/x11/wayland/cocoa_handles()).
  Factory: one backend active at a time (`active_backend()` via each backend's `is_active()`).
  Both pumps now wait until an event is emitted, a wake is requested or the deadline passes (spurious
  wake-ups from messages/events that become no gfx event). Tests: tests/platform/backends/
  platform_backend_test.cpp (parameterized over available_platform_backends(), 12 × 2),
  backends/glfw (feature set + messages; input via PostMessageW of WM_KEYDOWN/WM_CHAR/WM_MOUSEMOVE/
  WM_*BUTTONDOWN/WM_MOUSEWHEEL/WM_CLOSE to the hidden window — GLFW's window procedure handles them),
  backends/sdl3. `gfx_add_test` gained `MODULE_BASE_DIRECTORY` (backend_test_support.cppm compiled into
  each executable: a shared module library gave "import 'std' has CRC mismatch").
- **Interface additions in the Vulkan task**: `BufferDescription::sparse`; `BufferViewDescriptor` and
  `AccelerationStructureDescriptor` in the `DescriptorResource` variant.
- **Handles**: 32-bit (24-bit slot index, 8-bit generation from 1; 0 = null). Value private; users test,
  compare (`<=>`), hash (`std::hash`) and format (`std::formatter`: `Buffer(index 5, generation 3)`).
  `HandleKey` (constructible only by `HandleIssuer`) gates `Handle::create/index/generation` for backends.
  Concepts: HandleType, GraphicsHandle, ComputeHandle, TransferHandle, ResourceHandle. 21 handle kinds.
  User question answered: users never manage pools; a handle→native lookup is backend-internal (decide
  with the user when the Vulkan backend is implemented; no SlotPool unless necessary).
- **Errors** (`gfx.errors`, below handles because handles throw): Error (base, runtime_error; backend
  optional, operation, source_location) → BackendCapabilityError, InvalidUsageError, InvalidHandleError,
  OutOfMemoryError, DeviceLostError, BackendApiError, ShaderCompilationError (step 1);
  ShaderLayoutMismatchError planned for step 2. Message: `[gfx::VULKAN] Operation: problem\n  Reason/
  Suggestion/Details...\n  Location: file:line (function)`. std::expected for normal outcomes
  (swapchain acquire), WaitStatus for waits. Destructors never throw.
- **Enumerations** (`gfx.enums`): 61 enumerations, each with `EnumerationTraits<E>` (IS_BITMASK, NAMES)
  verified at compile time (order, uniqueness, single bits) **and against GCC's own spelling of each
  enumerator via __PRETTY_FUNCTION__**, which also proves no enumerator is missing. Bitmask operators
  only for bitmask enumerations; `to_string`; `std::formatter`; `ENUMERATOR_COUNT<E>`;
  `FORMAT_PROPERTIES` (bytes/block, block size, components, aspects, numeric type);
  `RESOURCE_STATE_SYNCHRONIZATION` (render graph: ResourceState → stages, accesses, layout, categories, writes).
- **Shader layouts — run-time, reflected from SPIR-V** (step 2b, user decision 2026-10-14, replacing the
  earlier consteval GLSL reflection, which commercial engines do not do): `ShaderLayout` is a run-time
  value (stage, bindings with set/binding/type/count/block size/image format/**name**, push-constant
  offset+size, inputs, outputs, workgroup size) that the backend fills by reflecting the module's SPIR-V
  (SPIRV-Cross, step 4) — the fake backend reads layouts registered by the test instead. Validation at
  pipeline creation, **called by backends** after resolving handles: `validate_compute_pipeline(layout,
  shader)`, `validate_graphics_pipeline(layout, vertex, fragment_or_null, description)` → check_* functions
  (stage, pipeline layout incl. push-constant byte range per stage, vertex attributes, color formats) →
  ShaderLayoutMismatchError. `validate_shader_module_description()` (one stage, entry point, SPIR-V magic,
  GLSL only in Debug). Pipeline layouts explicit (`PipelineLayoutDescription`, bindings may carry names)
  or derived: `Device::create_pipeline_layout_from({shaders})` → `ReflectedPipelineLayout` (owns set
  layouts + pipeline layout; one push-constant range covering all shaders; CONFLICTING_BINDINGS error).
  `PipelineLayout::definition()` (PipelineLayoutDefinition: LayoutBindings with names + ranges;
  backends build it with `make_pipeline_layout_definition()`), `find_binding(name)` → `BindingLocation`
  (look up once). `DescriptorSet::write(BindingLocation, resource)` checks type + array element;
  `write(index, resource)` unchecked. Push constants: `recorder.push_constants(const PipelineLayout&, data,
  offset)` (concept PushConstantData rejects pointers/spans; `push_constant_stages()` checks coverage and
  returns the stages of overlapping ranges). Kept from the compile-time design: Recorder<Q>/scopes.
- **Shaders**: SPIR-V is the input (`ShaderSourceFormat::SPIRV` default). Build-time GLSL→SPIR-V with a
  CMake function `gfx_add_shaders()` (step 4; full GLSL incl. #include). **Debug builds** also accept GLSL
  compiled at run time; **Release builds only load compiled SPIR-V** (user: "release mode should only use
  the compiled code") — `gfx::SHADER_HOT_RELOAD` (enums.cppm) from macro `GFX_SHADER_HOT_RELOAD` set PUBLIC
  for `$<CONFIG:Debug>` in source/CMakeLists.txt; no override option.
- **Shader hot reload** (Debug only; implementation step 4): `Device::poll_shader_changes()` (non-virtual;
  Release returns {} without reaching the backend; Debug calls protected `reload_changed_shaders()`),
  once per frame when no thread records; `ShaderModuleDescription::source_path` names the watched file;
  report `std::vector<ShaderReload>` (ShaderReloadResult RELOADED / COMPILATION_FAILED / LAYOUT_MISMATCH +
  message). Accepted rules: a reload is applied when the new code still fits the pipeline layouts of the
  pipelines using it (old code kept otherwise); pipelines rebuilt automatically behind the same handles;
  synchronous compile in poll.
- **Parallelism**: queues first-class (QueueRequest per type, fallback when no dedicated family);
  CommandPool per thread × queue family (externally synchronized, lock-free recording);
  **FrameCommandPools** (per thread: one pool per frame in flight, `begin_frame(N)` waits on the frame
  timeline for the frame that last used the pool, resets it, reuses its command buffers);
  **parallel rendering passes** (step 2b): `Recorder::begin_parallel_rendering()` → ParallelRenderingScope
  (`inheritance()`, `execute(parts)`, only execute inside the pass) and `RenderingPartRecorder` on
  SECONDARY command buffers (only in-pass commands; base class RenderingCommands shared with
  RenderingScope); RenderingInheritance = primary handle + pass index + formats + samples (Vulkan
  secondaries with inheritance, D3D12 command list per part, Metal parallel render encoder);
  Recorder rejects secondary command buffers, Recorder::execute_commands removed; Device creation
  functions and the backend handle table must be thread-safe (Step 3 slot-table design);
  `Recorder<QueueType Q>` zero-cost typed wrapper with requires-constrained methods (draw does not
  compile on a compute recorder); Queue::submit(batches) non-blocking with timeline semaphore
  waits/signals; binary semaphores only for swapchain; per-queue mutex; Deleter keyed on per-queue
  timeline progress.
- **Render graph enablement**: sync2-style barriers (memory/buffer/image, queue ownership transfer),
  ResourceState table, dynamic rendering via RAII scope, attachment descriptions, memory requirements +
  placed resources (aliasing), transient attachments, concurrent sharing, timestamp/statistics queries.
- **Modules**: `gfx` umbrella re-exports `gfx.core`, `gfx.enums`, `gfx.errors`, `gfx.handles`,
  `gfx.interface`, `gfx.backend_factory`, `gfx.shader_tools`. Interfaces: module `gfx.interface` with
  partitions in `source/graphics/interface/`. Backends: `gfx.backends.<name>` in PRIVATE file sets.
- **`import std;`** is used everywhere (user choice "b"), with the requested **warning comment** in
  `cmake/GfxImportStandardLibrary.cmake` (experimental CMake gate UUID `451f2fe2-a8a2-47c3-bc32-94786d8fc91b`
  for CMake 4.4; `-DGFX_IMPORT_STD_GATE=<uuid>` override). `.hpp` only where a module cannot serve.
- **Tests**: GoogleTest (built from source and linked statically on Windows) + CTest in parallel; every
  test case is its own CTest test (`gtest_discover_tests(... DISCOVERY_MODE POST_BUILD)` — PRE_TEST wraps
  each test in a `cmake -P` launcher that debuggers would attach to). Compilation-failure tests
  (`gfx_add_compilation_failure_test`, RESOURCE_LOCK on the build tree, regex on the diagnostic), a
  platform-macro collision check (`cmake/scripts/GfxCheckPlatformMacroCollisions.cmake`), toolchain
  conformance tests and toolchain **defect detectors** (pass while a GCC defect exists).

## 6. Toolchain defects found and workarounds (GCC 16 / MinGW)

1. **`import std` emits template static locals as strong symbols** (e.g.
   `std::__format::__write_escaped_unicode_part<...>::__replace_rep`) → "multiple definition" when two
   objects format strings. Workaround: `-Wl,--allow-multiple-definition` on the INTERFACE target
   `gfx::toolchain_workarounds` (linked PUBLIC by `gfx`). GMF `#include`s do not have this defect
   (verified). Detector: `CompilationFailure.ImportStdDuplicatesStaticLocals` (forced `-O0`; optimized
   builds fold that symbol away).
2. **`<windows.h>` cannot share a translation unit with modules** (with `import std` *and* GMF includes):
   it includes libstdc++ config and x86 intrinsics inside `extern "C"`. Workaround header
   `source/include/gfx/windows_headers.hpp` (includes `<cstdlib>` and `<x86intrin.h>` first) — include it
   before any `import`. Detector: `CompilationFailure.WindowsHeadersConflictWithModules`. Includes must
   come **before** imports (import-then-include fails).
3. GMF includes drop declarations the purview never names (defaulted `auto operator<=>` needs
   `std::strong_ordering`) — only relevant if switching away from `import std`.
4. MinGW `std::print` needs `-lstdc++exp` (in toolchain workarounds).
5. GCC reports `std::source_location::current()` of a multi-line call at its **last** line.
6. **A capturing lambda in an in-class constructor of a class declared in one module partition and
   defined in another** (RenderingPartRecorder: `:command_buffer` declares it as a friend) makes the
   defining partition's BMI unreadable ("failed to read compiled module cluster N: Bad file data").
   Workaround: private static `[[noreturn]]` helper (`RenderingPartRecorder::reject`). Detector:
   `CompilationFailure.LambdaInConstructorCorruptsModuleFile` (tests/toolchain/defects, 4 small files;
   `gfx_add_compilation_failure_test` gained a `MODULES` argument for it). Without the cross-partition
   declaration or without the lambda, the BMI is fine.
7. **GCC 16 ICE (segfault) at the end of the mem-initializer list** of a constructor defined in a module
   implementation unit, for a class declared in an internal partition, when it default-initializes a
   member of a class template specialization from another module that no initializer names (e.g.
   `FenceHandle m_handle;` in VulkanFence). Minimized: the template needs neither a constructor nor a
   default member initializer; `FenceHandle m_handle{};` in the class avoids it (as does declaring the
   class in the implementation unit — the workaround used in vulkan_synchronization.cpp). Detector:
   `CompilationFailure.MemberInitializerOfPartitionClassCrashesCompiler` (tests/toolchain/defects,
   member_initializer_*: 5 small files).
8. **glslang 16.2 `-gVS`** (NonSemantic shader debug info) emits invalid SPIR-V for vertex shaders'
   built-in gl_PerVertex block (spirv-val error): `gfx_add_shaders()` uses `-g`, and the run-time
   compiler disables NonSemantic debug info.
9. Backend sources use `-Wno-missing-field-initializers` (per file): Vulkan structs are initialized
   with designated initializers that leave most members zero.
10. **`HandleTable::find()`/`remove()` instantiated in a module interface** (inline or not), for a tag of
   another module (e.g. `gfx::platform::WindowTag`) with a record pointing to a class of the instantiating
   module, makes that module file unreadable for importers ("failed to read compiled module cluster N: Bad
   file data", "failed to load binding"). `insert()` alone is fine; a local tag or a record without the
   pointer is fine. Workaround: table lookups live in module implementation units (Vulkan backend .cpp,
   tests/platform/fake_platform.cpp; the SDL3/GLFW backends must do the same). Detector:
   `CompilationFailure.TableLookupInModuleInterfaceCorruptsModuleFile` (tests/toolchain/defects/
   table_lookup_*). Found by bisecting the fake platform with a script that stubs functions.
11. stb_image's `<stdio.h>` in a global module fragment conflicts with `import std` (vsnprintf linkage):
   define `STBI_NO_STDIO` before including it there. FreeType's headers include `<stdio.h>` too (no option to
   avoid it): FreeType is reached through a plain TU behind a std-free header (freetype_library.hpp). HarfBuzz,
   zstd and stb_truetype headers are fine in a GMF next to `import std`; msdfgen/msdf-atlas-gen (C++ headers) are not.

## 7. Repository layout (current)

```
gfx/
├── CMakeLists.txt            orchestrates cmake/ modules; add_subdirectory(source, tests)
├── CMakePresets.json         debug/release (Ninja, MSYS2 PATH via ${pathListSep}), test jobs ""
├── build.py                  root (user moved it back from builder/); python build.py [--test]
├── .vscode/                  tasks.json, launch.json, settings.json (see §8)
├── .claude/                  this memory
├── cmake/  GfxImportStandardLibrary, GfxToolchainRequirements, GfxProjectOptions (+workarounds),
│           GfxRuntimeDeployment, GfxDependencies (volk, VMA, SPIRV-Cross, glslang, RenderDoc header),
│           GfxShaders (gfx_add_shaders), GfxTesting,
│           scripts/{GfxCheckPlatformMacroCollisions, GfxEmbedSpirv}
├── docs/   platform-design.md (windowing + input design, decisions, phases), architecture.md (as built, diagrams)
├── examples/windowed/  gfx_windowed_example (P6)
├── source/ CMakeLists.txt (add_subdirectory core, graphics)
│   ├── core/      gfx.core: core.cppm, enums.cppm, errors.cppm/.cpp, handles.cppm, types.cppm,
│   │              native_window.cppm, slot_table.cppm, handle_table.cppm, include/gfx/windows_headers.hpp
│   ├── graphics/  gfx: gfx.cppm, enums.cppm, errors.cppm/.cpp, handles.cppm, backend_factory.cppm/.cpp,
│   │              interface/ (gfx.interface partitions), shaders/ (shader_tools.cppm,
│   │              spirv_reflection.cpp, shader_compiler.cpp), backends/{vulkan, null, d3d12, metal}/
│   └── platform/  gfx.platform: platform.cppm, enums.cppm (generated Key/KeyCode tables), handles.cppm,
│                  backend_factory.cppm/.cpp, interface/ (gfx.platform.interface partitions + image.cpp,
│                  stb_image_implementation.cpp), input/ (gfx.platform.input: input.cppm + partitions
│                  binding, schema, frame, event_channel, input_system; .cpp units + json_reader.hpp/.cpp),
│                  backends/sdl3/ (P3), backends/glfw/ (P4)
│   └── platform_graphics/  gfx.platform.graphics (P5): platform_graphics.cppm/.cpp
│   └── ui/        gfx.ui (U0): ui.cppm, enums.cppm, handles.cppm, errors.cppm; text/ (U2: unicode.cppm, gfx.ui.text
│                  partitions + engine.cppm internal + font_atlas/font_library/bidi/line_breaking/shaping/text_layout .cpp);
│                  font/atlas/atlas_format.cppm (U1,
│                  in gfx.ui); font/ = gfx.ui.atlas_tools (U1): atlas_tools.cppm, implementations.cppm (internal),
│                  source/, decoders/ (+ freetype_library.hpp/.cpp plain TU), sdf/ (+ msdfgen_kernel plain TU),
│                  atlas/ (+ rectangle_packer plain TU), shaping/; tools/atlas_builder/ (gfx_ui_atlas_builder);
│                  draw/ (U3: draw.cppm + draw_list.cpp = gfx.ui.draw, debug_overlay.cppm/.cpp), render/ (U3:
│                  renderer.cppm/.cpp = gfx.ui.renderer, shaders/ui.vert, ui.frag); U4: style/style.cppm,
│                  input/frame_input.cppm, context/ (context.cppm + types/frame/context_class partitions, state.cppm
│                  internal, .cpp units; U5: popups, text_input, numbers, colors, combos; U6: trees, tabs, menus,
│                  tooltips, drag_drop; U7: tables, clipper, visuals), platform/ (gfx_ui_platform:
│                  ui_platform.cppm/.cpp); U5: text/boundaries.cppm, text_editor.cppm/.cpp
└── tests/  toolchain/ (+defects/), core/ (gfx_core_tests + slot_table/), platform/ (fake_platform
            .cppm/.cpp, input_test_support.cppm + 10 test files; backends/ = shared backend suite +
            sdl3/ + glfw/), platform_graphics/ (presentation_test.cpp; includes graphics/vulkan/vulkan_test.hpp),
            ui/ (ui_foundations_test.cpp; font_libraries_test.cpp = plain TU, fetched OFL fonts; U1: atlas_*_test.cpp,
            font_*_test.cpp, sdf_generator_test.cpp, module gfx.tests.ui_atlas_support, xxhash_reference plain TU,
            atlas_embed_test.cpp + gfx_add_ui_atlas, scripts/GfxCompareAtlasBuilds.cmake; U2: font_atlas_test.cpp,
            text_layout_test.cpp, text_test_support.cppm, text_allocation_test.cpp; U3: draw_list_test.cpp,
            renderer_test.cpp = gfx_ui_draw_tests with atlas gfx.tests.draw_latin; U4: context_test.cpp,
            context_render_test.cpp, context_test_support.cppm = gfx_ui_context_tests (atlas context_latin; U5
            input_widgets_test.cpp, U6 structure_test.cpp, U7 table_test.cpp),
            ui_platform_test.cpp = gfx_ui_platform_tests (atlas platform_latin)), compilation/ui/,
            graphics/{backend_factory,
            enums,errors,handles,interface,shader_layout,shader_tools,windows_headers,vulkan}/
            (vulkan: real GPUs, shaders/ via gfx_add_shaders), compilation/{handles,enums,interface,platform}/
```

## 8. Build, test, debug

- `python build.py` (Debug into build/debug) · `python build.py --test` · `--build-type Release` ·
  `--clean-all` (handles read-only Git files).
- `cmake --workflow --preset debug` · `ctest --preset debug` (parallel) · `-L compilation`.
- Presets put `C:/msys64/ucrt64/bin` first on PATH with **`${pathListSep}`** (a `$pathListSep` typo broke
  this until the fix) — builds work from any shell/IDE even without MSYS2 on PATH.
- VS Code: Run and Debug → **gfx: Debug Test** (pick executable + gtest filter; no CMake Tools needed);
  Testing panel "Debug Test" uses **gfx: Debug Test (CMake Tools Testing panel)** (`${cmake.testProgram}`
  placeholders; `cmake.ctest.debugLaunchTarget` in settings.json). preLaunchTask **gfx: Build Debug**
  builds only (never reconfigures — reconfiguring under CMake Tools cancelled its runs).
  GTEST_BREAK_ON_FAILURE=1 stops gdb at the failing assertion (select the test frame in the call stack).
  The executable list in launch.json must be updated when a test executable is added.
- PowerShell splits `-DNAME=a.b` arguments at the dot: quote `-D` arguments.
- Since 2026-10-18 the linker's error output (ld's stderr) is lost in Claude's Bash shell: the defect detector
  `CompilationFailure.ImportStdDuplicatesStaticLocals` fails there with an empty diagnostic. Run ctest from PowerShell.
- Vulkan tests: `build/debug/tests/graphics/vulkan/gfx_vulkan_tests.exe`; physical device 0 is the Intel
  GPU, the default selection is NVIDIA; `GFX_TEST_PHYSICAL_DEVICE=0` runs on Intel, `GFX_TEST_VALIDATION=0`
  disables the layers (the harness sets `VK_ADD_LAYER_PATH` itself). CTest `VulkanCapture.RenderDoc`
  runs one test under `renderdoccmd` (when RenderDoc is installed) and checks the `.rdc` file.
- Source files may have CRLF endings (core.autocrlf=true); scripts that rewrite files must keep them.

## 9. Status

| Step | Content | State |
|---|---|---|
| 0 | Toolchain, CMake/presets/build.py, GoogleTest, toolchain tests | done, committed (ffae3d2, 0c8af25) |
| 1 | enums, errors, handles, unit/compilation/macro tests, workarounds | done, committed (8b750f7, 58bbcfc) |
| — | VS Code debugging, preset PATH fix, POST_BUILD discovery | committed (bafcb25) + later fixes |
| 2 | Interfaces, descriptions, Recorder<Q>, shader layouts; **2b**: run-time SPIR-V-reflected layouts, Debug-only reload API, parallel rendering passes, FrameCommandPools; **2c**: production interface (bindless/AZDO, validation levels, RT, mesh, GPU-driven, caches, frame resources, diagnostics) | done, committed (6bacc72, 1bbb792) |
| 3 | backend_factory + Vulkan/Null/D3D12/Metal stubs + thread-safe handle table | done, committed (e839407) |
| 3b | **Real Vulkan backend** (volk, VMA, sync2, dynamic rendering, bindless, pipeline cache, async pipelines, hot reload, RT, mesh, sparse, breadcrumbs, RenderDoc) | done, committed (e839407); presentation added in P5 |
| 4 | Shader toolchain: `gfx_add_shaders()`, SPIRV-Cross reflection, Debug run-time GLSL/HLSL compile + hot reload | **done with step 3b** except DXC (HLSL through glslang) and a shipped shader header declaring the bindless arrays (tests have their own `bindless.glsl`) |
| P | **Platform library: windowing + input** (user prompt 2026-10-15: backend-agnostic windows/surfaces above GLFW, SDL3, Win32, XCB, Cocoa, Web, headless; normalized events; physical `Key` / logical `KeyCode`; action mapping à la Enhanced Input / InputMap with compile-time schema, contexts, per-player routing, rebinding + persistence; lock-free input snapshots; `NativeWindowHandle` bridge to graphics; reused parts moved to a unified core; README with examples; architecture overview with diagrams at the end) | P0 (e3638c0), P1 (5cc9de0), P2 (a42d68a), P3 (SDL3, 2dcac3c), P4 (GLFW, e33b318), P5 (presentation, 4ec3519), P6 (example + docs, bbea683) committed; **follow-up (ignore_held_inputs, Debug frame metrics overlay) committed (a9f1e78)** (451 tests Debug + Release). Every phase of the platform plan is implemented. Design: [docs/platform-design.md](../docs/platform-design.md) |
| UI | **UI + SDF/MSDF font system** (user prompt 2026-10-16: hybrid immediate/retained UI with Dear ImGui + docking parity, tables, accessibility; every text through an SDF/MSDF pipeline: any font format → msdfgen/msdf-atlas-gen atlases built at build time, serialized `.uiatlas`, loaded with no glyph work; HarfBuzz, SheenBidi, line breaking; consumes graphics (and audio) abstractions; GitHub repo) | **design approved 2026-10-17** ([docs/ui-design.md](../docs/ui-design.md), answers in §D: audio out of scope, the rest as recommended); U0 (b1b4ba4), U1 (4ba8362), U2 (fccdc7a), U3 (ba6e838), U4 (5511d8a), U5 (1acbc63), U6 (a77c83e), follow-up after U6 (5e7f8a6: allocation-free cache misses, LRU fix, bidi fast path; cache policy accepted) committed; **U7 (tables, list clipper, splitters, resizable child regions, plots, progress bars, images) done, awaiting validation** (§B "U7 as built", 11 deviations); U8 (docking) next. |
| 5 | Samples (headless + windowed) — scope to confirm | **windowed done in P6** (`examples/windowed`); a headless sample (compute + readback) not yet |
| 6 | Final README, architecture overview **with diagrams** of the code as built | **README + docs/architecture.md written in P6** (whole project); left: CI (MSVC/Clang/Linux), packaging, Doxygen HTML |

Test count at last report: **748**, all passing in Debug and Release, 2026-10-18 (after U7: + 21 table/visual tests, + 1 render
× 2 GPUs, + 3 draw list, + 1 rounded image × 2 GPUs, + 1 allocation, + 1 compile-failure; skips Debug 4, Release 19; run from
PowerShell, see §8). Before U7: 718 (after the follow-up after U6: + 3
allocation tests, + 2 cache tests; skips Debug 4, Release 19). Before: 713 (after U6: + 27 structure tests, + 1 render
× 2 GPUs, + 1 allocation, + 1 compile-failure; skips Debug 4, Release 19). Before U6: 682 (after U5: + 9 text editor tests, + 26
input widget tests + 2 render × 2 GPUs, + 1 draw list, + 1 allocation, + 1 IME × 2 platform backends, + 1 compile-failure;
skips Debug 4, Release 19). Before U5: 638 (after U4: + 37 context tests + 2
render (2 GPUs), + 6 platform tests (2 backends × adapter, cursors, visible window), + 1 allocation test, + 2
compile-failure tests). Before U4: 590 (after U3: + 52 gfx_ui_draw_tests =
18 CPU + 17 × 2 GPUs, + 1 allocation test, − 5 DebugOverlay unit tests; presentation overlay test replaced by the UI one;
skips Debug 4, Release 19). Before U3: 542 (after U2: + 19 gfx_ui_text_tests,
1 gfx_ui_text_allocation_tests). Before U2: 522 (after U1: + 48
gfx_ui_atlas_tools_tests, 2 gfx_ui_atlas_embed_tests, 4 UiAtlasTool.* CLI tests; skips Debug 4, Release 27). Before U1:
468 (after U0: + 7 gfx_ui_tests,
9 gfx_ui_font_library_tests, 2 UI compile-failure tests; skips: Debug 4, Release 27). Before U0: 451 (after the P6 follow-up:
+ 5 held-input tests, 9 FrameMetrics/DebugOverlay unit tests (7 skip in Release), overlay read back from a
swapchain image × 4 (skip in Release)). After P6: 433 (+ 2 example runs
`WindowedExample.{SDL3,GLFW}` and `OnlySizesThatChangeAreReported` × 2). After P5: 429 (+ 24 presentation
tests = 6 × SDL3/GLFW × NVIDIA/Intel, visible windows, validation required). **Check `CMAKE_BUILD_TYPE` in
build/release/CMakeCache.txt** before trusting a Release run: during P5 it was found set to Debug (cause
unknown; `cmake --preset release` fixed it) — the Release skips (Debug-only compiler/hot reload) prove it.
Before P5: 405 (after P4: 69 platform tests,
24 shared backend tests (12 × SDL3, GLFW), 16 SDL3 + 9 GLFW backend tests (the two clipboard tests skipped
unless GFX_TEST_CLIPBOARD=1), 32 compilation tests; 90 Vulkan tests: NVIDIA 89 + RenderDoc via CTest; Intel 81 + 9 skipped). One unexplained
failure of `CompilationCheck.NamesDoNotCollideWithPlatformMacros` was seen once in a Debug run while the
Release build ran concurrently (output lost); not reproduced in 5 later runs, 3 of them under the same load.

### Step 2c as built (production interface, 2026-10-14)

- **Build macro**: `GFX_DEBUG_BUILD` (PUBLIC, `$<CONFIG:Debug>`) → `DEBUG_BUILD`, `SHADER_HOT_RELOAD`,
  `DEFAULT_VALIDATION_LEVEL` (COMMANDS in Debug, CREATION in Release) in enums.cppm.
- **Validation levels**: `BackendObject` stores the level (non-virtual `validation_level()`,
  `validates(level)`, protected ctor/`set_validation_level`); `InstanceDescription::validation_level`.
  CREATION = descriptions, shader/pipeline checks (backends call validators only at ≥ CREATION),
  recorder/part/execute construction checks; COMMANDS = push-constant coverage, indirect count buffer,
  trace_rays ray-generation region, Uploader texel size. Also `set_debug_name()` (pure virtual).
- **Bindless** (bindless.cppm): `BindlessIndex<Kind>` (public value, GPU data, invalid = INVALID_BINDLESS_INDEX,
  formatter), aliases SampledImageIndex/StorageImageIndex/SamplerIndex/StorageBufferIndex/
  AccelerationStructureIndex; `BindlessResourceKind` value = binding in BINDLESS_SET (0); names
  gfx_sampled_images, gfx_storage_images, gfx_samplers, gfx_storage_buffers, gfx_acceleration_structures;
  `bindless_pipeline_layout_definition()` (arrays PARTIALLY_BOUND|UPDATE_AFTER_BIND, stages ALL, push
  constants [0,128) ALL). `DeviceDescription::bindless` capacities. Resources register by default
  (`bindless = true`): Buffer::bindless_index (STORAGE), ImageView::sampled_index/storage_index,
  Sampler::bindless_index, AccelerationStructure::bindless_index (top level). Pipelines with a null layout
  use `Device::bindless_pipeline_layout()`; backends bind the table in record_begin /
  record_begin_rendering_part; `push_constants(data)` without a layout uses the bindless layout
  (`CommandBuffer::bindless_pipeline_layout()`). `Device::bindless_usage()`.
- **Shaders**: `ShaderSourceFormat::HLSL`; description `source` (renamed from glsl_source), `defines`,
  `include_directories`, `source_path`; `SpecializationConstant` + `specialization_constant(id, value)`;
  ShaderLayout gains `specialization_constants`.
- **Pipelines**: compute/graphics/RT descriptions have specialization_constants, `cache`
  (PipelineCacheHandle), `creation_mode` (IMMEDIATE/ASYNCHRONOUS on `DeviceDescription::task_scheduler`);
  pipelines have `status()` (READY/COMPILING/COMPILATION_FAILED) and `wait()`. Graphics: task, mesh,
  geometry, tessellation shaders, view_mask, `state.dynamic_states` (DynamicState bitmask; viewport,
  scissor, stencil reference, blend constants always dynamic), conservative rasterization, depth bounds,
  primitive restart, sample mask, shading rate, patch control points. `PipelineCache` (serialize,
  loaded_initial_data). Validation: `validate_compute_pipeline(layout, shader, constants)`,
  `validate_graphics_pipeline(layout, GraphicsPipelineShaders, description)` (vertex XOR mesh, task needs
  mesh, tessellation pairs, no geometry/tess with mesh, stage interface last-pre-raster→fragment,
  specialization ids), `validate_ray_tracing_pipeline(layout, description, resolve)`. LayoutMismatch +=
  STAGE_INTERFACE_MISMATCH, UNKNOWN_SPECIALIZATION_CONSTANT.
- **Ray tracing**: AccelerationStructure (+ descriptions, TriangleGeometry, BoundingBoxGeometry,
  InstanceGeometry, build description/sizes, 64-byte `AccelerationStructureInstance` with bitfields +
  `make_instance`), RayTracingPipeline (groups, shader_group_handles), ShaderBindingTable(+Region),
  `shader_binding_table_layout()`; recorder: build/copy/write properties, bind_pipeline(RT), trace_rays(_indirect).
- **Commands**: blit_image, resolve_image, clear_depth_stencil_image, `generate_mipmaps()` (library
  algorithm), clear_attachments, extended dynamic state setters, set_fragment_shading_rate,
  draw_(indexed_)indirect_count (`IndirectCount`), draw_mesh_tasks(_indirect)(_count); argument structs
  double as indirect records (static_asserted sizes); RenderingDescription view_mask +
  shading_rate_attachment; `CommandStatistics` per command buffer (counted by recorders, reset at begin);
  `ParallelRenderingScope::execute(unique_ptrs)` no longer allocates.
- **Memory / sparse / misc**: BufferView, MemoryHeapBudget + `Device::memory_budget()`,
  MemoryStatistics, persistent mapping documented; sparse_binding.cppm + `Queue::bind_sparse()`,
  `Device::sparse_image_requirements()`; Sampler reduction mode; SurfaceSupport +
  `PhysicalDevice::surface_support()`; Swapchain maximum_frame_latency, `wait_for_frame_latency()`,
  `set_hdr_metadata()`; DeviceLimits (+RT, mesh, subgroup, bindless maxima, ...); DeviceFeature += 9;
  ResourceState += SHADER_* (any stage, bindless), ACCELERATION_STRUCTURE_*, SHADING_RATE_ATTACHMENT.
- **Core features**: `CORE_FEATURES` (TIMELINE_SEMAPHORE, DYNAMIC_RENDERING, DESCRIPTOR_INDEXING);
  `select_physical_device()` skips devices lacking them (error lists what each lacks).
- **Diagnostics**: CrashDiagnostic (BREADCRUMBS = debug labels written by the GPU; VENDOR_REPORT),
  `DeviceLostError::active_markers()`/`crash_report_path()` (message "GPU was inside: a > b"),
  CaptureTool + `Device::capture_tool/begin_capture/end_capture`.
- **Library helpers on the interfaces** (backend-independent): `FrameTimeline` (pacing, frame_signal,
  deleter collect), `TransientBufferAllocator` (lock-free CAS bump allocator, persistently mapped,
  per-frame regions; frame N uses region N % frames; `write(range)` / `write(value)` rejecting ranges and
  pointers), `Uploader` (staging + barriers, upload_buffer/upload_image), `GpuProfiler` (timestamp
  scopes, read back N frames later).
- **Tests**: bindless_test, pipeline_validation_test (incl. RT data layouts), frame_resources_test (real
  threads), recorder/device/errors additions; compile-failure: TransferRecorderCannotTraceRays,
  BindlessIndicesOfDifferentKindsDoNotMix. Fake backend: FakeQueryPool, sized FakeBuffer, bindless layout
  (`default_bindless_layout()`), `use_validation_level()`, `last_timeline()`, `last_query_pool()`.
- Gotchas met: lambdas that use a local `constexpr` need it `static constexpr`; span parameters do not
  deduce from fixed-extent spans (accept contiguous ranges instead); `\n` in shell heredocs passed to
  python becomes a newline — write such scripts with the Write tool.
- **Not addressed** (documented as future): external memory/semaphore interop, multi-GPU, video,
  device-generated commands, MSVC/Clang/Linux builds (step 6 CI).

### Step 2 as built (details for the next session)

- `source/interface/`: module `gfx.interface`, 34 partitions (after 2b): common_types (Version, Extent, Offset,
  Rectangle, Viewport, ImageSubresourceRange/Layers, ComponentMapping, MemoryRequirements,
  MemoryPlacement, NativeWindow), backend_object (BackendObject : HandleIssuer; backend(),
  debug_name(), throw_unsupported(), throw_not_implemented()), shader_layout, shader_validation,
  synchronization (GlobalMemoryBarrier/BufferBarrier/ImageBarrier/BarrierBatch, buffer_barrier(),
  image_barrier(), needs_barrier(), semaphore waits/signals), commands (copy regions, draw arguments,
  ColorAttachment, DepthStencilAttachment, RenderingDescription), one partition per object (25 classes),
  recorder, frame_command_pools, interface.cppm (typed_objects and layout_compatibility removed in 2b).
- Rules applied: **non-noexcept operations** are pure virtual with an out-of-class throwing definition;
  **noexcept accessors** (handle(), size(), ...) are plain pure virtual (a noexcept body cannot throw).
  Every description member has a default initializer (GCC -Wmissing-field-initializers otherwise).
- CommandBuffer recording primitives (`record_*`) are **protected**; Recorder<Q>, RenderingScope<Q>,
  RenderingCommands, ParallelRenderingScope and RenderingPartRecorder are friends. Recorder: copies/barriers/queries/labels on every queue; compute pipeline, compute
  descriptor sets, push constants, dispatch, events, begin/end_query need COMPUTE capability;
  begin_rendering needs GRAPHICS and returns RenderingScope (viewport, scissor, graphics pipeline,
  graphics descriptor sets, push constants, vertex/index buffers, draws). Runtime check at Recorder
  construction: has_all(queue_type_capabilities(buffer.queue_type()), CAPABILITIES).
- (Removed in 2b: Device's typed front doors and `DescriptorSet::write<"name", Shader>`; see §5 for the
  run-time replacements.) Texel buffers and acceleration structures cannot be written yet (no BufferView).
- Tests: tests/unit/shader_layout, tests/unit/interface (fake backend module `gfx.tests.fake_backend`),
  10 new compilation-failure tests, macro check now scans every .cppm of source/ (837 names).
- Findings: default arguments are not inherited by overrides — call backends through interface
  references; MinGW `<windows.h>` defines min/max only for C, not C++ (MSVC defines them in C++).

### Production-readiness decisions (user, 2026-10-14) — step 2c

After a gap analysis against Unreal RHI / NVRHI / Diligent / The Forge / Dawn, the user decided:
1. **Define the whole interface now (step 2c, "as much as possible"); the next step is the Vulkan
   backend** (step 3 = factory + stubs + thread-safe handle table, 3b = real Vulkan backend). This reverses
   the earlier "no backend implementation for now".
2. **GLSL and HLSL** accepted, converted to SPIR-V at the appropriate moment (build time with
   `gfx_add_shaders()` — glslang / DXC; run time in Debug builds only).
3. **Bindless by default everywhere, AZDO (Approaching Zero Driver Overhead) at all levels.**
4. **Validation levels as recommended**: full checks in Debug; Release keeps only checks that cost
   nothing per command (creation-time checks); selectable at instance creation.
5. **Keep exceptions.**
6. **Targets: MSVC and Clang on Windows, plus Linux** (GCC/Clang). Metal ignored for now.
7. Order as recommended: 2c interface → 3 → 3b Vulkan → 4 shader toolchain → 5 samples/benchmarks →
   6 CI (MSVC/Clang/GCC, Linux), packaging, Doxygen, architecture document.

## 10. Open questions (asked, not answered yet)

00. ~~**UI system (2026-10-16)**~~ ANSWERED 2026-10-17 (docs/ui-design.md §D): audio ignored altogether; build on
    gfx.platform; same repository (source/ui); core unchanged (Handle/Identifier); UiCapabilityError yes (alias);
    delete DebugOverlay's bitmap font in U3; build-time atlas + `#embed` + consteval validation; .notdef + report
    now, optional run-time SDF cache later; phases U0–U10 in order; FreeType fetched and pinned; UIA vs AccessKit
    decided in U9 after a trial; fetched OFL test fonts. Facts: GCC 16 has flat_map/mdspan/pmr, no constexpr cmath;
    `#embed` works in modules (C++26 extension: -Wno-c++26-extensions on generated files only); MSYS2 packages
    exist for freetype, harfbuzz, libunibreak, woff2, brotli, zstd (only zstd is installed); none for msdfgen,
    msdf-atlas-gen, SheenBidi.
0. **Platform library (2026-10-15)** — answers recorded in docs/platform-design.md §D: restructure into
   source/{core,graphics,platform} (graphics namespace/modules unchanged) YES; one error hierarchy in
   core (`BackendIdentity`) YES; `NativeWindowHandle` in core with OS handles only YES; naming deviations
   (`WindowHandle`/`Window`, `KeyModifier::CONTROL`, `*Identifier`, `PlatformBackendKind`,
   `DELETE_FORWARD`) YES; dependencies: find_package + FetchContent fallback (GLFW 3.4, SDL3 3.4.10
   static), nlohmann-json, stb_image; Vulkan presentation YES ("the windowed example should really
   run"); hold/tap triggers + rumble/LEDs now, sensors/AXIS_3D later; window tests allowed; **no gamepad**
   (inject events, SDL virtual gamepads); **no std::variant** at first (reverted, see follow-up); **threading model of commercial engines** (researched:
   Unreal, Unity, Godot, SDL3, GLFW — design A9: main thread owns the platform and pumps; input once per
   game frame, immutable frame shared with jobs, double-buffered; EventChannel SPSC for a separate game
   thread; render thread never touches platform/input, reads window sizes via atomics).
   **Follow-up (2026-10-15):** events and NativeWindowHandle use **std::variant** (preferred to a union;
   DescriptorResource unchanged); **backends SDL3 and GLFW only** (no headless; others reported as not
   built); A9 threading model confirmed. **Implementation started: P0 (restructure).**
   Facts: MSYS2 has glfw 3.4, sdl3 3.4.10, nlohmann-json 3.12, stb, benchmark (none installed; pacman is
   `C:\msys64\usr\bin\pacman.exe`, not on the Git Bash PATH). Graphics `NativeWindow{connection, window}`
   carries no windowing-system tag; the Vulkan instance enables no surface extensions yet.

1. Keep `import std` + `--allow-multiple-definition`, or switch modules to global-module-fragment
   `#include`s (recommended by Claude: avoids the linker flag and the experimental CMake gate)?
2. Handle tags as `struct` (current) or `class`?
3. Did "builder folder" mean the build **output** directory (currently `build/`)?
4. Add a Doxygen HTML target (needs the MSYS2 `doxygen` package)?
5. (Step 2) Render-graph gaps found: secondary command buffers inside a dynamic rendering pass —
   **done in 2b** (parallel rendering passes); still open: no BufferView object for texel buffers. Add it?
6. (Step 2) Value types named `Extent`/`Offset`/`Rectangle` (no `2D`/`3D` suffix, per the no-abbreviation
   rule; Extent has depth = 1 by default). OK?
**Step 2b done** (2026-10-14): see §5 (shader layouts, shaders, hot reload, parallelism) and §6.6. Files:
source/interface/{shader_layout, shader_validation (replaces layout_compatibility), pipeline_layout,
shader_module, descriptor_set, commands, command_buffer, recorder, frame_command_pools, device}.cppm;
typed_objects.cppm and layout_compatibility.cppm deleted (the user's index still has the old staged
versions: they need `git add -A`). Tests: tests/unit/shader_layout/shader_validation_test.cpp,
tests/unit/interface/{device,recorder,frame_command_pools}_test.cpp (parallel parts recorded on real
std::jthreads), fake backend resolves handles and calls the validators; compile-failure tests: 6 removed
(typed pipelines x2, typed descriptor writes x2, typed push constants, GLSL reflection), 3 added
(ParallelRenderingScopeCannotDraw, RenderingPartsCannotRecordBarriers, PushConstantsRejectPointers) + the
defect detector. README examples (blur with reflected layout; parallel G-buffer) compiled with -Werror.
`gfx_add_shaders()` is referenced in messages/README but arrives in step 4.

7. ~~**Shader redesign requested by the user (2026-10-14)**~~ ANSWERED: all recommendations accepted, done in step 2b. before Step 2 is committed: the user doubts the
   consteval GLSL reflection ("not what commercial engines do") and wants the shader pipeline rebuilt the
   commercial way, with SPIR-V support. Claude proposed: build-time GLSL→SPIR-V (glslang via CMake),
   SPIR-V as the primary input, **run-time reflection (SPIRV-Cross) as the authority**, validation at
   pipeline creation (existing check logic reused at run time), pipeline layouts explicit or derived from
   reflection, descriptor writes by name (looked up once) or by index, push-constant size checked at run
   time, run-time GLSL compiler Debug-only by default (CMake option for Release), hot reload accepted when
   the new shader still fits the pipeline layouts using it, pipelines rebuilt behind the same handles.
   Removes the consteval reflector, GlslShader/DeclaredShader, TypedShaderModule/TypedPipelineLayout, typed
   Device front doors, write<"name", S>, push_constants<S> and their compile-failure tests; keeps
   Recorder<Q>/RenderingScope. Optional extra asked: build-time generated C++ headers (sokol-shdc style).
   Awaiting the user's decisions.
8. ~~**Parallel rendering requested**~~ ANSWERED (done in step 2b; "frames" read as threads, pool ring added). ("add what is missing for CommandPool (split renderpass across
   frames)"): Claude read "frames" as threads/command buffers (a pass cannot span frames) and proposed
   begin_parallel_rendering() → ParallelRenderingScope (inheritance(), execute(parts)) with a
   RenderingPartRecorder for secondary buffers (draw-state commands only), plus an optional per-thread,
   per-frame-in-flight pool ring reset through a timeline semaphore. Awaiting confirmation.
