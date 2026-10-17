# ==============================================================================
# gfx::ui: a hybrid immediate/retained UI whose text is rendered from SDF/MSDF font atlases
# (`import gfx.ui;`, gfx.core included). Design and phases: docs/ui-design.md.
# ==============================================================================
add_library(gfx_ui)
add_library(gfx::ui ALIAS gfx_ui)

target_sources(gfx_ui
    PUBLIC
        FILE_SET public_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            ui.cppm
            enums.cppm
            handles.cppm
            errors.cppm
            font/atlas/atlas_format.cppm
    PRIVATE
        font/atlas/atlas_format.cpp)

target_compile_features(gfx_ui PUBLIC cxx_std_23)
target_link_libraries(gfx_ui
    PUBLIC gfx::core
    PRIVATE gfx::project_options)

# ==============================================================================
# gfx::ui_atlas_tools: the build-time font pipeline (`import gfx.ui.atlas_tools;`): font decoders
# (FreeType, stb_truetype), distance-field generators (msdfgen, built-in), packers, the .uiatlas
# writer and AtlasBuilder. Applications that only load atlases do not link it.
# ==============================================================================
if(NOT GFX_BUILD_UI_ATLAS_TOOLS)
    return()
endif()

include(GfxDependencies)
gfx_require_freetype()
gfx_require_msdfgen()
gfx_require_harfbuzz()
gfx_require_zstd()
gfx_require_stb_truetype()

add_library(gfx_ui_atlas_tools)
add_library(gfx::ui_atlas_tools ALIAS gfx_ui_atlas_tools)

# FreeType and msdfgen are reached through plain translation units (freetype_library.cpp,
# msdfgen_kernel.cpp, rectangle_packer.cpp) behind headers that include no standard header: their
# headers include the C and C++ standard libraries, which cannot share a unit with `import std`.
target_sources(gfx_ui_atlas_tools
    PUBLIC
        FILE_SET public_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            font/atlas_tools.cppm
            font/source/font_source.cppm
            font/source/character_selection.cppm
            font/decoders/outline.cppm
            font/decoders/font_decoder.cppm
            font/sdf/sdf_generator.cppm
            font/atlas/atlas_packer.cppm
            font/atlas/atlas_writer.cppm
            font/atlas/atlas_builder.cppm
    PRIVATE
        FILE_SET internal_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            font/implementations.cppm
    PRIVATE
        font/source/font_source.cpp
        font/source/character_selection.cpp
        font/decoders/outline.cpp
        font/decoders/font_decoder.cpp
        font/decoders/freetype_decoder.cpp
        font/decoders/freetype_library.cpp
        font/decoders/stb_truetype_decoder.cpp
        font/decoders/stb_truetype_implementation.cpp
        font/sdf/msdfgen_generator.cpp
        font/sdf/msdfgen_kernel.cpp
        font/sdf/built_in_generator.cpp
        font/atlas/atlas_packer.cpp
        font/atlas/rectangle_packer.cpp
        font/atlas/atlas_writer.cpp
        font/atlas/atlas_builder.cpp
        font/shaping/shaping_font.cpp)

target_compile_features(gfx_ui_atlas_tools PUBLIC cxx_std_23)
target_link_libraries(gfx_ui_atlas_tools
    PUBLIC gfx::ui
    PRIVATE gfx::project_options gfx::freetype gfx::msdfgen gfx::msdf_atlas_gen gfx::harfbuzz gfx::harfbuzz_subset gfx::zstd
            gfx::stb_truetype)

# The atlas tool, which gfx_add_ui_atlas() (cmake/GfxUiAtlas.cmake) runs at build time.
add_executable(gfx_ui_atlas_builder tools/atlas_builder/atlas_builder_tool.cpp)
target_link_libraries(gfx_ui_atlas_builder PRIVATE gfx::ui_atlas_tools gfx::project_options)
include(GfxRuntimeDeployment)
gfx_deploy_runtime_dependencies(gfx_ui_atlas_builder)
