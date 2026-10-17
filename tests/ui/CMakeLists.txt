# Tests of the UI library (`import gfx.ui;`), of its atlas tools, of atlases embedded at build time,
# and of the third-party libraries of its font pipeline as gfx builds them (font_libraries_test.cpp, a
# plain translation unit), with fetched OFL test fonts.
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

if(NOT TARGET gfx_ui_atlas_tools)
    return()
endif()

# The atlas tools: format, sources, decoders, generators, packers, writer and builder.
gfx_add_test(gfx_ui_atlas_tools_tests
    SOURCES atlas_format_test.cpp font_source_test.cpp font_decoder_test.cpp sdf_generator_test.cpp atlas_builder_test.cpp
            xxhash_reference.cpp
    MODULES atlas_test_support.cppm
    LIBRARIES gfx::ui_atlas_tools gfx::zstd gfx::harfbuzz)
target_compile_definitions(gfx_ui_atlas_tools_tests PRIVATE GFX_UI_TEST_FONT_DIRECTORY="${GFX_UI_TEST_FONT_DIRECTORY}")

# Atlases built by gfx_add_ui_atlas() and embedded: #embed for one, a byte list for the other.
include(GfxUiAtlas)
gfx_add_test(gfx_ui_atlas_embed_tests
    SOURCES atlas_embed_test.cpp
    LIBRARIES gfx::ui)
gfx_add_ui_atlas(gfx_ui_atlas_embed_tests
    NAME latin
    FONT ${GFX_UI_TEST_FONT_DIRECTORY}/NotoSans.ttf
    CHARACTERS BASIC_LATIN
    VARIATION wght=500
    KIND MTSDF SIZE 32 RANGE 4
    EMBED MODULE gfx.tests.latin_atlas NAMESPACE gfx::tests::atlases
    OUTPUT_VARIABLE latin_atlas)
gfx_add_ui_atlas(gfx_ui_atlas_embed_tests
    NAME digits
    FONT ${GFX_UI_TEST_FONT_DIRECTORY}/SourceSans3-Regular.otf
    CHARACTERS U+0030-U+0039
    KIND SDF GENERATOR BUILT_IN SIZE 24 RANGE 4 NO_SHAPING_FONT
    EMBED MODULE gfx.tests.digits_atlas NAMESPACE gfx::tests::atlases EMBEDDING BYTE_LIST
    OUTPUT_VARIABLE digits_atlas)
target_compile_definitions(gfx_ui_atlas_embed_tests PRIVATE GFX_TEST_LATIN_ATLAS="${latin_atlas}" GFX_TEST_DIGITS_ATLAS="${digits_atlas}")

# The atlas tool's command line.
add_test(NAME UiAtlasTool.PrintsItsOptions COMMAND gfx_ui_atlas_builder --help)
set_tests_properties(UiAtlasTool.PrintsItsOptions PROPERTIES PASS_REGULAR_EXPRESSION "--embed-module")
add_test(NAME UiAtlasTool.RejectsUnknownOptions COMMAND gfx_ui_atlas_builder --font a.ttf --output a.uiatlas --frobnicate)
set_tests_properties(UiAtlasTool.RejectsUnknownOptions PROPERTIES PASS_REGULAR_EXPRESSION "error: unknown option --frobnicate")
add_test(NAME UiAtlasTool.FailsWhenRequiredCharactersAreMissing
    COMMAND gfx_ui_atlas_builder --font ${GFX_UI_TEST_FONT_DIRECTORY}/NotoSans.ttf --output ${CMAKE_CURRENT_BINARY_DIR}/missing.uiatlas
            --characters "U+0041 U+4E2D" --require-all-characters)
set_tests_properties(UiAtlasTool.FailsWhenRequiredCharactersAreMissing PROPERTIES
    PASS_REGULAR_EXPRESSION "error: .NotoSans.ttf. lacks 1 character.s., drawn as .notdef: U.4E2D")
add_test(NAME UiAtlasTool.SameInputsGiveSameBytes
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:gfx_ui_atlas_builder> -DFONT=${GFX_UI_TEST_FONT_DIRECTORY}/NotoSans.ttf
            -DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/GfxCompareAtlasBuilds.cmake)
