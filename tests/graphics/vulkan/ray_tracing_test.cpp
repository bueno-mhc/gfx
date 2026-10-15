#include "vulkan_test.hpp"

import gfx.tests.vulkan_shaders;

namespace
{

using namespace gfx;
using gfx::tests::VulkanDeviceTest;
namespace shaders = gfx::tests::vulkan_shaders;

/// @brief Push constants of trace.rgen and query.comp.
struct TracePush
{
    std::uint32_t structure = 0;
    std::uint32_t results = 0;
};

/// @brief Waits for acceleration structure builds before later builds and ray traversal.
constexpr GlobalMemoryBarrier AFTER_BUILD{
    .source_stages = PipelineStage::ACCELERATION_STRUCTURE_BUILD,
    .source_access = MemoryAccess::ACCELERATION_STRUCTURE_WRITE,
    .destination_stages = PipelineStage::ACCELERATION_STRUCTURE_BUILD | PipelineStage::RAY_TRACING_SHADER | PipelineStage::COMPUTE_SHADER,
    .destination_access = MemoryAccess::ACCELERATION_STRUCTURE_READ | MemoryAccess::ACCELERATION_STRUCTURE_WRITE,
};

/// @brief A device with ray tracing pipelines and ray queries, and one triangle in a two-level scene.
class VulkanRayTracingTest : public VulkanDeviceTest
{
protected:
    [[nodiscard]] auto required_features() const -> std::vector<DeviceFeature> override
    {
        return {DeviceFeature::RAY_TRACING, DeviceFeature::RAY_QUERY};
    }

    /// @brief Builds the bottom level (one triangle around the origin, in the z = 0 plane) and the top
    ///        level (one instance of it).
    void build_scene(AccelerationStructureBuildOption bottom_options = AccelerationStructureBuildOption::PREFER_FAST_TRACE)
    {
        // The triangle's vertices, read by the build.
        const std::array<float, 9> vertices{-0.5F, -0.5F, 0.0F, 0.5F, -0.5F, 0.0F, 0.0F, 0.5F, 0.0F};
        m_vertices = m_device->create_buffer({.size = sizeof(vertices),
                                              .usage = BufferUsage::ACCELERATION_STRUCTURE_BUILD_INPUT | BufferUsage::SHADER_DEVICE_ADDRESS,
                                              .location = MemoryLocation::HOST_UPLOAD,
                                              .debug_name = "triangle"});
        std::memcpy(m_vertices->mapped_memory().data(), vertices.data(), sizeof(vertices));
        const std::array triangles{TriangleGeometry{.vertex_buffer = m_vertices->handle(), .vertex_count = 3,
                                                    .options = GeometryOption::OPAQUE_GEOMETRY}};
        m_bottom_build = {.type = AccelerationStructureType::BOTTOM_LEVEL, .options = bottom_options, .triangles = triangles};
        const AccelerationStructureBuildSizes bottom_sizes = m_device->acceleration_structure_build_sizes(m_bottom_build);
        m_bottom_storage = m_device->create_buffer({.size = bottom_sizes.structure_size, .usage = BufferUsage::ACCELERATION_STRUCTURE_STORAGE,
                                                    .debug_name = "bottom storage"});
        m_bottom = m_device->create_acceleration_structure({.type = AccelerationStructureType::BOTTOM_LEVEL,
                                                            .buffer = m_bottom_storage->handle(),
                                                            .size = bottom_sizes.structure_size,
                                                            .debug_name = "bottom"});

        // One instance of it, at the identity transform.
        const AccelerationStructureInstance instance = make_instance(m_bottom->device_address(),
                                                                     {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F});
        m_instances = m_device->create_buffer({.size = sizeof(instance),
                                               .usage = BufferUsage::ACCELERATION_STRUCTURE_BUILD_INPUT | BufferUsage::SHADER_DEVICE_ADDRESS,
                                               .location = MemoryLocation::HOST_UPLOAD,
                                               .debug_name = "instances"});
        std::memcpy(m_instances->mapped_memory().data(), &instance, sizeof(instance));
        m_top_build = {.type = AccelerationStructureType::TOP_LEVEL, .instances = {.buffer = m_instances->handle(), .count = 1}};
        const AccelerationStructureBuildSizes top_sizes = m_device->acceleration_structure_build_sizes(m_top_build);
        m_top_storage = m_device->create_buffer({.size = top_sizes.structure_size, .usage = BufferUsage::ACCELERATION_STRUCTURE_STORAGE,
                                                 .debug_name = "top storage"});
        m_top = m_device->create_acceleration_structure({.type = AccelerationStructureType::TOP_LEVEL,
                                                         .buffer = m_top_storage->handle(),
                                                         .size = top_sizes.structure_size,
                                                         .debug_name = "top"});

        // One scratch buffer for both builds, one after the other.
        m_scratch = m_device->create_buffer({.size = std::max(bottom_sizes.build_scratch_size, top_sizes.build_scratch_size),
                                             .usage = BufferUsage::STORAGE | BufferUsage::SHADER_DEVICE_ADDRESS,
                                             .bindless = false,
                                             .debug_name = "scratch"});
        m_bottom_build.destination = m_bottom->handle();
        m_bottom_build.scratch_buffer = m_scratch->handle();
        m_top_build.destination = m_top->handle();
        m_top_build.scratch_buffer = m_scratch->handle();
        run([&](GraphicsRecorder& recorder)
        {
            recorder.build_acceleration_structures(std::span{&m_bottom_build, 1});
            const std::array barrier{AFTER_BUILD};
            recorder.barrier({.memory_barriers = barrier});
            recorder.build_acceleration_structures(std::span{&m_top_build, 1});
            recorder.barrier({.memory_barriers = barrier});
        });
    }

    /// @brief The 4x4 grid of results: 1 where a ray hit the triangle, 2 where it missed.
    static void expect_triangle(const std::vector<std::uint32_t>& results)
    {
        ASSERT_EQ(results.size(), 16U);
        EXPECT_EQ(results[1 * 4 + 1], 1U);
        EXPECT_EQ(results[1 * 4 + 2], 1U);
        EXPECT_EQ(results[0], 2U);
        EXPECT_EQ(results[3 * 4 + 3], 2U);
        EXPECT_EQ(results[2 * 4 + 1], 2U);
    }

    void release_device_objects() override
    {
        m_top.reset();
        m_bottom.reset();
        m_top_storage.reset();
        m_bottom_storage.reset();
        m_scratch.reset();
        m_instances.reset();
        m_vertices.reset();
    }

    std::unique_ptr<Buffer> m_vertices;
    std::unique_ptr<Buffer> m_instances;
    std::unique_ptr<Buffer> m_bottom_storage;
    std::unique_ptr<Buffer> m_top_storage;
    std::unique_ptr<Buffer> m_scratch;
    std::unique_ptr<AccelerationStructure> m_bottom;
    std::unique_ptr<AccelerationStructure> m_top;
    AccelerationStructureBuildDescription m_bottom_build;
    AccelerationStructureBuildDescription m_top_build;
};

TEST_F(VulkanRayTracingTest, AccelerationStructuresHaveAddressesAndBindlessIndices)
{
    build_scene();
    EXPECT_NE(m_bottom->device_address(), 0U);
    EXPECT_EQ(m_bottom->type(), AccelerationStructureType::BOTTOM_LEVEL);
    EXPECT_FALSE(m_bottom->bindless_index().is_valid()) << "only top levels are registered";
    EXPECT_TRUE(m_top->bindless_index().is_valid());
    EXPECT_EQ(m_device->bindless_usage().used_by(BindlessResourceKind::ACCELERATION_STRUCTURE), 1U);
}

TEST_F(VulkanRayTracingTest, RayTracingPipelinesTraceRaysThroughTheShaderBindingTable)
{
    build_scene();
    std::unique_ptr<ShaderModule> generation = m_device->create_shader_module(shaders::trace_rgen);
    std::unique_ptr<ShaderModule> miss = m_device->create_shader_module(shaders::trace_rmiss);
    std::unique_ptr<ShaderModule> closest_hit = m_device->create_shader_module(shaders::trace_rchit);
    const std::array groups{
        RayTracingShaderGroup{.type = RayTracingShaderGroupType::GENERAL, .general = generation->handle()},
        RayTracingShaderGroup{.type = RayTracingShaderGroupType::GENERAL, .general = miss->handle()},
        RayTracingShaderGroup{.type = RayTracingShaderGroupType::TRIANGLES_HIT, .closest_hit = closest_hit->handle()},
    };
    std::unique_ptr<RayTracingPipeline> pipeline = m_device->create_ray_tracing_pipeline({.groups = groups, .debug_name = "trace"});
    EXPECT_EQ(pipeline->status(), PipelineStatus::READY);

    // The table: one record per region, each holding its group's handle.
    const DeviceLimits& limits = m_device->physical_device().properties().limits;
    const ShaderBindingTableLayout layout = shader_binding_table_layout(limits.shader_group_handle_size, limits.shader_group_handle_alignment,
                                                                        limits.shader_group_base_alignment, {.miss = 1, .hit = 1});
    const std::vector<std::byte> handles = pipeline->shader_group_handles(0, 3);
    ASSERT_EQ(handles.size(), 3U * limits.shader_group_handle_size);
    std::unique_ptr<Buffer> table = m_device->create_buffer({.size = layout.size,
                                                             .usage = BufferUsage::SHADER_BINDING_TABLE | BufferUsage::SHADER_DEVICE_ADDRESS,
                                                             .location = MemoryLocation::HOST_UPLOAD,
                                                             .debug_name = "shader binding table"});
    const std::span<std::byte> records = table->mapped_memory();
    std::memcpy(records.data() + layout.ray_generation_offset, handles.data(), limits.shader_group_handle_size);
    std::memcpy(records.data() + layout.miss_offset, handles.data() + limits.shader_group_handle_size, limits.shader_group_handle_size);
    std::memcpy(records.data() + layout.hit_offset, handles.data() + 2U * limits.shader_group_handle_size, limits.shader_group_handle_size);

    std::unique_ptr<Buffer> results = m_device->create_buffer(
        {.size = 16 * sizeof(std::uint32_t), .usage = BufferUsage::STORAGE | BufferUsage::TRANSFER_SOURCE, .debug_name = "results"});
    run([&](GraphicsRecorder& recorder)
    {
        recorder.bind_pipeline(pipeline->handle());
        recorder.push_constants(TracePush{.structure = m_top->bindless_index().value(), .results = results->bindless_index().value()});
        recorder.trace_rays(layout.table(table->handle()), 4, 4);
    });
    expect_triangle(read_buffer<std::uint32_t>(*results, 16, ResourceState::SHADER_STORAGE_READ_WRITE));
}

TEST_F(VulkanRayTracingTest, ComputeShadersQueryRays)
{
    build_scene();
    std::unique_ptr<ShaderModule> query = m_device->create_shader_module(shaders::query_comp);
    std::unique_ptr<ComputePipeline> pipeline = m_device->create_compute_pipeline({.shader = query->handle()});
    std::unique_ptr<Buffer> results = m_device->create_buffer(
        {.size = 16 * sizeof(std::uint32_t), .usage = BufferUsage::STORAGE | BufferUsage::TRANSFER_SOURCE, .debug_name = "results"});
    run<QueueType::COMPUTE>([&](ComputeRecorder& recorder)
    {
        recorder.bind_pipeline(pipeline->handle());
        recorder.push_constants(TracePush{.structure = m_top->bindless_index().value(), .results = results->bindless_index().value()});
        recorder.dispatch(1);
    });
    expect_triangle(read_buffer<std::uint32_t>(*results, 16, ResourceState::COMPUTE_SHADER_STORAGE_WRITE));
}

TEST_F(VulkanRayTracingTest, BottomLevelsAreCompacted)
{
    build_scene(AccelerationStructureBuildOption::PREFER_FAST_TRACE | AccelerationStructureBuildOption::ALLOW_COMPACTION);
    std::unique_ptr<QueryPool> sizes = m_device->create_query_pool({.type = QueryType::ACCELERATION_STRUCTURE_COMPACTED_SIZE, .count = 1});
    run([&](GraphicsRecorder& recorder)
    {
        const std::array structures{m_bottom->handle()};
        recorder.write_acceleration_structure_properties(structures, sizes->handle(), 0);
    });
    std::array<std::uint64_t, 1> compacted{};
    ASSERT_TRUE(sizes->results(0, compacted, true));
    EXPECT_GT(compacted[0], 0U);
    EXPECT_LE(compacted[0], m_bottom->size());

    // The compacted copy, in storage of its own size.
    std::unique_ptr<Buffer> storage = m_device->create_buffer({.size = compacted[0], .usage = BufferUsage::ACCELERATION_STRUCTURE_STORAGE});
    std::unique_ptr<AccelerationStructure> compact = m_device->create_acceleration_structure(
        {.type = AccelerationStructureType::BOTTOM_LEVEL, .buffer = storage->handle(), .size = compacted[0], .debug_name = "compact"});
    run([&](GraphicsRecorder& recorder)
    {
        recorder.copy_acceleration_structure(m_bottom->handle(), compact->handle(), AccelerationStructureCopyMode::COMPACT);
    });
    EXPECT_NE(compact->device_address(), 0U);
}

TEST_F(VulkanDeviceTest, RayTracingNeedsItsFeature)
{
    if (m_device->physical_device().supports(DeviceFeature::RAY_TRACING))
    {
        EXPECT_THROW((void)m_device->create_ray_tracing_pipeline({}), BackendCapabilityError) << "the test device does not enable it";
    }
    EXPECT_THROW((void)m_device->create_acceleration_structure({}), BackendCapabilityError);
}

} // namespace
