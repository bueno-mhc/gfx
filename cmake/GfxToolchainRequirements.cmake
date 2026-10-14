# ==============================================================================
# Validates the toolchain and sets the language level. Must be included AFTER project().
# ==============================================================================
include_guard(GLOBAL)

if(NOT CMAKE_GENERATOR MATCHES "^Ninja")
    message(FATAL_ERROR
        "gfx: the '${CMAKE_GENERATOR}' generator cannot build C++ modules that use `import std`.\n"
        "  Suggestion: configure with -G Ninja (MSYS2 package mingw-w64-ucrt-x86_64-ninja),\n"
        "              or use `python build.py` / `cmake --preset debug`.")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
    message(FATAL_ERROR
        "gfx: GCC ${CMAKE_CXX_COMPILER_VERSION} does not ship the C++23 standard library module.\n"
        "  Suggestion: use GCC 15 or newer (verified with GCC 16.1 from MSYS2 UCRT64).")
elseif(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(WARNING "gfx: only GCC 16 has been verified; ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} is untested.")
endif()

if(NOT "23" IN_LIST CMAKE_CXX_COMPILER_IMPORT_STD)
    message(FATAL_ERROR
        "gfx: `import std` is unavailable for C++23 (CMAKE_CXX_COMPILER_IMPORT_STD='${CMAKE_CXX_COMPILER_IMPORT_STD}').\n"
        "  Reason: most likely the CMAKE_EXPERIMENTAL_CXX_IMPORT_STD UUID does not match CMake ${CMAKE_VERSION}.\n"
        "  Suggestion: read the WARNING block in cmake/GfxImportStandardLibrary.cmake.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_MODULE_STD ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()
