# gfx

A C++23 graphics rendering abstraction that models Vulkan's object model (instance, physical device,
device, queues, resources, commands, synchronization) above concrete backends. It is written with
C++ modules, strongly typed numeric handles and compile-time validated shader layouts.

> **Status:** step 1: enumerations and constants, errors, and typed handles. Interfaces and
> backends are added in later steps.

```cpp
import std;
import gfx;

static_assert(gfx::format_properties(gfx::Format::BC7_SRGB_BLOCK).bytes_per_block == 16);
auto stages = gfx::ShaderStage::VERTEX | gfx::ShaderStage::FRAGMENT;   // bitmask enumerations only
std::string text = std::format("{}", stages);                          // "VERTEX | FRAGMENT"
gfx::BufferHandle buffer;                // null; handles come from a Device and support no arithmetic
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
