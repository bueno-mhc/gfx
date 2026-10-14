# ==============================================================================
# Enables CMake's support for `import std;`. Must be included BEFORE project().
# ==============================================================================
# WARNING: `import std;` relies on an EXPERIMENTAL CMake feature.
# ------------------------------------------------------------------------------
# Every C++ module of this project uses `import std;` instead of textual
# #include directives. CMake only builds the standard library module when the
# experimental gate CMAKE_EXPERIMENTAL_CXX_IMPORT_STD holds the exact UUID that
# the running CMake release expects. That UUID changes between CMake releases
# (it is published in Help/dev/experimental.rst of the CMake source tree), so
# upgrading or downgrading CMake breaks the configure step until the table
# below is updated.
#   * Verified with: CMake 4.4.0-rc2, GCC 16.1 (MSYS2 UCRT64), Ninja 1.13.2.
#   * Override without editing this file: -DGFX_IMPORT_STD_GATE=<uuid>.
#   * Only the Ninja generators can build `import std` (the Makefile and
#     Visual Studio generators cannot), so this project requires Ninja.
#   * Fallback if the gate is ever removed or broken: replace `import std;`
#     with #include directives inside each module's global module fragment.
#     Beware that GCC then discards global-module-fragment declarations the
#     module purview never names (for example std::strong_ordering needed by a
#     defaulted `auto operator<=>`), so such return types must be spelled out.
# ==============================================================================
include_guard(GLOBAL)

set(GFX_IMPORT_STD_GATE "" CACHE STRING
    "UUID for CMAKE_EXPERIMENTAL_CXX_IMPORT_STD; empty selects the value verified for the running CMake release")

if(GFX_IMPORT_STD_GATE)
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "${GFX_IMPORT_STD_GATE}")
elseif(CMAKE_VERSION VERSION_GREATER_EQUAL 4.4 AND CMAKE_VERSION VERSION_LESS 4.5)
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
else()
    message(FATAL_ERROR
        "gfx: no `import std` gate UUID is known for CMake ${CMAKE_VERSION}.\n"
        "  Reason: CMAKE_EXPERIMENTAL_CXX_IMPORT_STD must hold a release-specific UUID, and this project was\n"
        "          verified only with CMake 4.4.x.\n"
        "  Suggestion: look up the UUID for CMake ${CMAKE_VERSION} in Help/dev/experimental.rst of the CMake\n"
        "              sources and pass -DGFX_IMPORT_STD_GATE=<uuid>, or add it to the table in\n"
        "              cmake/GfxImportStandardLibrary.cmake.")
endif()
