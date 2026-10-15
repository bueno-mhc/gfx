/// @file vulkan_device.cppm
/// @brief Internal partition: VulkanDevice and the machinery every object of a device shares - the
///        handle tables (handle -> native record), the queues and their progress timelines, the deleter,
///        and the bindless descriptor table.
module;

#include <volk.h>
#include <vk_mem_alloc.h>

module gfx.backends.vulkan:device;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import gfx.interface;
import gfx.core.handle_table;
import :common;
import :instance;

namespace gfx::vulkan
{

class VulkanDevice;
class VulkanQueue;
class VulkanShaderModule;
class VulkanDescriptorSetLayout;
class VulkanPipelineLayout;

// ------------------------------------------------------------------------------------------- records

/// @brief What commands need to know about a buffer.
struct BufferRecord
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkDeviceAddress address = 0;
    BufferUsage usage = BufferUsage::NONE;
};

/// @brief What commands need to know about an image.
struct ImageRecord
{
    VkImage image = VK_NULL_HANDLE;
    Format format = Format::UNDEFINED;
    ImageAspect aspects = ImageAspect::NONE; ///< Every aspect of its format.
    Extent extent{};
    std::uint32_t mip_level_count = 1;
    std::uint32_t array_layer_count = 1;
    SampleCount samples = SampleCount::ONE;
    ImageUsage usage = ImageUsage::NONE;
};

/// @brief What commands and descriptors need to know about an image view.
struct ImageViewRecord
{
    VkImageView view = VK_NULL_HANDLE;
    ImageHandle image{};
    Format format = Format::UNDEFINED;
    ImageSubresourceRange range{};
    SampleCount samples = SampleCount::ONE;
};

/// @brief A sampler.
struct SamplerRecord
{
    VkSampler sampler = VK_NULL_HANDLE;
};

/// @brief A buffer view.
struct BufferViewRecord
{
    VkBufferView view = VK_NULL_HANDLE;
};

/// @brief An explicit memory allocation (resources are placed in it).
struct DeviceMemoryRecord
{
    VmaAllocation allocation = nullptr;
    VkDeviceSize size = 0;
};

/// @brief A shader module (its object: pipelines read its code and layout).
struct ShaderModuleRecord
{
    VulkanShaderModule* module = nullptr;
};

/// @brief A descriptor set layout.
struct DescriptorSetLayoutRecord
{
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    const VulkanDescriptorSetLayout* object = nullptr;
};

/// @brief A pipeline layout.
struct PipelineLayoutRecord
{
    VkPipelineLayout layout = VK_NULL_HANDLE;
    const VulkanPipelineLayout* object = nullptr;
};

/// @brief The native pipeline behind a gfx pipeline. It changes when an asynchronous compilation
///        completes and when hot reload rebuilds the pipeline, so commands read it when they bind.
struct PipelineState
{
    std::atomic<VkPipeline> pipeline{VK_NULL_HANDLE};
    std::atomic<PipelineStatus> status{PipelineStatus::COMPILING};
};

/// @brief A compute pipeline.
struct ComputePipelineRecord
{
    const PipelineState* state = nullptr;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

/// @brief A graphics pipeline.
struct GraphicsPipelineRecord
{
    const PipelineState* state = nullptr;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

/// @brief A ray tracing pipeline.
struct RayTracingPipelineRecord
{
    const PipelineState* state = nullptr;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

/// @brief A descriptor pool.
struct DescriptorPoolRecord
{
    VkDescriptorPool pool = VK_NULL_HANDLE;
};

/// @brief A descriptor set.
struct DescriptorSetRecord
{
    VkDescriptorSet set = VK_NULL_HANDLE;
};

/// @brief A command pool.
struct CommandPoolRecord
{
    VkCommandPool pool = VK_NULL_HANDLE;
};

/// @brief Recording state of a command buffer that submissions check (written by the recording thread,
///        read when it is submitted).
struct CommandBufferStatus
{
    std::atomic<bool> executable{false}; ///< Recorded completely and successfully since the last begin.
    std::string failure;                 ///< Why the last recording failed (a command that threw, or the end).
    CommandBufferHandle inherited_primary{}; ///< Secondary command buffers: the primary whose pass they continue.
    std::uint32_t inherited_pass = 0;        ///< Secondary command buffers: which parallel pass of that primary.
};

/// @brief What a submission needs to know about a command buffer.
struct CommandBufferRecord
{
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    CommandBufferLevel level = CommandBufferLevel::PRIMARY;
    QueueType queue_type = QueueType::GRAPHICS;
    const CommandBufferStatus* status = nullptr;
};

/// @brief A queue.
struct QueueRecord
{
    VulkanQueue* queue = nullptr;
};

/// @brief A fence.
struct FenceRecord
{
    VkFence fence = VK_NULL_HANDLE;
};

/// @brief What submissions promised a timeline semaphore. Owned by the semaphore; queues read it to
///        refuse submissions that would deadlock their hardware queue (see VulkanQueue::order_timeline_batches()).
struct TimelineSignals
{
    const TimelineSemaphore* semaphore = nullptr; ///< The semaphore (for its name in errors).
    std::atomic<std::uint64_t> submitted{0};      ///< Largest value a submitted batch signals.
};

/// @brief A binary or timeline semaphore.
struct SemaphoreRecord
{
    VkSemaphore semaphore = VK_NULL_HANDLE;
    TimelineSignals* timeline = nullptr; ///< Signals promised by submissions (timeline semaphores only).
};

/// @brief An event.
struct EventRecord
{
    VkEvent event = VK_NULL_HANDLE;
};

/// @brief A query pool.
struct QueryPoolRecord
{
    VkQueryPool pool = VK_NULL_HANDLE;
    QueryType type = QueryType::TIMESTAMP;
    std::uint32_t count = 0;
};

/// @brief An acceleration structure.
struct AccelerationStructureRecord
{
    VkAccelerationStructureKHR structure = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;
    AccelerationStructureType type = AccelerationStructureType::BOTTOM_LEVEL;
};

/// @brief A pipeline cache.
struct PipelineCacheRecord
{
    VkPipelineCache cache = VK_NULL_HANDLE;
};

/// @brief A debug label as a GPU breadcrumb (CrashDiagnostic::BREADCRUMBS): command buffers write
///        checkpoints pointing at these nodes when labels begin and end; after a device loss, the last
///        checkpoint each queue reached names the labels the GPU was inside (the parents' chain).
struct BreadcrumbLabel
{
    std::string name;                        ///< Label name.
    const BreadcrumbLabel* parent = nullptr; ///< Enclosing label.
    const BreadcrumbLabel* opened = nullptr; ///< For the end of a label: the node that began it (null for a beginning).
};

/// @brief The handle tables of a device: one per kind of object, resolving handles in commands.
struct HandleTables
{
    gfx::backends::HandleTable<BufferTag, BufferRecord> buffers{BackendKind::VULKAN};
    gfx::backends::HandleTable<ImageTag, ImageRecord> images{BackendKind::VULKAN};
    gfx::backends::HandleTable<ImageViewTag, ImageViewRecord> image_views{BackendKind::VULKAN};
    gfx::backends::HandleTable<SamplerTag, SamplerRecord> samplers{BackendKind::VULKAN};
    gfx::backends::HandleTable<BufferViewTag, BufferViewRecord> buffer_views{BackendKind::VULKAN};
    gfx::backends::HandleTable<DeviceMemoryTag, DeviceMemoryRecord> memories{BackendKind::VULKAN};
    gfx::backends::HandleTable<ShaderModuleTag, ShaderModuleRecord> shader_modules{BackendKind::VULKAN};
    gfx::backends::HandleTable<DescriptorSetLayoutTag, DescriptorSetLayoutRecord> descriptor_set_layouts{BackendKind::VULKAN};
    gfx::backends::HandleTable<PipelineLayoutTag, PipelineLayoutRecord> pipeline_layouts{BackendKind::VULKAN};
    gfx::backends::HandleTable<ComputePipelineTag, ComputePipelineRecord> compute_pipelines{BackendKind::VULKAN};
    gfx::backends::HandleTable<GraphicsPipelineTag, GraphicsPipelineRecord> graphics_pipelines{BackendKind::VULKAN};
    gfx::backends::HandleTable<RayTracingPipelineTag, RayTracingPipelineRecord> ray_tracing_pipelines{BackendKind::VULKAN};
    gfx::backends::HandleTable<DescriptorPoolTag, DescriptorPoolRecord> descriptor_pools{BackendKind::VULKAN};
    gfx::backends::HandleTable<DescriptorSetTag, DescriptorSetRecord> descriptor_sets{BackendKind::VULKAN};
    gfx::backends::HandleTable<CommandPoolTag, CommandPoolRecord> command_pools{BackendKind::VULKAN};
    gfx::backends::HandleTable<CommandBufferTag, CommandBufferRecord> command_buffers{BackendKind::VULKAN};
    gfx::backends::HandleTable<QueueTag, QueueRecord> queues{BackendKind::VULKAN};
    gfx::backends::HandleTable<FenceTag, FenceRecord> fences{BackendKind::VULKAN};
    gfx::backends::HandleTable<SemaphoreTag, SemaphoreRecord> semaphores{BackendKind::VULKAN};
    gfx::backends::HandleTable<TimelineSemaphoreTag, SemaphoreRecord> timeline_semaphores{BackendKind::VULKAN};
    gfx::backends::HandleTable<EventTag, EventRecord> events{BackendKind::VULKAN};
    gfx::backends::HandleTable<QueryPoolTag, QueryPoolRecord> query_pools{BackendKind::VULKAN};
    gfx::backends::HandleTable<AccelerationStructureTag, AccelerationStructureRecord> acceleration_structures{BackendKind::VULKAN};
    gfx::backends::HandleTable<PipelineCacheTag, PipelineCacheRecord> pipeline_caches{BackendKind::VULKAN};
};

// ---------------------------------------------------------------------------------------------- queues

/// @brief One native queue. Several gfx queues share it when the family has fewer queues than were
///        requested. Every submission also signals its progress timeline, which tells the deleter (and
///        wait_idle) how far the GPU has come.
/// @brief A timeline wait of a submitted batch that nothing signaled or promised to signal when it was
///        submitted: its hardware queue stalls there until a signal from elsewhere (the CPU, another
///        hardware queue) reaches the value.
struct UnresolvedWait
{
    TimelineSemaphoreHandle semaphore{}; ///< Semaphore waited on.
    std::uint64_t value = 0;             ///< Value waited for.
    QueueHandle queue{};                 ///< gfx queue that submitted the wait.
};

struct NativeQueue
{
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t family = 0;
    std::uint32_t index = 0;
    QueueCapability capabilities = QueueCapability::NONE;
    VkSemaphore progress = VK_NULL_HANDLE;      ///< Timeline: value N = submission N completed.
    std::atomic<std::uint64_t> submitted{0};    ///< Value of the last submission.
    std::mutex mutex;                           ///< Vulkan queues are externally synchronized.
    std::vector<UnresolvedWait> unresolved_waits; ///< Waits the queue may be stalled on (under mutex).
};

/// @brief The timeline waits and signals of one batch of a submission or of a sparse binding.
struct TimelineBatch
{
    std::span<const TimelineSemaphoreWait> waits{};
    std::span<const TimelineSemaphoreSignal> signals{};
};

/// @brief A gfx queue: a requested queue type mapped onto a native queue.
class VulkanQueue final : public Queue
{
public:
    VulkanQueue(VulkanDevice& device, NativeQueue& native, QueueType type, std::uint32_t index);
    ~VulkanQueue() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override;
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override;
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto handle() const noexcept -> QueueHandle override;
    [[nodiscard]] auto type() const noexcept -> QueueType override;
    [[nodiscard]] auto family_index() const noexcept -> std::uint32_t override;
    [[nodiscard]] auto capabilities() const noexcept -> QueueCapability override;
    void submit(std::span<const SubmitDescription> batches, FenceHandle fence) override;
    [[nodiscard]] auto present(const PresentDescription& presentation) -> SwapchainStatus override;
    void wait_idle() override;
    void bind_sparse(const SparseBindDescription& description) override;

    /// @brief The native queue it submits to.
    [[nodiscard]] auto native() noexcept -> NativeQueue&
    {
        return m_native;
    }

    /// @brief Checks the timeline waits and signals of @p batches, about to be submitted in this order
    ///        to the native queue (whose mutex the caller holds), and returns the waits the native queue
    ///        may then be stalled on. A hardware queue runs its submissions in order, so a signal of
    ///        exactly the value an unresolved wait of an earlier submission to the same hardware queue
    ///        waits for can never run: it alone could release the wait (anything else reaching the value
    ///        first would make it decrease the semaphore). Such a submission is refused (GPU deadlock).
    /// @throws InvalidUsageError when the wait and the signal come from this queue, BackendCapabilityError
    ///         when they come from two queues that share one hardware queue.
    [[nodiscard]] auto order_timeline_batches(std::span<const TimelineBatch> batches,
                                              std::string_view operation) const -> std::vector<UnresolvedWait>;

    /// @brief Records what a successful submission of @p batches promised: the values it signals, and
    ///        the waits the native queue may now be stalled on (from order_timeline_batches()).
    void record_timeline_batches(std::span<const TimelineBatch> batches,
                                 std::vector<UnresolvedWait> unresolved);

private:
    /// @brief Throws the error explaining why @p signal can never run behind @p wait.
    [[noreturn]] void reject_deadlock(const UnresolvedWait& wait,
                                      const TimelineSemaphoreSignal& signal,
                                      std::string_view operation) const;

    VulkanDevice& m_device;
    NativeQueue& m_native;
    QueueType m_type;
    QueueHandle m_handle;
    std::string m_debug_name;

    // Arrays reused by every submission (under the native queue's mutex): no allocation per submit.
    std::vector<VkSubmitInfo2> m_submits;
    std::vector<VkCommandBufferSubmitInfo> m_command_buffers;
    std::vector<VkSemaphoreSubmitInfo> m_waits;
    std::vector<VkSemaphoreSubmitInfo> m_signals;
    std::vector<TimelineBatch> m_timeline_batches;
};

// --------------------------------------------------------------------------------------------- deleter

/// @brief Deferred destruction: a deletion runs once every queue has completed the submissions made
///        before it was enqueued (each queue's progress timeline reached the value it had then).
class VulkanDeleter final : public Deleter
{
public:
    explicit VulkanDeleter(VulkanDevice& device);

    [[nodiscard]] auto backend() const noexcept -> BackendKind override;
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override;
    void set_debug_name(std::string_view name) override;
    void enqueue(std::move_only_function<void()> deletion) override;
    auto collect() -> std::size_t override;
    void flush() override;
    [[nodiscard]] auto pending_count() const noexcept -> std::size_t override;

    /// @brief Runs every pending deletion now (the device is idle or being destroyed).
    auto run_all() noexcept -> std::size_t;

private:
    struct Pending
    {
        std::move_only_function<void()> deletion;
        std::array<std::uint64_t, 16> submitted{}; ///< Last submission of each native queue at enqueue time.
    };

    VulkanDevice& m_device;
    mutable std::mutex m_mutex;
    std::deque<Pending> m_pending;
    std::atomic<std::size_t> m_pending_count{0};
    std::string m_debug_name{"deleter"};
};

// -------------------------------------------------------------------------------------------- bindless

/// @brief The bindless descriptor table: one update-after-bind, partially bound descriptor set with one
///        array per BindlessResourceKind (binding = kind), and the allocation of its indices.
class BindlessTable
{
public:
    BindlessTable(VulkanDevice& device, const BindlessTableDescription& description, bool acceleration_structures);
    ~BindlessTable();

    BindlessTable(const BindlessTable&) = delete;
    auto operator=(const BindlessTable&) -> BindlessTable& = delete;

    /// @brief Kinds the table holds (acceleration structures only with ray tracing or ray queries).
    [[nodiscard]] auto kinds() const noexcept -> std::span<const BindlessResourceKind>;

    /// @brief Capacities of the arrays.
    [[nodiscard]] auto description() const noexcept -> const BindlessTableDescription&
    {
        return m_description;
    }

    /// @brief The descriptor set layout of set BINDLESS_SET.
    [[nodiscard]] auto set_layout() const noexcept -> VkDescriptorSetLayout
    {
        return m_set_layout;
    }

    /// @brief The descriptor set every command buffer binds.
    [[nodiscard]] auto set() const noexcept -> VkDescriptorSet
    {
        return m_set;
    }

    /// @brief Registers a sampled image view.
    [[nodiscard]] auto add_sampled_image(VkImageView view) -> SampledImageIndex;
    /// @brief Registers a storage image view.
    [[nodiscard]] auto add_storage_image(VkImageView view) -> StorageImageIndex;
    /// @brief Registers a sampler.
    [[nodiscard]] auto add_sampler(VkSampler sampler) -> SamplerIndex;
    /// @brief Registers a storage buffer.
    [[nodiscard]] auto add_storage_buffer(VkBuffer buffer) -> StorageBufferIndex;
    /// @brief Registers a top-level acceleration structure.
    [[nodiscard]] auto add_acceleration_structure(VkAccelerationStructureKHR structure) -> AccelerationStructureIndex;

    /// @brief Releases index @p index of @p kind once the GPU no longer uses it (through the deleter).
    void release_later(BindlessResourceKind kind, std::uint32_t index);

    /// @brief How full the arrays are.
    [[nodiscard]] auto usage() const -> BindlessTableUsage;

private:
    // Allocates an index of @p kind (throws when the array is full).
    [[nodiscard]] auto allocate(BindlessResourceKind kind) -> std::uint32_t;
    void release(BindlessResourceKind kind, std::uint32_t index);
    void write(BindlessResourceKind kind, std::uint32_t index, const VkDescriptorImageInfo* image,
               const VkDescriptorBufferInfo* buffer, const void* next);

    struct Array
    {
        std::uint32_t next = 0;                 ///< First index never used.
        std::vector<std::uint32_t> free{};      ///< Released indices.
        std::uint32_t used = 0;                 ///< Registered resources.
    };

    VulkanDevice& m_device;
    BindlessTableDescription m_description;
    std::vector<BindlessResourceKind> m_kinds;
    VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;
    mutable std::mutex m_mutex;
    std::array<Array, ENUMERATOR_COUNT<BindlessResourceKind>> m_arrays{};
};

// ---------------------------------------------------------------------------------------------- device

/// @brief A Vulkan device (VkDevice) with its queues, memory allocator, handle tables, bindless table,
///        default pipeline cache and deleter.
class VulkanDevice final : public Device
{
public:
    VulkanDevice(VulkanPhysicalDevice& physical_device, const DeviceDescription& description);
    ~VulkanDevice() override;

    // BackendObject
    [[nodiscard]] auto backend() const noexcept -> BackendKind override;
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override;
    void set_debug_name(std::string_view name) override;

    // Device
    [[nodiscard]] auto physical_device() const noexcept -> const PhysicalDevice& override;
    [[nodiscard]] auto queue(QueueType type, std::uint32_t index) -> Queue& override;
    [[nodiscard]] auto queue_count(QueueType type) const noexcept -> std::uint32_t override;
    [[nodiscard]] auto deleter() noexcept -> Deleter& override;
    void wait_idle() override;
    [[nodiscard]] auto bindless_pipeline_layout() const noexcept -> const PipelineLayout& override;
    [[nodiscard]] auto bindless_usage() const noexcept -> BindlessTableUsage override;
    [[nodiscard]] auto memory_budget() const -> std::vector<MemoryHeapBudget> override;
    [[nodiscard]] auto memory_statistics() const -> MemoryStatistics override;
    [[nodiscard]] auto memory_requirements(const BufferDescription& description) const -> MemoryRequirements override;
    [[nodiscard]] auto memory_requirements(const ImageDescription& description) const -> MemoryRequirements override;
    [[nodiscard]] auto allocate_memory(const DeviceMemoryDescription& description) -> std::unique_ptr<DeviceMemory> override;
    [[nodiscard]] auto create_buffer(const BufferDescription& description) -> std::unique_ptr<Buffer> override;
    [[nodiscard]] auto create_image(const ImageDescription& description) -> std::unique_ptr<Image> override;
    [[nodiscard]] auto create_image_view(const ImageViewDescription& description) -> std::unique_ptr<ImageView> override;
    [[nodiscard]] auto create_sampler(const SamplerDescription& description) -> std::unique_ptr<Sampler> override;
    [[nodiscard]] auto create_buffer_view(const BufferViewDescription& description) -> std::unique_ptr<BufferView> override;
    [[nodiscard]] auto sparse_image_requirements(ImageHandle image) const -> SparseImageRequirements override;
    [[nodiscard]] auto create_shader_module(const ShaderModuleDescription& description) -> std::unique_ptr<ShaderModule> override;
    [[nodiscard]] auto create_descriptor_set_layout(const DescriptorSetLayoutDescription& description)
        -> std::unique_ptr<DescriptorSetLayout> override;
    [[nodiscard]] auto create_pipeline_layout(const PipelineLayoutDescription& description) -> std::unique_ptr<PipelineLayout> override;
    [[nodiscard]] auto create_compute_pipeline(const ComputePipelineDescription& description)
        -> std::unique_ptr<ComputePipeline> override;
    [[nodiscard]] auto create_graphics_pipeline(const GraphicsPipelineDescription& description)
        -> std::unique_ptr<GraphicsPipeline> override;
    [[nodiscard]] auto create_descriptor_pool(const DescriptorPoolDescription& description) -> std::unique_ptr<DescriptorPool> override;
    [[nodiscard]] auto create_pipeline_cache(const PipelineCacheDescription& description) -> std::unique_ptr<PipelineCache> override;
    [[nodiscard]] auto create_ray_tracing_pipeline(const RayTracingPipelineDescription& description)
        -> std::unique_ptr<RayTracingPipeline> override;
    [[nodiscard]] auto acceleration_structure_build_sizes(const AccelerationStructureBuildDescription& description) const
        -> AccelerationStructureBuildSizes override;
    [[nodiscard]] auto create_acceleration_structure(const AccelerationStructureDescription& description)
        -> std::unique_ptr<AccelerationStructure> override;
    [[nodiscard]] auto create_command_pool(const CommandPoolDescription& description) -> std::unique_ptr<CommandPool> override;
    [[nodiscard]] auto create_fence(const FenceDescription& description) -> std::unique_ptr<Fence> override;
    [[nodiscard]] auto create_semaphore(const SemaphoreDescription& description) -> std::unique_ptr<Semaphore> override;
    [[nodiscard]] auto create_timeline_semaphore(const TimelineSemaphoreDescription& description)
        -> std::unique_ptr<TimelineSemaphore> override;
    [[nodiscard]] auto create_event(const EventDescription& description) -> std::unique_ptr<Event> override;
    [[nodiscard]] auto create_query_pool(const QueryPoolDescription& description) -> std::unique_ptr<QueryPool> override;
    [[nodiscard]] auto create_swapchain(const SwapchainDescription& description) -> std::unique_ptr<Swapchain> override;
    [[nodiscard]] auto capture_tool() const noexcept -> CaptureTool override;
    void begin_capture(std::string_view title) override;
    void end_capture() override;

    // ----------------------------------------------------------------------- shared with the objects

    /// @brief The native device.
    [[nodiscard]] auto native() const noexcept -> VkDevice
    {
        return m_device;
    }

    /// @brief Device-level functions of this device.
    [[nodiscard]] auto functions() const noexcept -> const VolkDeviceTable&
    {
        return m_functions;
    }

    /// @brief The memory allocator.
    [[nodiscard]] auto allocator() const noexcept -> VmaAllocator
    {
        return m_allocator;
    }

    /// @brief The physical device.
    [[nodiscard]] auto vulkan_physical_device() const noexcept -> const VulkanPhysicalDevice&
    {
        return m_physical_device;
    }

    /// @brief Handle tables.
    [[nodiscard]] auto handles() noexcept -> HandleTables&
    {
        return m_handles;
    }

    /// @brief The bindless descriptor table.
    [[nodiscard]] auto bindless() noexcept -> BindlessTable&
    {
        return *m_bindless;
    }

    /// @brief The deleter, with its backend type.
    [[nodiscard]] auto vulkan_deleter() noexcept -> VulkanDeleter&
    {
        return *m_deleter;
    }

    /// @brief Native queues (the deleter tracks their progress).
    [[nodiscard]] auto native_queues() noexcept -> std::span<const std::unique_ptr<NativeQueue>>
    {
        return m_native_queues;
    }

    /// @brief Whether @p feature is enabled on this device.
    [[nodiscard]] auto enabled(DeviceFeature feature) const noexcept -> bool
    {
        return m_enabled_features[static_cast<std::size_t>(std::to_underlying(feature))];
    }

    /// @brief Throws a BackendCapabilityError naming @p feature when it is not enabled.
    void require(DeviceFeature feature, std::string_view operation) const;

    /// @brief Queue family of queue type @p type (UINT32_MAX when no such queue exists).
    [[nodiscard]] auto queue_family(QueueType type) const noexcept -> std::uint32_t;

    /// @brief Pipeline stages and memory accesses the enabled features allow in barriers.
    [[nodiscard]] auto supported_stages() const noexcept -> VkPipelineStageFlags2
    {
        return m_supported_stages;
    }

    /// @brief Memory accesses the enabled features allow in barriers.
    [[nodiscard]] auto supported_access() const noexcept -> VkAccessFlags2
    {
        return m_supported_access;
    }

    /// @brief The default pipeline cache (pipelines that name no cache use it).
    [[nodiscard]] auto default_pipeline_cache() const noexcept -> VkPipelineCache
    {
        return m_pipeline_cache;
    }

    /// @brief Runs background work (asynchronous pipeline compilation); empty when the application gave
    ///        no scheduler.
    [[nodiscard]] auto task_scheduler() const noexcept -> const TaskScheduler&
    {
        return m_task_scheduler;
    }

    /// @brief Crash diagnostics collected (breadcrumbs, vendor reports).
    [[nodiscard]] auto crash_diagnostics() const noexcept -> CrashDiagnostic
    {
        return m_crash_diagnostics;
    }

    /// @brief Names a native object in debugging tools (when VK_EXT_debug_utils is enabled).
    void name_object(VkObjectType type, std::uint64_t handle, std::string_view name) const;

    /// @brief Names a native object in debugging tools.
    template<class NativeHandle>
    void name_object(VkObjectType type, NativeHandle handle, std::string_view name) const
    {
        name_object(type, reinterpret_cast<std::uint64_t>(handle), name);
    }

    /// @brief Checks @p result like check(), and reports a lost device with its crash diagnostics.
    void check_result(VkResult result, std::string_view operation, std::string_view function,
                      std::source_location location = std::source_location::current()) const;

    /// @brief Throws the DeviceLostError of a lost device, with the breadcrumbs the queues reached and the
    ///        driver's fault report (as the device's crash diagnostics allow).
    [[noreturn]] void report_device_lost(std::string_view operation, std::string_view function, std::source_location location) const;

protected:
    [[nodiscard]] auto reload_changed_shaders() -> std::vector<ShaderReload> override;

private:
    [[nodiscard]] auto detect_capture_tool() const -> CaptureTool;
    [[nodiscard]] auto write_fault_report() const -> std::string;
    void enable_features(const DeviceDescription& description);
    void create_device(const DeviceDescription& description);
    void create_queues();
    void create_allocator();

private:
    VulkanPhysicalDevice& m_physical_device;
    VkDevice m_device = VK_NULL_HANDLE;
    VolkDeviceTable m_functions{};
    VmaAllocator m_allocator = nullptr;
    VkPipelineCache m_pipeline_cache = VK_NULL_HANDLE;
    std::array<bool, ENUMERATOR_COUNT<DeviceFeature>> m_enabled_features{};
    std::vector<const char*> m_extensions;
    VkPipelineStageFlags2 m_supported_stages = 0;
    VkAccessFlags2 m_supported_access = 0;
    CrashDiagnostic m_crash_diagnostics = CrashDiagnostic::NONE;
    CaptureTool m_capture_tool = CaptureTool::NONE;
    std::atomic<bool> m_capturing{false};
    TaskScheduler m_task_scheduler;
    std::string m_debug_name;

    // Queues: requested (type, index) -> family and native queue.
    struct QueueAssignment
    {
        QueueType type = QueueType::GRAPHICS;
        std::uint32_t family = 0;
        std::uint32_t native_index = 0;
        float priority = 1.0F;
    };
    std::vector<QueueAssignment> m_queue_assignments;
    std::array<std::uint32_t, ENUMERATOR_COUNT<QueueType>> m_queue_families{};

    // Destroyed in reverse order: objects referring to the tables go first.
    HandleTables m_handles;
    std::vector<std::unique_ptr<NativeQueue>> m_native_queues;
    std::vector<std::unique_ptr<VulkanQueue>> m_queues;
    std::unique_ptr<VulkanDeleter> m_deleter;
    std::unique_ptr<BindlessTable> m_bindless;
    std::unique_ptr<VulkanPipelineLayout> m_bindless_pipeline_layout;
};

} // namespace gfx::vulkan
