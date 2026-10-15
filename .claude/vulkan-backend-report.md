# Vulkan backend and SPIR-V infrastructure: final report

Task requested on 2026-10-15 and completed the same day, over several automatically continued
sessions. The request:

> "Create the Vulkan backend and the SPIR-V infrastructure. For now, don't worry about the render
> surface [...] Do not create code for this render surface or its abstract interface yet. Fully
> implement Vulkan support."

Nothing is committed: HEAD is still `1bbb792` (step 2c), and `stash@{0}` (the shelved AZDO rework) was
not touched. The plan with its full log is [vulkan-backend-plan.md](vulkan-backend-plan.md).

## 1. What was built

### Infrastructure
- **CMake options** `GFX_BACKEND_VULKAN` (ON), `GFX_BACKEND_NULL` (ON), `GFX_BACKEND_DIRECT3D12`
  (ON on Windows), `GFX_BACKEND_METAL` (ON on Apple) → `GFX_WITH_<NAME>_BACKEND`. The C language is
  enabled for volk.
- **Dependencies** (`cmake/GfxDependencies.cmake`, system package first, FetchContent otherwise):
  Vulkan headers, volk (always fetched, `vulkan-sdk-1.4.350.0`), VMA 3.4, SPIRV-Cross (C API),
  glslang, and RenderDoc's `renderdoc_app.h` (fetched, v1.45).
- **build.py** passes absolute compiler paths and fails when the build directory's cache holds
  another build type. This fixes a cc.exe/gcc mismatch that silently made "Release" runs Debug.
- **Backend factory** (`gfx.backend_factory`, re-exported by `gfx`): `backend_availability()`,
  `available_backends()`, `create_instance(BackendKind, InstanceDescription)`.
- **Null backend**: an instance without physical devices. **Direct3D 12 / Metal**: stubs that throw
  "not implemented yet".
- **Handle tables**: the `gfx_slot_table` library provides `SlotTable`, with lock-free lookups, a mutex
  on insert and remove, chunks of 4096 slots and FIFO reuse past a threshold. `HandleTable<Tag, Record>`
  wraps it with typed handles and throws `InvalidHandleError`.

### SPIR-V infrastructure (`gfx.shader_tools`)
- `reflect_spirv()` (SPIRV-Cross C API) builds a `ShaderLayout` with:
  - bindings with names, types, counts, block sizes and image formats;
  - push constants;
  - inputs and outputs with formats;
  - workgroup size;
  - specialization constants.
- `compile_shader()` and `shader_dependencies()` use the glslang C API in **Debug builds only** (Release
  does not link glslang). They handle GLSL and HLSL, defines, include directories and debug info, and
  return the included files for hot reload.
- `gfx_add_shaders()` (`cmake/GfxShaders.cmake`) compiles with glslangValidator at build time:
  - uses depfiles, so shaders rebuild when an include changes;
  - adds `-g` in Debug;
  - generates a C++ module with `<name>_spirv` arrays and ready `ShaderModuleDescription` constants, which carry the source path, defines and include directories in Debug.

### Vulkan backend (`source/backends/vulkan/`, module `gfx.backends.vulkan`)
| Area | Built |
|---|---|
| Instance | Vulkan 1.3 loader required, validation layers on request (descriptive error when missing), debug messenger → `debug_callback`, portability enumeration |
| Physical devices | properties and limits (1.1–1.3, RT, mesh, FSR, fault), features, queue families, `supports()` for every `DeviceFeature`, format support (VkFormatProperties3), tool properties |
| Device | queue family selection (dedicated compute/transfer when present; extra requests share native queues), automatic cost-free features, opt-in features (missing ones listed together), VMA, handle tables, bindless table, default pipeline cache, debug names, leak report at destruction |
| Queues | submit2 with binary and timeline semaphores (command buffer state, level and queue type checked), `wait_idle`, `bind_sparse`; a progress timeline per native queue |
| Synchronization | Fence, Semaphore, TimelineSemaphore, Event, QueryPool; deleter keyed on every native queue's progress |
| Resources | DeviceMemory, Buffer (persistently mapped, placed/aliased, sparse, device address, bindless), Image (validated against the format's properties, transient, placed, sparse), ImageView, Sampler, BufferView; memory budget and statistics; sparse image requirements |
| Shaders and pipelines | ShaderModule (validate, Debug compile, reflect, stage features), descriptor set/pipeline layouts, descriptor pools and sets (texel buffers, acceleration structures), PipelineCache, compute/graphics/ray tracing pipelines (specialization, background compilation on the task scheduler, status/wait), hot reload behind the same handles |
| Commands | every `record_*`: synchronization2 barriers (stages masked to the queue, accesses filtered to their stages), ownership transfers, copies, fills, updates, clears, blits, resolves, mipmaps, dynamic rendering with parallel parts (secondary buffers with inheritance), every draw (direct, indirect, count, mesh), dynamic state, dispatch, queries, events, debug labels; a stale handle poisons the recording |
| Ray tracing | acceleration structures (sizes, create, build, copy, compaction), RT pipelines, shader binding tables, `trace_rays`, ray queries |
| Diagnostics | capture tool detection, RenderDoc captures from code, breadcrumbs (NV checkpoints on debug labels), `DeviceLostError` with the active label chain, `VK_EXT_device_fault` report files |

### Tests
- **Unit tests**: `slot_table` (8, including real threads), `backend_factory` (5), `shader_tools` (reflection, compiler, `gfx_add_shaders`).
- **`tests/backends/vulkan/`**: 90 tests on the machine's real GPUs, with validation layers. Any validation error fails the test.
  - Areas covered: instance, device, synchronization, resources, pipelines, hot reload (dispatches before and after the reload), commands, ray tracing, mesh shading, diagnostics, and the frame helpers on Vulkan.
  - Frame-helper tests run 5 frames with 2 in flight through FrameTimeline, FrameCommandPools, Uploader, TransientBufferAllocator and GpuProfiler.
- **CTest `VulkanCapture.RenderDoc`** (when RenderDoc is installed) runs a test under `renderdoccmd`. The test captures its own work and checks that the `.rdc` file was written.

## 2. Decisions taken without the user

All of them were **accepted by the user on 2026-10-15** ("do as recommended").

1. **Presentation is not implemented** (your instruction). `create_swapchain`, `Queue::present` and `surface_support` throw `BackendCapabilityError` naming the missing render-surface abstraction.
2. **volk** loads Vulkan at run time, with one device function table per device. Debug-utils functions use the instance-level pointers, which are not in the device table.
3. **The backend is a C++ module** (internal partitions + implementation units with `volk.h` in their global module fragments). GCC 16 handles it, apart from defect 7 below.
4. **Features**: everything that costs nothing is enabled automatically. Ray tracing, ray queries, mesh shaders, variable-rate shading, conservative rasterization, sparse resources and crash markers are enabled only on request.
5. **GPU_CRASH_MARKERS means NVIDIA diagnostic checkpoints only.** AMD buffer markers are not implemented, so AMD GPUs report the feature as unsupported rather than accepting it silently.
6. **Shader debug info `-g`, not `-gVS`.** glslang 16.2 writes invalid NonSemantic debug info for vertex shaders.
7. **HLSL goes through glslang** at build and run time; DXC support is deferred (DXC is not installed here).
8. **CPU-written memory is host-coherent.** HOST_UPLOAD and DEVICE_LOCAL_HOST_VISIBLE memory is required HOST_COHERENT, because TransientBufferAllocator and Uploader write mapped memory without flushing. Vulkan guarantees such a memory type.
9. **Several gfx queues may share one native queue** when a family has fewer queues than requested. Each native queue has a mutex and a progress timeline.
10. **Classes needed by a single implementation unit are declared in that unit**, not in a partition (workaround for GCC defect 7).
11. **Style.** You reformatted `vulkan_bindless.cpp`; its mechanical rules were applied to every new source file with a script:
    - one parameter per line in function definitions with several parameters;
    - a blank line before each step comment.

    Comment density stays at one comment per step, not per statement. Bitwise-OR chains keep `|` at the end of lines.

## 3. Interface changes (made while implementing)

- `BufferDescription::sparse` (sparse buffers need the flag at creation).
- `BufferViewDescriptor{view}` and `AccelerationStructureDescriptor{structure}` in the
  `DescriptorResource` variant (classic descriptor sets could not hold texel buffers or acceleration
  structures).

## 4. Deviations and limitations

- **Shared hardware queues** (user's choice: document it, detect it and throw). On a GPU with a single queue (the Intel UHD here), every queue type runs on that queue. A submission that waits for a value only a later submission to the same hardware queue signals would deadlock.
  - Documented on `Queue` (Doxygen: "Order of waits and signals") and in the README.
  - Detected by `Queue::submit` and `Queue::bind_sparse` at `ValidationLevel::CREATION` and above (Release included). Each timeline semaphore tracks the largest value submitted batches promise. Each hardware queue keeps the waits nothing had signaled or promised when they were submitted; they are pruned once reached. The batches are checked in order (a batch's waits before its signals) before anything is submitted.
  - Refused: a later submission to the same hardware queue that signals **exactly** the value of such a wait. That is the certain case: anything else releasing the wait first would make the signal decrease the semaphore, which Vulkan forbids. A greater value is accepted, because a smaller value from the CPU or another queue may release the wait first; "wait for N, signal N+1" chains are common and valid.
  - The error is `InvalidUsageError` when the wait and the signal come from one gfx queue, and `BackendCapabilityError` when they come from two queues sharing a hardware queue. It names the semaphore, the value, both queues and the hardware queue.
  - Tests: `SignalsQueuedBehindTheirOwnWaitAreRefused`, `WaitsAndSignalsInOneSubmissionAreCheckedInBatchOrder`, `QueuesSharingAHardwareQueueRefuseWaitsSubmittedBeforeTheirSignals` (runs on every GPU: it requests more graphics queues than the family has). `ComputeAndGraphicsQueuesSynchronizeThroughTimelines` now also runs on the Intel GPU, submitting the signal first there.
  - Not detected: longer cycles through other semaphores or queues (A waits for B, which waits for A's later work).
- **Device loss is not tested**: triggering it would hang the GPU. The report path (breadcrumbs, fault report) is exercised only up to its inputs.
- **Not done**: DXC, a shipped shader header declaring the bindless arrays (the tests have their own `bindless.glsl`), AMD buffer markers, presentation.
- **Toolchain defect 7** (GCC 16 ICE) is now minimized, with detector `CompilationFailure.MemberInitializerOfPartitionClassCrashesCompiler` (5 small files in `tests/toolchain/defects/member_initializer_*`). The trigger: a constructor defined in a module implementation unit, for a class declared in an internal partition, default-initializes a member of a class template specialization from another module (`FenceHandle m_handle;`). Writing `FenceHandle m_handle{};` in the class avoids it, as does the workaround in use (declaring the class in its implementation unit).

## 5. Test results (2026-10-15)

| Run | Result |
|---|---|
| Debug (after the validation follow-up) | **264/264 passed** (2 skipped by design) |
| Release (after the validation follow-up) | **264/264 passed** (14 skipped by design) |
| `VulkanCapture.RenderDoc` (CTest, under renderdoccmd) | passed in Debug and Release |
| Vulkan tests, NVIDIA RTX 5060 Laptop | 89 passed, 1 skipped (the RenderDoc test, which runs under CTest `VulkanCapture.RenderDoc`) |
| Vulkan tests, Intel UHD (`GFX_TEST_PHYSICAL_DEVICE=0`) | 81 passed, 9 skipped: mesh shading (3), ray tracing (4), `DeletionsWaitForEveryQueue` (needs two hardware queues), RenderDoc (1) |

Debug skips by design: `ShaderCompilerTest.ReleaseBuildsDoNotCompileSource` and the RenderDoc gtest
outside renderdoccmd. Release skips by design: the Debug-only compiler and hot-reload tests.

## 6. Final clean runs

Both build directories were deleted and rebuilt after the wrap-up (restyle, README, CLAUDE.md): Debug
and Release 260/260. After the validation follow-up (deadlock detection, defect 7 detector), the
incremental Debug and Release runs pass 264/264, the RenderDoc capture included.

## 7. Automatic continuation removed

The scheduled task `gfx-vulkan-backend-continuation` and the in-session cron job `58113e17` were
deleted when the plan was marked COMPLETE. `.claude/heartbeat.py` and the plan stay in the repository
as a record; delete them before committing if you do not want them versioned.

## 8. Suggested commit message

```
Vulkan backend and SPIR-V infrastructure

Backend factory (create_instance by BackendKind), null backend, Direct3D 12
and Metal stubs, thread-safe handle tables (SlotTable, HandleTable).

SPIR-V tools: reflect_spirv (SPIRV-Cross), compile_shader (glslang, Debug
builds only), gfx_add_shaders() compiling GLSL/HLSL at build time into a
generated module of ShaderModuleDescription constants.

Vulkan backend (volk, VMA): instance, physical devices, devices with
automatic and opt-in features, queues sharing native queues, timeline
deleter, bindless descriptor table, resources (placed, sparse), pipelines
(cache, background compilation, hot reload), synchronization2 command
recording with parallel rendering passes, ray tracing, mesh shading,
RenderDoc captures, breadcrumbs and device fault reports. Presentation
waits for the render-surface abstraction.

Interface: BufferDescription::sparse, BufferViewDescriptor and
AccelerationStructureDescriptor descriptors. Upload memory is host-coherent.

Submissions that would deadlock a hardware queue (a later signal of exactly
the value an unresolved earlier wait waits for) are refused with an error.

GCC 16 defect detector: MemberInitializerOfPartitionClassCrashesCompiler.

Tests on real GPUs with validation layers (90 Vulkan tests).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
