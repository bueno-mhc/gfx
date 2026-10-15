/// @file descriptor_set_layout.cppm
/// @brief DescriptorSetLayout: the bindings of one descriptor set.
export module gfx.interface:descriptor_set_layout;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;

export namespace gfx
{

/// @brief One binding of a descriptor set layout.
struct DescriptorSetLayoutBinding
{
    std::uint32_t binding = 0;                                       ///< Binding index.
    DescriptorType type = DescriptorType::UNIFORM_BUFFER;            ///< Kind of resource.
    std::uint32_t count = 1;                                         ///< Number of descriptors.
    ShaderStage stages = ShaderStage::NONE;                          ///< Stages that may access it.
    DescriptorBindingOption options = DescriptorBindingOption::NONE; ///< Descriptor indexing options.
    std::string name{};                                              ///< Name shaders use for it (may be empty);
                                                                     ///< PipelineLayout::find_binding() searches it.

    bool operator==(const DescriptorSetLayoutBinding&) const = default;
};

/// @brief How to create a DescriptorSetLayout.
struct DescriptorSetLayoutDescription
{
    std::span<const DescriptorSetLayoutBinding> bindings{}; ///< Bindings of the set.
    std::string_view debug_name{};                          ///< Name shown by debugging tools.
};

/// @brief The bindings of one descriptor set; pipeline layouts and descriptor sets are built from it.
class DescriptorSetLayout : public BackendObject
{
public:
    /// @brief Handle naming this layout in pipeline layouts and descriptor set allocations.
    [[nodiscard]] virtual auto handle() const noexcept -> DescriptorSetLayoutHandle = 0;

    /// @brief Bindings of the set.
    [[nodiscard]] virtual auto bindings() const noexcept -> std::span<const DescriptorSetLayoutBinding> = 0;
};

} // namespace gfx
