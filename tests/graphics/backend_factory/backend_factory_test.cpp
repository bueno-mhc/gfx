#include <gtest/gtest.h>

import std;
import gfx;

namespace
{

using namespace gfx;

auto
contains(std::string_view text, std::string_view part) -> bool
{
    return text.find(part) != std::string_view::npos;
}

TEST(BackendFactoryTest, NullBackendIsAlwaysAvailableAndHasNoPhysicalDevice)
{
    if (!backend_availability(BackendKind::NULL_BACKEND).built)
    {
        GTEST_SKIP() << "built with GFX_BACKEND_NULL=OFF";
    }
    EXPECT_TRUE(backend_availability(BackendKind::NULL_BACKEND).available);
    EXPECT_EQ(available_backends().back(), BackendKind::NULL_BACKEND);

    // An instance without physical devices: selecting one says why.
    const std::unique_ptr<Instance> instance = create_instance(BackendKind::NULL_BACKEND, {.validation_level = ValidationLevel::NONE});
    EXPECT_EQ(instance->backend(), BackendKind::NULL_BACKEND);
    EXPECT_EQ(instance->validation_level(), ValidationLevel::NONE);
    EXPECT_EQ(instance->physical_device_count(), 0U);
    EXPECT_THROW((void)instance->physical_device(0), InvalidUsageError);
    try
    {
        (void)instance->select_physical_device();
        FAIL() << "select_physical_device must throw";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_TRUE(contains(error.what(), "no physical device supports the NULL_BACKEND backend")) << error.what();
    }
}

TEST(BackendFactoryTest, UnimplementedBackendSaysItIsNotImplemented)
{
    if (!backend_availability(BackendKind::DIRECT3D12).built)
    {
        GTEST_SKIP() << "built with GFX_BACKEND_DIRECT3D12=OFF";
    }
    const BackendAvailability availability = backend_availability(BackendKind::DIRECT3D12);
    EXPECT_FALSE(availability.available);
    EXPECT_EQ(availability.reason, "the Direct3D 12 backend is not implemented yet");
    try
    {
        (void)create_instance(BackendKind::DIRECT3D12);
        FAIL() << "create_instance must throw";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_EQ(error.backend(), BackendKind::DIRECT3D12);
        EXPECT_TRUE(contains(error.what(), "Direct3D 12 backend is not implemented yet")) << error.what();
        EXPECT_TRUE(contains(error.what(), "gfx::available_backends()")) << error.what();
    }
}

TEST(BackendFactoryTest, BackendThatWasNotBuiltNamesItsCMakeOption)
{
    if (backend_availability(BackendKind::METAL).built)
    {
        GTEST_SKIP() << "built with GFX_BACKEND_METAL=ON";
    }
    const BackendAvailability availability = backend_availability(BackendKind::METAL);
    EXPECT_FALSE(availability.available);
    EXPECT_TRUE(contains(availability.reason, "-DGFX_BACKEND_METAL=ON")) << availability.reason;
    try
    {
        (void)create_instance(BackendKind::METAL);
        FAIL() << "create_instance must throw";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_TRUE(contains(error.what(), "rebuild gfx with -DGFX_BACKEND_METAL=ON")) << error.what();
    }
}

TEST(BackendFactoryTest, AvailableBackendsAreAvailable)
{
    for (const BackendKind backend : available_backends())
    {
        const BackendAvailability availability = backend_availability(backend);
        EXPECT_TRUE(availability.built) << to_string(backend);
        EXPECT_TRUE(availability.available) << to_string(backend);
        EXPECT_TRUE(availability.reason.empty()) << to_string(backend);
    }
}

TEST(BackendFactoryTest, VulkanInstanceEnumeratesPhysicalDevices)
{
    if (!backend_availability(BackendKind::VULKAN).available)
    {
        GTEST_SKIP() << backend_availability(BackendKind::VULKAN).reason;
    }
    const std::unique_ptr<Instance> instance = create_instance(BackendKind::VULKAN, {.application_name = "backend factory test"});
    EXPECT_EQ(instance->backend(), BackendKind::VULKAN);
    for (std::size_t index = 0; index < instance->physical_device_count(); ++index)
    {
        EXPECT_FALSE(instance->physical_device(index).properties().name.empty());
    }
    EXPECT_THROW((void)instance->physical_device(instance->physical_device_count()), InvalidUsageError);
}

} // namespace
