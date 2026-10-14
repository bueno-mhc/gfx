/// @file typed_objects.cppm
/// @brief Objects that carry their compile-time definition in their type: a shader module typed by its
///        ShaderDefinition, and a pipeline layout typed by its PipelineLayoutDefinition. Device's typed
///        creation functions return them, and pipeline creation checks them against each other at
///        compile time.
export module gfx.interface:typed_objects;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import :shader_layout;
import :layout_compatibility;
import :shader_module;
import :descriptor_set_layout;
import :pipeline_layout;

export namespace gfx
{

/// @brief A ShaderModule whose layout is known at compile time (Shader::LAYOUT). Owns the module.
template<ShaderDefinition Shader>
class TypedShaderModule
{
public:
    /// @brief The shader definition.
    using ShaderType = Shader;

    /// @brief Takes ownership of a module created from Shader::SOURCE.
    explicit TypedShaderModule(std::unique_ptr<ShaderModule> shader_module)
        : m_shader_module{std::move(shader_module)}
    {}

    /// @brief The module.
    [[nodiscard]] auto shader_module() const noexcept -> ShaderModule&
    {
        return *m_shader_module;
    }

    /// @brief Handle naming the module in pipeline descriptions.
    [[nodiscard]] auto handle() const noexcept -> ShaderModuleHandle
    {
        return m_shader_module->handle();
    }

private:
    std::unique_ptr<ShaderModule> m_shader_module;
};

/// @brief A PipelineLayout created from a compile-time PipelineLayoutDefinition, together with its
///        descriptor set layouts (one per set index). Owns them all.
template<const PipelineLayoutDefinition& Definition>
class TypedPipelineLayout
{
public:
    /// @brief Takes ownership of the set layouts (index = set) and of the pipeline layout.
    TypedPipelineLayout(std::vector<std::unique_ptr<DescriptorSetLayout>> set_layouts,
                        std::unique_ptr<PipelineLayout> pipeline_layout)
        : m_set_layouts{std::move(set_layouts)}
        , m_pipeline_layout{std::move(pipeline_layout)}
    {}

    /// @brief The definition the layout was created from.
    [[nodiscard]] static constexpr auto definition() noexcept -> const PipelineLayoutDefinition&
    {
        return Definition;
    }

    /// @brief The pipeline layout.
    [[nodiscard]] auto pipeline_layout() const noexcept -> PipelineLayout&
    {
        return *m_pipeline_layout;
    }

    /// @brief Handle naming the pipeline layout.
    [[nodiscard]] auto handle() const noexcept -> PipelineLayoutHandle
    {
        return m_pipeline_layout->handle();
    }

    /// @brief Layout of descriptor set @p set (to allocate descriptor sets).
    /// @throws InvalidUsageError when the definition has no such set.
    [[nodiscard]] auto set_layout(std::uint32_t set) const -> DescriptorSetLayout&
    {
        // Reject set indices beyond the definition, with the valid range in the message.
        if (set >= m_set_layouts.size())
        {
            throw InvalidUsageError({
                .backend = m_pipeline_layout->backend(),
                .operation = "TypedPipelineLayout::set_layout",
                .problem = std::format("set {} does not exist; the pipeline layout has {} set(s)", set, m_set_layouts.size()),
                .suggestion = "",
            });
        }
        return *m_set_layouts[set];
    }

private:
    std::vector<std::unique_ptr<DescriptorSetLayout>> m_set_layouts;
    std::unique_ptr<PipelineLayout> m_pipeline_layout;
};

} // namespace gfx
