# The UI editor example: gfx_ui_editor_example [--backend sdl3|glfw] [--device <index>] [--frames <count>] [--validation]
#                                               [--sweep] [--check-allocations] [--warm-up <frames>] [--layout <file>]
add_executable(gfx_ui_editor_example ui_editor_example.cpp)
target_link_libraries(gfx_ui_editor_example PRIVATE gfx::platform_graphics gfx::ui gfx::ui_platform gfx::project_options)
gfx_deploy_runtime_dependencies(gfx_ui_editor_example)

# Its fonts: atlases built here from fetched OFL fonts (Noto Sans for Latin, Greek and Cyrillic; Noto Sans Hebrew and
# Arabic as its fallbacks), embedded into modules of the program. The characters are the blocks' assigned ones the
# fonts draw (Greek without the Coptic letters), and the build fails if a font lacks one.
include(GfxDependencies)
include(GfxUiAtlas)
gfx_require_ui_test_fonts()
gfx_add_ui_atlas(gfx_ui_editor_example
    NAME editor_latin
    FONT ${GFX_UI_TEST_FONT_DIRECTORY}/NotoSans.ttf
    CHARACTERS BASIC_LATIN LATIN_1_SUPPLEMENT LATIN_EXTENDED_A CYRILLIC
               U+0370-U+0377 U+037A-U+037F U+0384-U+038A U+038C U+038E-U+03A1 U+03A3-U+03E1 U+03F0-U+03FF
               U+2000-U+2064 U+2066-U+206F
    KIND MTSDF SIZE 32 RANGE 4
    REQUIRE_ALL_CHARACTERS
    EMBED MODULE ui_editor.latin_font NAMESPACE ui_editor)
gfx_add_ui_atlas(gfx_ui_editor_example
    NAME editor_hebrew
    FONT ${GFX_UI_TEST_FONT_DIRECTORY}/NotoSansHebrew.ttf
    CHARACTERS U+0591-U+05C7 U+05D0-U+05EA U+05EF-U+05F4
    KIND MTSDF SIZE 32 RANGE 4
    REQUIRE_ALL_CHARACTERS
    EMBED MODULE ui_editor.hebrew_font NAMESPACE ui_editor)
gfx_add_ui_atlas(gfx_ui_editor_example
    NAME editor_arabic
    FONT ${GFX_UI_TEST_FONT_DIRECTORY}/NotoSansArabic.ttf
    CHARACTERS ARABIC
    KIND MTSDF SIZE 32 RANGE 4
    REQUIRE_ALL_CHARACTERS
    EMBED MODULE ui_editor.arabic_font NAMESPACE ui_editor)

# One automated run per platform backend: five cycles of a synthetic mouse sweeping the window (120 frames each, the
# time advancing 1/60 s per frame: every run draws the same frames), no allocation in the UI's frames nor its renderer
# in the last two (the first three draw everything the sweep shows at least once: the plot's tooltips first appear in
# the second), and the validation layers when the tests found them (tests/graphics/vulkan); a validation error or an
# allocation fails the run.
if(GFX_BUILD_TESTS)
    set(_gfx_example_arguments --frames 600 --warm-up 360 --sweep --check-allocations)
    set(_gfx_example_environment "")
    if(GFX_VULKAN_VALIDATION_LAYER_DIRECTORY)
        list(APPEND _gfx_example_arguments --validation)
        set(_gfx_example_environment "VK_ADD_LAYER_PATH=${GFX_VULKAN_VALIDATION_LAYER_DIRECTORY}")
    endif()
    foreach(_gfx_backend IN ITEMS SDL3 GLFW)
        if(GFX_PLATFORM_BACKEND_${_gfx_backend})
            string(TOLOWER ${_gfx_backend} _gfx_backend_name)
            add_test(NAME UiEditorExample.${_gfx_backend}
                     COMMAND gfx_ui_editor_example --backend ${_gfx_backend_name} ${_gfx_example_arguments})
            set_tests_properties(UiEditorExample.${_gfx_backend} PROPERTIES
                ENVIRONMENT "${_gfx_example_environment}"
                LABELS example)
        endif()
    endforeach()
endif()
