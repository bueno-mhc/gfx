/// @file vulkan_commands.cpp
/// @brief Command pools and command buffers: every recording primitive of the interface, translated to
///        Vulkan (synchronization2 barriers, dynamic rendering with parallel parts, bindless binding at
///        the start of every command buffer). Nothing outside this unit needs these classes, so they are
///        declared here.
module;

#include <volk.h>

module gfx.backends.vulkan;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import gfx.interface;
import :common;
import :translation;
import :instance;
import :device;
import :pipelines;

namespace gfx::vulkan
{

namespace
{

constexpr VkPipelineStageFlags2 SHADER_STAGES =
    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
    VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT |
    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
    VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR |
    VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT;

constexpr VkPipelineStageFlags2 TRANSFER_STAGES = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT |
                                                  VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT |
                                                  VK_PIPELINE_STAGE_2_CLEAR_BIT;

constexpr VkPipelineStageFlags2 GRAPHICS_STAGES =
    VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
    VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT |
    VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
    VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
    VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT;

/// @brief Stages a queue with @p capabilities can synchronize.
constexpr auto
queue_stages(QueueCapability capabilities) noexcept -> VkPipelineStageFlags2
{
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT | TRANSFER_STAGES;
    if (has_any(capabilities, QueueCapability::COMPUTE))
    {
        stages |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                  VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    }
    if (has_any(capabilities, QueueCapability::GRAPHICS))
    {
        stages |= GRAPHICS_STAGES | VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
    }
    return stages;
}

/// @brief The accesses of @p access that some stage of @p stages can perform (synchronization2 rejects
///        an access without a stage that performs it, e.g. a color attachment write on a compute queue).
constexpr auto
accesses_of_stages(VkAccessFlags2 access,
                   VkPipelineStageFlags2 stages) noexcept -> VkAccessFlags2
{
    if ((stages & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT) != 0)
    {
        return access;
    }
    if ((stages & VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT) != 0)
    {
        stages |= GRAPHICS_STAGES;
    }
    if ((stages & VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT) != 0)
    {
        stages |= TRANSFER_STAGES;
    }
    constexpr std::array<std::pair<VkAccessFlags2, VkPipelineStageFlags2>, 21> PERFORMED_BY{{
        {VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR},
        {VK_ACCESS_2_INDEX_READ_BIT, VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT},
        {VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT},
        {VK_ACCESS_2_UNIFORM_READ_BIT, SHADER_STAGES},
        {VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT},
        {VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, SHADER_STAGES},
        {VK_ACCESS_2_SHADER_STORAGE_READ_BIT, SHADER_STAGES | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR},
        {VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, SHADER_STAGES},
        {VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
        {VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
        {VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT},
        {VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT},
        {VK_ACCESS_2_TRANSFER_READ_BIT, TRANSFER_STAGES | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR},
        {VK_ACCESS_2_TRANSFER_WRITE_BIT, TRANSFER_STAGES | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR},
        {VK_ACCESS_2_HOST_READ_BIT, VK_PIPELINE_STAGE_2_HOST_BIT},
        {VK_ACCESS_2_HOST_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT},
        {VK_ACCESS_2_MEMORY_READ_BIT, ~VkPipelineStageFlags2{0}},
        {VK_ACCESS_2_MEMORY_WRITE_BIT, ~VkPipelineStageFlags2{0}},
        {VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR, SHADER_STAGES | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR},
        {VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR},
        {VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR, VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR},
    }};
    VkAccessFlags2 kept = 0;
    for (const auto& [bit, performers] : PERFORMED_BY)
    {
        if ((access & bit) != 0 && (stages & performers) != 0)
        {
            kept |= bit;
        }
    }
    return kept;
}

/// @brief A clear color for an image of @p format: float for normalized and float formats, integers
///        for integer formats.
auto
clear_color(Format format,
            const std::array<float, 4>& color) noexcept -> VkClearColorValue
{
    VkClearColorValue value{};
    switch (format_properties(format).numeric_type)
    {
        case FormatNumericType::UNSIGNED_INTEGER:
            for (std::size_t index = 0; index < 4; ++index)
            {
                value.uint32[index] = static_cast<std::uint32_t>(color[index]);
            }
            break;
        case FormatNumericType::SIGNED_INTEGER:
            for (std::size_t index = 0; index < 4; ++index)
            {
                value.int32[index] = static_cast<std::int32_t>(color[index]);
            }
            break;
        default:
            for (std::size_t index = 0; index < 4; ++index)
            {
                value.float32[index] = color[index];
            }
            break;
    }
    return value;
}

/// @brief @p storage emptied, with room for @p count elements. Storage reused by every command grows only
///        when a command is larger than every command before: recording allocates nothing once it has grown.
template<class Element>
auto
reuse(std::vector<Element>& storage,
      std::size_t count) -> std::vector<Element>&
{
    storage.clear();
    storage.reserve(count);
    return storage;
}

} // namespace

// ---------------------------------------------------------------------------------------------------
// Command pools
// ---------------------------------------------------------------------------------------------------

/// @brief The native pool, shared with the command buffers allocated from it: it is destroyed (through
///        the deleter) only once the pool object and every command buffer are gone.
struct NativeCommandPool
{
    VulkanDevice& device;
    VkCommandPool pool = VK_NULL_HANDLE;

    NativeCommandPool(VulkanDevice& owner, VkCommandPool native) noexcept
        : device{owner}
        , pool{native}
    {}

    NativeCommandPool(const NativeCommandPool&) = delete;
    auto operator=(const NativeCommandPool&) -> NativeCommandPool& = delete;

    ~NativeCommandPool()
    {
        device.vulkan_deleter().enqueue([device = &device, pool = pool]
        {
            device->functions().vkDestroyCommandPool(device->native(), pool, nullptr);
        });
    }
};

class VulkanCommandBuffer;

class VulkanCommandPool final : public CommandPool
{
public:
    VulkanCommandPool(VulkanDevice& device, const CommandPoolDescription& description);
    ~VulkanCommandPool() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }

    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }

    void set_debug_name(std::string_view name) override
    {
        m_debug_name = name;
        m_device.name_object(VK_OBJECT_TYPE_COMMAND_POOL, m_native->pool, m_debug_name);
    }

    [[nodiscard]] auto handle() const noexcept -> CommandPoolHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto queue_type() const noexcept -> QueueType override
    {
        return m_queue_type;
    }

    [[nodiscard]] auto allocate(CommandBufferLevel level, std::string_view debug_name) -> std::unique_ptr<CommandBuffer> override;
    void reset() override;

private:
    VulkanDevice& m_device;
    std::shared_ptr<NativeCommandPool> m_native;
    QueueType m_queue_type;
    CommandPoolOption m_options;
    CommandPoolHandle m_handle;
    std::string m_debug_name;
};

// ---------------------------------------------------------------------------------------------------
// Command buffers
// ---------------------------------------------------------------------------------------------------

class VulkanCommandBuffer final : public CommandBuffer
{
public:
    VulkanCommandBuffer(VulkanDevice& device,
                        std::shared_ptr<NativeCommandPool> pool,
                        VkCommandBuffer command_buffer,
                        QueueType queue_type,
                        CommandBufferLevel level,
                        bool resettable,
                        std::string_view debug_name);
    ~VulkanCommandBuffer() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }

    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }

    void set_debug_name(std::string_view name) override
    {
        m_debug_name = name;
        m_device.name_object(VK_OBJECT_TYPE_COMMAND_BUFFER, m_command_buffer, m_debug_name);
    }

    [[nodiscard]] auto handle() const noexcept -> CommandBufferHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto queue_type() const noexcept -> QueueType override
    {
        return m_queue_type;
    }

    [[nodiscard]] auto level() const noexcept -> CommandBufferLevel override
    {
        return m_level;
    }

    void reset() override;

    [[nodiscard]] auto bindless_pipeline_layout() const noexcept -> const PipelineLayout& override
    {
        return m_device.bindless_pipeline_layout();
    }

protected:
    void record_begin() override;
    void record_begin_rendering_part(const RenderingInheritance& inheritance) override;
    void record_end() noexcept override;
    void record_barrier(const BarrierBatch& barriers) override;
    void record_copy_buffer(BufferHandle source, BufferHandle destination, std::span<const BufferCopyRegion> regions) override;
    void record_copy_buffer_to_image(BufferHandle source,
                                     ImageHandle destination,
                                     ImageLayout destination_layout,
                                     std::span<const BufferImageCopyRegion> regions) override;
    void record_copy_image_to_buffer(ImageHandle source,
                                     ImageLayout source_layout,
                                     BufferHandle destination,
                                     std::span<const BufferImageCopyRegion> regions) override;
    void record_copy_image(ImageHandle source,
                           ImageLayout source_layout,
                           ImageHandle destination,
                           ImageLayout destination_layout,
                           std::span<const ImageCopyRegion> regions) override;
    void record_fill_buffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value) override;
    void record_update_buffer(BufferHandle buffer, std::uint64_t offset, std::span<const std::byte> data) override;
    void record_clear_color_image(ImageHandle image,
                                  ImageLayout layout,
                                  const std::array<float, 4>& color,
                                  const ImageSubresourceRange& range) override;
    void record_bind_compute_pipeline(ComputePipelineHandle pipeline) override;
    void record_bind_graphics_pipeline(GraphicsPipelineHandle pipeline) override;
    void record_bind_descriptor_sets(PipelineBindPoint bind_point,
                                     PipelineLayoutHandle layout,
                                     std::uint32_t first_set,
                                     std::span<const DescriptorSetHandle> sets,
                                     std::span<const std::uint32_t> dynamic_offsets) override;
    void record_push_constants(PipelineLayoutHandle layout,
                               ShaderStage stages,
                               std::uint32_t offset,
                               std::span<const std::byte> data) override;
    void record_dispatch(std::uint32_t group_count_x, std::uint32_t group_count_y, std::uint32_t group_count_z) override;
    void record_dispatch_indirect(BufferHandle buffer, std::uint64_t offset) override;
    void record_set_event(EventHandle event, const BarrierBatch& barriers) override;
    void record_wait_events(std::span<const EventHandle> events, const BarrierBatch& barriers) override;
    void record_reset_event(EventHandle event, PipelineStage stages) override;
    void record_begin_rendering(const RenderingDescription& rendering) override;
    [[nodiscard]] auto record_begin_parallel_rendering(const RenderingDescription& rendering) -> RenderingInheritance override;
    void record_end_rendering() noexcept override;
    void record_set_viewport(const Viewport& viewport) override;
    void record_set_scissor(const Rectangle& scissor) override;
    void record_bind_vertex_buffers(std::uint32_t first_binding, std::span<const VertexBufferBinding> buffers) override;
    void record_bind_index_buffer(BufferHandle buffer, std::uint64_t offset, IndexType type) override;
    void record_draw(const DrawArguments& arguments) override;
    void record_draw_indexed(const DrawIndexedArguments& arguments) override;
    void record_draw_indirect(const IndirectDrawArguments& arguments) override;
    void record_draw_indexed_indirect(const IndirectDrawArguments& arguments) override;
    void record_reset_query_pool(QueryPoolHandle pool, std::uint32_t first, std::uint32_t count) override;
    void record_write_timestamp(QueryPoolHandle pool, PipelineStage stage, std::uint32_t query) override;
    void record_begin_query(QueryPoolHandle pool, std::uint32_t query) override;
    void record_end_query(QueryPoolHandle pool, std::uint32_t query) override;
    void record_begin_debug_label(std::string_view name, const std::array<float, 4>& color) override;
    void record_end_debug_label() override;
    void record_execute_commands(std::span<const CommandBufferHandle> secondary_command_buffers) override;
    void record_blit_image(ImageHandle source,
                           ImageLayout source_layout,
                           ImageHandle destination,
                           ImageLayout destination_layout,
                           std::span<const ImageBlitRegion> regions,
                           Filter filter) override;
    void record_resolve_image(ImageHandle source,
                              ImageLayout source_layout,
                              ImageHandle destination,
                              ImageLayout destination_layout,
                              std::span<const ImageResolveRegion> regions) override;
    void record_clear_depth_stencil_image(ImageHandle image,
                                          ImageLayout layout,
                                          float depth,
                                          std::uint32_t stencil,
                                          const ImageSubresourceRange& range) override;
    void record_clear_attachments(std::span<const ClearAttachment> attachments, std::span<const ClearRectangle> rectangles) override;
    void record_set_stencil_reference(StencilFace faces, std::uint32_t reference) override;
    void record_set_blend_constants(const std::array<float, 4>& constants) override;
    void record_set_depth_bias(float constant_factor, float clamp, float slope_factor) override;
    void record_set_depth_bounds(float minimum, float maximum) override;
    void record_set_line_width(float width) override;
    void record_set_cull_mode(CullMode mode) override;
    void record_set_front_face(FrontFace front_face) override;
    void record_set_primitive_topology(PrimitiveTopology topology) override;
    void record_set_depth_test(bool enable) override;
    void record_set_depth_write(bool enable) override;
    void record_set_depth_compare(CompareOperation operation) override;
    void record_set_stencil_test(bool enable) override;
    void record_set_stencil_operation(StencilFace faces,
                                      StencilOperation fail,
                                      StencilOperation pass,
                                      StencilOperation depth_fail,
                                      CompareOperation compare) override;
    void record_set_fragment_shading_rate(ShadingRate rate, const std::array<ShadingRateCombiner, 2>& combiners) override;
    void record_draw_indirect_count(const IndirectDrawArguments& arguments, const IndirectCount& count) override;
    void record_draw_indexed_indirect_count(const IndirectDrawArguments& arguments, const IndirectCount& count) override;
    void record_draw_mesh_tasks(const DrawMeshTasksArguments& arguments) override;
    void record_draw_mesh_tasks_indirect(const IndirectDrawArguments& arguments) override;
    void record_draw_mesh_tasks_indirect_count(const IndirectDrawArguments& arguments, const IndirectCount& count) override;
    void record_bind_ray_tracing_pipeline(RayTracingPipelineHandle pipeline) override;
    void record_trace_rays(const ShaderBindingTable& table, std::uint32_t width, std::uint32_t height, std::uint32_t depth) override;
    void record_trace_rays_indirect(const ShaderBindingTable& table, BufferHandle buffer, std::uint64_t offset) override;
    void record_build_acceleration_structures(std::span<const AccelerationStructureBuildDescription> builds) override;
    void record_copy_acceleration_structure(AccelerationStructureHandle source,
                                            AccelerationStructureHandle destination,
                                            AccelerationStructureCopyMode mode) override;
    void record_write_acceleration_structure_properties(std::span<const AccelerationStructureHandle> structures,
                                                        QueryPoolHandle pool,
                                                        std::uint32_t first_query) override;

private:
    /// @brief The record of @p handle; an invalid handle marks the recording failed, then throws.
    template<class Tag, class Record>
    [[nodiscard]] auto resolve(const gfx::backends::HandleTable<Tag, Record>& table, Handle<Tag> handle, std::string_view operation) -> const Record&
    {
        const Record* record = table.find(handle);
        if (record == nullptr) [[unlikely]]
        {
            mark_failed(operation);
            return table.get(handle, operation);
        }
        return *record;
    }

    /// @brief Marks the recording failed and throws an InvalidUsageError.
    [[noreturn]] void fail(std::string_view operation, std::string problem, std::string suggestion = {});

    /// @brief Remembers that a command failed: the command buffer cannot be submitted.
    void mark_failed(std::string_view operation)
    {
        if (m_status.failure.empty())
        {
            m_status.failure = std::format("{} failed while recording '{}'", operation, m_debug_name);
        }
    }

    /// @brief Throws unless the device has @p feature (and marks the recording failed).
    void require(DeviceFeature feature, std::string_view operation)
    {
        if (!m_device.enabled(feature))
        {
            mark_failed(operation);
            m_device.require(feature, operation);
        }
    }

    /// @brief Native stages for barriers of this command buffer's queue.
    [[nodiscard]] auto stages(PipelineStage stages) const noexcept -> VkPipelineStageFlags2
    {
        return to_vulkan(stages) & m_stage_mask;
    }

    /// @brief Queue family indices of an ownership transfer (both ignored when there is none).
    [[nodiscard]] auto families(std::optional<QueueType> source, std::optional<QueueType> destination) const noexcept
        -> std::pair<std::uint32_t, std::uint32_t>;

    /// @brief Subresource range of @p image, with the image's aspects when the default COLOR does not apply.
    [[nodiscard]] static auto range_of(const ImageRecord& image, const ImageSubresourceRange& range) noexcept -> VkImageSubresourceRange;

    /// @brief Subresource layers of @p image, likewise.
    [[nodiscard]] static auto layers_of(const ImageRecord& image, const ImageSubresourceLayers& layers) noexcept -> VkImageSubresourceLayers;

    /// @brief Translates @p barriers into the reusable arrays and returns the dependency information.
    [[nodiscard]] auto dependency(const BarrierBatch& barriers, std::string_view operation) -> VkDependencyInfo;

    /// @brief Fills the attachments of a rendering pass; returns the rendering information.
    [[nodiscard]] auto rendering_information(const RenderingDescription& rendering, VkRenderingFlags flags, RenderingInheritance& inheritance)
        -> VkRenderingInfo;

    /// @brief Binds the bindless table for every bind point the queue supports.
    void bind_bindless_table(bool graphics_only);

    /// @brief The native pipeline of a pipeline state, which must be ready.
    [[nodiscard]] auto ready_pipeline(const PipelineState& state, std::string_view operation) -> VkPipeline;

    /// @brief Stride of indirect records (0 = tightly packed records of @p record_size bytes).
    [[nodiscard]] static auto stride_of(const IndirectDrawArguments& arguments, std::uint32_t record_size) noexcept -> std::uint32_t
    {
        return arguments.stride == 0 ? record_size : arguments.stride;
    }

    /// @brief Checks a multi-draw: draw counts above 1 need MULTI_DRAW_INDIRECT.
    void check_draw_count(std::uint32_t draw_count, std::string_view operation)
    {
        if (draw_count > 1)
        {
            require(DeviceFeature::MULTI_DRAW_INDIRECT, operation);
        }
    }

    /// @brief Hands the breadcrumbs of the previous recording to the deleter (the GPU may still be
    ///        executing it, and a device loss reads them) and starts new ones.
    void retire_breadcrumbs()
    {
        if (m_breadcrumbs_enabled && m_breadcrumbs && !m_breadcrumbs->empty())
        {
            m_device.vulkan_deleter().enqueue([kept = std::move(m_breadcrumbs)]
            {
            });
        }
        if (m_breadcrumbs_enabled && (!m_breadcrumbs || !m_breadcrumbs->empty()))
        {
            m_breadcrumbs = std::make_shared<std::deque<BreadcrumbLabel>>();
        }
        m_label_stack.clear();
    }

    /// @brief A strided device address region of a shader binding table.
    [[nodiscard]] auto region_of(const ShaderBindingTableRegion& region, std::string_view operation) -> VkStridedDeviceAddressRegionKHR;

private:
    VulkanDevice& m_device;
    const VolkDeviceTable& m_functions;
    std::shared_ptr<NativeCommandPool> m_pool;
    VkCommandBuffer m_command_buffer;
    QueueType m_queue_type;
    CommandBufferLevel m_level;
    bool m_resettable;
    QueueCapability m_capabilities;
    VkPipelineStageFlags2 m_stage_mask;
    CommandBufferStatus m_status;
    std::uint32_t m_parallel_passes = 0;
    std::optional<std::uint32_t> m_current_parallel_pass;
    std::uint32_t m_open_labels = 0;
    bool m_breadcrumbs_enabled = false;
    std::shared_ptr<std::deque<BreadcrumbLabel>> m_breadcrumbs;
    std::vector<const BreadcrumbLabel*> m_label_stack;
    CommandBufferHandle m_handle;
    std::string m_debug_name;

    // Arrays reused by every barrier: no allocation per command once they have grown.
    std::vector<VkMemoryBarrier2> m_memory_barriers;
    std::vector<VkBufferMemoryBarrier2> m_buffer_barriers;
    std::vector<VkImageMemoryBarrier2> m_image_barriers;
    std::vector<VkRenderingAttachmentInfo> m_color_attachments;

    // Arrays reused by copies, blits, resolves, attachment clears, acceleration structure builds and
    // property queries, and the null-terminated name of a debug label: no allocation per command either.
    std::vector<VkBufferCopy> m_buffer_copies{};
    std::vector<VkBufferImageCopy> m_buffer_image_copies{};
    std::vector<VkImageCopy> m_image_copies{};
    std::vector<VkImageBlit> m_image_blits{};
    std::vector<VkImageResolve> m_image_resolves{};
    std::vector<VkClearAttachment> m_clear_attachments{};
    std::vector<VkClearRect> m_clear_rectangles{};
    std::vector<VkAccelerationStructureGeometryKHR> m_build_geometries{};
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> m_build_ranges{};
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> m_build_informations{};
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> m_build_range_pointers{};
    std::vector<VkAccelerationStructureKHR> m_acceleration_structures{};
    std::string m_label_name{};
};

// ---------------------------------------------------------------------------------------------------
// VulkanCommandPool
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::create_command_pool(const CommandPoolDescription& description) -> std::unique_ptr<CommandPool>
{
    return std::make_unique<VulkanCommandPool>(*this, description);
}

VulkanCommandPool::VulkanCommandPool(VulkanDevice& device,
                                     const CommandPoolDescription& description)
    : CommandPool{}
    , m_device{device}
    , m_queue_type{description.queue_type}
    , m_options{description.options}
    , m_debug_name{description.debug_name}
{
    set_validation_level(device.validation_level());

    // The family of the queue type; transient and individually resettable buffers as requested.
    const std::uint32_t family = device.queue_family(description.queue_type);
    if (family == std::numeric_limits<std::uint32_t>::max())
    {
        throw_unsupported("Device::create_command_pool", std::format("{} queue", description.queue_type),
                          std::format("'{}' has no queue family for {} work", device.physical_device().properties().name,
                                      description.queue_type),
                          "create pools for the queue types the device has");
    }
    VkCommandPoolCreateFlags flags = 0;
    if (has_any(description.options, CommandPoolOption::TRANSIENT))
    {
        flags |= VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    }
    if (has_any(description.options, CommandPoolOption::RESETTABLE_COMMAND_BUFFERS))
    {
        flags |= VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    }
    const VkCommandPoolCreateInfo information{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = flags,
        .queueFamilyIndex = family,
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    device.check_result(device.functions().vkCreateCommandPool(device.native(), &information, nullptr, &pool), "Device::create_command_pool",
                        "vkCreateCommandPool");
    m_native = std::make_shared<NativeCommandPool>(device, pool);
    m_handle = device.handles().command_pools.insert({.pool = pool});
    device.name_object(VK_OBJECT_TYPE_COMMAND_POOL, pool, m_debug_name);
}

VulkanCommandPool::~VulkanCommandPool()
{
    (void)m_device.handles().command_pools.remove(m_handle);
}

auto
VulkanCommandPool::allocate(CommandBufferLevel level,
                            std::string_view debug_name) -> std::unique_ptr<CommandBuffer>
{
    const VkCommandBufferAllocateInfo information{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_native->pool,
        .level = level == CommandBufferLevel::PRIMARY ? VK_COMMAND_BUFFER_LEVEL_PRIMARY : VK_COMMAND_BUFFER_LEVEL_SECONDARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    m_device.check_result(m_device.functions().vkAllocateCommandBuffers(m_device.native(), &information, &command_buffer),
                          "CommandPool::allocate", "vkAllocateCommandBuffers");
    return std::make_unique<VulkanCommandBuffer>(m_device, m_native, command_buffer, m_queue_type, level,
                                                 has_any(m_options, CommandPoolOption::RESETTABLE_COMMAND_BUFFERS), debug_name);
}

void
VulkanCommandPool::reset()
{
    m_device.check_result(m_device.functions().vkResetCommandPool(m_device.native(), m_native->pool, 0), "CommandPool::reset",
                          "vkResetCommandPool");
}

// ---------------------------------------------------------------------------------------------------
// VulkanCommandBuffer: lifetime
// ---------------------------------------------------------------------------------------------------

VulkanCommandBuffer::VulkanCommandBuffer(VulkanDevice& device,
                                         std::shared_ptr<NativeCommandPool> pool,
                                         VkCommandBuffer command_buffer,
                                         QueueType queue_type,
                                         CommandBufferLevel level,
                                         bool resettable,
                                         std::string_view debug_name)
    : CommandBuffer{}
    , m_device{device}
    , m_functions{device.functions()}
    , m_pool{std::move(pool)}
    , m_command_buffer{command_buffer}
    , m_queue_type{queue_type}
    , m_level{level}
    , m_resettable{resettable}
    , m_capabilities{queue_type_capabilities(queue_type)}
    , m_debug_name{debug_name}
{
    set_validation_level(device.validation_level());

    // Barriers name only the stages the device and the queue type both have.
    m_stage_mask = device.supported_stages() & queue_stages(m_capabilities);

    // Debug labels double as GPU breadcrumbs when the device collects them.
    m_breadcrumbs_enabled = has_any(device.crash_diagnostics(), CrashDiagnostic::BREADCRUMBS) && device.enabled(DeviceFeature::GPU_CRASH_MARKERS);
    retire_breadcrumbs();
    m_handle = device.handles().command_buffers.insert(
        {.command_buffer = command_buffer, .level = level, .queue_type = queue_type, .status = &m_status});
    device.name_object(VK_OBJECT_TYPE_COMMAND_BUFFER, command_buffer, m_debug_name);
}

VulkanCommandBuffer::~VulkanCommandBuffer()
{
    // Freed once the GPU is done with it; the pool lives until then.
    (void)m_device.handles().command_buffers.remove(m_handle);
    retire_breadcrumbs();
    m_device.vulkan_deleter().enqueue([pool = m_pool, command_buffer = m_command_buffer]
    {
        pool->device.functions().vkFreeCommandBuffers(pool->device.native(), pool->pool, 1, &command_buffer);
    });
}

void
VulkanCommandBuffer::reset()
{
    if (!m_resettable)
    {
        throw InvalidUsageError({
            .backend = BackendKind::VULKAN,
            .operation = "CommandBuffer::reset",
            .problem = std::format("command buffer '{}' comes from a pool without RESETTABLE_COMMAND_BUFFERS", m_debug_name),
            .suggestion = "reset its pool (CommandPool::reset), or create the pool with CommandPoolOption::RESETTABLE_COMMAND_BUFFERS",
        });
    }
    m_device.check_result(m_functions.vkResetCommandBuffer(m_command_buffer, 0), "CommandBuffer::reset", "vkResetCommandBuffer");
    m_status.executable.store(false, std::memory_order_release);
}

void
VulkanCommandBuffer::fail(std::string_view operation,
                          std::string problem,
                          std::string suggestion)
{
    mark_failed(operation);
    throw InvalidUsageError({
        .backend = BackendKind::VULKAN,
        .operation = std::string{operation},
        .problem = std::move(problem),
        .suggestion = std::move(suggestion),
    });
}

// ---------------------------------------------------------------------------------------------------
// VulkanCommandBuffer: beginning and ending
// ---------------------------------------------------------------------------------------------------

void
VulkanCommandBuffer::bind_bindless_table(bool graphics_only)
{
    // Every pipeline using the bindless layout finds the table bound: no binding per draw or dispatch.
    const auto& layout = static_cast<const VulkanPipelineLayout&>(m_device.bindless_pipeline_layout());
    const VkDescriptorSet set = m_device.bindless().set();
    const auto bind = [&](VkPipelineBindPoint bind_point)
    {
        m_functions.vkCmdBindDescriptorSets(m_command_buffer, bind_point, layout.native(), BINDLESS_SET, 1, &set, 0, nullptr);
    };
    if (has_any(m_capabilities, QueueCapability::GRAPHICS))
    {
        bind(VK_PIPELINE_BIND_POINT_GRAPHICS);
    }
    if (graphics_only)
    {
        return;
    }
    if (has_any(m_capabilities, QueueCapability::COMPUTE))
    {
        bind(VK_PIPELINE_BIND_POINT_COMPUTE);
        if (m_device.enabled(DeviceFeature::RAY_TRACING))
        {
            bind(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR);
        }
    }
}

void
VulkanCommandBuffer::record_begin()
{
    // A fresh recording: not executable until it ends well.
    m_status.executable.store(false, std::memory_order_release);
    m_status.failure.clear();
    m_parallel_passes = 0;
    m_current_parallel_pass.reset();
    m_open_labels = 0;
    retire_breadcrumbs();
    const VkCommandBufferBeginInfo information{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    m_device.check_result(m_functions.vkBeginCommandBuffer(m_command_buffer, &information), "Recorder::Recorder", "vkBeginCommandBuffer");
    bind_bindless_table(false);
}

void
VulkanCommandBuffer::record_begin_rendering_part(const RenderingInheritance& inheritance)
{
    // A secondary command buffer continuing a pass of its primary, with the pass's formats.
    m_status.executable.store(false, std::memory_order_release);
    m_status.failure.clear();
    m_status.inherited_primary = inheritance.primary;
    m_status.inherited_pass = inheritance.pass;
    m_open_labels = 0;
    retire_breadcrumbs();
    std::array<VkFormat, MAXIMUM_COLOR_ATTACHMENTS> formats{};
    for (std::uint32_t index = 0; index < inheritance.color_count; ++index)
    {
        formats[index] = to_vulkan(inheritance.color_formats[index]);
    }
    const VkCommandBufferInheritanceRenderingInfo rendering{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO,
        .viewMask = inheritance.view_mask,
        .colorAttachmentCount = inheritance.color_count,
        .pColorAttachmentFormats = formats.data(),
        .depthAttachmentFormat = to_vulkan(inheritance.depth_format),
        .stencilAttachmentFormat = to_vulkan(inheritance.stencil_format),
        .rasterizationSamples = to_vulkan(inheritance.samples),
    };
    const VkCommandBufferInheritanceInfo inheritance_information{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
                                                                 .pNext = &rendering};
    const VkCommandBufferBeginInfo information{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inheritance_information,
    };
    m_device.check_result(m_functions.vkBeginCommandBuffer(m_command_buffer, &information), "RenderingPartRecorder::RenderingPartRecorder",
                          "vkBeginCommandBuffer");
    bind_bindless_table(true);
}

void
VulkanCommandBuffer::record_end() noexcept
{
    // Labels left open are closed, then the recording ends; failures surface when it is submitted.
    while (m_open_labels > 0)
    {
        record_end_debug_label();
    }
    const VkResult result = m_functions.vkEndCommandBuffer(m_command_buffer);
    if (result != VK_SUCCESS && m_status.failure.empty())
    {
        m_status.failure = std::format("vkEndCommandBuffer returned {}", result_name(result));
    }
    m_status.executable.store(m_status.failure.empty(), std::memory_order_release);
}

// ---------------------------------------------------------------------------------------------------
// VulkanCommandBuffer: synchronization
// ---------------------------------------------------------------------------------------------------

auto
VulkanCommandBuffer::families(std::optional<QueueType> source, std::optional<QueueType> destination) const noexcept
    -> std::pair<std::uint32_t, std::uint32_t>
{
    if (!source || !destination)
    {
        return {VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED};
    }
    const std::uint32_t source_family = m_device.queue_family(*source);
    const std::uint32_t destination_family = m_device.queue_family(*destination);
    if (source_family == destination_family)
    {
        return {VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED};
    }
    return {source_family, destination_family};
}

auto
VulkanCommandBuffer::range_of(const ImageRecord& image,
                              const ImageSubresourceRange& range) noexcept -> VkImageSubresourceRange
{
    VkImageSubresourceRange native = to_vulkan(range);
    if (range.aspects == ImageAspect::COLOR && !has_any(image.aspects, ImageAspect::COLOR))
    {
        native.aspectMask = to_vulkan(image.aspects);
    }
    return native;
}

auto
VulkanCommandBuffer::layers_of(const ImageRecord& image,
                               const ImageSubresourceLayers& layers) noexcept -> VkImageSubresourceLayers
{
    VkImageSubresourceLayers native = to_vulkan(layers);
    if (layers.aspects == ImageAspect::COLOR && !has_any(image.aspects, ImageAspect::COLOR))
    {
        native.aspectMask = to_vulkan(image.aspects);
    }
    return native;
}

auto
VulkanCommandBuffer::dependency(const BarrierBatch& barriers,
                                std::string_view operation) -> VkDependencyInfo
{
    HandleTables& handles = m_device.handles();
    const VkAccessFlags2 supported_access = m_device.supported_access();

    // Stages masked to the queue; accesses to what those stages perform.
    const auto translate = [&](PipelineStage source_stages, MemoryAccess source_access, PipelineStage destination_stages,
                               MemoryAccess destination_access)
    {
        const VkPipelineStageFlags2 source = stages(source_stages);
        const VkPipelineStageFlags2 destination = stages(destination_stages);
        return std::array<std::uint64_t, 4>{source, accesses_of_stages(to_vulkan(source_access) & supported_access, source), destination,
                                            accesses_of_stages(to_vulkan(destination_access) & supported_access, destination)};
    };

    // Global memory barriers.
    m_memory_barriers.clear();
    for (const GlobalMemoryBarrier& barrier : barriers.memory_barriers)
    {
        const auto [source, source_access, destination, destination_access] =
            translate(barrier.source_stages, barrier.source_access, barrier.destination_stages, barrier.destination_access);
        m_memory_barriers.push_back({.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                                     .srcStageMask = source,
                                     .srcAccessMask = source_access,
                                     .dstStageMask = destination,
                                     .dstAccessMask = destination_access});
    }

    // Buffer barriers, with ownership transfers between queue families.
    m_buffer_barriers.clear();
    for (const BufferBarrier& barrier : barriers.buffer_barriers)
    {
        const BufferRecord& buffer = resolve(handles.buffers, barrier.buffer, operation);
        const auto [source, source_access, destination, destination_access] =
            translate(barrier.source_stages, barrier.source_access, barrier.destination_stages, barrier.destination_access);
        const auto [source_family, destination_family] = families(barrier.source_queue, barrier.destination_queue);
        m_buffer_barriers.push_back({.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                                     .srcStageMask = source,
                                     .srcAccessMask = source_access,
                                     .dstStageMask = destination,
                                     .dstAccessMask = destination_access,
                                     .srcQueueFamilyIndex = source_family,
                                     .dstQueueFamilyIndex = destination_family,
                                     .buffer = buffer.buffer,
                                     .offset = barrier.offset,
                                     .size = barrier.size == WHOLE_SIZE ? VK_WHOLE_SIZE : barrier.size});
    }

    // Image barriers, with layout transitions.
    m_image_barriers.clear();
    for (const ImageBarrier& barrier : barriers.image_barriers)
    {
        const ImageRecord& image = resolve(handles.images, barrier.image, operation);
        const auto [source, source_access, destination, destination_access] =
            translate(barrier.source_stages, barrier.source_access, barrier.destination_stages, barrier.destination_access);
        const auto [source_family, destination_family] = families(barrier.source_queue, barrier.destination_queue);
        m_image_barriers.push_back({.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                    .srcStageMask = source,
                                    .srcAccessMask = source_access,
                                    .dstStageMask = destination,
                                    .dstAccessMask = destination_access,
                                    .oldLayout = to_vulkan(barrier.old_layout),
                                    .newLayout = to_vulkan(barrier.new_layout),
                                    .srcQueueFamilyIndex = source_family,
                                    .dstQueueFamilyIndex = destination_family,
                                    .image = image.image,
                                    .subresourceRange = range_of(image, barrier.range)});
    }
    return {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = static_cast<std::uint32_t>(m_memory_barriers.size()),
        .pMemoryBarriers = m_memory_barriers.data(),
        .bufferMemoryBarrierCount = static_cast<std::uint32_t>(m_buffer_barriers.size()),
        .pBufferMemoryBarriers = m_buffer_barriers.data(),
        .imageMemoryBarrierCount = static_cast<std::uint32_t>(m_image_barriers.size()),
        .pImageMemoryBarriers = m_image_barriers.data(),
    };
}

void
VulkanCommandBuffer::record_barrier(const BarrierBatch& barriers)
{
    const VkDependencyInfo information = dependency(barriers, "Recorder::barrier");
    m_functions.vkCmdPipelineBarrier2(m_command_buffer, &information);
}

void
VulkanCommandBuffer::record_set_event(EventHandle event,
                                      const BarrierBatch& barriers)
{
    const VkEvent native = resolve(m_device.handles().events, event, "Recorder::set_event").event;
    const VkDependencyInfo information = dependency(barriers, "Recorder::set_event");
    m_functions.vkCmdSetEvent2(m_command_buffer, native, &information);
}

void
VulkanCommandBuffer::record_wait_events(std::span<const EventHandle> events,
                                        const BarrierBatch& barriers)
{
    // One dependency per event: the barriers given when each was set.
    static constexpr std::string_view OPERATION = "Recorder::wait_events";
    std::array<VkEvent, 16> natives{};
    std::array<VkDependencyInfo, 16> dependencies{};
    if (events.size() > natives.size())
    {
        fail(OPERATION, std::format("{} events are waited at once; at most {} are", events.size(), natives.size()),
             "wait in several commands");
    }
    const VkDependencyInfo information = dependency(barriers, OPERATION);
    for (std::size_t index = 0; index < events.size(); ++index)
    {
        natives[index] = resolve(m_device.handles().events, events[index], OPERATION).event;
        dependencies[index] = information;
    }
    m_functions.vkCmdWaitEvents2(m_command_buffer, static_cast<std::uint32_t>(events.size()), natives.data(), dependencies.data());
}

void
VulkanCommandBuffer::record_reset_event(EventHandle event,
                                        PipelineStage stage_mask)
{
    const VkEvent native = resolve(m_device.handles().events, event, "Recorder::reset_event").event;
    m_functions.vkCmdResetEvent2(m_command_buffer, native, stages(stage_mask));
}

// ---------------------------------------------------------------------------------------------------
// VulkanCommandBuffer: transfers
// ---------------------------------------------------------------------------------------------------

void
VulkanCommandBuffer::record_copy_buffer(BufferHandle source,
                                        BufferHandle destination,
                                        std::span<const BufferCopyRegion> regions)
{
    static constexpr std::string_view OPERATION = "Recorder::copy_buffer";
    const BufferRecord& from = resolve(m_device.handles().buffers, source, OPERATION);
    const BufferRecord& to = resolve(m_device.handles().buffers, destination, OPERATION);

    // Every region inside both buffers (checked when commands are validated), translated into reused storage.
    std::vector<VkBufferCopy>& native = reuse(m_buffer_copies, regions.size());
    for (const BufferCopyRegion& region : regions)
    {
        if (validates(ValidationLevel::COMMANDS) &&
            (region.source_offset + region.size > from.size || region.destination_offset + region.size > to.size))
        {
            fail(OPERATION, std::format("copying {} bytes from offset {} of a {}-byte buffer to offset {} of a {}-byte buffer leaves "
                                        "the buffers",
                                        region.size, region.source_offset, from.size, region.destination_offset, to.size));
        }
        native.push_back({.srcOffset = region.source_offset, .dstOffset = region.destination_offset, .size = region.size});
    }
    m_functions.vkCmdCopyBuffer(m_command_buffer, from.buffer, to.buffer, static_cast<std::uint32_t>(native.size()), native.data());
}

void
VulkanCommandBuffer::record_copy_buffer_to_image(BufferHandle source,
                                                 ImageHandle destination,
                                                 ImageLayout destination_layout,
                                                 std::span<const BufferImageCopyRegion> regions)
{
    static constexpr std::string_view OPERATION = "Recorder::copy_buffer_to_image";
    const BufferRecord& buffer = resolve(m_device.handles().buffers, source, OPERATION);
    const ImageRecord& image = resolve(m_device.handles().images, destination, OPERATION);

    // The regions, translated into reused storage.
    std::vector<VkBufferImageCopy>& native = reuse(m_buffer_image_copies, regions.size());
    for (const BufferImageCopyRegion& region : regions)
    {
        native.push_back({.bufferOffset = region.buffer_offset,
                          .bufferRowLength = region.buffer_row_length,
                          .bufferImageHeight = region.buffer_image_height,
                          .imageSubresource = layers_of(image, region.image_subresource),
                          .imageOffset = to_vulkan(region.image_offset),
                          .imageExtent = to_vulkan(region.image_extent)});
    }
    m_functions.vkCmdCopyBufferToImage(m_command_buffer, buffer.buffer, image.image, to_vulkan(destination_layout),
                                       static_cast<std::uint32_t>(native.size()), native.data());
}

void
VulkanCommandBuffer::record_copy_image_to_buffer(ImageHandle source,
                                                 ImageLayout source_layout,
                                                 BufferHandle destination,
                                                 std::span<const BufferImageCopyRegion> regions)
{
    static constexpr std::string_view OPERATION = "Recorder::copy_image_to_buffer";
    const ImageRecord& image = resolve(m_device.handles().images, source, OPERATION);
    const BufferRecord& buffer = resolve(m_device.handles().buffers, destination, OPERATION);

    // The regions, translated into reused storage.
    std::vector<VkBufferImageCopy>& native = reuse(m_buffer_image_copies, regions.size());
    for (const BufferImageCopyRegion& region : regions)
    {
        native.push_back({.bufferOffset = region.buffer_offset,
                          .bufferRowLength = region.buffer_row_length,
                          .bufferImageHeight = region.buffer_image_height,
                          .imageSubresource = layers_of(image, region.image_subresource),
                          .imageOffset = to_vulkan(region.image_offset),
                          .imageExtent = to_vulkan(region.image_extent)});
    }
    m_functions.vkCmdCopyImageToBuffer(m_command_buffer, image.image, to_vulkan(source_layout), buffer.buffer,
                                       static_cast<std::uint32_t>(native.size()), native.data());
}

void
VulkanCommandBuffer::record_copy_image(ImageHandle source,
                                       ImageLayout source_layout,
                                       ImageHandle destination,
                                       ImageLayout destination_layout,
                                       std::span<const ImageCopyRegion> regions)
{
    static constexpr std::string_view OPERATION = "Recorder::copy_image";
    const ImageRecord& from = resolve(m_device.handles().images, source, OPERATION);
    const ImageRecord& to = resolve(m_device.handles().images, destination, OPERATION);

    // The regions, translated into reused storage.
    std::vector<VkImageCopy>& native = reuse(m_image_copies, regions.size());
    for (const ImageCopyRegion& region : regions)
    {
        native.push_back({.srcSubresource = layers_of(from, region.source_subresource),
                          .srcOffset = to_vulkan(region.source_offset),
                          .dstSubresource = layers_of(to, region.destination_subresource),
                          .dstOffset = to_vulkan(region.destination_offset),
                          .extent = to_vulkan(region.extent)});
    }
    m_functions.vkCmdCopyImage(m_command_buffer, from.image, to_vulkan(source_layout), to.image, to_vulkan(destination_layout),
                               static_cast<std::uint32_t>(native.size()), native.data());
}

void
VulkanCommandBuffer::record_fill_buffer(BufferHandle buffer,
                                        std::uint64_t offset,
                                        std::uint64_t size,
                                        std::uint32_t value)
{
    static constexpr std::string_view OPERATION = "Recorder::fill_buffer";
    const BufferRecord& record = resolve(m_device.handles().buffers, buffer, OPERATION);
    if (validates(ValidationLevel::COMMANDS) && (offset % 4 != 0 || (size != WHOLE_SIZE && (size % 4 != 0 || offset + size > record.size))))
    {
        fail(OPERATION, std::format("filling {} bytes at offset {} of a {}-byte buffer: offset and size are multiples of 4 inside the buffer",
                                    size, offset, record.size));
    }
    m_functions.vkCmdFillBuffer(m_command_buffer, record.buffer, offset, size == WHOLE_SIZE ? VK_WHOLE_SIZE : size, value);
}

void
VulkanCommandBuffer::record_update_buffer(BufferHandle buffer,
                                          std::uint64_t offset,
                                          std::span<const std::byte> data)
{
    static constexpr std::string_view OPERATION = "Recorder::update_buffer";
    const BufferRecord& record = resolve(m_device.handles().buffers, buffer, OPERATION);
    if (validates(ValidationLevel::COMMANDS) &&
        (offset % 4 != 0 || data.size() % 4 != 0 || data.empty() || data.size() > 65536 || offset + data.size() > record.size))
    {
        fail(OPERATION, std::format("updating {} bytes at offset {} of a {}-byte buffer: updates are 4 to 65536 bytes, multiples of 4, "
                                    "at an offset that is a multiple of 4, inside the buffer",
                                    data.size(), offset, record.size),
             "copy larger data from a staging buffer (Uploader)");
    }
    m_functions.vkCmdUpdateBuffer(m_command_buffer, record.buffer, offset, data.size(), data.data());
}

void
VulkanCommandBuffer::record_clear_color_image(ImageHandle image,
                                              ImageLayout layout,
                                              const std::array<float, 4>& color,
                                              const ImageSubresourceRange& range)
{
    const ImageRecord& record = resolve(m_device.handles().images, image, "Recorder::clear_color_image");
    const VkClearColorValue value = clear_color(record.format, color);
    const VkImageSubresourceRange native = range_of(record, range);
    m_functions.vkCmdClearColorImage(m_command_buffer, record.image, to_vulkan(layout), &value, 1, &native);
}

void
VulkanCommandBuffer::record_clear_depth_stencil_image(ImageHandle image,
                                                      ImageLayout layout,
                                                      float depth,
                                                      std::uint32_t stencil,
                                                      const ImageSubresourceRange& range)
{
    const ImageRecord& record = resolve(m_device.handles().images, image, "Recorder::clear_depth_stencil_image");
    const VkClearDepthStencilValue value{.depth = depth, .stencil = stencil};
    const VkImageSubresourceRange native = range_of(record, range);
    m_functions.vkCmdClearDepthStencilImage(m_command_buffer, record.image, to_vulkan(layout), &value, 1, &native);
}

void
VulkanCommandBuffer::record_blit_image(ImageHandle source,
                                       ImageLayout source_layout,
                                       ImageHandle destination,
                                       ImageLayout destination_layout,
                                       std::span<const ImageBlitRegion> regions,
                                       Filter filter)
{
    static constexpr std::string_view OPERATION = "Recorder::blit_image";
    const ImageRecord& from = resolve(m_device.handles().images, source, OPERATION);
    const ImageRecord& to = resolve(m_device.handles().images, destination, OPERATION);

    // The regions, translated into reused storage.
    std::vector<VkImageBlit>& native = reuse(m_image_blits, regions.size());
    for (const ImageBlitRegion& region : regions)
    {
        native.push_back({.srcSubresource = layers_of(from, region.source_subresource),
                          .srcOffsets = {to_vulkan(region.source_bounds[0]), to_vulkan(region.source_bounds[1])},
                          .dstSubresource = layers_of(to, region.destination_subresource),
                          .dstOffsets = {to_vulkan(region.destination_bounds[0]), to_vulkan(region.destination_bounds[1])}});
    }
    m_functions.vkCmdBlitImage(m_command_buffer, from.image, to_vulkan(source_layout), to.image, to_vulkan(destination_layout),
                               static_cast<std::uint32_t>(native.size()), native.data(), to_vulkan(filter));
}

void
VulkanCommandBuffer::record_resolve_image(ImageHandle source,
                                          ImageLayout source_layout,
                                          ImageHandle destination,
                                          ImageLayout destination_layout,
                                          std::span<const ImageResolveRegion> regions)
{
    static constexpr std::string_view OPERATION = "Recorder::resolve_image";
    const ImageRecord& from = resolve(m_device.handles().images, source, OPERATION);
    const ImageRecord& to = resolve(m_device.handles().images, destination, OPERATION);

    // The regions, translated into reused storage.
    std::vector<VkImageResolve>& native = reuse(m_image_resolves, regions.size());
    for (const ImageResolveRegion& region : regions)
    {
        native.push_back({.srcSubresource = layers_of(from, region.source_subresource),
                          .srcOffset = to_vulkan(region.source_offset),
                          .dstSubresource = layers_of(to, region.destination_subresource),
                          .dstOffset = to_vulkan(region.destination_offset),
                          .extent = to_vulkan(region.extent)});
    }
    m_functions.vkCmdResolveImage(m_command_buffer, from.image, to_vulkan(source_layout), to.image, to_vulkan(destination_layout),
                                  static_cast<std::uint32_t>(native.size()), native.data());
}

// ---------------------------------------------------------------------------------------------------
// VulkanCommandBuffer: pipelines, descriptors, push constants
// ---------------------------------------------------------------------------------------------------

auto
VulkanCommandBuffer::ready_pipeline(const PipelineState& state,
                                    std::string_view operation) -> VkPipeline
{
    switch (state.status.load(std::memory_order_acquire))
    {
        case PipelineStatus::READY: return state.pipeline.load(std::memory_order_acquire);
        case PipelineStatus::COMPILING:
            fail(operation, "the pipeline is still compiling in the background",
                "bind it once status() is READY (or call wait()); draw with a fallback pipeline meanwhile");
        case PipelineStatus::COMPILATION_FAILED:
            fail(operation, "the pipeline failed to compile", "call wait() to get the compilation's error");
    }
    fail(operation, "the pipeline has an unknown status");
}

void
VulkanCommandBuffer::record_bind_compute_pipeline(ComputePipelineHandle pipeline)
{
    static constexpr std::string_view OPERATION = "Recorder::bind_pipeline";
    const ComputePipelineRecord& record = resolve(m_device.handles().compute_pipelines, pipeline, OPERATION);
    m_functions.vkCmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, ready_pipeline(*record.state, OPERATION));
}

void
VulkanCommandBuffer::record_bind_graphics_pipeline(GraphicsPipelineHandle pipeline)
{
    static constexpr std::string_view OPERATION = "RenderingScope::bind_pipeline";
    const GraphicsPipelineRecord& record = resolve(m_device.handles().graphics_pipelines, pipeline, OPERATION);
    m_functions.vkCmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ready_pipeline(*record.state, OPERATION));
}

void
VulkanCommandBuffer::record_bind_ray_tracing_pipeline(RayTracingPipelineHandle pipeline)
{
    static constexpr std::string_view OPERATION = "Recorder::bind_pipeline";
    const RayTracingPipelineRecord& record = resolve(m_device.handles().ray_tracing_pipelines, pipeline, OPERATION);
    m_functions.vkCmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, ready_pipeline(*record.state, OPERATION));
}

void
VulkanCommandBuffer::record_bind_descriptor_sets(PipelineBindPoint bind_point,
                                                 PipelineLayoutHandle layout,
                                                 std::uint32_t first_set,
                                                 std::span<const DescriptorSetHandle> sets,
                                                 std::span<const std::uint32_t> dynamic_offsets)
{
    static constexpr std::string_view OPERATION = "bind_descriptor_sets";
    const PipelineLayoutRecord& record = resolve(m_device.handles().pipeline_layouts, layout, OPERATION);
    std::array<VkDescriptorSet, MAXIMUM_BOUND_DESCRIPTOR_SETS> natives{};
    if (sets.size() > natives.size())
    {
        fail(OPERATION, std::format("{} descriptor sets are bound at once; at most {} are", sets.size(), natives.size()));
    }
    for (std::size_t index = 0; index < sets.size(); ++index)
    {
        natives[index] = resolve(m_device.handles().descriptor_sets, sets[index], OPERATION).set;
    }
    m_functions.vkCmdBindDescriptorSets(m_command_buffer, to_vulkan(bind_point), record.layout, first_set,
                                        static_cast<std::uint32_t>(sets.size()), natives.data(),
                                        static_cast<std::uint32_t>(dynamic_offsets.size()), dynamic_offsets.data());
}

void
VulkanCommandBuffer::record_push_constants(PipelineLayoutHandle layout,
                                           ShaderStage stage_mask,
                                           std::uint32_t offset,
                                           std::span<const std::byte> data)
{
    const PipelineLayoutRecord& record = resolve(m_device.handles().pipeline_layouts, layout, "push_constants");
    m_functions.vkCmdPushConstants(m_command_buffer, record.layout, to_vulkan(stage_mask), offset, static_cast<std::uint32_t>(data.size()),
                                   data.data());
}

// ---------------------------------------------------------------------------------------------------
// VulkanCommandBuffer: compute
// ---------------------------------------------------------------------------------------------------

void
VulkanCommandBuffer::record_dispatch(std::uint32_t group_count_x,
                                     std::uint32_t group_count_y,
                                     std::uint32_t group_count_z)
{
    m_functions.vkCmdDispatch(m_command_buffer, group_count_x, group_count_y, group_count_z);
}

void
VulkanCommandBuffer::record_dispatch_indirect(BufferHandle buffer,
                                              std::uint64_t offset)
{
    const BufferRecord& record = resolve(m_device.handles().buffers, buffer, "Recorder::dispatch_indirect");
    m_functions.vkCmdDispatchIndirect(m_command_buffer, record.buffer, offset);
}

// ---------------------------------------------------------------------------------------------------
// VulkanCommandBuffer: rendering
// ---------------------------------------------------------------------------------------------------

auto
VulkanCommandBuffer::rendering_information(const RenderingDescription& rendering,
                                           VkRenderingFlags flags,
                                           RenderingInheritance& inheritance) -> VkRenderingInfo
{
    static constexpr std::string_view OPERATION = "Recorder::begin_rendering";
    HandleTables& handles = m_device.handles();
    if (rendering.color_attachments.size() > MAXIMUM_COLOR_ATTACHMENTS)
    {
        fail(OPERATION, std::format("{} color attachments; at most {}", rendering.color_attachments.size(), MAXIMUM_COLOR_ATTACHMENTS));
    }

    // Color attachments, with their resolve targets; their formats and samples for parallel parts.
    m_color_attachments.clear();
    inheritance.color_count = static_cast<std::uint32_t>(rendering.color_attachments.size());
    for (std::size_t index = 0; index < rendering.color_attachments.size(); ++index)
    {
        const ColorAttachment& attachment = rendering.color_attachments[index];
        const ImageViewRecord& view = resolve(handles.image_views, attachment.view, OPERATION);
        const VkImageView resolve_view =
            attachment.resolve_view.is_valid() ? resolve(handles.image_views, attachment.resolve_view, OPERATION).view : VK_NULL_HANDLE;
        VkClearValue clear{};
        clear.color = clear_color(view.format, attachment.clear_color);
        m_color_attachments.push_back({
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = view.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = resolve_view != VK_NULL_HANDLE ? to_vulkan(attachment.resolve_mode) : VK_RESOLVE_MODE_NONE,
            .resolveImageView = resolve_view,
            .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = to_vulkan(attachment.load),
            .storeOp = to_vulkan(attachment.store),
            .clearValue = clear,
        });
        inheritance.color_formats[index] = view.format;
        inheritance.samples = view.samples;
    }
    m_color_attachments.push_back({});
    m_color_attachments.push_back({});

    // The depth/stencil attachment: one view feeds the depth and/or stencil attachment its format has.
    VkRenderingAttachmentInfo* depth = nullptr;
    VkRenderingAttachmentInfo* stencil = nullptr;
    if (rendering.depth_stencil_attachment)
    {
        const DepthStencilAttachment& attachment = *rendering.depth_stencil_attachment;
        const ImageViewRecord& view = resolve(handles.image_views, attachment.view, OPERATION);
        const ImageAspect aspects = format_aspects(view.format);
        const VkImageLayout layout =
            attachment.read_only ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkClearValue clear{};
        clear.depthStencil = {.depth = attachment.clear_depth, .stencil = attachment.clear_stencil};
        if (has_any(aspects, ImageAspect::DEPTH))
        {
            depth = &m_color_attachments[m_color_attachments.size() - 2];
            *depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                      .imageView = view.view,
                      .imageLayout = layout,
                      .loadOp = to_vulkan(attachment.depth_load),
                      .storeOp = to_vulkan(attachment.depth_store),
                      .clearValue = clear};
            inheritance.depth_format = view.format;
        }
        if (has_any(aspects, ImageAspect::STENCIL))
        {
            stencil = &m_color_attachments.back();
            *stencil = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .imageView = view.view,
                        .imageLayout = layout,
                        .loadOp = to_vulkan(attachment.stencil_load),
                        .storeOp = to_vulkan(attachment.stencil_store),
                        .clearValue = clear};
            inheritance.stencil_format = view.format;
        }
        inheritance.samples = view.samples;
    }
    inheritance.view_mask = rendering.view_mask;
    if (rendering.view_mask != 0)
    {
        require(DeviceFeature::MULTIVIEW, OPERATION);
    }
    return {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .flags = flags,
        .renderArea = to_vulkan(rendering.area),
        .layerCount = rendering.layer_count,
        .viewMask = rendering.view_mask,
        .colorAttachmentCount = inheritance.color_count,
        .pColorAttachments = m_color_attachments.data(),
        .pDepthAttachment = depth,
        .pStencilAttachment = stencil,
    };
}

void
VulkanCommandBuffer::record_begin_rendering(const RenderingDescription& rendering)
{
    RenderingInheritance inheritance;
    VkRenderingInfo information = rendering_information(rendering, 0, inheritance);

    // A shading rate attachment, when the pass has one.
    VkRenderingFragmentShadingRateAttachmentInfoKHR shading_rate{.sType = VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR};
    if (rendering.shading_rate_attachment)
    {
        require(DeviceFeature::VARIABLE_RATE_SHADING, "Recorder::begin_rendering");
        shading_rate.imageView = resolve(m_device.handles().image_views, rendering.shading_rate_attachment->view, "Recorder::begin_rendering").view;
        shading_rate.imageLayout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
        shading_rate.shadingRateAttachmentTexelSize = {.width = rendering.shading_rate_attachment->texel_size.width,
                                                       .height = rendering.shading_rate_attachment->texel_size.height};
        information.pNext = &shading_rate;
    }
    m_functions.vkCmdBeginRendering(m_command_buffer, &information);
    if (!rendering.debug_name.empty())
    {
        record_begin_debug_label(rendering.debug_name, {0.4F, 0.6F, 1.0F, 1.0F});
    }
}

auto
VulkanCommandBuffer::record_begin_parallel_rendering(const RenderingDescription& rendering) -> RenderingInheritance
{
    // The pass's content comes from secondary command buffers; they need its formats.
    RenderingInheritance inheritance;
    inheritance.primary = m_handle;
    inheritance.pass = m_parallel_passes++;
    const VkRenderingInfo information = rendering_information(rendering, VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT, inheritance);
    m_functions.vkCmdBeginRendering(m_command_buffer, &information);
    m_current_parallel_pass = inheritance.pass;
    return inheritance;
}

void
VulkanCommandBuffer::record_end_rendering() noexcept
{
    m_functions.vkCmdEndRendering(m_command_buffer);
    m_current_parallel_pass.reset();
}

void
VulkanCommandBuffer::record_execute_commands(std::span<const CommandBufferHandle> secondary_command_buffers)
{
    static constexpr std::string_view OPERATION = "ParallelRenderingScope::execute";
    std::array<VkCommandBuffer, 64> natives{};
    std::size_t count = 0;
    for (const CommandBufferHandle handle : secondary_command_buffers)
    {
        // Each part: a recorded secondary command buffer of this pass.
        const CommandBufferRecord& record = resolve(m_device.handles().command_buffers, handle, OPERATION);
        if (validates(ValidationLevel::COMMANDS))
        {
            if (record.level != CommandBufferLevel::SECONDARY || !record.status->executable.load(std::memory_order_acquire))
            {
                fail(OPERATION, std::format("{} is not a recorded secondary command buffer{}", handle,
                                            record.status->failure.empty() ? std::string{} : std::format(" ({})", record.status->failure)),
                     "record each part with a RenderingPartRecorder that has gone out of scope");
            }
            if (record.status->inherited_primary != m_handle || !m_current_parallel_pass ||
                record.status->inherited_pass != *m_current_parallel_pass)
            {
                fail(OPERATION, std::format("{} was recorded for another rendering pass", handle),
                     "record parts with the inheritance() of the ParallelRenderingScope that executes them");
            }
        }
        natives[count++] = record.command_buffer;
        if (count == natives.size())
        {
            m_functions.vkCmdExecuteCommands(m_command_buffer, static_cast<std::uint32_t>(count), natives.data());
            count = 0;
        }
    }
    if (count != 0)
    {
        m_functions.vkCmdExecuteCommands(m_command_buffer, static_cast<std::uint32_t>(count), natives.data());
    }
}

void
VulkanCommandBuffer::record_set_viewport(const Viewport& viewport)
{
    const VkViewport native{.x = viewport.x,
                            .y = viewport.y,
                            .width = viewport.width,
                            .height = viewport.height,
                            .minDepth = viewport.minimum_depth,
                            .maxDepth = viewport.maximum_depth};
    m_functions.vkCmdSetViewport(m_command_buffer, 0, 1, &native);
}

void
VulkanCommandBuffer::record_set_scissor(const Rectangle& scissor)
{
    const VkRect2D native = to_vulkan(scissor);
    m_functions.vkCmdSetScissor(m_command_buffer, 0, 1, &native);
}

void
VulkanCommandBuffer::record_bind_vertex_buffers(std::uint32_t first_binding,
                                                std::span<const VertexBufferBinding> buffers)
{
    static constexpr std::string_view OPERATION = "RenderingScope::bind_vertex_buffers";
    std::array<VkBuffer, MAXIMUM_VERTEX_INPUT_BINDINGS> natives{};
    std::array<VkDeviceSize, MAXIMUM_VERTEX_INPUT_BINDINGS> offsets{};
    if (buffers.size() > natives.size())
    {
        fail(OPERATION, std::format("{} vertex buffers are bound at once; at most {} are", buffers.size(), natives.size()));
    }
    for (std::size_t index = 0; index < buffers.size(); ++index)
    {
        natives[index] = resolve(m_device.handles().buffers, buffers[index].buffer, OPERATION).buffer;
        offsets[index] = buffers[index].offset;
    }
    m_functions.vkCmdBindVertexBuffers(m_command_buffer, first_binding, static_cast<std::uint32_t>(buffers.size()), natives.data(),
                                       offsets.data());
}

void
VulkanCommandBuffer::record_bind_index_buffer(BufferHandle buffer,
                                              std::uint64_t offset,
                                              IndexType type)
{
    const BufferRecord& record = resolve(m_device.handles().buffers, buffer, "RenderingScope::bind_index_buffer");
    m_functions.vkCmdBindIndexBuffer(m_command_buffer, record.buffer, offset, to_vulkan(type));
}

void
VulkanCommandBuffer::record_draw(const DrawArguments& arguments)
{
    m_functions.vkCmdDraw(m_command_buffer, arguments.vertex_count, arguments.instance_count, arguments.first_vertex,
                          arguments.first_instance);
}

void
VulkanCommandBuffer::record_draw_indexed(const DrawIndexedArguments& arguments)
{
    m_functions.vkCmdDrawIndexed(m_command_buffer, arguments.index_count, arguments.instance_count, arguments.first_index,
                                 arguments.vertex_offset, arguments.first_instance);
}

void
VulkanCommandBuffer::record_draw_indirect(const IndirectDrawArguments& arguments)
{
    static constexpr std::string_view OPERATION = "RenderingScope::draw_indirect";
    check_draw_count(arguments.draw_count, OPERATION);
    const BufferRecord& record = resolve(m_device.handles().buffers, arguments.buffer, OPERATION);
    m_functions.vkCmdDrawIndirect(m_command_buffer, record.buffer, arguments.offset, arguments.draw_count,
                                  stride_of(arguments, sizeof(DrawArguments)));
}

void
VulkanCommandBuffer::record_draw_indexed_indirect(const IndirectDrawArguments& arguments)
{
    static constexpr std::string_view OPERATION = "RenderingScope::draw_indexed_indirect";
    check_draw_count(arguments.draw_count, OPERATION);
    const BufferRecord& record = resolve(m_device.handles().buffers, arguments.buffer, OPERATION);
    m_functions.vkCmdDrawIndexedIndirect(m_command_buffer, record.buffer, arguments.offset, arguments.draw_count,
                                         stride_of(arguments, sizeof(DrawIndexedArguments)));
}

void
VulkanCommandBuffer::record_draw_indirect_count(const IndirectDrawArguments& arguments,
                                                const IndirectCount& count)
{
    static constexpr std::string_view OPERATION = "RenderingScope::draw_indirect_count";
    require(DeviceFeature::DRAW_INDIRECT_COUNT, OPERATION);
    const BufferRecord& records = resolve(m_device.handles().buffers, arguments.buffer, OPERATION);
    const BufferRecord& counter = resolve(m_device.handles().buffers, count.buffer, OPERATION);
    m_functions.vkCmdDrawIndirectCount(m_command_buffer, records.buffer, arguments.offset, counter.buffer, count.offset,
                                       count.maximum_draw_count, stride_of(arguments, sizeof(DrawArguments)));
}

void
VulkanCommandBuffer::record_draw_indexed_indirect_count(const IndirectDrawArguments& arguments,
                                                        const IndirectCount& count)
{
    static constexpr std::string_view OPERATION = "RenderingScope::draw_indexed_indirect_count";
    require(DeviceFeature::DRAW_INDIRECT_COUNT, OPERATION);
    const BufferRecord& records = resolve(m_device.handles().buffers, arguments.buffer, OPERATION);
    const BufferRecord& counter = resolve(m_device.handles().buffers, count.buffer, OPERATION);
    m_functions.vkCmdDrawIndexedIndirectCount(m_command_buffer, records.buffer, arguments.offset, counter.buffer, count.offset,
                                              count.maximum_draw_count, stride_of(arguments, sizeof(DrawIndexedArguments)));
}

void
VulkanCommandBuffer::record_draw_mesh_tasks(const DrawMeshTasksArguments& arguments)
{
    require(DeviceFeature::MESH_SHADER, "RenderingScope::draw_mesh_tasks");
    m_functions.vkCmdDrawMeshTasksEXT(m_command_buffer, arguments.group_count_x, arguments.group_count_y, arguments.group_count_z);
}

void
VulkanCommandBuffer::record_draw_mesh_tasks_indirect(const IndirectDrawArguments& arguments)
{
    static constexpr std::string_view OPERATION = "RenderingScope::draw_mesh_tasks_indirect";
    require(DeviceFeature::MESH_SHADER, OPERATION);
    check_draw_count(arguments.draw_count, OPERATION);
    const BufferRecord& record = resolve(m_device.handles().buffers, arguments.buffer, OPERATION);
    m_functions.vkCmdDrawMeshTasksIndirectEXT(m_command_buffer, record.buffer, arguments.offset, arguments.draw_count,
                                              stride_of(arguments, sizeof(DrawMeshTasksArguments)));
}

void
VulkanCommandBuffer::record_draw_mesh_tasks_indirect_count(const IndirectDrawArguments& arguments,
                                                           const IndirectCount& count)
{
    static constexpr std::string_view OPERATION = "RenderingScope::draw_mesh_tasks_indirect_count";
    require(DeviceFeature::MESH_SHADER, OPERATION);
    require(DeviceFeature::DRAW_INDIRECT_COUNT, OPERATION);
    const BufferRecord& records = resolve(m_device.handles().buffers, arguments.buffer, OPERATION);
    const BufferRecord& counter = resolve(m_device.handles().buffers, count.buffer, OPERATION);
    m_functions.vkCmdDrawMeshTasksIndirectCountEXT(m_command_buffer, records.buffer, arguments.offset, counter.buffer, count.offset,
                                                   count.maximum_draw_count, stride_of(arguments, sizeof(DrawMeshTasksArguments)));
}

void
VulkanCommandBuffer::record_clear_attachments(std::span<const ClearAttachment> attachments,
                                              std::span<const ClearRectangle> rectangles)
{
    // The attachments with their clear values, then the rectangles, translated into reused storage.
    std::vector<VkClearAttachment>& native_attachments = reuse(m_clear_attachments, attachments.size());
    for (const ClearAttachment& attachment : attachments)
    {
        VkClearValue value{};
        if (has_any(attachment.aspects, ImageAspect::COLOR))
        {
            value.color = {.float32 = {attachment.color[0], attachment.color[1], attachment.color[2], attachment.color[3]}};
        }
        else
        {
            value.depthStencil = {.depth = attachment.depth, .stencil = attachment.stencil};
        }
        native_attachments.push_back(
            {.aspectMask = to_vulkan(attachment.aspects), .colorAttachment = attachment.color_attachment, .clearValue = value});
    }
    std::vector<VkClearRect>& native_rectangles = reuse(m_clear_rectangles, rectangles.size());
    for (const ClearRectangle& rectangle : rectangles)
    {
        native_rectangles.push_back({.rect = to_vulkan(rectangle.area), .baseArrayLayer = rectangle.base_layer, .layerCount = rectangle.layer_count});
    }
    m_functions.vkCmdClearAttachments(m_command_buffer, static_cast<std::uint32_t>(native_attachments.size()), native_attachments.data(),
                                      static_cast<std::uint32_t>(native_rectangles.size()), native_rectangles.data());
}

// ---------------------------------------------------------------------------------------------------
// VulkanCommandBuffer: dynamic state
// ---------------------------------------------------------------------------------------------------

void
VulkanCommandBuffer::record_set_stencil_reference(StencilFace faces,
                                                  std::uint32_t reference)
{
    m_functions.vkCmdSetStencilReference(m_command_buffer, to_vulkan(faces), reference);
}

void
VulkanCommandBuffer::record_set_blend_constants(const std::array<float, 4>& constants)
{
    m_functions.vkCmdSetBlendConstants(m_command_buffer, constants.data());
}

void
VulkanCommandBuffer::record_set_depth_bias(float constant_factor,
                                           float clamp,
                                           float slope_factor)
{
    m_functions.vkCmdSetDepthBias(m_command_buffer, constant_factor, clamp, slope_factor);
}

void
VulkanCommandBuffer::record_set_depth_bounds(float minimum,
                                             float maximum)
{
    m_functions.vkCmdSetDepthBounds(m_command_buffer, minimum, maximum);
}

void
VulkanCommandBuffer::record_set_line_width(float width)
{
    m_functions.vkCmdSetLineWidth(m_command_buffer, width);
}

void
VulkanCommandBuffer::record_set_cull_mode(CullMode mode)
{
    m_functions.vkCmdSetCullMode(m_command_buffer, to_vulkan(mode));
}

void
VulkanCommandBuffer::record_set_front_face(FrontFace front_face)
{
    m_functions.vkCmdSetFrontFace(m_command_buffer, to_vulkan(front_face));
}

void
VulkanCommandBuffer::record_set_primitive_topology(PrimitiveTopology topology)
{
    m_functions.vkCmdSetPrimitiveTopology(m_command_buffer, to_vulkan(topology));
}

void
VulkanCommandBuffer::record_set_depth_test(bool enable)
{
    m_functions.vkCmdSetDepthTestEnable(m_command_buffer, enable ? VK_TRUE : VK_FALSE);
}

void
VulkanCommandBuffer::record_set_depth_write(bool enable)
{
    m_functions.vkCmdSetDepthWriteEnable(m_command_buffer, enable ? VK_TRUE : VK_FALSE);
}

void
VulkanCommandBuffer::record_set_depth_compare(CompareOperation operation)
{
    m_functions.vkCmdSetDepthCompareOp(m_command_buffer, to_vulkan(operation));
}

void
VulkanCommandBuffer::record_set_stencil_test(bool enable)
{
    m_functions.vkCmdSetStencilTestEnable(m_command_buffer, enable ? VK_TRUE : VK_FALSE);
}

void
VulkanCommandBuffer::record_set_stencil_operation(StencilFace faces,
                                                  StencilOperation fail_operation,
                                                  StencilOperation pass,
                                                  StencilOperation depth_fail,
                                                  CompareOperation compare)
{
    m_functions.vkCmdSetStencilOp(m_command_buffer, to_vulkan(faces), to_vulkan(fail_operation), to_vulkan(pass), to_vulkan(depth_fail),
                                  to_vulkan(compare));
}

void
VulkanCommandBuffer::record_set_fragment_shading_rate(ShadingRate rate,
                                                      const std::array<ShadingRateCombiner, 2>& combiners)
{
    require(DeviceFeature::VARIABLE_RATE_SHADING, "RenderingScope::set_fragment_shading_rate");
    const VkExtent2D size = to_vulkan(rate);
    const std::array<VkFragmentShadingRateCombinerOpKHR, 2> native{to_vulkan(combiners[0]), to_vulkan(combiners[1])};
    m_functions.vkCmdSetFragmentShadingRateKHR(m_command_buffer, &size, native.data());
}

// ---------------------------------------------------------------------------------------------------
// VulkanCommandBuffer: queries and labels
// ---------------------------------------------------------------------------------------------------

void
VulkanCommandBuffer::record_reset_query_pool(QueryPoolHandle pool,
                                             std::uint32_t first,
                                             std::uint32_t count)
{
    const QueryPoolRecord& record = resolve(m_device.handles().query_pools, pool, "Recorder::reset_query_pool");
    m_functions.vkCmdResetQueryPool(m_command_buffer, record.pool, first, count);
}

void
VulkanCommandBuffer::record_write_timestamp(QueryPoolHandle pool,
                                            PipelineStage stage,
                                            std::uint32_t query)
{
    const QueryPoolRecord& record = resolve(m_device.handles().query_pools, pool, "Recorder::write_timestamp");
    const VkPipelineStageFlags2 native = stages(stage);
    m_functions.vkCmdWriteTimestamp2(m_command_buffer, native != 0 ? native : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, record.pool, query);
}

void
VulkanCommandBuffer::record_begin_query(QueryPoolHandle pool,
                                        std::uint32_t query)
{
    const QueryPoolRecord& record = resolve(m_device.handles().query_pools, pool, "Recorder::begin_query");
    m_functions.vkCmdBeginQuery(m_command_buffer, record.pool, query, 0);
}

void
VulkanCommandBuffer::record_end_query(QueryPoolHandle pool,
                                      std::uint32_t query)
{
    const QueryPoolRecord& record = resolve(m_device.handles().query_pools, pool, "Recorder::end_query");
    m_functions.vkCmdEndQuery(m_command_buffer, record.pool, query);
}

void
VulkanCommandBuffer::record_begin_debug_label(std::string_view name,
                                              const std::array<float, 4>& color)
{
    // Breadcrumbs: a checkpoint naming the label, inside the labels still open.
    if (m_breadcrumbs_enabled)
    {
        const BreadcrumbLabel& node = m_breadcrumbs->emplace_back(
            BreadcrumbLabel{.name = std::string{name}, .parent = m_label_stack.empty() ? nullptr : m_label_stack.back(), .opened = nullptr});
        m_label_stack.push_back(&node);
        m_functions.vkCmdSetCheckpointNV(m_command_buffer, &node);
    }

    // Labels reach debugging tools through VK_EXT_debug_utils, their names null-terminated in reused storage.
    ++m_open_labels;
    if (m_device.vulkan_physical_device().instance().has_debug_utils() && vkCmdBeginDebugUtilsLabelEXT != nullptr)
    {
        m_label_name.assign(name);
        const VkDebugUtilsLabelEXT label{.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
                                         .pLabelName = m_label_name.c_str(),
                                         .color = {color[0], color[1], color[2], color[3]}};
        vkCmdBeginDebugUtilsLabelEXT(m_command_buffer, &label);
    }
}

void
VulkanCommandBuffer::record_end_debug_label()
{
    if (m_open_labels == 0)
    {
        return;
    }
    --m_open_labels;
    if (m_breadcrumbs_enabled && !m_label_stack.empty())
    {
        const BreadcrumbLabel* opened = m_label_stack.back();
        m_label_stack.pop_back();
        const BreadcrumbLabel& node =
            m_breadcrumbs->emplace_back(BreadcrumbLabel{.name = opened->name, .parent = opened->parent, .opened = opened});
        m_functions.vkCmdSetCheckpointNV(m_command_buffer, &node);
    }
    if (m_device.vulkan_physical_device().instance().has_debug_utils() && vkCmdEndDebugUtilsLabelEXT != nullptr)
    {
        vkCmdEndDebugUtilsLabelEXT(m_command_buffer);
    }
}

// ---------------------------------------------------------------------------------------------------
// VulkanCommandBuffer: ray tracing
// ---------------------------------------------------------------------------------------------------

auto
VulkanCommandBuffer::region_of(const ShaderBindingTableRegion& region,
                               std::string_view operation) -> VkStridedDeviceAddressRegionKHR
{
    if (!region.buffer.is_valid())
    {
        return {};
    }
    const BufferRecord& buffer = resolve(m_device.handles().buffers, region.buffer, operation);
    return {.deviceAddress = buffer.address + region.offset, .stride = region.stride, .size = region.size};
}

void
VulkanCommandBuffer::record_trace_rays(const ShaderBindingTable& table,
                                       std::uint32_t width,
                                       std::uint32_t height,
                                       std::uint32_t depth)
{
    static constexpr std::string_view OPERATION = "Recorder::trace_rays";
    require(DeviceFeature::RAY_TRACING, OPERATION);
    const VkStridedDeviceAddressRegionKHR ray_generation = region_of(table.ray_generation, OPERATION);
    const VkStridedDeviceAddressRegionKHR miss = region_of(table.miss, OPERATION);
    const VkStridedDeviceAddressRegionKHR hit = region_of(table.hit, OPERATION);
    const VkStridedDeviceAddressRegionKHR callable = region_of(table.callable, OPERATION);
    m_functions.vkCmdTraceRaysKHR(m_command_buffer, &ray_generation, &miss, &hit, &callable, width, height, depth);
}

void
VulkanCommandBuffer::record_trace_rays_indirect(const ShaderBindingTable& table,
                                                BufferHandle buffer,
                                                std::uint64_t offset)
{
    static constexpr std::string_view OPERATION = "Recorder::trace_rays_indirect";
    require(DeviceFeature::RAY_TRACING, OPERATION);
    const VkStridedDeviceAddressRegionKHR ray_generation = region_of(table.ray_generation, OPERATION);
    const VkStridedDeviceAddressRegionKHR miss = region_of(table.miss, OPERATION);
    const VkStridedDeviceAddressRegionKHR hit = region_of(table.hit, OPERATION);
    const VkStridedDeviceAddressRegionKHR callable = region_of(table.callable, OPERATION);
    const BufferRecord& arguments = resolve(m_device.handles().buffers, buffer, OPERATION);
    if (arguments.address == 0)
    {
        fail(OPERATION, "the arguments buffer has no device address", "create it with BufferUsage::SHADER_DEVICE_ADDRESS");
    }
    m_functions.vkCmdTraceRaysIndirectKHR(m_command_buffer, &ray_generation, &miss, &hit, &callable, arguments.address + offset);
}

// ---------------------------------------------------------------------------------------------------
// Acceleration structures
// ---------------------------------------------------------------------------------------------------

namespace
{

/// @brief Native geometries of a build: its triangles, its bounding boxes and, at the top level, its instances.
[[nodiscard]] auto
geometry_count(const AccelerationStructureBuildDescription& description) noexcept -> std::size_t
{
    return description.triangles.size() + description.bounding_boxes.size() +
           (description.type == AccelerationStructureType::TOP_LEVEL ? 1 : 0);
}

/// @brief Storage for @p Capacity elements inside the object, or on the heap for a call that needs more:
///        common calls allocate nothing, without storage that outlives them.
template<class Element, std::size_t Capacity>
class SmallArray
{
public:
    explicit SmallArray(std::size_t size)
        : m_size{size}
    {
        if (size > Capacity)
        {
            m_heap.resize(size);
        }
    }

    SmallArray(const SmallArray&) = delete;
    auto operator=(const SmallArray&) -> SmallArray& = delete;

    [[nodiscard]] auto elements() noexcept -> std::span<Element>
    {
        return m_size > Capacity ? std::span<Element>{m_heap} : std::span<Element>{m_inline}.first(m_size);
    }

private:
    std::size_t m_size{};
    std::array<Element, Capacity> m_inline{};
    std::vector<Element> m_heap{};
};

/// @brief Device address of @p buffer at @p offset (0 for a null handle).
auto
address_of(VulkanDevice& device,
           BufferHandle buffer,
           std::uint64_t offset,
           std::string_view operation) -> VkDeviceAddress
{
    if (!buffer.is_valid())
    {
        return 0;
    }
    const BufferRecord& record = device.handles().buffers.get(buffer, operation);
    if (record.address == 0)
    {
        throw InvalidUsageError({
            .backend = BackendKind::VULKAN,
            .operation = std::string{operation},
            .problem = std::format("{} has no device address", buffer),
            .suggestion = "create build inputs with BufferUsage::ACCELERATION_STRUCTURE_BUILD_INPUT and scratch buffers with "
                          "BufferUsage::STORAGE | BufferUsage::SHADER_DEVICE_ADDRESS",
        });
    }
    return record.address + offset;
}

/// @brief Translates @p description into @p geometries and @p ranges (geometry_count() elements each, owned
///        by the caller) and returns the build, which points at @p geometries; with @p for_sizes, the
///        structures and scratch are not resolved.
auto
build_geometry(VulkanDevice& device,
               const AccelerationStructureBuildDescription& description,
               bool for_sizes,
               std::string_view operation,
               std::span<VkAccelerationStructureGeometryKHR> geometries,
               std::span<VkAccelerationStructureBuildRangeInfoKHR> ranges) -> VkAccelerationStructureBuildGeometryInfoKHR
{
    // Each geometry with its range: the primitives it holds.
    std::size_t next = 0;
    const auto add = [&](const VkAccelerationStructureGeometryKHR& geometry, std::uint32_t primitive_count)
    {
        geometries[next] = geometry;
        ranges[next] = {.primitiveCount = primitive_count};
        ++next;
    };

    // Triangles: vertex positions, optional indices and transform.
    for (const TriangleGeometry& triangles : description.triangles)
    {
        const bool indexed = triangles.index_buffer.is_valid();
        VkAccelerationStructureGeometryKHR geometry{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                                                    .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
                                                    .flags = to_vulkan(triangles.options)};
        geometry.geometry.triangles = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat = to_vulkan(triangles.vertex_format),
            .vertexData = {.deviceAddress = for_sizes ? 0 : address_of(device, triangles.vertex_buffer, triangles.vertex_offset, operation)},
            .vertexStride = triangles.vertex_stride,
            .maxVertex = triangles.vertex_count == 0 ? 0 : triangles.vertex_count - 1,
            .indexType = indexed ? to_vulkan(triangles.index_type) : VK_INDEX_TYPE_NONE_KHR,
            .indexData = {.deviceAddress = for_sizes || !indexed ? 0 : address_of(device, triangles.index_buffer, triangles.index_offset, operation)},
            .transformData = {.deviceAddress = for_sizes ? 0 : address_of(device, triangles.transform_buffer, triangles.transform_offset, operation)},
        };
        add(geometry, (indexed ? triangles.index_count : triangles.vertex_count) / 3);
    }

    // Bounding boxes of procedural geometry.
    for (const BoundingBoxGeometry& boxes : description.bounding_boxes)
    {
        VkAccelerationStructureGeometryKHR geometry{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                                                    .geometryType = VK_GEOMETRY_TYPE_AABBS_KHR,
                                                    .flags = to_vulkan(boxes.options)};
        geometry.geometry.aabbs = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR,
            .data = {.deviceAddress = for_sizes ? 0 : address_of(device, boxes.buffer, boxes.offset, operation)},
            .stride = boxes.stride,
        };
        add(geometry, boxes.count);
    }

    // Instances of a top-level structure.
    if (description.type == AccelerationStructureType::TOP_LEVEL)
    {
        VkAccelerationStructureGeometryKHR geometry{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                                                    .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR};
        geometry.geometry.instances = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .arrayOfPointers = VK_FALSE,
            .data = {.deviceAddress = for_sizes ? 0
                                                : address_of(device, description.instances.buffer, description.instances.offset, operation)},
        };
        add(geometry, description.instances.count);
    }

    // The build itself.
    HandleTables& handles = device.handles();
    VkAccelerationStructureBuildGeometryInfoKHR information{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = to_vulkan(description.type),
        .flags = to_vulkan(description.options),
        .mode = description.mode == AccelerationStructureBuildMode::UPDATE ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                                                                           : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = static_cast<std::uint32_t>(next),
        .pGeometries = geometries.data(),
    };
    if (!for_sizes)
    {
        information.srcAccelerationStructure =
            description.mode == AccelerationStructureBuildMode::UPDATE
                ? handles.acceleration_structures.get(description.source, operation).structure
                : VK_NULL_HANDLE;
        information.dstAccelerationStructure = handles.acceleration_structures.get(description.destination, operation).structure;
        information.scratchData.deviceAddress = address_of(device, description.scratch_buffer, description.scratch_offset, operation);
    }
    return information;
}

/// @brief A bottom- or top-level acceleration structure in buffer storage.
class VulkanAccelerationStructure final : public AccelerationStructure
{
public:
    VulkanAccelerationStructure(VulkanDevice& device, const AccelerationStructureDescription& description);
    ~VulkanAccelerationStructure() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }

    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }

    void set_debug_name(std::string_view name) override
    {
        m_debug_name = name;
        m_device.name_object(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, m_structure, m_debug_name);
    }

    [[nodiscard]] auto handle() const noexcept -> AccelerationStructureHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto type() const noexcept -> AccelerationStructureType override
    {
        return m_type;
    }

    [[nodiscard]] auto size() const noexcept -> std::uint64_t override
    {
        return m_size;
    }

    [[nodiscard]] auto device_address() const noexcept -> std::uint64_t override
    {
        return m_address;
    }

    [[nodiscard]] auto bindless_index() const noexcept -> AccelerationStructureIndex override
    {
        return m_bindless_index;
    }

private:
    VulkanDevice& m_device;
    VkAccelerationStructureKHR m_structure = VK_NULL_HANDLE;
    AccelerationStructureType m_type;
    std::uint64_t m_size;
    VkDeviceAddress m_address = 0;
    AccelerationStructureIndex m_bindless_index;
    AccelerationStructureHandle m_handle;
    std::string m_debug_name;
};

/// @brief Throws unless the device builds acceleration structures.
void
require_acceleration_structures(const VulkanDevice& device,
                                std::string_view operation)
{
    if (!device.enabled(DeviceFeature::RAY_QUERY))
    {
        device.require(DeviceFeature::RAY_TRACING, operation);
    }
}

} // namespace

auto
VulkanDevice::acceleration_structure_build_sizes(const AccelerationStructureBuildDescription& description) const
    -> AccelerationStructureBuildSizes
{
    static constexpr std::string_view OPERATION = "Device::acceleration_structure_build_sizes";
    require_acceleration_structures(*this, OPERATION);

    // The driver sizes a build from its geometries and primitive counts alone (translated on the stack
    // unless the build has more geometries than SIZED_GEOMETRIES: sizing every frame allocates nothing).
    static constexpr std::size_t SIZED_GEOMETRIES = 16;
    const std::size_t count = geometry_count(description);
    SmallArray<VkAccelerationStructureGeometryKHR, SIZED_GEOMETRIES> geometries{count};
    SmallArray<VkAccelerationStructureBuildRangeInfoKHR, SIZED_GEOMETRIES> ranges{count};
    SmallArray<std::uint32_t, SIZED_GEOMETRIES> primitive_counts{count};
    const VkAccelerationStructureBuildGeometryInfoKHR information =
        build_geometry(const_cast<VulkanDevice&>(*this), description, true, OPERATION, geometries.elements(), ranges.elements());
    std::ranges::transform(ranges.elements(), primitive_counts.elements().begin(), &VkAccelerationStructureBuildRangeInfoKHR::primitiveCount);
    VkAccelerationStructureBuildSizesInfoKHR sizes{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    m_functions.vkGetAccelerationStructureBuildSizesKHR(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &information,
                                                        primitive_counts.elements().data(), &sizes);
    return {.structure_size = sizes.accelerationStructureSize,
            .build_scratch_size = sizes.buildScratchSize,
            .update_scratch_size = sizes.updateScratchSize};
}

auto
VulkanDevice::create_acceleration_structure(const AccelerationStructureDescription& description) -> std::unique_ptr<AccelerationStructure>
{
    require_acceleration_structures(*this, "Device::create_acceleration_structure");
    return std::make_unique<VulkanAccelerationStructure>(*this, description);
}

VulkanAccelerationStructure::VulkanAccelerationStructure(VulkanDevice& device,
                                                         const AccelerationStructureDescription& description)
    : AccelerationStructure{}
    , m_device{device}
    , m_type{description.type}
    , m_size{description.size}
    , m_debug_name{description.debug_name}
{
    set_validation_level(device.validation_level());
    static constexpr std::string_view OPERATION = "Device::create_acceleration_structure";

    // Storage: a range of a buffer with ACCELERATION_STRUCTURE_STORAGE usage, 256-byte aligned.
    const BufferRecord& buffer = device.handles().buffers.get(description.buffer, OPERATION);
    if (validates(ValidationLevel::CREATION) &&
        (!has_any(buffer.usage, BufferUsage::ACCELERATION_STRUCTURE_STORAGE) || description.offset % 256 != 0 ||
         description.offset + description.size > buffer.size || description.size == 0))
    {
        throw InvalidUsageError({
            .backend = BackendKind::VULKAN,
            .operation = std::string{OPERATION},
            .problem = std::format("acceleration structure '{}' of {} bytes at offset {} does not fit its storage: a buffer with "
                                   "ACCELERATION_STRUCTURE_STORAGE usage (it has {}), at a multiple of 256, inside its {} bytes",
                                   m_debug_name, description.size, description.offset, buffer.usage, buffer.size),
            .suggestion = "size it with Device::acceleration_structure_build_sizes()",
        });
    }
    const VkAccelerationStructureCreateInfoKHR information{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = buffer.buffer,
        .offset = description.offset,
        .size = description.size,
        .type = to_vulkan(description.type),
    };
    device.check_result(device.functions().vkCreateAccelerationStructureKHR(device.native(), &information, nullptr, &m_structure), OPERATION,
                        "vkCreateAccelerationStructureKHR");

    // Its address (for instances), its bindless index (top level), its handle.
    const VkAccelerationStructureDeviceAddressInfoKHR address{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
                                                              .accelerationStructure = m_structure};
    m_address = device.functions().vkGetAccelerationStructureDeviceAddressKHR(device.native(), &address);
    if (description.bindless && description.type == AccelerationStructureType::TOP_LEVEL)
    {
        m_bindless_index = device.bindless().add_acceleration_structure(m_structure);
    }
    m_handle = device.handles().acceleration_structures.insert({.structure = m_structure, .address = m_address, .type = m_type});
    device.name_object(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, m_structure, m_debug_name);
}

VulkanAccelerationStructure::~VulkanAccelerationStructure()
{
    (void)m_device.handles().acceleration_structures.remove(m_handle);
    if (m_bindless_index.is_valid())
    {
        m_device.bindless().release_later(BindlessResourceKind::ACCELERATION_STRUCTURE, m_bindless_index.value());
    }
    m_device.vulkan_deleter().enqueue([device = &m_device, structure = m_structure]
    {
        device->functions().vkDestroyAccelerationStructureKHR(device->native(), structure, nullptr);
    });
}

void
VulkanCommandBuffer::record_build_acceleration_structures(std::span<const AccelerationStructureBuildDescription> builds)
{
    static constexpr std::string_view OPERATION = "Recorder::build_acceleration_structures";
    require_acceleration_structures(m_device, OPERATION);

    // Reused storage sized for every build: the geometries and ranges of each build follow the previous
    // build's, and nothing moves while builds point at them.
    std::size_t geometry_total = 0;
    for (const AccelerationStructureBuildDescription& build : builds)
    {
        geometry_total += geometry_count(build);
    }
    m_build_geometries.resize(geometry_total);
    m_build_ranges.resize(geometry_total);
    m_build_informations.resize(builds.size());
    m_build_range_pointers.resize(builds.size());

    // Every build at once: the GPU runs them in parallel.
    const std::uint32_t scratch_alignment =
        m_device.vulkan_physical_device().capabilities().acceleration_structure_properties.minAccelerationStructureScratchOffsetAlignment;
    std::size_t first_geometry = 0;
    for (std::size_t index = 0; index < builds.size(); ++index)
    {
        const std::size_t count = geometry_count(builds[index]);
        const std::span<VkAccelerationStructureGeometryKHR> geometries = std::span{m_build_geometries}.subspan(first_geometry, count);
        const std::span<VkAccelerationStructureBuildRangeInfoKHR> ranges = std::span{m_build_ranges}.subspan(first_geometry, count);
        try
        {
            m_build_informations[index] = build_geometry(m_device, builds[index], false, OPERATION, geometries, ranges);
        }
        catch (...)
        {
            mark_failed(OPERATION);
            throw;
        }
        if (validates(ValidationLevel::COMMANDS) && scratch_alignment != 0 &&
            m_build_informations[index].scratchData.deviceAddress % scratch_alignment != 0)
        {
            fail(OPERATION, std::format("the scratch memory of a build must be aligned to {} bytes", scratch_alignment),
                 "offset scratch ranges by multiples of the alignment");
        }
        m_build_range_pointers[index] = ranges.data();
        first_geometry += count;
    }
    m_functions.vkCmdBuildAccelerationStructuresKHR(m_command_buffer, static_cast<std::uint32_t>(builds.size()), m_build_informations.data(),
                                                    m_build_range_pointers.data());
}

void
VulkanCommandBuffer::record_copy_acceleration_structure(AccelerationStructureHandle source,
                                                        AccelerationStructureHandle destination,
                                                        AccelerationStructureCopyMode mode)
{
    static constexpr std::string_view OPERATION = "Recorder::copy_acceleration_structure";
    require_acceleration_structures(m_device, OPERATION);
    const VkCopyAccelerationStructureInfoKHR information{
        .sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR,
        .src = resolve(m_device.handles().acceleration_structures, source, OPERATION).structure,
        .dst = resolve(m_device.handles().acceleration_structures, destination, OPERATION).structure,
        .mode = to_vulkan(mode),
    };
    m_functions.vkCmdCopyAccelerationStructureKHR(m_command_buffer, &information);
}

void
VulkanCommandBuffer::record_write_acceleration_structure_properties(std::span<const AccelerationStructureHandle> structures,
                                                                    QueryPoolHandle pool,
                                                                    std::uint32_t first_query)
{
    static constexpr std::string_view OPERATION = "Recorder::write_acceleration_structure_properties";
    require_acceleration_structures(m_device, OPERATION);
    const QueryPoolRecord& queries = resolve(m_device.handles().query_pools, pool, OPERATION);

    // The structures, resolved into reused storage.
    std::vector<VkAccelerationStructureKHR>& natives = reuse(m_acceleration_structures, structures.size());
    for (const AccelerationStructureHandle structure : structures)
    {
        natives.push_back(resolve(m_device.handles().acceleration_structures, structure, OPERATION).structure);
    }
    m_functions.vkCmdWriteAccelerationStructuresPropertiesKHR(m_command_buffer, static_cast<std::uint32_t>(natives.size()), natives.data(),
                                                              VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, queries.pool,
                                                              first_query);
}

} // namespace gfx::vulkan
