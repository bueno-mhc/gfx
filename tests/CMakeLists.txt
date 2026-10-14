# One subdirectory per test suite; the helpers live in cmake/GfxTesting.cmake.
add_subdirectory(toolchain)    # toolchain conformance (C++23, import std, GoogleTest with modules)
add_subdirectory(unit)         # GoogleTest unit tests of the library, one subdirectory per area
add_subdirectory(compilation)  # code that must not compile, and naming checks
