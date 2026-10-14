# gfx — project memory

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
| Code style (user's reformat) | Allman braces everywhere (namespace, class, function, if/for/try/catch, even one-line bodies); out-of-class definitions put `auto` on its own line (`auto\nError::backend() const noexcept -> ...`); one parameter per line when several; leading-comma member-initializer lists; short comments describing each step inside function bodies; friends in their own `private:` section before data members. Tests: `TEST(Suite, Name)` then `{` on the next line. |

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
- **No backend is implemented for now** (user decision): Vulkan, Null, D3D12 and Metal backends are stubs
  that throw descriptive "not implemented yet" errors. Only Vulkan infrastructure (packages) is prepared.
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
- **Compile-time shader layout** (step 2): `ShaderLayout` literal fixed-capacity value (stage, bindings
  with set/binding/type/count/block size/image format/name, stage inputs/outputs, push-constant size,
  workgroup size); produced by consteval `reflect_glsl(source, stage)` (GLSL subset: layout-qualified
  globals, UBO/SSBO/push-constant blocks with exact std140/std430 sizes incl. structs, arrays and runtime
  arrays, opaque types, stage I/O, local_size; no preprocessor) or by a consteval builder for declared
  layouts. Shader definitions `GlslShader<SOURCE, STAGE>` / declared → concept `ShaderDefinition`.
  Checks: `check_layout()` (one constexpr function; `if consteval` selects compile error vs
  ShaderLayoutMismatchError), `CompatibleWith<S, Layout>` (GCC prints the full LayoutCheck value on
  failure), `PushConstantsFor<T, S>`, `binding_of<"name", S>()`, vertex inputs vs VertexLayout,
  fragment outputs vs attachments, `pipeline_layout_from<...>()` merge. At ShaderModule creation the
  real SPIR-V is reflected (SPIRV-Cross) and compared with the compile-time layout. Prototype validated
  with GCC 16 (session-history §2).
- **Shaders**: GLSL only, compiled to **SPIR-V**; sources inline raw strings **and** CMake-embedded
  `.glsl` files (`gfx_embed_shaders()`), step 4.
- **Shader hot reload** (step 4, user approved recommendations): backend-independent GLSL→SPIR-V compiler
  (glslang + SPIRV-Cross reflection) in the library; reload triggered by the engine calling something
  like `poll_shader_changes()` (no hidden threads); a reload whose layout differs from the compile-time
  layout is rejected (old shader kept, error reported); **Debug only — forced off in Release**.
- **Parallelism**: queues first-class (QueueRequest per type, fallback when no dedicated family);
  CommandPool per thread × queue family (externally synchronized, lock-free recording);
  `Recorder<QueueType Q>` zero-cost typed wrapper with requires-constrained methods (draw does not
  compile on a compute recorder); Queue::submit(batches) non-blocking with timeline semaphore
  waits/signals; binary semaphores only for swapchain; per-queue mutex; Deleter keyed on per-queue
  timeline progress.
- **Render graph enablement**: sync2-style barriers (memory/buffer/image, queue ownership transfer),
  ResourceState table, dynamic rendering via RAII scope, attachment descriptions, memory requirements +
  placed resources (aliasing), transient attachments, concurrent sharing, timestamp/statistics queries.
- **Modules**: `gfx` umbrella re-exports `gfx.enums`, `gfx.errors`, `gfx.handles` (and later
  `gfx.interface`, `gfx.backend_factory`). Interfaces: module `gfx.interface` with partitions in
  `source/interface/`. Backends: `gfx.backends.<name>` in PRIVATE file sets.
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

## 7. Repository layout (current)

```
gfx/
├── CMakeLists.txt            orchestrates cmake/ modules; add_subdirectory(source, tests)
├── CMakePresets.json         debug/release (Ninja, MSYS2 PATH via ${pathListSep}), test jobs ""
├── build.py                  root (user moved it back from builder/); python build.py [--test]
├── .vscode/                  tasks.json, launch.json, settings.json (see §8)
├── .claude/                  this memory
├── cmake/  GfxImportStandardLibrary, GfxToolchainRequirements, GfxProjectOptions (+workarounds),
│           GfxRuntimeDeployment, GfxDependencies, GfxTesting, scripts/GfxCheckPlatformMacroCollisions
├── source/ CMakeLists.txt, gfx.cppm, enums.cppm, errors.cppm/.cpp, handles.cppm,
│           interface/ (34 partitions of gfx.interface), include/gfx/windows_headers.hpp
└── tests/  toolchain/ (+defects/), unit/{enums,errors,handles,interface,shader_layout,platform}/,
            compilation/{handles,enums,interface,shader_layout}/
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

## 9. Status

| Step | Content | State |
|---|---|---|
| 0 | Toolchain, CMake/presets/build.py, GoogleTest, toolchain tests | done, committed (ffae3d2, 0c8af25) |
| 1 | enums, errors, handles, unit/compilation/macro tests, workarounds | done, committed (8b750f7, 58bbcfc) |
| — | VS Code debugging, preset PATH fix, POST_BUILD discovery | committed (bafcb25) + later fixes |
| 2 | Interfaces, descriptions, Recorder<Q>, compile-time shader layouts | **done, awaiting validation** (102 tests) |
| 3 | backend_factory + Vulkan/Null/D3D12/Metal stubs throwing "not implemented yet" | next |
| 4 | CMake shader embedding, GLSL→SPIR-V compiler, hot reload (Debug only) | |
| 5 | Samples (headless + windowed) — scope to confirm, no backend is implemented | |
| 6 | Final README, architecture overview **with diagrams** of the code as built | |

Test count at last report: **102**, all passing in Debug and Release (from scratch).

### Step 2 as built (details for the next session)

- `source/interface/`: module `gfx.interface`, 34 partitions: common_types (Version, Extent, Offset,
  Rectangle, Viewport, ImageSubresourceRange/Layers, ComponentMapping, MemoryRequirements,
  MemoryPlacement, NativeWindow), backend_object (BackendObject : HandleIssuer; backend(),
  debug_name(), throw_unsupported(), throw_not_implemented()), shader_layout, layout_compatibility,
  synchronization (GlobalMemoryBarrier/BufferBarrier/ImageBarrier/BarrierBatch, buffer_barrier(),
  image_barrier(), needs_barrier(), semaphore waits/signals), commands (copy regions, draw arguments,
  ColorAttachment, DepthStencilAttachment, RenderingDescription), one partition per object (25 classes),
  recorder, typed_objects (TypedShaderModule<S>, TypedPipelineLayout<DEFINITION>), interface.cppm.
- Rules applied: **non-noexcept operations** are pure virtual with an out-of-class throwing definition;
  **noexcept accessors** (handle(), size(), ...) are plain pure virtual (a noexcept body cannot throw).
  Every description member has a default initializer (GCC -Wmissing-field-initializers otherwise).
- CommandBuffer recording primitives (`record_*`) are **protected**; Recorder<Q> and RenderingScope<Q>
  are friends. Recorder: copies/barriers/queries/labels on every queue; compute pipeline, compute
  descriptor sets, push constants, dispatch, events, begin/end_query need COMPUTE capability;
  begin_rendering needs GRAPHICS and returns RenderingScope (viewport, scissor, graphics pipeline,
  graphics descriptor sets, push constants, vertex/index buffers, draws). Runtime check at Recorder
  construction: has_all(queue_type_capabilities(buffer.queue_type()), CAPABILITIES).
- Device typed front doors (member templates): create_shader<S>(), create_pipeline_layout<DEFINITION>()
  (one set layout per set index), create_compute_pipeline(layout, shader), create_graphics_pipeline<
  VERTICES, ATTACHMENTS>(layout, vertex, fragment, state) — each static_asserts LayoutDiagnostic<check>.
- DescriptorSet::write<"name", Shader>(resource): binding resolved and resource type checked at compile
  time (DescriptorResourceOf<Type>); the set index is not checked. Texel buffers and acceleration
  structures cannot be written yet (no BufferView object).
- Tests: tests/unit/shader_layout, tests/unit/interface (fake backend module `gfx.tests.fake_backend`),
  10 new compilation-failure tests, macro check now scans every .cppm of source/ (837 names).
- Findings: default arguments are not inherited by overrides — call backends through interface
  references; MinGW `<windows.h>` defines min/max only for C, not C++ (MSVC defines them in C++).

## 10. Open questions (asked, not answered yet)

1. Keep `import std` + `--allow-multiple-definition`, or switch modules to global-module-fragment
   `#include`s (recommended by Claude: avoids the linker flag and the experimental CMake gate)?
2. Handle tags as `struct` (current) or `class`?
3. Did "builder folder" mean the build **output** directory (currently `build/`)?
4. Add a Doxygen HTML target (needs the MSYS2 `doxygen` package)?
5. (Step 2) Render-graph gaps found: secondary command buffers recorded **inside** a dynamic rendering
   pass need inheritance information (attachment formats) — needed to record one pass on several
   threads; no BufferView object for texel buffers. Add them?
6. (Step 2) Value types named `Extent`/`Offset`/`Rectangle` (no `2D`/`3D` suffix, per the no-abbreviation
   rule; Extent has depth = 1 by default). OK?
