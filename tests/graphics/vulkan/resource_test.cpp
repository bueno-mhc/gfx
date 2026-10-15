#include "vulkan_test.hpp"

namespace
{

using namespace gfx;
using gfx::tests::VulkanDeviceTest;

auto
contains(std::string_view text, std::string_view part) -> bool
{
    return text.find(part) != std::string_view::npos;
}

auto
used(const Device& device, BindlessResourceKind kind) -> std::uint32_t
{
    return device.bindless_usage().used_by(kind);
}

TEST_F(VulkanDeviceTest, HostVisibleBuffersArePersistentlyMapped)
{
    std::unique_ptr<Buffer> upload = m_device->create_buffer(
        {.size = 256, .usage = BufferUsage::TRANSFER_SOURCE, .location = MemoryLocation::HOST_UPLOAD, .debug_name = "upload"});
    EXPECT_EQ(upload->size(), 256U);
    EXPECT_EQ(upload->usage(), BufferUsage::TRANSFER_SOURCE);
    EXPECT_EQ(upload->location(), MemoryLocation::HOST_UPLOAD);
    EXPECT_EQ(upload->debug_name(), "upload");
    const std::span<std::uint32_t> words = upload->mapped_elements<std::uint32_t>();
    ASSERT_EQ(words.size(), 64U);
    std::ranges::iota(words, 0U);
    upload->flush_mapped_range();
    EXPECT_EQ(upload->mapped_elements<std::uint32_t>()[63], 63U);
    EXPECT_EQ(upload->mapped_memory().data(), upload->mapped_memory().data()) << "the mapping never moves";

    std::unique_ptr<Buffer> readback = m_device->create_buffer(
        {.size = 64, .usage = BufferUsage::TRANSFER_DESTINATION, .location = MemoryLocation::HOST_READBACK});
    readback->invalidate_mapped_range(16, 16);
    EXPECT_THROW(readback->invalidate_mapped_range(60, 16), InvalidUsageError);
}

TEST_F(VulkanDeviceTest, DeviceLocalBuffersAreNotMapped)
{
    std::unique_ptr<Buffer> buffer = m_device->create_buffer({.size = 1024, .usage = BufferUsage::VERTEX, .debug_name = "vertices"});
    try
    {
        (void)buffer->mapped_memory();
        FAIL() << "mapped_memory must throw";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_TRUE(contains(error.what(), "buffer 'vertices' lives in DEVICE_LOCAL memory")) << error.what();
    }
    EXPECT_THROW((void)buffer->device_address(), InvalidUsageError);
}

TEST_F(VulkanDeviceTest, StorageBuffersGetBindlessIndicesAndAddresses)
{
    const std::uint32_t before = used(*m_device, BindlessResourceKind::STORAGE_BUFFER);
    std::unique_ptr<Buffer> first = m_device->create_buffer({.size = 64, .usage = BufferUsage::STORAGE | BufferUsage::SHADER_DEVICE_ADDRESS});
    std::unique_ptr<Buffer> second = m_device->create_buffer({.size = 64, .usage = BufferUsage::STORAGE});
    std::unique_ptr<Buffer> unregistered = m_device->create_buffer({.size = 64, .usage = BufferUsage::STORAGE, .bindless = false});
    EXPECT_TRUE(first->bindless_index().is_valid());
    EXPECT_TRUE(second->bindless_index().is_valid());
    EXPECT_NE(first->bindless_index(), second->bindless_index());
    EXPECT_FALSE(unregistered->bindless_index().is_valid());
    EXPECT_EQ(used(*m_device, BindlessResourceKind::STORAGE_BUFFER), before + 2);
    EXPECT_NE(first->device_address(), 0U);

    // Destroyed buffers return their index once the GPU is done (here: at once, nothing was submitted).
    first.reset();
    m_device->deleter().collect();
    EXPECT_EQ(used(*m_device, BindlessResourceKind::STORAGE_BUFFER), before + 1);
}

TEST_F(VulkanDeviceTest, InvalidBufferDescriptionsAreRejected)
{
    EXPECT_THROW((void)m_device->create_buffer({.size = 0, .usage = BufferUsage::STORAGE}), InvalidUsageError);
    EXPECT_THROW((void)m_device->create_buffer({.size = 16}), InvalidUsageError);
    if (!m_device->physical_device().supports(DeviceFeature::RAY_TRACING))
    {
        EXPECT_THROW((void)m_device->create_buffer({.size = 16, .usage = BufferUsage::SHADER_BINDING_TABLE}), BackendCapabilityError);
    }
}

TEST_F(VulkanDeviceTest, BuffersPlacedInOneMemoryAliasEachOther)
{
    // Two buffers over the same host-visible bytes: what one writes, the other reads.
    const BufferDescription description{.size = 4096, .usage = BufferUsage::TRANSFER_SOURCE | BufferUsage::TRANSFER_DESTINATION,
                                        .location = MemoryLocation::HOST_UPLOAD};
    const MemoryRequirements requirements = m_device->memory_requirements(description);
    EXPECT_GE(requirements.size, 4096U);
    EXPECT_NE(requirements.memory_type_bits, 0U);
    std::unique_ptr<DeviceMemory> memory = m_device->allocate_memory({.size = requirements.size,
                                                                      .location = MemoryLocation::HOST_UPLOAD,
                                                                      .memory_type_bits = requirements.memory_type_bits,
                                                                      .debug_name = "aliased"});
    EXPECT_EQ(memory->size(), requirements.size);
    EXPECT_EQ(memory->location(), MemoryLocation::HOST_UPLOAD);
    BufferDescription placed = description;
    placed.placement = MemoryPlacement{.memory = memory->handle(), .offset = 0};
    std::unique_ptr<Buffer> writer = m_device->create_buffer(placed);
    std::unique_ptr<Buffer> reader = m_device->create_buffer(placed);
    writer->mapped_elements<std::uint32_t>()[10] = 0xCAFEU;
    EXPECT_EQ(reader->mapped_elements<std::uint32_t>()[10], 0xCAFEU);

    // A placement that does not fit is refused.
    BufferDescription outside = description;
    outside.placement = MemoryPlacement{.memory = memory->handle(), .offset = requirements.size};
    EXPECT_THROW((void)m_device->create_buffer(outside), InvalidUsageError);
}

TEST_F(VulkanDeviceTest, ImagesAndViewsResolveTheirRanges)
{
    std::unique_ptr<Image> image = m_device->create_image({
        .format = Format::R8G8B8A8_UNORM,
        .extent = {.width = 64, .height = 32},
        .mip_level_count = 7,
        .array_layer_count = 2,
        .usage = ImageUsage::SAMPLED | ImageUsage::STORAGE | ImageUsage::TRANSFER_DESTINATION,
        .debug_name = "texture",
    });
    EXPECT_EQ(image->format(), Format::R8G8B8A8_UNORM);
    EXPECT_EQ(image->extent(), (Extent{.width = 64, .height = 32, .depth = 1}));
    EXPECT_EQ(image->mip_level_count(), 7U);
    EXPECT_EQ(image->array_layer_count(), 2U);

    std::unique_ptr<ImageView> view = m_device->create_image_view(
        {.image = image->handle(), .type = ImageViewType::TWO_DIMENSIONAL_ARRAY, .range = {.base_mip_level = 2}});
    EXPECT_EQ(view->image(), image->handle());
    EXPECT_EQ(view->format(), Format::R8G8B8A8_UNORM);
    EXPECT_EQ(view->range(), (ImageSubresourceRange{.aspects = ImageAspect::COLOR, .base_mip_level = 2, .mip_level_count = 5,
                                                    .base_array_layer = 0, .array_layer_count = 2}));
    EXPECT_TRUE(view->sampled_index().is_valid());
    EXPECT_TRUE(view->storage_index().is_valid());

    // Views beyond the image are refused.
    EXPECT_THROW((void)m_device->create_image_view({.image = image->handle(), .range = {.base_mip_level = 7}}), InvalidUsageError);
}

TEST_F(VulkanDeviceTest, DepthViewsUseTheDepthAspect)
{
    std::unique_ptr<Image> depth = m_device->create_image({
        .format = Format::D32_SFLOAT,
        .extent = {.width = 16, .height = 16},
        .usage = ImageUsage::DEPTH_STENCIL_ATTACHMENT | ImageUsage::SAMPLED,
    });
    std::unique_ptr<ImageView> view = m_device->create_image_view({.image = depth->handle()});
    EXPECT_EQ(view->range().aspects, ImageAspect::DEPTH);
    EXPECT_TRUE(view->sampled_index().is_valid());
    EXPECT_FALSE(view->storage_index().is_valid());
}

TEST_F(VulkanDeviceTest, UnsupportedImagesAreRejectedWithTheReason)
{
    // Compressed formats cannot be rendered to; sizes must fit the dimension; mip chains have a length.
    try
    {
        (void)m_device->create_image(
            {.format = Format::BC1_RGBA_UNORM_BLOCK, .extent = {.width = 16, .height = 16}, .usage = ImageUsage::COLOR_ATTACHMENT});
        FAIL() << "create_image must throw";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_TRUE(contains(error.what(), "PhysicalDevice::format_support()")) << error.what();
    }
    EXPECT_THROW((void)m_device->create_image({.format = Format::R8_UNORM, .extent = {.width = 16, .height = 16, .depth = 2},
                                               .usage = ImageUsage::SAMPLED}),
                 InvalidUsageError);
    EXPECT_THROW((void)m_device->create_image({.format = Format::R8_UNORM, .extent = {.width = 16, .height = 16},
                                               .mip_level_count = 6, .usage = ImageUsage::SAMPLED}),
                 InvalidUsageError);
}

TEST_F(VulkanDeviceTest, SamplersAreRegisteredInTheBindlessTable)
{
    const std::uint32_t before = used(*m_device, BindlessResourceKind::SAMPLER);
    std::unique_ptr<Sampler> linear = m_device->create_sampler({.debug_name = "linear"});
    std::unique_ptr<Sampler> shadow = m_device->create_sampler({.compare_operation = CompareOperation::LESS, .debug_name = "shadow"});
    EXPECT_TRUE(linear->bindless_index().is_valid());
    EXPECT_NE(linear->bindless_index(), shadow->bindless_index());
    EXPECT_EQ(used(*m_device, BindlessResourceKind::SAMPLER), before + 2);
    if (m_device->physical_device().supports(DeviceFeature::SAMPLER_ANISOTROPY))
    {
        std::unique_ptr<Sampler> anisotropic = m_device->create_sampler({.maximum_anisotropy = 8.0F});
        EXPECT_THROW((void)m_device->create_sampler({.maximum_anisotropy = 1000.0F}), InvalidUsageError);
    }
    if (m_device->physical_device().supports(DeviceFeature::SAMPLER_FILTER_MINMAX))
    {
        std::unique_ptr<Sampler> minimum = m_device->create_sampler({.reduction_mode = SamplerReductionMode::MINIMUM});
    }
}

TEST_F(VulkanDeviceTest, BufferViewsNeedTexelUsage)
{
    std::unique_ptr<Buffer> texels = m_device->create_buffer({.size = 256, .usage = BufferUsage::UNIFORM_TEXEL | BufferUsage::STORAGE_TEXEL});
    std::unique_ptr<BufferView> view = m_device->create_buffer_view({.buffer = texels->handle(), .format = Format::R32_UINT});
    EXPECT_EQ(view->buffer(), texels->handle());
    EXPECT_EQ(view->format(), Format::R32_UINT);
    std::unique_ptr<Buffer> plain = m_device->create_buffer({.size = 256, .usage = BufferUsage::STORAGE});
    EXPECT_THROW((void)m_device->create_buffer_view({.buffer = plain->handle(), .format = Format::R32_UINT}), InvalidUsageError);
}

TEST_F(VulkanDeviceTest, MemoryBudgetAndStatisticsDescribeTheHeaps)
{
    const std::vector<MemoryHeapBudget> heaps = m_device->memory_budget();
    ASSERT_FALSE(heaps.empty());
    EXPECT_TRUE(std::ranges::any_of(heaps, [](const MemoryHeapBudget& heap) { return heap.properties == MemoryProperty::DEVICE_LOCAL; }));
    for (const MemoryHeapBudget& heap : heaps)
    {
        EXPECT_GT(heap.size, 0U);
        EXPECT_GT(heap.budget, 0U);
    }

    const MemoryStatistics before = m_device->memory_statistics();
    std::unique_ptr<Buffer> buffer = m_device->create_buffer({.size = 1 << 20, .usage = BufferUsage::STORAGE});
    const MemoryStatistics after = m_device->memory_statistics();
    EXPECT_EQ(after.allocation_count, before.allocation_count + 1);
    EXPECT_GE(after.allocation_bytes, before.allocation_bytes + (1 << 20));
    EXPECT_GE(after.block_bytes, after.allocation_bytes);
}

/// @brief A device with sparse binding and a sparse-binding queue.
class VulkanSparseTest : public VulkanDeviceTest
{
protected:
    [[nodiscard]] auto required_features() const -> std::vector<DeviceFeature> override
    {
        return {DeviceFeature::SPARSE_BINDING};
    }

    [[nodiscard]] auto device_description() const -> DeviceDescription override
    {
        static constexpr std::array<QueueRequest, 2> QUEUES{
            QueueRequest{.type = QueueType::GRAPHICS, .count = 1, .priority = 1.0F},
            QueueRequest{.type = QueueType::SPARSE_BINDING, .count = 1, .priority = 1.0F},
        };
        return {.queues = QUEUES};
    }
};

TEST_F(VulkanSparseTest, SparseImagesAreBoundTileByTile)
{
    std::unique_ptr<Image> image = m_device->create_image({
        .format = Format::R8G8B8A8_UNORM,
        .extent = {.width = 1024, .height = 1024},
        .mip_level_count = 11,
        .usage = ImageUsage::SAMPLED | ImageUsage::TRANSFER_DESTINATION,
        .options = ImageCreationOption::SPARSE_RESIDENCY,
    });
    const SparseImageRequirements requirements = m_device->sparse_image_requirements(image->handle());
    EXPECT_EQ(requirements.aspects, ImageAspect::COLOR);
    EXPECT_GT(requirements.tile_extent.width, 0U);
    EXPECT_GT(requirements.tile_size, 0U);
    EXPECT_GT(requirements.mip_tail_size, 0U);

    // One tile of level 0, and the mip tail.
    std::unique_ptr<DeviceMemory> memory = m_device->allocate_memory({
        .size = requirements.tile_size + requirements.mip_tail_size,
        .memory_type_bits = m_device->memory_requirements(ImageDescription{.format = Format::R8G8B8A8_UNORM,
                                                                           .extent = {.width = 1024, .height = 1024},
                                                                           .mip_level_count = 11,
                                                                           .usage = ImageUsage::SAMPLED | ImageUsage::TRANSFER_DESTINATION,
                                                                           .options = ImageCreationOption::SPARSE_RESIDENCY})
                                .memory_type_bits,
    });
    std::unique_ptr<TimelineSemaphore> bound = m_device->create_timeline_semaphore({});
    const std::array tiles{SparseImageBind{.image = image->handle(), .extent = requirements.tile_extent, .memory = memory->handle()}};
    const std::array tail{SparseImageOpaqueBind{.image = image->handle(), .resource_offset = requirements.mip_tail_offset,
                                                .size = requirements.mip_tail_size, .memory = memory->handle(),
                                                .memory_offset = requirements.tile_size}};
    const std::array signal{TimelineSemaphoreSignal{.semaphore = bound->handle(), .value = 1}};
    m_device->queue(QueueType::SPARSE_BINDING).bind_sparse({.image_opaque_binds = tail, .image_binds = tiles, .timeline_signals = signal});
    EXPECT_EQ(bound->wait(1), WaitStatus::SIGNALED);

    // A sparse buffer, bound and unbound.
    std::unique_ptr<Buffer> buffer = m_device->create_buffer({.size = 1 << 20, .usage = BufferUsage::STORAGE, .sparse = true});
    const MemoryRequirements buffer_requirements =
        m_device->memory_requirements(BufferDescription{.size = 1 << 20, .usage = BufferUsage::STORAGE, .sparse = true});
    std::unique_ptr<DeviceMemory> pages = m_device->allocate_memory(
        {.size = buffer_requirements.alignment, .memory_type_bits = buffer_requirements.memory_type_bits});
    const std::array page{SparseBufferBind{.buffer = buffer->handle(), .size = buffer_requirements.alignment, .memory = pages->handle()}};
    const std::array signal2{TimelineSemaphoreSignal{.semaphore = bound->handle(), .value = 2}};
    m_device->queue(QueueType::SPARSE_BINDING).bind_sparse({.buffer_binds = page, .timeline_signals = signal2});
    EXPECT_EQ(bound->wait(2), WaitStatus::SIGNALED);
    m_device->wait_idle();
}

TEST_F(VulkanDeviceTest, SparseBindingNeedsASparseQueue)
{
    EXPECT_THROW(m_device->queue(QueueType::GRAPHICS).bind_sparse({}), BackendCapabilityError);
}

} // namespace
