// Determinism: the same workload recorded twice gives the same canonical trace (every record in the same order, with
// the same sites, depths, values and threads; times left out); a different workload gives another.
#include <gtest/gtest.h>
#include <gfx/profiler.hpp>

import std;
import gfx.profiler;

namespace
{

using namespace gfx::profiler;

/// Five frames of nested scopes, counters, markers, a worker thread per frame and spans across frames; @p variant
/// changes one counter's values.
auto
workload(int variant) -> Trace
{
    Profiler profiler;
    set_current_profiler(&profiler);
    set_thread_name("main");
    const SessionHandle session = profiler.create_session({.name = "determinism"});
    for (int frame = 0; frame < 5; ++frame)
    {
        profiler.begin_frame(session);
        {
            PROFILE_SCOPE("step");
            for (int substep = 0; substep < 3; ++substep)
            {
                PROFILE_SCOPE("substep");
                PROFILE_COUNTER("bodies", 10 + substep + frame);
            }
            PROFILE_ASYNC_BEGIN("readback", frame);
            if (frame > 0)
            {
                PROFILE_ASYNC_END("readback", frame - 1);
            }
            std::jthread worker{[frame, variant]
            {
                set_thread_name("worker");
                PROFILE_SCOPE("job");
                PROFILE_COUNTER("items", frame * 2 + variant);
                PROFILE_MEMORY("buffer", 1024 * (frame + 1));
            }};
            worker.join();
            PROFILE_MARKER("done");
        }
        profiler.end_frame(session);
    }
    Trace trace = profiler.finalize_session(session);
    set_current_profiler(nullptr);
    return trace;
}

TEST(ProfilerDeterminismTest, TheSameWorkloadGivesTheSameCanonicalTrace)
{
    if constexpr (!PROFILER_ENABLED)
    {
        GTEST_SKIP() << "the profiler is compiled out (GFX_PROFILER off)";
    }
    const Trace first = workload(0);
    const Trace second = workload(0);
    const std::string canonical = canonical_trace(first);
    EXPECT_EQ(canonical, canonical_trace(second));
    EXPECT_NE(canonical, canonical_trace(workload(1)));

    // The canonical form names every record and no time.
    EXPECT_NE(canonical.find("site 0 'bodies'"), std::string::npos) << canonical;
    EXPECT_NE(canonical.find("thread 0 'main'"), std::string::npos) << canonical;
    EXPECT_EQ(std::ranges::count(canonical, '\n'), std::ranges::count(canonical_trace(second), '\n'));
    std::size_t scopes = 0;
    for (const TraceFrame& frame : first.frames())
    {
        scopes += frame.scopes.size();
        EXPECT_EQ(frame.dropped_records, 0U);
    }
    EXPECT_EQ(scopes, 5U * 5U); // a step, three substeps and a job per frame
    EXPECT_EQ(first.frames()[4].unmatched_records, 1U); // the last frame's span is still open
}

TEST(ProfilerDeterminismTest, ScopesBegunInTheSameClockTickKeepTheOrderTheyRan)
{
    // Empty sibling scopes "a" and "b" in turn: many begin in the tick the one before began (a tick is 100 ns here), and
    // the canonical form still lists them in the order they ran (it sorted them unstably by begin time before: found by
    // the GPU backend's trace-structure test, PF4).
    if constexpr (!PROFILER_ENABLED)
    {
        GTEST_SKIP() << "the profiler is compiled out (GFX_PROFILER off)";
    }
    constexpr int PAIRS = 2000;
    Profiler profiler;
    set_current_profiler(&profiler);
    const SessionHandle session = profiler.create_session({.name = "ties"});
    profiler.begin_frame(session);
    {
        PROFILE_SCOPE("frame");
        for (int pair = 0; pair < PAIRS; ++pair)
        {
            {
                PROFILE_SCOPE("a");
            }
            {
                PROFILE_SCOPE("b");
            }
        }
    }
    profiler.end_frame(session);
    const Trace trace = profiler.finalize_session(session);
    set_current_profiler(nullptr);

    // Sites in name order: "a" 0, "b" 1, "frame" 2; then the frame scope and the pairs in turn.
    const std::string canonical = canonical_trace(trace);
    std::string expected = "  scope thread 0 site 2 depth 0\n";
    for (int pair = 0; pair < PAIRS; ++pair)
    {
        expected += "  scope thread 0 site 0 depth 1\n  scope thread 0 site 1 depth 1\n";
    }
    EXPECT_NE(canonical.find(expected), std::string::npos) << "a scope out of the order it ran in";
    ASSERT_EQ(trace.frames().size(), 1U);
    EXPECT_EQ(trace.frames()[0].scopes.size(), std::size_t{2 * PAIRS + 1});
}

} // namespace
