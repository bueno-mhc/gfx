/// @file vulkan_layouts.cpp
/// @brief VulkanDescriptorSetLayout and VulkanPipelineLayout.
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

// ---------------------------------------------------------------------------------------------------
// VulkanDescriptorSetLayout
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::create_descriptor_set_layout(const DescriptorSetLayoutDescription& description) -> std::unique_ptr<DescriptorSetLayout>
{
    return std::make_unique<VulkanDescriptorSetLayout>(*this, description);
}

VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VulkanDevice& device,
                                                     const DescriptorSetLayoutDescription& description)
    : DescriptorSetLayout{}
    , m_device{device}
    , m_bindings{description.bindings.begin(), description.bindings.end()}
    , m_debug_name{description.debug_name}
{
    set_validation_level(device.validation_level());

    // Each binding once; a variable-sized binding must be the last one.
    if (validates(ValidationLevel::CREATION))
    {
        for (std::size_t index = 0; index < m_bindings.size(); ++index)
        {
            const DescriptorSetLayoutBinding& binding = m_bindings[index];
            const bool repeated = std::ranges::any_of(m_bindings.begin(), m_bindings.begin() + static_cast<std::ptrdiff_t>(index),
                                                      [&binding](const DescriptorSetLayoutBinding& other)
            {
                return other.binding == binding.binding;
            });
            const bool variable_not_last =
                has_any(binding.options, DescriptorBindingOption::VARIABLE_DESCRIPTOR_COUNT) &&
                std::ranges::any_of(m_bindings, [&binding](const DescriptorSetLayoutBinding& other)
            {
                return other.binding > binding.binding;
            });
            if (repeated || variable_not_last || (binding.type == DescriptorType::ACCELERATION_STRUCTURE &&
                                                  !device.enabled(DeviceFeature::RAY_TRACING) && !device.enabled(DeviceFeature::RAY_QUERY)))
            {
                throw InvalidUsageError({
                    .backend = BackendKind::VULKAN,
                    .operation = "Device::create_descriptor_set_layout",
                    .problem = std::format("descriptor set layout '{}': binding {} {}", m_debug_name, binding.binding,
                                           repeated ? "is declared twice"
                                           : variable_not_last ? "has VARIABLE_DESCRIPTOR_COUNT but is not the last binding"
                                                               : "holds acceleration structures, which need RAY_TRACING or RAY_QUERY"),
                    .suggestion = "",
                });
            }
        }
    }

    // Native bindings and their descriptor indexing flags.
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<VkDescriptorBindingFlags> flags;
    bindings.reserve(m_bindings.size());
    flags.reserve(m_bindings.size());
    for (const DescriptorSetLayoutBinding& binding : m_bindings)
    {
        bindings.push_back({
            .binding = binding.binding,
            .descriptorType = to_vulkan(binding.type),
            .descriptorCount = binding.count,
            .stageFlags = to_vulkan(binding.stages),
        });
        flags.push_back(to_vulkan(binding.options));
        m_update_after_bind = m_update_after_bind || has_any(binding.options, DescriptorBindingOption::UPDATE_AFTER_BIND);
    }
    const VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<std::uint32_t>(flags.size()),
        .pBindingFlags = flags.data(),
    };
    const VkDescriptorSetLayoutCreateInfo create_information{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &binding_flags,
        .flags = m_update_after_bind ? VkDescriptorSetLayoutCreateFlags{VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT} : 0U,
        .bindingCount = static_cast<std::uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
    device.check_result(device.functions().vkCreateDescriptorSetLayout(device.native(), &create_information, nullptr, &m_layout),
                        "Device::create_descriptor_set_layout", "vkCreateDescriptorSetLayout");
    m_handle = device.handles().descriptor_set_layouts.insert({.layout = m_layout, .object = this});
    device.name_object(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, m_layout, m_debug_name);
}

VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
{
    // Layouts are not used by the GPU once pipelines and sets exist: destroyed at once.
    (void)m_device.handles().descriptor_set_layouts.remove(m_handle);
    m_device.vulkan_deleter().enqueue([device = &m_device, layout = m_layout]
    {
        device->functions().vkDestroyDescriptorSetLayout(device->native(), layout, nullptr);
    });
}

auto
VulkanDescriptorSetLayout::backend() const noexcept -> BackendKind
{
    return BackendKind::VULKAN;
}

auto
VulkanDescriptorSetLayout::debug_name() const noexcept -> std::string_view
{
    return m_debug_name;
}

void
VulkanDescriptorSetLayout::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    m_device.name_object(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, m_layout, m_debug_name);
}

auto
VulkanDescriptorSetLayout::handle() const noexcept -> DescriptorSetLayoutHandle
{
    return m_handle;
}

auto
VulkanDescriptorSetLayout::bindings() const noexcept -> std::span<const DescriptorSetLayoutBinding>
{
    return m_bindings;
}

// ---------------------------------------------------------------------------------------------------
// VulkanPipelineLayout
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::create_pipeline_layout(const PipelineLayoutDescription& description) -> std::unique_ptr<PipelineLayout>
{
    return std::make_unique<VulkanPipelineLayout>(*this, description);
}

VulkanPipelineLayout::VulkanPipelineLayout(VulkanDevice& device,
                                           const PipelineLayoutDescription& description)
    : PipelineLayout{}
    , m_device{device}
    , m_debug_name{description.debug_name}
{
    set_validation_level(device.validation_level());
    static constexpr std::string_view OPERATION = "Device::create_pipeline_layout";

    // The set layouts, resolved once.
    std::vector<VkDescriptorSetLayout> set_layouts;
    std::vector<const DescriptorSetLayout*> objects;
    for (const DescriptorSetLayoutHandle handle : description.set_layouts)
    {
        const DescriptorSetLayoutRecord& record = device.handles().descriptor_set_layouts.get(handle, OPERATION);
        set_layouts.push_back(record.layout);
        objects.push_back(record.object);
    }

    // Push-constant ranges within the device's limit, and no stage in two ranges.
    if (validates(ValidationLevel::CREATION))
    {
        const std::uint32_t limit = device.physical_device().properties().limits.maximum_push_constant_size;
        ShaderStage seen = ShaderStage::NONE;
        for (const PushConstantRange& range : description.push_constant_ranges)
        {
            if (range.offset + range.size > limit || range.size == 0 || range.offset % 4 != 0 || range.size % 4 != 0 ||
                has_any(seen, range.stages))
            {
                throw InvalidUsageError({
                    .backend = BackendKind::VULKAN,
                    .operation = std::string{OPERATION},
                    .problem = std::format("pipeline layout '{}': push-constant range [{}, {}) for {} is invalid: ranges are "
                                           "non-empty multiples of 4 bytes within the device's {} bytes, and no stage is in "
                                           "two ranges",
                                           m_debug_name, range.offset, range.offset + range.size, range.stages, limit),
                    .suggestion = "",
                });
            }
            seen |= range.stages;
        }
    }
    m_definition = make_pipeline_layout_definition(objects, description.push_constant_ranges);
    create(set_layouts, description.debug_name);
}

VulkanPipelineLayout::VulkanPipelineLayout(VulkanDevice& device,
                                           VkDescriptorSetLayout bindless_set_layout,
                                           PipelineLayoutDefinition definition)
    : PipelineLayout{}
    , m_device{device}
    , m_definition{std::move(definition)}
{
    set_validation_level(device.validation_level());
    const std::array<VkDescriptorSetLayout, 1> set_layouts{bindless_set_layout};
    create(set_layouts, "bindless pipeline layout");
}

void
VulkanPipelineLayout::create(std::span<const VkDescriptorSetLayout> set_layouts,
                             std::string_view debug_name)
{
    std::vector<VkPushConstantRange> ranges;
    for (const PushConstantRange& range : m_definition.push_constant_ranges)
    {
        ranges.push_back({.stageFlags = to_vulkan(range.stages), .offset = range.offset, .size = range.size});
    }
    const VkPipelineLayoutCreateInfo create_information{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<std::uint32_t>(set_layouts.size()),
        .pSetLayouts = set_layouts.data(),
        .pushConstantRangeCount = static_cast<std::uint32_t>(ranges.size()),
        .pPushConstantRanges = ranges.data(),
    };
    m_device.check_result(m_device.functions().vkCreatePipelineLayout(m_device.native(), &create_information, nullptr, &m_layout),
                          "Device::create_pipeline_layout", "vkCreatePipelineLayout");
    m_debug_name = debug_name;
    m_handle = m_device.handles().pipeline_layouts.insert({.layout = m_layout, .object = this});
    m_device.name_object(VK_OBJECT_TYPE_PIPELINE_LAYOUT, m_layout, m_debug_name);
}

VulkanPipelineLayout::~VulkanPipelineLayout()
{
    (void)m_device.handles().pipeline_layouts.remove(m_handle);
    m_device.vulkan_deleter().enqueue([device = &m_device, layout = m_layout]
    {
        device->functions().vkDestroyPipelineLayout(device->native(), layout, nullptr);
    });
}

auto
VulkanPipelineLayout::backend() const noexcept -> BackendKind
{
    return BackendKind::VULKAN;
}

auto
VulkanPipelineLayout::debug_name() const noexcept -> std::string_view
{
    return m_debug_name;
}

void
VulkanPipelineLayout::set_debug_name(std::string_view name)
{
    m_debug_name = name;
    m_device.name_object(VK_OBJECT_TYPE_PIPELINE_LAYOUT, m_layout, m_debug_name);
}

auto
VulkanPipelineLayout::handle() const noexcept -> PipelineLayoutHandle
{
    return m_handle;
}

auto
VulkanPipelineLayout::definition() const noexcept -> const PipelineLayoutDefinition&
{
    return m_definition;
}

} // namespace gfx::vulkan
