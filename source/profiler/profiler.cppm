/// @file profiler.cppm
/// @brief `import gfx.profiler;` — the gfx profiler (docs/profiler-performance-design.md): CPU scopes, counters, sizes,
///        markers and asynchronous spans recorded from any thread into per-thread lock-free rings, sessions of frames,
///        traces (Chrome Tracing JSON, a binary format), reports with percentiles. It depends on gfx.core only, so
///        every gfx library may use it; GPU timings come through gfx.profiler.graphics.
///
///        The PROFILE_* macros of <gfx/profiler.hpp> are the shortest way to record; with the option GFX_PROFILER
///        off (PROFILER_ENABLED false) they produce no code and no data.
export module gfx.profiler;

export import gfx.profiler.enums;
export import gfx.profiler.handles;
export import gfx.profiler.errors;
export import :site;
export import :trace;
export import :session;
export import :report;
export import :exporters;
