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
    LANGUAGES CXX)

option(GFX_BUILD_TESTS "Build the GoogleTest unit tests and register them with CTest" ON)
option(GFX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

include(GfxToolchainRequirements)
include(GfxProjectOptions)

if(GFX_BUILD_TESTS)
    include(GfxTesting)
    add_subdirectory(tests)
endif()
