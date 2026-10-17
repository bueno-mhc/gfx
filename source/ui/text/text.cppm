/// @file text.cppm
/// @brief gfx.ui.text: fonts and text of the UI library — FontAtlas (a loaded `.uiatlas`), FontLibrary
///        (atlases and fonts by handle, fallback chains) and TextLayoutEngine (bidi, scripts, fallback,
///        shaping, line breaking, cached layouts). Re-exported by gfx.ui.
export module gfx.ui.text;

export import :font_atlas;
export import :font_library;
export import :text_layout;
