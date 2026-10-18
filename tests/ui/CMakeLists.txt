# Tests of the UI library (`import gfx.ui;`), of its atlas tools, of atlases embedded at build time,
# and of the third-party libraries of its font pipeline as gfx builds them (font_libraries_test.cpp, a
# plain translation unit), with fetched OFL test fonts.
gfx_add_test(gfx_ui_tests
    SOURCES ui_foundations_test.cpp
    LIBRARIES gfx::ui)

# Where the Vulkan validation layers are (found by tests/graphics/vulkan), for the tests that draw on the GPUs.
set(_gfx_ui_layer_directory "")
if(GFX_VULKAN_VALIDATION_LAYER_DIRECTORY)
    set(_gfx_ui_layer_directory "${GFX_VULKAN_VALIDATION_LAYER_DIRECTORY}")
endif()

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

# Text: fonts, layout (bidi, scripts, fallback, shaping, line breaking, alignment), the cache, carets;
# atlases built for it at build time and embedded.
gfx_add_test(gfx_ui_text_tests
    SOURCES font_atlas_test.cpp text_layout_test.cpp text_editor_test.cpp
    MODULES text_test_support.cppm
    LIBRARIES gfx::ui)
foreach(atlas IN ITEMS
        "text_latin|NotoSans.ttf|BASIC_LATIN,LATIN_1_SUPPLEMENT,U+2013-U+2014,U+2018-U+201D,U+2026,U+FFFD|"
        "text_latin_basic|NotoSans.ttf|BASIC_LATIN|NO_SHAPING_FONT"
        "text_hebrew|NotoSansHebrew.ttf|U+05D0-U+05EA,U+0020|"
        "text_arabic|NotoSansArabic.ttf|U+0621-U+064A,U+064B-U+0652,U+0660-U+0669,U+0020|")
    string(REPLACE "|" ";" parts "${atlas}")
    list(GET parts 0 name)
    list(GET parts 1 font)
    list(GET parts 2 characters)
    list(GET parts 3 options)
    string(REPLACE "," ";" characters "${characters}")
    gfx_add_ui_atlas(gfx_ui_text_tests
        NAME ${name}
        FONT ${GFX_UI_TEST_FONT_DIRECTORY}/${font}
        CHARACTERS ${characters}
        KIND MTSDF SIZE 32 RANGE 4 ${options}
        EMBED MODULE gfx.tests.${name} NAMESPACE gfx::tests::text_atlases
        OUTPUT_VARIABLE ${name}_file)
endforeach()
target_compile_definitions(gfx_ui_text_tests PRIVATE GFX_TEST_TEXT_LATIN_ATLAS="${text_latin_file}")

# No allocation when a cached layout is laid out again (operator new counted in this executable).
gfx_add_test(gfx_ui_text_allocation_tests
    SOURCES text_allocation_test.cpp
    LIBRARIES gfx::ui)
gfx_add_ui_atlas(gfx_ui_text_allocation_tests
    NAME allocation_latin
    FONT ${GFX_UI_TEST_FONT_DIRECTORY}/NotoSans.ttf
    CHARACTERS BASIC_LATIN
    EMBED MODULE gfx.tests.allocation_latin NAMESPACE gfx::tests::text_atlases)

# The immediate-mode UI driven by synthetic input (UiContext, UiFrame), with an atlas built for it.
gfx_add_test(gfx_ui_context_tests
    SOURCES context_test.cpp context_render_test.cpp input_widgets_test.cpp
    MODULES context_test_support.cppm
    LIBRARIES gfx::ui)
gfx_add_ui_atlas(gfx_ui_context_tests
    NAME context_latin
    FONT ${GFX_UI_TEST_FONT_DIRECTORY}/NotoSans.ttf
    CHARACTERS BASIC_LATIN
    KIND MTSDF SIZE 32 RANGE 4
    EMBED MODULE gfx.tests.context_latin NAMESPACE gfx::tests::context_atlases)
target_include_directories(gfx_ui_context_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/graphics/vulkan)
target_compile_definitions(gfx_ui_context_tests PRIVATE GFX_TEST_VULKAN_LAYER_DIRECTORY="${_gfx_ui_layer_directory}")

# The UI on the platform library: frame input from window events, window services, and a UI in a visible
# window on every platform backend, presented with Vulkan and the validation layers.
if(TARGET gfx_ui_platform AND TARGET gfx_platform_graphics)
    gfx_add_test(gfx_ui_platform_tests
        SOURCES ui_platform_test.cpp
        LIBRARIES gfx::ui_platform gfx::platform_graphics)
    gfx_add_ui_atlas(gfx_ui_platform_tests
        NAME platform_latin
        FONT ${GFX_UI_TEST_FONT_DIRECTORY}/NotoSans.ttf
        CHARACTERS BASIC_LATIN LATIN_1_SUPPLEMENT
        KIND MTSDF SIZE 32 RANGE 4
        EMBED MODULE gfx.tests.platform_latin NAMESPACE gfx::tests::platform_atlases)
    target_compile_definitions(gfx_ui_platform_tests PRIVATE GFX_TEST_VULKAN_LAYER_DIRECTORY="${_gfx_ui_layer_directory}")
    target_include_directories(gfx_ui_platform_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/graphics/vulkan)
endif()

# Drawing: draw lists and the debug overlay (on the CPU), and the renderer on every GPU of the machine with
# the Vulkan validation layers (found by tests/graphics/vulkan; its fixtures in vulkan_test.hpp).
gfx_add_test(gfx_ui_draw_tests
    SOURCES draw_list_test.cpp renderer_test.cpp
    LIBRARIES gfx::ui)
gfx_add_ui_atlas(gfx_ui_draw_tests
    NAME draw_latin
    FONT ${GFX_UI_TEST_FONT_DIRECTORY}/NotoSans.ttf
    CHARACTERS BASIC_LATIN
    KIND MTSDF SIZE 32 RANGE 4
    EMBED MODULE gfx.tests.draw_latin NAMESPACE gfx::tests::draw_atlases)
target_compile_definitions(gfx_ui_draw_tests PRIVATE GFX_TEST_VULKAN_LAYER_DIRECTORY="${_gfx_ui_layer_directory}")
target_include_directories(gfx_ui_draw_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/graphics/vulkan)
