/// @file ui.cppm
/// @brief Umbrella module: `import gfx.ui;` provides the whole public API of the UI library (a hybrid
///        immediate/retained UI whose text is rendered from SDF/MSDF font atlases), gfx.core included.
///        The design, and the phases that build it, are in docs/ui-design.md. Atlases are built by
///        gfx.ui.atlas_tools (a build-time library), read with gfx.ui.atlas_format and loaded, with the
///        fonts and the text layout engine, by gfx.ui.text; gfx.ui.draw describes what to draw and
///        gfx.ui.renderer draws it with the graphics abstraction.
export module gfx.ui;

export import gfx.core;
export import gfx.ui.enums;
export import gfx.ui.handles;
export import gfx.ui.errors;
export import gfx.ui.atlas_format;
export import gfx.ui.unicode;
export import gfx.ui.text;
export import gfx.ui.draw;
export import gfx.ui.debug_overlay;
export import gfx.ui.renderer;
