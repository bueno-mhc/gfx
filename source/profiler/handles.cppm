/// @file handles.cppm
/// @brief The handles of the profiler: one tag per kind of object an application names (sessions, frame sinks).
///        Handle<Tag> comes from gfx.core.handles, which this module re-exports: 32 bits (a slot index and a
///        generation), issued by the profiler, trivially copyable, comparable, hashable and formattable
///        ("ProfilerSession(index 0, generation 1)").
export module gfx.profiler.handles;

import std;
export import gfx.core.handles;

export namespace gfx::profiler
{

/// @brief Tag of SessionHandle.
struct SessionTag
{
    static constexpr std::string_view NAME = "ProfilerSession";
};

/// @brief Tag of GpuSourceHandle.
struct GpuSourceTag
{
    static constexpr std::string_view NAME = "ProfilerGpuSource";
};

/// @brief Tag of SinkHandle.
struct SinkTag
{
    static constexpr std::string_view NAME = "ProfilerSink";
};

/// @brief A recording session of a Profiler.
using SessionHandle = Handle<SessionTag>;

/// @brief A function a Profiler gives each completed frame (a live sink).
using SinkHandle = Handle<SinkTag>;

/// @brief A source of GPU timings a Profiler collects from (GpuTimingSource).
using GpuSourceHandle = Handle<GpuSourceTag>;

} // namespace gfx::profiler

// Instantiated here once, so every module importing the handles shares one instantiation (toolchain defect 13).
template class gfx::Handle<gfx::profiler::SessionTag>;
template class gfx::Handle<gfx::profiler::SinkTag>;
template class gfx::Handle<gfx::profiler::GpuSourceTag>;
