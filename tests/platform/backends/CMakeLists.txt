# The platform backends on this machine: the contract every backend honors, run on each backend built
# (platform_backend_test.cpp, parameterized), then what is particular to each backend. Every executable
# compiles the shared helpers (backend_test_support.cppm, module gfx.tests.backend_support).
gfx_add_test(gfx_platform_backend_tests
    SOURCES platform_backend_test.cpp
    MODULES backend_test_support.cppm
    LIBRARIES gfx::platform)

if(GFX_PLATFORM_BACKEND_SDL3)
    add_subdirectory(sdl3)
endif()
if(GFX_PLATFORM_BACKEND_GLFW)
    add_subdirectory(glfw)
endif()
