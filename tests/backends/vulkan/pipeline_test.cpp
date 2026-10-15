#include "vulkan_test.hpp"

import gfx.tests.vulkan_shaders;

namespace
{

using namespace gfx;
using gfx::tests::VulkanDeviceTest;
namespace shaders = gfx::tests::vulkan_shaders;

auto
contains(std::string_view text, std::string_view part) -> bool
{
    return text.find(part) != std::string_view::npos;
}

TEST_F(VulkanDeviceTest, ShaderModulesReflectTheirSpirv)
{
    std::unique_ptr<ShaderModule> fill = m_device->create_shader_module(shaders::fill_comp);
    EXPECT_EQ(fill->stage(), ShaderStage::COMPUTE);
    EXPECT_TRUE(fill->handle().is_valid());
    EXPECT_EQ(fill->debug_name(), "fill.comp");
    EXPECT_EQ(fill->layout().workgroup_size, (std::array<std::uint32_t, 3>{64, 1, 1}));
    const ShaderBinding* buffers = fill->layout().find("gfx_storage_buffers");
    ASSERT_NE(buffers, nullptr);
    EXPECT_EQ(buffers->count, 0U) << "a bindless array is runtime-sized";
    EXPECT_EQ(fill->layout().push_constant_size, 12U);

    // A description whose stage does not match its code is refused.
    ShaderModuleDescription wrong = shaders::fill_comp;
    wrong.stage = ShaderStage::FRAGMENT;
    EXPECT_THROW((void)m_device->create_shader_module(wrong), InvalidUsageError);
}

TEST_F(VulkanDeviceTest, GlslSourceCompilesInDebugBuildsOnly)
{
    static constexpr std::string_view SOURCE = "#version 460\nlayout(local_size_x = 8) in;\nvoid main() {}\n";
    const ShaderModuleDescription description{.stage = ShaderStage::COMPUTE,
                                              .source_format = ShaderSourceFormat::GLSL,
                                              .source = SOURCE,
                                              .name = "inline.comp"};
    if constexpr (SHADER_HOT_RELOAD)
    {
        std::unique_ptr<ShaderModule> module = m_device->create_shader_module(description);
        EXPECT_EQ(module->layout().workgroup_size[0], 8U);
    }
    else
    {
        EXPECT_THROW((void)m_device->create_shader_module(description), InvalidUsageError);
    }
}

TEST_F(VulkanDeviceTest, ComputePipelinesUseTheBindlessLayoutByDefault)
{
    std::unique_ptr<ShaderModule> fill = m_device->create_shader_module(shaders::fill_comp);
    std::unique_ptr<ComputePipeline> pipeline = m_device->create_compute_pipeline({.shader = fill->handle(), .debug_name = "fill"});
    EXPECT_EQ(pipeline->status(), PipelineStatus::READY);
    EXPECT_EQ(pipeline->layout(), m_device->bindless_pipeline_layout().handle());
    EXPECT_EQ(pipeline->workgroup_size(), (std::array<std::uint32_t, 3>{64, 1, 1}));
    EXPECT_TRUE(pipeline->handle().is_valid());
    pipeline->wait();
}

TEST_F(VulkanDeviceTest, PipelinesThatDoNotFitTheirLayoutAreRefused)
{
    // classic.comp needs set 0 bindings 0 and 1, which the bindless layout declares differently.
    std::unique_ptr<ShaderModule> classic = m_device->create_shader_module(shaders::classic_comp);
    try
    {
        (void)m_device->create_compute_pipeline({.shader = classic->handle()});
        FAIL() << "create_compute_pipeline must throw";
    }
    catch (const ShaderLayoutMismatchError& error)
    {
        EXPECT_TRUE(contains(error.what(), "set 0 binding 0")) << error.what();
    }

    // Constants the shader does not declare are refused too.
    std::unique_ptr<ShaderModule> fill = m_device->create_shader_module(shaders::fill_comp);
    const std::array constants{specialization_constant(7, 1U)};
    EXPECT_THROW((void)m_device->create_compute_pipeline({.shader = fill->handle(), .specialization_constants = constants}),
                 ShaderLayoutMismatchError);
}

TEST_F(VulkanDeviceTest, ReflectedLayoutsServeClassicDescriptorSets)
{
    // The layout derived from the shader, a pool, and a set written by binding name.
    std::unique_ptr<ShaderModule> classic = m_device->create_shader_module(shaders::classic_comp);
    const ReflectedPipelineLayout layout = m_device->create_pipeline_layout_from({classic.get()}, "classic");
    const std::array constants{specialization_constant(0, 3U)};
    std::unique_ptr<ComputePipeline> pipeline =
        m_device->create_compute_pipeline({.layout = layout.handle(), .shader = classic->handle(), .specialization_constants = constants});
    EXPECT_EQ(pipeline->status(), PipelineStatus::READY);

    const std::array sizes{DescriptorPoolSize{.type = DescriptorType::UNIFORM_BUFFER, .count = 4},
                           DescriptorPoolSize{.type = DescriptorType::STORAGE_BUFFER, .count = 4}};
    std::unique_ptr<DescriptorPool> pool = m_device->create_descriptor_pool({.maximum_sets = 2, .sizes = sizes, .debug_name = "pool"});
    std::unique_ptr<DescriptorSet> set = pool->allocate(layout.set_layout(0).handle());
    EXPECT_EQ(set->layout(), layout.set_layout(0).handle());

    std::unique_ptr<Buffer> parameters = m_device->create_buffer({.size = 16, .usage = BufferUsage::UNIFORM});
    std::unique_ptr<Buffer> values = m_device->create_buffer({.size = 256, .usage = BufferUsage::STORAGE});
    set->write(layout.find_binding("parameters"), BufferDescriptor{.buffer = parameters->handle()});
    set->write(layout.find_binding("data"), BufferDescriptor{.buffer = values->handle()});
    EXPECT_THROW(set->write(layout.find_binding("data"), SamplerDescriptor{}), InvalidUsageError);

    // A pool with live sets cannot be reset; without them it can.
    EXPECT_THROW(pool->reset(), InvalidUsageError);
    set.reset();
    pool->reset();
}

TEST_F(VulkanDeviceTest, GraphicsPipelinesCheckTheirAttachmentsAndVertexInput)
{
    std::unique_ptr<ShaderModule> vertex = m_device->create_shader_module(shaders::triangle_vert);
    std::unique_ptr<ShaderModule> fragment = m_device->create_shader_module(shaders::triangle_frag);
    const std::array formats{Format::R8G8B8A8_UNORM};
    std::unique_ptr<GraphicsPipeline> pipeline = m_device->create_graphics_pipeline({
        .vertex_shader = vertex->handle(),
        .fragment_shader = fragment->handle(),
        .color_formats = formats,
        .depth_format = Format::D32_SFLOAT,
        .state = {.depth_stencil = {.depth_test = true, .depth_write = true},
                  .dynamic_states = DynamicState::CULL_MODE | DynamicState::DEPTH_TEST,
                  .debug_name = "triangle"},
    });
    EXPECT_EQ(pipeline->status(), PipelineStatus::READY);
    EXPECT_EQ(pipeline->debug_name(), "triangle");

    // A floating-point output into an integer attachment is refused.
    const std::array integers{Format::R32_UINT};
    EXPECT_THROW((void)m_device->create_graphics_pipeline(
                     {.vertex_shader = vertex->handle(), .fragment_shader = fragment->handle(), .color_formats = integers}),
                 ShaderLayoutMismatchError);

    // Vertex attributes must feed every vertex input.
    std::unique_ptr<ShaderModule> attributes = m_device->create_shader_module(shaders::vertices_vert);
    EXPECT_THROW((void)m_device->create_graphics_pipeline(
                     {.vertex_shader = attributes->handle(), .fragment_shader = fragment->handle(), .color_formats = formats}),
                 ShaderLayoutMismatchError);
    const std::array bindings{VertexInputBinding{.binding = 0, .stride = 24}};
    const std::array inputs{VertexInputAttribute{.location = 0, .binding = 0, .format = Format::R32G32_SFLOAT, .offset = 0},
                            VertexInputAttribute{.location = 1, .binding = 0, .format = Format::R32G32B32A32_SFLOAT, .offset = 8}};
    std::unique_ptr<GraphicsPipeline> with_vertices = m_device->create_graphics_pipeline({.vertex_shader = attributes->handle(),
                                                                                          .fragment_shader = fragment->handle(),
                                                                                          .vertex_bindings = bindings,
                                                                                          .vertex_attributes = inputs,
                                                                                          .color_formats = formats});
    EXPECT_EQ(with_vertices->status(), PipelineStatus::READY);
}

TEST_F(VulkanDeviceTest, PipelinesCompileInTheBackgroundOnTheTaskScheduler)
{
    // A device whose task scheduler runs every task on its own thread.
    std::vector<std::jthread> workers;
    std::mutex workers_mutex;
    DeviceDescription description = device_description();
    description.task_scheduler = [&workers, &workers_mutex](std::move_only_function<void()> task)
    {
        const std::scoped_lock lock{workers_mutex};
        workers.emplace_back(std::move(task));
    };
    std::unique_ptr<Device> device = m_physical_device->create_device(description);
    {
        std::unique_ptr<ShaderModule> fill = device->create_shader_module(shaders::fill_comp);
        std::vector<std::unique_ptr<ComputePipeline>> pipelines;
        for (int index = 0; index < 8; ++index)
        {
            pipelines.push_back(device->create_compute_pipeline({.shader = fill->handle(), .creation_mode = PipelineCreationMode::ASYNCHRONOUS}));
        }
        for (const std::unique_ptr<ComputePipeline>& pipeline : pipelines)
        {
            EXPECT_NE(pipeline->status(), PipelineStatus::COMPILATION_FAILED);
            pipeline->wait();
            EXPECT_EQ(pipeline->status(), PipelineStatus::READY);
        }
    }
    workers.clear();
}

TEST_F(VulkanDeviceTest, AsynchronousCreationWithoutASchedulerCompilesAtOnce)
{
    std::unique_ptr<ShaderModule> fill = m_device->create_shader_module(shaders::fill_comp);
    std::unique_ptr<ComputePipeline> pipeline =
        m_device->create_compute_pipeline({.shader = fill->handle(), .creation_mode = PipelineCreationMode::ASYNCHRONOUS});
    EXPECT_EQ(pipeline->status(), PipelineStatus::READY);
}

TEST_F(VulkanDeviceTest, PipelineCachesKeepCompiledPipelinesAcrossRuns)
{
    std::unique_ptr<PipelineCache> cache = m_device->create_pipeline_cache({.debug_name = "cache"});
    EXPECT_FALSE(cache->loaded_initial_data());
    std::unique_ptr<ShaderModule> fill = m_device->create_shader_module(shaders::fill_comp);
    std::unique_ptr<ComputePipeline> pipeline = m_device->create_compute_pipeline({.shader = fill->handle(), .cache = cache->handle()});
    const std::vector<std::byte> data = cache->serialize();
    EXPECT_GT(data.size(), 32U);

    // The data of this device is accepted; anything else is ignored.
    std::unique_ptr<PipelineCache> reloaded = m_device->create_pipeline_cache({.initial_data = data});
    EXPECT_TRUE(reloaded->loaded_initial_data());
    const std::array<std::byte, 64> garbage{};
    std::unique_ptr<PipelineCache> ignored = m_device->create_pipeline_cache({.initial_data = garbage});
    EXPECT_FALSE(ignored->loaded_initial_data());
}

TEST_F(VulkanDeviceTest, DestroyedShaderModulesLeaveTheirPipelinesUsable)
{
    std::unique_ptr<ShaderModule> fill = m_device->create_shader_module(shaders::fill_comp);
    std::unique_ptr<ComputePipeline> pipeline = m_device->create_compute_pipeline({.shader = fill->handle()});
    fill.reset();
    EXPECT_EQ(pipeline->status(), PipelineStatus::READY);
    EXPECT_TRUE(m_device->poll_shader_changes().empty());
}

} // namespace
