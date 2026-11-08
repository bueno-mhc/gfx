// GPU timings on every GPU of the machine, with the validation layers: calibrated timestamps against steady_clock, GPU
// scopes around known work placed inside the CPU's submission, nesting, dropped and incomplete scopes, the same work using
// the same queries, a profiler session showing GPU scopes (traces, reports, files); and a device without timestamps.
#include <gfx/profiler.hpp>
#include "vulkan_test.hpp"

import gfx.profiler.graphics;
import gfx.tests.fake_backend;

namespace
{

using namespace gfx;
using namespace gfx::profiler;

/// steady_clock nanoseconds now.
auto
steady_now() -> std::int64_t
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// Every physical device of the machine that runs gfx.
auto
timed_physical_devices() -> std::vector<std::size_t>
{
    std::vector<std::size_t> devices;
    if (!backend_availability(BackendKind::VULKAN).available)
    {
        return devices;
    }
    const std::unique_ptr<Instance> instance = create_instance(BackendKind::VULKAN, {.application_name = "gfx GPU timer tests (enumeration)"});
    for (std::size_t index = 0; index < instance->physical_device_count(); ++index)
    {
        if (instance->physical_device(index).missing_core_features().empty())
        {
            devices.push_back(index);
        }
    }
    return devices;
}

/// The sites the tests record.
constinit const ProfileSite OUTER_SITE{"outer", ProfilerCategory::GPU};
constinit const ProfileSite FILL_SITE{"fill", ProfilerCategory::GPU};
constinit const ProfileSite COPY_SITE{"copy", ProfilerCategory::GPU};

/// A device on one GPU, two 32 MiB buffers to fill and copy.
class GpuTimerTest : public tests::VulkanDeviceTest,
                     public testing::WithParamInterface<std::size_t>
{
protected:
    static constexpr std::uint64_t BYTES = 32U << 20U;

    void SetUp() override
    {
        VulkanDeviceTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        m_source = m_device->create_buffer({.size = BYTES, .usage = BufferUsage::TRANSFER_SOURCE | BufferUsage::TRANSFER_DESTINATION, .bindless = false});
        m_destination = m_device->create_buffer({.size = BYTES, .usage = BufferUsage::TRANSFER_DESTINATION, .bindless = false});
    }

    [[nodiscard]] auto physical_device() -> PhysicalDevice& override
    {
        return m_instance->physical_device(GetParam());
    }

    void release_device_objects() override
    {
        m_source.reset();
        m_destination.reset();
    }

    /// Records a frame of the timer on the compute queue (outer, then fill and copy inside it), submits it and waits;
    /// the steady_clock nanoseconds before the submission and after the wait.
    auto timed_frame(GpuTimer& timer,
                     bool submit = true) -> std::pair<std::int64_t, std::int64_t>
    {
        std::unique_ptr<CommandPool> pool = m_device->create_command_pool({.queue_type = QueueType::COMPUTE, .debug_name = "timed frame"});
        std::unique_ptr<CommandBuffer> commands = pool->allocate(CommandBufferLevel::PRIMARY, "timed frame");
        {
            ComputeRecorder recorder{*commands};
            timer.begin_frame(recorder);
            const auto outer = timer.scope(recorder, OUTER_SITE);
            {
                const auto fill = timer.scope(recorder, FILL_SITE);
                recorder.fill_buffer(m_source->handle(), 0, BYTES, 0x12345678U);
            }
            const std::array barriers{buffer_barrier(m_source->handle(), ResourceState::TRANSFER_DESTINATION, ResourceState::TRANSFER_SOURCE)};
            recorder.barrier({.buffer_barriers = barriers});
            {
                const auto copy = timer.scope(recorder, COPY_SITE);
                const std::array regions{BufferCopyRegion{.size = BYTES}};
                recorder.copy_buffer(m_source->handle(), m_destination->handle(), regions);
            }
        }
        const std::int64_t before = steady_now();
        if (submit)
        {
            submit_and_wait(m_device->queue(QueueType::COMPUTE), commands->handle());
        }
        return {before, steady_now()};
    }

    std::unique_ptr<Buffer> m_source{};
    std::unique_ptr<Buffer> m_destination{};
};

TEST_P(GpuTimerTest, CalibratedTimestampsFollowSteadyClock)
{
    if (!m_device->enabled(DeviceFeature::CALIBRATED_TIMESTAMPS))
    {
        EXPECT_THROW((void)m_device->calibrate_timestamps(), BackendCapabilityError);
        GTEST_SKIP() << "'" << m_physical_device->properties().name << "' has no calibrated timestamps (GpuTimer estimates)";
    }
    const std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();
    const TimestampCalibration first = m_device->calibrate_timestamps();
    const std::chrono::steady_clock::time_point after = std::chrono::steady_clock::now();
    EXPECT_GE(first.cpu_time, before - first.deviation);
    EXPECT_LE(first.cpu_time, after + first.deviation);
    EXPECT_LT(first.deviation, std::chrono::milliseconds{1});

    // Fifty milliseconds later both clocks advanced alike (within 0.1 % and the deviations).
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    const TimestampCalibration second = m_device->calibrate_timestamps();
    const double period = m_physical_device->properties().limits.timestamp_period_nanoseconds;
    const double gpu_elapsed = static_cast<double>(second.gpu_ticks - first.gpu_ticks) * period;
    const auto cpu_elapsed = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(second.cpu_time - first.cpu_time).count());
    EXPECT_NEAR(gpu_elapsed, cpu_elapsed, 0.001 * cpu_elapsed + static_cast<double>((first.deviation + second.deviation).count()) + 1000.0)
        << "GPU " << gpu_elapsed << " ns, CPU " << cpu_elapsed << " ns";
    std::println("{}: calibrated, deviation {} ns", m_physical_device->properties().name, first.deviation.count());
}

TEST_P(GpuTimerTest, ScopesTimeTheirWorkInsideItsSubmission)
{
    GpuTimer timer{*m_device, {.queue = QueueType::COMPUTE, .frames_in_flight = 2, .always = true}};
    EXPECT_NE(timer.alignment(), GpuClockAlignment::UNAVAILABLE);
    for (int frame = 0; frame < 3; ++frame)
    {
        const auto [before, after] = timed_frame(timer);
        std::vector<GpuScopeRecord> scopes;
        timer.collect(scopes, true);
        ASSERT_EQ(scopes.size(), 3U);

        // In query order: outer (0) around fill (2) and copy (4).
        EXPECT_EQ(scopes[0].site, &OUTER_SITE);
        EXPECT_EQ(scopes[1].site, &FILL_SITE);
        EXPECT_EQ(scopes[2].site, &COPY_SITE);
        EXPECT_EQ(scopes[0].query, 0U);
        EXPECT_EQ(scopes[1].query, 2U);
        EXPECT_EQ(scopes[2].query, 4U);
        EXPECT_EQ(scopes[0].depth, 0U);
        EXPECT_EQ(scopes[1].depth, 1U);
        EXPECT_EQ(scopes[2].depth, 1U);
        for (const GpuScopeRecord& scope : scopes)
        {
            EXPECT_EQ(scope.status, GpuScopeStatus::TIMED);
            EXPECT_LT(scope.begin, scope.end);
            EXPECT_LE(scopes[0].begin, scope.begin);
            EXPECT_LE(scope.end, scopes[0].end);
        }
        EXPECT_LE(scopes[1].end, scopes[2].begin);

        // On the CPU's timeline, inside the submission (within the alignment's deviation and 0.2 ms).
        const std::int64_t margin = timer.alignment_deviation().count() + 200'000;
        EXPECT_GE(scopes[0].begin, before - margin) << "began " << (before - scopes[0].begin) << " ns before the submission";
        EXPECT_LE(scopes[0].end, after + margin) << "ended " << (scopes[0].end - after) << " ns after the wait";
        if (frame == 2)
        {
            std::println("{}: {} alignment (deviation {} ns); fill {:.1f} us, copy {:.1f} us of 32 MiB", m_physical_device->properties().name,
                         timer.alignment(), timer.alignment_deviation().count(), static_cast<double>(scopes[1].end - scopes[1].begin) / 1000.0,
                         static_cast<double>(scopes[2].end - scopes[2].begin) / 1000.0);
        }
    }
}

TEST_P(GpuTimerTest, EstimatedAlignmentPlacesScopesInsideTheirSubmissionToo)
{
    // Without calibrated timestamps: the offset from submissions writing a timestamp, off by half a round trip at most.
    GpuTimer timer{*m_device, {.queue = QueueType::COMPUTE, .frames_in_flight = 2, .always = true, .calibrated = false}};
    EXPECT_EQ(timer.alignment(), GpuClockAlignment::ESTIMATED);
    EXPECT_GT(timer.alignment_deviation().count(), 0);
    EXPECT_LT(timer.alignment_deviation(), std::chrono::milliseconds{5});
    for (int frame = 0; frame < 3; ++frame)
    {
        const auto [before, after] = timed_frame(timer);
        std::vector<GpuScopeRecord> scopes;
        timer.collect(scopes, true);
        ASSERT_EQ(scopes.size(), 3U);
        const std::int64_t margin = timer.alignment_deviation().count() + 200'000;
        EXPECT_GE(scopes[0].begin, before - margin) << "began " << (before - scopes[0].begin) << " ns before the submission";
        EXPECT_LE(scopes[0].end, after + margin) << "ended " << (scopes[0].end - after) << " ns after the wait";
    }
    std::println("{}: estimated alignment, deviation {} ns", m_physical_device->properties().name, timer.alignment_deviation().count());
}

TEST_P(GpuTimerTest, FramesBeyondTheirQueriesDropScopes)
{
    // One timed scope per frame: outer gets it, fill and copy are dropped (recorded where they were recorded).
    GpuTimer timer{*m_device, {.queue = QueueType::COMPUTE, .frames_in_flight = 1, .scopes_per_frame = 1, .always = true}};
    (void)timed_frame(timer);
    std::vector<GpuScopeRecord> scopes;
    timer.collect(scopes, true);
    ASSERT_EQ(scopes.size(), 3U);
    EXPECT_EQ(scopes[0].status, GpuScopeStatus::TIMED);
    EXPECT_EQ(scopes[1].status, GpuScopeStatus::DROPPED);
    EXPECT_EQ(scopes[2].status, GpuScopeStatus::DROPPED);
    EXPECT_EQ(scopes[1].begin, scopes[1].end);
    EXPECT_EQ(timer.latest().size(), 3U);
}

TEST_P(GpuTimerTest, FramesNeverSubmittedAreIncomplete)
{
    GpuTimer timer{*m_device, {.queue = QueueType::COMPUTE, .frames_in_flight = 2, .always = true, .wait_limit = std::chrono::milliseconds{50}}};
    (void)timed_frame(timer, false);
    std::vector<GpuScopeRecord> scopes;
    timer.collect(scopes, false);
    EXPECT_TRUE(scopes.empty()); // the current frame is read only when waited for
    timer.collect(scopes, true);
    ASSERT_EQ(scopes.size(), 3U);
    for (const GpuScopeRecord& scope : scopes)
    {
        EXPECT_EQ(scope.status, GpuScopeStatus::INCOMPLETE);
    }
}

TEST_P(GpuTimerTest, TheSameWorkUsesTheSameQueries)
{
    const auto run = [this]
    {
        GpuTimer timer{*m_device, {.queue = QueueType::COMPUTE, .always = true}};
        std::vector<std::tuple<const ProfileSite*, std::uint32_t, std::uint32_t, GpuScopeStatus>> shape;
        std::vector<GpuScopeRecord> scopes;
        for (int frame = 0; frame < 4; ++frame)
        {
            (void)timed_frame(timer);
            timer.collect(scopes, true);
        }
        for (const GpuScopeRecord& scope : scopes)
        {
            shape.emplace_back(scope.site, scope.query, scope.depth, scope.status);
        }
        return shape;
    };
    const auto first = run();
    EXPECT_EQ(first.size(), 12U);
    EXPECT_EQ(first, run());
}

TEST_P(GpuTimerTest, ProfilerSessionsShowGpuScopes)
{
    if constexpr (!PROFILER_ENABLED)
    {
        GTEST_SKIP() << "the profiler is compiled out (GFX_PROFILER off)";
    }
    Profiler profiler;
    set_current_profiler(&profiler);
    GpuTimer timer{*m_device, {.queue = QueueType::COMPUTE}};
    const GpuSourceHandle source = profiler.add_gpu_source(timer);
    const SessionHandle session = profiler.create_session({.name = "GPU frames"});
    static constinit const ProfileSite submit_site{"submit and wait"};
    for (int frame = 0; frame < 4; ++frame)
    {
        profiler.begin_frame(session);
        {
            const ProfileScope scope{submit_site};
            (void)timed_frame(timer);
        }
        profiler.end_frame(session);
    }
    const Trace trace = profiler.finalize_session(session);
    profiler.remove_gpu_source(source);
    set_current_profiler(nullptr);

    // Every frame's GPU scopes, each inside the frame's CPU scope around the submission.
    ASSERT_EQ(trace.gpu_tracks().size(), 1U);
    EXPECT_NE(trace.gpu_tracks()[0].name.find("COMPUTE"), std::string::npos) << trace.gpu_tracks()[0].name;
    const std::int64_t margin = trace.gpu_tracks()[0].deviation + 200'000;
    ASSERT_EQ(trace.frames().size(), 4U);
    for (const TraceFrame& frame : trace.frames())
    {
        ASSERT_EQ(frame.gpu_scopes.size(), 3U) << "frame " << frame.index;
        ASSERT_EQ(frame.scopes.size(), 1U);
        for (const TraceGpuScope& scope : frame.gpu_scopes)
        {
            EXPECT_EQ(scope.status, GpuScopeStatus::TIMED);
            EXPECT_GE(scope.begin, frame.scopes[0].begin - margin);
            EXPECT_LE(scope.end, frame.scopes[0].end + margin);
        }
    }
    const Report report = make_report(trace);
    ASSERT_EQ(report.gpu_stages.size(), 3U);
    EXPECT_EQ(report.gpu_stages[0].name, "outer");
    EXPECT_GT(report.gpu_time.mean, 0.0);
    EXPECT_GT(report.overlap_ratio, 0.0);
    EXPECT_LT(report.idle_ratio, 1.0);
    EXPECT_EQ(read_binary_trace(write_binary_trace(trace, true)), trace);
    EXPECT_NE(to_chrome_json(trace).find(R"("pid":2)"), std::string::npos);
}

TEST_P(GpuTimerTest, GpuMacrosTimeThroughTheTimer)
{
    if constexpr (!PROFILER_ENABLED)
    {
        GTEST_SKIP() << "the profiler is compiled out (GFX_PROFILER off)";
    }
    Profiler profiler;
    set_current_profiler(&profiler);
    GpuTimer timer{*m_device, {.queue = QueueType::GRAPHICS}};
    const GpuSourceHandle source = profiler.add_gpu_source(timer);
    const SessionHandle session = profiler.create_session({.name = "macros", .categories = ProfilerCategory::GPU});
    profiler.begin_frame(session);
    run<QueueType::GRAPHICS>([&](GraphicsRecorder& recorder)
    {
        timer.begin_frame(recorder);
        PROFILE_GPU_SCOPE("macro fill", recorder, timer);
        recorder.fill_buffer(m_source->handle(), 0, BYTES, 7U);
    });
    profiler.end_frame(session);
    const Trace trace = profiler.finalize_session(session);
    profiler.remove_gpu_source(source);
    set_current_profiler(nullptr);
    ASSERT_EQ(trace.frames()[0].gpu_scopes.size(), 1U);
    EXPECT_EQ(trace.sites()[trace.frames()[0].gpu_scopes[0].site].name, "macro fill");
    EXPECT_EQ(trace.frames()[0].gpu_scopes[0].status, GpuScopeStatus::TIMED);
}

INSTANTIATE_TEST_SUITE_P(Gpu,
                         GpuTimerTest,
                         testing::ValuesIn(timed_physical_devices()),
                         [](const testing::TestParamInfo<std::size_t>& parameter) { return std::format("Device{}", parameter.param); });
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GpuTimerTest);

TEST(GpuTimerFakeDeviceTest, QueuesWithoutTimestampsGiveUnavailableScopes)
{
    // The fake device's queues report no timestamp bits: scopes are recorded, untimed, and nothing is reset or written.
    tests::FakePhysicalDevice physical_device{PhysicalDeviceType::DISCRETE_GPU};
    const std::unique_ptr<Device> device = physical_device.create_device({});
    GpuTimer timer{*device, {.queue = QueueType::GRAPHICS, .always = true}};
    EXPECT_EQ(timer.alignment(), GpuClockAlignment::UNAVAILABLE);
    std::unique_ptr<CommandPool> pool = device->create_command_pool({.queue_type = QueueType::GRAPHICS});
    std::unique_ptr<CommandBuffer> commands = pool->allocate(CommandBufferLevel::PRIMARY, "fake");
    {
        GraphicsRecorder recorder{*commands};
        timer.begin_frame(recorder);
        const auto outer = timer.scope(recorder, OUTER_SITE);
        const auto inner = timer.scope(recorder, FILL_SITE);
    }
    std::vector<GpuScopeRecord> scopes;
    timer.collect(scopes, true);
    ASSERT_EQ(scopes.size(), 2U);
    EXPECT_EQ(scopes[0].status, GpuScopeStatus::UNAVAILABLE);
    EXPECT_EQ(scopes[1].status, GpuScopeStatus::UNAVAILABLE);
    EXPECT_EQ(scopes[1].depth, 1U);
    EXPECT_THROW((GpuTimer{*device, {.queue = QueueType::SPARSE_BINDING}}), ProfilerError);
    EXPECT_THROW((GpuTimer{*device, {.frames_in_flight = 0}}), ProfilerError);
}

} // namespace
