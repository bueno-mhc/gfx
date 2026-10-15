# Vulkan backend and SPIR-V infrastructure — execution plan and progress

Last activity: 2026-10-15T16:00:23-03:00
Status: COMPLETE

This file drives a long autonomous task requested by the user on 2026-10-15:

> "Create the Vulkan backend and the SPIR-V infrastructure. For now, don't worry about the render
> surface, it is to be abstracted in a similar way, with backend implementations for SDL, GLFW, etc.
> Do not create code for this render surface or its abstract interface yet. Fully implement Vulkan
> support. If the time limits and credits run out, prepare everything to automatically continue
> execution when the limit is made available."

Context: the user committed step 2c (commit 1bbb792) and **shelved** the later "AZDO enforcement"
rework (`git stash@{0}`): keep the generic step-2c interface (classic descriptor sets, vertex input,
direct draws all stay). Do not touch the stash.

## How to continue (for a resumed or fresh session)

1. Read `.claude/CLAUDE.md` (conventions, toolchain defects, build commands) and this file.
2. **Heartbeat rule:** if `Last activity` above is less than 120 minutes old, another session is
   working: stop immediately without changing anything. Otherwise set it to the current time and
   continue. Update it whenever an item is checked (and at least every ~30 minutes of work).
3. Continue from the first unchecked item. Keep every build green: build with
   `python build.py --test` (Debug) and, before checking a phase off, also `--build-type Release`.
4. Never commit or push (the user commits). Never touch `git stash`.
5. When everything is checked: write `.claude/vulkan-backend-report.md` (what was built, decisions,
   deviations, test results, suggested commit message), set `Status: COMPLETE`, update
   `.claude/CLAUDE.md` (§5, §7, §9), delete the scheduled task `gfx-vulkan-backend-continuation`
   (scheduled-tasks tools) and any in-session cron job, then stop.

## Decisions taken without the user (recorded here and in the final report)

- **Render surface / presentation not implemented** (user instruction): `Device::create_swapchain`,
  `Queue::present` and `PhysicalDevice::surface_support` throw BackendCapabilityError naming the
  missing render-surface abstraction. No surface code, no window code.
- **Handle tables:** lock-free reads, mutex on insert/remove, chunked (4096 slots per chunk), generation
  per slot (`gfx.backends.common` `SlotTable<Record>`). Records are small native structs (never
  pointers to user-owned objects unless the slot dies with the object).
- **Bindless on Vulkan:** descriptor indexing (core 1.2): one update-after-bind, partially bound
  descriptor set; one index allocator per kind; indices freed through the Deleter.
- **Vulkan 1.3 baseline** (dynamic rendering, synchronization2, timeline semaphores, maintenance4,
  extended dynamic state, buffer device address when available). Optional extensions enabled when
  requested and supported: mesh shader (EXT), ray tracing pipeline + acceleration structure + ray query
  (KHR), fragment shading rate (KHR), conservative rasterization (EXT), memory budget (EXT), device fault
  (EXT), NV diagnostic checkpoints / AMD buffer marker (breadcrumbs), debug utils.
- **Memory:** Vulkan Memory Allocator (MSYS2 `include/vma/vk_mem_alloc.h`), implementation in one
  plain C++ translation unit.
- **SPIR-V tools:** SPIRV-Cross **C API** for reflection (all builds), glslang **C API** for run-time
  GLSL/HLSL compilation (Debug builds only; not linked in Release). Build-time compilation with
  `glslangValidator` (`-D` for HLSL); DXC is used for HLSL when `find_program(dxc)` finds it.
- **Modules:** backend = module `gfx.backends.vulkan` (PRIVATE file set): primary interface exports only
  `create_vulkan_instance()`; internal partitions and implementation units include `<vulkan/vulkan.h>`
  and VMA in their global module fragments. (If GCC 16 cannot merge these GMF declarations, fall back
  to non-module `.cpp` translation units that `#include` first and `import` after; record it.)
- **Capture tools:** detection through `vkGetPhysicalDeviceToolProperties`; begin/end capture through the
  RenderDoc in-application API (header fetched with FetchContent, loaded only if RenderDoc is already
  injected); other tools → BackendCapabilityError.

## Architecture (target)

```
source/
  backend_factory.cppm            export module gfx.backend_factory: create_instance(BackendKind, description),
                                  available_backends(); re-exported by `gfx`
  backend_factory.cpp             implementation (imports the enabled backends)
  shaders/shader_tools.cppm       export module gfx.shader_tools: reflect_spirv(), compile_shader() (Debug)
  shaders/spirv_reflection.cpp    module gfx.shader_tools; GMF <spirv_cross/spirv_cross_c.h>
  shaders/shader_compiler.cpp     module gfx.shader_tools; GMF <glslang/Include/glslang_c_interface.h> (Debug)
  backends/common/slot_table.cppm export module gfx.backends.common (PRIVATE)
  backends/null/null_backend.cppm export module gfx.backends.null
  backends/vulkan/vulkan_backend.cppm   export module gfx.backends.vulkan (create_vulkan_instance)
  backends/vulkan/*.cppm          internal partitions (GMF: vulkan.h, vma)
  backends/vulkan/*.cpp           implementation units
  backends/vulkan/vma_implementation.cpp   plain TU, VMA_IMPLEMENTATION
cmake/GfxShaders.cmake            gfx_add_shaders(<target> SHADERS ... [INCLUDE_DIRECTORIES] [DEFINES])
cmake/scripts/GfxEmbedSpirv.cmake  .spv → C++ module with constexpr arrays
tests/backends/vulkan/            GPU tests (skip when no Vulkan device); validation layers on
tests/unit/shaders/               reflection + compiler + gfx_add_shaders tests
```

## Checklist

### Phase A — infrastructure
- [x] A1 Plan file, in-session cron, durable scheduled task (heartbeat rule)
- [x] A2 CMake: backend options (GFX_BACKEND_VULKAN/NULL/D3D12/METAL), Vulkan/VMA/glslang/SPIRV-Cross discovery, DXC optional
- [x] A3 GCC prototype: partition with GMF vulkan.h + implementation unit + import std (decide module vs plain TUs)
- [x] A4 backend_factory + null/d3d12/metal stubs + umbrella export + tests
- [x] A5 SlotTable (thread-safe) + tests (concurrent insert/lookup/remove)

### Phase B — SPIR-V infrastructure
- [x] B1 reflect_spirv (SPIRV-Cross C API) → ShaderLayout (bindings, names, types, counts, block sizes, image formats, push constants, inputs/outputs + formats, workgroup size, specialization constants)
- [x] B2 compile_shader (glslang C API, GLSL + HLSL, defines, includes, Debug only, ShaderCompilationError)
- [x] B3 gfx_add_shaders (glslangValidator/DXC, depfiles, generated module) + tests (reflect the embedded SPIR-V)

### Phase C — Vulkan core
- [x] C1 Instance (layers, debug messenger → debug_callback, validation level), physical devices (properties, limits, features, queue families, format support, tooling info)
- [x] C2 Device (queue family selection, feature chain, VMA, slot tables, bindless set + layout, pipeline cache, debug names)
- [x] C3 Queues (submit2 with binary/timeline semaphores, wait_idle, bind_sparse, present → throws), device-lost reporting
- [x] C4 Fence, Semaphore, TimelineSemaphore, Event
- [x] C5 Deleter (per-queue timeline values)

### Phase D — resources
- [x] D1 Buffer (VMA, persistent mapping, flush/invalidate, device address, bindless), memory requirements, DeviceMemory + placement
- [x] D2 Image, ImageView (bindless), Sampler (bindless), BufferView
- [x] D3 memory_budget, memory_statistics, sparse image requirements

### Phase E — shaders and pipelines
- [x] E1 ShaderModule (validate, Debug compile, reflect, VkShaderModule)
- [x] E2 DescriptorSetLayout, PipelineLayout, DescriptorPool, DescriptorSet
- [x] E3 PipelineCache
- [x] E4 Compute pipeline (specialization, async on task scheduler, status/wait)
- [x] E5 Graphics pipeline (dynamic rendering, full state, mesh/task, tessellation, geometry, VRS, multiview)
- [x] E6 Hot reload (poll_shader_changes, Debug)

### Phase F — commands
- [x] F1 CommandPool, CommandBuffer begin/end (bindless bound), barriers (sync2), copies, fill, update, clears, blit, resolve
- [x] F2 Rendering (begin/end, parallel parts with inheritance), dynamic state, draws (direct, indirect, count, mesh), vertex/index buffers, descriptor sets, push constants
- [x] F3 Dispatch (direct, indirect), events, queries, debug labels, breadcrumbs
- [x] F4 Ray tracing (acceleration structures: sizes, create, build, copy, compaction; RT pipeline; group handles; trace rays)

### Phase G — diagnostics
- [x] G1 Capture tools (tooling info, RenderDoc API)
- [x] G2 Crash diagnostics (checkpoints/buffer markers → DeviceLostError markers, VK_EXT_device_fault → crash report)

### Phase H — GPU tests (real device; skip without one)
- [x] H1 Harness (fixture, VK_ADD_LAYER_PATH, validation errors fail the test)
- [x] H2 Instance/devices, buffers (map/write/read), compute end-to-end with bindless storage buffers
- [x] H3 Graphics offscreen (indirect triangle → readback), parallel rendering, uploader + transient allocator, timestamps (GpuProfiler)
- [x] H4 Cross-queue timelines (compute → graphics), pipeline validation errors, hot reload, async pipelines
- [x] H5 Ray tracing and mesh shading (when supported)

### Phase I — wrap-up
- [x] I1 README, launch.json, CLAUDE.md, clean Debug + Release runs, report, remove scheduled jobs

## Log

- 2026-10-15: plan written.
- 2026-10-15: A1 done: in-session cron 58113e17 (hourly :17, session-only, 7-day expiry) and scheduled task gfx-vulkan-backend-continuation (every 3 h at :41, Auto mode, runs only when the heartbeat is older than 120 min).
- 2026-10-15: A2 done: options GFX_BACKEND_{VULKAN,NULL,D3D12,METAL}; gfx_require_{vulkan_headers,volk,vulkan_memory_allocator,spirv_cross,glslang} (system first, FetchContent otherwise); volk (fetched) loads Vulkan at run time; C language enabled for volk.
- 2026-10-15: A3 done: the backend is a C++ module (gfx.backends.vulkan): internal partitions and implementation units include volk.h in their GMF and import std - GCC 16 builds and runs it (no plain-TU fallback needed). A4 done: gfx.backend_factory (backend_availability, available_backends, create_instance), null backend = instance without physical devices, Direct3D 12 / Metal stubs report 'not implemented yet'; CMake option renamed GFX_BACKEND_DIRECT3D12.
- 2026-10-15: A5 done: gfx_slot_table library (module gfx.backends.slot_table, std only: lock-free lookups, chunked, FIFO reuse past a threshold) + gfx.backends.handle_table (typed wrapper, InvalidHandleError). Phase A green: 151 tests Debug + Release.
- 2026-10-15: B1-B3 done: gfx.shader_tools (reflect_spirv via SPIRV-Cross C API; compile_shader via glslang C API in Debug only, GLSL+HLSL, defines, includes, debug info, included_files for hot reload), cmake/GfxShaders.cmake gfx_add_shaders() (glslangValidator, depfiles, generated module with <name>_spirv arrays and ready ShaderModuleDescription constants carrying source path/defines/includes in Debug). DXC: not installed here, HLSL goes through glslang at build and run time (DXC support deferred, recorded as deviation).
- 2026-10-15: FIX: build.py passed compiler names (gcc/g++) that differed from the cached paths (cc.exe), CMake wiped the cache and the re-run lost -DCMAKE_BUILD_TYPE=Release, so earlier 'Release' runs were Debug. build.py now passes absolute compiler paths and fails if the cache holds another build type. Real Release run: 171/171 pass, Release binaries do not link glslang.
- 2026-10-15: C1 done: VulkanInstance (1.3 loader check, validation layers on request with a descriptive error when missing, debug utils messenger -> debug_callback, portability enumeration) and VulkanPhysicalDevice (properties/limits/features of 1.1-1.3 + RT/AS/ray query/mesh/FSR/fault, sorted extensions, supports() for every DeviceFeature, format_support via VkFormatProperties3, surface_support throws). Translation partition. -Wno-missing-field-initializers for the Vulkan backend files only. GPU test harness tests/backends/vulkan (validation messages fail tests; loader notes are printed). H1 started.
- 2026-10-15: C2 done: VulkanDevice (automatic cost-free features + opt-in RT/mesh/VRS/conservative/sparse/crash markers, missing features listed at once; queue families: dedicated compute/transfer when present, requests beyond a family's queue count share native queues; volk device table; VMA with BDA+budget; default pipeline cache; leak report at destruction), BindlessTable (UAB partially-bound set, index allocator, deferred index release), descriptor set/pipeline layouts, VulkanQueue submit (progress timeline per native queue), VulkanDeleter. 16 Vulkan tests pass on NVIDIA and Intel with validation.
- 2026-10-15: C3-C5 done: submit2 with binary+timeline waits/signals (validated command buffer state, level, queue type), wait_idle on the progress timeline, present throws (render surface), Fence/Semaphore/TimelineSemaphore/Event/QueryPool (host-reset at creation); deleter keyed on every native queue's progress (test: deletion waits for a blocked compute queue). bind_sparse moved to D3, device-lost breadcrumbs to G2. 24 Vulkan tests pass on NVIDIA and Intel. TOOLCHAIN DEFECT 7: GCC 16 ICE (segfault) at the end of the mem-initializer list of some out-of-class constructors (VulkanFence, VulkanQueryPool; not VulkanEvent of the same shape) when the class comes from an internal partition's BMI; workaround: classes only one implementation unit needs are declared in that unit (no partition). Not minimized yet.
- 2026-10-15: D1-D3 done: VulkanDeviceMemory (dedicated, CAN_ALIAS, mapped when host-visible), VulkanBuffer (own VMA allocation persistently mapped / placed via vmaCreateAliasingBuffer2 with placement checks / sparse; flush+invalidate; device address; bindless storage index), VulkanImage (format/usage/limits validated with vkGetPhysicalDeviceImageFormatProperties2; transient prefers lazily allocated; placed; sparse), VulkanImageView (REMAINING resolved, non-array views see 1 layer, cube 6; depth aspect by default; bindless sampled/storage), VulkanSampler (anisotropy, minmax, bindless), VulkanBufferView, memory_budget (VMA budgets), memory_statistics, sparse_image_requirements, Queue::bind_sparse (timeline waits/signals + progress). INTERFACE ADDITION: BufferDescription::sparse. Physical device 0 is the Intel GPU (one queue per family), the default selection is NVIDIA. 37 Vulkan tests pass on both.
- 2026-10-15: Style signal: the user reformatted vulkan_bindless.cpp (one parameter per line in definitions, a comment before each step, blank lines between steps, multi-line bit-or); follow it in new code and adapt the other Vulkan files at wrap-up.
- 2026-10-15: E1-E6 done: VulkanShaderModule (validated, Debug compile, reflect, stage features), descriptor pools/sets (variable count, exhaustion -> OutOfMemoryError, typed writes incl. texel buffers and acceleration structures), pipeline caches (header checked against the device), compute/graphics/ray tracing pipelines (PipelineState read by commands; async on the task scheduler; validators at CREATION), hot reload (source + includes watched; SPIR-V modules learn includes with the new shader_dependencies() preprocessor pass; reload validates every dependent pipeline before swapping; rebuilds behind the same handles). INTERFACE ADDITIONS: BufferViewDescriptor, AccelerationStructureDescriptor in DescriptorResource. DECISION: debug info -g instead of -gVS (glslang 16.2 writes invalid NonSemantic debug info for vertex shaders). 52 Vulkan tests pass on both GPUs.
- 2026-10-15: F1-F3 done: VulkanCommandPool (native pool shared with its buffers, destroyed after the last), VulkanCommandBuffer: every record_* (sync2 barriers with stages masked to the queue and accesses filtered to their stages, ownership transfers, copies/fill/update/clears/blit/resolve, bindless table bound at begin for every bind point, dynamic rendering with parallel parts (inheritance checked at execute), draws incl. indirect count and mesh, dynamic state, queries, debug labels), recording failures poison the command buffer (submit refuses it). F4 code written (acceleration structures + trace rays). KNOWN LIMITATION: on devices with one queue (Intel UHD) every queue type shares one native queue: a submission waiting for a value another 'queue' signals later deadlocks (tests skip there). 69 Vulkan tests pass on NVIDIA, 67 + 2 skipped on Intel.
- 2026-10-15: F4 done: acceleration structures (build sizes, create in buffer storage, bindless top levels, builds of triangles/boxes/instances, copy incl. compaction, compacted-size queries), RT pipelines + shader binding tables + trace_rays, ray queries in compute: 5 tests pass on NVIDIA (skip on Intel: no RT). Buffers for ray tracing are allocated with the alignments their addresses need (vmaCreateBufferWithAlignment).
- 2026-10-15: G1-G2 done: capture_tool() (RenderDoc in-application API when injected; driver tool properties for Nsight/PIX), begin/end_capture through renderdoc_app.h (fetched, v1.45) in a plain TU (windows.h) - real capture verified by CTest VulkanCapture.RenderDoc (renderdoccmd injects RenderDoc; .rdc written). Breadcrumbs = NV diagnostic checkpoints on debug labels (nodes kept alive by the deleter); device loss -> DeviceLostError with the deepest active label chain and, with VENDOR_REPORT, a VK_EXT_device_fault text+binary report. GPU_CRASH_MARKERS now means NV checkpoints only (AMD buffer markers not implemented: not reported as supported). Device loss itself is not triggered by tests (would hang the GPU).
- 2026-10-15: H1-H5 done: frame_resources_test (FrameTimeline + FrameCommandPools + Uploader + TransientBufferAllocator + GpuProfiler over 5 frames, 2 in flight; image uploads with regions; profiler scope ordering), mesh_test (mesh only, task+mesh, indirect, indirect count; Intel skips), hot reload tests now dispatch before/after the reload (same pipeline handle runs the new code). Decision: HOST_UPLOAD and DEVICE_LOCAL_HOST_VISIBLE memory is required HOST_COHERENT (the transient ring and the uploader write mapped memory without flushing; Vulkan guarantees a HOST_VISIBLE|HOST_COHERENT type).
- 2026-10-15: Phase H verified: Debug 260/260 (2 skips: Release-only compiler test, RenderDoc gtest outside renderdoccmd), Release 260/260 (Debug-only compiler/hot-reload tests skip). Starting I1 wrap-up.
- 2026-10-15: I1 done: restyle (one parameter per line in definitions, blank line before step comments; CRLF kept), README (status, factory example compiled, gfx_add_shaders, Vulkan backend table, options, layout, GPU test variables), launch.json (4 new test executables), CLAUDE.md (header, 5, 6, 7, 8, 9), report vulkan-backend-report.md. Clean runs from scratch: Debug 260/260, Release 260/260 (RenderDoc capture included). Status COMPLETE; scheduled task and cron job deleted.
- 2026-10-15: User validation: all decisions accepted as recommended; shared-hardware-queue limitation documented (Queue Doxygen, README) and detected: Queue::submit/bind_sparse refuse a later signal of exactly the value of an unresolved wait on the same hardware queue (InvalidUsageError same queue, BackendCapabilityError across queues sharing hardware); 3 new tests, cross-queue command test now runs on Intel. GCC defect 7 minimized (member of an imported class template default-initialized by a constructor defined in an implementation unit, class from an internal partition) with detector CompilationFailure.MemberInitializerOfPartitionClassCrashesCompiler.
