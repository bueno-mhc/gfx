// WindowSwapchain's acquisition semaphores on the graphics tests' fake device (a fake GPU whose submissions
// complete when the test says so) and the platform tests' fake window: a semaphore is given to the presentation
// engine again only once the submission that waited on it has completed, whatever order the images come back
// in (an image can come back the frame after it was presented, before the GPU ran that frame); more semaphores
// are made while the GPU falls behind, and past 64 the device is waited for.
#include <gtest/gtest.h>

import std;
import gfx.platform.graphics;
import gfx.tests.fake_backend;
import gfx.tests.fake_platform;

namespace
{

using gfx::InvalidUsageError;
using gfx::PhysicalDeviceType;
using gfx::SwapchainStatus;
using gfx::platform::SwapchainFrame;
using gfx::platform::WindowSwapchain;
using gfx::tests::FakeDevice;
using gfx::tests::FakePhysicalDevice;
using gfx::tests::FakePlatform;
using gfx::tests::FakeSwapchain;

/// Images in a scripted order, repeated.
auto
images_in_order(std::vector<std::uint32_t> order) -> FakeSwapchain::NextImage
{
    return [order = std::move(order), next = std::size_t{0}]() mutable -> std::expected<std::uint32_t, SwapchainStatus>
    { return order[next++ % order.size()]; };
}

class WindowSwapchainTest : public ::testing::Test
{
protected:
    /// One frame as the examples record it: an image, a submission that waits on its acquisition semaphore
    /// and signals its rendering semaphore, the presentation. false when no image was acquired.
    auto render_frame(WindowSwapchain& presentation) -> bool
    {
        const std::optional<SwapchainFrame> frame = presentation.acquire();
        if (!frame)
        {
            return false;
        }
        // Through the interface, as user code does (default arguments belong to the interface).
        gfx::Queue& queue = m_device.fake_queue();
        const std::array waits{gfx::SemaphoreWait{.semaphore = frame->acquired}};
        const std::array signals{gfx::SemaphoreSignal{.semaphore = frame->rendered}};
        queue.submit(std::array{gfx::SubmitDescription{.waits = waits, .signals = signals}});
        (void)presentation.present(queue, *frame);
        return true;
    }

    /// The fake GPU completes the frames up to @p frame (one submission per frame).
    void complete_frames_through(std::uint64_t frame) noexcept
    {
        m_device.complete_submissions(frame);
    }

    /// The acquisitions whose semaphore a submission made before them, and not completed then, still waited on.
    [[nodiscard]] auto early_acquisitions() -> std::string
    {
        std::string early;
        const std::vector<FakeSwapchain::Acquisition>& acquisitions = m_device.last_swapchain().acquisitions();
        for (std::size_t index = 0; index < acquisitions.size(); ++index)
        {
            // The last submission before this acquisition that waited on its semaphore.
            const FakeSwapchain::Acquisition& acquisition = acquisitions[index];
            std::uint64_t last_wait = 0;
            for (const gfx::tests::FakeQueue::SubmittedWait& wait : m_device.fake_queue().waits())
            {
                if (wait.semaphore == acquisition.signal && wait.submission <= acquisition.submitted)
                {
                    last_wait = std::max(last_wait, wait.submission);
                }
            }
            if (last_wait > acquisition.completed)
            {
                early += std::format("acquisition {} signals {}, which submission {} waits on (the GPU completed {})\n", index + 1,
                                     acquisition.signal, last_wait, acquisition.completed);
            }
        }
        return early;
    }

    FakePhysicalDevice m_physical_device{PhysicalDeviceType::DISCRETE_GPU};
    FakeDevice m_device{m_physical_device};
    FakePlatform m_platform;
    std::unique_ptr<gfx::platform::Window> m_window = m_platform.create_window({.size = {.width = 320, .height = 240, .depth = 1}});
};

TEST_F(WindowSwapchainTest, AnImageComingBackAtOnceDoesNotFreeTheSemaphoreItsFrameWaitsOn)
{
    // The examples' frame loop: frame N acquires its image, then waits for frame N - 2 (FrameTimeline with two
    // frames in flight), so the GPU may still run frames N - 1 and N - 2. The images come back round robin, then
    // the frame after they were presented (as a laptop's discrete GPU did under load: before the fix, the
    // semaphore of the frame before was given to the next acquisition).
    m_device.set_swapchain_images(images_in_order({0, 1, 2, 0, 0, 0, 1, 1, 2, 0, 1, 2, 2, 2, 2, 0, 1, 2}));
    WindowSwapchain presentation{m_device, *m_window};
    for (std::uint64_t frame = 1; frame <= 90; ++frame)
    {
        complete_frames_through(frame > 3 ? frame - 3 : 0);
        ASSERT_TRUE(render_frame(presentation));
    }
    EXPECT_EQ(early_acquisitions(), "");

    // Four acquisition semaphores (three images plus one) and three rendering semaphores were enough.
    EXPECT_EQ(m_device.semaphores_created(), 7U);
    EXPECT_EQ(m_device.fake_queue().presented().size(), 90U);
}

TEST_F(WindowSwapchainTest, SemaphoresAreAddedWhileTheGpuFallsBehind)
{
    // The GPU completes nothing for 12 frames: each acquisition needs a semaphore no frame waits on.
    WindowSwapchain presentation{m_device, *m_window};
    for (int frame = 0; frame < 12; ++frame)
    {
        ASSERT_TRUE(render_frame(presentation));
    }
    EXPECT_EQ(m_device.semaphores_created(), 12U + 3U);

    // Once it caught up, the semaphores are reused: no more are made.
    for (int frame = 0; frame < 30; ++frame)
    {
        complete_frames_through(m_device.submitted_count());
        ASSERT_TRUE(render_frame(presentation));
    }
    EXPECT_EQ(m_device.semaphores_created(), 12U + 3U);
    EXPECT_EQ(early_acquisitions(), "");
    EXPECT_EQ(m_device.fake_deleter().flushes(), 0U);
}

TEST_F(WindowSwapchainTest, PastSixtyFourSemaphoresTheDeviceIsWaitedFor)
{
    // 64 frames pending: the next acquisition waits for the device (a deleter flush) instead of making more.
    WindowSwapchain presentation{m_device, *m_window};
    for (int frame = 0; frame < 70; ++frame)
    {
        ASSERT_TRUE(render_frame(presentation));
    }
    EXPECT_EQ(m_device.semaphores_created(), 64U + 3U);
    EXPECT_EQ(m_device.fake_deleter().flushes(), 1U);
    EXPECT_EQ(early_acquisitions(), "");
}

TEST_F(WindowSwapchainTest, AFailedAcquisitionKeepsItsSemaphore)
{
    // The second acquisition times out: its semaphore, never signaled, serves the next one.
    m_device.set_swapchain_images([next = 0]() mutable -> std::expected<std::uint32_t, SwapchainStatus>
                                  {
                                      const int call = next++;
                                      if (call == 1)
                                      {
                                          return std::unexpected(SwapchainStatus::TIMEOUT);
                                      }
                                      return static_cast<std::uint32_t>(call % 3);
                                  });
    WindowSwapchain presentation{m_device, *m_window};
    ASSERT_TRUE(render_frame(presentation));
    EXPECT_FALSE(render_frame(presentation));
    ASSERT_TRUE(render_frame(presentation));
    const std::vector<FakeSwapchain::Acquisition>& acquisitions = m_device.last_swapchain().acquisitions();
    ASSERT_EQ(acquisitions.size(), 3U);
    EXPECT_EQ(acquisitions[1].signal, acquisitions[2].signal);
    EXPECT_NE(acquisitions[0].signal, acquisitions[2].signal);
    EXPECT_EQ(m_device.semaphores_created(), 7U);
}

TEST_F(WindowSwapchainTest, RecreationLeavesPendingSemaphoresToTheDeleter)
{
    // The swapchain is recreated while frames are pending; afterwards no acquisition reuses a semaphore early, and
    // the ones pending before come back (no more semaphores are made).
    m_device.set_swapchain_images(images_in_order({0, 0, 1, 2, 2, 1}));
    WindowSwapchain presentation{m_device, *m_window};
    for (std::uint64_t frame = 1; frame <= 40; ++frame)
    {
        if (frame == 20)
        {
            presentation.request_recreation();
        }
        complete_frames_through(frame > 3 ? frame - 3 : 0);
        ASSERT_TRUE(render_frame(presentation));
    }
    EXPECT_EQ(presentation.recreation_count(), 1U);
    EXPECT_EQ(m_device.last_swapchain().resizes(), 1U);
    EXPECT_EQ(early_acquisitions(), "");
    EXPECT_EQ(m_device.semaphores_created(), 7U);
}

TEST_F(WindowSwapchainTest, FramesNeverPresentedAreNamed)
{
    // Frames acquired and never presented keep their semaphores: once 64 are held, acquisitions are refused.
    WindowSwapchain presentation{m_device, *m_window};
    for (int frame = 0; frame < 64; ++frame)
    {
        ASSERT_TRUE(presentation.acquire().has_value());
    }
    try
    {
        (void)presentation.acquire();
        FAIL() << "a 65th acquisition without presentation was accepted";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("never presented"), std::string_view::npos) << error.what();
        EXPECT_NE(std::string_view{error.what()}.find("WindowSwapchain::present()"), std::string_view::npos) << error.what();
    }
}

} // namespace
