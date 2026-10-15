# Tests of the platform library (`import gfx.platform;`), through a test-only fake backend
# (fake_platform.cppm): no window system needed.
gfx_add_test(gfx_platform_tests
    SOURCES
        enums_test.cpp
        handles_test.cpp
        event_test.cpp
        image_test.cpp
        window_test.cpp
        platform_test.cpp
        fake_platform.cpp
    MODULES
        fake_platform.cppm
    LIBRARIES gfx::platform)
