# Tests of the UI library (`import gfx.ui;`), and of the third-party libraries of its font pipeline as
# gfx builds them (font_libraries_test.cpp, a plain translation unit), with fetched OFL test fonts.
gfx_add_test(gfx_ui_tests
    SOURCES ui_foundations_test.cpp
    LIBRARIES gfx::ui)

include(GfxDependencies)
gfx_require_ui_test_fonts()
gfx_require_freetype()
gfx_require_msdfgen()
gfx_require_harfbuzz()
gfx_require_zstd()
gfx_require_sheenbidi()
gfx_require_libunibreak()
gfx_require_stb_truetype()
gfx_add_test(gfx_ui_font_library_tests
    SOURCES font_libraries_test.cpp
    LIBRARIES gfx::freetype gfx::msdfgen gfx::msdf_atlas_gen gfx::harfbuzz gfx::harfbuzz_subset gfx::zstd
              gfx::sheenbidi gfx::libunibreak gfx::stb_truetype)
target_compile_definitions(gfx_ui_font_library_tests PRIVATE GFX_UI_TEST_FONT_DIRECTORY="${GFX_UI_TEST_FONT_DIRECTORY}")
# A plain translation unit of C and C++ headers: no module scanning.
set_target_properties(gfx_ui_font_library_tests PROPERTIES CXX_SCAN_FOR_MODULES OFF)
