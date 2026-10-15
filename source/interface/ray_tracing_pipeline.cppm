/// @file ray_tracing_pipeline.cppm
/// @brief RayTracingPipeline: ray generation, miss, hit and callable shaders grouped for trace_rays(),
///        and the layout of the shader binding table that selects them.
export module gfx.interface:ray_tracing_pipeline;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :shader_module;

export namespace gfx
{

/// @brief One shader group: a ray generation, miss or callable shader (GENERAL), or the hit shaders of
///        one kind of geometry.
struct RayTracingShaderGroup
{
    RayTracingShaderGroupType type = RayTracingShaderGroupType::GENERAL; ///< Kind of group.
    ShaderModuleHandle general{};      ///< GENERAL: the ray generation, miss or callable shader.
    ShaderModuleHandle closest_hit{};  ///< Hit groups: closest-hit shader (may be null).
    ShaderModuleHandle any_hit{};      ///< Hit groups: any-hit shader (may be null).
    ShaderModuleHandle intersection{}; ///< PROCEDURAL_HIT: intersection shader.
};

/// @brief How to create a RayTracingPipeline.
struct RayTracingPipelineDescription
{
    PipelineLayoutHandle layout{};                           ///< Pipeline layout (null = the bindless layout).
    std::span<const RayTracingShaderGroup> groups{};         ///< Groups; their order is the order of
                                                             ///< shader_group_handles().
    std::uint32_t maximum_recursion_depth = 1;               ///< Nested trace calls (1 = no recursion).
    std::uint32_t maximum_payload_size = 32;                 ///< Bytes of the largest ray payload.
    std::uint32_t maximum_attribute_size = 8;                ///< Bytes of the largest hit attributes.
    std::span<const SpecializationConstant> specialization_constants{}; ///< Constant values of every shader.
    PipelineCacheHandle cache{};                             ///< Cache to use (null = the device's own).
    PipelineCreationMode creation_mode = PipelineCreationMode::IMMEDIATE; ///< Compile now or in the background.
    std::string_view debug_name{};                           ///< Name shown by debugging tools.
};

/// @brief One region of a shader binding table: records of one kind in a buffer
///        (SHADER_BINDING_TABLE usage).
struct ShaderBindingTableRegion
{
    BufferHandle buffer{};    ///< Buffer holding the records (null = empty region).
    std::uint64_t offset = 0; ///< First byte (a multiple of DeviceLimits::shader_group_base_alignment).
    std::uint64_t stride = 0; ///< Bytes between records.
    std::uint64_t size = 0;   ///< Bytes of the region.
};

/// @brief The shader binding table trace_rays() uses: which group runs for ray generation, misses,
///        hits and callables.
struct ShaderBindingTable
{
    ShaderBindingTableRegion ray_generation{}; ///< One record.
    ShaderBindingTableRegion miss{};           ///< Miss records.
    ShaderBindingTableRegion hit{};            ///< Hit group records.
    ShaderBindingTableRegion callable{};       ///< Callable records.
};

/// @brief Records in each region of a shader binding table.
struct ShaderBindingTableCounts
{
    std::uint32_t miss = 1;             ///< Miss records.
    std::uint32_t hit = 1;              ///< Hit group records.
    std::uint32_t callable = 0;         ///< Callable records.
    std::uint32_t record_data_size = 0; ///< Bytes of data after each shader group handle.
};

/// @brief Where each region of a shader binding table lies in one buffer.
struct ShaderBindingTableLayout
{
    std::uint64_t stride = 0;                ///< Bytes of one record.
    std::uint64_t ray_generation_offset = 0; ///< First byte of the ray generation record.
    std::uint64_t miss_offset = 0;           ///< First byte of the miss records.
    std::uint64_t hit_offset = 0;            ///< First byte of the hit records.
    std::uint64_t callable_offset = 0;       ///< First byte of the callable records.
    std::uint64_t size = 0;                  ///< Bytes of the whole table.
    ShaderBindingTableCounts counts{};       ///< Records per region.

    /// @brief The table, once written into @p buffer at @p base_offset.
    [[nodiscard]] constexpr auto table(BufferHandle buffer, std::uint64_t base_offset = 0) const noexcept -> ShaderBindingTable
    {
        const auto region = [&](std::uint64_t offset, std::uint32_t count) -> ShaderBindingTableRegion
        {
            if (count == 0)
            {
                return {};
            }
            return {.buffer = buffer, .offset = base_offset + offset, .stride = stride, .size = stride * count};
        };
        return {.ray_generation = region(ray_generation_offset, 1),
                .miss = region(miss_offset, counts.miss),
                .hit = region(hit_offset, counts.hit),
                .callable = region(callable_offset, counts.callable)};
    }
};

/// @brief Lays a shader binding table out in one buffer: one ray generation record, then the miss, hit
///        and callable records, each region aligned to @p base_alignment and each record to
///        @p handle_alignment (values from DeviceLimits).
[[nodiscard]] constexpr auto
shader_binding_table_layout(std::uint32_t handle_size,
                            std::uint32_t handle_alignment,
                            std::uint32_t base_alignment,
                            const ShaderBindingTableCounts& counts) noexcept -> ShaderBindingTableLayout
{
    const auto align = [](std::uint64_t value, std::uint64_t alignment)
    {
        return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
    };
    ShaderBindingTableLayout layout;
    layout.counts = counts;
    layout.stride = align(std::uint64_t{handle_size} + counts.record_data_size, handle_alignment);
    layout.ray_generation_offset = 0;
    layout.miss_offset = align(layout.stride, base_alignment);
    layout.hit_offset = align(layout.miss_offset + layout.stride * counts.miss, base_alignment);
    layout.callable_offset = align(layout.hit_offset + layout.stride * counts.hit, base_alignment);
    layout.size = layout.callable_offset + layout.stride * counts.callable;
    return layout;
}

/// @brief Ray tracing shaders grouped for trace_rays(); a peer of ComputePipeline.
class RayTracingPipeline : public BackendObject
{
public:
    /// @brief Handle naming this pipeline when binding it.
    [[nodiscard]] virtual auto handle() const noexcept -> RayTracingPipelineHandle = 0;

    /// @brief Pipeline layout.
    [[nodiscard]] virtual auto layout() const noexcept -> PipelineLayoutHandle = 0;

    /// @brief Whether it can be bound (see PipelineCreationMode::ASYNCHRONOUS).
    [[nodiscard]] virtual auto status() const noexcept -> PipelineStatus = 0;

    /// @brief Blocks until compiled.
    /// @throws The compilation error when it failed.
    virtual void wait() const = 0;

    /// @brief Identifiers of groups [first, first + count), DeviceLimits::shader_group_handle_size bytes
    ///        each, to copy into shader binding table records.
    [[nodiscard]] virtual auto shader_group_handles(std::uint32_t first, std::uint32_t count) const -> std::vector<std::byte> = 0;
};

void
RayTracingPipeline::wait() const
{
    throw_not_implemented("RayTracingPipeline::wait");
}

auto
RayTracingPipeline::shader_group_handles(std::uint32_t /*first*/,
                                         std::uint32_t /*count*/) const -> std::vector<std::byte>
{
    throw_not_implemented("RayTracingPipeline::shader_group_handles");
}

} // namespace gfx
