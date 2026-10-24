/// @file vulkan_resources.cpp
/// @brief Device memory, buffers, images, image views, samplers and buffer views (allocated with the
///        Vulkan Memory Allocator), memory queries and sparse binding. Nothing outside this unit needs
///        the classes, so they are declared here.
module;

#include <volk.h>
#include <vk_mem_alloc.h>

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

/// @brief How VMA allocates memory for a resource at @p location (persistently mapped when host-visible).
///        Memory the CPU writes is coherent: CPU writes need no flush (TransientBufferAllocator and
///        Uploader rely on it); readback memory may be cached and is invalidated before reads.
auto
resource_allocation(MemoryLocation location) noexcept -> VmaAllocationCreateInfo
{
    switch (location)
    {
        case MemoryLocation::DEVICE_LOCAL: return {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        case MemoryLocation::HOST_UPLOAD:
            return {.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                    .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                    .requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        case MemoryLocation::HOST_READBACK:
            return {.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                    .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST};
        case MemoryLocation::DEVICE_LOCAL_HOST_VISIBLE:
            return {.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                    .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                    .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    }
    return {.usage = VMA_MEMORY_USAGE_AUTO};
}

/// @brief Memory properties a raw allocation at @p location requires and prefers.
auto
memory_allocation(MemoryLocation location) noexcept -> VmaAllocationCreateInfo
{
    switch (location)
    {
        case MemoryLocation::DEVICE_LOCAL:
            return {.flags = VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT, .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};
        case MemoryLocation::HOST_UPLOAD:
            return {.flags = VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                    .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        case MemoryLocation::HOST_READBACK:
            return {.flags = VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                    .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                    .preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT};
        case MemoryLocation::DEVICE_LOCAL_HOST_VISIBLE:
            return {.flags = VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                    .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    }
    return {};
}

/// @brief Queue families resources shared CONCURRENTLY are visible to: every family of the device.
auto
concurrent_families(VulkanDevice& device) -> std::vector<std::uint32_t>
{
    std::vector<std::uint32_t> families;
    for (const std::unique_ptr<NativeQueue>& queue : device.native_queues())
    {
        if (!std::ranges::contains(families, queue->family))
        {
            families.push_back(queue->family);
        }
    }
    return families;
}

/// @brief Buffer usage with what the backend adds: device addresses for the usages that need them.
auto
buffer_usage(const VulkanDevice& device,
             BufferUsage usage) noexcept -> VkBufferUsageFlags
{
    VkBufferUsageFlags native = to_vulkan(usage);
    if (has_any(usage, BufferUsage::ACCELERATION_STRUCTURE_STORAGE | BufferUsage::ACCELERATION_STRUCTURE_BUILD_INPUT |
                           BufferUsage::SHADER_BINDING_TABLE) &&
        device.enabled(DeviceFeature::BUFFER_DEVICE_ADDRESS))
    {
        native |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    return native;
}

/// @brief The native creation information of a buffer (sharing arrays live in @p families).
auto
buffer_information(VulkanDevice& device,
                   const BufferDescription& description,
                   std::vector<std::uint32_t>& families) -> VkBufferCreateInfo
{
    families = concurrent_families(device);
    const bool concurrent = description.sharing == SharingMode::CONCURRENT && families.size() > 1;
    VkBufferCreateFlags flags = 0;
    if (description.sparse)
    {
        flags |= VK_BUFFER_CREATE_SPARSE_BINDING_BIT | VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT;
    }
    return {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .flags = flags,
        .size = description.size,
        .usage = buffer_usage(device, description.usage),
        .sharingMode = concurrent ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = concurrent ? static_cast<std::uint32_t>(families.size()) : 0U,
        .pQueueFamilyIndices = concurrent ? families.data() : nullptr,
    };
}

/// @brief The native creation information of an image.
auto
image_information(VulkanDevice& device,
                  const ImageDescription& description,
                  std::vector<std::uint32_t>& families) -> VkImageCreateInfo
{
    families = concurrent_families(device);
    const bool concurrent = description.sharing == SharingMode::CONCURRENT && families.size() > 1;
    return {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = to_vulkan(description.options),
        .imageType = to_vulkan(description.dimension),
        .format = to_vulkan(description.format),
        .extent = to_vulkan(description.extent),
        .mipLevels = description.mip_level_count,
        .arrayLayers = description.array_layer_count,
        .samples = to_vulkan(description.samples),
        .tiling = to_vulkan(description.tiling),
        .usage = to_vulkan(description.usage),
        .sharingMode = concurrent ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = concurrent ? static_cast<std::uint32_t>(families.size()) : 0U,
        .pQueueFamilyIndices = concurrent ? families.data() : nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
}

/// @brief Throws the InvalidUsageError of a resource description.
[[noreturn]] void
reject(std::string_view operation,
       std::string problem,
       std::string suggestion = {})
{
    throw InvalidUsageError({
        .backend = BackendKind::VULKAN,
        .operation = std::string{operation},
        .problem = std::move(problem),
        .suggestion = std::move(suggestion),
    });
}

/// @brief Checks that a resource with @p requirements fits at @p placement.
void
check_placement(VulkanDevice& device,
                const MemoryPlacement& placement,
                const VkMemoryRequirements& requirements,
                std::string_view operation,
                std::string_view name)
{
    const DeviceMemoryRecord& memory = device.handles().memories.get(placement.memory, operation);
    VmaAllocationInfo information{};
    vmaGetAllocationInfo(device.allocator(), memory.allocation, &information);
    if (placement.offset % requirements.alignment != 0 || placement.offset + requirements.size > memory.size ||
        (requirements.memoryTypeBits & (1U << information.memoryType)) == 0)
    {
        reject(operation,
               std::format("'{}' needs {} bytes aligned to {} in memory types {:#x}; it cannot be placed at offset {} of {}, "
                           "whose {} bytes are of memory type {}",
                           name, requirements.size, requirements.alignment, requirements.memoryTypeBits, placement.offset,
                           placement.memory, memory.size, information.memoryType),
               "place it with Device::memory_requirements() in mind: aligned offset, enough room, a compatible memory type");
    }
}

// ---------------------------------------------------------------------------------------------------
// Classes
// ---------------------------------------------------------------------------------------------------

/// @brief Memory allocated explicitly, for placed (aliased) and sparse resources.
class VulkanDeviceMemory final : public DeviceMemory
{
public:
    VulkanDeviceMemory(VulkanDevice& device, const DeviceMemoryDescription& description);
    ~VulkanDeviceMemory() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto handle() const noexcept -> DeviceMemoryHandle override
    {
        return m_handle;
    }
    [[nodiscard]] auto size() const noexcept -> std::uint64_t override
    {
        return m_size;
    }
    [[nodiscard]] auto location() const noexcept -> MemoryLocation override
    {
        return m_location;
    }

private:
    VulkanDevice& m_device;
    VmaAllocation m_allocation = nullptr;
    std::uint64_t m_size;
    MemoryLocation m_location;
    DeviceMemoryHandle m_handle;
    std::string m_debug_name;
};

/// @brief A buffer: its own VMA allocation, placed in a DeviceMemory, or sparse.
class VulkanBuffer final : public Buffer
{
public:
    VulkanBuffer(VulkanDevice& device, const BufferDescription& description);
    ~VulkanBuffer() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto handle() const noexcept -> BufferHandle override
    {
        return m_handle;
    }
    [[nodiscard]] auto size() const noexcept -> std::uint64_t override
    {
        return m_size;
    }
    [[nodiscard]] auto usage() const noexcept -> BufferUsage override
    {
        return m_usage;
    }
    [[nodiscard]] auto location() const noexcept -> MemoryLocation override
    {
        return m_location;
    }
    [[nodiscard]] auto mapped_memory() -> std::span<std::byte> override;
    void flush_mapped_range(std::uint64_t offset, std::uint64_t size) override;
    void invalidate_mapped_range(std::uint64_t offset, std::uint64_t size) override;
    [[nodiscard]] auto device_address() const -> std::uint64_t override;
    [[nodiscard]] auto bindless_index() const noexcept -> StorageBufferIndex override
    {
        return m_bindless_index;
    }

private:
    // The allocation backing a mapped range, and the range's offset in it.
    [[nodiscard]] auto mapped_allocation(std::uint64_t offset, std::uint64_t size, std::string_view operation) const
        -> std::pair<VmaAllocation, std::uint64_t>;

private:
    VulkanDevice& m_device;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = nullptr;       ///< Own allocation (null when placed or sparse).
    VmaAllocation m_placed_in = nullptr;        ///< Allocation of the DeviceMemory it is placed in.
    std::uint64_t m_placement_offset = 0;
    std::byte* m_mapped = nullptr;
    std::uint64_t m_size;
    BufferUsage m_usage;
    MemoryLocation m_location;
    VkDeviceAddress m_address = 0;
    StorageBufferIndex m_bindless_index;
    BufferHandle m_handle;
    std::string m_debug_name;
};

/// @brief An image: its own VMA allocation, placed in a DeviceMemory, or sparse.
class VulkanImage final : public Image
{
public:
    VulkanImage(VulkanDevice& device, const ImageDescription& description);
    ~VulkanImage() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto handle() const noexcept -> ImageHandle override
    {
        return m_handle;
    }
    [[nodiscard]] auto dimension() const noexcept -> ImageDimension override
    {
        return m_description.dimension;
    }
    [[nodiscard]] auto format() const noexcept -> Format override
    {
        return m_description.format;
    }
    [[nodiscard]] auto extent() const noexcept -> Extent override
    {
        return m_description.extent;
    }
    [[nodiscard]] auto mip_level_count() const noexcept -> std::uint32_t override
    {
        return m_description.mip_level_count;
    }
    [[nodiscard]] auto array_layer_count() const noexcept -> std::uint32_t override
    {
        return m_description.array_layer_count;
    }
    [[nodiscard]] auto samples() const noexcept -> SampleCount override
    {
        return m_description.samples;
    }
    [[nodiscard]] auto usage() const noexcept -> ImageUsage override
    {
        return m_description.usage;
    }

private:
    void validate(const VkImageCreateInfo& information) const;

private:
    VulkanDevice& m_device;
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = nullptr;
    ImageDescription m_description;
    ImageHandle m_handle;
    std::string m_debug_name;
};

/// @brief An image view, registered in the bindless table as a sampled and/or storage image.
class VulkanImageView final : public ImageView
{
public:
    VulkanImageView(VulkanDevice& device, const ImageViewDescription& description);
    ~VulkanImageView() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto handle() const noexcept -> ImageViewHandle override
    {
        return m_handle;
    }
    [[nodiscard]] auto image() const noexcept -> ImageHandle override
    {
        return m_image;
    }
    [[nodiscard]] auto type() const noexcept -> ImageViewType override
    {
        return m_type;
    }
    [[nodiscard]] auto format() const noexcept -> Format override
    {
        return m_format;
    }
    [[nodiscard]] auto range() const noexcept -> ImageSubresourceRange override
    {
        return m_range;
    }
    [[nodiscard]] auto sampled_index() const noexcept -> SampledImageIndex override
    {
        return m_sampled_index;
    }
    [[nodiscard]] auto storage_index() const noexcept -> StorageImageIndex override
    {
        return m_storage_index;
    }

private:
    VulkanDevice& m_device;
    VkImageView m_view = VK_NULL_HANDLE;
    ImageHandle m_image;
    ImageViewType m_type;
    Format m_format = Format::UNDEFINED;
    ImageSubresourceRange m_range;
    SampledImageIndex m_sampled_index;
    StorageImageIndex m_storage_index;
    ImageViewHandle m_handle;
    std::string m_debug_name;
};

/// @brief A sampler, registered in the bindless table.
class VulkanSampler final : public Sampler
{
public:
    VulkanSampler(VulkanDevice& device, const SamplerDescription& description);
    ~VulkanSampler() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto handle() const noexcept -> SamplerHandle override
    {
        return m_handle;
    }
    [[nodiscard]] auto bindless_index() const noexcept -> SamplerIndex override
    {
        return m_bindless_index;
    }

private:
    VulkanDevice& m_device;
    VkSampler m_sampler = VK_NULL_HANDLE;
    SamplerIndex m_bindless_index;
    SamplerHandle m_handle;
    std::string m_debug_name;
};

/// @brief A formatted view of a buffer range.
class VulkanBufferView final : public BufferView
{
public:
    VulkanBufferView(VulkanDevice& device, const BufferViewDescription& description);
    ~VulkanBufferView() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto handle() const noexcept -> BufferViewHandle override
    {
        return m_handle;
    }
    [[nodiscard]] auto buffer() const noexcept -> BufferHandle override
    {
        return m_buffer;
    }
    [[nodiscard]] auto format() const noexcept -> Format override
    {
        return m_format;
    }

private:
    VulkanDevice& m_device;
    VkBufferView m_view = VK_NULL_HANDLE;
    BufferHandle m_buffer;
    Format m_format;
    BufferViewHandle m_handle;
    std::string m_debug_name;
};

} // namespace

// ---------------------------------------------------------------------------------------------------
// VulkanDeviceMemory
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::allocate_memory(const DeviceMemoryDescription& description) -> std::unique_ptr<DeviceMemory>
{
    return std::make_unique<VulkanDeviceMemory>(*this, description);
}

VulkanDeviceMemory::VulkanDeviceMemory(VulkanDevice& device,
                                       const DeviceMemoryDescription& description)
    : DeviceMemory{}
    , m_device{device}
    , m_size{description.size}
    , m_location{description.location}
    , m_debug_name{description.debug_name}
{
    set_validation_level(device.validation_level());
    if (description.size == 0)
    {
        reject("Device::allocate_memory", std::format("device memory '{}' has no size", m_debug_name));
    }

    // Its own block, aligned for any resource placed at a multiple of 64 KiB.
    const VkMemoryRequirements requirements{.size = description.size, .alignment = 65536, .memoryTypeBits = description.memory_type_bits};
    VmaAllocationCreateInfo allocation = memory_allocation(description.location);
    allocation.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    device.check_result(vmaAllocateMemory(device.allocator(), &requirements, &allocation, &m_allocation, nullptr),
                        "Device::allocate_memory", "vmaAllocateMemory");
    m_handle = device.handles().memories.insert({.allocation = m_allocation, .size = m_size});
    if (!m_debug_name.empty())
    {
        vmaSetAllocationName(device.allocator(), m_allocation, m_debug_name.c_str());
    }
}

VulkanDeviceMemory::~VulkanDeviceMemory()
{
    (void)m_device.handles().memories.remove(m_handle);
    m_device.vulkan_deleter().enqueue([device = &m_device, allocation = m_allocation]
    {
        vmaFreeMemory(device->allocator(), allocation);
    });
}

void
VulkanDeviceMemory::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    vmaSetAllocationName(m_device.allocator(), m_allocation, m_debug_name.c_str());
}

// ---------------------------------------------------------------------------------------------------
// VulkanBuffer
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::memory_requirements(const BufferDescription& description) const -> MemoryRequirements
{
    std::vector<std::uint32_t> families;
    const VkBufferCreateInfo buffer = buffer_information(const_cast<VulkanDevice&>(*this), description, families);
    const VkDeviceBufferMemoryRequirements requirements_information{.sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS,
                                                                    .pCreateInfo = &buffer};
    VkMemoryRequirements2 requirements{.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    m_functions.vkGetDeviceBufferMemoryRequirements(m_device, &requirements_information, &requirements);
    return {.size = requirements.memoryRequirements.size,
            .alignment = requirements.memoryRequirements.alignment,
            .memory_type_bits = requirements.memoryRequirements.memoryTypeBits};
}

auto
VulkanDevice::create_buffer(const BufferDescription& description) -> std::unique_ptr<Buffer>
{
    return std::make_unique<VulkanBuffer>(*this, description);
}

VulkanBuffer::VulkanBuffer(VulkanDevice& device,
                           const BufferDescription& description)
    : Buffer{}
    , m_device{device}
    , m_size{description.size}
    , m_usage{description.usage}
    , m_location{description.location}
    , m_debug_name{description.debug_name}
{
    set_validation_level(device.validation_level());
    static constexpr std::string_view OPERATION = "Device::create_buffer";

    // A size, a usage, and the features its usage and options need.
    if (validates(ValidationLevel::CREATION))
    {
        if (description.size == 0 || description.usage == BufferUsage::NONE)
        {
            reject(OPERATION, std::format("buffer '{}' needs a size and a usage (size {}, usage {})", m_debug_name, description.size,
                                          description.usage));
        }
        if (description.sparse && description.placement)
        {
            reject(OPERATION, std::format("buffer '{}' cannot be both sparse and placed", m_debug_name),
                   "sparse buffers receive memory through Queue::bind_sparse()");
        }
    }
    if (has_any(description.usage, BufferUsage::SHADER_DEVICE_ADDRESS))
    {
        device.require(DeviceFeature::BUFFER_DEVICE_ADDRESS, OPERATION);
    }
    if (has_any(description.usage, BufferUsage::ACCELERATION_STRUCTURE_STORAGE | BufferUsage::ACCELERATION_STRUCTURE_BUILD_INPUT) &&
        !device.enabled(DeviceFeature::RAY_QUERY))
    {
        device.require(DeviceFeature::RAY_TRACING, OPERATION);
    }
    if (has_any(description.usage, BufferUsage::SHADER_BINDING_TABLE))
    {
        device.require(DeviceFeature::RAY_TRACING, OPERATION);
    }
    if (description.sparse)
    {
        device.require(DeviceFeature::SPARSE_BINDING, OPERATION);
    }

    std::vector<std::uint32_t> families;
    const VkBufferCreateInfo information = buffer_information(device, description, families);
    if (description.sparse)
    {
        // No memory: Queue::bind_sparse() binds it.
        device.check_result(device.functions().vkCreateBuffer(device.native(), &information, nullptr, &m_buffer), OPERATION,
                            "vkCreateBuffer");
    }
    else if (description.placement)
    {
        // Placed in an existing allocation (the placement is checked first).
        if (validates(ValidationLevel::CREATION))
        {
            const MemoryRequirements requirements = device.memory_requirements(description);
            check_placement(device, *description.placement,
                            {.size = requirements.size, .alignment = requirements.alignment, .memoryTypeBits = requirements.memory_type_bits},
                            OPERATION, m_debug_name);
        }
        const DeviceMemoryRecord& memory = device.handles().memories.get(description.placement->memory, OPERATION);
        device.check_result(vmaCreateAliasingBuffer2(device.allocator(), memory.allocation, description.placement->offset, &information,
                                                     &m_buffer),
                            OPERATION, "vmaCreateAliasingBuffer2");
        m_placed_in = memory.allocation;
        m_placement_offset = description.placement->offset;
        VmaAllocationInfo allocation{};
        vmaGetAllocationInfo(device.allocator(), memory.allocation, &allocation);
        if (allocation.pMappedData != nullptr)
        {
            m_mapped = static_cast<std::byte*>(allocation.pMappedData) + m_placement_offset;
        }
    }
    else
    {
        // Ray tracing reads addresses, which must be aligned: structure storage to 256 bytes, build
        // scratch (any addressable storage buffer) to the driver's scratch alignment, shader binding
        // tables to the group base alignment.
        const PhysicalDeviceCapabilities& capabilities = device.vulkan_physical_device().capabilities();
        VkDeviceSize alignment = 0;
        if (has_any(description.usage, BufferUsage::ACCELERATION_STRUCTURE_STORAGE))
        {
            alignment = 256;
        }
        if (has_all(description.usage, BufferUsage::STORAGE | BufferUsage::SHADER_DEVICE_ADDRESS) &&
            (device.enabled(DeviceFeature::RAY_TRACING) || device.enabled(DeviceFeature::RAY_QUERY)))
        {
            alignment = std::max<VkDeviceSize>(alignment, capabilities.acceleration_structure_properties.minAccelerationStructureScratchOffsetAlignment);
        }
        if (has_any(description.usage, BufferUsage::SHADER_BINDING_TABLE))
        {
            alignment = std::max<VkDeviceSize>(alignment, capabilities.ray_tracing_properties.shaderGroupBaseAlignment);
        }

        // Its own allocation, persistently mapped when the CPU can reach it.
        const VmaAllocationCreateInfo allocation_information = resource_allocation(description.location);
        VmaAllocationInfo allocation{};
        device.check_result(vmaCreateBufferWithAlignment(device.allocator(), &information, &allocation_information, alignment, &m_buffer,
                                                         &m_allocation, &allocation),
                            OPERATION, "vmaCreateBufferWithAlignment");
        m_mapped = static_cast<std::byte*>(allocation.pMappedData);
    }

    // Its address, its bindless index, its handle.
    if ((information.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0)
    {
        const VkBufferDeviceAddressInfo address{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = m_buffer};
        m_address = device.functions().vkGetBufferDeviceAddress(device.native(), &address);
    }
    if (description.bindless && has_any(description.usage, BufferUsage::STORAGE))
    {
        m_bindless_index = device.bindless().add_storage_buffer(m_buffer);
    }
    m_handle = device.handles().buffers.insert({.buffer = m_buffer, .size = m_size, .address = m_address, .usage = m_usage});
    set_debug_name(m_debug_name);
}

VulkanBuffer::~VulkanBuffer()
{
    (void)m_device.handles().buffers.remove(m_handle);
    if (m_bindless_index.is_valid())
    {
        m_device.bindless().release_later(BindlessResourceKind::STORAGE_BUFFER, m_bindless_index.value());
    }
    m_device.vulkan_deleter().enqueue([device = &m_device, buffer = m_buffer, allocation = m_allocation]
    {
        if (allocation != nullptr)
        {
            vmaDestroyBuffer(device->allocator(), buffer, allocation);
        }
        else
        {
            device->functions().vkDestroyBuffer(device->native(), buffer, nullptr);
        }
    });
}

void
VulkanBuffer::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    m_device.name_object(VK_OBJECT_TYPE_BUFFER, m_buffer, m_debug_name);
    if (m_allocation != nullptr && !m_debug_name.empty())
    {
        vmaSetAllocationName(m_device.allocator(), m_allocation, m_debug_name.c_str());
    }
}

auto
VulkanBuffer::mapped_memory() -> std::span<std::byte>
{
    if (m_mapped == nullptr)
    {
        reject("Buffer::mapped_memory",
               std::format("buffer '{}' lives in {} memory, which the CPU cannot access", m_debug_name, m_location),
               "create it with MemoryLocation::HOST_UPLOAD, HOST_READBACK or DEVICE_LOCAL_HOST_VISIBLE, or copy through a "
               "staging buffer (Uploader)");
    }
    return {m_mapped, static_cast<std::size_t>(m_size)};
}

auto
VulkanBuffer::mapped_allocation(std::uint64_t offset, std::uint64_t size, std::string_view operation) const
    -> std::pair<VmaAllocation, std::uint64_t>
{
    if (m_mapped == nullptr)
    {
        reject(operation, std::format("buffer '{}' is not mapped", m_debug_name));
    }
    const std::uint64_t length = size == WHOLE_SIZE ? m_size - std::min(offset, m_size) : size;
    if (offset + length > m_size)
    {
        reject(operation, std::format("range [{}, {}) is outside buffer '{}' of {} bytes", offset, offset + length, m_debug_name, m_size));
    }
    return m_allocation != nullptr ? std::pair{m_allocation, offset} : std::pair{m_placed_in, m_placement_offset + offset};
}

void
VulkanBuffer::flush_mapped_range(std::uint64_t offset,
                                 std::uint64_t size)
{
    const auto [allocation, allocation_offset] = mapped_allocation(offset, size, "Buffer::flush_mapped_range");
    const std::uint64_t length = size == WHOLE_SIZE ? m_size - offset : size;
    m_device.check_result(vmaFlushAllocation(m_device.allocator(), allocation, allocation_offset, length), "Buffer::flush_mapped_range",
                          "vmaFlushAllocation");
}

void
VulkanBuffer::invalidate_mapped_range(std::uint64_t offset,
                                      std::uint64_t size)
{
    const auto [allocation, allocation_offset] = mapped_allocation(offset, size, "Buffer::invalidate_mapped_range");
    const std::uint64_t length = size == WHOLE_SIZE ? m_size - offset : size;
    m_device.check_result(vmaInvalidateAllocation(m_device.allocator(), allocation, allocation_offset, length),
                          "Buffer::invalidate_mapped_range", "vmaInvalidateAllocation");
}

auto
VulkanBuffer::device_address() const -> std::uint64_t
{
    if (m_address == 0)
    {
        reject("Buffer::device_address", std::format("buffer '{}' has no device address", m_debug_name),
               "create it with BufferUsage::SHADER_DEVICE_ADDRESS");
    }
    return m_address;
}

// ---------------------------------------------------------------------------------------------------
// VulkanImage
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::memory_requirements(const ImageDescription& description) const -> MemoryRequirements
{
    std::vector<std::uint32_t> families;
    const VkImageCreateInfo image = image_information(const_cast<VulkanDevice&>(*this), description, families);
    const VkDeviceImageMemoryRequirements requirements_information{.sType = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS,
                                                                   .pCreateInfo = &image};
    VkMemoryRequirements2 requirements{.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    m_functions.vkGetDeviceImageMemoryRequirements(m_device, &requirements_information, &requirements);
    return {.size = requirements.memoryRequirements.size,
            .alignment = requirements.memoryRequirements.alignment,
            .memory_type_bits = requirements.memoryRequirements.memoryTypeBits};
}

auto
VulkanDevice::create_image(const ImageDescription& description) -> std::unique_ptr<Image>
{
    return std::make_unique<VulkanImage>(*this, description);
}

VulkanImage::VulkanImage(VulkanDevice& device,
                         const ImageDescription& description)
    : Image{}
    , m_device{device}
    , m_description{description}
    , m_debug_name{description.debug_name}
{
    set_validation_level(device.validation_level());
    static constexpr std::string_view OPERATION = "Device::create_image";
    m_description.debug_name = {};
    m_description.placement.reset();

    std::vector<std::uint32_t> families;
    const VkImageCreateInfo information = image_information(device, description, families);
    if (validates(ValidationLevel::CREATION))
    {
        validate(information);
    }
    if (has_any(description.usage, ImageUsage::SHADING_RATE_ATTACHMENT))
    {
        device.require(DeviceFeature::VARIABLE_RATE_SHADING, OPERATION);
    }

    const bool sparse = has_any(description.options, ImageCreationOption::SPARSE_RESIDENCY);
    if (sparse)
    {
        // No memory: Queue::bind_sparse() binds its tiles.
        device.require(DeviceFeature::SPARSE_BINDING, OPERATION);
        device.check_result(device.functions().vkCreateImage(device.native(), &information, nullptr, &m_image), OPERATION, "vkCreateImage");
    }
    else if (description.placement)
    {
        if (validates(ValidationLevel::CREATION))
        {
            const MemoryRequirements requirements = device.memory_requirements(description);
            check_placement(device, *description.placement,
                            {.size = requirements.size, .alignment = requirements.alignment, .memoryTypeBits = requirements.memory_type_bits},
                            OPERATION, m_debug_name);
        }
        const DeviceMemoryRecord& memory = device.handles().memories.get(description.placement->memory, OPERATION);
        device.check_result(vmaCreateAliasingImage2(device.allocator(), memory.allocation, description.placement->offset, &information, &m_image),
                            OPERATION, "vmaCreateAliasingImage2");
    }
    else
    {
        // Its own allocation; transient attachments prefer lazily allocated memory (tile memory on mobile GPUs).
        VmaAllocationCreateInfo allocation = resource_allocation(description.location);
        if (has_any(description.usage, ImageUsage::TRANSIENT_ATTACHMENT))
        {
            allocation.preferredFlags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
        }
        device.check_result(vmaCreateImage(device.allocator(), &information, &allocation, &m_image, &m_allocation, nullptr), OPERATION,
                            "vmaCreateImage");
    }

    m_handle = device.handles().images.insert({
        .image = m_image,
        .format = description.format,
        .aspects = format_aspects(description.format),
        .extent = description.extent,
        .mip_level_count = description.mip_level_count,
        .array_layer_count = description.array_layer_count,
        .samples = description.samples,
        .usage = description.usage,
    });
    set_debug_name(m_debug_name);
}

void
VulkanImage::validate(const VkImageCreateInfo& information) const
{
    static constexpr std::string_view OPERATION = "Device::create_image";
    const ImageDescription& description = m_description;

    // Shape: a format, a usage, a size that fits the dimension.
    const Extent& extent = description.extent;
    const bool shape_fits = description.format != Format::UNDEFINED && description.usage != ImageUsage::NONE && extent.width > 0 &&
                            extent.height > 0 && extent.depth > 0 && description.mip_level_count > 0 &&
                            description.array_layer_count > 0 &&
                            (description.dimension != ImageDimension::ONE_DIMENSIONAL || (extent.height == 1 && extent.depth == 1)) &&
                            (description.dimension != ImageDimension::TWO_DIMENSIONAL || extent.depth == 1) &&
                            (description.dimension != ImageDimension::THREE_DIMENSIONAL || description.array_layer_count == 1);
    if (!shape_fits)
    {
        reject(OPERATION,
               std::format("image '{}' ({}, {}, {}x{}x{}, {} mip level(s), {} layer(s), usage {}) is malformed: it needs a format, "
                           "a usage, a non-zero size whose unused dimensions are 1, and one layer when three-dimensional",
                           m_debug_name, description.dimension, description.format, extent.width, extent.height, extent.depth,
                           description.mip_level_count, description.array_layer_count, description.usage));
    }

    // Mip levels: at most a full chain.
    const std::uint32_t largest = std::max({extent.width, extent.height, extent.depth});
    const std::uint32_t full_chain = static_cast<std::uint32_t>(std::bit_width(largest));
    if (description.mip_level_count > full_chain)
    {
        reject(OPERATION, std::format("image '{}' of {}x{}x{} has at most {} mip levels, not {}", m_debug_name, extent.width,
                                      extent.height, extent.depth, full_chain, description.mip_level_count));
    }

    // The device must support this combination of format, type, tiling, usage and size.
    VkImageFormatProperties2 properties{.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
    const VkPhysicalDeviceImageFormatInfo2 query{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .format = information.format,
        .type = information.imageType,
        .tiling = information.tiling,
        .usage = information.usage,
        .flags = information.flags,
    };
    const VkResult result =
        vkGetPhysicalDeviceImageFormatProperties2(m_device.vulkan_physical_device().native(), &query, &properties);
    const VkImageFormatProperties& limits = properties.imageFormatProperties;
    if (result == VK_ERROR_FORMAT_NOT_SUPPORTED || extent.width > limits.maxExtent.width || extent.height > limits.maxExtent.height ||
        extent.depth > limits.maxExtent.depth || description.mip_level_count > limits.maxMipLevels ||
        description.array_layer_count > limits.maxArrayLayers ||
        (limits.sampleCounts & static_cast<VkSampleCountFlags>(information.samples)) == 0)
    {
        throw BackendCapabilityError({
            .backend = BackendKind::VULKAN,
            .operation = std::string{OPERATION},
            .capability = std::format("{} {} image with usage {}", description.format, description.dimension, description.usage),
            .reason = result == VK_ERROR_FORMAT_NOT_SUPPORTED
                          ? std::format("'{}' does not support this format with this usage, tiling and options",
                                        m_device.physical_device().properties().name)
                          : std::format("'{}' allows at most {}x{}x{}, {} mip levels, {} layers and sample counts {} for it",
                                        m_device.physical_device().properties().name, limits.maxExtent.width,
                                        limits.maxExtent.height, limits.maxExtent.depth, limits.maxMipLevels, limits.maxArrayLayers,
                                        from_vulkan(limits.sampleCounts)),
            .suggestion = "check PhysicalDevice::format_support(), or choose another format or usage",
        });
    }
    m_device.check_result(result, OPERATION, "vkGetPhysicalDeviceImageFormatProperties2");
}

VulkanImage::~VulkanImage()
{
    (void)m_device.handles().images.remove(m_handle);
    m_device.vulkan_deleter().enqueue([device = &m_device, image = m_image, allocation = m_allocation]
    {
        if (allocation != nullptr)
        {
            vmaDestroyImage(device->allocator(), image, allocation);
        }
        else
        {
            device->functions().vkDestroyImage(device->native(), image, nullptr);
        }
    });
}

void
VulkanImage::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    m_device.name_object(VK_OBJECT_TYPE_IMAGE, m_image, m_debug_name);
    if (m_allocation != nullptr && !m_debug_name.empty())
    {
        vmaSetAllocationName(m_device.allocator(), m_allocation, m_debug_name.c_str());
    }
}

// ---------------------------------------------------------------------------------------------------
// VulkanImageView
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::create_image_view(const ImageViewDescription& description) -> std::unique_ptr<ImageView>
{
    return std::make_unique<VulkanImageView>(*this, description);
}

VulkanImageView::VulkanImageView(VulkanDevice& device,
                                 const ImageViewDescription& description)
    : ImageView{}
    , m_device{device}
    , m_image{description.image}
    , m_type{description.type}
    , m_debug_name{description.debug_name}
{
    set_validation_level(device.validation_level());
    static constexpr std::string_view OPERATION = "Device::create_image_view";
    const ImageRecord& image = device.handles().images.get(description.image, OPERATION);

    // The range, resolved: the image's format, every remaining level and layer, and the format's aspects
    // when the default COLOR does not apply.
    m_format = description.format == Format::UNDEFINED ? image.format : description.format;
    m_range = description.range;
    const bool cube = m_type == ImageViewType::CUBE || m_type == ImageViewType::CUBE_ARRAY;
    const bool arrayed = m_type == ImageViewType::ONE_DIMENSIONAL_ARRAY || m_type == ImageViewType::TWO_DIMENSIONAL_ARRAY ||
                         m_type == ImageViewType::CUBE_ARRAY;
    if (m_range.mip_level_count == REMAINING_MIP_LEVELS)
    {
        m_range.mip_level_count = m_range.base_mip_level < image.mip_level_count ? image.mip_level_count - m_range.base_mip_level : 0U;
    }
    if (m_range.array_layer_count == REMAINING_ARRAY_LAYERS)
    {
        // Views that are not arrays see one layer (a cube six), whatever the image holds.
        const std::uint32_t remaining = m_range.base_array_layer < image.array_layer_count
                                            ? image.array_layer_count - m_range.base_array_layer
                                            : 0U;
        m_range.array_layer_count = m_type == ImageViewType::CUBE ? 6U : arrayed ? remaining : std::min(remaining, 1U);
    }
    if (m_range.aspects == ImageAspect::COLOR && !has_any(image.aspects, ImageAspect::COLOR))
    {
        m_range.aspects = image.aspects;
    }
    if (validates(ValidationLevel::CREATION))
    {
        const bool layers_fit_type = cube ? m_range.array_layer_count % 6 == 0 && (arrayed || m_range.array_layer_count == 6)
                                          : arrayed || m_range.array_layer_count == 1;
        if (std::uint64_t{m_range.base_mip_level} + m_range.mip_level_count > image.mip_level_count ||
            std::uint64_t{m_range.base_array_layer} + m_range.array_layer_count > image.array_layer_count ||
            m_range.mip_level_count == 0 || m_range.array_layer_count == 0 || !has_all(image.aspects, m_range.aspects) ||
            !layers_fit_type)
        {
            reject(OPERATION,
                   std::format("{} view '{}' of levels [{}, +{}), layers [{}, +{}) and aspects {} does not fit the image (levels {}, "
                               "layers {}, aspects {}); a view that is not an array sees one layer, a cube six",
                               m_type, m_debug_name, m_range.base_mip_level, m_range.mip_level_count, m_range.base_array_layer,
                               m_range.array_layer_count, m_range.aspects, image.mip_level_count, image.array_layer_count, image.aspects));
        }
    }

    const VkImageViewCreateInfo information{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image.image,
        .viewType = to_vulkan(m_type),
        .format = to_vulkan(m_format),
        .components = to_vulkan(description.components),
        .subresourceRange = to_vulkan(m_range),
    };
    device.check_result(device.functions().vkCreateImageView(device.native(), &information, nullptr, &m_view), OPERATION,
                        "vkCreateImageView");

    // The bindless table: sampled and storage views of one aspect.
    if (description.bindless && std::has_single_bit(std::to_underlying(m_range.aspects)))
    {
        if (has_any(image.usage, ImageUsage::SAMPLED))
        {
            m_sampled_index = device.bindless().add_sampled_image(m_view);
        }
        if (has_any(image.usage, ImageUsage::STORAGE))
        {
            m_storage_index = device.bindless().add_storage_image(m_view);
        }
    }
    m_handle = device.handles().image_views.insert(
        {.view = m_view, .image = m_image, .format = m_format, .range = m_range, .samples = image.samples});
    device.name_object(VK_OBJECT_TYPE_IMAGE_VIEW, m_view, m_debug_name);
}

VulkanImageView::~VulkanImageView()
{
    (void)m_device.handles().image_views.remove(m_handle);
    if (m_sampled_index.is_valid())
    {
        m_device.bindless().release_later(BindlessResourceKind::SAMPLED_IMAGE, m_sampled_index.value());
    }
    if (m_storage_index.is_valid())
    {
        m_device.bindless().release_later(BindlessResourceKind::STORAGE_IMAGE, m_storage_index.value());
    }
    m_device.vulkan_deleter().enqueue([device = &m_device, view = m_view]
    {
        device->functions().vkDestroyImageView(device->native(), view, nullptr);
    });
}

void
VulkanImageView::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    m_device.name_object(VK_OBJECT_TYPE_IMAGE_VIEW, m_view, m_debug_name);
}

// ---------------------------------------------------------------------------------------------------
// VulkanSampler
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::create_sampler(const SamplerDescription& description) -> std::unique_ptr<Sampler>
{
    return std::make_unique<VulkanSampler>(*this, description);
}

VulkanSampler::VulkanSampler(VulkanDevice& device,
                             const SamplerDescription& description)
    : Sampler{}
    , m_device{device}
    , m_debug_name{description.debug_name}
{
    set_validation_level(device.validation_level());
    static constexpr std::string_view OPERATION = "Device::create_sampler";

    // Anisotropy and reduction need their features; anisotropy stays within the limit.
    const float anisotropy = description.maximum_anisotropy.value_or(1.0F);
    if (description.maximum_anisotropy)
    {
        device.require(DeviceFeature::SAMPLER_ANISOTROPY, OPERATION);
        const float limit = device.physical_device().properties().limits.maximum_sampler_anisotropy;
        if (validates(ValidationLevel::CREATION) && (anisotropy < 1.0F || anisotropy > limit))
        {
            reject(OPERATION, std::format("sampler '{}': anisotropy {} is outside [1, {}]", m_debug_name, anisotropy, limit));
        }
    }
    if (description.reduction_mode != SamplerReductionMode::WEIGHTED_AVERAGE)
    {
        device.require(DeviceFeature::SAMPLER_FILTER_MINMAX, OPERATION);
    }

    const VkSamplerReductionModeCreateInfo reduction{.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
                                                     .reductionMode = to_vulkan(description.reduction_mode)};
    const VkSamplerCreateInfo information{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = description.reduction_mode != SamplerReductionMode::WEIGHTED_AVERAGE ? &reduction : nullptr,
        .magFilter = to_vulkan(description.magnification_filter),
        .minFilter = to_vulkan(description.minification_filter),
        .mipmapMode = to_vulkan(description.mipmap_mode),
        .addressModeU = to_vulkan(description.address_mode_u),
        .addressModeV = to_vulkan(description.address_mode_v),
        .addressModeW = to_vulkan(description.address_mode_w),
        .mipLodBias = description.level_of_detail_bias,
        .anisotropyEnable = description.maximum_anisotropy ? VK_TRUE : VK_FALSE,
        .maxAnisotropy = anisotropy,
        .compareEnable = description.compare_operation ? VK_TRUE : VK_FALSE,
        .compareOp = to_vulkan(description.compare_operation.value_or(CompareOperation::ALWAYS)),
        .minLod = description.minimum_level_of_detail,
        .maxLod = description.maximum_level_of_detail,
        .borderColor = to_vulkan(description.border_color),
        .unnormalizedCoordinates = description.unnormalized_coordinates ? VK_TRUE : VK_FALSE,
    };
    device.check_result(device.functions().vkCreateSampler(device.native(), &information, nullptr, &m_sampler), OPERATION,
                        "vkCreateSampler");
    if (description.bindless)
    {
        m_bindless_index = device.bindless().add_sampler(m_sampler);
    }
    m_handle = device.handles().samplers.insert({.sampler = m_sampler});
    device.name_object(VK_OBJECT_TYPE_SAMPLER, m_sampler, m_debug_name);
}

VulkanSampler::~VulkanSampler()
{
    (void)m_device.handles().samplers.remove(m_handle);
    if (m_bindless_index.is_valid())
    {
        m_device.bindless().release_later(BindlessResourceKind::SAMPLER, m_bindless_index.value());
    }
    m_device.vulkan_deleter().enqueue([device = &m_device, sampler = m_sampler]
    {
        device->functions().vkDestroySampler(device->native(), sampler, nullptr);
    });
}

void
VulkanSampler::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    m_device.name_object(VK_OBJECT_TYPE_SAMPLER, m_sampler, m_debug_name);
}

// ---------------------------------------------------------------------------------------------------
// VulkanBufferView
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::create_buffer_view(const BufferViewDescription& description) -> std::unique_ptr<BufferView>
{
    return std::make_unique<VulkanBufferView>(*this, description);
}

VulkanBufferView::VulkanBufferView(VulkanDevice& device,
                                   const BufferViewDescription& description)
    : BufferView{}
    , m_device{device}
    , m_buffer{description.buffer}
    , m_format{description.format}
    , m_debug_name{description.debug_name}
{
    set_validation_level(device.validation_level());
    static constexpr std::string_view OPERATION = "Device::create_buffer_view";
    const BufferRecord& buffer = device.handles().buffers.get(description.buffer, OPERATION);

    // A texel buffer usage the format supports, and an aligned range inside the buffer.
    if (validates(ValidationLevel::CREATION))
    {
        const BufferUsage texel_usage = buffer.usage & (BufferUsage::UNIFORM_TEXEL | BufferUsage::STORAGE_TEXEL);
        const BufferUsage supported = device.physical_device().format_support(description.format).buffer_usage;
        const std::uint64_t alignment = device.physical_device().properties().limits.minimum_texel_buffer_offset_alignment;
        const std::uint64_t size = description.size == WHOLE_SIZE ? buffer.size - std::min(description.offset, buffer.size) : description.size;
        if (texel_usage == BufferUsage::NONE || !has_all(supported, texel_usage) || description.offset % alignment != 0 ||
            description.offset + size > buffer.size || size == 0)
        {
            reject(OPERATION,
                   std::format("buffer view '{}' of {} over [{}, {}) is invalid: the buffer needs UNIFORM_TEXEL or STORAGE_TEXEL usage "
                               "(it has {}) that the format supports (it supports {}), and the offset a multiple of {} inside its {} "
                               "bytes",
                               m_debug_name, description.format, description.offset, description.offset + size, buffer.usage, supported,
                               alignment, buffer.size));
        }
    }

    const VkBufferViewCreateInfo information{
        .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
        .buffer = buffer.buffer,
        .format = to_vulkan(description.format),
        .offset = description.offset,
        .range = description.size == WHOLE_SIZE ? VK_WHOLE_SIZE : description.size,
    };
    device.check_result(device.functions().vkCreateBufferView(device.native(), &information, nullptr, &m_view), OPERATION,
                        "vkCreateBufferView");
    m_handle = device.handles().buffer_views.insert({.view = m_view});
    device.name_object(VK_OBJECT_TYPE_BUFFER_VIEW, m_view, m_debug_name);
}

VulkanBufferView::~VulkanBufferView()
{
    (void)m_device.handles().buffer_views.remove(m_handle);
    m_device.vulkan_deleter().enqueue([device = &m_device, view = m_view]
    {
        device->functions().vkDestroyBufferView(device->native(), view, nullptr);
    });
}

void
VulkanBufferView::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    m_device.name_object(VK_OBJECT_TYPE_BUFFER_VIEW, m_view, m_debug_name);
}

// ---------------------------------------------------------------------------------------------------
// Memory queries
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::memory_budget() const -> std::vector<MemoryHeapBudget>
{
    // VMA reads VK_EXT_memory_budget when it is enabled, and estimates the budget otherwise.
    const VkPhysicalDeviceMemoryProperties& memory = m_physical_device.capabilities().memory_properties;
    std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
    vmaGetHeapBudgets(m_allocator, budgets.data());
    std::vector<MemoryHeapBudget> heaps;
    for (std::uint32_t heap = 0; heap < memory.memoryHeapCount; ++heap)
    {
        heaps.push_back({
            .properties = (memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0 ? MemoryProperty::DEVICE_LOCAL
                                                                                                    : MemoryProperty::NONE,
            .size = memory.memoryHeaps[heap].size,
            .budget = budgets[heap].budget,
            .usage = budgets[heap].usage,
        });
    }
    return heaps;
}

auto
VulkanDevice::memory_statistics() const -> MemoryStatistics
{
    VmaTotalStatistics statistics{};
    vmaCalculateStatistics(m_allocator, &statistics);
    return {
        .block_count = statistics.total.statistics.blockCount,
        .allocation_count = statistics.total.statistics.allocationCount,
        .block_bytes = statistics.total.statistics.blockBytes,
        .allocation_bytes = statistics.total.statistics.allocationBytes,
    };
}

auto
VulkanDevice::sparse_image_requirements(ImageHandle image) const -> SparseImageRequirements
{
    static constexpr std::string_view OPERATION = "Device::sparse_image_requirements";
    const ImageRecord& record = const_cast<VulkanDevice*>(this)->handles().images.get(image, OPERATION);

    // The first aspect's tiles and mip tail; tiles are as large as the memory alignment.
    const VkImageSparseMemoryRequirementsInfo2 query{.sType = VK_STRUCTURE_TYPE_IMAGE_SPARSE_MEMORY_REQUIREMENTS_INFO_2, .image = record.image};
    std::uint32_t count = 0;
    m_functions.vkGetImageSparseMemoryRequirements2(m_device, &query, &count, nullptr);
    std::vector<VkSparseImageMemoryRequirements2> requirements(count, {.sType = VK_STRUCTURE_TYPE_SPARSE_IMAGE_MEMORY_REQUIREMENTS_2});
    m_functions.vkGetImageSparseMemoryRequirements2(m_device, &query, &count, requirements.data());
    if (requirements.empty())
    {
        reject(OPERATION, std::format("{} is not a sparse image", image), "create it with ImageCreationOption::SPARSE_RESIDENCY");
    }
    VkMemoryRequirements memory{};
    m_functions.vkGetImageMemoryRequirements(m_device, record.image, &memory);
    const VkSparseImageMemoryRequirements& first = requirements.front().memoryRequirements;
    ImageAspect aspects = ImageAspect::NONE;
    if ((first.formatProperties.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) != 0)
    {
        aspects |= ImageAspect::COLOR;
    }
    if ((first.formatProperties.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0)
    {
        aspects |= ImageAspect::DEPTH;
    }
    if ((first.formatProperties.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
    {
        aspects |= ImageAspect::STENCIL;
    }
    return {
        .aspects = aspects,
        .tile_extent = {.width = first.formatProperties.imageGranularity.width,
                        .height = first.formatProperties.imageGranularity.height,
                        .depth = first.formatProperties.imageGranularity.depth},
        .mip_tail_first_level = first.imageMipTailFirstLod,
        .mip_tail_size = first.imageMipTailSize,
        .mip_tail_offset = first.imageMipTailOffset,
        .mip_tail_stride = (first.formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT) != 0 ? 0 : first.imageMipTailStride,
        .tile_size = memory.alignment,
    };
}

// ---------------------------------------------------------------------------------------------------
// Sparse binding
// ---------------------------------------------------------------------------------------------------

void
VulkanQueue::bind_sparse(const SparseBindDescription& description)
{
    static constexpr std::string_view OPERATION = "Queue::bind_sparse";
    m_device.require(DeviceFeature::SPARSE_BINDING, OPERATION);
    if (!has_any(m_native.capabilities, QueueCapability::SPARSE_BINDING))
    {
        throw_unsupported(OPERATION, "sparse binding", std::format("'{}' cannot bind sparse memory", m_debug_name),
                          "request a QueueType::SPARSE_BINDING queue and bind through it");
    }
    HandleTables& handles = m_device.handles();

    // Memory of a binding: the allocation's block and offset (none to unbind).
    const auto memory_of = [this, &handles](DeviceMemoryHandle handle, std::uint64_t offset) -> std::pair<VkDeviceMemory, VkDeviceSize>
    {
        if (!handle.is_valid())
        {
            return {VK_NULL_HANDLE, 0};
        }
        VmaAllocationInfo information{};
        vmaGetAllocationInfo(m_device.allocator(), handles.memories.get(handle, OPERATION).allocation, &information);
        return {information.deviceMemory, information.offset + offset};
    };

    // One bind per range, grouped per resource as Vulkan wants them.
    std::vector<VkSparseMemoryBind> buffer_binds;
    std::vector<VkSparseBufferMemoryBindInfo> buffers;
    for (const SparseBufferBind& bind : description.buffer_binds)
    {
        const auto [memory, offset] = memory_of(bind.memory, bind.memory_offset);
        buffer_binds.push_back({.resourceOffset = bind.resource_offset, .size = bind.size, .memory = memory, .memoryOffset = offset});
    }
    for (std::size_t index = 0; index < description.buffer_binds.size(); ++index)
    {
        buffers.push_back({.buffer = handles.buffers.get(description.buffer_binds[index].buffer, OPERATION).buffer,
                           .bindCount = 1,
                           .pBinds = &buffer_binds[index]});
    }
    std::vector<VkSparseMemoryBind> opaque_binds;
    std::vector<VkSparseImageOpaqueMemoryBindInfo> opaque;
    for (const SparseImageOpaqueBind& bind : description.image_opaque_binds)
    {
        const auto [memory, offset] = memory_of(bind.memory, bind.memory_offset);
        opaque_binds.push_back({.resourceOffset = bind.resource_offset, .size = bind.size, .memory = memory, .memoryOffset = offset});
    }
    for (std::size_t index = 0; index < description.image_opaque_binds.size(); ++index)
    {
        opaque.push_back({.image = handles.images.get(description.image_opaque_binds[index].image, OPERATION).image,
                          .bindCount = 1,
                          .pBinds = &opaque_binds[index]});
    }
    std::vector<VkSparseImageMemoryBind> image_binds;
    std::vector<VkSparseImageMemoryBindInfo> images;
    for (const SparseImageBind& bind : description.image_binds)
    {
        const auto [memory, offset] = memory_of(bind.memory, bind.memory_offset);
        image_binds.push_back({
            .subresource = {.aspectMask = to_vulkan(bind.subresource.aspects),
                            .mipLevel = bind.subresource.mip_level,
                            .arrayLayer = bind.subresource.base_array_layer},
            .offset = to_vulkan(bind.offset),
            .extent = to_vulkan(bind.extent),
            .memory = memory,
            .memoryOffset = offset,
        });
    }
    for (std::size_t index = 0; index < description.image_binds.size(); ++index)
    {
        images.push_back({.image = handles.images.get(description.image_binds[index].image, OPERATION).image,
                          .bindCount = 1,
                          .pBinds = &image_binds[index]});
    }

    // Timeline waits and signals, plus the queue's progress.
    std::vector<VkSemaphore> wait_semaphores;
    std::vector<std::uint64_t> wait_values;
    for (const TimelineSemaphoreWait& wait : description.timeline_waits)
    {
        wait_semaphores.push_back(handles.timeline_semaphores.get(wait.semaphore, OPERATION).semaphore);
        wait_values.push_back(wait.value);
    }
    std::vector<VkSemaphore> signal_semaphores;
    std::vector<std::uint64_t> signal_values;
    for (const TimelineSemaphoreSignal& signal : description.timeline_signals)
    {
        signal_semaphores.push_back(handles.timeline_semaphores.get(signal.semaphore, OPERATION).semaphore);
        signal_values.push_back(signal.value);
    }

    const std::scoped_lock lock{m_native.mutex};

    // Refuse an order of timeline waits and signals that would deadlock the hardware queue.
    const bool ordered = validates(ValidationLevel::CREATION);
    const std::array timeline_batch{TimelineBatch{.waits = description.timeline_waits, .signals = description.timeline_signals}};
    if (ordered)
    {
        order_timeline_batches(timeline_batch, OPERATION);
    }

    // The queue's progress is signaled with the binding.
    const std::uint64_t progress = m_native.submitted.load(std::memory_order_relaxed) + 1;
    signal_semaphores.push_back(m_native.progress);
    signal_values.push_back(progress);
    const VkTimelineSemaphoreSubmitInfo values{
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .waitSemaphoreValueCount = static_cast<std::uint32_t>(wait_values.size()),
        .pWaitSemaphoreValues = wait_values.data(),
        .signalSemaphoreValueCount = static_cast<std::uint32_t>(signal_values.size()),
        .pSignalSemaphoreValues = signal_values.data(),
    };
    const VkBindSparseInfo bind{
        .sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO,
        .pNext = &values,
        .waitSemaphoreCount = static_cast<std::uint32_t>(wait_semaphores.size()),
        .pWaitSemaphores = wait_semaphores.data(),
        .bufferBindCount = static_cast<std::uint32_t>(buffers.size()),
        .pBufferBinds = buffers.data(),
        .imageOpaqueBindCount = static_cast<std::uint32_t>(opaque.size()),
        .pImageOpaqueBinds = opaque.data(),
        .imageBindCount = static_cast<std::uint32_t>(images.size()),
        .pImageBinds = images.data(),
        .signalSemaphoreCount = static_cast<std::uint32_t>(signal_semaphores.size()),
        .pSignalSemaphores = signal_semaphores.data(),
    };
    m_device.check_result(m_device.functions().vkQueueBindSparse(m_native.queue, 1, &bind, VK_NULL_HANDLE), OPERATION,
                          "vkQueueBindSparse");
    m_native.submitted.store(progress, std::memory_order_release);

    // What the binding promised, and the waits the hardware queue may now be stalled on.
    if (ordered)
    {
        record_timeline_batches(timeline_batch);
    }
}

} // namespace gfx::vulkan
