/// @file vulkan_pipelines.cppm
/// @brief Internal partition: the binding model and the pipelines - descriptor set layouts, pipeline
///        layouts, descriptor pools and sets, shader modules, pipeline caches and pipelines.
module;

#include <volk.h>

module gfx.backends.vulkan:pipelines;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import gfx.interface;
import :common;
import :device;

namespace gfx::vulkan
{

/// @brief A descriptor set layout (VkDescriptorSetLayout) and its bindings.
class VulkanDescriptorSetLayout final : public DescriptorSetLayout
{
public:
    VulkanDescriptorSetLayout(VulkanDevice& device, const DescriptorSetLayoutDescription& description);
    ~VulkanDescriptorSetLayout() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override;
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override;
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto handle() const noexcept -> DescriptorSetLayoutHandle override;
    [[nodiscard]] auto bindings() const noexcept -> std::span<const DescriptorSetLayoutBinding> override;

    /// @brief The native layout.
    [[nodiscard]] auto native() const noexcept -> VkDescriptorSetLayout
    {
        return m_layout;
    }

    /// @brief Whether sets of this layout need an update-after-bind pool.
    [[nodiscard]] auto update_after_bind() const noexcept -> bool
    {
        return m_update_after_bind;
    }

private:
    VulkanDevice& m_device;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    std::vector<DescriptorSetLayoutBinding> m_bindings;
    bool m_update_after_bind = false;
    DescriptorSetLayoutHandle m_handle;
    std::string m_debug_name;
};

/// @brief A pipeline layout (VkPipelineLayout) and the definition pipelines are checked against.
class VulkanPipelineLayout final : public PipelineLayout
{
public:
    /// @brief A layout made of descriptor set layouts created by the application.
    VulkanPipelineLayout(VulkanDevice& device, const PipelineLayoutDescription& description);

    /// @brief The bindless pipeline layout: the bindless set layout in set BINDLESS_SET.
    VulkanPipelineLayout(VulkanDevice& device, VkDescriptorSetLayout bindless_set_layout, PipelineLayoutDefinition definition);

    ~VulkanPipelineLayout() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override;
    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override;
    void set_debug_name(std::string_view name) override;
    [[nodiscard]] auto handle() const noexcept -> PipelineLayoutHandle override;
    [[nodiscard]] auto definition() const noexcept -> const PipelineLayoutDefinition& override;

    /// @brief The native layout.
    [[nodiscard]] auto native() const noexcept -> VkPipelineLayout
    {
        return m_layout;
    }

private:
    void create(std::span<const VkDescriptorSetLayout> set_layouts, std::string_view debug_name);

private:
    VulkanDevice& m_device;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    PipelineLayoutDefinition m_definition;
    PipelineLayoutHandle m_handle;
    std::string m_debug_name;
};

} // namespace gfx::vulkan
