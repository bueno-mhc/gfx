// Exporters: Chrome Tracing JSON parsed back by nlohmann/json, the binary trace (plain and zstd) read back equal,
// corrupt or truncated bytes refused with ProfilerError, files written and read.
#include <gtest/gtest.h>
#include "chrome_json_reader.hpp"

import std;
import gfx.profiler;

namespace
{

using namespace gfx::profiler;

/// A trace with every kind of record, names that need escaping, two threads and @p frames frames.
auto
sample_trace(int frames) -> Trace
{
    std::vector<TraceSite> sites{
        {.identifier = 1, .name_identifier = 11, .name = "step", .category = ProfilerCategory::PHYSICS, .file = "C:\\step.cpp", .line = 10},
        {.identifier = 2, .name_identifier = 12, .name = "say \"hi\"\n", .category = ProfilerCategory::GENERAL, .file = "a.cpp", .line = 20},
        {.identifier = 3, .name_identifier = 13, .name = "bodies", .category = ProfilerCategory::GENERAL, .file = "a.cpp", .line = 30},
        {.identifier = 4, .name_identifier = 14, .name = "error", .category = ProfilerCategory::GENERAL, .file = "a.cpp", .line = 40},
        {.identifier = 5, .name_identifier = 15, .name = "staging", .category = ProfilerCategory::GPU | ProfilerCategory::GRAPHICS, .file = "a.cpp", .line = 50},
        {.identifier = 6, .name_identifier = 16, .name = "submission", .category = ProfilerCategory::GRAPHICS, .file = "a.cpp", .line = 60},
        {.identifier = 7, .name_identifier = 17, .name = "dispatch", .category = ProfilerCategory::GPU, .file = "a.cpp", .line = 70},
    };
    std::vector<TraceFrame> trace_frames;
    for (int frame = 0; frame < frames; ++frame)
    {
        const std::int64_t base = 1000LL * frame;
        trace_frames.push_back(
            {.index = static_cast<std::uint64_t>(frame),
             .begin = base,
             .end = base + 900,
             .scopes = {{.thread = 0, .site = 0, .begin = base + 10, .end = base + 500, .depth = 0},
                        {.thread = 1, .site = 0, .begin = base + 20, .end = base + 120, .depth = 2}},
             .samples = {{.thread = 0, .site = 2, .time = base + 30, .kind = RecordKind::COUNTER, .value = std::bit_cast<std::uint64_t>(std::int64_t{-frame})},
                         {.thread = 0, .site = 3, .time = base + 31, .kind = RecordKind::COUNTER_FLOAT, .value = std::bit_cast<std::uint64_t>(0.5 * frame)},
                         {.thread = 1, .site = 4, .time = base + 32, .kind = RecordKind::MEMORY, .value = 4096},
                         {.thread = 1, .site = 1, .time = base + 33, .kind = RecordKind::MARKER, .value = 0}},
             .async_spans = {{.site = 5, .token = 0xABCDEF, .begin_thread = 0, .end_thread = 1, .begin = base + 40, .end = base + 800}},
             .gpu_scopes = {{.track = 0, .site = 6, .begin = base + 200, .end = base + 700, .depth = 0, .query = 0, .status = GpuScopeStatus::TIMED},
                            {.track = 0, .site = 6, .begin = base + 210, .end = base + 210, .depth = 1, .query = 2, .status = GpuScopeStatus::DROPPED}},
             .dropped_records = static_cast<std::uint64_t>(frame),
             .unmatched_records = 1});
    }
    return Trace{"exported \"session\"", std::move(sites), {{.name = "main"}, {.name = ""}}, std::move(trace_frames),
                 {{.name = "GPU \"compute\"", .alignment = GpuClockAlignment::CALIBRATED, .deviation = 1500}}};
}

TEST(ProfilerExporterTest, ChromeJsonIsValidAndHoldsEveryEvent)
{
    const std::string json = to_chrome_json(sample_trace(3));
    const ChromeJsonSummary summary = summarize_chrome_json(json.data(), json.size(), "step");
    ASSERT_TRUE(summary.valid) << json.substr(0, 400);
    EXPECT_EQ(summary.metadata, 3 + 2 + 1);  // the frames' track, two threads, the two processes' names, the GPU track
    EXPECT_EQ(summary.complete, 3 * 4);      // a frame, two scopes and a timed GPU scope per frame
    EXPECT_EQ(summary.counters, 3 * 3);      // counter, float counter, size
    EXPECT_EQ(summary.instants, 3 * 2);      // a marker and a dropped GPU scope per frame
    EXPECT_EQ(summary.async_begins, 3);
    EXPECT_EQ(summary.async_ends, 3);
    EXPECT_EQ(summary.named_complete, 6);
    EXPECT_NEAR(summary.named_duration, 3 * (0.490 + 0.100), 1.0e-9); // microseconds
    EXPECT_NE(json.find(R"("name":"say \"hi\"\n")"), std::string::npos);
    EXPECT_NE(json.find(R"("file":"C:\\step.cpp")"), std::string::npos);
    EXPECT_NE(json.find(R"("id":"0xabcdef")"), std::string::npos);
    EXPECT_NE(json.find(R"json("name":"GPU \"compute\" (CALIBRATED, deviation 1500 ns)")json"), std::string::npos) << json.substr(0, 1200);
    EXPECT_NE(json.find(R"json("name":"dispatch (DROPPED)")json"), std::string::npos);
}

TEST(ProfilerExporterTest, BinaryTracesReadBackEqual)
{
    const Trace trace = sample_trace(50);
    const std::vector<std::byte> plain = write_binary_trace(trace);
    const std::vector<std::byte> compressed = write_binary_trace(trace, true);
    EXPECT_EQ(read_binary_trace(plain), trace);
    EXPECT_EQ(read_binary_trace(compressed), trace);
    EXPECT_LT(compressed.size(), plain.size() / 4) << plain.size() << " bytes compressed to " << compressed.size();
    EXPECT_EQ(read_binary_trace(write_binary_trace(Trace{})), Trace{});

    // The header: magic, version, flags.
    ASSERT_GE(plain.size(), PROFILER_TRACE_HEADER_SIZE);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(plain.data()), PROFILER_TRACE_MAGIC.size()), PROFILER_TRACE_MAGIC);
    EXPECT_EQ(static_cast<std::uint8_t>(plain[8]), PROFILER_TRACE_VERSION);
    EXPECT_EQ(static_cast<std::uint8_t>(compressed[12]), 1U);
}

TEST(ProfilerExporterTest, CorruptBinaryTracesAreRefused)
{
    const std::vector<std::byte> bytes = write_binary_trace(sample_trace(2));
    const auto refused = [](std::span<const std::byte> corrupt, std::string_view expected)
    {
        try
        {
            (void)read_binary_trace(corrupt);
            ADD_FAILURE() << "accepted; expected: " << expected;
        }
        catch (const ProfilerError& error)
        {
            EXPECT_EQ(error.subsystem(), ProfilerSubsystem::EXPORT);
            EXPECT_NE(error.problem().find(expected), std::string_view::npos) << error.what();
        }
    };

    refused(std::span{bytes}.first(10), "fewer than a header's");
    std::vector<std::byte> wrong_magic = bytes;
    wrong_magic[0] = std::byte{'X'};
    refused(wrong_magic, "do not begin with 'GFXTRACE'");
    std::vector<std::byte> wrong_version = bytes;
    wrong_version[8] = std::byte{9};
    refused(wrong_version, std::format("format version 9 is not {}", PROFILER_TRACE_VERSION));
    refused(std::span{bytes}.first(bytes.size() - 1), "announces");
    std::vector<std::byte> extra = bytes;
    extra.push_back(std::byte{0});
    refused(extra, "announces");

    // A GPU scope naming a track the trace does not have.
    const Trace trace = sample_trace(1);
    std::vector<TraceFrame> frames{trace.frames().begin(), trace.frames().end()};
    frames[0].gpu_scopes[0].track = 5;
    refused(write_binary_trace(Trace{"wrong track", {trace.sites().begin(), trace.sites().end()}, {trace.threads().begin(), trace.threads().end()},
                                     std::move(frames), {trace.gpu_tracks().begin(), trace.gpu_tracks().end()}}),
            "GPU track 5 of 1");

    // A payload whose sizes lie: the header patched to a shorter payload ends a string early.
    std::vector<std::byte> truncated{bytes.begin(), bytes.begin() + 60};
    const std::uint64_t stored = truncated.size() - PROFILER_TRACE_HEADER_SIZE;
    for (std::size_t byte = 0; byte < 8; ++byte)
    {
        truncated[16 + byte] = static_cast<std::byte>(stored >> (8 * byte));
        truncated[24 + byte] = static_cast<std::byte>(stored >> (8 * byte));
    }
    refused(truncated, "");
}

TEST(ProfilerExporterTest, FilesAreWrittenAndReadBack)
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / std::format("gfx_profiler_test_{}", std::random_device{}());
    std::filesystem::create_directories(directory);
    const Trace trace = sample_trace(4);
    export_trace(trace, TraceFormat::BINARY, directory / "trace.gfxtrace");
    export_trace(trace, TraceFormat::BINARY_COMPRESSED, directory / "trace.gfxtrace.zst");
    export_trace(trace, TraceFormat::CHROME_JSON, directory / "trace.json");
    EXPECT_EQ(import_trace(directory / "trace.gfxtrace"), trace);
    EXPECT_EQ(import_trace(directory / "trace.gfxtrace.zst"), trace);
    EXPECT_GT(std::filesystem::file_size(directory / "trace.json"), 1000U);
    EXPECT_THROW(export_trace(trace, TraceFormat::BINARY, directory / "missing" / "trace.gfxtrace"), ProfilerError);
    EXPECT_THROW((void)import_trace(directory / "absent.gfxtrace"), ProfilerError);
    EXPECT_THROW((void)import_trace(directory / "trace.json"), ProfilerError);
    std::filesystem::remove_all(directory);
}

} // namespace
