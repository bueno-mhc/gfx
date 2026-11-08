// Reads Chrome Tracing JSON with nlohmann/json in a plain translation unit (its headers cannot share one with
// `import std`): this header is std-free.
#pragma once

/// What a Chrome Tracing JSON document holds.
struct ChromeJsonSummary
{
    bool valid = false;            ///< Parsed, with a "traceEvents" array.
    long long events = 0;          ///< Every event.
    long long complete = 0;        ///< "X" events.
    long long counters = 0;        ///< "C" events.
    long long instants = 0;        ///< "i" events.
    long long metadata = 0;        ///< "M" events.
    long long async_begins = 0;    ///< "b" events.
    long long async_ends = 0;      ///< "e" events.
    long long named_complete = 0;  ///< "X" events named as asked.
    double named_duration = 0.0;   ///< Their "dur" summed (microseconds).
};

/// Summarizes @p size bytes of JSON at @p text, counting the complete events named @p complete_name.
ChromeJsonSummary summarize_chrome_json(const char* text,
                                        unsigned long long size,
                                        const char* complete_name);
