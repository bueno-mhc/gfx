/// @file vulkan_bindless.cpp
/// @brief BindlessTable: the device-wide descriptor set holding every bindless resource.
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

constexpr auto
index_of(BindlessResourceKind kind) noexcept -> std::size_t
{
    return static_cast<std::size_t>(std::to_underlying(kind));
}

} // namespace

BindlessTable::BindlessTable(VulkanDevice& device,
                             const BindlessTableDescription& description,
                             bool acceleration_structures)
    : m_device{device}
    , m_description{description}
{
    // Get the device's limits and functions, and the kinds of bindless resources it can hold.
    const DeviceLimits& limits = device.physical_device().properties().limits;
    const VolkDeviceTable& functions = device.functions();

    // The kinds this device holds, each within the device's limits.
    m_kinds = {BindlessResourceKind::SAMPLED_IMAGE, 
               BindlessResourceKind::STORAGE_IMAGE, 
               BindlessResourceKind::SAMPLER,
               BindlessResourceKind::STORAGE_BUFFER};

    // Check if the device supports bindless acceleration structures, and add them if requested.
    if (acceleration_structures)
    {
        m_kinds.push_back(BindlessResourceKind::ACCELERATION_STRUCTURE);
    }

    // Check that the requested capacities are within the device's limits.
    for (const BindlessResourceKind kind : m_kinds)
    {
        // Get the requested capacity and the device's maximum for this kind.
        const std::uint32_t capacity = description.capacity(kind);
        const std::uint32_t maximum = limits.maximum_bindless_resources[index_of(kind)];

        // Check if the requested capacity is valid.
        if (capacity == 0 || capacity > maximum)
        {
            throw BackendCapabilityError({
                .backend = BackendKind::VULKAN,
                .operation = "PhysicalDevice::create_device",
                .capability = std::format("bindless {} array of {} descriptors", kind, capacity),
                .reason = std::format("'{}' allows between 1 and {} bindless {} descriptors", device.physical_device().properties().name,
                                      maximum, kind),
                .suggestion = "set DeviceDescription::bindless within DeviceLimits::maximum_bindless_resources",
            });
        }
    }

    // One partially bound, update-after-bind array per kind, visible to every stage.
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<VkDescriptorBindingFlags> flags;
    std::vector<VkDescriptorPoolSize> sizes;

    // Populate the layout binding, binding flags, and pool size for each kind.
    for (const BindlessResourceKind kind : m_kinds)
    {
        // Retrieve the Vulkan descriptor type for this kind.
        const VkDescriptorType type = to_vulkan(bindless_descriptor_type(kind));

        // Populate the layout binding, binding flags, and pool size for this kind.
        bindings.push_back({
            .binding = static_cast<std::uint32_t>(std::to_underlying(kind)),
            .descriptorType = type,
            .descriptorCount = description.capacity(kind),
            .stageFlags = VK_SHADER_STAGE_ALL,
        });

        // Set the binding flags to allow partially bound, update-after-bind, and update-unused-while-pending.
        flags.push_back(VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT 
                            | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT 
                                | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT);

        // Add the pool size for this kind to the sizes vector.
        sizes.push_back({.type = type, .descriptorCount = description.capacity(kind)});
    }

    // Create the descriptor set layout with the specified bindings and flags.
    const VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<std::uint32_t>(flags.size()),
        .pBindingFlags = flags.data(),
    };

    // Create the descriptor set layout with the specified bindings and flags.
    const VkDescriptorSetLayoutCreateInfo layout{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &binding_flags,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = static_cast<std::uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };

    // Check the result of creating the descriptor set layout and throw an error if it fails.
    device.check_result(functions.vkCreateDescriptorSetLayout(device.native(), &layout, nullptr, &m_set_layout),
                        "PhysicalDevice::create_device", "vkCreateDescriptorSetLayout");

    // Create the descriptor pool with the specified sizes and flags.
    const VkDescriptorPoolCreateInfo pool{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = static_cast<std::uint32_t>(sizes.size()),
        .pPoolSizes = sizes.data(),
    };

    // Create the descriptor pool and check the result, throwing an error if it fails.
    const VkResult pool_result = functions.vkCreateDescriptorPool(device.native(), &pool, nullptr, &m_pool);

    // If creating the descriptor pool fails, destroy the descriptor set layout and throw an error.
    if (pool_result != VK_SUCCESS)
    {
        functions.vkDestroyDescriptorSetLayout(device.native(), m_set_layout, nullptr);
        device.check_result(pool_result, "PhysicalDevice::create_device", "vkCreateDescriptorPool");
    }

    // Allocate the descriptor set from the pool and check the result, throwing an error if it fails.
    const VkDescriptorSetAllocateInfo allocation{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &m_set_layout,
    };

    // Allocate the descriptor set and check the result, throwing an error if it fails.
    const VkResult set_result = functions.vkAllocateDescriptorSets(device.native(), &allocation, &m_set);

    // If allocating the descriptor set fails, destroy the descriptor pool and layout, and throw an error.
    if (set_result != VK_SUCCESS)
    {
        functions.vkDestroyDescriptorPool(device.native(), m_pool, nullptr);
        functions.vkDestroyDescriptorSetLayout(device.native(), m_set_layout, nullptr);
        device.check_result(set_result, "PhysicalDevice::create_device", "vkAllocateDescriptorSets");
    }

    // Name the descriptor set layout, pool, and set for debugging purposes.
    device.name_object(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, m_set_layout, "bindless table layout");
    device.name_object(VK_OBJECT_TYPE_DESCRIPTOR_POOL, m_pool, "bindless table pool");
    device.name_object(VK_OBJECT_TYPE_DESCRIPTOR_SET, m_set, "bindless table");
}

BindlessTable::~BindlessTable()
{
    // Destroy the descriptor pool and set layout when the BindlessTable is destroyed.
    const VolkDeviceTable& functions = m_device.functions();
    functions.vkDestroyDescriptorPool(m_device.native(), m_pool, nullptr);
    functions.vkDestroyDescriptorSetLayout(m_device.native(), m_set_layout, nullptr);
}

auto
BindlessTable::kinds() const noexcept -> std::span<const BindlessResourceKind>
{
    return m_kinds;
}

auto
BindlessTable::allocate(BindlessResourceKind kind) -> std::uint32_t
{
    // Released indices first, then fresh ones.
    Array& array = m_arrays[index_of(kind)];
    std::uint32_t index = 0;

    // Check which indices are available for allocation: either from the free list or from the next available index.
    if (!array.free.empty()) // If there are any free indices, pop one from the back of the free list. 
    {
        index = array.free.back();
        array.free.pop_back();
    }
    else if (array.next < m_description.capacity(kind)) // Otherwise, if there is still capacity, use the next index. 
    {
        index = array.next++;
    }
    else // If neither is possible, throw an OutOfMemoryError.
    {
        throw OutOfMemoryError({
            .backend = BackendKind::VULKAN,
            .operation = std::format("register a bindless {}", kind),
            .domain = MemoryDomain::DEVICE,
            .requested_bytes = 0,
            .details = std::format("the bindless {} array is full ({} descriptors)", kind, m_description.capacity(kind)),
        });
    }

    // Increment the used count for this kind.
    ++array.used;

    // Return the allocated index for the specified kind of bindless resource.
    return index;
}

void
BindlessTable::release(BindlessResourceKind kind,
                       std::uint32_t index)
{
    const std::scoped_lock lock{m_mutex};
    Array& array = m_arrays[index_of(kind)];
    array.free.push_back(index);
    --array.used;
}

void
BindlessTable::release_later(BindlessResourceKind kind,
                             std::uint32_t index)
{
    // The index may still be in shaders' hands until the GPU finishes what was submitted.
    m_device.vulkan_deleter().enqueue([this, kind, index]
    {
        release(kind, index);
    });
}

void
BindlessTable::write(BindlessResourceKind kind,
                     std::uint32_t index,
                     const VkDescriptorImageInfo* image,
                     const VkDescriptorBufferInfo* buffer,
                     const void* next)
{
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = next,
        .dstSet = m_set,
        .dstBinding = static_cast<std::uint32_t>(std::to_underlying(kind)),
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = to_vulkan(bindless_descriptor_type(kind)),
        .pImageInfo = image,
        .pBufferInfo = buffer,
    };
    m_device.functions().vkUpdateDescriptorSets(m_device.native(), 1, &write, 0, nullptr);
}

auto
BindlessTable::add_sampled_image(VkImageView view) -> SampledImageIndex
{
    const std::scoped_lock lock{m_mutex};
    const std::uint32_t index = allocate(BindlessResourceKind::SAMPLED_IMAGE);
    const VkDescriptorImageInfo image{.imageView = view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    write(BindlessResourceKind::SAMPLED_IMAGE, index, &image, nullptr, nullptr);
    return SampledImageIndex{index};
}

auto
BindlessTable::add_storage_image(VkImageView view) -> StorageImageIndex
{
    const std::scoped_lock lock{m_mutex};
    const std::uint32_t index = allocate(BindlessResourceKind::STORAGE_IMAGE);
    const VkDescriptorImageInfo image{.imageView = view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    write(BindlessResourceKind::STORAGE_IMAGE, index, &image, nullptr, nullptr);
    return StorageImageIndex{index};
}

auto
BindlessTable::add_sampler(VkSampler sampler) -> SamplerIndex
{
    const std::scoped_lock lock{m_mutex};
    const std::uint32_t index = allocate(BindlessResourceKind::SAMPLER);
    const VkDescriptorImageInfo image{.sampler = sampler};
    write(BindlessResourceKind::SAMPLER, index, &image, nullptr, nullptr);
    return SamplerIndex{index};
}

auto
BindlessTable::add_storage_buffer(VkBuffer buffer) -> StorageBufferIndex
{
    const std::scoped_lock lock{m_mutex};
    const std::uint32_t index = allocate(BindlessResourceKind::STORAGE_BUFFER);
    const VkDescriptorBufferInfo information{.buffer = buffer, .offset = 0, .range = VK_WHOLE_SIZE};
    write(BindlessResourceKind::STORAGE_BUFFER, index, nullptr, &information, nullptr);
    return StorageBufferIndex{index};
}

auto
BindlessTable::add_acceleration_structure(VkAccelerationStructureKHR structure) -> AccelerationStructureIndex
{
    const std::scoped_lock lock{m_mutex};
    const std::uint32_t index = allocate(BindlessResourceKind::ACCELERATION_STRUCTURE);
    const VkWriteDescriptorSetAccelerationStructureKHR information{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &structure,
    };
    write(BindlessResourceKind::ACCELERATION_STRUCTURE, index, nullptr, nullptr, &information);
    return AccelerationStructureIndex{index};
}

auto
BindlessTable::usage() const -> BindlessTableUsage
{
    const std::scoped_lock lock{m_mutex};
    BindlessTableUsage usage;
    for (const BindlessResourceKind kind : m_kinds)
    {
        usage.used[index_of(kind)] = m_arrays[index_of(kind)].used;
        usage.capacity[index_of(kind)] = m_description.capacity(kind);
    }
    return usage;
}

} // namespace gfx::vulkan
