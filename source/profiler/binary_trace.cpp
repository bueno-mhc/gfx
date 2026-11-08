// The binary trace: a 32-byte header (magic, version, flags, payload size, stored size) and the payload — sites,
// threads, frames — little-endian, optionally compressed with zstd. Files: export_trace() and import_trace().
module;

#include <zstd.h>

module gfx.profiler;

import std;
import gfx.profiler.enums;
import gfx.profiler.errors;

namespace gfx::profiler
{

namespace
{

constexpr std::uint32_t COMPRESSED_FLAG = 1U;
constexpr int COMPRESSION_LEVEL = 3;

/// Appends little-endian values to a byte vector.
class ByteWriter
{
public:
    void put_u8(std::uint8_t value)
    {
        m_bytes.push_back(static_cast<std::byte>(value));
    }

    void put_u32(std::uint32_t value)
    {
        for (std::uint32_t shift = 0; shift < 32U; shift += 8U)
        {
            put_u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void put_u64(std::uint64_t value)
    {
        for (std::uint32_t shift = 0; shift < 64U; shift += 8U)
        {
            put_u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void put_i64(std::int64_t value)
    {
        put_u64(std::bit_cast<std::uint64_t>(value));
    }

    void put_string(std::string_view text)
    {
        put_u32(static_cast<std::uint32_t>(text.size()));
        for (const char character : text)
        {
            put_u8(static_cast<std::uint8_t>(character));
        }
    }

    [[nodiscard]] auto bytes() && -> std::vector<std::byte>
    {
        return std::move(m_bytes);
    }

private:
    std::vector<std::byte> m_bytes{};
};

/// Throws ProfilerError: the bytes are no valid binary trace.
[[noreturn]] void
throw_invalid(std::string_view problem)
{
    throw ProfilerError({.subsystem = ProfilerSubsystem::EXPORT,
                         .operation = "read_binary_trace",
                         .problem = std::string{problem},
                         .suggestion = std::format("read a file written by write_binary_trace() or export_trace() (format version {})", PROFILER_TRACE_VERSION)});
}

/// Reads little-endian values, checking every read against the end.
class ByteReader
{
public:
    explicit ByteReader(std::span<const std::byte> bytes) noexcept
        : m_bytes{bytes}
    {
    }

    auto u8() -> std::uint8_t
    {
        require(1);
        return static_cast<std::uint8_t>(m_bytes[m_offset++]);
    }

    auto u32() -> std::uint32_t
    {
        std::uint32_t value = 0;
        for (std::uint32_t shift = 0; shift < 32U; shift += 8U)
        {
            value |= std::uint32_t{u8()} << shift;
        }
        return value;
    }

    auto u64() -> std::uint64_t
    {
        std::uint64_t value = 0;
        for (std::uint32_t shift = 0; shift < 64U; shift += 8U)
        {
            value |= std::uint64_t{u8()} << shift;
        }
        return value;
    }

    auto i64() -> std::int64_t
    {
        return std::bit_cast<std::int64_t>(u64());
    }

    auto string() -> std::string
    {
        const std::uint32_t size = u32();
        require(size);
        std::string text(size, '\0');
        for (char& character : text)
        {
            character = static_cast<char>(m_bytes[m_offset++]);
        }
        return text;
    }

    /// A count of elements each at least @p element_bytes long (refused when the rest cannot hold them).
    auto count(std::size_t element_bytes) -> std::uint32_t
    {
        const std::uint32_t value = u32();
        if (static_cast<std::uint64_t>(value) * element_bytes > m_bytes.size() - m_offset)
        {
            throw_invalid(std::format("a count of {} elements exceeds the {} bytes left", value, m_bytes.size() - m_offset));
        }
        return value;
    }

    [[nodiscard]] auto finished() const noexcept -> bool
    {
        return m_offset == m_bytes.size();
    }

private:
    void require(std::size_t size) const
    {
        if (size > m_bytes.size() - m_offset)
        {
            throw_invalid(std::format("the trace is truncated: {} bytes needed at offset {}, {} left", size, m_offset, m_bytes.size() - m_offset));
        }
    }

    std::span<const std::byte> m_bytes;
    std::size_t m_offset{0};
};

/// The payload of @p trace.
auto
payload_of(const Trace& trace) -> std::vector<std::byte>
{
    ByteWriter writer;
    writer.put_string(trace.name());
    writer.put_u32(static_cast<std::uint32_t>(trace.sites().size()));
    for (const TraceSite& site : trace.sites())
    {
        writer.put_u64(site.identifier);
        writer.put_u64(site.name_identifier);
        writer.put_string(site.name);
        writer.put_u32(std::to_underlying(site.category));
        writer.put_string(site.file);
        writer.put_u32(site.line);
    }
    writer.put_u32(static_cast<std::uint32_t>(trace.threads().size()));
    for (const TraceThread& thread : trace.threads())
    {
        writer.put_string(thread.name);
    }
    writer.put_u32(static_cast<std::uint32_t>(trace.gpu_tracks().size()));
    for (const TraceGpuTrack& track : trace.gpu_tracks())
    {
        writer.put_string(track.name);
        writer.put_u8(std::to_underlying(track.alignment));
        writer.put_i64(track.deviation);
    }
    writer.put_u32(static_cast<std::uint32_t>(trace.frames().size()));
    for (const TraceFrame& frame : trace.frames())
    {
        writer.put_u64(frame.index);
        writer.put_i64(frame.begin);
        writer.put_i64(frame.end);
        writer.put_u64(frame.dropped_records);
        writer.put_u64(frame.unmatched_records);
        writer.put_u32(static_cast<std::uint32_t>(frame.scopes.size()));
        for (const TraceScope& scope : frame.scopes)
        {
            writer.put_u32(scope.thread);
            writer.put_u32(scope.site);
            writer.put_i64(scope.begin);
            writer.put_i64(scope.end);
            writer.put_u32(scope.depth);
        }
        writer.put_u32(static_cast<std::uint32_t>(frame.samples.size()));
        for (const TraceSample& sample : frame.samples)
        {
            writer.put_u32(sample.thread);
            writer.put_u32(sample.site);
            writer.put_i64(sample.time);
            writer.put_u8(std::to_underlying(sample.kind));
            writer.put_u64(sample.value);
        }
        writer.put_u32(static_cast<std::uint32_t>(frame.async_spans.size()));
        for (const TraceAsyncSpan& span : frame.async_spans)
        {
            writer.put_u32(span.site);
            writer.put_u64(span.token);
            writer.put_u32(span.begin_thread);
            writer.put_u32(span.end_thread);
            writer.put_i64(span.begin);
            writer.put_i64(span.end);
        }
        writer.put_u32(static_cast<std::uint32_t>(frame.gpu_scopes.size()));
        for (const TraceGpuScope& scope : frame.gpu_scopes)
        {
            writer.put_u32(scope.track);
            writer.put_u32(scope.site);
            writer.put_i64(scope.begin);
            writer.put_i64(scope.end);
            writer.put_u32(scope.depth);
            writer.put_u32(scope.query);
            writer.put_u8(std::to_underlying(scope.status));
        }
    }
    return std::move(writer).bytes();
}

/// The trace @p payload holds.
auto
trace_of(std::span<const std::byte> payload) -> Trace
{
    ByteReader reader{payload};
    std::string name = reader.string();
    std::vector<TraceSite> sites(reader.count(32));
    for (TraceSite& site : sites)
    {
        site.identifier = reader.u64();
        site.name_identifier = reader.u64();
        site.name = reader.string();
        site.category = static_cast<ProfilerCategory>(reader.u32());
        site.file = reader.string();
        site.line = reader.u32();
    }
    std::vector<TraceThread> threads(reader.count(4));
    for (TraceThread& thread : threads)
    {
        thread.name = reader.string();
    }

    std::vector<TraceGpuTrack> tracks(reader.count(13));
    for (TraceGpuTrack& track : tracks)
    {
        track.name = reader.string();
        const std::uint8_t alignment = reader.u8();
        if (alignment >= ENUMERATOR_COUNT<GpuClockAlignment>)
        {
            throw_invalid(std::format("GPU clock alignment {}", alignment));
        }
        track.alignment = static_cast<GpuClockAlignment>(alignment);
        track.deviation = reader.i64();
    }

    // Indices into the sites, threads and GPU tracks are checked: a reader of the trace may use them unchecked.
    const auto check_site = [&sites](std::uint32_t site)
    {
        if (site >= sites.size())
        {
            throw_invalid(std::format("site {} of {}", site, sites.size()));
        }
        return site;
    };
    const auto check_thread = [&threads](std::uint32_t thread)
    {
        if (thread >= threads.size())
        {
            throw_invalid(std::format("thread {} of {}", thread, threads.size()));
        }
        return thread;
    };
    const auto check_track = [&tracks](std::uint32_t track)
    {
        if (track >= tracks.size())
        {
            throw_invalid(std::format("GPU track {} of {}", track, tracks.size()));
        }
        return track;
    };
    std::vector<TraceFrame> frames(reader.count(52));
    for (TraceFrame& frame : frames)
    {
        frame.index = reader.u64();
        frame.begin = reader.i64();
        frame.end = reader.i64();
        frame.dropped_records = reader.u64();
        frame.unmatched_records = reader.u64();
        frame.scopes.resize(reader.count(28));
        for (TraceScope& scope : frame.scopes)
        {
            scope.thread = check_thread(reader.u32());
            scope.site = check_site(reader.u32());
            scope.begin = reader.i64();
            scope.end = reader.i64();
            scope.depth = reader.u32();
        }
        frame.samples.resize(reader.count(25));
        for (TraceSample& sample : frame.samples)
        {
            sample.thread = check_thread(reader.u32());
            sample.site = check_site(reader.u32());
            sample.time = reader.i64();
            const std::uint8_t kind = reader.u8();
            if (kind >= ENUMERATOR_COUNT<RecordKind>)
            {
                throw_invalid(std::format("record kind {}", kind));
            }
            sample.kind = static_cast<RecordKind>(kind);
            sample.value = reader.u64();
        }
        frame.async_spans.resize(reader.count(36));
        for (TraceAsyncSpan& span : frame.async_spans)
        {
            span.site = check_site(reader.u32());
            span.token = reader.u64();
            span.begin_thread = check_thread(reader.u32());
            span.end_thread = check_thread(reader.u32());
            span.begin = reader.i64();
            span.end = reader.i64();
        }
        frame.gpu_scopes.resize(reader.count(33));
        for (TraceGpuScope& scope : frame.gpu_scopes)
        {
            scope.track = check_track(reader.u32());
            scope.site = check_site(reader.u32());
            scope.begin = reader.i64();
            scope.end = reader.i64();
            scope.depth = reader.u32();
            scope.query = reader.u32();
            const std::uint8_t status = reader.u8();
            if (status >= ENUMERATOR_COUNT<GpuScopeStatus>)
            {
                throw_invalid(std::format("GPU scope status {}", status));
            }
            scope.status = static_cast<GpuScopeStatus>(status);
        }
    }
    if (!reader.finished())
    {
        throw_invalid("bytes follow the last frame");
    }
    return Trace{std::move(name), std::move(sites), std::move(threads), std::move(frames), std::move(tracks)};
}

} // namespace

auto
write_binary_trace(const Trace& trace,
                   bool compressed) -> std::vector<std::byte>
{
    const std::vector<std::byte> payload = payload_of(trace);
    std::vector<std::byte> stored;
    if (compressed)
    {
        stored.resize(ZSTD_compressBound(payload.size()));
        const std::size_t size = ZSTD_compress(stored.data(), stored.size(), payload.data(), payload.size(), COMPRESSION_LEVEL);
        if (ZSTD_isError(size) != 0U)
        {
            throw ProfilerError({.subsystem = ProfilerSubsystem::EXPORT,
                                 .operation = "write_binary_trace",
                                 .problem = std::format("zstd could not compress the trace: {}", ZSTD_getErrorName(size)),
                                 .suggestion = "write it uncompressed"});
        }
        stored.resize(size);
    }

    // The header, then the payload as stored.
    ByteWriter header;
    for (const char character : PROFILER_TRACE_MAGIC)
    {
        header.put_u8(static_cast<std::uint8_t>(character));
    }
    header.put_u32(PROFILER_TRACE_VERSION);
    header.put_u32(compressed ? COMPRESSED_FLAG : 0U);
    header.put_u64(payload.size());
    header.put_u64(compressed ? stored.size() : payload.size());
    std::vector<std::byte> bytes = std::move(header).bytes();
    const std::vector<std::byte>& body = compressed ? stored : payload;
    bytes.insert(bytes.end(), body.begin(), body.end());
    return bytes;
}

auto
read_binary_trace(std::span<const std::byte> bytes) -> Trace
{
    if (bytes.size() < PROFILER_TRACE_HEADER_SIZE)
    {
        throw_invalid(std::format("{} bytes are fewer than a header's {}", bytes.size(), PROFILER_TRACE_HEADER_SIZE));
    }
    ByteReader header{bytes.first(PROFILER_TRACE_HEADER_SIZE)};
    for (const char expected : PROFILER_TRACE_MAGIC)
    {
        if (header.u8() != static_cast<std::uint8_t>(expected))
        {
            throw_invalid(std::format("the bytes do not begin with '{}'", PROFILER_TRACE_MAGIC));
        }
    }
    const std::uint32_t version = header.u32();
    if (version != PROFILER_TRACE_VERSION)
    {
        throw_invalid(std::format("format version {} is not {}", version, PROFILER_TRACE_VERSION));
    }
    const std::uint32_t flags = header.u32();
    const std::uint64_t payload_size = header.u64();
    const std::uint64_t stored_size = header.u64();
    const std::span<const std::byte> stored = bytes.subspan(PROFILER_TRACE_HEADER_SIZE);
    if (stored.size() != stored_size)
    {
        throw_invalid(std::format("the header announces {} bytes after it, {} follow", stored_size, stored.size()));
    }
    if ((flags & COMPRESSED_FLAG) == 0U)
    {
        if (payload_size != stored_size)
        {
            throw_invalid(std::format("an uncompressed payload of {} bytes is stored in {}", payload_size, stored_size));
        }
        return trace_of(stored);
    }
    if (payload_size > std::uint64_t{1} << 40U)
    {
        throw_invalid(std::format("a payload of {} bytes is beyond any trace", payload_size));
    }
    std::vector<std::byte> payload(payload_size);
    const std::size_t size = ZSTD_decompress(payload.data(), payload.size(), stored.data(), stored.size());
    if (ZSTD_isError(size) != 0U || size != payload_size)
    {
        throw_invalid(ZSTD_isError(size) != 0U ? std::format("zstd: {}", ZSTD_getErrorName(size))
                                               : std::format("the payload decompressed to {} bytes, not {}", size, payload_size));
    }
    return trace_of(payload);
}

void
export_trace(const Trace& trace,
             TraceFormat format,
             const std::filesystem::path& path)
{
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file)
    {
        throw ProfilerError({.subsystem = ProfilerSubsystem::EXPORT,
                             .operation = "export_trace",
                             .problem = std::format("cannot open '{}' for writing", path.generic_string()),
                             .suggestion = "check that the directory exists and is writable"});
    }
    if (format == TraceFormat::CHROME_JSON)
    {
        const std::string json = to_chrome_json(trace);
        file.write(json.data(), static_cast<std::streamsize>(json.size()));
    }
    else
    {
        const std::vector<std::byte> bytes = write_binary_trace(trace, format == TraceFormat::BINARY_COMPRESSED);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!file.flush())
    {
        throw ProfilerError({.subsystem = ProfilerSubsystem::EXPORT,
                             .operation = "export_trace",
                             .problem = std::format("writing '{}' failed", path.generic_string()),
                             .suggestion = "check the free space of its disk"});
    }
}

auto
import_trace(const std::filesystem::path& path) -> Trace
{
    std::ifstream file{path, std::ios::binary};
    if (!file)
    {
        throw ProfilerError({.subsystem = ProfilerSubsystem::EXPORT,
                             .operation = "import_trace",
                             .problem = std::format("cannot open '{}' for reading", path.generic_string()),
                             .suggestion = "check the path"});
    }
    const std::vector<char> characters{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    return read_binary_trace(std::as_bytes(std::span{characters}));
}

} // namespace gfx::profiler
