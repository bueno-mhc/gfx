/// @file vulkan_synchronization.cpp
/// @brief Fences, semaphores, timeline semaphores, events and query pools. Nothing outside this unit
///        needs their classes, so they are declared here (no partition).
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

namespace gfx::vulkan
{

namespace
{

/// @brief A timeout in Vulkan nanoseconds (WAIT_INDEFINITELY = UINT64_MAX, negative = 0).
auto
timeout_nanoseconds(std::chrono::nanoseconds timeout) noexcept -> std::uint64_t
{
    if (timeout == WAIT_INDEFINITELY)
    {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return timeout.count() < 0 ? 0 : static_cast<std::uint64_t>(timeout.count());
}

} // namespace

/// @brief A fence (VkFence).
class VulkanFence final : public Fence
{
public:
    VulkanFence(VulkanDevice& device, const FenceDescription& description);
    ~VulkanFence() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override;
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override;
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto handle() const noexcept -> FenceHandle override;
    [[nodiscard]] auto is_signaled() const -> bool override;
    [[nodiscard]] auto wait(std::chrono::nanoseconds timeout) const -> WaitStatus override;
    void reset() override;

private:
    VulkanDevice& m_device;
    VkFence m_fence = VK_NULL_HANDLE;
    FenceHandle m_handle;
    std::string m_debug_name;
};

/// @brief A binary semaphore (VkSemaphore).
class VulkanSemaphore final : public Semaphore
{
public:
    VulkanSemaphore(VulkanDevice& device, const SemaphoreDescription& description);
    ~VulkanSemaphore() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override;
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override;
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto handle() const noexcept -> SemaphoreHandle override;

private:
    VulkanDevice& m_device;
    VkSemaphore m_semaphore = VK_NULL_HANDLE;
    SemaphoreHandle m_handle;
    std::string m_debug_name;
};

/// @brief A timeline semaphore (VkSemaphore of type TIMELINE).
class VulkanTimelineSemaphore final : public TimelineSemaphore
{
public:
    VulkanTimelineSemaphore(VulkanDevice& device, const TimelineSemaphoreDescription& description);
    ~VulkanTimelineSemaphore() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override;
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override;
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto handle() const noexcept -> TimelineSemaphoreHandle override;
    [[nodiscard]] auto value() const -> std::uint64_t override;
    [[nodiscard]] auto wait(std::uint64_t value, std::chrono::nanoseconds timeout) const -> WaitStatus override;
    void signal(std::uint64_t value) override;

private:
    VulkanDevice& m_device;
    VkSemaphore m_semaphore = VK_NULL_HANDLE;
    TimelineSemaphoreHandle m_handle;
    std::string m_debug_name;
    TimelineSignals m_signals; ///< What submissions promised it (read by queues).
};

/// @brief An event (VkEvent), set and waited on by command buffers or the CPU.
class VulkanEvent final : public Event
{
public:
    VulkanEvent(VulkanDevice& device, const EventDescription& description);
    ~VulkanEvent() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override;
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override;
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto handle() const noexcept -> EventHandle override;
    [[nodiscard]] auto is_set() const -> bool override;
    void set() override;
    void reset() override;

private:
    VulkanDevice& m_device;
    VkEvent m_event = VK_NULL_HANDLE;
    EventHandle m_handle;
    std::string m_debug_name;
};

/// @brief A query pool (VkQueryPool). Its queries are reset when it is created.
class VulkanQueryPool final : public QueryPool
{
public:
    VulkanQueryPool(VulkanDevice& device, const QueryPoolDescription& description);
    ~VulkanQueryPool() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override;
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override;
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto handle() const noexcept -> QueryPoolHandle override;
    [[nodiscard]] auto type() const noexcept -> QueryType override;
    [[nodiscard]] auto count() const noexcept -> std::uint32_t override;
    [[nodiscard]] auto results(std::uint32_t first, std::span<std::uint64_t> destination, bool wait) -> bool override;

private:
    VulkanDevice& m_device;
    VkQueryPool m_pool = VK_NULL_HANDLE;
    QueryType m_type;
    std::uint32_t m_count;
    std::uint32_t m_values_per_query = 1;
    QueryPoolHandle m_handle;
    std::string m_debug_name;
};

// ---------------------------------------------------------------------------------------------------
// VulkanFence
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::create_fence(const FenceDescription& description) -> std::unique_ptr<Fence>
{
    return std::make_unique<VulkanFence>(*this, description);
}

VulkanFence::VulkanFence(VulkanDevice& device,
                         const FenceDescription& description)
    : Fence{}
    , m_device{device}
    , m_debug_name(description.debug_name)
{
    set_validation_level(device.validation_level());
    const VkFenceCreateInfo create_information{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = description.signaled ? VkFenceCreateFlags{VK_FENCE_CREATE_SIGNALED_BIT} : 0U,
    };
    device.check_result(device.functions().vkCreateFence(device.native(), &create_information, nullptr, &m_fence), "Device::create_fence",
                        "vkCreateFence");
    m_handle = device.handles().fences.insert({.fence = m_fence});
    device.name_object(VK_OBJECT_TYPE_FENCE, m_fence, m_debug_name);
}

VulkanFence::~VulkanFence()
{
    (void)m_device.handles().fences.remove(m_handle);
    m_device.vulkan_deleter().enqueue([device = &m_device, fence = m_fence]
    {
        device->functions().vkDestroyFence(device->native(), fence, nullptr);
    });
}

auto
VulkanFence::backend() const noexcept -> BackendKind
{
    return BackendKind::VULKAN;
}

auto
VulkanFence::debug_name() const noexcept -> std::string_view
{
    return m_debug_name;
}

void
VulkanFence::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    m_device.name_object(VK_OBJECT_TYPE_FENCE, m_fence, m_debug_name);
}

auto
VulkanFence::handle() const noexcept -> FenceHandle
{
    return m_handle;
}

auto
VulkanFence::is_signaled() const -> bool
{
    const VkResult result = m_device.functions().vkGetFenceStatus(m_device.native(), m_fence);
    m_device.check_result(result, "Fence::is_signaled", "vkGetFenceStatus");
    return result == VK_SUCCESS;
}

auto
VulkanFence::wait(std::chrono::nanoseconds timeout) const -> WaitStatus
{
    const VkResult result = m_device.functions().vkWaitForFences(m_device.native(), 1, &m_fence, VK_TRUE, timeout_nanoseconds(timeout));
    m_device.check_result(result, "Fence::wait", "vkWaitForFences");
    return result == VK_TIMEOUT ? WaitStatus::TIMEOUT : WaitStatus::SIGNALED;
}

void
VulkanFence::reset()
{
    m_device.check_result(m_device.functions().vkResetFences(m_device.native(), 1, &m_fence), "Fence::reset", "vkResetFences");
}

// ---------------------------------------------------------------------------------------------------
// VulkanSemaphore
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::create_semaphore(const SemaphoreDescription& description) -> std::unique_ptr<Semaphore>
{
    return std::make_unique<VulkanSemaphore>(*this, description);
}

VulkanSemaphore::VulkanSemaphore(VulkanDevice& device,
                                 const SemaphoreDescription& description)
    : Semaphore{}
    , m_device{device}
    , m_debug_name(description.debug_name)
{
    set_validation_level(device.validation_level());
    const VkSemaphoreCreateInfo create_information{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    device.check_result(device.functions().vkCreateSemaphore(device.native(), &create_information, nullptr, &m_semaphore),
                        "Device::create_semaphore", "vkCreateSemaphore");
    m_handle = device.handles().semaphores.insert({.semaphore = m_semaphore});
    device.name_object(VK_OBJECT_TYPE_SEMAPHORE, m_semaphore, m_debug_name);
}

VulkanSemaphore::~VulkanSemaphore()
{
    (void)m_device.handles().semaphores.remove(m_handle);
    m_device.vulkan_deleter().enqueue([device = &m_device, semaphore = m_semaphore]
    {
        device->functions().vkDestroySemaphore(device->native(), semaphore, nullptr);
    });
}

auto
VulkanSemaphore::backend() const noexcept -> BackendKind
{
    return BackendKind::VULKAN;
}

auto
VulkanSemaphore::debug_name() const noexcept -> std::string_view
{
    return m_debug_name;
}

void
VulkanSemaphore::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    m_device.name_object(VK_OBJECT_TYPE_SEMAPHORE, m_semaphore, m_debug_name);
}

auto
VulkanSemaphore::handle() const noexcept -> SemaphoreHandle
{
    return m_handle;
}

// ---------------------------------------------------------------------------------------------------
// VulkanTimelineSemaphore
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::create_timeline_semaphore(const TimelineSemaphoreDescription& description) -> std::unique_ptr<TimelineSemaphore>
{
    return std::make_unique<VulkanTimelineSemaphore>(*this, description);
}

VulkanTimelineSemaphore::VulkanTimelineSemaphore(VulkanDevice& device,
                                                 const TimelineSemaphoreDescription& description)
    : TimelineSemaphore{}
    , m_device{device}
    , m_debug_name(description.debug_name)
{
    set_validation_level(device.validation_level());
    const VkSemaphoreTypeCreateInfo timeline{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                                             .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                                             .initialValue = description.initial_value};
    const VkSemaphoreCreateInfo create_information{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &timeline};
    device.check_result(device.functions().vkCreateSemaphore(device.native(), &create_information, nullptr, &m_semaphore),
                        "Device::create_timeline_semaphore", "vkCreateSemaphore");
    // Queues read what submissions promised it through its record.
    m_signals.semaphore = this;
    m_handle = device.handles().timeline_semaphores.insert({.semaphore = m_semaphore, .timeline = &m_signals});
    device.name_object(VK_OBJECT_TYPE_SEMAPHORE, m_semaphore, m_debug_name);
}

VulkanTimelineSemaphore::~VulkanTimelineSemaphore()
{
    (void)m_device.handles().timeline_semaphores.remove(m_handle);
    m_device.vulkan_deleter().enqueue([device = &m_device, semaphore = m_semaphore]
    {
        device->functions().vkDestroySemaphore(device->native(), semaphore, nullptr);
    });
}

auto
VulkanTimelineSemaphore::backend() const noexcept -> BackendKind
{
    return BackendKind::VULKAN;
}

auto
VulkanTimelineSemaphore::debug_name() const noexcept -> std::string_view
{
    return m_debug_name;
}

void
VulkanTimelineSemaphore::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    m_device.name_object(VK_OBJECT_TYPE_SEMAPHORE, m_semaphore, m_debug_name);
}

auto
VulkanTimelineSemaphore::handle() const noexcept -> TimelineSemaphoreHandle
{
    return m_handle;
}

auto
VulkanTimelineSemaphore::value() const -> std::uint64_t
{
    std::uint64_t current = 0;
    m_device.check_result(m_device.functions().vkGetSemaphoreCounterValue(m_device.native(), m_semaphore, &current),
                          "TimelineSemaphore::value", "vkGetSemaphoreCounterValue");
    return current;
}

auto
VulkanTimelineSemaphore::wait(std::uint64_t value,
                              std::chrono::nanoseconds timeout) const -> WaitStatus
{
    const VkSemaphoreWaitInfo wait{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &m_semaphore,
        .pValues = &value,
    };
    const VkResult result = m_device.functions().vkWaitSemaphores(m_device.native(), &wait, timeout_nanoseconds(timeout));
    m_device.check_result(result, "TimelineSemaphore::wait", "vkWaitSemaphores");
    return result == VK_TIMEOUT ? WaitStatus::TIMEOUT : WaitStatus::SIGNALED;
}

void
VulkanTimelineSemaphore::signal(std::uint64_t value)
{
    // Timelines only move forward.
    if (validates(ValidationLevel::CREATION))
    {
        const std::uint64_t current = this->value();
        if (value <= current)
        {
            throw InvalidUsageError({
                .backend = BackendKind::VULKAN,
                .operation = "TimelineSemaphore::signal",
                .problem = std::format("timeline semaphore '{}' is at {}; signaling {} would not advance it", m_debug_name,
                                       current, value),
                .suggestion = "signal a value greater than TimelineSemaphore::value()",
            });
        }
    }
    const VkSemaphoreSignalInfo signal{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, .semaphore = m_semaphore, .value = value};
    m_device.check_result(m_device.functions().vkSignalSemaphore(m_device.native(), &signal), "TimelineSemaphore::signal",
                          "vkSignalSemaphore");
}

// ---------------------------------------------------------------------------------------------------
// VulkanEvent
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::create_event(const EventDescription& description) -> std::unique_ptr<Event>
{
    require(DeviceFeature::EVENTS, "Device::create_event");
    return std::make_unique<VulkanEvent>(*this, description);
}

VulkanEvent::VulkanEvent(VulkanDevice& device,
                         const EventDescription& description)
    : Event{}
    , m_device{device}
    , m_debug_name(description.debug_name)
{
    set_validation_level(device.validation_level());
    const VkEventCreateInfo create_information{.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
    device.check_result(device.functions().vkCreateEvent(device.native(), &create_information, nullptr, &m_event), "Device::create_event",
                        "vkCreateEvent");
    m_handle = device.handles().events.insert({.event = m_event});
    device.name_object(VK_OBJECT_TYPE_EVENT, m_event, m_debug_name);
}

VulkanEvent::~VulkanEvent()
{
    (void)m_device.handles().events.remove(m_handle);
    m_device.vulkan_deleter().enqueue([device = &m_device, event = m_event]
    {
        device->functions().vkDestroyEvent(device->native(), event, nullptr);
    });
}

auto
VulkanEvent::backend() const noexcept -> BackendKind
{
    return BackendKind::VULKAN;
}

auto
VulkanEvent::debug_name() const noexcept -> std::string_view
{
    return m_debug_name;
}

void
VulkanEvent::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    m_device.name_object(VK_OBJECT_TYPE_EVENT, m_event, m_debug_name);
}

auto
VulkanEvent::handle() const noexcept -> EventHandle
{
    return m_handle;
}

auto
VulkanEvent::is_set() const -> bool
{
    const VkResult result = m_device.functions().vkGetEventStatus(m_device.native(), m_event);
    m_device.check_result(result, "Event::is_set", "vkGetEventStatus");
    return result == VK_EVENT_SET;
}

void
VulkanEvent::set()
{
    m_device.check_result(m_device.functions().vkSetEvent(m_device.native(), m_event), "Event::set", "vkSetEvent");
}

void
VulkanEvent::reset()
{
    m_device.check_result(m_device.functions().vkResetEvent(m_device.native(), m_event), "Event::reset", "vkResetEvent");
}

// ---------------------------------------------------------------------------------------------------
// VulkanQueryPool
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::create_query_pool(const QueryPoolDescription& description) -> std::unique_ptr<QueryPool>
{
    // Each kind of query needs its feature.
    switch (description.type)
    {
    case QueryType::PIPELINE_STATISTICS: require(DeviceFeature::PIPELINE_STATISTICS_QUERY, "Device::create_query_pool"); break;
    case QueryType::TIMESTAMP: require(DeviceFeature::TIMESTAMP_QUERY, "Device::create_query_pool"); break;
    case QueryType::ACCELERATION_STRUCTURE_COMPACTED_SIZE:
        if (!enabled(DeviceFeature::RAY_QUERY))
        {
            require(DeviceFeature::RAY_TRACING, "Device::create_query_pool");
        }
        break;
    case QueryType::OCCLUSION: break;
    }
    return std::make_unique<VulkanQueryPool>(*this, description);
}

VulkanQueryPool::VulkanQueryPool(VulkanDevice& device,
                                 const QueryPoolDescription& description)
    : QueryPool{}
    , m_device{device}
    , m_type{description.type}
    , m_count{description.count}
    , m_debug_name(description.debug_name)
{
    set_validation_level(device.validation_level());
    if (description.count == 0 ||
        (description.type == QueryType::PIPELINE_STATISTICS && description.statistics == PipelineStatistic::NONE))
    {
        throw InvalidUsageError({
            .backend = BackendKind::VULKAN,
            .operation = "Device::create_query_pool",
            .problem = std::format("query pool '{}' must hold at least one query{}", m_debug_name,
                                   description.type == QueryType::PIPELINE_STATISTICS ? " and select statistics" : ""),
            .suggestion = "",
        });
    }
    if (description.type == QueryType::PIPELINE_STATISTICS)
    {
        m_values_per_query = static_cast<std::uint32_t>(std::popcount(std::to_underlying(description.statistics)));
    }

    const VkQueryPoolCreateInfo create_information{
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = to_vulkan(description.type),
        .queryCount = description.count,
        .pipelineStatistics = description.type == QueryType::PIPELINE_STATISTICS ? to_vulkan(description.statistics) : 0U,
    };
    device.check_result(device.functions().vkCreateQueryPool(device.native(), &create_information, nullptr, &m_pool),
                        "Device::create_query_pool", "vkCreateQueryPool");

    // Queries start reset (host query reset is core in Vulkan 1.2).
    if (device.vulkan_physical_device().capabilities().features12.hostQueryReset == VK_TRUE)
    {
        device.functions().vkResetQueryPool(device.native(), m_pool, 0, m_count);
    }
    m_handle = device.handles().query_pools.insert({.pool = m_pool, .type = m_type, .count = m_count});
    device.name_object(VK_OBJECT_TYPE_QUERY_POOL, m_pool, m_debug_name);
}

VulkanQueryPool::~VulkanQueryPool()
{
    (void)m_device.handles().query_pools.remove(m_handle);
    m_device.vulkan_deleter().enqueue([device = &m_device, pool = m_pool]
    {
        device->functions().vkDestroyQueryPool(device->native(), pool, nullptr);
    });
}

auto
VulkanQueryPool::backend() const noexcept -> BackendKind
{
    return BackendKind::VULKAN;
}

auto
VulkanQueryPool::debug_name() const noexcept -> std::string_view
{
    return m_debug_name;
}

void
VulkanQueryPool::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    m_device.name_object(VK_OBJECT_TYPE_QUERY_POOL, m_pool, m_debug_name);
}

auto
VulkanQueryPool::handle() const noexcept -> QueryPoolHandle
{
    return m_handle;
}

auto
VulkanQueryPool::type() const noexcept -> QueryType
{
    return m_type;
}

auto
VulkanQueryPool::count() const noexcept -> std::uint32_t
{
    return m_count;
}

auto
VulkanQueryPool::results(std::uint32_t first,
                         std::span<std::uint64_t> destination,
                         bool wait) -> bool
{
    // Whole queries, inside the pool.
    const std::size_t query_count = destination.size() / m_values_per_query;
    if (destination.size() % m_values_per_query != 0 || first + query_count > m_count)
    {
        throw InvalidUsageError({
            .backend = BackendKind::VULKAN,
            .operation = "QueryPool::results",
            .problem = std::format("query pool '{}' holds {} queries of {} value(s); results {} values from query {} do not fit",
                                   m_debug_name, m_count, m_values_per_query, destination.size(), first),
            .suggestion = "pass a destination holding a multiple of the values per query, within the pool",
        });
    }
    if (query_count == 0)
    {
        return true;
    }
    const VkResult result = m_device.functions().vkGetQueryPoolResults(
        m_device.native(), m_pool, first, static_cast<std::uint32_t>(query_count), destination.size_bytes(), destination.data(),
        sizeof(std::uint64_t) * m_values_per_query, VK_QUERY_RESULT_64_BIT | (wait ? VkQueryResultFlags{VK_QUERY_RESULT_WAIT_BIT} : 0U));
    m_device.check_result(result, "QueryPool::results", "vkGetQueryPoolResults");
    return result == VK_SUCCESS;
}

} // namespace gfx::vulkan
