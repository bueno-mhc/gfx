# Interfaces exercised through a test-only fake backend (fake_backend.cppm).
gfx_add_test(gfx_interface_tests
    SOURCES
        recorder_test.cpp
        device_test.cpp
        frame_command_pools_test.cpp
        synchronization_test.cpp
    MODULES
        fake_backend.cppm
    LIBRARIES gfx::gfx)
