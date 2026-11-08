// Plain translation unit: nlohmann/json reads the profiler's Chrome Tracing JSON (chrome_json_reader.hpp).
#include "chrome_json_reader.hpp"

#include <nlohmann/json.hpp>

#include <string>

ChromeJsonSummary
summarize_chrome_json(const char* text,
                      unsigned long long size,
                      const char* complete_name)
{
    ChromeJsonSummary summary;
    const nlohmann::json document = nlohmann::json::parse(text, text + size, nullptr, false);
    if (document.is_discarded() || !document.contains("traceEvents") || !document["traceEvents"].is_array())
    {
        return summary;
    }
    summary.valid = true;
    for (const nlohmann::json& event : document["traceEvents"])
    {
        ++summary.events;
        const std::string phase = event.value("ph", std::string{});
        summary.complete += phase == "X" ? 1 : 0;
        summary.counters += phase == "C" ? 1 : 0;
        summary.instants += phase == "i" ? 1 : 0;
        summary.metadata += phase == "M" ? 1 : 0;
        summary.async_begins += phase == "b" ? 1 : 0;
        summary.async_ends += phase == "e" ? 1 : 0;
        if (phase == "X" && event.value("name", std::string{}) == complete_name)
        {
            ++summary.named_complete;
            summary.named_duration += event.value("dur", 0.0);
        }
    }
    return summary;
}
