# Run-time validation of reflected shader layouts (no backend needed).
gfx_add_test(gfx_shader_layout_tests
    SOURCES
        shader_validation_test.cpp
    LIBRARIES gfx::gfx)
