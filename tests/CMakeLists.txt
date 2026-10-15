# One subdirectory per test suite; the helpers live in cmake/GfxTesting.cmake.
add_subdirectory(toolchain)    # toolchain conformance (C++23, import std, GoogleTest with modules)
add_subdirectory(core)         # gfx.core: shared machinery, errors, handles, identifiers, native window handle
add_subdirectory(graphics)     # the graphics library: unit tests, and its backends on the machine's GPUs
add_subdirectory(compilation)  # code that must not compile, and naming checks
