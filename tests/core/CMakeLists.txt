# Tests of gfx.core: they import gfx.core only, so the shared layer works without the graphics library.
gfx_add_test(gfx_core_tests
    SOURCES
        enumeration_test.cpp
        identifier_test.cpp
        backend_identity_test.cpp
        native_window_test.cpp
        math_test.cpp
    LIBRARIES gfx::core)

add_subdirectory(slot_table)
