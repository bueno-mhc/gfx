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
option(GFX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# Backends. gfx::create_instance() throws a descriptive error for a backend that was not built.
option(GFX_BACKEND_VULKAN "Build the Vulkan backend" ON)
option(GFX_BACKEND_NULL "Build the null backend (a stub that is not implemented yet)" ON)
option(GFX_BACKEND_DIRECT3D12 "Build the Direct3D 12 backend (a stub that is not implemented yet)" ${WIN32})
option(GFX_BACKEND_METAL "Build the Metal backend (a stub that is not implemented yet)" ${APPLE})

# The platform library (windowing and input). gfx::platform::create_platform() throws a descriptive
# error for a backend that was not built.
option(GFX_BUILD_PLATFORM "Build the platform library (windowing and input)" ON)
include(CMakeDependentOption)
cmake_dependent_option(GFX_PLATFORM_BACKEND_SDL3 "Build the SDL3 platform backend (SDL3 found or fetched)" ON
                       "GFX_BUILD_PLATFORM" OFF)

include(GfxToolchainRequirements)
include(GfxProjectOptions)
include(GfxShaders)       # gfx_add_shaders(): GLSL / HLSL -> SPIR-V at build time

add_subdirectory(source)

if(GFX_BUILD_TESTS)
    include(GfxTesting)
    add_subdirectory(tests)
endif()
