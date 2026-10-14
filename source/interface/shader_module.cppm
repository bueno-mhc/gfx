/// @file shader_module.cppm
/// @brief ShaderModule: a compiled shader together with its reflected layout.
export module gfx.interface:shader_module;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :shader_layout;

export namespace gfx
{

/// @brief How to create a ShaderModule: GLSL source (compiled to SPIR-V by the library) or SPIR-V.
struct ShaderModuleDescription
{
    ShaderStage stage = ShaderStage::NONE;                    ///< Stage the shader runs in.
    ShaderSourceFormat source_format = ShaderSourceFormat::GLSL; ///< GLSL source or SPIR-V binary.
    std::string_view glsl_source{};                           ///< GLSL source (ShaderSourceFormat::GLSL).
    std::span<const std::uint32_t> spirv{};                   ///< SPIR-V words (ShaderSourceFormat::SPIRV).
    std::string_view entry_point = "main";                    ///< Entry point function.
    std::string_view name{};                                  ///< Name for messages (file name or debug name).
    std::optional<ShaderLayout> expected_layout{};            ///< Layout known at compile time; the module's
                                                              ///< reflected layout must equal it.
};

/// @brief A compiled shader. Its layout is reflected from the SPIR-V; when the description carried an
///        expected layout, creation fails with ShaderLayoutMismatchError if the two differ.
class ShaderModule : public BackendObject
{
public:
    /// @brief Handle naming this module in pipeline descriptions.
    [[nodiscard]] virtual auto handle() const noexcept -> ShaderModuleHandle = 0;

    /// @brief Stage the shader runs in.
    [[nodiscard]] virtual auto stage() const noexcept -> ShaderStage = 0;

    /// @brief Layout reflected from the compiled code.
    [[nodiscard]] virtual auto layout() const noexcept -> const ShaderLayout& = 0;
};

} // namespace gfx
