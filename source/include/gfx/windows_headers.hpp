/// @file windows_headers.hpp
/// @brief Includes <windows.h> so that it can coexist with `import gfx;` in one translation unit.
///
/// With GCC 16 on MinGW, <windows.h> pulls the libstdc++ configuration header and the x86 intrinsic
/// headers in from inside `extern "C"` blocks. Their declarations then conflict with the ones the
/// standard library module provides with C++ linkage ("conflicting language linkage for imported
/// declaration"). Including <cstdlib> and <x86intrin.h> first gives those declarations C++ linkage,
/// and the include guards keep <windows.h> from declaring them again.
///
/// Usage: include this header (instead of <windows.h>) before every `import` of the translation unit:
/// @code
/// #include <gfx/windows_headers.hpp>
/// import std;
/// import gfx;
/// @endcode
/// Define WIN32_LEAN_AND_MEAN or NOMINMAX before including it if desired.
#pragma once

#if defined(_WIN32)
#include <cstdlib>
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#include <x86intrin.h>
#endif
#include <windows.h>
#endif
