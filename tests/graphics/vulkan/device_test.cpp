#include "vulkan_test.hpp"

import gfx.tests.vulkan_shaders;

namespace
{

using namespace gfx;
using gfx::tests::VulkanDeviceTest;
using gfx::tests::VulkanInstanceTest;
namespace shaders = gfx::tests::vulkan_shaders;

auto
contains(std::string_view text, std::string_view part) -> bool
{
    return text.find(part) != std::string_view::npos;
}

TEST_F(VulkanDeviceTest, DeviceCreatesTheRequestedQueues)
{
    EXPECT_EQ(m_device->backend(), BackendKind::VULKAN);
    EXPECT_EQ(m_device->debug_name(), "test device");
    EXPECT_EQ(&m_device->physical_device(), m_physical_device);
    for (const QueueType type : {QueueType::GRAPHICS, QueueType::COMPUTE, QueueType::TRANSFER})
    {
        ASSERT_EQ(m_device->queue_count(type), 1U) << to_string(type);
        Queue& queue = m_device->queue(type);
        EXPECT_EQ(queue.type(), type);
        EXPECT_TRUE(queue.handle().is_valid());
        EXPECT_TRUE(has_all(queue.capabilities(), queue_type_capabilities(type))) << to_string(type);
        EXPECT_LT(queue.family_index(), m_physical_device->queue_families().size());
    }
    EXPECT_EQ(m_device->queue_count(QueueType::SPARSE_BINDING), 0U);

    // Dedicated families when the device has them.
    if (m_physical_device->supports(DeviceFeature::ASYNCHRONOUS_COMPUTE_QUEUE))
    {
        EXPECT_NE(m_device->queue(QueueType::COMPUTE).family_index(), m_device->queue(QueueType::GRAPHICS).family_index());
        EXPECT_FALSE(has_any(m_device->queue(QueueType::COMPUTE).capabilities(), QueueCapability::GRAPHICS));
    }
}

TEST_F(VulkanDeviceTest, AskingForAQueueThatWasNotRequestedSaysSo)
{
    try
    {
        (void)m_device->queue(QueueType::COMPUTE, 3);
        FAIL() << "queue must throw";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_TRUE(contains(error.what(), "queue 3 of type COMPUTE was not requested; the device has 1 COMPUTE queue(s)"))
            << error.what();
    }
}

TEST_F(VulkanInstanceTest, MissingRequiredFeaturesAreAllNamed)
{
    // Every feature the device lacks, next to one it has.
    PhysicalDevice& device = physical_device();
    std::vector<DeviceFeature> features;
    for (const auto& entry : EnumerationTraits<DeviceFeature>::NAMES)
    {
        if (!device.supports(entry.value))
        {
            features.push_back(entry.value);
        }
    }
    if (features.empty())
    {
        GTEST_SKIP() << "'" << device.properties().name << "' supports every feature";
    }
    features.push_back(DeviceFeature::TIMELINE_SEMAPHORE);
    try
    {
        (void)device.create_device({.required_features = features});
        FAIL() << "create_device must throw";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_TRUE(contains(error.what(), std::format("does not support {}", features.front()))) << error.what();
        for (const DeviceFeature feature : std::span{features}.first(features.size() - 1))
        {
            EXPECT_TRUE(contains(error.what(), to_string(feature))) << error.what();
        }
        EXPECT_FALSE(contains(error.what(), "TIMELINE_SEMAPHORE")) << error.what();
    }
}

TEST_F(VulkanInstanceTest, MoreQueuesThanAFamilyHasShareNativeQueues)
{
    PhysicalDevice& device = physical_device();
    const std::array queues{QueueRequest{.type = QueueType::GRAPHICS, .count = 40, .priority = 0.5F}};
    std::unique_ptr<Device> logical = device.create_device({.queues = queues});
    ASSERT_EQ(logical->queue_count(QueueType::GRAPHICS), 40U);
    EXPECT_NE(logical->queue(QueueType::GRAPHICS, 0).handle(), logical->queue(QueueType::GRAPHICS, 39).handle());
    logical->queue(QueueType::GRAPHICS, 39).submit({});
    logical->wait_idle();
}

TEST_F(VulkanDeviceTest, BindlessPipelineLayoutHoldsTheTableAndThePushConstants)
{
    const PipelineLayout& layout = m_device->bindless_pipeline_layout();
    EXPECT_TRUE(layout.handle().is_valid());
    const std::array<BindlessResourceKind, 4> kinds{BindlessResourceKind::SAMPLED_IMAGE, BindlessResourceKind::STORAGE_IMAGE,
                                                    BindlessResourceKind::SAMPLER, BindlessResourceKind::STORAGE_BUFFER};
    EXPECT_EQ(layout.definition(), bindless_pipeline_layout_definition({}, kinds));
    const BindingLocation textures = layout.find_binding("gfx_sampled_images");
    EXPECT_EQ(textures.set, BINDLESS_SET);
    EXPECT_EQ(textures.type, DescriptorType::SAMPLED_IMAGE);
    EXPECT_EQ(textures.count, DEFAULT_BINDLESS_SAMPLED_IMAGE_CAPACITY);

    const BindlessTableUsage usage = m_device->bindless_usage();
    EXPECT_EQ(usage.used_by(BindlessResourceKind::STORAGE_BUFFER), 0U);
    EXPECT_EQ(usage.capacity_of(BindlessResourceKind::STORAGE_BUFFER), DEFAULT_BINDLESS_STORAGE_BUFFER_CAPACITY);
}

TEST_F(VulkanDeviceTest, DescriptorSetAndPipelineLayoutsNameTheirBindings)
{
    const std::array bindings{
        DescriptorSetLayoutBinding{.binding = 0, .type = DescriptorType::UNIFORM_BUFFER, .count = 1, .stages = ShaderStage::VERTEX,
                                   .name = "camera"},
        DescriptorSetLayoutBinding{.binding = 1, .type = DescriptorType::COMBINED_IMAGE_SAMPLER, .count = 4,
                                   .stages = ShaderStage::FRAGMENT, .name = "textures"},
    };
    std::unique_ptr<DescriptorSetLayout> set_layout =
        m_device->create_descriptor_set_layout({.bindings = bindings, .debug_name = "material set"});
    EXPECT_EQ(set_layout->bindings().size(), 2U);
    EXPECT_EQ(set_layout->debug_name(), "material set");

    const std::array set_layouts{set_layout->handle(), set_layout->handle()};
    const std::array ranges{PushConstantRange{.stages = ShaderStage::VERTEX | ShaderStage::FRAGMENT, .offset = 0, .size = 64}};
    std::unique_ptr<PipelineLayout> layout =
        m_device->create_pipeline_layout({.set_layouts = set_layouts, .push_constant_ranges = ranges, .debug_name = "material"});
    EXPECT_EQ(layout->definition().set_count(), 2U);
    EXPECT_EQ(layout->find_binding("textures"), (BindingLocation{.set = 0, .binding = 1,
                                                                .type = DescriptorType::COMBINED_IMAGE_SAMPLER, .count = 4}));
    EXPECT_EQ(layout->definition().push_constant_range(ShaderStage::FRAGMENT)->size, 64U);

    // Invalid push-constant ranges and stale handles are reported.
    const std::array too_large{PushConstantRange{.stages = ShaderStage::COMPUTE, .offset = 0, .size = 4096}};
    EXPECT_THROW((void)m_device->create_pipeline_layout({.push_constant_ranges = too_large}), InvalidUsageError);
    const DescriptorSetLayoutHandle stale = set_layout->handle();
    set_layout.reset();
    const std::array stale_layouts{stale};
    EXPECT_THROW((void)m_device->create_pipeline_layout({.set_layouts = stale_layouts}), InvalidHandleError);
}

TEST_F(VulkanDeviceTest, LayoutsFromReflectedDefinitionsDeclareTheirSets)
{
    const PipelineLayoutDefinition definition{
        .bindings = {LayoutBinding{.set = 1, .binding = 2, .type = DescriptorType::STORAGE_BUFFER, .count = 1,
                                   .stages = ShaderStage::COMPUTE, .name = "particles"}},
        .push_constant_ranges = {PushConstantRange{.stages = ShaderStage::COMPUTE, .offset = 0, .size = 16}},
    };
    std::vector<std::unique_ptr<DescriptorSetLayout>> sets;
    std::vector<DescriptorSetLayoutHandle> handles;
    for (std::uint32_t set = 0; set < definition.set_count(); ++set)
    {
        const std::vector<DescriptorSetLayoutBinding> bindings = definition.set_bindings(set);
        sets.push_back(m_device->create_descriptor_set_layout({.bindings = bindings}));
        handles.push_back(sets.back()->handle());
    }
    std::unique_ptr<PipelineLayout> layout =
        m_device->create_pipeline_layout({.set_layouts = handles, .push_constant_ranges = definition.push_constant_ranges});
    EXPECT_EQ(layout->definition(), definition);
}

TEST_F(VulkanDeviceTest, EmptySubmissionsAdvanceTheDeleter)
{
    // A deletion enqueued now waits for what was submitted before; nothing was, so it runs at once.
    int runs = 0;
    m_device->deleter().enqueue([&runs] { ++runs; });
    EXPECT_EQ(m_device->deleter().collect(), 1U);
    EXPECT_EQ(runs, 1);

    // After a submission, it waits for the GPU to complete it.
    Queue& queue = m_device->queue(QueueType::GRAPHICS);
    queue.submit({});
    m_device->deleter().enqueue([&runs] { ++runs; });
    queue.wait_idle();
    m_device->deleter().collect();
    EXPECT_EQ(runs, 2);
    EXPECT_EQ(m_device->deleter().pending_count(), 0U);

    // flush() waits for the whole device.
    m_device->queue(QueueType::COMPUTE).submit({});
    m_device->deleter().enqueue([&runs] { ++runs; });
    m_device->deleter().flush();
    EXPECT_EQ(runs, 3);
}

TEST_F(VulkanDeviceTest, SwapchainsNeedAWindow)
{
    // Presentation itself is tested on real windows (tests/platform_graphics).
    try
    {
        (void)m_device->create_swapchain({});
        FAIL() << "create_swapchain must throw";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_TRUE(contains(error.what(), "names no window")) << error.what();
    }
    EXPECT_THROW((void)m_device->queue(QueueType::GRAPHICS).present({}), InvalidHandleError);
}

TEST_F(VulkanDeviceTest, DevicesOfBothGpusCanCoexist)
{
    // Every physical device at once, each with its own function table.
    std::vector<std::unique_ptr<Device>> devices;
    for (std::size_t index = 0; index < m_instance->physical_device_count(); ++index)
    {
        if (m_instance->physical_device(index).missing_core_features().empty())
        {
            devices.push_back(m_instance->physical_device(index).create_device({}));
        }
    }
    for (const std::unique_ptr<Device>& device : devices)
    {
        device->queue(QueueType::GRAPHICS).submit({});
        device->wait_idle();
    }
}

TEST_F(VulkanDeviceTest, ComputeLimitsAndFeaturesAreReported)
{
    // Vulkan's guaranteed minimums; subgroups of compute shaders have their basic operations (Vulkan 1.1).
    const DeviceLimits& limits = m_physical_device->properties().limits;
    EXPECT_GE(limits.maximum_storage_buffer_range, std::uint64_t{1} << 27U);
    EXPECT_GE(limits.maximum_compute_shared_memory_size, 16384U);
    EXPECT_TRUE(has_all(limits.subgroup_operations, SubgroupOperation::BASIC)) << to_string(limits.subgroup_operations);

    // The memory model costs nothing (enabled when supported); 64-bit atomics only when required.
    EXPECT_EQ(m_device->enabled(DeviceFeature::SHADER_MEMORY_MODEL), m_physical_device->supports(DeviceFeature::SHADER_MEMORY_MODEL));
    EXPECT_FALSE(m_device->enabled(DeviceFeature::SHADER_INTEGER64_ATOMICS));
    EXPECT_TRUE(m_device->enabled(DeviceFeature::TIMELINE_SEMAPHORE)) << "core features are always enabled";
    EXPECT_FALSE(m_device->enabled(DeviceFeature::RAY_TRACING)) << "costly features only when required";
}

/// @brief A device with 64-bit atomics and the memory model.
class VulkanWideAtomicsTest : public VulkanDeviceTest
{
protected:
    [[nodiscard]] auto required_features() const -> std::vector<DeviceFeature> override
    {
        return {DeviceFeature::SHADER_INTEGER64_ATOMICS, DeviceFeature::SHADER_MEMORY_MODEL};
    }
};

/// @brief Push constants of wide_atomics.comp.
struct WideAtomicsPush
{
    std::uint32_t keys = 0;
    std::uint32_t counter = 0;
    std::uint32_t result = 0;
    std::uint32_t count = 0;
};

TEST_F(VulkanWideAtomicsTest, WorkgroupsExchangeSixtyFourBitKeysWithinADispatch)
{
    EXPECT_TRUE(m_device->enabled(DeviceFeature::SHADER_INTEGER64_ATOMICS));
    EXPECT_TRUE(m_device->enabled(DeviceFeature::SHADER_MEMORY_MODEL));
    std::unique_ptr<ShaderModule> module = m_device->create_shader_module(shaders::wide_atomics_comp);
    std::unique_ptr<ComputePipeline> pipeline = m_device->create_compute_pipeline({.shader = module->handle(), .debug_name = "wide atomics"});
    const auto storage = [this](std::string_view name) {
        return m_device->create_buffer({.size = 16, .usage = BufferUsage::STORAGE | BufferUsage::TRANSFER_SOURCE | BufferUsage::TRANSFER_DESTINATION,
                                        .debug_name = name});
    };
    std::unique_ptr<Buffer> keys = storage("keys");
    std::unique_ptr<Buffer> counter = storage("counter");
    std::unique_ptr<Buffer> result = storage("result");

    // Enough workgroups to run on every compute unit at once.
    constexpr std::uint32_t COUNT = 64 * 1000 + 13;
    run<QueueType::COMPUTE>([&](ComputeRecorder& recorder)
    {
        recorder.fill_buffer(keys->handle(), 0, WHOLE_SIZE, 0);
        recorder.fill_buffer(counter->handle(), 0, WHOLE_SIZE, 0);
        const GlobalMemoryBarrier filled{.source_stages = PipelineStage::TRANSFER,
                                         .source_access = MemoryAccess::TRANSFER_WRITE,
                                         .destination_stages = PipelineStage::COMPUTE_SHADER,
                                         .destination_access = MemoryAccess::SHADER_STORAGE_READ | MemoryAccess::SHADER_STORAGE_WRITE};
        recorder.barrier({.memory_barriers = std::span{&filled, 1}});
        recorder.bind_pipeline(pipeline->handle());
        recorder.push_constants(WideAtomicsPush{.keys = keys->bindless_index().value(),
                                                .counter = counter->bindless_index().value(),
                                                .result = result->bindless_index().value(),
                                                .count = COUNT});
        recorder.dispatch((COUNT + 63) / 64);
    });

    // The largest (index · 7 mod COUNT) is COUNT - 1, reached once: at the index whose product is COUNT - 1.
    std::uint64_t expected = 0;
    for (std::uint64_t index = 0; index < COUNT; ++index)
    {
        expected = std::max(expected, (((index * 7) % COUNT) << 32U) | index);
    }
    const std::vector<std::uint32_t> words = read_buffer<std::uint32_t>(*result, 2, ResourceState::COMPUTE_SHADER_STORAGE_WRITE);
    EXPECT_EQ(words[0], static_cast<std::uint32_t>(expected & 0xFFFFFFFFU));
    EXPECT_EQ(words[1], static_cast<std::uint32_t>(expected >> 32U));
    EXPECT_EQ(read_buffer<std::uint32_t>(*counter, 1, ResourceState::COMPUTE_SHADER_STORAGE_WRITE)[0], (COUNT + 63) / 64);
}

} // namespace
