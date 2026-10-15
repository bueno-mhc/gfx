// Pipeline creation checks run by backends: graphics stage combinations (vertex, tessellation, geometry,
// task, mesh), the interface between stages, specialization constants, ray tracing groups; and the
// ray tracing data layouts (instance records, shader binding tables).
#include <gtest/gtest.h>

import std;
import gfx;
import gfx.tests.fake_backend;

namespace
{

using namespace gfx;
using gfx::tests::FakeDevice;
using gfx::tests::FakePhysicalDevice;

inline constexpr std::array<std::uint32_t, 1> FAKE_SPIRV{SPIRV_MAGIC_NUMBER};

auto stage_layout(ShaderStage stage) -> ShaderLayout
{
    return {.stage = stage};
}

/// A fake device whose "reflection" knows shaders of every stage.
class PipelineValidationTest : public ::testing::Test
{
protected:
    PipelineValidationTest()
    {
        ShaderLayout vertex = stage_layout(ShaderStage::VERTEX);
        vertex.outputs = {{.location = 0, .format = Format::R32G32_SFLOAT, .name = "v_uv"}};
        vertex.specialization_constants = {{.id = 3, .size = 4, .name = "USE_FOG"}};
        ShaderLayout fragment = stage_layout(ShaderStage::FRAGMENT);
        fragment.inputs = {{.location = 0, .format = Format::R32G32_SFLOAT, .name = "v_uv"}};
        fragment.outputs = {{.location = 0, .format = Format::R32G32B32A32_SFLOAT, .name = "o_color"}};
        ShaderLayout integer_fragment = fragment;
        integer_fragment.inputs[0].format = Format::R32G32_UINT;
        ShaderLayout mesh = stage_layout(ShaderStage::MESH);
        mesh.outputs = vertex.outputs;

        m_device.add_reflected_layout("vertex", vertex);
        m_device.add_reflected_layout("fragment", fragment);
        m_device.add_reflected_layout("integer fragment", integer_fragment);
        m_device.add_reflected_layout("task", stage_layout(ShaderStage::TASK));
        m_device.add_reflected_layout("mesh", mesh);
        m_device.add_reflected_layout("geometry", stage_layout(ShaderStage::GEOMETRY));
        m_device.add_reflected_layout("ray generation", stage_layout(ShaderStage::RAY_GENERATION));
        m_device.add_reflected_layout("miss", stage_layout(ShaderStage::MISS));
        m_device.add_reflected_layout("closest hit", stage_layout(ShaderStage::CLOSEST_HIT));
        m_device.add_reflected_layout("intersection", stage_layout(ShaderStage::INTERSECTION));
    }

    auto shader(std::string_view name, ShaderStage stage) -> ShaderModuleHandle
    {
        m_modules.push_back(device().create_shader_module({.stage = stage, .spirv = FAKE_SPIRV, .name = name}));
        return m_modules.back()->handle();
    }

    auto device() -> Device&
    {
        return m_device;
    }

    /// A graphics pipeline description rendering one float color attachment with the bindless layout.
    [[nodiscard]] auto graphics() const -> GraphicsPipelineDescription
    {
        return {.color_formats = COLOR_FORMATS};
    }

protected:
    static constexpr std::array<Format, 1> COLOR_FORMATS{Format::B8G8R8A8_SRGB};
    FakePhysicalDevice m_physical_device{PhysicalDeviceType::DISCRETE_GPU};
    FakeDevice m_device{m_physical_device};
    std::vector<std::unique_ptr<ShaderModule>> m_modules;
};

TEST_F(PipelineValidationTest, VertexAndMeshPipelinesAreAccepted)
{
    GraphicsPipelineDescription vertex_pipeline = graphics();
    vertex_pipeline.vertex_shader = shader("vertex", ShaderStage::VERTEX);
    vertex_pipeline.fragment_shader = shader("fragment", ShaderStage::FRAGMENT);
    EXPECT_NO_THROW((void)device().create_graphics_pipeline(vertex_pipeline));

    GraphicsPipelineDescription mesh_pipeline = graphics();
    mesh_pipeline.task_shader = shader("task", ShaderStage::TASK);
    mesh_pipeline.mesh_shader = shader("mesh", ShaderStage::MESH);
    mesh_pipeline.fragment_shader = shader("fragment", ShaderStage::FRAGMENT);
    EXPECT_NO_THROW((void)device().create_graphics_pipeline(mesh_pipeline)) << "mesh output feeds the fragment input";
}

TEST_F(PipelineValidationTest, StageCombinationsMustFormAPipeline)
{
    GraphicsPipelineDescription nothing = graphics();
    try
    {
        (void)device().create_graphics_pipeline(nothing);
        FAIL() << "InvalidUsageError was not thrown";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_EQ(error.problem(), "the pipeline has neither a vertex nor a mesh shader");
    }

    GraphicsPipelineDescription both = graphics();
    both.vertex_shader = shader("vertex", ShaderStage::VERTEX);
    both.mesh_shader = shader("mesh", ShaderStage::MESH);
    EXPECT_THROW((void)device().create_graphics_pipeline(both), InvalidUsageError);

    GraphicsPipelineDescription lonely_task = graphics();
    lonely_task.vertex_shader = shader("vertex", ShaderStage::VERTEX);
    lonely_task.task_shader = shader("task", ShaderStage::TASK);
    EXPECT_THROW((void)device().create_graphics_pipeline(lonely_task), InvalidUsageError);

    GraphicsPipelineDescription mesh_with_geometry = graphics();
    mesh_with_geometry.mesh_shader = shader("mesh", ShaderStage::MESH);
    mesh_with_geometry.geometry_shader = shader("geometry", ShaderStage::GEOMETRY);
    EXPECT_THROW((void)device().create_graphics_pipeline(mesh_with_geometry), InvalidUsageError);

    GraphicsPipelineDescription swapped = graphics();
    swapped.vertex_shader = shader("fragment", ShaderStage::FRAGMENT);
    try
    {
        (void)device().create_graphics_pipeline(swapped);
        FAIL() << "ShaderLayoutMismatchError was not thrown";
    }
    catch (const ShaderLayoutMismatchError& error)
    {
        EXPECT_EQ(error.mismatch(), LayoutMismatch::WRONG_SHADER_STAGE);
    }
}

TEST_F(PipelineValidationTest, TheFragmentShaderReadsWhatThePreviousStageWrites)
{
    GraphicsPipelineDescription description = graphics();
    description.vertex_shader = shader("vertex", ShaderStage::VERTEX);
    description.fragment_shader = shader("integer fragment", ShaderStage::FRAGMENT);
    try
    {
        (void)device().create_graphics_pipeline(description);
        FAIL() << "ShaderLayoutMismatchError was not thrown";
    }
    catch (const ShaderLayoutMismatchError& error)
    {
        EXPECT_EQ(error.mismatch(), LayoutMismatch::STAGE_INTERFACE_MISMATCH);
        EXPECT_EQ(error.problem(), "the FRAGMENT stage reads location 0 as R32G32_UINT, but the VERTEX stage writes R32G32_SFLOAT there");
    }
}

TEST_F(PipelineValidationTest, SpecializationConstantsMustBeDeclared)
{
    GraphicsPipelineDescription description = graphics();
    description.vertex_shader = shader("vertex", ShaderStage::VERTEX);
    description.fragment_shader = shader("fragment", ShaderStage::FRAGMENT);
    const std::array declared{specialization_constant(3, true)};
    description.specialization_constants = declared;
    EXPECT_NO_THROW((void)device().create_graphics_pipeline(description));

    const std::array unknown{specialization_constant(9, 2.5F)};
    description.specialization_constants = unknown;
    try
    {
        (void)device().create_graphics_pipeline(description);
        FAIL() << "ShaderLayoutMismatchError was not thrown";
    }
    catch (const ShaderLayoutMismatchError& error)
    {
        EXPECT_EQ(error.mismatch(), LayoutMismatch::UNKNOWN_SPECIALIZATION_CONSTANT);
        EXPECT_EQ(error.problem(), "the pipeline sets specialization constant 9, which none of its shaders declares");
    }
    static_assert(specialization_constant(1, 1.0F).value == 0x3F800000U && specialization_constant(1, true).value == 1U);
}

TEST_F(PipelineValidationTest, CreationChecksCanBeTurnedOff)
{
    m_device.use_validation_level(ValidationLevel::NONE);
    GraphicsPipelineDescription nothing = graphics();
    EXPECT_NO_THROW((void)device().create_graphics_pipeline(nothing)) << "ValidationLevel::NONE trusts the caller";
}

TEST_F(PipelineValidationTest, RayTracingGroupsAreChecked)
{
    // A valid pipeline reaches the backend (which does not build ray tracing pipelines).
    const std::array valid{
        RayTracingShaderGroup{.type = RayTracingShaderGroupType::GENERAL, .general = shader("ray generation", ShaderStage::RAY_GENERATION)},
        RayTracingShaderGroup{.type = RayTracingShaderGroupType::GENERAL, .general = shader("miss", ShaderStage::MISS)},
        RayTracingShaderGroup{.type = RayTracingShaderGroupType::TRIANGLES_HIT,
                              .closest_hit = shader("closest hit", ShaderStage::CLOSEST_HIT)},
        RayTracingShaderGroup{.type = RayTracingShaderGroupType::PROCEDURAL_HIT,
                              .intersection = shader("intersection", ShaderStage::INTERSECTION)},
    };
    EXPECT_THROW((void)device().create_ray_tracing_pipeline({.groups = valid}), BackendCapabilityError);

    // A closest-hit shader in a GENERAL group.
    const std::array misplaced{RayTracingShaderGroup{.type = RayTracingShaderGroupType::GENERAL,
                                                     .general = shader("closest hit", ShaderStage::CLOSEST_HIT)}};
    try
    {
        (void)device().create_ray_tracing_pipeline({.groups = misplaced});
        FAIL() << "InvalidUsageError was not thrown";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_EQ(error.problem(), "group 0: its general shader 'closest hit' is a CLOSEST_HIT shader");
    }

    // No ray generation, and a procedural group without an intersection shader.
    const std::array only_miss{RayTracingShaderGroup{.type = RayTracingShaderGroupType::GENERAL, .general = shader("miss", ShaderStage::MISS)}};
    EXPECT_THROW((void)device().create_ray_tracing_pipeline({.groups = only_miss}), InvalidUsageError);
    const std::array no_intersection{
        RayTracingShaderGroup{.type = RayTracingShaderGroupType::GENERAL, .general = shader("ray generation", ShaderStage::RAY_GENERATION)},
        RayTracingShaderGroup{.type = RayTracingShaderGroupType::PROCEDURAL_HIT}};
    EXPECT_THROW((void)device().create_ray_tracing_pipeline({.groups = no_intersection}), InvalidUsageError);
}

TEST(RayTracingDataTest, InstanceRecordsMatchTheNativeLayout)
{
    static_assert(sizeof(AccelerationStructureInstance) == ACCELERATION_STRUCTURE_INSTANCE_SIZE);
    const AccelerationStructureInstance instance =
        make_instance(0xABCD00, {1, 0, 0, 5, 0, 1, 0, 6, 0, 0, 1, 7}, 0x1234567, 0x0F, 3,
                      AccelerationStructureInstanceOption::FORCE_OPAQUE_GEOMETRY);
    EXPECT_EQ(instance.custom_index, 0x234567U) << "24 bits";
    EXPECT_EQ(instance.mask, 0x0FU);
    EXPECT_EQ(instance.shader_binding_table_offset, 3U);
    EXPECT_EQ(instance.options, 4U);
    EXPECT_EQ(instance.transform[3], 5.0F);

    // The second word packs the custom index in its low 24 bits and the mask in the high 8.
    std::array<std::uint32_t, 16> words{};
    std::memcpy(words.data(), &instance, sizeof(instance));
    EXPECT_EQ(words[12], 0x0F234567U);
    EXPECT_EQ(words[13], 0x04000003U);
}

TEST(RayTracingDataTest, ShaderBindingTablesAreLaidOutWithTheDeviceAlignments)
{
    // 32-byte handles aligned to 32, regions aligned to 64, 8 bytes of data per record.
    constexpr ShaderBindingTableLayout layout =
        shader_binding_table_layout(32, 32, 64, {.miss = 2, .hit = 3, .callable = 0, .record_data_size = 8});
    static_assert(layout.stride == 64);
    static_assert(layout.ray_generation_offset == 0 && layout.miss_offset == 64 && layout.hit_offset == 192);
    static_assert(layout.size == 192 + 3 * 64);

    constexpr BufferHandle buffer{};
    constexpr ShaderBindingTable table = layout.table(buffer, 1024);
    static_assert(table.ray_generation.offset == 1024 && table.ray_generation.size == table.ray_generation.stride);
    static_assert(table.hit.offset == 1024 + 192 && table.hit.size == 3 * 64);
    static_assert(table.callable.size == 0, "empty regions stay empty");
    SUCCEED();
}

} // namespace
