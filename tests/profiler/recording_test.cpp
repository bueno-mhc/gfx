// Recording: scopes nest and pair per thread, samples keep their values, spans cross threads and frames, many threads
// record at once, categories filter, full rings drop and count, unmatched records are counted, sessions refuse misuse,
// frame counts stop sessions, sinks see frames. With the profiler compiled out (GFX_PROFILER off), nothing is recorded.
#include <gtest/gtest.h>
#include <gfx/profiler.hpp>

import std;
import gfx.profiler;

namespace
{

using namespace gfx::profiler;

/// A profiler made current for the test (tests needing records skip when the profiler is compiled out).
class ProfilerTest : public testing::Test
{
protected:
    void SetUp() override
    {
        if constexpr (!PROFILER_ENABLED)
        {
            GTEST_SKIP() << "the profiler is compiled out (GFX_PROFILER off): ProfilerCompiledOutTest covers this build";
        }
        set_current_profiler(&m_profiler);
    }

    void TearDown() override
    {
        set_current_profiler(nullptr);
    }

    /// Records @p frames frames of @p body (given the frame's number) in a session of @p description.
    auto record(const SessionDescription& description,
                int frames,
                const std::function<void(int)>& body) -> Trace
    {
        const SessionHandle session = m_profiler.create_session(description);
        for (int frame = 0; frame < frames; ++frame)
        {
            m_profiler.begin_frame(session);
            body(frame);
            m_profiler.end_frame(session);
        }
        return m_profiler.finalize_session(session);
    }

    /// The name of @p site in @p trace.
    static auto name_of(const Trace& trace,
                        std::uint32_t site) -> std::string_view
    {
        return trace.sites()[site].name;
    }

    Profiler m_profiler{};
};

TEST_F(ProfilerTest, ScopesNestAndPairOnTheirThread)
{
    const Trace trace = record({.name = "nesting"}, 1, [](int)
    {
        PROFILE_SCOPE("outer");
        {
            PROFILE_SCOPE("inner");
        }
        {
            PROFILE_SCOPE("inner");
            PROFILE_SCOPE("innermost");
        }
    });
    ASSERT_EQ(trace.frames().size(), 1U);
    const TraceFrame& frame = trace.frames()[0];
    ASSERT_EQ(frame.scopes.size(), 4U);

    // Scopes are kept as they end: inner, innermost, inner, outer.
    EXPECT_EQ(name_of(trace, frame.scopes[0].site), "inner");
    EXPECT_EQ(name_of(trace, frame.scopes[1].site), "innermost");
    EXPECT_EQ(name_of(trace, frame.scopes[2].site), "inner");
    EXPECT_EQ(name_of(trace, frame.scopes[3].site), "outer");
    EXPECT_EQ(frame.scopes[0].depth, 1U);
    EXPECT_EQ(frame.scopes[1].depth, 2U);
    EXPECT_EQ(frame.scopes[2].depth, 1U);
    EXPECT_EQ(frame.scopes[3].depth, 0U);
    const TraceScope& outer = frame.scopes[3];
    for (const TraceScope& scope : frame.scopes)
    {
        EXPECT_LE(outer.begin, scope.begin);
        EXPECT_LE(scope.end, outer.end);
        EXPECT_LE(scope.begin, scope.end);
    }
    EXPECT_LE(frame.begin, outer.begin);
    EXPECT_LE(outer.end, frame.end);
    EXPECT_EQ(frame.dropped_records, 0U);
    EXPECT_EQ(frame.unmatched_records, 0U);

    // The two "inner" scopes are two sites (two lines); a site knows its file.
    EXPECT_NE(frame.scopes[0].site, frame.scopes[2].site);
    EXPECT_EQ(trace.sites()[frame.scopes[3].site].file.ends_with("recording_test.cpp"), true);
}

TEST_F(ProfilerTest, SamplesKeepTheirKindsAndValues)
{
    const Trace trace = record({.name = "samples"}, 1, [](int)
    {
        PROFILE_COUNTER("bodies", 42);
        PROFILE_COUNTER("negative", -7);
        PROFILE_COUNTER_FLOAT("error", 0.25);
        PROFILE_MEMORY("staging", 1U << 20U);
        PROFILE_MARKER("rebuilt");
    });
    const TraceFrame& frame = trace.frames()[0];
    ASSERT_EQ(frame.samples.size(), 5U);
    EXPECT_EQ(frame.samples[0].kind, RecordKind::COUNTER);
    EXPECT_EQ(frame.samples[0].number(), 42.0);
    EXPECT_EQ(frame.samples[1].number(), -7.0);
    EXPECT_EQ(frame.samples[2].kind, RecordKind::COUNTER_FLOAT);
    EXPECT_EQ(frame.samples[2].number(), 0.25);
    EXPECT_EQ(frame.samples[3].kind, RecordKind::MEMORY);
    EXPECT_EQ(frame.samples[3].value, 1U << 20U);
    EXPECT_EQ(frame.samples[4].kind, RecordKind::MARKER);
    EXPECT_EQ(name_of(trace, frame.samples[4].site), "rebuilt");
    for (std::size_t index = 1; index < frame.samples.size(); ++index)
    {
        EXPECT_LE(frame.samples[index - 1].time, frame.samples[index].time);
    }
}

TEST_F(ProfilerTest, SpansCrossThreadsAndFrames)
{
    const Trace trace = record({.name = "spans"}, 3, [](int frame)
    {
        if (frame == 0)
        {
            PROFILE_ASYNC_BEGIN("submission", 7);
            PROFILE_ASYNC_BEGIN("submission", 8);
        }
        if (frame == 1)
        {
            std::jthread worker{[]
            {
                set_thread_name("completion");
                PROFILE_ASYNC_END("submission", 7);
            }};
        }
        if (frame == 2)
        {
            PROFILE_ASYNC_END("submission", 8);
        }
    });
    ASSERT_EQ(trace.frames().size(), 3U);
    EXPECT_TRUE(trace.frames()[0].async_spans.empty());
    ASSERT_EQ(trace.frames()[1].async_spans.size(), 1U);
    ASSERT_EQ(trace.frames()[2].async_spans.size(), 1U);
    const TraceAsyncSpan& crossing = trace.frames()[1].async_spans[0];
    EXPECT_EQ(crossing.token, 7U);
    EXPECT_NE(crossing.begin_thread, crossing.end_thread);
    EXPECT_EQ(trace.threads()[crossing.end_thread].name, "completion");
    EXPECT_LT(crossing.begin, crossing.end);
    EXPECT_EQ(trace.frames()[2].async_spans[0].token, 8U);
    EXPECT_EQ(name_of(trace, crossing.site), "submission");
}

TEST_F(ProfilerTest, ManyThreadsRecordAtOnce)
{
    constexpr int THREADS = 8;
    constexpr int SCOPES = 1000;
    const Trace trace = record({.name = "threads"}, 1, [](int)
    {
        std::vector<std::jthread> workers;
        for (int thread = 0; thread < THREADS; ++thread)
        {
            workers.emplace_back([thread]
            {
                set_thread_name(std::format("worker {}", thread));
                for (int scope = 0; scope < SCOPES; ++scope)
                {
                    PROFILE_SCOPE("job");
                    PROFILE_SCOPE("part");
                    PROFILE_COUNTER("index", scope);
                }
            });
        }
    });
    const TraceFrame& frame = trace.frames()[0];
    EXPECT_EQ(frame.dropped_records, 0U);
    EXPECT_EQ(frame.unmatched_records, 0U);
    ASSERT_EQ(trace.threads().size(), static_cast<std::size_t>(THREADS));
    for (std::uint32_t thread = 0; thread < THREADS; ++thread)
    {
        const auto scopes = std::ranges::count(frame.scopes, thread, &TraceScope::thread);
        const auto samples = std::ranges::count(frame.samples, thread, &TraceSample::thread);
        EXPECT_EQ(scopes, 2 * SCOPES) << trace.threads()[thread].name;
        EXPECT_EQ(samples, SCOPES) << trace.threads()[thread].name;
        EXPECT_TRUE(trace.threads()[thread].name.starts_with("worker ")) << trace.threads()[thread].name;

        // Each thread's counters arrive in the order recorded.
        std::int64_t expected = 0;
        for (const TraceSample& sample : frame.samples)
        {
            if (sample.thread == thread)
            {
                EXPECT_EQ(sample.number(), static_cast<double>(expected++));
            }
        }
    }
}

TEST_F(ProfilerTest, CategoriesChooseWhatIsRecorded)
{
    const Trace trace = record({.name = "categories", .categories = ProfilerCategory::PHYSICS | ProfilerCategory::GPU}, 1, [](int)
    {
        EXPECT_TRUE(is_recording(ProfilerCategory::PHYSICS));
        EXPECT_FALSE(is_recording(ProfilerCategory::UI));
        PROFILE_SCOPE("general");
        PROFILE_SCOPE_CATEGORY("physics", ProfilerCategory::PHYSICS);
        PROFILE_SCOPE_CATEGORY("ui", ProfilerCategory::UI);
    });
    const TraceFrame& frame = trace.frames()[0];
    ASSERT_EQ(frame.scopes.size(), 1U);
    EXPECT_EQ(name_of(trace, frame.scopes[0].site), "physics");
    EXPECT_EQ(trace.sites()[frame.scopes[0].site].category, ProfilerCategory::PHYSICS);
}

TEST_F(ProfilerTest, NothingIsRecordedWithoutASessionOrACurrentProfiler)
{
    // Before the session: not recorded.
    EXPECT_FALSE(is_recording());
    {
        PROFILE_SCOPE("before");
    }
    const SessionHandle session = m_profiler.create_session({.name = "absent"});
    m_profiler.begin_frame(session);
    {
        // No current profiler: not recorded.
        set_current_profiler(nullptr);
        PROFILE_SCOPE("without profiler");
        PROFILE_MARKER("without profiler");
        set_current_profiler(&m_profiler);
        PROFILE_MARKER("recorded");
    }
    m_profiler.end_frame(session);
    const Trace trace = m_profiler.finalize_session(session);
    EXPECT_FALSE(is_recording());
    ASSERT_EQ(trace.frames().size(), 1U);
    EXPECT_TRUE(trace.frames()[0].scopes.empty());
    ASSERT_EQ(trace.frames()[0].samples.size(), 1U);
    EXPECT_EQ(name_of(trace, trace.frames()[0].samples[0].site), "recorded");
}

TEST_F(ProfilerTest, FullRingsDropAndCountRecords)
{
    // A ring of 64 records: 100 markers in a frame keep 64, drop 36; the next frame starts empty.
    Profiler small{{.ring_capacity = 64}};
    set_current_profiler(&small);
    const SessionHandle session = small.create_session({.name = "small ring"});
    small.begin_frame(session);
    for (int marker = 0; marker < 100; ++marker)
    {
        PROFILE_MARKER("tick");
    }
    small.end_frame(session);
    small.begin_frame(session);
    PROFILE_MARKER("tick");
    small.end_frame(session);
    const Trace trace = small.finalize_session(session);
    set_current_profiler(&m_profiler);
    ASSERT_EQ(trace.frames().size(), 2U);
    EXPECT_EQ(trace.frames()[0].samples.size(), 64U);
    EXPECT_EQ(trace.frames()[0].dropped_records, 36U);
    EXPECT_EQ(trace.frames()[1].samples.size(), 1U);
    EXPECT_EQ(trace.frames()[1].dropped_records, 0U);
}

TEST_F(ProfilerTest, UnmatchedRecordsAreCounted)
{
    static constinit const ProfileSite ended{"ended without a begin"};
    static constinit const ProfileSite open{"left open"};
    const SessionHandle session = m_profiler.create_session({.name = "unmatched"});
    m_profiler.begin_frame(session);
    record_scope_end(ended);
    record_scope_begin(open);
    PROFILE_ASYNC_END("never begun", 3);
    m_profiler.end_frame(session);
    const Trace trace = m_profiler.finalize_session(session);
    ASSERT_EQ(trace.frames().size(), 1U);
    // The end without a begin, the span's end without a begin, and the scope left open at finalize.
    EXPECT_EQ(trace.frames()[0].unmatched_records, 3U);
    EXPECT_TRUE(trace.frames()[0].scopes.empty());
}

TEST_F(ProfilerTest, NamedScopesEndEarlyAndFunctionsNameTheirScopes)
{
    const Trace trace = record({.name = "named"}, 1, [](int)
    {
        PROFILE_FUNCTION();
        PROFILE_NAMED_SCOPE(upload, "upload");
        upload.end();
        PROFILE_SCOPE("after upload");
    });
    const TraceFrame& frame = trace.frames()[0];
    ASSERT_EQ(frame.scopes.size(), 3U);
    EXPECT_EQ(name_of(trace, frame.scopes[0].site), "upload");
    EXPECT_EQ(name_of(trace, frame.scopes[1].site), "after upload");
    EXPECT_LE(frame.scopes[0].end, frame.scopes[1].begin);
    EXPECT_EQ(frame.scopes[0].depth, 1U);
    EXPECT_EQ(frame.scopes[1].depth, 1U);
    EXPECT_NE(name_of(trace, frame.scopes[2].site).find("TestBody"), std::string_view::npos) << name_of(trace, frame.scopes[2].site);
}

TEST_F(ProfilerTest, FrameCountsStopSessions)
{
    const SessionHandle session = m_profiler.create_session({.name = "three frames", .frames = 3});
    std::vector<bool> recording;
    for (int frame = 0; frame < 5; ++frame)
    {
        m_profiler.begin_frame(session);
        PROFILE_MARKER("frame");
        recording.push_back(m_profiler.end_frame(session));
    }
    EXPECT_EQ(recording, (std::vector<bool>{true, true, false, false, false}));
    EXPECT_EQ(m_profiler.session_state(session), SessionState::STOPPED);
    EXPECT_FALSE(is_recording());
    const Trace trace = m_profiler.finalize_session(session);
    EXPECT_EQ(trace.frames().size(), 3U);
    EXPECT_EQ(m_profiler.session_state(session), SessionState::FINALIZED);
}

TEST_F(ProfilerTest, SinksSeeEachFrame)
{
    std::vector<std::string> seen;
    const SinkHandle sink = m_profiler.add_sink([&seen](const TraceFrame& frame, std::span<const TraceSite> sites, std::span<const TraceThread>)
    {
        for (const TraceScope& scope : frame.scopes)
        {
            seen.push_back(std::format("{} {}", frame.index, sites[scope.site].name));
        }
    });
    const SessionHandle session = m_profiler.create_session({.name = "sinks"});
    for (int frame = 0; frame < 3; ++frame)
    {
        if (frame == 2)
        {
            m_profiler.remove_sink(sink);
        }
        m_profiler.begin_frame(session);
        {
            PROFILE_SCOPE("work");
        }
        m_profiler.end_frame(session);
    }
    (void)m_profiler.finalize_session(session);
    EXPECT_EQ(seen, (std::vector<std::string>{"0 work", "1 work"}));
    EXPECT_THROW(m_profiler.remove_sink(sink), ProfilerError);
}

/// A GPU source handing out scripted scopes: what collect() returns at each call, and what it returns when waited for.
class ScriptedGpuSource final : public GpuTimingSource
{
public:
    [[nodiscard]] auto track_name() const -> std::string override
    {
        return "scripted GPU";
    }

    [[nodiscard]] auto alignment() const noexcept -> GpuClockAlignment override
    {
        return GpuClockAlignment::ESTIMATED;
    }

    [[nodiscard]] auto alignment_deviation() const noexcept -> std::chrono::nanoseconds override
    {
        return std::chrono::nanoseconds{250};
    }

    void collect(std::vector<GpuScopeRecord>& scopes,
                 bool wait) override
    {
        ++m_collections;
        m_waited = m_waited || wait;
        scopes.insert(scopes.end(), m_ready.begin(), m_ready.end());
        m_ready.clear();
        if (wait)
        {
            scopes.insert(scopes.end(), m_late.begin(), m_late.end());
            m_late.clear();
        }
    }

    std::vector<GpuScopeRecord> m_ready{};
    std::vector<GpuScopeRecord> m_late{};
    int m_collections = 0;
    bool m_waited = false;
};

/// steady_clock nanoseconds now (the GPU sources' timeline).
auto
steady_nanoseconds() -> std::int64_t
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

TEST(ProfilerGpuSourceTest, GpuScopesAreFiledIntoTheFramesTheyBeganIn)
{
    static constinit const ProfileSite dispatch{"dispatch", ProfilerCategory::GPU};
    static constinit const ProfileSite ui_pass{"ui pass", ProfilerCategory::UI};
    Profiler profiler;
    ScriptedGpuSource source;
    EXPECT_FALSE(source.attached());
    const GpuSourceHandle handle = profiler.add_gpu_source(source);
    EXPECT_TRUE(source.attached());
    EXPECT_THROW((void)profiler.add_gpu_source(source), ProfilerError);

    // Frame 0 does GPU work that is read back two frames later; frame 2's work is still running when frame 2 ends, and
    // one scope never comes back until finalize waits; a UI scope is not recorded by this session.
    const SessionHandle session = profiler.create_session({.name = "gpu", .categories = ProfilerCategory::GPU | ProfilerCategory::GENERAL});
    std::vector<GpuScopeRecord> frame_zero_work;
    for (int frame = 0; frame < 3; ++frame)
    {
        profiler.begin_frame(session);
        const std::int64_t now = steady_nanoseconds();
        if (frame == 0)
        {
            frame_zero_work = {{.site = &dispatch, .begin = now + 10, .end = now + 20, .depth = 0, .query = 0, .status = GpuScopeStatus::TIMED},
                               {.site = &ui_pass, .begin = now + 11, .end = now + 12, .depth = 1, .query = 2, .status = GpuScopeStatus::TIMED}};
        }
        if (frame == 2)
        {
            source.m_ready = frame_zero_work;
            source.m_ready.push_back(
                {.site = &dispatch, .begin = now + 1'000'000'000, .end = now + 1'000'000'100, .depth = 0, .query = 0, .status = GpuScopeStatus::TIMED});
            source.m_late.push_back({.site = &dispatch, .begin = now + 5, .end = now + 5, .depth = 0, .query = 2, .status = GpuScopeStatus::INCOMPLETE});
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        profiler.end_frame(session);
    }
    const Trace trace = profiler.finalize_session(session);
    EXPECT_TRUE(source.m_waited);
    EXPECT_GE(source.m_collections, 4);

    ASSERT_EQ(trace.gpu_tracks().size(), 1U);
    EXPECT_EQ(trace.gpu_tracks()[0].name, "scripted GPU");
    EXPECT_EQ(trace.gpu_tracks()[0].alignment, GpuClockAlignment::ESTIMATED);
    EXPECT_EQ(trace.gpu_tracks()[0].deviation, 250);
    ASSERT_EQ(trace.frames().size(), 3U);

    // Frame 0's scope reached frame 0 (the UI one was left out); frame 2 got the incomplete one and the one beginning
    // after it ended (finalize files late ones into the last frame).
    ASSERT_EQ(trace.frames()[0].gpu_scopes.size(), 1U);
    EXPECT_EQ(trace.sites()[trace.frames()[0].gpu_scopes[0].site].name, "dispatch");
    EXPECT_EQ(trace.frames()[0].gpu_scopes[0].duration(), 10);
    EXPECT_TRUE(trace.frames()[1].gpu_scopes.empty());
    ASSERT_EQ(trace.frames()[2].gpu_scopes.size(), 2U);
    EXPECT_EQ(std::ranges::count(trace.frames()[2].gpu_scopes, GpuScopeStatus::INCOMPLETE, &TraceGpuScope::status), 1);
    EXPECT_FALSE(trace.find_site("ui pass").has_value());

    profiler.remove_gpu_source(handle);
    EXPECT_FALSE(source.attached());
    EXPECT_THROW(profiler.remove_gpu_source(handle), ProfilerError);
}

TEST(ProfilerSessionTest, MisuseIsRefusedWithDescriptiveErrors)
{
    // Ring capacities must be powers of two.
    try
    {
        Profiler wrong{{.ring_capacity = 100}};
        ADD_FAILURE() << "a ring of 100 records was accepted";
    }
    catch (const ProfilerError& error)
    {
        EXPECT_EQ(error.subsystem(), ProfilerSubsystem::RECORDING);
        EXPECT_NE(std::string_view{error.what()}.find("[gfx::profiler::RECORDING] Profiler::Profiler"), std::string_view::npos) << error.what();
    }

    Profiler profiler;
    Profiler other;
    EXPECT_THROW((void)profiler.create_session({.categories = ProfilerCategory::NONE}), ProfilerError);
    const SessionHandle session = profiler.create_session({.name = "first"});
    try
    {
        (void)profiler.create_session({.name = "second"});
        ADD_FAILURE() << "a second session was accepted while the first records";
    }
    catch (const ProfilerError& error)
    {
        EXPECT_EQ(error.subsystem(), ProfilerSubsystem::SESSIONS);
        EXPECT_NE(error.problem().find("('first') still records"), std::string_view::npos) << error.what();
    }
    EXPECT_THROW(other.begin_frame(session), ProfilerError);
    EXPECT_THROW(other.end_frame(session), ProfilerError);
    EXPECT_THROW((void)other.session_state(SessionHandle{}), ProfilerError);
    (void)profiler.finalize_session(session);
    EXPECT_THROW((void)profiler.finalize_session(session), ProfilerError);

    // A new session once the first is finalized.
    const SessionHandle next = profiler.create_session({.name = "next"});
    EXPECT_NE(next, session);
    (void)profiler.finalize_session(next);
}

TEST(ProfilerCompiledOutTest, NothingIsRecordedWhenCompiledOut)
{
    if constexpr (PROFILER_ENABLED)
    {
        GTEST_SKIP() << "the profiler records in this build (GFX_PROFILER on): ProfilerTest covers it";
    }
    Profiler profiler;
    set_current_profiler(&profiler);
    const SessionHandle session = profiler.create_session({.name = "compiled out"});
    profiler.begin_frame(session);
    static constinit const ProfileSite site{"compiled out"};
    {
        const ProfileScope scope{site};
        record_marker(site);
        record_counter(site, 3);
        PROFILE_SCOPE("compiled out macro");
        PROFILE_MARKER("compiled out macro");
    }
    EXPECT_FALSE(is_recording());
    profiler.end_frame(session);
    const Trace trace = profiler.finalize_session(session);
    set_current_profiler(nullptr);
    ASSERT_EQ(trace.frames().size(), 1U);
    EXPECT_TRUE(trace.frames()[0].scopes.empty());
    EXPECT_TRUE(trace.frames()[0].samples.empty());
    EXPECT_TRUE(trace.sites().empty());
}

} // namespace
