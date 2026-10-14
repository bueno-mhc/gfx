# gfx — session history (2026-10-14)

Chronological record of the first working session: what was asked, decided, found and built.
The condensed, always-current state lives in [CLAUDE.md](CLAUDE.md).

---

## 1. Initial request

The user asked for a C++23 Vulkan-style rendering abstraction (see CLAUDE.md §1), with: an
**Architecture & Design Decisions** section first, then an implementation plan with code snippets for
validation, code only after approval, code compiled and tested locally (tools as in
`C:\Users\mathe\Desktop\Coding\galileo\build.py`), the repo at `C:\Users\mathe\Desktop\Coding\gfx`, a
GitHub repository with CMake (C++23 modules, backend options), README, LICENSE and the git commands, and
at the end an architecture overview with diagrams. "Do not assume anything, ask the user."

## 2. Environment discovery and prototype (before the design)

- galileo's build.py defaults to the *MinGW Makefiles* generator; CMake's C++ module support only works
  with Ninja (and Visual Studio) generators → gfx defaults to Ninja.
- Found: GCC 16.1 (MSYS2 UCRT64), CMake 4.4.0-rc2, mingw32-make, Python 3.14, git; missing: Ninja,
  Vulkan SDK/headers, glslang, DXC, Slang, clang, MSVC, `gh`. MSYS2 offered everything except DXC/Slang.
- `import std` with GCC 16 works (compiled, linked with `-lstdc++exp`, ran). CMake 4.4 supports it only
  behind the experimental gate `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD` (UUID `451f2fe2-…`, verified).
- **Smart App Control** blocked freshly built executables (always in %TEMP%, sometimes in the project).
  Later the user turned it off.
- Prototype (scratchpad, GCC 16, direct `g++ -fmodules`): module partitions; private backend module +
  factory implementation unit (consumer cannot see the backend); consteval GLSL reflection with exact
  std140/std430 sizes (`Camera { mat4; vec3; float }` = 80, `Particles { uint; Particle items[]; }` = 16,
  push constant `{ vec2; float }` = 12, bindless `texture2D u_textures[]` count 0); class-type NTTP
  (`binding_of<"u_output", BlurShader>()`), deducing-this consteval builder; `CompatibleWith` concept.
  On a descriptor-type mismatch GCC prints:
  `'gfx::LayoutCheck{gfx::LayoutError::DescriptorTypeMismatch, 0, 1, gfx::DescriptorType::StorageImage, gfx::DescriptorType::SampledImage}.ok()' evaluates to false`.
  GMF-include pitfall found: a defaulted `auto operator<=>` needs `std::strong_ordering`, which GCC
  discards from the global module fragment unless named.

## 3. Architecture & Design Decisions presented (D1–D12)

D1 object model · D2 handles (tag-based numeric, no maps; backend-internal lookup) · D3 interface vs
implementation, factory boundary (language, PRIVATE file sets, private members, macro hygiene), pure
virtual + throwing definition · D4 compile-time shader layout (layout value, consteval GLSL parser and
builder, three validation points: compile time, ShaderModule creation via SPIR-V reflection, pipeline
creation) · D5 parallelism (queues, per-thread pools, Recorder<Q>, non-blocking submit, timeline
semaphores, events) · D6 render-graph enablement · D7 errors · D8 modules · D9 RAII + deferred deletion
(retire points per queue timeline) · D10 naming · D11 compile time over run time · D12 testing.

## 4. User answers to the 18 design questions

1. Install MSYS2 packages; only Vulkan backend infrastructure needed; **GoogleTest** (not Catch2).
2. **`import std`** (option b) with a warning comment about the experimental CMake gate.
3. Generate `.hpp` only if needed.
4. **SCREAMING_SNAKE_CASE** for enumerators and constants.
5. **Pure virtual + throwing default body.**
6. **No `m_`** in plain description structs.
7. "Constants" interpretation as proposed.
8. **GLSL only**, **SPIR-V** format.
9. Inline sources **and** CMake embedding.
10. D3D12/Metal: compiled stubs that throw "not implemented yet"; implement nothing for them.
11. Legacy RenderPass/Framebuffer: do not implement if dynamic rendering suffices; ask if something is missing.
12. Add DescriptorPool and DeviceMemory (and fold the surface into Swapchain).
13. 32-bit handles.
14. Vulkan backend depth: **implement nothing for now**.
15. Null backend: just throw descriptive errors.
16. GoogleTest + CTest **with parallelization**.
17. Samples: headless and windowed.
18. Repo already cloned at `gfx` from https://github.com/bueno-mhc/gfx.git.

Additional user directives: always FetchContent when a resource is unavailable; check/install gtest;
**Handle must be its own class** without operations on its value; no SlotPool unless necessary (handles
are given by the device; users need no pools — confirmed); **full names, no abbreviations**; step-by-step
with validation after each step, user commits, stop for important questions; **shader auto reload**;
doxygen on public interfaces.

## 5. Step 0 — toolchain and build infrastructure

- Installed: ninja, vulkan-headers, vulkan-loader, vulkan-validation-layers, glslang, spirv-tools,
  spirv-cross, vulkan-memory-allocator, gtest (dry run first: nothing upgraded).
- Vulkan probe (throwaway): both GPUs support every needed feature (see CLAUDE.md §4).
- MSYS2's `libgtest.dll` was blocked by Smart App Control → GoogleTest built from source and linked
  statically on Windows (`GFX_BUILD_GOOGLETEST_FROM_SOURCE`), FetchContent `FIND_PACKAGE_ARGS` otherwise.
- CMake cannot set PATH for gtest discovery → `gfx_deploy_runtime_dependencies()` copies toolchain DLLs
  next to executables.
- Presets v11 (`"jobs": ""` = all cores), build.py (Ninja default, full-name options), toolchain
  conformance tests (10).
- User feedback: organize folders (tests in subfolders, CMake logic in `cmake/`, "build files in a builder
  folder"). Reorganized: thin root CMakeLists, `cmake/Gfx*.cmake` per concern, `tests/<suite>/`,
  `builder/build.py`. The user then **moved build.py back to the root** ("Corrected build path").
- Answers: SAC done (turned off); LICENSE holder "Matheus Henrique Couto Bueno"; `class Handle` (not
  `struct`), aliases fine; keep the naming exceptions; shader reload recommendations accepted, reload
  **disabled in Release**.

## 6. Step 1 — enums, errors, handles

- `source/enums.cppm`, `errors.cppm/.cpp`, `handles.cppm`, `gfx.cppm`, CMake library target `gfx`.
- Findings: (a) `<windows.h>` + modules conflict (both styles) → `gfx/windows_headers.hpp`; (b) `import
  std` duplicate strong symbols → `-Wl,--allow-multiple-definition`; GMF includes avoid it; (c) detectors
  added; (d) build.py `--clean-all` failed on read-only Git pack files → `onexc` chmod handler.
- Tests: 57 (unit, 8 compilation failures, macro collision check over 391 names vs 30,230 macros,
  toolchain + defects), Debug and Release from scratch.
- Questions left open: see CLAUDE.md §10.

## 7. VS Code debugging

- Added `.vscode/tasks.json`, `launch.json`, `settings.json`.
- CMake 4.4 `gtest_discover_tests(PRE_TEST)` registers each test as `cmake -P LaunchTest.cmake` → a
  debugger would attach to cmake.exe → switched to **POST_BUILD** discovery (tests run the executable).
- gdb verified: file:line breakpoints in tests and in module code (`handles.cppm:143`), locals readable;
  names show module attachment (`gfx::Handle@gfx.handles<...>`); breaking by function name on module
  code does not resolve. `GTEST_BREAK_ON_FAILURE=1` stops at `DebugBreak`, the test frame is #3.
- Problem reported: "no tests were found" with the `${cmake.testProgram}` configuration from the Run
  button. Investigation: CMake Tools had not been configured (no configure preset selected); after
  selection it configured and ran tests, but a Testing-panel debug run was "Canceled" when the
  preLaunchTask reconfigured the build tree. Root bug found: **`$pathListSep` must be `${pathListSep}`**
  in CMakePresets.json (PATH never contained MSYS2 from the preset; `system32` was dropped). Fixed;
  verified configure/build/58 tests with MSYS2 removed from PATH. Why CMake Tools' F5 picker reported no
  tests was not determined (its discovery command works by hand and logs nothing); the default
  configuration was made independent of CMake Tools, preLaunchTask builds only.
- errors_test computed an expected line by arithmetic; the user's reformat broke it → now passes an
  explicit source_location; new test `LocationDefaultsToTheCaller`.

## 8. Next (at the time of writing)

Step 2: interfaces (`gfx.interface` partitions in `source/interface/`), description structs, BackendObject
base with throwing defaults, Recorder<Q> + RenderingScope, barriers and rendering descriptions,
compile-time shader layout + compatibility checks, ShaderLayoutMismatchError, typed device front doors,
tests with fake backend objects, compilation-failure tests for the typed API.
