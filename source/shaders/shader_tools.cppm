/// @file shader_tools.cppm
/// @brief Module gfx.shader_tools: SPIR-V reflection (every build) and the run-time GLSL / HLSL compiler
///        (Debug builds). Backends use them to create shader modules; tools and tests may call them too.
///
/// - reflect_spirv() reads a shader's interface (ShaderLayout) from its SPIR-V with SPIRV-Cross.
/// - compile_shader() compiles GLSL or HLSL source to SPIR-V with glslang. It exists in Debug builds only
///   (SHADER_HOT_RELOAD): Release builds load SPIR-V compiled at build time by gfx_add_shaders().
export module gfx.shader_tools;

import std;
import gfx.enums;
import gfx.errors;
import gfx.interface;

export namespace gfx
{

/// @brief Reflects the interface of the entry point @p entry_point of a SPIR-V module: its stage,
///        descriptor bindings (with names, types, array sizes, block sizes and storage image formats),
///        the push-constant bytes it reads, its stage inputs and outputs, its workgroup size and its
///        specialization constants. Only resources the entry point uses are reported.
/// @param spirv       SPIR-V words.
/// @param stage       Expected stage, or ShaderStage::NONE for the stage of the (only) entry point named
///                    @p entry_point.
/// @param entry_point Entry point to reflect.
/// @param shader_name Name used in error messages.
/// @throws InvalidUsageError when the words are not valid SPIR-V or have no such entry point.
[[nodiscard]] auto reflect_spirv(std::span<const std::uint32_t> spirv,
                                 ShaderStage stage = ShaderStage::NONE,
                                 std::string_view entry_point = "main",
                                 std::string_view shader_name = {}) -> ShaderLayout;

/// @brief SPIR-V produced by compile_shader(), and the files the source included.
struct CompiledShader
{
    std::vector<std::uint32_t> spirv{};          ///< SPIR-V words.
    std::vector<std::string> included_files{};   ///< Every file #included, directly or not (hot reload watches them).
};

/// @brief Compiles the GLSL or HLSL source of @p description (source_format, source, stage, entry_point,
///        defines, include_directories, source_path, name) to SPIR-V 1.6 for Vulkan 1.3, with debug
///        information. `#include "file"` is searched next to the including file (source_path for the
///        main source), then in include_directories; `#include <file>` only in include_directories.
///        Debug builds only (SHADER_HOT_RELOAD).
/// @throws ShaderCompilationError with the compiler's log when the source does not compile.
/// @throws InvalidUsageError in Release builds, and when the description holds SPIR-V instead of source.
[[nodiscard]] auto compile_shader(const ShaderModuleDescription& description) -> CompiledShader;

/// @brief The files the GLSL or HLSL source of @p description includes, directly or not, found by
///        running only the preprocessor (much faster than compile_shader()). Debug builds only.
/// @throws ShaderCompilationError when the preprocessor fails (e.g. an include is missing).
/// @throws InvalidUsageError in Release builds, and when the description holds SPIR-V instead of source.
[[nodiscard]] auto shader_dependencies(const ShaderModuleDescription& description) -> std::vector<std::string>;

} // namespace gfx
