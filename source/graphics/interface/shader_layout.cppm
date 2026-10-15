/// @file shader_layout.cppm
/// @brief ShaderLayout: the interface of a compiled shader, reflected from its SPIR-V.
///
/// A ShaderLayout lists what a shader binds (descriptor sets and bindings, push constants), consumes
/// (stage inputs) and produces (stage outputs). The library reflects it from the shader's SPIR-V when
/// the ShaderModule is created (ShaderModule::layout()), the way commercial engines read the interface
/// from compiled bytecode instead of declaring it twice. Pipeline creation then compares it with the
/// pipeline layout, the vertex layout and the color attachments (shader_validation.cppm).
export module gfx.interface:shader_layout;

import std;
import gfx.enums;

export namespace gfx
{

/// @brief A resource a shader binds through a descriptor set.
struct ShaderBinding
{
    std::uint32_t set = 0;                                ///< Descriptor set index.
    std::uint32_t binding = 0;                            ///< Binding index inside the set.
    DescriptorType type = DescriptorType::UNIFORM_BUFFER; ///< Kind of resource.
    std::uint32_t count = 1;                              ///< Array size; 0 = runtime-sized (bindless).
    std::uint32_t block_size = 0;                         ///< Size in bytes of a uniform or storage block (the
                                                          ///< fixed part, for a block ending in a runtime array).
    Format image_format = Format::UNDEFINED;              ///< Format qualifier of a storage image.
    std::string name{};                                   ///< Variable name (block instance name, or the block
                                                          ///< name when the instance is anonymous).

    bool operator==(const ShaderBinding&) const = default;
};

/// @brief An input or output variable of a shader stage.
struct StageVariable
{
    std::uint32_t location = 0;        ///< First location.
    std::uint32_t location_count = 1;  ///< Locations used (matrices and arrays use several).
    Format format = Format::UNDEFINED; ///< Format of one location (e.g. R32G32B32_SFLOAT for vec3).
    std::string name{};                ///< Variable name.

    bool operator==(const StageVariable&) const = default;
};

/// @brief A specialization constant a shader declares (GLSL `layout(constant_id = N) const`, HLSL
///        `[[vk::constant_id(N)]] const`).
struct SpecializationConstantInfo
{
    std::uint32_t id = 0;   ///< Constant identifier.
    std::uint32_t size = 4; ///< Bytes of its value.
    std::string name{};     ///< Variable name.

    bool operator==(const SpecializationConstantInfo&) const = default;
};

/// @brief Interface of one shader: bindings, push constants, stage inputs and outputs, workgroup size,
///        specialization constants.
struct ShaderLayout
{
    ShaderStage stage = ShaderStage::NONE;                ///< Stage the shader runs in.
    std::vector<ShaderBinding> bindings{};                ///< Resources bound through descriptor sets.
    std::uint32_t push_constant_offset = 0;               ///< First push-constant byte the shader reads.
    std::uint32_t push_constant_size = 0;                 ///< Push-constant bytes it reads from there (0 = none).
    std::vector<StageVariable> inputs{};                  ///< Stage inputs (vertex attributes for a vertex shader).
    std::vector<StageVariable> outputs{};                 ///< Stage outputs (color attachments for a fragment shader).
    std::array<std::uint32_t, 3> workgroup_size{1, 1, 1}; ///< Compute, task and mesh workgroup size.
    std::vector<SpecializationConstantInfo> specialization_constants{}; ///< Constants set at pipeline creation.

    /// @brief The binding called @p name, or nullptr.
    [[nodiscard]] auto find(std::string_view name) const noexcept -> const ShaderBinding*
    {
        const auto found = std::ranges::find(bindings, name, &ShaderBinding::name);
        return found == bindings.end() ? nullptr : &*found;
    }

    /// @brief The binding at (@p set, @p binding), or nullptr.
    [[nodiscard]] auto find(std::uint32_t set, std::uint32_t binding) const noexcept -> const ShaderBinding*
    {
        const auto found = std::ranges::find_if(bindings, [set, binding](const ShaderBinding& candidate)
        {
            return candidate.set == set && candidate.binding == binding;
        });
        return found == bindings.end() ? nullptr : &*found;
    }

    bool operator==(const ShaderLayout&) const = default;
};

} // namespace gfx
