#include "vulkan_test.hpp"

namespace
{

using namespace gfx;
using gfx::tests::VulkanInstanceTest;

auto
contains(std::string_view text, std::string_view part) -> bool
{
    return text.find(part) != std::string_view::npos;
}

TEST_F(VulkanInstanceTest, InstanceEnumeratesPhysicalDevicesWithTheirProperties)
{
    EXPECT_EQ(m_instance->backend(), BackendKind::VULKAN);
    EXPECT_EQ(m_instance->validation_level(), ValidationLevel::COMMANDS);
    ASSERT_GT(m_instance->physical_device_count(), 0U);
    for (std::size_t index = 0; index < m_instance->physical_device_count(); ++index)
    {
        const PhysicalDevice& device = m_instance->physical_device(index);
        const PhysicalDeviceProperties& properties = device.properties();
        EXPECT_EQ(device.backend(), BackendKind::VULKAN);
        EXPECT_EQ(device.validation_level(), ValidationLevel::COMMANDS);
        EXPECT_FALSE(properties.name.empty());
        EXPECT_EQ(device.debug_name(), properties.name);
        EXPECT_NE(properties.vendor_identifier, 0U);
        EXPECT_GE(properties.api_version, (Version{.major = 1, .minor = 3, .patch = 0})) << properties.name;
    }
}

TEST_F(VulkanInstanceTest, SelectedDeviceHasTheCoreFeaturesAndSaneLimits)
{
    const PhysicalDevice& device = physical_device();
    EXPECT_TRUE(device.missing_core_features().empty());
    const DeviceLimits& limits = device.properties().limits;
    EXPECT_GE(limits.maximum_image_dimension, 4096U);
    EXPECT_GE(limits.maximum_push_constant_size, 128U);
    EXPECT_GE(limits.maximum_bound_descriptor_sets, 4U);
    EXPECT_GE(limits.maximum_color_attachments, 4U);
    EXPECT_GE(limits.maximum_compute_workgroup_invocations, 128U);
    EXPECT_GT(limits.timestamp_period_nanoseconds, 0.0);
    EXPECT_TRUE(has_any(limits.color_sample_counts, SampleCount::FOUR));
    EXPECT_GT(limits.maximum_memory_allocation_size, 0U);
    EXPECT_GE(limits.maximum_subgroup_size, limits.minimum_subgroup_size);
    EXPECT_GE(limits.maximum_multiview_view_count, 6U);
    for (const std::uint32_t maximum : limits.maximum_bindless_resources)
    {
        (void)maximum;
    }
    EXPECT_GE(limits.maximum_bindless_resources[static_cast<std::size_t>(std::to_underlying(BindlessResourceKind::SAMPLED_IMAGE))],
              DEFAULT_BINDLESS_SAMPLED_IMAGE_CAPACITY);
    EXPECT_GE(limits.maximum_bindless_resources[static_cast<std::size_t>(std::to_underlying(BindlessResourceKind::SAMPLER))],
              DEFAULT_BINDLESS_SAMPLER_CAPACITY);
}

TEST_F(VulkanInstanceTest, QueueFamiliesIncludeAGraphicsFamilyThatAlsoComputesAndCopies)
{
    const PhysicalDevice& device = physical_device();
    ASSERT_FALSE(device.queue_families().empty());
    const bool universal = std::ranges::any_of(device.queue_families(), [](const QueueFamilyProperties& family)
    {
        return has_all(family.capabilities, QueueCapability::GRAPHICS | QueueCapability::COMPUTE | QueueCapability::TRANSFER) &&
               family.queue_count > 0 && family.timestamp_valid_bits > 0;
    });
    EXPECT_TRUE(universal);

    // Dedicated families are reported as features.
    const bool asynchronous_compute = std::ranges::any_of(device.queue_families(), [](const QueueFamilyProperties& family)
    {
        return has_any(family.capabilities, QueueCapability::COMPUTE) && !has_any(family.capabilities, QueueCapability::GRAPHICS);
    });
    EXPECT_EQ(device.supports(DeviceFeature::ASYNCHRONOUS_COMPUTE_QUEUE), asynchronous_compute);
}

TEST_F(VulkanInstanceTest, FormatSupportReportsWhatFormatsCanBeUsedFor)
{
    const PhysicalDevice& device = physical_device();
    const FormatSupport color = device.format_support(Format::R8G8B8A8_UNORM);
    EXPECT_TRUE(has_all(color.optimal_tiling_usage, ImageUsage::SAMPLED | ImageUsage::COLOR_ATTACHMENT |
                                                        ImageUsage::TRANSFER_SOURCE | ImageUsage::TRANSFER_DESTINATION));
    EXPECT_TRUE(has_all(color.buffer_usage, BufferUsage::UNIFORM_TEXEL | BufferUsage::VERTEX));
    const FormatSupport depth = device.format_support(Format::D32_SFLOAT);
    EXPECT_TRUE(has_all(depth.optimal_tiling_usage, ImageUsage::DEPTH_STENCIL_ATTACHMENT | ImageUsage::SAMPLED));
    EXPECT_FALSE(has_any(depth.optimal_tiling_usage, ImageUsage::COLOR_ATTACHMENT));
    const FormatSupport storage = device.format_support(Format::R32_UINT);
    EXPECT_TRUE(has_any(storage.optimal_tiling_usage, ImageUsage::STORAGE));
    EXPECT_TRUE(has_any(storage.buffer_usage, BufferUsage::STORAGE_TEXEL));
    EXPECT_EQ(device.format_support(Format::UNDEFINED).optimal_tiling_usage, ImageUsage::NONE);
}

TEST_F(VulkanInstanceTest, SurfacesNeedAWindowOfAWindowSystemVulkanPresentsTo)
{
    // Window surfaces are supported; the surfaces themselves are tested on real windows (tests/platform_graphics).
    const PhysicalDevice& device = physical_device();
    EXPECT_TRUE(device.supports(DeviceFeature::PRESENTATION));
    EXPECT_THROW((void)device.surface_support({}), InvalidUsageError);
    try
    {
        (void)device.surface_support(NativeWindowHandle{WebCanvasHandle{.selector = {'#', 'c'}}});
        FAIL() << "surface_support must throw";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_TRUE(contains(error.what(), "Vulkan has no surface for WEB_CANVAS windows")) << error.what();
    }
}

TEST_F(VulkanInstanceTest, ValidationLayersAreEnabledWhenInstalled)
{
    if (!gfx::tests::validation_requested())
    {
        GTEST_SKIP() << "GFX_TEST_VALIDATION=0";
    }
    EXPECT_TRUE(m_validation_enabled) << "the validation layers were not found (VK_ADD_LAYER_PATH)";
}

} // namespace
