# gfx

A C++23 graphics rendering abstraction that models Vulkan's object model (instance, physical device,
device, queues, resources, commands, synchronization) above concrete backends. It is written with
C++ modules, strongly typed numeric handles and compile-time validated shader layouts.

> **Status:** step 2: enumerations, errors, typed handles, every interface, typed command
> recording, parallel rendering passes and run-time validated shader layouts. Backends (step 3) and
> the shader compiler and hot reload (step 4) are not implemented yet.

Shaders reach the library as SPIR-V. Their interface (bindings with their names, push constants,
inputs, outputs) is **reflected from the SPIR-V** when the module is created, and pipeline creation
checks it against the pipeline layout, the vertex attributes and the color attachments, the way
commercial engines read the interface from compiled bytecode:

```cpp
import std;
import gfx;

struct BlurPush { float texel[2]; float radius; };                      // the shader reads 12 bytes

void blur(gfx::Device& device, std::span<const std::uint32_t> blur_spirv, gfx::CommandBuffer& command_buffer,
          gfx::DescriptorSet& set, gfx::ImageViewHandle output)
{
    // Layout reflected from the SPIR-V; the pipeline layout is derived from it (or declared explicitly).
    auto shader = device.create_shader_module({.stage = gfx::ShaderStage::COMPUTE, .spirv = blur_spirv, .name = "blur"});
    auto layout = device.create_pipeline_layout_from({shader.get()});
    auto pipeline = device.create_compute_pipeline({.layout = layout.handle(), .shader = shader->handle()});
    //   ^ ShaderLayoutMismatchError (set, binding, both types) if the shader does not fit the layout

    const gfx::BindingLocation output_binding = layout.find_binding("u_output");   // look a name up once
    set.write(output_binding, gfx::ImageDescriptor{.view = output, .layout = gfx::ImageLayout::GENERAL});
    //   ^ InvalidUsageError if the resource does not match the binding's type

    gfx::ComputeRecorder recorder{command_buffer};                                   // begins; ends when destroyed
    recorder.bind_pipeline(pipeline->handle());
    recorder.push_constants(layout.pipeline_layout(), BlurPush{{1.0F / 1920, 1.0F / 1080}, 4.0F});  // checked
    recorder.dispatch(240, 135);
    // recorder.begin_rendering({});   // does not compile: rendering needs a graphics queue
}
```

**Debug builds** also accept GLSL source, compiled at run time, and reload shaders whose file changed
(`device.poll_shader_changes()`, once per frame). **Release builds** only load SPIR-V compiled at
build time: GLSL input is rejected and polling does nothing (`gfx::SHADER_HOT_RELOAD`).

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
source on Windows and linked statically.

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
│   ├── GfxTesting.cmake                 GoogleTest, compilation-failure and naming test helpers
│   └── scripts/                         CMake scripts run by tests
├── source/               the library (module gfx and its parts)
│   ├── gfx.cppm          `import gfx;` umbrella
│   ├── enums.cppm        every enumeration and constant (the only file that defines them)
│   ├── errors.cppm/.cpp  exception hierarchy (gfx::Error, BackendCapabilityError, ...)
│   ├── handles.cppm      typed numeric handles and their concepts
│   ├── interface/        module gfx.interface: one partition per object or concept
│   │   ├── interface.cppm             primary interface unit (re-exports every partition)
│   │   ├── shader_layout.cppm         ShaderLayout, the interface reflected from a shader's SPIR-V
│   │   ├── shader_validation.cppm     checks of shader layouts against pipeline layouts, vertex
│   │   │                              attributes and attachments; layouts derived from shaders
│   │   ├── recorder.cppm              Recorder<QueueType>, rendering scopes, parallel rendering parts
│   │   ├── frame_command_pools.cppm   per-thread command pools for the frames in flight
│   │   └── device.cppm, buffer.cppm, ...  the abstract objects backends implement
│   └── include/gfx/      headers only where a module cannot serve (toolchain workarounds)
└── tests/
    ├── toolchain/        toolchain conformance; defects/ proves each workaround is still needed
    ├── unit/<area>/      GoogleTest unit tests, one subfolder per library area
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
