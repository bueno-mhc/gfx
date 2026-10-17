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
            errors.cppm)

target_compile_features(gfx_ui PUBLIC cxx_std_23)
target_link_libraries(gfx_ui
    PUBLIC gfx::core
    PRIVATE gfx::project_options)
