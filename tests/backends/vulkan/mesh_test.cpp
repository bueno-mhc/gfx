#include "vulkan_test.hpp"

import gfx.tests.vulkan_shaders;

namespace
{

using namespace gfx;
using gfx::tests::rgba;
using gfx::tests::VulkanDeviceTest;
namespace shaders = gfx::tests::vulkan_shaders;

constexpr Extent TARGET_EXTENT{.width = 32, .height = 32, .depth = 1};

/// @brief Push constants of halves.mesh: the colors of the left and right halves.
struct HalvesPush
{
    std::array<float, 4> left{};
    std::array<float, 4> right{};
};

constexpr HalvesPush COLORS{.left = {1.0F, 0.0F, 0.0F, 1.0F}, .right = {0.0F, 1.0F, 0.0F, 1.0F}};

/// @brief A device with task and mesh shaders, a color target and the halves pipelines.
class VulkanMeshTest : public VulkanDeviceTest
{
protected:
    [[nodiscard]] auto required_features() const -> std::vector<DeviceFeature> override
    {
        return {DeviceFeature::MESH_SHADER};
    }

    void SetUp() override
    {
        VulkanDeviceTest::SetUp();
        if (IsSkipped())
        {
            return;
        }
        m_target = m_device->create_image({.format = Format::R8G8B8A8_UNORM,
                                           .extent = TARGET_EXTENT,
                                           .usage = ImageUsage::COLOR_ATTACHMENT | ImageUsage::TRANSFER_SOURCE,
                                           .debug_name = "target"});
        m_target_view = m_device->create_image_view({.image = m_target->handle(), .debug_name = "target view"});
        m_task = m_device->create_shader_module(shaders::halves_task);
        m_mesh = m_device->create_shader_module(shaders::halves_mesh);
        m_fragment = m_device->create_shader_module(shaders::triangle_frag);
    }

    /// @brief The halves pipeline, with or without its task shader.
    [[nodiscard]] auto pipeline(bool with_task_shader) -> std::unique_ptr<GraphicsPipeline>
    {
        return m_device->create_graphics_pipeline({.fragment_shader = m_fragment->handle(),
                                                   .task_shader = with_task_shader ? m_task->handle() : ShaderModuleHandle{},
                                                   .mesh_shader = m_mesh->handle(),
                                                   .color_formats = FORMATS,
                                                   .state = {.rasterization = {.cull_mode = CullMode::NONE}}});
    }

    /// @brief Records a pass over the target that binds @p graphics_pipeline and calls @p draw, then
    ///        returns the target's texels.
    template<class Draw>
    [[nodiscard]] auto render(const GraphicsPipeline& graphics_pipeline,
                              Draw&& draw) -> std::vector<std::uint32_t>
    {
        run([&](GraphicsRecorder& recorder)
        {
            const std::array to_target{image_barrier(m_target->handle(), ResourceState::UNDEFINED, ResourceState::COLOR_ATTACHMENT_WRITE)};
            recorder.barrier({.image_barriers = to_target});
            const std::array attachments{ColorAttachment{.view = m_target_view->handle()}};
            RenderingScope scope = recorder.begin_rendering(
                {.area = {.width = TARGET_EXTENT.width, .height = TARGET_EXTENT.height}, .color_attachments = attachments});
            scope.set_viewport({.width = static_cast<float>(TARGET_EXTENT.width), .height = static_cast<float>(TARGET_EXTENT.height)});
            scope.set_scissor({.width = TARGET_EXTENT.width, .height = TARGET_EXTENT.height});
            scope.bind_pipeline(graphics_pipeline.handle());
            scope.push_constants(COLORS);
            draw(scope);
        });
        return read_image(*m_target, ResourceState::COLOR_ATTACHMENT_WRITE);
    }

    /// @brief Whether @p texels show the red left half and the green right half.
    static void expect_halves(const std::vector<std::uint32_t>& texels)
    {
        EXPECT_EQ(texels[0], rgba(255, 0, 0, 255));
        EXPECT_EQ(texels[TARGET_EXTENT.width / 2 - 1], rgba(255, 0, 0, 255));
        EXPECT_EQ(texels[TARGET_EXTENT.width / 2], rgba(0, 255, 0, 255));
        EXPECT_EQ(texels.back(), rgba(0, 255, 0, 255));
    }

    void release_device_objects() override
    {
        m_target_view.reset();
        m_target.reset();
        m_task.reset();
        m_mesh.reset();
        m_fragment.reset();
    }

    static constexpr std::array FORMATS{Format::R8G8B8A8_UNORM};

    std::unique_ptr<Image> m_target;
    std::unique_ptr<ImageView> m_target_view;
    std::unique_ptr<ShaderModule> m_task;
    std::unique_ptr<ShaderModule> m_mesh;
    std::unique_ptr<ShaderModule> m_fragment;
};

TEST_F(VulkanMeshTest, MeshShadersDrawWithoutVertexInput)
{
    std::unique_ptr<GraphicsPipeline> mesh = pipeline(false);
    expect_halves(render(*mesh, [](auto& scope)
    {
        scope.draw_mesh_tasks({.group_count_x = 2});
    }));
}

TEST_F(VulkanMeshTest, TaskShadersLaunchTheMeshWorkgroups)
{
    std::unique_ptr<GraphicsPipeline> task_and_mesh = pipeline(true);
    expect_halves(render(*task_and_mesh, [](auto& scope)
    {
        scope.draw_mesh_tasks({.group_count_x = 1});
    }));
}

TEST_F(VulkanMeshTest, MeshTasksAreLaunchedIndirectly)
{
    // The records a culling pass would write, and their count.
    std::unique_ptr<GraphicsPipeline> mesh = pipeline(false);
    std::unique_ptr<Buffer> arguments = m_device->create_buffer({.size = sizeof(DrawMeshTasksArguments) + sizeof(std::uint32_t),
                                                                 .usage = BufferUsage::INDIRECT,
                                                                 .location = MemoryLocation::HOST_UPLOAD,
                                                                 .debug_name = "mesh arguments"});
    const DrawMeshTasksArguments launch{.group_count_x = 2};
    std::memcpy(arguments->mapped_memory().data(), &launch, sizeof(launch));
    const std::uint32_t one = 1;
    std::memcpy(arguments->mapped_memory().data() + sizeof(launch), &one, sizeof(one));

    expect_halves(render(*mesh, [&](auto& scope)
    {
        scope.draw_mesh_tasks_indirect({.buffer = arguments->handle()});
    }));
    if (m_device->physical_device().supports(DeviceFeature::DRAW_INDIRECT_COUNT))
    {
        expect_halves(render(*mesh, [&](auto& scope)
        {
            scope.draw_mesh_tasks_indirect_count({.buffer = arguments->handle()},
                                                 {.buffer = arguments->handle(), .offset = sizeof(launch), .maximum_draw_count = 1});
        }));
    }
}

TEST_F(VulkanDeviceTest, MeshShadersNeedTheMeshShaderFeature)
{
    // The fixture's device does not ask for the feature, whether the GPU has it or not.
    try
    {
        (void)m_device->create_shader_module(shaders::halves_mesh);
        FAIL() << "create_shader_module must throw";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("MESH_SHADER"), std::string_view::npos) << error.what();
    }
}

} // namespace
