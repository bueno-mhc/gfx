# SPIR-V reflection, the run-time compiler (Debug builds) and gfx_add_shaders().
gfx_add_test(gfx_shader_tools_tests
    SOURCES
        reflection_test.cpp
        compiler_test.cpp
    LIBRARIES gfx::gfx)

# The test shaders, compiled at build time into module gfx.tests.shaders.
gfx_add_shaders(gfx_shader_tools_tests
    MODULE gfx.tests.shaders
    NAMESPACE gfx::tests::shaders
    SHADERS
        shaders/reflection.comp
        shaders/stage_io.vert
        shaders/stage_io.frag
        shaders/scale.comp.hlsl
        shaders/specialized.comp
    INCLUDE_DIRECTORIES shaders/include
    DEFINES TEXTURE_COUNT=4)

# The tests read shader sources from the source tree (run-time compilation).
target_compile_definitions(gfx_shader_tools_tests PRIVATE
    GFX_TEST_SHADER_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
