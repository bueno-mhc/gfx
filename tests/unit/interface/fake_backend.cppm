// Test-only backend. It shows the backend contract: every operation of an interface must be
// overridden (the classes would stay abstract otherwise), and operations a backend does not implement
// explicitly forward to the interface's definition, which throws a descriptive BackendCapabilityError.
// Like a real backend, the fake device resolves handles to its objects (decoding them with its handle
// key) and calls the library's validators before creating shader modules and pipelines. Shader
// "reflection" reads layouts registered by the test, standing in for SPIR-V reflection. The fakes
// record what the API passes to the backend, so tests can check it.
export module gfx.tests.fake_backend;

import std;
import gfx;

export namespace gfx::tests
{

/// Common part of every fake: the NULL backend, a debug name and handle creation.
template<class Interface>
class Fake : public Interface
{
public:
    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::NULL_BACKEND;
    }

    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }

    void set_debug_name(std::string_view name) override
    {
        m_debug_name = name;
    }

    /// The validation level a backend passes from its instance (tests choose it explicitly).
    void use_validation_level(ValidationLevel level) noexcept
    {
        Interface::set_validation_level(level);
    }

protected:
    template<class Tag>
    [[nodiscard]] static auto make_handle(std::uint32_t index) -> Handle<Tag>
    {
        return Handle<Tag>::create(Interface::handle_key(), index, 1);
    }

protected:
    std::string m_debug_name = "fake";
};

// ================================================================================================= pipeline layout

class FakePipelineLayout final : public Fake<PipelineLayout>
{
public:
    FakePipelineLayout(std::uint32_t index, PipelineLayoutDefinition definition, std::string_view name = "fake")
        : m_index{index}
        , m_definition{std::move(definition)}
    {
        m_debug_name = name;
    }

    [[nodiscard]] auto handle() const noexcept -> PipelineLayoutHandle override
    {
        return make_handle<PipelineLayoutTag>(m_index);
    }

    [[nodiscard]] auto definition() const noexcept -> const PipelineLayoutDefinition& override
    {
        return m_definition;
    }

private:
    std::uint32_t m_index;
    PipelineLayoutDefinition m_definition;
};

/// The bindless pipeline layout of a device with default capacities and every kind of resource.
[[nodiscard]] inline auto default_bindless_layout() -> const FakePipelineLayout&
{
    static constexpr std::array KINDS{BindlessResourceKind::SAMPLED_IMAGE, BindlessResourceKind::STORAGE_IMAGE,
                                      BindlessResourceKind::SAMPLER, BindlessResourceKind::STORAGE_BUFFER,
                                      BindlessResourceKind::ACCELERATION_STRUCTURE};
    static const FakePipelineLayout layout{1000, bindless_pipeline_layout_definition(BindlessTableDescription{}, KINDS), "bindless"};
    return layout;
}

// ================================================================================================= command buffer

/// Records every command as a line of text.
class FakeCommandBuffer final : public Fake<CommandBuffer>
{
public:
    explicit FakeCommandBuffer(QueueType queue_type,
                               CommandBufferLevel level = CommandBufferLevel::PRIMARY,
                               std::uint32_t index = 1)
        : m_queue_type{queue_type}
        , m_level{level}
        , m_index{index}
    {}

    [[nodiscard]] auto handle() const noexcept -> CommandBufferHandle override
    {
        return make_handle<CommandBufferTag>(m_index);
    }

    [[nodiscard]] auto queue_type() const noexcept -> QueueType override
    {
        return m_queue_type;
    }

    [[nodiscard]] auto level() const noexcept -> CommandBufferLevel override
    {
        return m_level;
    }

    [[nodiscard]] auto bindless_pipeline_layout() const noexcept -> const PipelineLayout& override
    {
        return *m_bindless_layout;
    }

    void use_bindless_layout(const PipelineLayout& layout) noexcept
    {
        m_bindless_layout = &layout;
    }

    void reset() override
    {
        m_log.clear();
    }

    [[nodiscard]] auto log() const noexcept -> const std::vector<std::string>&
    {
        return m_log;
    }

    [[nodiscard]] auto push_constant_bytes() const noexcept -> const std::vector<std::byte>&
    {
        return m_push_constant_bytes;
    }

protected:
    void record_begin() override
    {
        m_log.emplace_back("begin");
    }

    void record_begin_rendering_part(const RenderingInheritance& inheritance) override
    {
        m_log.push_back(std::format("begin_rendering_part {} pass {}", inheritance.primary, inheritance.pass));
    }

    void record_end() noexcept override
    {
        m_log.emplace_back("end");
    }

    void record_barrier(const BarrierBatch& barriers) override
    {
        m_log.push_back(std::format("barrier {} {} {}", barriers.memory_barriers.size(), barriers.buffer_barriers.size(),
                                    barriers.image_barriers.size()));
    }

    void record_copy_buffer(BufferHandle /*source*/, BufferHandle /*destination*/, std::span<const BufferCopyRegion> regions) override
    {
        m_log.push_back(std::format("copy_buffer {}", regions.size()));
    }

    void record_copy_buffer_to_image(BufferHandle /*source*/, ImageHandle /*destination*/, ImageLayout layout,
                                     std::span<const BufferImageCopyRegion> /*regions*/) override
    {
        m_log.push_back(std::format("copy_buffer_to_image {}", layout));
    }

    void record_copy_image_to_buffer(ImageHandle /*source*/, ImageLayout layout, BufferHandle /*destination*/,
                                     std::span<const BufferImageCopyRegion> /*regions*/) override
    {
        m_log.push_back(std::format("copy_image_to_buffer {}", layout));
    }

    void record_copy_image(ImageHandle /*source*/, ImageLayout /*source_layout*/, ImageHandle /*destination*/,
                           ImageLayout /*destination_layout*/, std::span<const ImageCopyRegion> regions) override
    {
        m_log.push_back(std::format("copy_image {}", regions.size()));
    }

    void record_fill_buffer(BufferHandle /*buffer*/, std::uint64_t offset, std::uint64_t size, std::uint32_t value) override
    {
        m_log.push_back(std::format("fill_buffer {} {} {}", offset, size, value));
    }

    void record_update_buffer(BufferHandle /*buffer*/, std::uint64_t offset, std::span<const std::byte> data) override
    {
        m_log.push_back(std::format("update_buffer {} {}", offset, data.size()));
    }

    void record_clear_color_image(ImageHandle /*image*/, ImageLayout layout, const std::array<float, 4>& /*color*/,
                                  const ImageSubresourceRange& /*range*/) override
    {
        m_log.push_back(std::format("clear_color_image {}", layout));
    }

    void record_bind_compute_pipeline(ComputePipelineHandle pipeline) override
    {
        m_log.push_back(std::format("bind_compute_pipeline {}", pipeline));
    }

    void record_bind_graphics_pipeline(GraphicsPipelineHandle pipeline) override
    {
        m_log.push_back(std::format("bind_graphics_pipeline {}", pipeline));
    }

    void record_bind_descriptor_sets(PipelineBindPoint bind_point, PipelineLayoutHandle /*layout*/, std::uint32_t first_set,
                                     std::span<const DescriptorSetHandle> sets,
                                     std::span<const std::uint32_t> /*dynamic_offsets*/) override
    {
        m_log.push_back(std::format("bind_descriptor_sets {} {} {}", bind_point, first_set, sets.size()));
    }

    void record_push_constants(PipelineLayoutHandle /*layout*/, ShaderStage stages, std::uint32_t offset,
                               std::span<const std::byte> data) override
    {
        m_push_constant_bytes.assign(data.begin(), data.end());
        m_log.push_back(std::format("push_constants {} {} {}", stages, offset, data.size()));
    }

    void record_dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override
    {
        m_log.push_back(std::format("dispatch {} {} {}", x, y, z));
    }

    void record_dispatch_indirect(BufferHandle /*buffer*/, std::uint64_t offset) override
    {
        m_log.push_back(std::format("dispatch_indirect {}", offset));
    }

    void record_set_event(EventHandle /*event*/, const BarrierBatch& /*barriers*/) override
    {
        m_log.emplace_back("set_event");
    }

    void record_wait_events(std::span<const EventHandle> events, const BarrierBatch& /*barriers*/) override
    {
        m_log.push_back(std::format("wait_events {}", events.size()));
    }

    void record_reset_event(EventHandle /*event*/, PipelineStage stages) override
    {
        m_log.push_back(std::format("reset_event {}", stages));
    }

    void record_begin_rendering(const RenderingDescription& rendering) override
    {
        m_log.push_back(std::format("begin_rendering {}", rendering.color_attachments.size()));
    }

    [[nodiscard]] auto record_begin_parallel_rendering(const RenderingDescription& rendering) -> RenderingInheritance override
    {
        // A real backend reads the formats from the attachments' views; the fake leaves them UNDEFINED.
        m_log.push_back(std::format("begin_parallel_rendering {}", rendering.color_attachments.size()));
        return {.primary = handle(),
                .pass = m_parallel_passes++,
                .color_formats = {},
                .color_count = static_cast<std::uint32_t>(rendering.color_attachments.size()),
                .depth_format = Format::UNDEFINED,
                .stencil_format = Format::UNDEFINED,
                .samples = SampleCount::ONE};
    }

    void record_end_rendering() noexcept override
    {
        m_log.emplace_back("end_rendering");
    }

    void record_set_viewport(const Viewport& viewport) override
    {
        m_log.push_back(std::format("set_viewport {} {}", viewport.width, viewport.height));
    }

    void record_set_scissor(const Rectangle& scissor) override
    {
        m_log.push_back(std::format("set_scissor {} {}", scissor.width, scissor.height));
    }

    void record_bind_vertex_buffers(std::uint32_t first_binding, std::span<const VertexBufferBinding> buffers) override
    {
        m_log.push_back(std::format("bind_vertex_buffers {} {}", first_binding, buffers.size()));
    }

    void record_bind_index_buffer(BufferHandle /*buffer*/, std::uint64_t offset, IndexType type) override
    {
        m_log.push_back(std::format("bind_index_buffer {} {}", offset, type));
    }

    void record_draw(const DrawArguments& arguments) override
    {
        m_log.push_back(std::format("draw {} {}", arguments.vertex_count, arguments.instance_count));
    }

    void record_draw_indexed(const DrawIndexedArguments& arguments) override
    {
        m_log.push_back(std::format("draw_indexed {} {}", arguments.index_count, arguments.instance_count));
    }

    void record_draw_indirect(const IndirectDrawArguments& arguments) override
    {
        m_log.push_back(std::format("draw_indirect {}", arguments.draw_count));
    }

    void record_draw_indexed_indirect(const IndirectDrawArguments& arguments) override
    {
        m_log.push_back(std::format("draw_indexed_indirect {}", arguments.draw_count));
    }

    void record_reset_query_pool(QueryPoolHandle /*pool*/, std::uint32_t first, std::uint32_t count) override
    {
        m_log.push_back(std::format("reset_query_pool {} {}", first, count));
    }

    void record_write_timestamp(QueryPoolHandle /*pool*/, PipelineStage stage, std::uint32_t query) override
    {
        m_log.push_back(std::format("write_timestamp {} {}", stage, query));
    }

    void record_begin_query(QueryPoolHandle /*pool*/, std::uint32_t query) override
    {
        m_log.push_back(std::format("begin_query {}", query));
    }

    void record_end_query(QueryPoolHandle /*pool*/, std::uint32_t query) override
    {
        m_log.push_back(std::format("end_query {}", query));
    }

    void record_begin_debug_label(std::string_view name, const std::array<float, 4>& /*color*/) override
    {
        m_log.push_back(std::format("begin_debug_label {}", name));
    }

    void record_end_debug_label() override
    {
        m_log.emplace_back("end_debug_label");
    }

    void record_execute_commands(std::span<const CommandBufferHandle> secondary_command_buffers) override
    {
        std::string handles;
        for (const CommandBufferHandle handle : secondary_command_buffers)
        {
            handles += std::format(" {}", handle);
        }
        m_log.push_back(std::format("execute_commands{}", handles));
    }

    void record_blit_image(ImageHandle /*source*/, ImageLayout source_layout, ImageHandle /*destination*/, ImageLayout destination_layout,
                           std::span<const ImageBlitRegion> regions, Filter filter) override
    {
        for (const ImageBlitRegion& region : regions)
        {
            m_log.push_back(std::format("blit_image {} {} {} -> {} {} {}x{} {}", source_layout, region.source_subresource.mip_level,
                                        region.source_bounds[1].x, destination_layout, region.destination_subresource.mip_level,
                                        region.destination_bounds[1].x, region.destination_bounds[1].y, filter));
        }
    }

    void record_resolve_image(ImageHandle /*source*/, ImageLayout /*source_layout*/, ImageHandle /*destination*/,
                              ImageLayout /*destination_layout*/, std::span<const ImageResolveRegion> regions) override
    {
        m_log.push_back(std::format("resolve_image {}", regions.size()));
    }

    void record_clear_depth_stencil_image(ImageHandle /*image*/, ImageLayout layout, float depth, std::uint32_t stencil,
                                          const ImageSubresourceRange& /*range*/) override
    {
        m_log.push_back(std::format("clear_depth_stencil_image {} {} {}", layout, depth, stencil));
    }

    void record_clear_attachments(std::span<const ClearAttachment> attachments, std::span<const ClearRectangle> rectangles) override
    {
        m_log.push_back(std::format("clear_attachments {} {}", attachments.size(), rectangles.size()));
    }

    void record_set_stencil_reference(StencilFace faces, std::uint32_t reference) override
    {
        m_log.push_back(std::format("set_stencil_reference {} {}", faces, reference));
    }

    void record_set_blend_constants(const std::array<float, 4>& constants) override
    {
        m_log.push_back(std::format("set_blend_constants {}", constants[0]));
    }

    void record_set_depth_bias(float constant_factor, float clamp, float slope_factor) override
    {
        m_log.push_back(std::format("set_depth_bias {} {} {}", constant_factor, clamp, slope_factor));
    }

    void record_set_depth_bounds(float minimum, float maximum) override
    {
        m_log.push_back(std::format("set_depth_bounds {} {}", minimum, maximum));
    }

    void record_set_line_width(float width) override
    {
        m_log.push_back(std::format("set_line_width {}", width));
    }

    void record_set_cull_mode(CullMode mode) override
    {
        m_log.push_back(std::format("set_cull_mode {}", mode));
    }

    void record_set_front_face(FrontFace front_face) override
    {
        m_log.push_back(std::format("set_front_face {}", front_face));
    }

    void record_set_primitive_topology(PrimitiveTopology topology) override
    {
        m_log.push_back(std::format("set_primitive_topology {}", topology));
    }

    void record_set_depth_test(bool enable) override
    {
        m_log.push_back(std::format("set_depth_test {}", enable));
    }

    void record_set_depth_write(bool enable) override
    {
        m_log.push_back(std::format("set_depth_write {}", enable));
    }

    void record_set_depth_compare(CompareOperation operation) override
    {
        m_log.push_back(std::format("set_depth_compare {}", operation));
    }

    void record_set_stencil_test(bool enable) override
    {
        m_log.push_back(std::format("set_stencil_test {}", enable));
    }

    void record_set_stencil_operation(StencilFace faces, StencilOperation fail, StencilOperation pass, StencilOperation depth_fail,
                                      CompareOperation compare) override
    {
        m_log.push_back(std::format("set_stencil_operation {} {} {} {} {}", faces, fail, pass, depth_fail, compare));
    }

    void record_set_fragment_shading_rate(ShadingRate rate, const std::array<ShadingRateCombiner, 2>& combiners) override
    {
        m_log.push_back(std::format("set_fragment_shading_rate {} {} {}", rate, combiners[0], combiners[1]));
    }

    void record_draw_indirect_count(const IndirectDrawArguments& /*arguments*/, const IndirectCount& count) override
    {
        m_log.push_back(std::format("draw_indirect_count {}", count.maximum_draw_count));
    }

    void record_draw_indexed_indirect_count(const IndirectDrawArguments& /*arguments*/, const IndirectCount& count) override
    {
        m_log.push_back(std::format("draw_indexed_indirect_count {}", count.maximum_draw_count));
    }

    void record_draw_mesh_tasks(const DrawMeshTasksArguments& arguments) override
    {
        m_log.push_back(std::format("draw_mesh_tasks {} {} {}", arguments.group_count_x, arguments.group_count_y, arguments.group_count_z));
    }

    void record_draw_mesh_tasks_indirect(const IndirectDrawArguments& arguments) override
    {
        m_log.push_back(std::format("draw_mesh_tasks_indirect {}", arguments.draw_count));
    }

    void record_draw_mesh_tasks_indirect_count(const IndirectDrawArguments& /*arguments*/, const IndirectCount& count) override
    {
        m_log.push_back(std::format("draw_mesh_tasks_indirect_count {}", count.maximum_draw_count));
    }

    void record_bind_ray_tracing_pipeline(RayTracingPipelineHandle pipeline) override
    {
        m_log.push_back(std::format("bind_ray_tracing_pipeline {}", pipeline));
    }

    void record_trace_rays(const ShaderBindingTable& /*table*/, std::uint32_t width, std::uint32_t height, std::uint32_t depth) override
    {
        m_log.push_back(std::format("trace_rays {} {} {}", width, height, depth));
    }

    void record_trace_rays_indirect(const ShaderBindingTable& /*table*/, BufferHandle /*buffer*/, std::uint64_t offset) override
    {
        m_log.push_back(std::format("trace_rays_indirect {}", offset));
    }

    void record_build_acceleration_structures(std::span<const AccelerationStructureBuildDescription> builds) override
    {
        m_log.push_back(std::format("build_acceleration_structures {}", builds.size()));
    }

    void record_copy_acceleration_structure(AccelerationStructureHandle /*source*/, AccelerationStructureHandle /*destination*/,
                                            AccelerationStructureCopyMode mode) override
    {
        m_log.push_back(std::format("copy_acceleration_structure {}", mode));
    }

    void record_write_acceleration_structure_properties(std::span<const AccelerationStructureHandle> structures,
                                                        QueryPoolHandle /*pool*/, std::uint32_t first_query) override
    {
        m_log.push_back(std::format("write_acceleration_structure_properties {} {}", structures.size(), first_query));
    }

private:
    QueueType m_queue_type;
    CommandBufferLevel m_level;
    std::uint32_t m_index;
    const PipelineLayout* m_bindless_layout = &default_bindless_layout();
    std::uint32_t m_parallel_passes = 0;
    std::vector<std::string> m_log;
    std::vector<std::byte> m_push_constant_bytes;
};

// ================================================================================================= simple objects

class FakeShaderModule final : public Fake<ShaderModule>
{
public:
    FakeShaderModule(std::uint32_t index, ShaderStage stage, const ShaderLayout& layout, std::string_view name)
        : m_index{index}
        , m_stage{stage}
        , m_layout{layout}
    {
        m_debug_name = name;
    }

    [[nodiscard]] auto handle() const noexcept -> ShaderModuleHandle override
    {
        return make_handle<ShaderModuleTag>(m_index);
    }

    [[nodiscard]] auto stage() const noexcept -> ShaderStage override
    {
        return m_stage;
    }

    [[nodiscard]] auto layout() const noexcept -> const ShaderLayout& override
    {
        return m_layout;
    }

private:
    std::uint32_t m_index;
    ShaderStage m_stage;
    ShaderLayout m_layout;
};

class FakeDescriptorSetLayout final : public Fake<DescriptorSetLayout>
{
public:
    FakeDescriptorSetLayout(std::uint32_t index, std::span<const DescriptorSetLayoutBinding> bindings)
        : m_index{index}
        , m_bindings{bindings.begin(), bindings.end()}
    {}

    [[nodiscard]] auto handle() const noexcept -> DescriptorSetLayoutHandle override
    {
        return make_handle<DescriptorSetLayoutTag>(m_index);
    }

    [[nodiscard]] auto bindings() const noexcept -> std::span<const DescriptorSetLayoutBinding> override
    {
        return m_bindings;
    }

private:
    std::uint32_t m_index;
    std::vector<DescriptorSetLayoutBinding> m_bindings;
};

class FakeComputePipeline final : public Fake<ComputePipeline>
{
public:
    FakeComputePipeline(std::uint32_t index, PipelineLayoutHandle layout)
        : m_index{index}
        , m_layout{layout}
    {}

    [[nodiscard]] auto handle() const noexcept -> ComputePipelineHandle override
    {
        return make_handle<ComputePipelineTag>(m_index);
    }

    [[nodiscard]] auto layout() const noexcept -> PipelineLayoutHandle override
    {
        return m_layout;
    }

    [[nodiscard]] auto workgroup_size() const noexcept -> std::array<std::uint32_t, 3> override
    {
        return {1, 1, 1};
    }

    [[nodiscard]] auto status() const noexcept -> PipelineStatus override
    {
        return PipelineStatus::READY;
    }

    void wait() const override
    {
    }

private:
    std::uint32_t m_index;
    PipelineLayoutHandle m_layout;
};

class FakeGraphicsPipeline final : public Fake<GraphicsPipeline>
{
public:
    FakeGraphicsPipeline(std::uint32_t index, PipelineLayoutHandle layout)
        : m_index{index}
        , m_layout{layout}
    {}

    [[nodiscard]] auto handle() const noexcept -> GraphicsPipelineHandle override
    {
        return make_handle<GraphicsPipelineTag>(m_index);
    }

    [[nodiscard]] auto status() const noexcept -> PipelineStatus override
    {
        return PipelineStatus::READY;
    }

    void wait() const override
    {
    }

    [[nodiscard]] auto layout() const noexcept -> PipelineLayoutHandle override
    {
        return m_layout;
    }

private:
    std::uint32_t m_index;
    PipelineLayoutHandle m_layout;
};

/// Records the last descriptor writes.
class FakeDescriptorSet final : public Fake<DescriptorSet>
{
public:
    [[nodiscard]] auto handle() const noexcept -> DescriptorSetHandle override
    {
        return make_handle<DescriptorSetTag>(1);
    }

    [[nodiscard]] auto layout() const noexcept -> DescriptorSetLayoutHandle override
    {
        return make_handle<DescriptorSetLayoutTag>(1);
    }

    void update(std::span<const DescriptorWrite> writes) override
    {
        for (const DescriptorWrite& write : writes)
        {
            m_bindings.push_back(write.binding);
            m_resources.insert(m_resources.end(), write.resources.begin(), write.resources.end());
        }
    }

    [[nodiscard]] auto written_bindings() const noexcept -> const std::vector<std::uint32_t>&
    {
        return m_bindings;
    }

    [[nodiscard]] auto written_resources() const noexcept -> const std::vector<DescriptorResource>&
    {
        return m_resources;
    }

private:
    std::vector<std::uint32_t> m_bindings;
    std::vector<DescriptorResource> m_resources;
};

/// Host-visible buffer (64 bytes unless created by FakeDevice); everything else is left to the
/// interface's defaults. STORAGE buffers get bindless index = handle index.
class FakeBuffer final : public Fake<Buffer>
{
public:
    explicit FakeBuffer(std::uint64_t size = 64,
                        BufferUsage usage = BufferUsage::STORAGE,
                        std::uint32_t index = 1,
                        std::string_view name = "fake")
        : m_memory(static_cast<std::size_t>(size))
        , m_usage{usage}
        , m_index{index}
    {
        m_debug_name = name;
    }

    [[nodiscard]] auto handle() const noexcept -> BufferHandle override
    {
        return make_handle<BufferTag>(m_index);
    }

    [[nodiscard]] auto size() const noexcept -> std::uint64_t override
    {
        return m_memory.size();
    }

    [[nodiscard]] auto usage() const noexcept -> BufferUsage override
    {
        return m_usage;
    }

    [[nodiscard]] auto location() const noexcept -> MemoryLocation override
    {
        return MemoryLocation::HOST_UPLOAD;
    }

    [[nodiscard]] auto bindless_index() const noexcept -> StorageBufferIndex override
    {
        return has_any(m_usage, BufferUsage::STORAGE) ? StorageBufferIndex{m_index} : StorageBufferIndex{};
    }

    [[nodiscard]] auto mapped_memory() -> std::span<std::byte> override
    {
        return m_memory;
    }

    void flush_mapped_range(std::uint64_t offset, std::uint64_t size) override
    {
        Buffer::flush_mapped_range(offset, size);
    }

    void invalidate_mapped_range(std::uint64_t offset, std::uint64_t size) override
    {
        Buffer::invalidate_mapped_range(offset, size);
    }

    [[nodiscard]] auto device_address() const -> std::uint64_t override
    {
        return Buffer::device_address();
    }

private:
    std::vector<std::byte> m_memory;
    BufferUsage m_usage;
    std::uint32_t m_index;
};

class FakeDeleter final : public Fake<Deleter>
{
public:
    void enqueue(std::move_only_function<void()> deletion) override
    {
        Deleter::enqueue(std::move(deletion));
    }

    auto collect() -> std::size_t override
    {
        ++m_collections;
        return 0;
    }

    [[nodiscard]] auto collections() const noexcept -> std::uint32_t
    {
        return m_collections;
    }

    void flush() override
    {
        Deleter::flush();
    }

    [[nodiscard]] auto pending_count() const noexcept -> std::size_t override
    {
        return 0;
    }

private:
    std::uint32_t m_collections = 0;
};

/// Timestamp results the test provides.
class FakeQueryPool final : public Fake<QueryPool>
{
public:
    FakeQueryPool(std::uint32_t index, QueryType type, std::uint32_t count)
        : m_index{index}
        , m_type{type}
        , m_results(count)
    {}

    [[nodiscard]] auto handle() const noexcept -> QueryPoolHandle override
    {
        return make_handle<QueryPoolTag>(m_index);
    }

    [[nodiscard]] auto type() const noexcept -> QueryType override
    {
        return m_type;
    }

    [[nodiscard]] auto count() const noexcept -> std::uint32_t override
    {
        return static_cast<std::uint32_t>(m_results.size());
    }

    [[nodiscard]] auto results(std::uint32_t first, std::span<std::uint64_t> destination, bool /*wait*/) -> bool override
    {
        std::ranges::copy(std::span{m_results}.subspan(first, destination.size()), destination.begin());
        return true;
    }

    /// Sets the value query @p query will report.
    void set_result(std::uint32_t query, std::uint64_t value)
    {
        m_results[query] = value;
    }

private:
    std::uint32_t m_index;
    QueryType m_type;
    std::vector<std::uint64_t> m_results;
};

/// Allocates FakeCommandBuffers with increasing handle indices and counts resets.
class FakeCommandPool final : public Fake<CommandPool>
{
public:
    FakeCommandPool(std::uint32_t index, QueueType queue_type)
        : m_index{index}
        , m_queue_type{queue_type}
    {}

    [[nodiscard]] auto handle() const noexcept -> CommandPoolHandle override
    {
        return make_handle<CommandPoolTag>(m_index);
    }

    [[nodiscard]] auto queue_type() const noexcept -> QueueType override
    {
        return m_queue_type;
    }

    [[nodiscard]] auto allocate(CommandBufferLevel level, std::string_view /*debug_name*/) -> std::unique_ptr<CommandBuffer> override
    {
        ++m_allocation_count;
        return std::make_unique<FakeCommandBuffer>(m_queue_type, level, m_index * 100 + m_allocation_count);
    }

    void reset() override
    {
        ++m_reset_count;
    }

    [[nodiscard]] auto allocation_count() const noexcept -> std::uint32_t
    {
        return m_allocation_count;
    }

    [[nodiscard]] auto reset_count() const noexcept -> std::uint32_t
    {
        return m_reset_count;
    }

private:
    std::uint32_t m_index;
    QueueType m_queue_type;
    std::uint32_t m_allocation_count = 0;
    std::uint32_t m_reset_count = 0;
};

/// A timeline whose value the test sets; it records the values waited for.
class FakeTimelineSemaphore final : public Fake<TimelineSemaphore>
{
public:
    [[nodiscard]] auto handle() const noexcept -> TimelineSemaphoreHandle override
    {
        return make_handle<TimelineSemaphoreTag>(1);
    }

    [[nodiscard]] auto value() const -> std::uint64_t override
    {
        return m_value;
    }

    [[nodiscard]] auto wait(std::uint64_t value, std::chrono::nanoseconds /*timeout*/) const -> WaitStatus override
    {
        m_waited_values.push_back(value);
        return value <= m_value ? WaitStatus::SIGNALED : WaitStatus::TIMEOUT;
    }

    void signal(std::uint64_t value) override
    {
        m_value = value;
    }

    [[nodiscard]] auto waited_values() const noexcept -> const std::vector<std::uint64_t>&
    {
        return m_waited_values;
    }

private:
    std::uint64_t m_value = 0;
    mutable std::vector<std::uint64_t> m_waited_values;
};

// ================================================================================================= device

/// What the API passed to the fake device (copies: descriptions only borrow their data).
struct CapturedDescriptions
{
    std::vector<std::string> shader_module_names{};
    std::vector<std::vector<DescriptorSetLayoutBinding>> set_layout_bindings{};
    std::vector<DescriptorSetLayoutHandle> pipeline_layout_set_layouts{};
    std::vector<PushConstantRange> push_constant_ranges{};
    std::optional<ComputePipelineDescription> compute_pipeline{};
    std::optional<GraphicsPipelineDescription> graphics_pipeline{};
    std::vector<VertexInputAttribute> vertex_attributes{};
    std::vector<Format> color_formats{};
};

/// Implements what the shader, pipeline and command pool tests need; every other operation forwards to
/// Device's default. Objects it created must outlive the creations that name them.
class FakeDevice final : public Fake<Device>
{
public:
    explicit FakeDevice(const PhysicalDevice& physical_device)
        : m_physical_device{physical_device}
    {}

    [[nodiscard]] auto captured() const noexcept -> const CapturedDescriptions&
    {
        return m_captured;
    }

    /// Stands in for SPIR-V reflection: modules named @p name get @p layout.
    void add_reflected_layout(std::string name, ShaderLayout layout)
    {
        m_reflected_layouts.insert_or_assign(std::move(name), std::move(layout));
    }

    [[nodiscard]] auto physical_device() const noexcept -> const PhysicalDevice& override
    {
        return m_physical_device;
    }

    [[nodiscard]] auto queue(QueueType type, std::uint32_t index) -> Queue& override
    {
        return Device::queue(type, index);
    }

    [[nodiscard]] auto queue_count(QueueType /*type*/) const noexcept -> std::uint32_t override
    {
        return 0;
    }

    [[nodiscard]] auto deleter() noexcept -> Deleter& override
    {
        return m_deleter;
    }

    void wait_idle() override
    {
        Device::wait_idle();
    }

    [[nodiscard]] auto memory_requirements(const BufferDescription& description) const -> MemoryRequirements override
    {
        return Device::memory_requirements(description);
    }

    [[nodiscard]] auto memory_requirements(const ImageDescription& description) const -> MemoryRequirements override
    {
        return Device::memory_requirements(description);
    }

    [[nodiscard]] auto allocate_memory(const DeviceMemoryDescription& description) -> std::unique_ptr<DeviceMemory> override
    {
        return Device::allocate_memory(description);
    }

    [[nodiscard]] auto create_buffer(const BufferDescription& description) -> std::unique_ptr<Buffer> override
    {
        auto buffer = std::make_unique<FakeBuffer>(description.size, description.usage, ++m_next_index, description.debug_name);
        buffer->use_validation_level(validation_level());
        return buffer;
    }

    [[nodiscard]] auto create_image(const ImageDescription& description) -> std::unique_ptr<Image> override
    {
        return Device::create_image(description);
    }

    [[nodiscard]] auto create_image_view(const ImageViewDescription& description) -> std::unique_ptr<ImageView> override
    {
        return Device::create_image_view(description);
    }

    [[nodiscard]] auto create_sampler(const SamplerDescription& description) -> std::unique_ptr<Sampler> override
    {
        return Device::create_sampler(description);
    }

    [[nodiscard]] auto create_shader_module(const ShaderModuleDescription& description) -> std::unique_ptr<ShaderModule> override
    {
        // Validate first, then "reflect" (look the layout up by name).
        validate_shader_module_description(description, backend());
        const auto reflected = m_reflected_layouts.find(std::string{description.name});
        if (reflected == m_reflected_layouts.end())
        {
            throw InvalidUsageError({.backend = backend(),
                                     .operation = "FakeDevice::create_shader_module",
                                     .problem = std::format("the test registered no layout for '{}'", description.name),
                                     .suggestion = "call add_reflected_layout() first"});
        }
        m_captured.shader_module_names.emplace_back(description.name);
        auto shader_module = std::make_unique<FakeShaderModule>(++m_next_index, description.stage, reflected->second, description.name);
        m_shader_modules[m_next_index] = shader_module.get();
        return shader_module;
    }

    [[nodiscard]] auto create_descriptor_set_layout(const DescriptorSetLayoutDescription& description)
        -> std::unique_ptr<DescriptorSetLayout> override
    {
        m_captured.set_layout_bindings.emplace_back(description.bindings.begin(), description.bindings.end());
        auto set_layout = std::make_unique<FakeDescriptorSetLayout>(++m_next_index, description.bindings);
        m_set_layouts[m_next_index] = set_layout.get();
        return set_layout;
    }

    [[nodiscard]] auto create_pipeline_layout(const PipelineLayoutDescription& description) -> std::unique_ptr<PipelineLayout> override
    {
        m_captured.pipeline_layout_set_layouts.assign(description.set_layouts.begin(), description.set_layouts.end());
        m_captured.push_constant_ranges.assign(description.push_constant_ranges.begin(), description.push_constant_ranges.end());

        // Resolve the set layouts, then describe the layout the way every backend does.
        std::vector<const DescriptorSetLayout*> set_layouts;
        for (const DescriptorSetLayoutHandle handle : description.set_layouts)
        {
            set_layouts.push_back(resolve(m_set_layouts, handle));
        }
        auto pipeline_layout = std::make_unique<FakePipelineLayout>(
            ++m_next_index, make_pipeline_layout_definition(set_layouts, description.push_constant_ranges),
            description.debug_name.empty() ? std::string_view{"fake"} : description.debug_name);
        m_pipeline_layouts[m_next_index] = pipeline_layout.get();
        return pipeline_layout;
    }

    [[nodiscard]] auto create_compute_pipeline(const ComputePipelineDescription& description)
        -> std::unique_ptr<ComputePipeline> override
    {
        // Resolve (no layout = the bindless one) and validate, as every backend does.
        if (validates(ValidationLevel::CREATION))
        {
            validate_compute_pipeline(layout_of(description.layout), *resolve(m_shader_modules, description.shader),
                                      description.specialization_constants);
        }
        m_captured.compute_pipeline = description;
        return std::make_unique<FakeComputePipeline>(++m_next_index, layout_of(description.layout).handle());
    }

    [[nodiscard]] auto create_graphics_pipeline(const GraphicsPipelineDescription& description)
        -> std::unique_ptr<GraphicsPipeline> override
    {
        // Resolve (no layout = the bindless one) and validate, as every backend does.
        if (validates(ValidationLevel::CREATION))
        {
            validate_graphics_pipeline(layout_of(description.layout),
                                       {.vertex = resolve_optional(description.vertex_shader),
                                        .fragment = resolve_optional(description.fragment_shader),
                                        .task = resolve_optional(description.task_shader),
                                        .mesh = resolve_optional(description.mesh_shader),
                                        .geometry = resolve_optional(description.geometry_shader),
                                        .tessellation_control = resolve_optional(description.tessellation_control_shader),
                                        .tessellation_evaluation = resolve_optional(description.tessellation_evaluation_shader)},
                                       description);
        }
        m_captured.graphics_pipeline = description;
        m_captured.vertex_attributes.assign(description.vertex_attributes.begin(), description.vertex_attributes.end());
        m_captured.color_formats.assign(description.color_formats.begin(), description.color_formats.end());
        return std::make_unique<FakeGraphicsPipeline>(++m_next_index, layout_of(description.layout).handle());
    }

    [[nodiscard]] auto create_ray_tracing_pipeline(const RayTracingPipelineDescription& description)
        -> std::unique_ptr<RayTracingPipeline> override
    {
        // Validate, then report that the fake cannot build one.
        if (validates(ValidationLevel::CREATION))
        {
            validate_ray_tracing_pipeline(layout_of(description.layout), description,
                                          [this](ShaderModuleHandle handle) { return resolve_optional(handle); });
        }
        return Device::create_ray_tracing_pipeline(description);
    }

    [[nodiscard]] auto bindless_pipeline_layout() const noexcept -> const PipelineLayout& override
    {
        return default_bindless_layout();
    }

    [[nodiscard]] auto bindless_usage() const noexcept -> BindlessTableUsage override
    {
        return {};
    }

    [[nodiscard]] auto memory_budget() const -> std::vector<MemoryHeapBudget> override
    {
        return Device::memory_budget();
    }

    [[nodiscard]] auto memory_statistics() const -> MemoryStatistics override
    {
        return Device::memory_statistics();
    }

    [[nodiscard]] auto create_buffer_view(const BufferViewDescription& description) -> std::unique_ptr<BufferView> override
    {
        return Device::create_buffer_view(description);
    }

    [[nodiscard]] auto sparse_image_requirements(ImageHandle image) const -> SparseImageRequirements override
    {
        return Device::sparse_image_requirements(image);
    }

    [[nodiscard]] auto create_pipeline_cache(const PipelineCacheDescription& description) -> std::unique_ptr<PipelineCache> override
    {
        return Device::create_pipeline_cache(description);
    }

    [[nodiscard]] auto acceleration_structure_build_sizes(const AccelerationStructureBuildDescription& description) const
        -> AccelerationStructureBuildSizes override
    {
        return Device::acceleration_structure_build_sizes(description);
    }

    [[nodiscard]] auto create_acceleration_structure(const AccelerationStructureDescription& description)
        -> std::unique_ptr<AccelerationStructure> override
    {
        return Device::create_acceleration_structure(description);
    }

    [[nodiscard]] auto capture_tool() const noexcept -> CaptureTool override
    {
        return CaptureTool::NONE;
    }

    void begin_capture(std::string_view title) override
    {
        Device::begin_capture(title);
    }

    void end_capture() override
    {
        Device::end_capture();
    }

    [[nodiscard]] auto create_descriptor_pool(const DescriptorPoolDescription& description) -> std::unique_ptr<DescriptorPool> override
    {
        return Device::create_descriptor_pool(description);
    }

    [[nodiscard]] auto create_command_pool(const CommandPoolDescription& description) -> std::unique_ptr<CommandPool> override
    {
        return std::make_unique<FakeCommandPool>(++m_next_index, description.queue_type);
    }

    [[nodiscard]] auto create_fence(const FenceDescription& description) -> std::unique_ptr<Fence> override
    {
        return Device::create_fence(description);
    }

    [[nodiscard]] auto create_semaphore(const SemaphoreDescription& description) -> std::unique_ptr<Semaphore> override
    {
        return Device::create_semaphore(description);
    }

    [[nodiscard]] auto create_timeline_semaphore(const TimelineSemaphoreDescription& description)
        -> std::unique_ptr<TimelineSemaphore> override
    {
        auto timeline = std::make_unique<FakeTimelineSemaphore>();
        timeline->signal(description.initial_value);
        m_last_timeline = timeline.get();
        return timeline;
    }

    /// The timeline semaphore created last.
    [[nodiscard]] auto last_timeline() const noexcept -> FakeTimelineSemaphore&
    {
        return *m_last_timeline;
    }

    /// The query pool created last.
    [[nodiscard]] auto last_query_pool() const noexcept -> FakeQueryPool&
    {
        return *m_last_query_pool;
    }

    [[nodiscard]] auto fake_deleter() noexcept -> FakeDeleter&
    {
        return m_deleter;
    }

    [[nodiscard]] auto create_event(const EventDescription& description) -> std::unique_ptr<Event> override
    {
        return Device::create_event(description);
    }

    [[nodiscard]] auto create_query_pool(const QueryPoolDescription& description) -> std::unique_ptr<QueryPool> override
    {
        auto pool = std::make_unique<FakeQueryPool>(++m_next_index, description.type, description.count);
        m_last_query_pool = pool.get();
        return pool;
    }

    [[nodiscard]] auto create_swapchain(const SwapchainDescription& description) -> std::unique_ptr<Swapchain> override
    {
        return Device::create_swapchain(description);
    }

protected:
    [[nodiscard]] auto reload_changed_shaders() -> std::vector<ShaderReload> override
    {
        return Device::reload_changed_shaders();
    }

private:
    /// The pipeline layout a description names; the bindless one when it names none.
    [[nodiscard]] auto layout_of(PipelineLayoutHandle handle) const -> const PipelineLayout&
    {
        return handle.is_valid() ? *resolve(m_pipeline_layouts, handle) : default_bindless_layout();
    }

    /// The shader module a description names, or nullptr for a null handle.
    [[nodiscard]] auto resolve_optional(ShaderModuleHandle handle) const -> const ShaderModule*
    {
        return handle.is_valid() ? resolve(m_shader_modules, handle) : nullptr;
    }

    /// The object a handle names, found through the handle's index (which only a HandleIssuer reads).
    template<class Object, class Tag>
    [[nodiscard]] static auto resolve(const std::map<std::uint32_t, const Object*>& objects, Handle<Tag> handle) -> const Object*
    {
        const auto found = handle.is_valid() ? objects.find(handle.index(handle_key())) : objects.end();
        if (found == objects.end())
        {
            throw InvalidHandleError({.backend = BackendKind::NULL_BACKEND,
                                      .operation = "FakeDevice::resolve",
                                      .handle_type = std::string{Tag::NAME},
                                      .problem = std::format("{} names no object of this device", handle)});
        }
        return found->second;
    }

private:
    const PhysicalDevice& m_physical_device;
    FakeDeleter m_deleter;
    std::uint32_t m_next_index = 0;
    CapturedDescriptions m_captured;
    std::map<std::string, ShaderLayout> m_reflected_layouts;
    std::map<std::uint32_t, const ShaderModule*> m_shader_modules;
    std::map<std::uint32_t, const DescriptorSetLayout*> m_set_layouts;
    std::map<std::uint32_t, const PipelineLayout*> m_pipeline_layouts;
    FakeTimelineSemaphore* m_last_timeline = nullptr;
    FakeQueryPool* m_last_query_pool = nullptr;
};

class FakePhysicalDevice final : public Fake<PhysicalDevice>
{
public:
    explicit FakePhysicalDevice(PhysicalDeviceType type,
                                std::vector<DeviceFeature> features = {CORE_FEATURES.begin(), CORE_FEATURES.end()})
        : m_features{std::move(features)}
    {
        m_properties.name = std::format("fake {}", type);
        m_properties.type = type;
    }

    [[nodiscard]] auto properties() const noexcept -> const PhysicalDeviceProperties& override
    {
        return m_properties;
    }

    [[nodiscard]] auto queue_families() const noexcept -> std::span<const QueueFamilyProperties> override
    {
        return {};
    }

    [[nodiscard]] auto supports(DeviceFeature feature) const noexcept -> bool override
    {
        return std::ranges::contains(m_features, feature);
    }

    [[nodiscard]] auto format_support(Format format) const -> FormatSupport override
    {
        return PhysicalDevice::format_support(format);
    }

    [[nodiscard]] auto surface_support(const NativeWindow& window) const -> SurfaceSupport override
    {
        return PhysicalDevice::surface_support(window);
    }

    [[nodiscard]] auto create_device(const DeviceDescription& /*description*/) -> std::unique_ptr<Device> override
    {
        return std::make_unique<FakeDevice>(*this);
    }

private:
    PhysicalDeviceProperties m_properties{};
    std::vector<DeviceFeature> m_features;
};

class FakeInstance final : public Fake<Instance>
{
public:
    explicit FakeInstance(std::initializer_list<PhysicalDeviceType> types)
    {
        for (const PhysicalDeviceType type : types)
        {
            m_physical_devices.push_back(std::make_unique<FakePhysicalDevice>(type));
        }
    }

    /// Adds a device with only @p features.
    void add_physical_device(PhysicalDeviceType type, std::vector<DeviceFeature> features)
    {
        m_physical_devices.push_back(std::make_unique<FakePhysicalDevice>(type, std::move(features)));
    }

    [[nodiscard]] auto physical_device_count() const noexcept -> std::size_t override
    {
        return m_physical_devices.size();
    }

    [[nodiscard]] auto physical_device(std::size_t index) -> PhysicalDevice& override
    {
        if (index >= m_physical_devices.size())
        {
            return Instance::physical_device(index);
        }
        return *m_physical_devices[index];
    }

private:
    std::vector<std::unique_ptr<FakePhysicalDevice>> m_physical_devices;
};

} // namespace gfx::tests

// Every fake overrides every operation of its interface.
static_assert(!std::is_abstract_v<gfx::tests::FakeCommandBuffer> && !std::is_abstract_v<gfx::tests::FakeDevice>);
static_assert(!std::is_abstract_v<gfx::tests::FakeBuffer> && !std::is_abstract_v<gfx::tests::FakeInstance> &&
              !std::is_abstract_v<gfx::tests::FakePhysicalDevice>);
static_assert(!std::is_abstract_v<gfx::tests::FakeCommandPool> && !std::is_abstract_v<gfx::tests::FakeTimelineSemaphore> &&
              !std::is_abstract_v<gfx::tests::FakeQueryPool>);
