/// @file pipeline_layout.cppm
/// @brief PipelineLayout: the descriptor set layouts and push constants a pipeline uses.
export module gfx.interface:pipeline_layout;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :layout_compatibility;

export namespace gfx
{

/// @brief Push constants visible to some stages.
struct PushConstantRange
{
    ShaderStage stages = ShaderStage::NONE; ///< Stages that may read them.
    std::uint32_t offset = 0;               ///< First byte.
    std::uint32_t size = 0;                 ///< Bytes.
};

/// @brief How to create a PipelineLayout.
struct PipelineLayoutDescription
{
    std::span<const DescriptorSetLayoutHandle> set_layouts{};    ///< Layout of set 0, 1, ...
    std::span<const PushConstantRange> push_constant_ranges{};   ///< Push-constant ranges.
    std::string_view debug_name{};                               ///< Name shown by debugging tools.
};

/// @brief The descriptor set layouts and push constants pipelines use. Device::create_pipeline_layout<
///        DEFINITION>() creates one (and its set layouts) from a compile-time PipelineLayoutDefinition.
class PipelineLayout : public BackendObject
{
public:
    /// @brief Handle naming this layout in pipelines and when binding descriptor sets.
    [[nodiscard]] virtual auto handle() const noexcept -> PipelineLayoutHandle = 0;

    /// @brief Every binding of every set and the push constants, as one definition (what pipeline
    ///        creation compares shader layouts with).
    [[nodiscard]] virtual auto definition() const noexcept -> const PipelineLayoutDefinition& = 0;
};

} // namespace gfx
