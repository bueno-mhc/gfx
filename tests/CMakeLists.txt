# One subdirectory per test suite; the helpers live in cmake/GfxTesting.cmake.
add_subdirectory(toolchain)    # toolchain conformance (C++23, import std, GoogleTest with modules)
add_subdirectory(core)         # gfx.core: shared machinery, errors, handles, identifiers, native window handle
add_subdirectory(graphics)     # the graphics library: unit tests, and its backends on the machine's GPUs
if(GFX_BUILD_PLATFORM)
    add_subdirectory(platform)          # the platform library (windowing and input)
    add_subdirectory(platform_graphics) # presentation to platform windows (gfx.platform.graphics)
endif()
add_subdirectory(compilation)  # code that must not compile, and naming checks
