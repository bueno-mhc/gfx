/// @file atlas_tools.cppm
/// @brief gfx.ui.atlas_tools: the build-time font pipeline of the UI library (`import gfx.ui.atlas_tools;`,
///        target gfx::ui_atlas_tools): font sources and decoders (FreeType, stb_truetype), character
///        selections, distance-field generators (msdfgen, built-in), packers, the `.uiatlas` writer and
///        the AtlasBuilder the atlas tool (gfx_ui_atlas_builder) and gfx_add_ui_atlas() run. Applications
///        load the atlases it builds with gfx.ui and never link these libraries.
export module gfx.ui.atlas_tools;

export import gfx.ui;
export import :font_source;
export import :character_selection;
export import :outline;
export import :font_decoder;
export import :sdf_generator;
export import :atlas_packer;
export import :atlas_writer;
export import :atlas_builder;
