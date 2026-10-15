#include "vulkan_test.hpp"

namespace
{

using namespace gfx;
using gfx::tests::VulkanDeviceTest;
using gfx::tests::VulkanInstanceTest;

auto
contains(std::string_view text, std::string_view part) -> bool
{
    return text.find(part) != std::string_view::npos;
}

/// @brief Whether this process was started by renderdoccmd for the capture test (see CMakeLists.txt).
auto
renderdoc_expected() -> bool
{
    const char* value = std::getenv("GFX_TEST_EXPECT_RENDERDOC");
    return value != nullptr && std::string_view{value} == "1";
}

TEST_F(VulkanDeviceTest, CapturesNeedACaptureTool)
{
    if (renderdoc_expected())
    {
        GTEST_SKIP() << "running under RenderDoc";
    }
    EXPECT_EQ(m_device->capture_tool(), CaptureTool::NONE);
    try
    {
        m_device->begin_capture("frame");
        FAIL() << "begin_capture must throw";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_TRUE(contains(error.what(), "no capture tool is attached")) << error.what();
    }
    EXPECT_THROW(m_device->end_capture(), InvalidUsageError);
}

TEST_F(VulkanDeviceTest, RenderDocCapturesTheWorkBetweenBeginAndEnd)
{
    if (!renderdoc_expected())
    {
        GTEST_SKIP() << "runs under renderdoccmd (CTest test VulkanCapture.RenderDoc)";
    }
    ASSERT_EQ(m_device->capture_tool(), CaptureTool::RENDERDOC);
    std::unique_ptr<Buffer> buffer = m_device->create_buffer({.size = 256, .usage = BufferUsage::TRANSFER_DESTINATION});
    m_device->begin_capture("gfx capture test");
    EXPECT_THROW(m_device->begin_capture("twice"), InvalidUsageError);
    run([&](GraphicsRecorder& recorder)
    {
        recorder.begin_debug_label("captured work");
        recorder.fill_buffer(buffer->handle(), 0, WHOLE_SIZE, 42);
        recorder.end_debug_label();
    });
    m_device->end_capture();

    // RenderDoc wrote the capture where renderdoccmd told it to.
    const char* directory = std::getenv("GFX_TEST_CAPTURE_DIRECTORY");
    ASSERT_NE(directory, nullptr);
    bool written = false;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{directory})
    {
        written = written || entry.path().extension() == ".rdc";
    }
    EXPECT_TRUE(written) << "no .rdc file in " << directory;
}

/// @brief A device that collects GPU breadcrumbs (and the driver's fault reports when it can).
class VulkanBreadcrumbTest : public VulkanDeviceTest
{
protected:
    [[nodiscard]] auto required_features() const -> std::vector<DeviceFeature> override
    {
        return {DeviceFeature::GPU_CRASH_MARKERS};
    }

    [[nodiscard]] auto device_description() const -> DeviceDescription override
    {
        DeviceDescription description = VulkanDeviceTest::device_description();
        description.crash_diagnostics = CrashDiagnostic::BREADCRUMBS;
        return description;
    }
};

TEST_F(VulkanBreadcrumbTest, LabelsAreRecordedAsBreadcrumbs)
{
    // Nested labels, in primary command buffers and in passes: valid checkpoints, normal results.
    std::unique_ptr<Buffer> buffer = m_device->create_buffer({.size = 64, .usage = BufferUsage::TRANSFER_DESTINATION | BufferUsage::TRANSFER_SOURCE});
    for (int frame = 0; frame < 3; ++frame)
    {
        run([&](GraphicsRecorder& recorder)
        {
            recorder.begin_debug_label("frame");
            recorder.begin_debug_label("upload");
            recorder.fill_buffer(buffer->handle(), 0, WHOLE_SIZE, static_cast<std::uint32_t>(frame));
            recorder.end_debug_label();
            recorder.begin_debug_label("left open: closed when the recording ends");
        });
    }
    EXPECT_EQ(read_buffer<std::uint32_t>(*buffer, 1)[0], 2U);
}

TEST_F(VulkanInstanceTest, BreadcrumbsNeedGpuCrashMarkers)
{
    PhysicalDevice& device = physical_device();
    if (device.supports(DeviceFeature::GPU_CRASH_MARKERS))
    {
        std::unique_ptr<Device> logical = device.create_device({.crash_diagnostics = CrashDiagnostic::BREADCRUMBS});
        EXPECT_EQ(logical->backend(), BackendKind::VULKAN);
        return;
    }
    try
    {
        (void)device.create_device({.crash_diagnostics = CrashDiagnostic::BREADCRUMBS});
        FAIL() << "create_device must throw";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_TRUE(contains(error.what(), "GPU_CRASH_MARKERS")) << error.what();
    }
}

TEST_F(VulkanInstanceTest, VendorReportsNeedTheDriversFaultReports)
{
    // Supported or refused with the reason: never silently ignored.
    PhysicalDevice& device = physical_device();
    try
    {
        std::unique_ptr<Device> logical = device.create_device({.crash_diagnostics = CrashDiagnostic::VENDOR_REPORT});
        EXPECT_EQ(logical->backend(), BackendKind::VULKAN);
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_TRUE(contains(error.what(), "VK_EXT_device_fault")) << error.what();
    }
}

} // namespace
