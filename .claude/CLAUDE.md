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
6. **A capturing lambda in an in-class constructor of a class declared in one module partition and
   defined in another** (RenderingPartRecorder: `:command_buffer` declares it as a friend) makes the
   defining partition's BMI unreadable ("failed to read compiled module cluster N: Bad file data").
   Workaround: private static `[[noreturn]]` helper (`RenderingPartRecorder::reject`). Detector:
   `CompilationFailure.LambdaInConstructorCorruptsModuleFile` (tests/toolchain/defects, 4 small files;
   `gfx_add_compilation_failure_test` gained a `MODULES` argument for it). Without the cross-partition
   declaration or without the lambda, the BMI is fine.

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
| 2 | Interfaces, descriptions, Recorder<Q>, shader layouts; **2b**: run-time SPIR-V-reflected layouts, Debug-only reload API, parallel rendering passes, FrameCommandPools; **2c**: production interface (bindless/AZDO, validation levels, RT, mesh, GPU-driven, caches, frame resources, diagnostics) | **done, awaiting validation** (138 tests, Debug + Release) |
| 3 | backend_factory + Vulkan/Null/D3D12/Metal stubs + thread-safe handle table (slot table design with the user) | next |
| 3b | **Real Vulkan backend** (VMA, sync2, dynamic rendering, bindless descriptor indexing, pipeline cache, breadcrumbs) | user: "the next step is to build the Vulkan layer" |
| 4 | Shader toolchain: `gfx_add_shaders()` (glslang + DXC → SPIR-V), SPIRV-Cross reflection, Debug run-time GLSL/HLSL compile + hot reload, shader headers declaring the bindless arrays | |
| 5 | Samples (headless + windowed) — scope to confirm, no backend is implemented | |
| 6 | Final README, architecture overview **with diagrams** of the code as built | |

Test count at last report: **138**, all passing in Debug and Release (from scratch), 2026-10-14.

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
