/// @file core.cppm
/// @brief `import gfx.core;`: what the gfx libraries share — the enumeration machinery and shared
///        enumerations, the error hierarchy, handles and identifiers, value types and the native window
///        handle. The graphics library (`import gfx;`) and the platform library (`import gfx.platform;`)
///        both re-export it.
export module gfx.core;

export import gfx.core.enums;
export import gfx.core.errors;
export import gfx.core.handles;
export import gfx.core.types;
export import gfx.core.native_window;
