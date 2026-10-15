#include "vulkan_test.hpp"

import gfx.tests.vulkan_shaders;

namespace
{

using namespace gfx;
using gfx::tests::rgba;
using gfx::tests::VulkanDeviceTest;
namespace shaders = gfx::tests::vulkan_shaders;

constexpr Extent TARGET_EXTENT{.width = 32, .height = 32, .depth = 1};

/// @brief Push constants of fill.comp.
struct FillPush
{
    std::uint32_t target = 0;
    std::uint32_t count = 0;
    std::uint32_t value = 0;
};

/// @brief Push constants of triangle.vert: the color of the triangle.
struct Tint
{
    std::array<float, 4> color{};
};

class VulkanCommandTest : public VulkanDeviceTest
{
protected:
    /// @brief A color target and its view, in TARGET_EXTENT.
    void create_target(Format format = Format::R8G8B8A8_UNORM)
    {
        m_target = m_device->create_image({.format = format,
                                           .extent = TARGET_EXTENT,
                                           .usage = ImageUsage::COLOR_ATTACHMENT | ImageUsage::TRANSFER_SOURCE,
                                           .debug_name = "target"});
        m_target_view = m_device->create_image_view({.image = m_target->handle(), .debug_name = "target view"});
    }

    /// @brief The triangle pipeline (vertex pulling, push-constant tint) for the target's format.
    [[nodiscard]] auto triangle_pipeline(std::span<const Format> formats) -> std::unique_ptr<GraphicsPipeline>
    {
        m_vertex = m_device->create_shader_module(shaders::triangle_vert);
        m_fragment = m_device->create_shader_module(shaders::triangle_frag);
        return m_device->create_graphics_pipeline({.vertex_shader = m_vertex->handle(),
                                                   .fragment_shader = m_fragment->handle(),
                                                   .color_formats = formats,
                                                   .state = {.rasterization = {.cull_mode = CullMode::NONE}}});
    }

    /// @brief A pass over the whole target.
    [[nodiscard]] auto pass(std::span<const ColorAttachment> attachments) const -> RenderingDescription
    {
        return {.area = {.width = TARGET_EXTENT.width, .height = TARGET_EXTENT.height}, .color_attachments = attachments};
    }

    /// @brief Viewport and scissor covering the target.
    static void cover_target(RenderingCommands& scope)
    {
        scope.set_viewport({.width = static_cast<float>(TARGET_EXTENT.width), .height = static_cast<float>(TARGET_EXTENT.height)});
        scope.set_scissor({.width = TARGET_EXTENT.width, .height = TARGET_EXTENT.height});
    }

    void release_device_objects() override
    {
        m_target_view.reset();
        m_target.reset();
        m_vertex.reset();
        m_fragment.reset();
    }

    std::unique_ptr<Image> m_target;
    std::unique_ptr<ImageView> m_target_view;
    std::unique_ptr<ShaderModule> m_vertex;
    std::unique_ptr<ShaderModule> m_fragment;
};

TEST_F(VulkanCommandTest, BuffersAreCopiedFilledAndUpdatedByTheGpu)
{
    std::unique_ptr<Buffer> upload = m_device->create_buffer(
        {.size = 64, .usage = BufferUsage::TRANSFER_SOURCE, .location = MemoryLocation::HOST_UPLOAD, .debug_name = "upload"});
    std::ranges::iota(upload->mapped_elements<std::uint32_t>(), 100U);
    std::unique_ptr<Buffer> target = m_device->create_buffer(
        {.size = 256, .usage = BufferUsage::TRANSFER_SOURCE | BufferUsage::TRANSFER_DESTINATION, .debug_name = "target"});

    run([&](GraphicsRecorder& recorder)
    {
        // Fill everything, copy 16 words over the start, update 4 words in the middle.
        recorder.fill_buffer(target->handle(), 0, WHOLE_SIZE, 7);
        const std::array after_fill{buffer_barrier(target->handle(), ResourceState::TRANSFER_DESTINATION, ResourceState::TRANSFER_DESTINATION)};
        recorder.barrier({.buffer_barriers = after_fill});
        const std::array regions{BufferCopyRegion{.size = 64}};
        recorder.copy_buffer(upload->handle(), target->handle(), regions);
        const std::array words{1U, 2U, 3U, 4U};
        recorder.update_buffer(target->handle(), 128, std::as_bytes(std::span{words}));
    });

    const std::vector<std::uint32_t> result = read_buffer<std::uint32_t>(*target, 64);
    EXPECT_EQ(result[0], 100U);
    EXPECT_EQ(result[15], 115U);
    EXPECT_EQ(result[16], 7U);
    EXPECT_EQ(result[32], 1U);
    EXPECT_EQ(result[35], 4U);
    EXPECT_EQ(result[63], 7U);
}

TEST_F(VulkanCommandTest, ComputeWritesBindlessStorageBuffers)
{
    // The AZDO path: the pipeline uses the bindless layout; the buffer is named by its index.
    std::unique_ptr<ShaderModule> fill = m_device->create_shader_module(shaders::fill_comp);
    std::unique_ptr<ComputePipeline> pipeline = m_device->create_compute_pipeline({.shader = fill->handle(), .debug_name = "fill"});
    std::unique_ptr<Buffer> values = m_device->create_buffer(
        {.size = 1000 * sizeof(std::uint32_t), .usage = BufferUsage::STORAGE | BufferUsage::TRANSFER_SOURCE, .debug_name = "values"});
    ASSERT_TRUE(values->bindless_index().is_valid());

    run<QueueType::COMPUTE>([&](ComputeRecorder& recorder)
    {
        recorder.bind_pipeline(pipeline->handle());
        recorder.push_constants(FillPush{.target = values->bindless_index().value(), .count = 1000, .value = 5000});
        recorder.dispatch((1000 + 63) / 64);
    });

    // A compute queue's work is visible to the graphics queue after the fence (same memory, no transfer).
    const std::vector<std::uint32_t> result = read_buffer<std::uint32_t>(*values, 1000, ResourceState::COMPUTE_SHADER_STORAGE_WRITE);
    for (std::uint32_t index = 0; index < 1000; index += 97)
    {
        EXPECT_EQ(result[index], 5000 + index);
    }
    EXPECT_EQ(result[999], 5999U);
}

TEST_F(VulkanCommandTest, ComputeUsesClassicDescriptorSets)
{
    std::unique_ptr<ShaderModule> classic = m_device->create_shader_module(shaders::classic_comp);
    const ReflectedPipelineLayout layout = m_device->create_pipeline_layout_from({classic.get()});
    const std::array constants{specialization_constant(0, 3U)};
    std::unique_ptr<ComputePipeline> pipeline =
        m_device->create_compute_pipeline({.layout = layout.handle(), .shader = classic->handle(), .specialization_constants = constants});

    // Parameters through a uniform buffer, values through a storage buffer.
    std::unique_ptr<Buffer> parameters = m_device->create_buffer(
        {.size = 16, .usage = BufferUsage::UNIFORM, .location = MemoryLocation::HOST_UPLOAD, .debug_name = "parameters"});
    parameters->mapped_elements<std::uint32_t>()[0] = 100;
    std::unique_ptr<Buffer> values = m_device->create_buffer({.size = 128 * sizeof(std::uint32_t),
                                                              .usage = BufferUsage::STORAGE | BufferUsage::TRANSFER_SOURCE,
                                                              .location = MemoryLocation::HOST_UPLOAD,
                                                              .debug_name = "values"});
    std::ranges::fill(values->mapped_elements<std::uint32_t>(), 7U);
    const std::array sizes{DescriptorPoolSize{.type = DescriptorType::UNIFORM_BUFFER, .count = 1},
                           DescriptorPoolSize{.type = DescriptorType::STORAGE_BUFFER, .count = 1}};
    std::unique_ptr<DescriptorPool> pool = m_device->create_descriptor_pool({.maximum_sets = 1, .sizes = sizes});
    std::unique_ptr<DescriptorSet> set = pool->allocate(layout.set_layout(0).handle());
    set->write(layout.find_binding("parameters"), BufferDescriptor{.buffer = parameters->handle()});
    set->write(layout.find_binding("data"), BufferDescriptor{.buffer = values->handle()});

    run([&](GraphicsRecorder& recorder)
    {
        recorder.bind_pipeline(pipeline->handle());
        const std::array sets{set->handle()};
        recorder.bind_descriptor_sets(layout.handle(), 0, sets);
        recorder.dispatch(2);
    });
    const std::vector<std::uint32_t> result = read_buffer<std::uint32_t>(*values, 128, ResourceState::COMPUTE_SHADER_STORAGE_WRITE);
    EXPECT_EQ(result[0], 21U);
    EXPECT_EQ(result[99], 21U);
    EXPECT_EQ(result[100], 7U) << "only parameters.count values are multiplied";
}

TEST_F(VulkanCommandTest, RenderingPassesClearAndDraw)
{
    create_target();
    const std::array formats{Format::R8G8B8A8_UNORM};
    std::unique_ptr<GraphicsPipeline> pipeline = triangle_pipeline(formats);
    run([&](GraphicsRecorder& recorder)
    {
        const std::array to_target{image_barrier(m_target->handle(), ResourceState::UNDEFINED, ResourceState::COLOR_ATTACHMENT_WRITE)};
        recorder.barrier({.image_barriers = to_target});
        const std::array attachments{ColorAttachment{.view = m_target_view->handle(), .clear_color = {0.0F, 0.0F, 1.0F, 1.0F}}};
        RenderingScope scope = recorder.begin_rendering(pass(attachments));
        cover_target(scope);
        scope.bind_pipeline(pipeline->handle());
        scope.push_constants(Tint{.color = {1.0F, 0.0F, 0.0F, 1.0F}});
        scope.draw({.vertex_count = 3});
    });
    const std::vector<std::uint32_t> pixels = read_image(*m_target, ResourceState::COLOR_ATTACHMENT_WRITE);
    EXPECT_EQ(pixels.front(), rgba(255, 0, 0, 255));
    EXPECT_EQ(pixels.back(), rgba(255, 0, 0, 255));
}

TEST_F(VulkanCommandTest, ClearOnlyPassesWriteTheClearColor)
{
    create_target();
    run([&](GraphicsRecorder& recorder)
    {
        const std::array to_target{image_barrier(m_target->handle(), ResourceState::UNDEFINED, ResourceState::COLOR_ATTACHMENT_WRITE)};
        recorder.barrier({.image_barriers = to_target});
        const std::array attachments{ColorAttachment{.view = m_target_view->handle(), .clear_color = {0.0F, 1.0F, 0.0F, 1.0F}}};
        RenderingScope scope = recorder.begin_rendering(pass(attachments));
    });
    const std::vector<std::uint32_t> pixels = read_image(*m_target, ResourceState::COLOR_ATTACHMENT_WRITE);
    EXPECT_EQ(pixels[17], rgba(0, 255, 0, 255));
}

TEST_F(VulkanCommandTest, IndirectDrawsReadTheirArgumentsFromBuffers)
{
    create_target();
    const std::array formats{Format::R8G8B8A8_UNORM};
    std::unique_ptr<GraphicsPipeline> pipeline = triangle_pipeline(formats);

    // The arguments a culling pass would write; the count read from a buffer when supported.
    std::unique_ptr<Buffer> arguments = m_device->create_buffer({.size = sizeof(DrawArguments) + sizeof(std::uint32_t),
                                                                 .usage = BufferUsage::INDIRECT,
                                                                 .location = MemoryLocation::HOST_UPLOAD,
                                                                 .debug_name = "arguments"});
    const DrawArguments draw{.vertex_count = 3};
    std::memcpy(arguments->mapped_memory().data(), &draw, sizeof(draw));
    const std::uint32_t one = 1;
    std::memcpy(arguments->mapped_memory().data() + sizeof(DrawArguments), &one, sizeof(one));
    const bool count_buffer = m_device->physical_device().supports(DeviceFeature::DRAW_INDIRECT_COUNT);

    run([&](GraphicsRecorder& recorder)
    {
        const std::array to_target{image_barrier(m_target->handle(), ResourceState::UNDEFINED, ResourceState::COLOR_ATTACHMENT_WRITE)};
        recorder.barrier({.image_barriers = to_target});
        const std::array attachments{ColorAttachment{.view = m_target_view->handle()}};
        RenderingScope scope = recorder.begin_rendering(pass(attachments));
        cover_target(scope);
        scope.bind_pipeline(pipeline->handle());
        scope.push_constants(Tint{.color = {1.0F, 1.0F, 0.0F, 1.0F}});
        if (count_buffer)
        {
            scope.draw_indirect_count({.buffer = arguments->handle()},
                                      {.buffer = arguments->handle(), .offset = sizeof(DrawArguments), .maximum_draw_count = 1});
        }
        else
        {
            scope.draw_indirect({.buffer = arguments->handle()});
        }
    });
    EXPECT_EQ(read_image(*m_target, ResourceState::COLOR_ATTACHMENT_WRITE)[100], rgba(255, 255, 0, 255));
}

TEST_F(VulkanCommandTest, VertexAndIndexBuffersFeedClassicVertexInput)
{
    create_target();
    m_vertex = m_device->create_shader_module(shaders::vertices_vert);
    m_fragment = m_device->create_shader_module(shaders::triangle_frag);
    const std::array formats{Format::R8G8B8A8_UNORM};
    const std::array bindings{VertexInputBinding{.binding = 0, .stride = 24}};
    const std::array inputs{VertexInputAttribute{.location = 0, .binding = 0, .format = Format::R32G32_SFLOAT, .offset = 0},
                            VertexInputAttribute{.location = 1, .binding = 0, .format = Format::R32G32B32A32_SFLOAT, .offset = 8}};
    std::unique_ptr<GraphicsPipeline> pipeline = m_device->create_graphics_pipeline({.vertex_shader = m_vertex->handle(),
                                                                                     .fragment_shader = m_fragment->handle(),
                                                                                     .vertex_bindings = bindings,
                                                                                     .vertex_attributes = inputs,
                                                                                     .color_formats = formats,
                                                                                     .state = {.rasterization = {.cull_mode = CullMode::NONE}}});

    // A full-screen quad of two triangles, cyan.
    const std::array<float, 24> vertices{-1, -1, 0, 1, 1, 1, 3, -1, 0, 1, 1, 1, -1, 3, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0};
    std::unique_ptr<Buffer> vertex_buffer = m_device->create_buffer(
        {.size = sizeof(vertices), .usage = BufferUsage::VERTEX, .location = MemoryLocation::HOST_UPLOAD, .debug_name = "vertices"});
    std::memcpy(vertex_buffer->mapped_memory().data(), vertices.data(), sizeof(vertices));
    const std::array<std::uint16_t, 4> indices{0, 1, 2, 0};
    std::unique_ptr<Buffer> index_buffer = m_device->create_buffer(
        {.size = sizeof(indices), .usage = BufferUsage::INDEX, .location = MemoryLocation::HOST_UPLOAD, .debug_name = "indices"});
    std::memcpy(index_buffer->mapped_memory().data(), indices.data(), sizeof(indices));

    run([&](GraphicsRecorder& recorder)
    {
        const std::array to_target{image_barrier(m_target->handle(), ResourceState::UNDEFINED, ResourceState::COLOR_ATTACHMENT_WRITE)};
        recorder.barrier({.image_barriers = to_target});
        const std::array attachments{ColorAttachment{.view = m_target_view->handle()}};
        RenderingScope scope = recorder.begin_rendering(pass(attachments));
        cover_target(scope);
        scope.bind_pipeline(pipeline->handle());
        const std::array vertex_buffers{VertexBufferBinding{.buffer = vertex_buffer->handle()}};
        scope.bind_vertex_buffers(0, vertex_buffers);
        scope.bind_index_buffer(index_buffer->handle(), 0, IndexType::UNSIGNED_INTEGER_16);
        scope.draw_indexed({.index_count = 3});
    });
    EXPECT_EQ(read_image(*m_target, ResourceState::COLOR_ATTACHMENT_WRITE)[33], rgba(0, 255, 255, 255));
}

TEST_F(VulkanCommandTest, FragmentShadersSampleBindlessTextures)
{
    create_target();
    // A 4x4 magenta texture, uploaded through a staging buffer.
    std::unique_ptr<Image> texture = m_device->create_image({.format = Format::R8G8B8A8_UNORM,
                                                             .extent = {.width = 4, .height = 4},
                                                             .usage = ImageUsage::SAMPLED | ImageUsage::TRANSFER_DESTINATION,
                                                             .debug_name = "texture"});
    std::unique_ptr<ImageView> texture_view = m_device->create_image_view({.image = texture->handle()});
    std::unique_ptr<Sampler> sampler = m_device->create_sampler({.magnification_filter = Filter::NEAREST});
    std::unique_ptr<Buffer> staging = m_device->create_buffer(
        {.size = 64, .usage = BufferUsage::TRANSFER_SOURCE, .location = MemoryLocation::HOST_UPLOAD, .debug_name = "staging"});
    std::ranges::fill(staging->mapped_elements<std::uint32_t>(), rgba(255, 0, 255, 255));

    m_vertex = m_device->create_shader_module(shaders::triangle_vert);
    m_fragment = m_device->create_shader_module(shaders::textured_frag);
    const std::array formats{Format::R8G8B8A8_UNORM};
    std::unique_ptr<GraphicsPipeline> pipeline = m_device->create_graphics_pipeline({.vertex_shader = m_vertex->handle(),
                                                                                     .fragment_shader = m_fragment->handle(),
                                                                                     .color_formats = formats,
                                                                                     .state = {.rasterization = {.cull_mode = CullMode::NONE}}});
    struct Indices
    {
        std::uint32_t texture = 0;
        std::uint32_t sampler = 0;
    };

    run([&](GraphicsRecorder& recorder)
    {
        const std::array to_copy{image_barrier(texture->handle(), ResourceState::UNDEFINED, ResourceState::TRANSFER_DESTINATION)};
        recorder.barrier({.image_barriers = to_copy});
        const std::array regions{BufferImageCopyRegion{.image_extent = {.width = 4, .height = 4}}};
        recorder.copy_buffer_to_image(staging->handle(), texture->handle(), ImageLayout::TRANSFER_DESTINATION, regions);
        const std::array barriers{
            image_barrier(texture->handle(), ResourceState::TRANSFER_DESTINATION, ResourceState::FRAGMENT_SHADER_SAMPLED_READ),
            image_barrier(m_target->handle(), ResourceState::UNDEFINED, ResourceState::COLOR_ATTACHMENT_WRITE)};
        recorder.barrier({.image_barriers = barriers});
        const std::array attachments{ColorAttachment{.view = m_target_view->handle()}};
        RenderingScope scope = recorder.begin_rendering(pass(attachments));
        cover_target(scope);
        scope.bind_pipeline(pipeline->handle());
        scope.push_constants(Indices{.texture = texture_view->sampled_index().value(), .sampler = sampler->bindless_index().value()}, 16);
        scope.draw({.vertex_count = 3});
    });
    EXPECT_EQ(read_image(*m_target, ResourceState::COLOR_ATTACHMENT_WRITE)[5], rgba(255, 0, 255, 255));
}

TEST_F(VulkanCommandTest, ParallelRenderingPassesExecutePartsRecordedOnOtherThreads)
{
    create_target();
    const std::array formats{Format::R8G8B8A8_UNORM};
    std::unique_ptr<GraphicsPipeline> pipeline = triangle_pipeline(formats);
    std::unique_ptr<CommandPool> primary_pool = m_device->create_command_pool({.queue_type = QueueType::GRAPHICS});
    std::unique_ptr<CommandBuffer> primary = primary_pool->allocate();

    // Four parts, each recorded by its own thread with its own pool, each drawing one quadrant.
    constexpr std::array<std::array<float, 4>, 4> COLORS{{{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 1, 1}}};
    std::vector<std::unique_ptr<CommandPool>> part_pools;
    std::vector<std::unique_ptr<CommandBuffer>> parts;
    for (std::size_t index = 0; index < 4; ++index)
    {
        part_pools.push_back(m_device->create_command_pool({.queue_type = QueueType::GRAPHICS}));
        parts.push_back(part_pools.back()->allocate(CommandBufferLevel::SECONDARY, std::format("part {}", index)));
    }
    {
        GraphicsRecorder recorder{*primary};
        const std::array to_target{image_barrier(m_target->handle(), ResourceState::UNDEFINED, ResourceState::COLOR_ATTACHMENT_WRITE)};
        recorder.barrier({.image_barriers = to_target});
        const std::array attachments{ColorAttachment{.view = m_target_view->handle()}};
        ParallelRenderingScope scope = recorder.begin_parallel_rendering(pass(attachments));
        {
            std::vector<std::jthread> threads;
            for (std::size_t index = 0; index < 4; ++index)
            {
                threads.emplace_back([&, index]
                {
                    RenderingPartRecorder part{*parts[index], scope.inheritance()};
                    const std::uint32_t half = TARGET_EXTENT.width / 2;
                    part.set_viewport({.width = static_cast<float>(TARGET_EXTENT.width), .height = static_cast<float>(TARGET_EXTENT.height)});
                    part.set_scissor({.x = static_cast<std::int32_t>(half * (index % 2)),
                                      .y = static_cast<std::int32_t>(half * (index / 2)),
                                      .width = half,
                                      .height = half});
                    part.bind_pipeline(pipeline->handle());
                    part.push_constants(Tint{.color = COLORS[index]});
                    part.draw({.vertex_count = 3});
                });
            }
        }
        scope.execute(parts);
    }
    submit_and_wait(m_device->queue(QueueType::GRAPHICS), primary->handle());

    const std::vector<std::uint32_t> pixels = read_image(*m_target, ResourceState::COLOR_ATTACHMENT_WRITE);
    const auto pixel = [&pixels](std::uint32_t x, std::uint32_t y) { return pixels[y * TARGET_EXTENT.width + x]; };
    EXPECT_EQ(pixel(4, 4), rgba(255, 0, 0, 255));
    EXPECT_EQ(pixel(20, 4), rgba(0, 255, 0, 255));
    EXPECT_EQ(pixel(4, 20), rgba(0, 0, 255, 255));
    EXPECT_EQ(pixel(20, 20), rgba(255, 255, 255, 255));
    EXPECT_EQ(primary->statistics().rendering_passes, 1U);
}

TEST_F(VulkanCommandTest, ComputeAndGraphicsQueuesSynchronizeThroughTimelines)
{
    // Compute writes a buffer and signals value 1; the graphics queue waits for it and copies the buffer.
    std::unique_ptr<ShaderModule> fill = m_device->create_shader_module(shaders::fill_comp);
    std::unique_ptr<ComputePipeline> pipeline = m_device->create_compute_pipeline({.shader = fill->handle()});
    std::unique_ptr<Buffer> values = m_device->create_buffer({.size = 256,
                                                              .usage = BufferUsage::STORAGE | BufferUsage::TRANSFER_SOURCE,
                                                              .sharing = SharingMode::CONCURRENT,
                                                              .debug_name = "shared values"});
    std::unique_ptr<Buffer> copy = m_device->create_buffer({.size = 256,
                                                            .usage = BufferUsage::TRANSFER_DESTINATION | BufferUsage::TRANSFER_SOURCE,
                                                            .debug_name = "copy"});
    std::unique_ptr<TimelineSemaphore> timeline = m_device->create_timeline_semaphore({.debug_name = "compute to graphics"});
    std::unique_ptr<CommandPool> compute_pool = m_device->create_command_pool({.queue_type = QueueType::COMPUTE});
    std::unique_ptr<CommandPool> graphics_pool = m_device->create_command_pool({.queue_type = QueueType::GRAPHICS});
    std::unique_ptr<CommandBuffer> compute = compute_pool->allocate();
    std::unique_ptr<CommandBuffer> graphics = graphics_pool->allocate();
    {
        ComputeRecorder recorder{*compute};
        recorder.bind_pipeline(pipeline->handle());
        recorder.push_constants(FillPush{.target = values->bindless_index().value(), .count = 64, .value = 1000});
        recorder.dispatch(1);
    }
    {
        GraphicsRecorder recorder{*graphics};
        const std::array regions{BufferCopyRegion{.size = 256}};
        recorder.copy_buffer(values->handle(), copy->handle(), regions);
    }

    // Submit the waiting side first where the queues run on separate hardware queues (the GPU sorts it
    // out); where they share one, the signaling side must come first (the other order is refused).
    const std::array graphics_buffers{graphics->handle()};
    const std::array waits{TimelineSemaphoreWait{.semaphore = timeline->handle(), .value = 1, .stages = PipelineStage::TRANSFER}};
    const std::array signal_two{TimelineSemaphoreSignal{.semaphore = timeline->handle(), .value = 2}};
    const std::array graphics_batch{SubmitDescription{.command_buffers = graphics_buffers, .timeline_waits = waits,
                                                      .timeline_signals = signal_two}};
    const std::array compute_buffers{compute->handle()};
    const std::array signal_one{TimelineSemaphoreSignal{.semaphore = timeline->handle(), .value = 1, .stages = PipelineStage::COMPUTE_SHADER}};
    const std::array compute_batch{SubmitDescription{.command_buffers = compute_buffers, .timeline_signals = signal_one}};
    if (queues_share_hardware(QueueType::COMPUTE, QueueType::GRAPHICS))
    {
        m_device->queue(QueueType::COMPUTE).submit(compute_batch);
        m_device->queue(QueueType::GRAPHICS).submit(graphics_batch);
    }
    else
    {
        m_device->queue(QueueType::GRAPHICS).submit(graphics_batch);
        m_device->queue(QueueType::COMPUTE).submit(compute_batch);
    }
    ASSERT_EQ(timeline->wait(2, std::chrono::seconds{10}), WaitStatus::SIGNALED);

    const std::vector<std::uint32_t> result = read_buffer<std::uint32_t>(*copy, 64);
    EXPECT_EQ(result[0], 1000U);
    EXPECT_EQ(result[63], 1063U);
}

TEST_F(VulkanCommandTest, TimestampsMeasureGpuWork)
{
    if (!m_device->physical_device().supports(DeviceFeature::TIMESTAMP_QUERY))
    {
        GTEST_SKIP() << "no timestamps";
    }
    std::unique_ptr<QueryPool> timestamps = m_device->create_query_pool({.type = QueryType::TIMESTAMP, .count = 2});
    std::unique_ptr<Buffer> scratch = m_device->create_buffer({.size = 1 << 20, .usage = BufferUsage::TRANSFER_DESTINATION});
    run([&](GraphicsRecorder& recorder)
    {
        recorder.reset_query_pool(timestamps->handle(), 0, 2);
        recorder.write_timestamp(timestamps->handle(), PipelineStage::ALL_COMMANDS, 0);
        recorder.fill_buffer(scratch->handle(), 0, WHOLE_SIZE, 1);
        recorder.write_timestamp(timestamps->handle(), PipelineStage::ALL_COMMANDS, 1);
    });
    std::array<std::uint64_t, 2> ticks{};
    ASSERT_TRUE(timestamps->results(0, ticks, true));
    EXPECT_GE(ticks[1], ticks[0]);
}

TEST_F(VulkanCommandTest, EventsSplitBarriers)
{
    std::unique_ptr<Event> event = m_device->create_event({});
    std::unique_ptr<Buffer> values = m_device->create_buffer(
        {.size = 64, .usage = BufferUsage::TRANSFER_SOURCE | BufferUsage::TRANSFER_DESTINATION});
    run([&](GraphicsRecorder& recorder)
    {
        recorder.fill_buffer(values->handle(), 0, WHOLE_SIZE, 9);
        const std::array barrier{buffer_barrier(values->handle(), ResourceState::TRANSFER_DESTINATION, ResourceState::TRANSFER_SOURCE)};
        recorder.set_event(event->handle(), {.buffer_barriers = barrier});
        const std::array events{event->handle()};
        recorder.wait_events(events, {.buffer_barriers = barrier});
    });
    EXPECT_TRUE(event->is_set());
    EXPECT_EQ(read_buffer<std::uint32_t>(*values, 16, ResourceState::TRANSFER_SOURCE)[15], 9U);
}

TEST_F(VulkanCommandTest, MipmapsAreGeneratedByBlits)
{
    // A 16x16 image whose level 0 is one color: every level ends with that color.
    std::unique_ptr<Image> image = m_device->create_image({
        .format = Format::R8G8B8A8_UNORM,
        .extent = {.width = 16, .height = 16},
        .mip_level_count = 5,
        .usage = ImageUsage::SAMPLED | ImageUsage::TRANSFER_SOURCE | ImageUsage::TRANSFER_DESTINATION,
    });
    std::unique_ptr<Buffer> readback = m_device->create_buffer(
        {.size = 4, .usage = BufferUsage::TRANSFER_DESTINATION, .location = MemoryLocation::HOST_READBACK});
    run([&](GraphicsRecorder& recorder)
    {
        const std::array to_clear{image_barrier(image->handle(), ResourceState::UNDEFINED, ResourceState::TRANSFER_DESTINATION)};
        recorder.barrier({.image_barriers = to_clear});
        recorder.clear_color_image(image->handle(), ImageLayout::TRANSFER_DESTINATION, {0.0F, 0.5F, 1.0F, 1.0F}, {.mip_level_count = 1});
        recorder.generate_mipmaps(image->handle(), image->extent(), 5, ResourceState::TRANSFER_DESTINATION, ResourceState::TRANSFER_SOURCE);
        const std::array regions{BufferImageCopyRegion{.image_subresource = {.mip_level = 4}, .image_extent = {.width = 1, .height = 1}}};
        recorder.copy_image_to_buffer(image->handle(), ImageLayout::TRANSFER_SOURCE, readback->handle(), regions);
        const std::array after{buffer_barrier(readback->handle(), ResourceState::TRANSFER_DESTINATION, ResourceState::HOST_READ)};
        recorder.barrier({.buffer_barriers = after});
    });
    readback->invalidate_mapped_range();
    const std::uint32_t texel = readback->mapped_elements<std::uint32_t>()[0];
    EXPECT_EQ(texel & 0xFFU, 0U);
    EXPECT_NEAR(static_cast<double>((texel >> 8U) & 0xFFU), 128.0, 1.0);
    EXPECT_EQ((texel >> 16U) & 0xFFU, 255U);
}

TEST_F(VulkanCommandTest, IntegerImagesAreClearedWithIntegerValues)
{
    std::unique_ptr<Image> image = m_device->create_image({.format = Format::R32_UINT,
                                                           .extent = {.width = 4, .height = 4},
                                                           .usage = ImageUsage::TRANSFER_SOURCE | ImageUsage::TRANSFER_DESTINATION});
    run([&](GraphicsRecorder& recorder)
    {
        const std::array to_clear{image_barrier(image->handle(), ResourceState::UNDEFINED, ResourceState::TRANSFER_DESTINATION)};
        recorder.barrier({.image_barriers = to_clear});
        recorder.clear_color_image(image->handle(), ImageLayout::TRANSFER_DESTINATION, {1234.0F, 0.0F, 0.0F, 0.0F}, {});
    });
    EXPECT_EQ(read_image(*image, ResourceState::TRANSFER_DESTINATION)[0], 1234U);
}

TEST_F(VulkanCommandTest, CommandsWithStaleHandlesFailAndTheCommandBufferCannotBeSubmitted)
{
    std::unique_ptr<Buffer> buffer = m_device->create_buffer({.size = 64, .usage = BufferUsage::TRANSFER_DESTINATION});
    const BufferHandle stale = buffer->handle();
    buffer.reset();
    std::unique_ptr<CommandPool> pool = m_device->create_command_pool({});
    std::unique_ptr<CommandBuffer> command_buffer = pool->allocate();
    {
        GraphicsRecorder recorder{*command_buffer};
        EXPECT_THROW(recorder.fill_buffer(stale, 0, WHOLE_SIZE, 0), InvalidHandleError);
    }
    const std::array command_buffers{command_buffer->handle()};
    const std::array batches{SubmitDescription{.command_buffers = command_buffers}};
    try
    {
        m_device->queue(QueueType::GRAPHICS).submit(batches);
        FAIL() << "submit must throw";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("Recorder::fill_buffer failed while recording"), std::string_view::npos) << error.what();
    }
}

TEST_F(VulkanCommandTest, CommandBuffersCountWhatTheyRecord)
{
    std::unique_ptr<ShaderModule> fill = m_device->create_shader_module(shaders::fill_comp);
    std::unique_ptr<ComputePipeline> pipeline = m_device->create_compute_pipeline({.shader = fill->handle()});
    std::unique_ptr<Buffer> values = m_device->create_buffer({.size = 256, .usage = BufferUsage::STORAGE});
    std::unique_ptr<CommandPool> pool = m_device->create_command_pool({.queue_type = QueueType::COMPUTE});
    std::unique_ptr<CommandBuffer> command_buffer = pool->allocate();
    {
        ComputeRecorder recorder{*command_buffer};
        recorder.bind_pipeline(pipeline->handle());
        for (std::uint32_t index = 0; index < 3; ++index)
        {
            recorder.push_constants(FillPush{.target = values->bindless_index().value(), .count = 64, .value = index});
            recorder.dispatch(1);
        }
    }
    const CommandStatistics& statistics = command_buffer->statistics();
    EXPECT_EQ(statistics.pipeline_binds, 1U);
    EXPECT_EQ(statistics.dispatches, 3U);
    EXPECT_EQ(statistics.push_constant_writes, 3U);
    EXPECT_EQ(statistics.descriptor_set_binds, 0U) << "the bindless table is bound by the backend";
    submit_and_wait(m_device->queue(QueueType::COMPUTE), command_buffer->handle());
}

TEST_F(VulkanCommandTest, ResettableCommandBuffersAreRecordedAgain)
{
    std::unique_ptr<CommandPool> pool =
        m_device->create_command_pool({.queue_type = QueueType::GRAPHICS, .options = CommandPoolOption::RESETTABLE_COMMAND_BUFFERS});
    std::unique_ptr<CommandBuffer> command_buffer = pool->allocate();
    std::unique_ptr<Buffer> values = m_device->create_buffer({.size = 64, .usage = BufferUsage::TRANSFER_DESTINATION | BufferUsage::TRANSFER_SOURCE});
    for (std::uint32_t round = 1; round <= 2; ++round)
    {
        {
            GraphicsRecorder recorder{*command_buffer};
            recorder.fill_buffer(values->handle(), 0, WHOLE_SIZE, round);
        }
        submit_and_wait(m_device->queue(QueueType::GRAPHICS), command_buffer->handle());
    }
    EXPECT_EQ(read_buffer<std::uint32_t>(*values, 1)[0], 2U);

    std::unique_ptr<CommandPool> plain = m_device->create_command_pool({});
    std::unique_ptr<CommandBuffer> once = plain->allocate();
    EXPECT_THROW(once->reset(), InvalidUsageError);
}

} // namespace
