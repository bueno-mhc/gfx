// Profiler traces of the GPU backend's steps (docs/profiler-performance-design.md, PF2), on each GPU of the machine with
// the Vulkan validation layers: a scene of every object kind traced covers every stage of section A5 (scopes on the CPU,
// GPU scopes through the backend's GpuTimer, added to the current profiler by the step itself) and the framework's; the
// GPU scopes lie inside their submissions on the CPU's timeline; the same steps give the same trace structure; the step's
// phases are timed on the GPU with or without a profiler (StepStatistics::phase_seconds, in every build).
#include "vulkan_test.hpp"
#include "profiling_scene.hpp"

import gfx.profiler;
import gfx.physics;
import gfx.physics.gpu_xpbd;

namespace
{

using namespace gfx::physics;
namespace gpu = gfx::physics::gpu_xpbd;

/// Every physical device of the machine that runs gfx.
auto
physical_devices() -> std::vector<std::size_t>
{
    std::vector<std::size_t> devices;
    if (!gfx::backend_availability(gfx::BackendKind::VULKAN).available)
    {
        return devices;
    }
    const std::unique_ptr<gfx::Instance> instance =
        gfx::create_instance(gfx::BackendKind::VULKAN, {.application_name = "gfx GPU physics tests (enumeration)"});
    for (std::size_t index = 0; index < instance->physical_device_count(); ++index)
    {
        if (instance->physical_device(index).missing_core_features().empty())
        {
            devices.push_back(index);
        }
    }
    return devices;
}

/// Each line of @p missing, for a failure message.
auto
lines(const std::vector<std::string>& missing) -> std::string
{
    std::string text;
    for (const std::string& line : missing)
    {
        text += "\n  " + line;
    }
    return text;
}

/// A world on each GPU, with the validation layers.
class GpuXpbdProfilingTest : public gfx::tests::VulkanDeviceTest,
                             public testing::WithParamInterface<std::size_t>
{
protected:
    [[nodiscard]] auto physical_device() -> gfx::PhysicalDevice& override
    {
        return m_instance->physical_device(GetParam());
    }

    /// The profiled scene, its cloth large enough for the sorts of its particles to take radix passes.
    [[nodiscard]] auto scene() -> gfx::tests::ProfiledScene
    {
        return gfx::tests::make_profiled_scene(gpu::create_physics_world({}, *m_device), CLOTH_COLUMNS);
    }

    static constexpr std::uint32_t CLOTH_COLUMNS = 66;
};

TEST_P(GpuXpbdProfilingTest, TheTraceCoversEveryStage)
{
    if (!gfx::profiler::PROFILER_ENABLED)
    {
        GTEST_SKIP() << "the profiler is compiled out in this build (GFX_PROFILER off)";
    }
    gfx::tests::ProfiledScene profiled = scene();
    const gfx::profiler::Trace trace = gfx::tests::trace_steps(profiled, 3, "GPU XPBD");
    const std::vector<std::string> framework = gfx::profiler::missing_stages(trace, gfx::tests::FRAMEWORK_STAGES);
    const std::vector<std::string> backend = gfx::profiler::missing_stages(trace, gpu::stage_coverage());
    EXPECT_TRUE(framework.empty()) << lines(framework);
    EXPECT_TRUE(backend.empty()) << lines(backend);

    // One GPU track, the backend's; every GPU scope timed (none dropped).
    ASSERT_EQ(trace.gpu_tracks().size(), 1U);
    EXPECT_TRUE(trace.gpu_tracks()[0].name.starts_with("GPU XPBD: "));
    std::size_t gpu_scopes = 0;
    for (const gfx::profiler::TraceFrame& frame : trace.frames())
    {
        for (const gfx::profiler::TraceGpuScope& scope : frame.gpu_scopes)
        {
            ++gpu_scopes;
            EXPECT_EQ(scope.status, gfx::profiler::GpuScopeStatus::TIMED) << trace.sites()[scope.site].name;
            EXPECT_LE(scope.begin, scope.end) << trace.sites()[scope.site].name;
        }
    }
    EXPECT_GT(gpu_scopes, 100U);

    // The step's sizes, as counters.
    EXPECT_EQ(gfx::tests::last_counter(trace, "GPU XPBD: bodies"), gfx::tests::PROFILED_SCENE_BODIES);
    EXPECT_EQ(gfx::tests::last_counter(trace, "GPU XPBD: particles"), CLOTH_COLUMNS * CLOTH_COLUMNS);
    EXPECT_EQ(gfx::tests::last_counter(trace, "GPU XPBD: joints"), 3.0);
    EXPECT_GT(gfx::tests::last_counter(trace, "GPU XPBD: dispatches").value_or(0.0), 100.0);
    EXPECT_GT(gfx::tests::last_counter(trace, "GPU XPBD: colors").value_or(0.0), 0.0);
    EXPECT_GT(gfx::tests::last_counter(trace, "GPU XPBD: contact points").value_or(0.0), 0.0);
}

TEST_P(GpuXpbdProfilingTest, GpuScopesLieInsideTheirSubmissions)
{
    if (!gfx::profiler::PROFILER_ENABLED)
    {
        GTEST_SKIP() << "the profiler is compiled out in this build (GFX_PROFILER off)";
    }
    gfx::tests::ProfiledScene profiled = scene();
    const gfx::profiler::Trace trace = gfx::tests::trace_steps(profiled, 3, "GPU XPBD");

    // Each submission's window on the CPU: from its submit's beginning to the end of the wait after it.
    const std::optional<std::uint32_t> submit_site = trace.find_site("GPU XPBD: submit");
    const std::optional<std::uint32_t> wait_site = trace.find_site("GPU XPBD: wait for the GPU");
    ASSERT_TRUE(submit_site && wait_site);
    std::vector<std::int64_t> submits;
    std::vector<std::int64_t> waits;
    for (const gfx::profiler::TraceFrame& frame : trace.frames())
    {
        for (const gfx::profiler::TraceScope& scope : frame.scopes)
        {
            if (scope.site == *submit_site)
            {
                submits.push_back(scope.begin);
            }
            if (scope.site == *wait_site)
            {
                waits.push_back(scope.end);
            }
        }
    }
    std::ranges::sort(submits);
    std::ranges::sort(waits);
    ASSERT_EQ(submits.size(), waits.size());
    ASSERT_FALSE(submits.empty());

    // Every timed GPU scope inside one of them, within the alignment's deviation (and 50 µs of rounding).
    ASSERT_EQ(trace.gpu_tracks().size(), 1U);
    const std::int64_t slack = trace.gpu_tracks()[0].deviation + 50'000;
    for (const gfx::profiler::TraceFrame& frame : trace.frames())
    {
        for (const gfx::profiler::TraceGpuScope& scope : frame.gpu_scopes)
        {
            bool inside = false;
            for (std::size_t submission = 0; submission < submits.size() && !inside; ++submission)
            {
                inside = submits[submission] - slack <= scope.begin && scope.end <= waits[submission] + slack;
            }
            EXPECT_TRUE(inside) << trace.sites()[scope.site].name << " [" << scope.begin << ", " << scope.end << "] ns";
        }
    }
}

TEST_P(GpuXpbdProfilingTest, TheSameStepsGiveTheSameTraceStructure)
{
    if (!gfx::profiler::PROFILER_ENABLED)
    {
        GTEST_SKIP() << "the profiler is compiled out in this build (GFX_PROFILER off)";
    }
    gfx::tests::ProfiledScene first = scene();
    const std::string first_trace = gfx::profiler::canonical_trace(gfx::tests::trace_steps(first, 3, "GPU XPBD"));
    gfx::tests::ProfiledScene second = scene();
    const std::string second_trace = gfx::profiler::canonical_trace(gfx::tests::trace_steps(second, 3, "GPU XPBD"));
    EXPECT_EQ(first_trace, second_trace);
}

TEST_P(GpuXpbdProfilingTest, PhasesAreTimedOnTheGpuWithoutAProfiler)
{
    // StepStatistics::phase_seconds from the GPU's timestamps in every build, the profiler compiled in or not.
    gfx::tests::ProfiledScene profiled = scene();
    for (int step = 0; step < 3; ++step)
    {
        profiled.world->step(1.0F / 60.0F);
    }
    const StepStatistics statistics = profiled.world->statistics();
    for (const StepPhase phase : {StepPhase::BROAD_PHASE, StepPhase::NARROW_PHASE, StepPhase::SOLVER, StepPhase::SOFT_BODIES, StepPhase::EVENTS})
    {
        EXPECT_GT(statistics.phase_seconds[std::to_underlying(phase)], 0.0) << gfx::to_string(phase);
        EXPECT_LT(statistics.phase_seconds[std::to_underlying(phase)], 1.0) << gfx::to_string(phase);
    }
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuXpbdProfilingTest,
                         testing::ValuesIn(physical_devices()),
                         [](const testing::TestParamInfo<std::size_t>& parameter) { return std::format("Device{}", parameter.param); });
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuXpbdProfilingTest);

} // namespace
