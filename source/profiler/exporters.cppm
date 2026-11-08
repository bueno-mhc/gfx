/// @file exporters.cppm
/// @brief Traces written out and read back:
///        - **Chrome Tracing JSON** (chrome://tracing, ui.perfetto.dev): one track per thread and one for the frames;
///          scopes as complete events, counters and sizes as counter events, markers as instant events, spans as
///          asynchronous events;
///        - **the binary trace**: a 32-byte header (PROFILER_TRACE_MAGIC, PROFILER_TRACE_VERSION, flags, the payload's
///          size and its stored size), then the payload (sites, threads, frames: little-endian, fixed-size records),
///          optionally compressed with zstd; read_binary_trace() gives back the same Trace.
export module gfx.profiler:exporters;

import std;
import gfx.profiler.enums;
import :trace;

export namespace gfx::profiler
{

/// @brief @p trace as Chrome Tracing JSON.
[[nodiscard]] auto to_chrome_json(const Trace& trace) -> std::string;

/// @brief @p trace in the binary format, compressed with zstd when @p compressed.
[[nodiscard]] auto write_binary_trace(const Trace& trace,
                                      bool compressed = false) -> std::vector<std::byte>;

/// @brief The trace @p bytes hold (compressed or not).
/// @throws ProfilerError if @p bytes are not a binary trace of PROFILER_TRACE_VERSION, or are truncated or corrupt.
[[nodiscard]] auto read_binary_trace(std::span<const std::byte> bytes) -> Trace;

/// @brief Writes @p trace to @p path in @p format.
/// @throws ProfilerError if the file cannot be written.
void export_trace(const Trace& trace,
                  TraceFormat format,
                  const std::filesystem::path& path);

/// @brief Reads a binary trace (compressed or not) from @p path.
/// @throws ProfilerError if the file cannot be read or holds no valid binary trace.
[[nodiscard]] auto import_trace(const std::filesystem::path& path) -> Trace;

} // namespace gfx::profiler
