// Chrome Tracing JSON: process 1 holds the CPU (track 0 the frames, track n + 1 the trace's thread n), process 2 the GPU
// (track n + 1 the trace's GPU track n). Times in microseconds with three decimals (the trace's nanoseconds).
module gfx.profiler;

import std;
import gfx.profiler.enums;

namespace gfx::profiler
{

namespace
{

/// @p text as the contents of a JSON string.
auto
escaped(std::string_view text) -> std::string
{
    std::string result;
    result.reserve(text.size());
    for (const char character : text)
    {
        switch (character)
        {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(character) < 0x20U)
                {
                    result += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(character)));
                }
                else
                {
                    result += character;
                }
                break;
        }
    }
    return result;
}

/// Nanoseconds as Chrome's microseconds.
auto
microseconds(std::int64_t nanoseconds) -> std::string
{
    return std::format("{:.3f}", static_cast<double>(nanoseconds) / 1000.0);
}

} // namespace

auto
to_chrome_json(const Trace& trace) -> std::string
{
    const std::span<const TraceSite> sites = trace.sites();
    std::string json = "{\"displayTimeUnit\":\"ns\",\"otherData\":{\"session\":\"" + escaped(trace.name()) + "\"},\"traceEvents\":[\n";
    bool first = true;
    const auto add = [&json, &first](std::string_view event)
    {
        json += first ? "" : ",\n";
        json += event;
        first = false;
    };

    // The tracks' names.
    add(std::format(R"({{"ph":"M","pid":1,"tid":0,"name":"thread_name","args":{{"name":"frames"}}}})"));
    for (std::size_t thread = 0; thread < trace.threads().size(); ++thread)
    {
        const std::string& name = trace.threads()[thread].name;
        add(std::format(R"({{"ph":"M","pid":1,"tid":{},"name":"thread_name","args":{{"name":"{}"}}}})", thread + 1,
                        escaped(name.empty() ? std::format("thread {}", thread) : name)));
    }
    if (!trace.gpu_tracks().empty())
    {
        add(R"({"ph":"M","pid":1,"tid":0,"name":"process_name","args":{"name":"CPU"}})");
        add(R"({"ph":"M","pid":2,"tid":0,"name":"process_name","args":{"name":"GPU"}})");
    }
    for (std::size_t track = 0; track < trace.gpu_tracks().size(); ++track)
    {
        const TraceGpuTrack& gpu_track = trace.gpu_tracks()[track];
        add(std::format(R"json({{"ph":"M","pid":2,"tid":{},"name":"thread_name","args":{{"name":"{} ({}, deviation {} ns)"}}}})json", track + 1, escaped(gpu_track.name),
                        gpu_track.alignment, gpu_track.deviation));
    }

    for (const TraceFrame& frame : trace.frames())
    {
        add(std::format(R"({{"ph":"X","pid":1,"tid":0,"name":"frame {}","cat":"frame","ts":{},"dur":{}}})", frame.index, microseconds(frame.begin),
                        microseconds(frame.end - frame.begin)));
        for (const TraceScope& scope : frame.scopes)
        {
            const TraceSite& site = sites[scope.site];
            add(std::format(R"({{"ph":"X","pid":1,"tid":{},"name":"{}","cat":"{}","ts":{},"dur":{},"args":{{"file":"{}","line":{}}}}})",
                            scope.thread + 1, escaped(site.name), site.category, microseconds(scope.begin), microseconds(scope.duration()),
                            escaped(site.file), site.line));
        }
        for (const TraceSample& sample : frame.samples)
        {
            const TraceSite& site = sites[sample.site];
            if (sample.kind == RecordKind::MARKER)
            {
                add(std::format(R"({{"ph":"i","s":"t","pid":1,"tid":{},"name":"{}","cat":"{}","ts":{}}})", sample.thread + 1, escaped(site.name),
                                site.category, microseconds(sample.time)));
                continue;
            }
            // JSON has no infinities nor NaNs: such samples show as null.
            const std::string value = sample.kind == RecordKind::COUNTER_FLOAT ? (std::isfinite(sample.number()) ? std::format("{}", sample.number()) : "null")
                                      : sample.kind == RecordKind::COUNTER     ? std::format("{}", std::bit_cast<std::int64_t>(sample.value))
                                                                              : std::format("{}", sample.value);
            add(std::format(R"({{"ph":"C","pid":1,"tid":{},"name":"{}","cat":"{}","ts":{},"args":{{"{}":{}}}}})", sample.thread + 1, escaped(site.name),
                            site.category, microseconds(sample.time), sample.kind == RecordKind::MEMORY ? "bytes" : "value", value));
        }
        for (const TraceGpuScope& scope : frame.gpu_scopes)
        {
            // Timed scopes as complete events; untimed ones as instants where they were recorded.
            const TraceSite& site = sites[scope.site];
            if (scope.status == GpuScopeStatus::TIMED)
            {
                add(std::format(R"({{"ph":"X","pid":2,"tid":{},"name":"{}","cat":"{}","ts":{},"dur":{},"args":{{"query":{},"depth":{}}}}})",
                                scope.track + 1, escaped(site.name), site.category, microseconds(scope.begin), microseconds(scope.duration()), scope.query,
                                scope.depth));
            }
            else
            {
                add(std::format(R"json({{"ph":"i","s":"t","pid":2,"tid":{},"name":"{} ({})","cat":"{}","ts":{}}})json", scope.track + 1, escaped(site.name),
                                scope.status, site.category, microseconds(scope.begin)));
            }
        }
        for (const TraceAsyncSpan& span : frame.async_spans)
        {
            const TraceSite& site = sites[span.site];
            add(std::format(R"({{"ph":"b","pid":1,"tid":{},"name":"{}","cat":"{}","id":"0x{:x}","ts":{}}})", span.begin_thread + 1, escaped(site.name),
                            site.category, span.token, microseconds(span.begin)));
            add(std::format(R"({{"ph":"e","pid":1,"tid":{},"name":"{}","cat":"{}","id":"0x{:x}","ts":{}}})", span.end_thread + 1, escaped(site.name),
                            site.category, span.token, microseconds(span.end)));
        }
    }
    json += "\n]}\n";
    return json;
}

} // namespace gfx::profiler
