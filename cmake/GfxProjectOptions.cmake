# ==============================================================================
# gfx_project_options: compiler warnings and platform libraries shared by every gfx target.
# gfx_toolchain_workarounds: link options every executable using gfx needs on a defective toolchain.
# ==============================================================================
include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# Toolchain workarounds (propagated to consumers of the gfx library)
# ------------------------------------------------------------------------------
add_library(gfx_toolchain_workarounds INTERFACE)
add_library(gfx::toolchain_workarounds ALIAS gfx_toolchain_workarounds)

# WORKAROUND - GCC 16 on MinGW: with `import std`, GCC emits static local variables of standard
# library templates (for example std::__format::__write_escaped_unicode_part<...>::__replace_rep)
# as ordinary strong symbols instead of COMDAT sections. Two object files that format strings then
# fail to link with "multiple definition of". Those definitions are identical, so letting the linker
# keep the first one is safe for them - but the option also hides genuine duplicate definitions.
# The test CompilationFailure.ImportStdDuplicatesStaticLocals fails once GCC no longer needs this.
if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(gfx_toolchain_workarounds INTERFACE -Wl,--allow-multiple-definition)
endif()

# MinGW's libstdc++ keeps the terminal back end of std::print in libstdc++exp.
if(MINGW)
    target_link_libraries(gfx_toolchain_workarounds INTERFACE stdc++exp)
endif()

# ------------------------------------------------------------------------------
# SIMD instruction set (propagated to consumers: the math's inline SIMD code is compiled in them)
# ------------------------------------------------------------------------------
add_library(gfx_simd_options INTERFACE)
add_library(gfx::simd_options ALIAS gfx_simd_options)
# No fused multiply-add contraction: GCC and Clang fuse `a * b + c` when the instruction set has FMA (AVX2), which
# would make results depend on the instruction set and scalar code differ from the SIMD kernels computing the same
# arithmetic; simulations stay deterministic across builds (MSVC does not contract by default).
if(NOT MSVC)
    target_compile_options(gfx_simd_options INTERFACE -ffp-contract=off)
endif()
if(GFX_SIMD_INSTRUCTION_SET STREQUAL "AVX2")
    if(MSVC)
        target_compile_options(gfx_simd_options INTERFACE /arch:AVX2)
    else()
        target_compile_options(gfx_simd_options INTERFACE -mavx2 -mfma)
    endif()
elseif(GFX_SIMD_INSTRUCTION_SET STREQUAL "NATIVE")
    if(MSVC)
        message(FATAL_ERROR "gfx: GFX_SIMD_INSTRUCTION_SET=NATIVE has no MSVC equivalent; choose SSE2 or AVX2.")
    endif()
    target_compile_options(gfx_simd_options INTERFACE -march=native)
elseif(NOT GFX_SIMD_INSTRUCTION_SET STREQUAL "SSE2")
    message(FATAL_ERROR "gfx: GFX_SIMD_INSTRUCTION_SET is '${GFX_SIMD_INSTRUCTION_SET}'; choose SSE2, AVX2 or NATIVE.")
endif()
message(STATUS "gfx: SIMD instruction set ${GFX_SIMD_INSTRUCTION_SET}")

# ------------------------------------------------------------------------------
# Project options (gfx's own targets only)
# ------------------------------------------------------------------------------
add_library(gfx_project_options INTERFACE)
add_library(gfx::project_options ALIAS gfx_project_options)
target_compile_features(gfx_project_options INTERFACE cxx_std_23)
target_link_libraries(gfx_project_options INTERFACE gfx::toolchain_workarounds)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(gfx_project_options INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Woverloaded-virtual
        $<$<BOOL:${GFX_WARNINGS_AS_ERRORS}>:-Werror>)
endif()
