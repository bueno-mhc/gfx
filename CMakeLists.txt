# ==============================================================================
# gfx - C++23 Vulkan-style graphics rendering abstraction
# All build logic lives in cmake/; this file only orchestrates it.
# ==============================================================================
cmake_minimum_required(VERSION 4.3)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# WARNING: enables CMake's EXPERIMENTAL `import std` support - read cmake/GfxImportStandardLibrary.cmake.
include(GfxImportStandardLibrary)

project(gfx
    VERSION 0.1.0
    DESCRIPTION "C++23 Vulkan-style graphics rendering abstraction"
    HOMEPAGE_URL "https://github.com/bueno-mhc/gfx"
    LANGUAGES C CXX)  # C: volk, the Vulkan meta-loader, is a C library built from source

option(GFX_BUILD_TESTS "Build the GoogleTest unit tests and register them with CTest" ON)
option(GFX_BUILD_EXAMPLES "Build the examples (examples/; the windowed and UI ones need the platform library, the physics one the physics library)" ON)
option(GFX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# Backends. gfx::create_instance() throws a descriptive error for a backend that was not built.
option(GFX_BACKEND_VULKAN "Build the Vulkan backend" ON)
option(GFX_BACKEND_NULL "Build the null backend (a stub that is not implemented yet)" ON)
option(GFX_BACKEND_DIRECT3D12 "Build the Direct3D 12 backend (a stub that is not implemented yet)" ${WIN32})
option(GFX_BACKEND_METAL "Build the Metal backend (a stub that is not implemented yet)" ${APPLE})

# The platform library (windowing and input). gfx::platform::create_platform() throws a descriptive
# error for a backend that was not built.
option(GFX_BUILD_PLATFORM "Build the platform library (windowing and input)" ON)
# The UI library (widgets, SDF/MSDF text): its font pipeline fetches FreeType, msdfgen, msdf-atlas-gen,
# HarfBuzz, zstd, SheenBidi and libunibreak (docs/ui-design.md).
option(GFX_BUILD_UI "Build the UI library (immediate/retained widgets, SDF/MSDF text)" ON)
include(CMakeDependentOption)
# The UI's build-time font pipeline (gfx::ui_atlas_tools and the atlas tool gfx_add_ui_atlas() runs);
# without it, applications load atlases built elsewhere.
cmake_dependent_option(GFX_BUILD_UI_ATLAS_TOOLS "Build the UI atlas tools (font decoders, SDF generators, atlas tool)" ON
                       "GFX_BUILD_UI" OFF)
cmake_dependent_option(GFX_PLATFORM_BACKEND_SDL3 "Build the SDL3 platform backend (SDL3 found or fetched)" ON
                       "GFX_BUILD_PLATFORM" OFF)
cmake_dependent_option(GFX_PLATFORM_BACKEND_GLFW "Build the GLFW platform backend (GLFW found or fetched)" ON
                       "GFX_BUILD_PLATFORM" OFF)
# The physics and camera libraries (docs/physics-design.md): they depend on gfx::core only.
option(GFX_BUILD_PHYSICS "Build the physics and camera libraries (decoupled from graphics, platform and UI)" ON)
cmake_dependent_option(GFX_PHYSICS_BACKEND_BOX3D "Build the Box3D physics backend (Box3D fetched, pinned and linked statically)" ON
                       "GFX_BUILD_PHYSICS" OFF)
# The GPU physics backend (docs/gpu-xpbd-design.md): XPBD on the graphics library's compute. Off by default: with it,
# gfx::physics depends on graphics through this backend (the rest of the physics library still does not).
cmake_dependent_option(GFX_PHYSICS_BACKEND_GPU_XPBD "Build the GPU physics backend (XPBD on the graphics library's Vulkan compute)" OFF
                       "GFX_BUILD_PHYSICS;GFX_BACKEND_VULKAN" OFF)

# The SIMD instruction set the math (gfx.core.math) and everything built on it compile for: SSE2 (every
# x86-64 processor; NEON on 64-bit ARM, WASM SIMD on WebAssembly), AVX2 (with FMA: Intel Haswell, AMD
# Excavator and later), NATIVE (the building machine's: not portable to other machines). Consumers
# compile with the same flags (inline SIMD code), so it applies to the whole build.
set(GFX_SIMD_INSTRUCTION_SET "SSE2" CACHE STRING "SIMD instruction set of the math: SSE2, AVX2 or NATIVE")
set_property(CACHE GFX_SIMD_INSTRUCTION_SET PROPERTY STRINGS SSE2 AVX2 NATIVE)

include(GfxToolchainRequirements)
include(GfxProjectOptions)
include(GfxShaders)       # gfx_add_shaders(): GLSL / HLSL -> SPIR-V at build time

add_subdirectory(source)

if(GFX_BUILD_TESTS)
    include(GfxTesting)
    add_subdirectory(tests)
endif()

# After the tests: the examples' automated runs reuse what the tests found (the validation layers).
if(GFX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
