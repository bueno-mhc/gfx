// Overhead: a scope (two records) costs at most 200 ns, a counter at most 80 ns, measured over millions of them in an
// optimized build with the profiler on (the profile preset). Debug builds and builds without the profiler skip it.
#include <gtest/gtest.h>

import std;
import gfx.profiler;

namespace
{

using namespace gfx::profiler;

TEST(ProfilerOverheadTest, ScopesAndCountersStayWithinTheirBudgets)
{
    if constexpr (!PROFILER_ENABLED || gfx::DEBUG_BUILD)
    {
        GTEST_SKIP() << "measured in optimized builds with the profiler on (python build.py --profile)";
    }
    constexpr int FRAMES = 20;
    constexpr int PER_FRAME = 100'000;
    static constinit const ProfileSite scope_site{"overhead scope"};
    static constinit const ProfileSite counter_site{"overhead counter"};
    static constinit const ProfileSite filtered_site{"overhead filtered", ProfilerCategory::UI};

    Profiler profiler{{.ring_capacity = 1U << 19U}};
    set_current_profiler(&profiler);
    const SessionHandle session = profiler.create_session({.name = "overhead", .categories = ProfilerCategory::GENERAL});
    using Clock = std::chrono::steady_clock;
    Clock::duration scopes{};
    Clock::duration counters{};
    Clock::duration filtered{};
    for (int frame = 0; frame < FRAMES; ++frame)
    {
        // Timed loops; the drain (end_frame) is not.
        profiler.begin_frame(session);
        const Clock::time_point begin = Clock::now();
        for (int index = 0; index < PER_FRAME; ++index)
        {
            const ProfileScope scope{scope_site};
        }
        const Clock::time_point scoped = Clock::now();
        for (int index = 0; index < PER_FRAME; ++index)
        {
            record_counter(counter_site, index);
        }
        const Clock::time_point counted = Clock::now();
        for (int index = 0; index < PER_FRAME; ++index)
        {
            record_counter(filtered_site, index);
        }
        const Clock::time_point skipped = Clock::now();
        scopes += scoped - begin;
        counters += counted - scoped;
        filtered += skipped - counted;
        profiler.end_frame(session);
    }
    const Trace trace = profiler.finalize_session(session);
    set_current_profiler(nullptr);

    const auto per_record = [](Clock::duration total)
    {
        return std::chrono::duration<double, std::nano>(total).count() / (static_cast<double>(FRAMES) * PER_FRAME);
    };
    const double per_scope = per_record(scopes);
    const double per_counter = per_record(counters);
    const double per_filtered = per_record(filtered);
    std::println("profiler overhead: {:.1f} ns per scope, {:.1f} ns per counter, {:.1f} ns per filtered counter", per_scope, per_counter,
                 per_filtered);
    RecordProperty("nanoseconds_per_scope", std::format("{:.1f}", per_scope));
    RecordProperty("nanoseconds_per_counter", std::format("{:.1f}", per_counter));
    EXPECT_LE(per_scope, 200.0);
    EXPECT_LE(per_counter, 80.0);
    EXPECT_LE(per_filtered, 10.0);

    // Every record was kept.
    for (const TraceFrame& frame : trace.frames())
    {
        EXPECT_EQ(frame.dropped_records, 0U);
        EXPECT_EQ(frame.scopes.size(), static_cast<std::size_t>(PER_FRAME));
        EXPECT_EQ(frame.samples.size(), static_cast<std::size_t>(PER_FRAME));
    }
}

} // namespace
