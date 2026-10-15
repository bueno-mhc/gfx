# gfx

A C++23 graphics rendering abstraction that models Vulkan's object model (instance, physical device,
device, queues, resources, commands, synchronization) above concrete backends. It is written with
C++ modules and strongly typed numeric handles, is **bindless by default**, and follows **AZDO**
(Approaching Zero Driver Overhead) principles throughout.

> **Status:** the interface is complete: objects, typed recording, bindless resources, parallel
> rendering passes, ray tracing, mesh shading, GPU-driven drawing, pipeline caches, frame resources,
> profiling and crash diagnostics. The **Vulkan backend** implements all of it except presentation,
> which waits for the render-surface abstraction (windows through SDL, GLFW, ...). The SPIR-V
> toolchain (build-time compilation, reflection, Debug-only run-time compilation and hot reload) is
> in place. The Direct3D 12 and Metal backends are stubs that throw "not implemented yet".

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

Every function the backend cannot provide on a given GPU throws `BackendCapabilityError`, naming the
missing feature and how to get it. Swapchains, presentation and surface queries throw until the
render-surface abstraction exists.

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

| CMake option | Default | Backend |
|---|---|---|
| `GFX_BACKEND_VULKAN` | `ON` | Vulkan (implemented) |
| `GFX_BACKEND_NULL` | `ON` | Null: an instance without physical devices |
| `GFX_BACKEND_DIRECT3D12` | `ON` on Windows | Direct3D 12 (stub) |
| `GFX_BACKEND_METAL` | `ON` on Apple platforms | Metal (stub) |

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
├── docs/                 design documents (docs/platform-design.md: windowing and input)
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
│   └── platform/         the platform library: windowing and input (`import gfx.platform;`, in progress)
│       ├── enums.cppm    every platform enumeration and constant (keys as USB HID usages, key codes,
│       │                 buttons, axes, modifiers, cursors, window modes and flags, event types)
│       ├── handles.cppm  window, monitor, cursor and device handles; action and binding-context
│       │                 identifiers ("jump"_action); player slots
│       ├── interface/    module gfx.platform.interface: events and their queue, images, monitors,
│       │                 Cursor, Window, InputDevices, Platform (event pump, watchers, threads)
│       └── backend_factory.cppm/.cpp  create_platform(PlatformBackendKind)
└── tests/
    ├── toolchain/        toolchain conformance; defects/ proves each workaround is still needed
    ├── core/             gfx.core (imports gfx.core only) and the slot table
    ├── graphics/<area>/  GoogleTest unit tests of the graphics library, one subfolder per area;
    │                     graphics/vulkan: the Vulkan backend on the machine's real GPUs
    ├── platform/         the platform library, through a test-only fake platform
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
