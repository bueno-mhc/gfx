// Profiler traces of the CPU backends' steps (docs/profiler-performance-design.md, PF2): a scene of every object kind
// stepped by the reference backend and by Box3D, the trace covering the framework's stages and the backend's, the step's
// sizes as counters; and the framework's soft bodies (stepped for Box3D) in the trace.
#include <gtest/gtest.h>

#include "profiling_scene.hpp"

import std;
import gfx.profiler;
import gfx.physics;

namespace
{

using namespace gfx::physics;
using gfx::profiler::StageCoverage;
using gfx::profiler::StageCoverageKind;

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

constexpr std::array REFERENCE_STAGES{
    StageCoverage{"edits", StageCoverageKind::RECORDED, "Reference: apply the edits", {}},
    StageCoverage{"bodies prepared", StageCoverageKind::RECORDED, "Reference: bodies", {}},
    StageCoverage{"broad phase", StageCoverageKind::RECORDED, "Reference: broad phase", {}},
    StageCoverage{"narrow phase", StageCoverageKind::RECORDED, "Reference: narrow phase", {}},
    StageCoverage{"islands", StageCoverageKind::RECORDED, "Reference: islands", {}},
    StageCoverage{"solver set-up", StageCoverageKind::RECORDED, "Reference: solver set-up", {}},
    StageCoverage{"sub-steps", StageCoverageKind::RECORDED, "Reference: sub-step", {}},
    StageCoverage{"integration", StageCoverageKind::RECORDED, "Reference: integrate", {}},
    StageCoverage{"the solve", StageCoverageKind::RECORDED, "Reference: solve", {}},
    StageCoverage{"results", StageCoverageKind::RECORDED, "Reference: results", {}},
    StageCoverage{"sleep", StageCoverageKind::RECORDED, "Reference: sleep", {}},
    StageCoverage{"events", StageCoverageKind::RECORDED, "Reference: events", {}},
    StageCoverage{"soft bodies (the framework's)", StageCoverageKind::RECORDED, "World: soft bodies", {}},
};

constexpr std::array BOX3D_STAGES{
    StageCoverage{"edits", StageCoverageKind::RECORDED, "Box3D: apply the edits", {}},
    StageCoverage{"Box3D's step", StageCoverageKind::RECORDED, "Box3D: b3World_Step", {}},
    StageCoverage{"Box3D's tasks", StageCoverageKind::RECORDED, "Box3D: task", {}},
    StageCoverage{"bodies captured", StageCoverageKind::RECORDED, "Box3D: capture the bodies", {}},
    StageCoverage{"contacts captured", StageCoverageKind::RECORDED, "Box3D: capture the contacts", {}},
    StageCoverage{"triggers captured", StageCoverageKind::RECORDED, "Box3D: capture the triggers", {}},
    StageCoverage{"joints captured", StageCoverageKind::RECORDED, "Box3D: capture the joints", {}},
    StageCoverage{"soft bodies (the framework's)", StageCoverageKind::RECORDED, "World: soft bodies", {}},
};

TEST(PhysicsProfilingTest, TheReferenceBackendsTraceCoversTheFrameworkAndItsStages)
{
    if (!gfx::profiler::PROFILER_ENABLED)
    {
        GTEST_SKIP() << "the profiler is compiled out in this build (GFX_PROFILER off)";
    }
    gfx::tests::ProfiledScene scene = gfx::tests::make_profiled_scene(create_physics_world({}, PhysicsBackendKind::REFERENCE));
    const gfx::profiler::Trace trace = gfx::tests::trace_steps(scene, 3, "reference");
    const std::vector<std::string> framework = gfx::profiler::missing_stages(trace, gfx::tests::FRAMEWORK_STAGES);
    const std::vector<std::string> backend = gfx::profiler::missing_stages(trace, REFERENCE_STAGES);
    EXPECT_TRUE(framework.empty()) << lines(framework);
    EXPECT_TRUE(backend.empty()) << lines(backend);

    // The step's sizes, as counters.
    EXPECT_EQ(gfx::tests::last_counter(trace, "World: rigid bodies"), gfx::tests::PROFILED_SCENE_BODIES);
    EXPECT_EQ(gfx::tests::last_counter(trace, "World: joints"), 3.0);
    EXPECT_EQ(gfx::tests::last_counter(trace, "World: soft bodies"), 1.0);
    EXPECT_GT(gfx::tests::last_counter(trace, "Reference: manifolds").value_or(0.0), 0.0);

    // Every stage is inside the step: the step's scope is the outermost of its thread.
    const gfx::profiler::Report report = gfx::profiler::make_report(trace);
    const auto step = std::ranges::find(report.stages, std::string_view{"World: step"}, &gfx::profiler::StageReport::name);
    ASSERT_NE(step, report.stages.end());
    EXPECT_EQ(step->calls, 3U);
}

TEST(PhysicsProfilingTest, TheBox3dBackendsTraceCoversTheFrameworkAndItsStages)
{
    if (!gfx::profiler::PROFILER_ENABLED)
    {
        GTEST_SKIP() << "the profiler is compiled out in this build (GFX_PROFILER off)";
    }
    if (!physics_backend_availability(PhysicsBackendKind::BOX3D).available)
    {
        GTEST_SKIP() << "Box3D is not built (GFX_PHYSICS_BACKEND_BOX3D)";
    }

    // Box3D's tasks on four threads (it hands the scheduler none without one).
    ThreadTaskScheduler scheduler{4};
    gfx::tests::ProfiledScene scene = gfx::tests::make_profiled_scene(create_physics_world({.task_scheduler = &scheduler}, PhysicsBackendKind::BOX3D));
    const gfx::profiler::Trace trace = gfx::tests::trace_steps(scene, 3, "Box3D");
    const std::vector<std::string> framework = gfx::profiler::missing_stages(trace, gfx::tests::FRAMEWORK_STAGES);
    const std::vector<std::string> backend = gfx::profiler::missing_stages(trace, BOX3D_STAGES);
    EXPECT_TRUE(framework.empty()) << lines(framework);
    EXPECT_TRUE(backend.empty()) << lines(backend);

    // Box3D's own profile and sizes, as counters; its tasks on the scheduler's named threads.
    EXPECT_TRUE(gfx::tests::last_counter(trace, "Box3D: step (ms)").has_value());
    EXPECT_GT(gfx::tests::last_counter(trace, "Box3D: contacts").value_or(0.0), 0.0);
    EXPECT_TRUE(std::ranges::any_of(trace.threads(), [](const gfx::profiler::TraceThread& thread) { return thread.name.starts_with("physics task"); }));
}

TEST(PhysicsProfilingTest, MissingStagesAreNamed)
{
    // A stage whose site never recorded, a fused stage without a reason: both named; an absent one with its reason: fine.
    const std::array stages{
        StageCoverage{"recorded", StageCoverageKind::RECORDED, "no such site", {}},
        StageCoverage{"fused", StageCoverageKind::FUSED_IN, "no such site either", {}},
        StageCoverage{"absent", StageCoverageKind::ABSENT, {}, "not done here"},
    };
    const std::vector<std::string> missing = gfx::profiler::missing_stages(gfx::profiler::Trace{}, stages);
    ASSERT_EQ(missing.size(), 3U) << lines(missing);
    EXPECT_NE(missing[0].find("'recorded'"), std::string::npos);
    EXPECT_NE(missing[1].find("without a reason"), std::string::npos);
    EXPECT_NE(missing[2].find("no such site either"), std::string::npos);
}

} // namespace
