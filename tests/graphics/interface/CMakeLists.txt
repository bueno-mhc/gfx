# Interfaces exercised through a test-only fake backend (fake_backend.cppm).
gfx_add_test(gfx_interface_tests
    SOURCES
        recorder_test.cpp
        device_test.cpp
        frame_command_pools_test.cpp
        frame_resources_test.cpp
        bindless_test.cpp
        pipeline_validation_test.cpp
        synchronization_test.cpp
        frame_metrics_test.cpp
    MODULES
        fake_backend.cppm
    LIBRARIES gfx::gfx)
