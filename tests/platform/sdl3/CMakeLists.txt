# The SDL3 platform backend on this machine: real windows (hidden unless a test shows one), events
# injected through SDL itself, and SDL virtual gamepads (no gamepad needed). The tests call SDL directly
# to inject input, so they link it too (the same static SDL the platform library uses).
gfx_add_test(gfx_platform_sdl3_tests
    SOURCES
        sdl3_platform_test.cpp
        sdl3_events_test.cpp
        sdl3_gamepad_test.cpp
    MODULES
        sdl3_test_support.cppm
    LIBRARIES gfx::platform gfx::sdl3)
