# What is particular to the SDL3 backend: events injected through SDL itself (SDL_PushEvent), SDL's
# event watch, SDL virtual gamepads (no gamepad needed) and the clipboard. The tests call SDL directly,
# so they link it too (the same static SDL the platform library uses).
gfx_add_test(gfx_platform_sdl3_tests
    SOURCES
        sdl3_platform_test.cpp
        sdl3_events_test.cpp
        sdl3_gamepad_test.cpp
    MODULES
        ${CMAKE_CURRENT_SOURCE_DIR}/../backend_test_support.cppm
        sdl3_test_support.cppm
    MODULE_BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    LIBRARIES gfx::platform gfx::sdl3)
