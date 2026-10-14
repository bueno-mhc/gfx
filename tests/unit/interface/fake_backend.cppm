// Test-only backend. It shows the backend contract: every operation of an interface must be
// overridden (the classes would stay abstract otherwise), and operations a backend does not implement
// explicitly forward to the interface's definition, which throws a descriptive BackendCapabilityError.
// The fakes record what the typed API passes to the backend, so tests can check it.
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

protected:
    template<class Tag>
    [[nodiscard]] static auto make_handle(std::uint32_t index) -> Handle<Tag>
    {
        return Handle<Tag>::create(Interface::handle_key(), index, 1);
    }

protected:
    std::string m_debug_name = "fake";
};

// ================================================================================================= command buffer

/// Records every command as a line of text.
class FakeCommandBuffer final : public Fake<CommandBuffer>
{
public:
    explicit FakeCommandBuffer(QueueType queue_type)
        : m_queue_type{queue_type}
    {}

    [[nodiscard]] auto handle() const noexcept -> CommandBufferHandle override
    {
        return make_handle<CommandBufferTag>(1);
    }

    [[nodiscard]] auto queue_type() const noexcept -> QueueType override
    {
        return m_queue_type;
    }

    [[nodiscard]] auto level() const noexcept -> CommandBufferLevel override
    {
        return CommandBufferLevel::PRIMARY;
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
        m_log.push_back(std::format("execute_commands {}", secondary_command_buffers.size()));
    }

private:
    QueueType m_queue_type;
    std::vector<std::string> m_log;
    std::vector<std::byte> m_push_constant_bytes;
};

// ================================================================================================= simple objects

class FakeShaderModule final : public Fake<ShaderModule>
{
public:
    FakeShaderModule(std::uint32_t index, ShaderStage stage, const ShaderLayout& layout)
        : m_index{index}
        , m_stage{stage}
        , m_layout{layout}
    {}

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

class FakePipelineLayout final : public Fake<PipelineLayout>
{
public:
    explicit FakePipelineLayout(std::uint32_t index)
        : m_index{index}
    {}

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
    PipelineLayoutDefinition m_definition{};
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

/// Host-visible buffer of 64 bytes; everything else is left to the interface's defaults.
class FakeBuffer final : public Fake<Buffer>
{
public:
    [[nodiscard]] auto handle() const noexcept -> BufferHandle override
    {
        return make_handle<BufferTag>(1);
    }

    [[nodiscard]] auto size() const noexcept -> std::uint64_t override
    {
        return m_memory.size();
    }

    [[nodiscard]] auto usage() const noexcept -> BufferUsage override
    {
        return BufferUsage::STORAGE;
    }

    [[nodiscard]] auto location() const noexcept -> MemoryLocation override
    {
        return MemoryLocation::HOST_UPLOAD;
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
    std::array<std::byte, 64> m_memory{};
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
        return Deleter::collect();
    }

    void flush() override
    {
        Deleter::flush();
    }

    [[nodiscard]] auto pending_count() const noexcept -> std::size_t override
    {
        return 0;
    }
};

// ================================================================================================= device

/// What the typed API passed to the fake device (copies: descriptions only borrow their data).
struct CapturedDescriptions
{
    std::optional<ShaderModuleDescription> shader_module{};
    std::string shader_source{};
    std::string shader_name{};
    std::vector<std::vector<DescriptorSetLayoutBinding>> set_layout_bindings{};
    std::vector<DescriptorSetLayoutHandle> pipeline_layout_set_layouts{};
    std::vector<PushConstantRange> push_constant_ranges{};
    std::optional<ComputePipelineDescription> compute_pipeline{};
    std::optional<GraphicsPipelineDescription> graphics_pipeline{};
    std::vector<VertexInputAttribute> vertex_attributes{};
    std::vector<Format> color_formats{};
};

/// Implements what the typed API needs; every other operation forwards to Device's default.
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
        return Device::create_buffer(description);
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
        m_captured.shader_module = description;
        m_captured.shader_source = std::string{description.glsl_source};
        m_captured.shader_name = std::string{description.name};
        return std::make_unique<FakeShaderModule>(++m_next_index, description.stage,
                                                  description.expected_layout.value_or(ShaderLayout{}));
    }

    [[nodiscard]] auto create_descriptor_set_layout(const DescriptorSetLayoutDescription& description)
        -> std::unique_ptr<DescriptorSetLayout> override
    {
        m_captured.set_layout_bindings.emplace_back(description.bindings.begin(), description.bindings.end());
        return std::make_unique<FakeDescriptorSetLayout>(++m_next_index, description.bindings);
    }

    [[nodiscard]] auto create_pipeline_layout(const PipelineLayoutDescription& description) -> std::unique_ptr<PipelineLayout> override
    {
        m_captured.pipeline_layout_set_layouts.assign(description.set_layouts.begin(), description.set_layouts.end());
        m_captured.push_constant_ranges.assign(description.push_constant_ranges.begin(), description.push_constant_ranges.end());
        return std::make_unique<FakePipelineLayout>(++m_next_index);
    }

    [[nodiscard]] auto create_compute_pipeline(const ComputePipelineDescription& description)
        -> std::unique_ptr<ComputePipeline> override
    {
        m_captured.compute_pipeline = description;
        return std::make_unique<FakeComputePipeline>(++m_next_index, description.layout);
    }

    [[nodiscard]] auto create_graphics_pipeline(const GraphicsPipelineDescription& description)
        -> std::unique_ptr<GraphicsPipeline> override
    {
        m_captured.graphics_pipeline = description;
        m_captured.vertex_attributes.assign(description.vertex_attributes.begin(), description.vertex_attributes.end());
        m_captured.color_formats.assign(description.color_formats.begin(), description.color_formats.end());
        return std::make_unique<FakeGraphicsPipeline>(++m_next_index, description.layout);
    }

    [[nodiscard]] auto create_descriptor_pool(const DescriptorPoolDescription& description) -> std::unique_ptr<DescriptorPool> override
    {
        return Device::create_descriptor_pool(description);
    }

    [[nodiscard]] auto create_command_pool(const CommandPoolDescription& description) -> std::unique_ptr<CommandPool> override
    {
        return Device::create_command_pool(description);
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
        return Device::create_timeline_semaphore(description);
    }

    [[nodiscard]] auto create_event(const EventDescription& description) -> std::unique_ptr<Event> override
    {
        return Device::create_event(description);
    }

    [[nodiscard]] auto create_query_pool(const QueryPoolDescription& description) -> std::unique_ptr<QueryPool> override
    {
        return Device::create_query_pool(description);
    }

    [[nodiscard]] auto create_swapchain(const SwapchainDescription& description) -> std::unique_ptr<Swapchain> override
    {
        return Device::create_swapchain(description);
    }

private:
    const PhysicalDevice& m_physical_device;
    FakeDeleter m_deleter;
    std::uint32_t m_next_index = 0;
    CapturedDescriptions m_captured;
};

class FakePhysicalDevice final : public Fake<PhysicalDevice>
{
public:
    explicit FakePhysicalDevice(PhysicalDeviceType type, std::vector<DeviceFeature> features = {})
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
