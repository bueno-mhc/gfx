// Recording and submitting commands allocates nothing once the reused storage has grown: this executable counts every
// operator new while frames of barriers, attachment clears, resolves, image and buffer copies, blits, debug labels,
// acceleration structure builds and property queries are recorded on a real GPU (GFX_TEST_PHYSICAL_DEVICE selects it),
// and while whole frames are recorded, submitted to two queues with timeline waits and signals and a fence, waited for and
// their deletions collected, after warm-up frames that record and submit the largest work.
#include "vulkan_test.hpp"

namespace
{

std::atomic<bool> g_counting{false};
std::atomic<std::uint64_t> g_allocations{0};

} // namespace

// Global allocation functions of this executable: counted while g_counting is set.
auto
operator new(std::size_t size) -> void*
{
    if (g_counting.load(std::memory_order_relaxed))
    {
        g_allocations.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size); memory != nullptr)
    {
        return memory;
    }
    throw std::bad_alloc{};
}

auto
operator new[](std::size_t size) -> void*
{
    return operator new(size);
}

// Never inlined: an optimized caller would otherwise see free() release memory from operator new
// (-Wmismatched-new-delete in Release builds).
[[gnu::noinline]] void
operator delete(void* memory) noexcept
{
    std::free(memory);
}

[[gnu::noinline]] void
operator delete[](void* memory) noexcept
{
    std::free(memory);
}

[[gnu::noinline]] void
operator delete(void* memory,
                std::size_t) noexcept
{
    std::free(memory);
}

[[gnu::noinline]] void
operator delete[](void* memory,
                  std::size_t) noexcept
{
    std::free(memory);
}

namespace
{

using namespace gfx;
using gfx::tests::rgba;
using gfx::tests::VulkanDeviceTest;

/// @brief Images are IMAGE_SIZE texels square: 2 x 2 quadrants of QUADRANT_SIZE texels, one per region of a command.
constexpr std::uint32_t IMAGE_SIZE = 32;
constexpr std::uint32_t QUADRANT_SIZE = 16;
constexpr Extent QUADRANT_EXTENT{.width = QUADRANT_SIZE, .height = QUADRANT_SIZE};
constexpr std::uint64_t QUADRANT_BYTES = std::uint64_t{QUADRANT_SIZE} * QUADRANT_SIZE * 4;
constexpr std::uint32_t MAXIMUM_REGIONS = 4;

/// @brief Frames recorded before allocations are counted: together they record the largest commands of a test.
constexpr std::uint32_t WARM_UP_FRAMES = MAXIMUM_REGIONS;

/// @brief Frames whose recording is counted.
constexpr std::uint32_t COUNTED_FRAMES = 60;

/// @brief Clear color of the rendering pass, and the color clear_attachments() writes over the left half of each quadrant.
constexpr std::array<float, 4> PASS_COLOR{0.0F, 0.0F, 0.0F, 1.0F};
constexpr std::array<float, 4> CLEAR_COLOR{1.0F, 0.0F, 1.0F, 1.0F};
constexpr std::uint32_t PASS_TEXEL = rgba(0, 0, 0, 255);
constexpr std::uint32_t CLEAR_TEXEL = rgba(255, 0, 255, 255);

/// @brief Waits for acceleration structure builds before later builds and property queries.
constexpr GlobalMemoryBarrier AFTER_BUILD{
    .source_stages = PipelineStage::ACCELERATION_STRUCTURE_BUILD,
    .source_access = MemoryAccess::ACCELERATION_STRUCTURE_WRITE,
    .destination_stages = PipelineStage::ACCELERATION_STRUCTURE_BUILD,
    .destination_access = MemoryAccess::ACCELERATION_STRUCTURE_READ | MemoryAccess::ACCELERATION_STRUCTURE_WRITE,
};

/// @brief Frames the CPU records ahead of the GPU in the submission test.
constexpr std::uint32_t FRAMES_IN_FLIGHT = 2;

/// @brief Deletions enqueued before the submission test's first frame: more than the deleter first holds, so that its
///        storage grows before the counted frames.
constexpr std::uint32_t EARLY_DELETIONS = 100;

/// @brief Orders a frame's fills after those of earlier frames (they write the same words).
constexpr GlobalMemoryBarrier BETWEEN_FILLS{
    .source_stages = PipelineStage::TRANSFER,
    .source_access = MemoryAccess::TRANSFER_WRITE,
    .destination_stages = PipelineStage::TRANSFER,
    .destination_access = MemoryAccess::TRANSFER_WRITE,
};

/// @brief Regions (rectangles, builds) of the commands of @p frame: 1 to MAXIMUM_REGIONS in turn, so that counted frames
///        record commands smaller than, and as large as, the largest of the warm-up frames.
constexpr auto
regions_of(std::uint32_t frame) noexcept -> std::uint32_t
{
    return frame % MAXIMUM_REGIONS + 1;
}

/// @brief The first texel of quadrant @p quadrant (left to right, then top to bottom).
constexpr auto
quadrant_offset(std::uint32_t quadrant) noexcept -> Offset
{
    return {.x = static_cast<std::int32_t>(quadrant % 2 * QUADRANT_SIZE), .y = static_cast<std::int32_t>(quadrant / 2 * QUADRANT_SIZE)};
}

/// @brief The word the source buffer holds in every texel of quadrant @p quadrant.
constexpr auto
pattern_of(std::uint32_t quadrant) noexcept -> std::uint32_t
{
    return rgba(32 * (quadrant + 1), 64, 128, 255);
}

/// @brief A graphics command buffer recorded frame after frame, each frame submitted and waited for; recordings are
///        counted after the warm-up frames.
class VulkanCommandAllocationTest : public VulkanDeviceTest
{
protected:
    /// @brief Records WARM_UP_FRAMES + COUNTED_FRAMES frames with @p record_frame(recorder, frame) and returns the
    ///        allocations of the counted recordings (submissions are not counted).
    template<class Recording>
    auto record_frames(Recording&& record_frame) -> std::uint64_t
    {
        std::unique_ptr<CommandPool> pool =
            m_device->create_command_pool({.queue_type = QueueType::GRAPHICS, .debug_name = "allocation test pool"});
        std::unique_ptr<CommandBuffer> command_buffer = pool->allocate(CommandBufferLevel::PRIMARY, "allocation test commands");
        g_allocations = 0;
        for (std::uint32_t frame = 0; frame < WARM_UP_FRAMES + COUNTED_FRAMES && !HasFatalFailure(); ++frame)
        {
            // The command buffer is recorded again once the GPU has run the previous frame.
            pool->reset();
            g_counting = frame >= WARM_UP_FRAMES;
            {
                GraphicsRecorder recorder{*command_buffer};
                record_frame(recorder, frame);
            }
            g_counting = false;
            submit_and_wait(m_device->queue(QueueType::GRAPHICS), command_buffer->handle());
        }
        return g_allocations.load();
    }

    /// @brief A RGBA8 image of IMAGE_SIZE texels square.
    [[nodiscard]] auto create_image(SampleCount samples,
                                    ImageUsage usage,
                                    std::string_view name) -> std::unique_ptr<Image>
    {
        return m_device->create_image({.format = Format::R8G8B8A8_UNORM,
                                       .extent = {.width = IMAGE_SIZE, .height = IMAGE_SIZE},
                                       .samples = samples,
                                       .usage = usage,
                                       .debug_name = name});
    }
};

/// @brief Bottom-level structures rebuilt every frame, then a top level: devices that build acceleration structures.
class VulkanAccelerationStructureAllocationTest : public VulkanCommandAllocationTest
{
protected:
    [[nodiscard]] auto required_features() const -> std::vector<DeviceFeature> override
    {
        return {DeviceFeature::RAY_QUERY};
    }
};

/// @brief Frames submitted to the compute and graphics queues: recorded, submitted with timeline waits and signals and a
///        fence, waited for, deletions enqueued and collected; frames are counted whole after the warm-up frames.
class VulkanSubmissionAllocationTest : public VulkanDeviceTest
{
};

TEST_F(VulkanCommandAllocationTest, TransfersAndAttachmentClearsAllocateNothingAfterTheFirstFrames)
{
    // A multisampled target cleared in rectangles and resolved; the resolved texels copied, mirrored by a blit and read
    // back; a source buffer uploaded into an image and copied into a buffer.
    constexpr ImageUsage TRANSFERS = ImageUsage::TRANSFER_SOURCE | ImageUsage::TRANSFER_DESTINATION;
    std::unique_ptr<Image> multisampled =
        create_image(SampleCount::FOUR, ImageUsage::COLOR_ATTACHMENT | ImageUsage::TRANSFER_SOURCE, "multisampled");
    std::unique_ptr<ImageView> multisampled_view =
        m_device->create_image_view({.image = multisampled->handle(), .debug_name = "multisampled view"});
    std::unique_ptr<Image> resolved = create_image(SampleCount::ONE, TRANSFERS, "resolved");
    std::unique_ptr<Image> copied = create_image(SampleCount::ONE, TRANSFERS, "copied");
    std::unique_ptr<Image> blitted = create_image(SampleCount::ONE, TRANSFERS, "blitted");
    std::unique_ptr<Image> uploaded = create_image(SampleCount::ONE, TRANSFERS, "uploaded");
    std::unique_ptr<Buffer> source = m_device->create_buffer({.size = MAXIMUM_REGIONS * QUADRANT_BYTES,
                                                              .usage = BufferUsage::TRANSFER_SOURCE,
                                                              .location = MemoryLocation::HOST_UPLOAD,
                                                              .debug_name = "source"});
    const std::span<std::uint32_t> source_words = source->mapped_elements<std::uint32_t>();
    for (std::uint32_t quadrant = 0; quadrant < MAXIMUM_REGIONS; ++quadrant)
    {
        std::ranges::fill(source_words.subspan(quadrant * QUADRANT_BYTES / 4, QUADRANT_BYTES / 4), pattern_of(quadrant));
    }
    std::unique_ptr<Buffer> destination = m_device->create_buffer({.size = 2 * MAXIMUM_REGIONS * QUADRANT_BYTES,
                                                                   .usage = BufferUsage::TRANSFER_SOURCE | BufferUsage::TRANSFER_DESTINATION,
                                                                   .debug_name = "destination"});

    // One region per quadrant in every command (a frame uses the first regions_of(frame)): the left half of each
    // quadrant cleared, blits mirroring each quadrant left to right, buffer quadrants one after the other.
    std::array<ClearRectangle, MAXIMUM_REGIONS> rectangles{};
    std::array<ImageResolveRegion, MAXIMUM_REGIONS> resolves{};
    std::array<ImageCopyRegion, MAXIMUM_REGIONS> image_copies{};
    std::array<ImageBlitRegion, MAXIMUM_REGIONS> blits{};
    std::array<BufferImageCopyRegion, MAXIMUM_REGIONS> buffer_image_copies{};
    std::array<BufferCopyRegion, MAXIMUM_REGIONS> buffer_copies{};
    for (std::uint32_t quadrant = 0; quadrant < MAXIMUM_REGIONS; ++quadrant)
    {
        const Offset first = quadrant_offset(quadrant);
        const Offset last{.x = first.x + static_cast<std::int32_t>(QUADRANT_SIZE), .y = first.y + static_cast<std::int32_t>(QUADRANT_SIZE), .z = 1};
        rectangles[quadrant] = {.area = {.x = first.x, .y = first.y, .width = QUADRANT_SIZE / 2, .height = QUADRANT_SIZE}};
        resolves[quadrant] = {.source_offset = first, .destination_offset = first, .extent = QUADRANT_EXTENT};
        image_copies[quadrant] = {.source_offset = first, .destination_offset = first, .extent = QUADRANT_EXTENT};
        blits[quadrant] = {.source_bounds = {first, last},
                           .destination_bounds = {Offset{.x = last.x, .y = first.y}, Offset{.x = first.x, .y = last.y, .z = 1}}};
        buffer_image_copies[quadrant] = {.buffer_offset = quadrant * QUADRANT_BYTES, .image_offset = first, .image_extent = QUADRANT_EXTENT};
        buffer_copies[quadrant] = {.source_offset = quadrant * QUADRANT_BYTES,
                                   .destination_offset = (MAXIMUM_REGIONS + quadrant) * QUADRANT_BYTES,
                                   .size = QUADRANT_BYTES};
    }
    const std::array attachments{ColorAttachment{.view = multisampled_view->handle(), .clear_color = PASS_COLOR}};
    const std::array clears{ClearAttachment{.color = CLEAR_COLOR}};

    const std::uint64_t allocations = record_frames([&](GraphicsRecorder& recorder, std::uint32_t frame)
    {
        const std::uint32_t regions = regions_of(frame);
        recorder.begin_debug_label("Copies, blits, resolves and attachment clears");

        // Every image starts the frame discarded: the target is rendered, the others are written by transfers.
        const std::array start{
            image_barrier(multisampled->handle(), ResourceState::UNDEFINED, ResourceState::COLOR_ATTACHMENT_WRITE),
            image_barrier(resolved->handle(), ResourceState::UNDEFINED, ResourceState::TRANSFER_DESTINATION),
            image_barrier(copied->handle(), ResourceState::UNDEFINED, ResourceState::TRANSFER_DESTINATION),
            image_barrier(blitted->handle(), ResourceState::UNDEFINED, ResourceState::TRANSFER_DESTINATION),
            image_barrier(uploaded->handle(), ResourceState::UNDEFINED, ResourceState::TRANSFER_DESTINATION),
        };
        recorder.barrier({.image_barriers = start});

        // Rectangles cleared inside the pass (as squares drawn without a pipeline), resolved to single-sampled texels.
        {
            RenderingScope scope =
                recorder.begin_rendering({.area = {.width = IMAGE_SIZE, .height = IMAGE_SIZE}, .color_attachments = attachments});
            scope.clear_attachments(clears, std::span{rectangles}.first(regions));
        }
        const std::array rendered{image_barrier(multisampled->handle(), ResourceState::COLOR_ATTACHMENT_WRITE, ResourceState::TRANSFER_SOURCE)};
        recorder.barrier({.image_barriers = rendered});
        recorder.resolve_image(multisampled->handle(), ImageLayout::TRANSFER_SOURCE, resolved->handle(), ImageLayout::TRANSFER_DESTINATION,
                               std::span{resolves}.first(regions));
        const std::array resolved_texels{image_barrier(resolved->handle(), ResourceState::TRANSFER_DESTINATION, ResourceState::TRANSFER_SOURCE)};
        recorder.barrier({.image_barriers = resolved_texels});

        // The resolved quadrants copied, mirrored and read back; the source buffer uploaded and copied.
        recorder.copy_image(resolved->handle(), ImageLayout::TRANSFER_SOURCE, copied->handle(), ImageLayout::TRANSFER_DESTINATION,
                            std::span{image_copies}.first(regions));
        recorder.blit_image(resolved->handle(), ImageLayout::TRANSFER_SOURCE, blitted->handle(), ImageLayout::TRANSFER_DESTINATION,
                            std::span{blits}.first(regions), Filter::NEAREST);
        recorder.copy_image_to_buffer(resolved->handle(), ImageLayout::TRANSFER_SOURCE, destination->handle(),
                                      std::span{buffer_image_copies}.first(regions));
        recorder.copy_buffer_to_image(source->handle(), uploaded->handle(), ImageLayout::TRANSFER_DESTINATION,
                                      std::span{buffer_image_copies}.first(regions));
        recorder.copy_buffer(source->handle(), destination->handle(), std::span{buffer_copies}.first(regions));
        recorder.end_debug_label();
    });
    EXPECT_EQ(allocations, 0U) << "recording allocated after " << WARM_UP_FRAMES << " warm-up frames";

    // The last frame's quadrants: cleared on the left (on the right once mirrored), uploaded and copied from the source.
    const std::uint32_t regions = regions_of(WARM_UP_FRAMES + COUNTED_FRAMES - 1);
    const std::vector<std::uint32_t> resolved_texels = read_image(*resolved, ResourceState::TRANSFER_SOURCE);
    const std::vector<std::uint32_t> copied_texels = read_image(*copied, ResourceState::TRANSFER_DESTINATION);
    const std::vector<std::uint32_t> blitted_texels = read_image(*blitted, ResourceState::TRANSFER_DESTINATION);
    const std::vector<std::uint32_t> uploaded_texels = read_image(*uploaded, ResourceState::TRANSFER_DESTINATION);
    const std::vector<std::uint32_t> destination_words =
        read_buffer<std::uint32_t>(*destination, 2 * MAXIMUM_REGIONS * QUADRANT_BYTES / 4, ResourceState::TRANSFER_DESTINATION);
    for (std::uint32_t quadrant = 0; quadrant < regions; ++quadrant)
    {
        const Offset first = quadrant_offset(quadrant);
        const auto texel = [&](const std::vector<std::uint32_t>& texels, std::int32_t x, std::int32_t y)
        {
            return texels[static_cast<std::size_t>(first.y + y) * IMAGE_SIZE + static_cast<std::size_t>(first.x + x)];
        };
        const std::size_t read_back = quadrant * QUADRANT_BYTES / 4 + QUADRANT_SIZE * (QUADRANT_SIZE / 2);
        EXPECT_EQ(texel(resolved_texels, 4, 8), CLEAR_TEXEL) << "quadrant " << quadrant;
        EXPECT_EQ(texel(resolved_texels, 12, 8), PASS_TEXEL) << "quadrant " << quadrant;
        EXPECT_EQ(texel(copied_texels, 4, 8), CLEAR_TEXEL) << "quadrant " << quadrant;
        EXPECT_EQ(texel(copied_texels, 12, 8), PASS_TEXEL) << "quadrant " << quadrant;
        EXPECT_EQ(texel(blitted_texels, 4, 8), PASS_TEXEL) << "quadrant " << quadrant;
        EXPECT_EQ(texel(blitted_texels, 12, 8), CLEAR_TEXEL) << "quadrant " << quadrant;
        EXPECT_EQ(texel(uploaded_texels, 8, 8), pattern_of(quadrant)) << "quadrant " << quadrant;
        EXPECT_EQ(destination_words[read_back + 4], CLEAR_TEXEL) << "quadrant " << quadrant;
        EXPECT_EQ(destination_words[read_back + 12], PASS_TEXEL) << "quadrant " << quadrant;
        EXPECT_EQ(destination_words[(MAXIMUM_REGIONS + quadrant) * QUADRANT_BYTES / 4], pattern_of(quadrant)) << "quadrant " << quadrant;
    }
}

TEST_F(VulkanAccelerationStructureAllocationTest, BuildsAndPropertyQueriesAllocateNothingAfterTheFirstFrames)
{
    // One triangle, read by every geometry of every build.
    const std::array<float, 9> vertices{-0.5F, -0.5F, 0.0F, 0.5F, -0.5F, 0.0F, 0.0F, 0.5F, 0.0F};
    std::unique_ptr<Buffer> vertex_buffer = m_device->create_buffer({.size = sizeof(vertices),
                                                                     .usage = BufferUsage::ACCELERATION_STRUCTURE_BUILD_INPUT |
                                                                              BufferUsage::SHADER_DEVICE_ADDRESS,
                                                                     .location = MemoryLocation::HOST_UPLOAD,
                                                                     .debug_name = "triangle"});
    std::memcpy(vertex_buffer->mapped_memory().data(), vertices.data(), sizeof(vertices));
    std::array<TriangleGeometry, MAXIMUM_REGIONS> triangles{};
    triangles.fill({.vertex_buffer = vertex_buffer->handle(), .vertex_count = 3, .options = GeometryOption::OPAQUE_GEOMETRY});

    // Bottom levels large enough for builds of 1 to MAXIMUM_REGIONS geometries, each with its own scratch memory (builds
    // recorded together run in parallel).
    constexpr AccelerationStructureBuildOption OPTIONS =
        AccelerationStructureBuildOption::PREFER_FAST_TRACE | AccelerationStructureBuildOption::ALLOW_COMPACTION;
    AccelerationStructureBuildSizes largest{};
    for (std::uint32_t count = 1; count <= MAXIMUM_REGIONS; ++count)
    {
        const AccelerationStructureBuildSizes sizes =
            m_device->acceleration_structure_build_sizes({.options = OPTIONS, .triangles = std::span{triangles}.first(count)});
        largest.structure_size = std::max(largest.structure_size, sizes.structure_size);
        largest.build_scratch_size = std::max(largest.build_scratch_size, sizes.build_scratch_size);
    }
    std::vector<std::unique_ptr<Buffer>> buffers;
    std::vector<std::unique_ptr<AccelerationStructure>> bottom_levels;
    std::array<AccelerationStructureBuildDescription, MAXIMUM_REGIONS> builds{};
    std::array<AccelerationStructureHandle, MAXIMUM_REGIONS> structures{};
    for (std::uint32_t index = 0; index < MAXIMUM_REGIONS; ++index)
    {
        const Buffer& storage = *buffers.emplace_back(
            m_device->create_buffer({.size = largest.structure_size, .usage = BufferUsage::ACCELERATION_STRUCTURE_STORAGE}));
        const Buffer& scratch = *buffers.emplace_back(m_device->create_buffer(
            {.size = largest.build_scratch_size, .usage = BufferUsage::STORAGE | BufferUsage::SHADER_DEVICE_ADDRESS, .bindless = false}));
        const AccelerationStructure& bottom = *bottom_levels.emplace_back(m_device->create_acceleration_structure(
            {.type = AccelerationStructureType::BOTTOM_LEVEL, .buffer = storage.handle(), .size = largest.structure_size}));
        builds[index] = {.options = OPTIONS, .destination = bottom.handle(), .scratch_buffer = scratch.handle()};
        structures[index] = bottom.handle();
    }

    // A top level holding one instance of the first bottom level.
    const AccelerationStructureInstance instance = make_instance(bottom_levels[0]->device_address(),
                                                                 {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F});
    std::unique_ptr<Buffer> instances = m_device->create_buffer({.size = sizeof(instance),
                                                                 .usage = BufferUsage::ACCELERATION_STRUCTURE_BUILD_INPUT |
                                                                          BufferUsage::SHADER_DEVICE_ADDRESS,
                                                                 .location = MemoryLocation::HOST_UPLOAD,
                                                                 .debug_name = "instances"});
    std::memcpy(instances->mapped_memory().data(), &instance, sizeof(instance));
    AccelerationStructureBuildDescription top_build{.type = AccelerationStructureType::TOP_LEVEL,
                                                    .instances = {.buffer = instances->handle(), .count = 1}};
    const AccelerationStructureBuildSizes top_sizes = m_device->acceleration_structure_build_sizes(top_build);
    std::unique_ptr<Buffer> top_storage =
        m_device->create_buffer({.size = top_sizes.structure_size, .usage = BufferUsage::ACCELERATION_STRUCTURE_STORAGE});
    std::unique_ptr<Buffer> top_scratch = m_device->create_buffer(
        {.size = top_sizes.build_scratch_size, .usage = BufferUsage::STORAGE | BufferUsage::SHADER_DEVICE_ADDRESS, .bindless = false});
    std::unique_ptr<AccelerationStructure> top_level = m_device->create_acceleration_structure(
        {.type = AccelerationStructureType::TOP_LEVEL, .buffer = top_storage->handle(), .size = top_sizes.structure_size});
    top_build.destination = top_level->handle();
    top_build.scratch_buffer = top_scratch->handle();
    std::unique_ptr<QueryPool> compacted_sizes =
        m_device->create_query_pool({.type = QueryType::ACCELERATION_STRUCTURE_COMPACTED_SIZE, .count = MAXIMUM_REGIONS});

    std::uint64_t sized = 0;
    const std::uint64_t allocations = record_frames([&](GraphicsRecorder& recorder, std::uint32_t frame)
    {
        // Builds of 1 to MAXIMUM_REGIONS geometries, sized every frame as geometry that changes would be.
        const std::uint32_t count = regions_of(frame);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            builds[index].triangles = std::span{triangles}.first((index + frame) % MAXIMUM_REGIONS + 1);
            sized = std::max(sized, m_device->acceleration_structure_build_sizes(builds[index]).structure_size);
        }
        recorder.build_acceleration_structures(std::span{builds}.first(count));
        const std::array after_build{AFTER_BUILD};
        recorder.barrier({.memory_barriers = after_build});

        // The top level over the first bottom level, and the compacted sizes of the bottom levels.
        recorder.build_acceleration_structures(std::span{&top_build, 1});
        recorder.reset_query_pool(compacted_sizes->handle(), 0, MAXIMUM_REGIONS);
        recorder.write_acceleration_structure_properties(std::span{structures}.first(count), compacted_sizes->handle(), 0);
    });
    EXPECT_EQ(allocations, 0U) << "recording allocated after " << WARM_UP_FRAMES << " warm-up frames";
    EXPECT_GT(sized, 0U);
    EXPECT_LE(sized, largest.structure_size);

    // The last frame's bottom levels were built: each has a compacted size.
    const std::uint32_t count = regions_of(WARM_UP_FRAMES + COUNTED_FRAMES - 1);
    std::array<std::uint64_t, MAXIMUM_REGIONS> compacted{};
    ASSERT_TRUE(compacted_sizes->results(0, std::span{compacted}.first(count), true));
    for (std::uint32_t index = 0; index < count; ++index)
    {
        EXPECT_GT(compacted[index], 0U) << "bottom level " << index;
        EXPECT_LE(compacted[index], largest.structure_size) << "bottom level " << index;
    }
    EXPECT_NE(top_level->device_address(), 0U);
}

TEST_F(VulkanSubmissionAllocationTest, SubmissionsWaitsAndDeletionsAllocateNothingAfterTheFirstFrames)
{
    // Frames paced by a timeline, recorded into per-frame pools of both queues (on a GPU with one queue, such as an
    // integrated one, both queue types share its hardware queue).
    Queue& graphics = m_device->queue(QueueType::GRAPHICS);
    Queue& compute = m_device->queue(QueueType::COMPUTE);
    FrameTimeline timeline{*m_device, FRAMES_IN_FLIGHT, "allocation test frames"};
    FrameCommandPools graphics_pools{*m_device, {.queue_type = QueueType::GRAPHICS, .debug_name = "allocation test graphics"},
                                     timeline.timeline(), FRAMES_IN_FLIGHT};
    FrameCommandPools compute_pools{*m_device, {.queue_type = QueueType::COMPUTE, .debug_name = "allocation test compute"},
                                    timeline.timeline(), FRAMES_IN_FLIGHT};

    // Timeline semaphores from the compute work to the graphics work, from batch to batch of a submission, and from the
    // CPU to the GPU; a fence on the compute work.
    std::unique_ptr<TimelineSemaphore> handoff = m_device->create_timeline_semaphore({.debug_name = "handoff"});
    std::unique_ptr<TimelineSemaphore> chain = m_device->create_timeline_semaphore({.debug_name = "chain"});
    std::unique_ptr<TimelineSemaphore> host = m_device->create_timeline_semaphore({.debug_name = "host"});
    std::unique_ptr<Fence> compute_done = m_device->create_fence({.debug_name = "compute done"});

    // Each queue writes the frame's number into words of its own buffer: the compute work one, each graphics batch its own.
    std::unique_ptr<Buffer> compute_words =
        m_device->create_buffer({.size = 4, .usage = BufferUsage::TRANSFER_DESTINATION, .debug_name = "compute words"});
    std::unique_ptr<Buffer> graphics_words = m_device->create_buffer({.size = MAXIMUM_REGIONS * 4,
                                                                      .usage = BufferUsage::TRANSFER_SOURCE | BufferUsage::TRANSFER_DESTINATION,
                                                                      .debug_name = "graphics words"});

    // Deletions record the order they run in (into storage reserved here), early ones first.
    std::vector<std::uint32_t> deleted;
    deleted.reserve(EARLY_DELETIONS + MAXIMUM_REGIONS * (WARM_UP_FRAMES + COUNTED_FRAMES));
    std::uint32_t enqueued = 0;
    const auto enqueue_deletions = [&](std::uint32_t count)
    {
        for (std::uint32_t deletion = 0; deletion < count; ++deletion)
        {
            m_device->deleter().enqueue([&deleted, sequence = enqueued++] { deleted.push_back(sequence); });
        }
    };
    enqueue_deletions(EARLY_DELETIONS);

    g_allocations = 0;
    std::uint64_t chain_value = 0;
    for (std::uint32_t frame_index = 0; frame_index < WARM_UP_FRAMES + COUNTED_FRAMES && !HasFatalFailure(); ++frame_index)
    {
        // The frame: the pools of the frame that last used them reset, the deletions the GPU is done with run.
        g_counting = frame_index >= WARM_UP_FRAMES;
        const std::uint64_t frame = timeline.begin_frame();
        graphics_pools.begin_frame(frame);
        compute_pools.begin_frame(frame);

        // The compute work: one fill, signaling the handoff, with the fence.
        CommandBuffer& compute_commands = compute_pools.command_buffer();
        {
            ComputeRecorder recorder{compute_commands};
            recorder.fill_buffer(compute_words->handle(), 0, 4, static_cast<std::uint32_t>(frame));
        }
        const std::array compute_buffers{compute_commands.handle()};
        const std::array compute_signals{TimelineSemaphoreSignal{.semaphore = handoff->handle(), .value = frame}};
        const std::array compute_batches{SubmitDescription{.command_buffers = compute_buffers, .timeline_signals = compute_signals}};
        compute.submit(compute_batches, compute_done->handle());

        // The graphics work: 1 to MAXIMUM_REGIONS batches in turn, each waiting for the one before through the chain. The
        // first waits for the compute work and for a value the CPU signals only after the submission (a wait nothing has
        // promised: the hardware queue may stall on it); the last signals the frame.
        const std::uint32_t batch_count = regions_of(frame_index);
        const std::array first_waits{TimelineSemaphoreWait{.semaphore = handoff->handle(), .value = frame},
                                     TimelineSemaphoreWait{.semaphore = host->handle(), .value = frame}};
        const std::array frame_signals{timeline.frame_signal()};
        std::array<CommandBufferHandle, MAXIMUM_REGIONS> graphics_buffers{};
        std::array<TimelineSemaphoreWait, MAXIMUM_REGIONS> chain_waits{};
        std::array<TimelineSemaphoreSignal, MAXIMUM_REGIONS> chain_signals{};
        std::array<SubmitDescription, MAXIMUM_REGIONS> graphics_batches{};
        for (std::uint32_t batch = 0; batch < batch_count; ++batch)
        {
            CommandBuffer& commands = graphics_pools.command_buffer();
            {
                GraphicsRecorder recorder{commands};
                const std::array before_fill{BETWEEN_FILLS};
                recorder.barrier({.memory_barriers = before_fill});
                recorder.fill_buffer(graphics_words->handle(), std::uint64_t{batch} * 4, 4, static_cast<std::uint32_t>(frame));
            }
            graphics_buffers[batch] = commands.handle();
            chain_waits[batch] = {.semaphore = chain->handle(), .value = chain_value}; // Used from the second batch on.
            const bool last = batch + 1 == batch_count;
            if (!last)
            {
                chain_signals[batch] = {.semaphore = chain->handle(), .value = ++chain_value};
            }
            graphics_batches[batch] = {
                .command_buffers = std::span{graphics_buffers}.subspan(batch, 1),
                .timeline_waits = batch == 0 ? std::span<const TimelineSemaphoreWait>{first_waits} : std::span{chain_waits}.subspan(batch, 1),
                .timeline_signals = last ? std::span<const TimelineSemaphoreSignal>{frame_signals} : std::span{chain_signals}.subspan(batch, 1),
            };
        }
        graphics.submit(std::span{graphics_batches}.first(batch_count));

        // Deletions (as many as batches), the CPU's value, the compute work waited for through its fence.
        enqueue_deletions(batch_count);
        host->signal(frame);
        const WaitStatus compute_status = compute_done->wait(std::chrono::seconds{10});
        compute_done->reset();
        g_counting = false;
        ASSERT_EQ(compute_status, WaitStatus::SIGNALED) << "frame " << frame;
    }
    EXPECT_EQ(g_allocations.load(), 0U) << "frames allocated after " << WARM_UP_FRAMES << " warm-up frames";

    // The GPU reached every value; the last frame's batches wrote its number.
    timeline.wait_idle();
    const std::uint64_t last_frame = timeline.current_frame();
    EXPECT_EQ(handoff->value(), last_frame);
    EXPECT_EQ(host->value(), last_frame);
    EXPECT_EQ(chain->value(), chain_value);
    const std::vector<std::uint32_t> words = read_buffer<std::uint32_t>(*graphics_words, MAXIMUM_REGIONS);
    for (std::uint32_t batch = 0; batch < regions_of(WARM_UP_FRAMES + COUNTED_FRAMES - 1); ++batch)
    {
        EXPECT_EQ(words[batch], last_frame) << "batch " << batch;
    }

    // Every deletion ran once, in the order of its enqueue.
    m_device->deleter().flush();
    EXPECT_EQ(m_device->deleter().pending_count(), 0U);
    ASSERT_EQ(deleted.size(), enqueued);
    for (std::uint32_t sequence = 0; sequence < enqueued; ++sequence)
    {
        ASSERT_EQ(deleted[sequence], sequence) << "deletion " << sequence << " of " << enqueued;
    }
}

} // namespace
