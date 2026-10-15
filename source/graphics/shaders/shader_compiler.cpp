/// @file shader_compiler.cpp
/// @brief compile_shader(): GLSL and HLSL to SPIR-V with the glslang C API, in Debug builds only.
///        Release builds do not link glslang: compile_shader() reports that it is unavailable.
module;

#if GFX_DEBUG_BUILD
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#endif

module gfx.shader_tools;

import std;
import gfx.enums;
import gfx.errors;
import gfx.interface;

namespace gfx
{

namespace
{

[[noreturn]] void
reject(const ShaderModuleDescription& description,
       std::string problem,
       std::string suggestion)
{
    throw InvalidUsageError({
        .backend = std::nullopt,
        .operation = "compile_shader",
        .problem = std::format("shader '{}': {}", description.name, std::move(problem)),
        .suggestion = std::move(suggestion),
    });
}

#if GFX_DEBUG_BUILD

/// @brief glslang's stage for a gfx stage.
auto
glslang_stage_of(ShaderStage stage) noexcept -> std::optional<glslang_stage_t>
{
    switch (stage)
    {
        case ShaderStage::VERTEX:                   return GLSLANG_STAGE_VERTEX;
        case ShaderStage::TESSELLATION_CONTROL:     return GLSLANG_STAGE_TESSCONTROL;
        case ShaderStage::TESSELLATION_EVALUATION:  return GLSLANG_STAGE_TESSEVALUATION;
        case ShaderStage::GEOMETRY:                 return GLSLANG_STAGE_GEOMETRY;
        case ShaderStage::FRAGMENT:                 return GLSLANG_STAGE_FRAGMENT;
        case ShaderStage::COMPUTE:                  return GLSLANG_STAGE_COMPUTE;
        case ShaderStage::TASK:                     return GLSLANG_STAGE_TASK;
        case ShaderStage::MESH:                     return GLSLANG_STAGE_MESH;
        case ShaderStage::RAY_GENERATION:           return GLSLANG_STAGE_RAYGEN;
        case ShaderStage::ANY_HIT:                  return GLSLANG_STAGE_ANYHIT;
        case ShaderStage::CLOSEST_HIT:              return GLSLANG_STAGE_CLOSESTHIT;
        case ShaderStage::MISS:                     return GLSLANG_STAGE_MISS;
        case ShaderStage::INTERSECTION:             return GLSLANG_STAGE_INTERSECT;
        case ShaderStage::CALLABLE:                 return GLSLANG_STAGE_CALLABLE;
    default: return std::nullopt;
    }
}

/// @brief An included file handed to glslang; the C struct comes first so glslang can free it through
///        the callbacks.
struct IncludedFile
{
    glsl_include_result_t result{};
    std::string path;
    std::string contents;
};

/// @brief Resolves #include directives and remembers every file included.
class Includer
{
public:
    explicit Includer(const ShaderModuleDescription& description)
    {
        // The main source's directory, then the include directories.
        if (!description.source_path.empty())
        {
            m_main_directory = std::filesystem::path{description.source_path}.parent_path();
        }
        for (const std::string_view directory : description.include_directories)
        {
            m_include_directories.emplace_back(directory);
        }
    }

    [[nodiscard]] auto callbacks() noexcept -> glsl_include_callbacks_t
    {
        return {.include_system = &Includer::include_system,
                .include_local = &Includer::include_local,
                .free_include_result = &Includer::free_include_result};
    }

    [[nodiscard]] auto included_files() && -> std::vector<std::string>
    {
        return std::move(m_included_files);
    }

private:
    // #include "file": next to the including file, then in the include directories.
    static auto include_local(void* context, const char* header_name, const char* includer_name, std::size_t depth)
        -> glsl_include_result_t*
    {
        Includer& self = *static_cast<Includer*>(context);
        const std::filesystem::path directory =
            depth <= 1 || includer_name == nullptr || *includer_name == '\0' ? self.m_main_directory
                                                                            : std::filesystem::path{includer_name}.parent_path();
        if (glsl_include_result_t* result = self.open(directory / header_name))
        {
            return result;
        }
        return include_system(context, header_name, includer_name, depth);
    }

    // #include <file>: in the include directories.
    static auto include_system(void* context, const char* header_name, const char* /*includer_name*/, std::size_t /*depth*/)
        -> glsl_include_result_t*
    {
        Includer& self = *static_cast<Includer*>(context);
        for (const std::filesystem::path& directory : self.m_include_directories)
        {
            if (glsl_include_result_t* result = self.open(directory / header_name))
            {
                return result;
            }
        }
        return nullptr;
    }

    static auto free_include_result(void* /*context*/, glsl_include_result_t* result) -> int
    {
        delete reinterpret_cast<IncludedFile*>(result);
        return 0;
    }

    // The file at @p path, or nullptr when it cannot be read.
    auto open(const std::filesystem::path& path) -> glsl_include_result_t*
    {
        std::error_code error;
        if (path.empty() || !std::filesystem::is_regular_file(path, error))
        {
            return nullptr;
        }
        std::ifstream stream{path, std::ios::binary};
        if (!stream)
        {
            return nullptr;
        }
        auto file = std::make_unique<IncludedFile>();
        file->path = std::filesystem::weakly_canonical(path, error).generic_string();
        file->contents.assign(std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{});
        file->result = {.header_name = file->path.c_str(),
                        .header_data = file->contents.c_str(),
                        .header_length = file->contents.size()};
        if (std::ranges::find(m_included_files, file->path) == m_included_files.end())
        {
            m_included_files.push_back(file->path);
        }
        return &file.release()->result;
    }

private:
    std::filesystem::path m_main_directory;
    std::vector<std::filesystem::path> m_include_directories;
    std::vector<std::string> m_included_files;
};


/// @brief One run of glslang over the source of a description: preprocessing, then (optionally)
///        parsing, linking and SPIR-V generation.
class Compilation
{
public:
    /// @throws InvalidUsageError for a description glslang cannot take.
    Compilation(const ShaderModuleDescription& description, std::string_view operation)
        : m_description{description}
        , m_operation{operation}
        , m_includer{description}
        , m_source{description.source}
        , m_entry_point{description.entry_point}
        , m_source_path{description.source_path.empty() ? description.name : description.source_path}
        , m_shader_name{description.name.empty() ? description.source_path : description.name}
    {
        // One stage, and GLSL's fixed entry point.
        const std::optional<glslang_stage_t> stage = glslang_stage_of(description.stage);
        if (!stage)
        {
            reject(description, std::format("the stage must be exactly one shader stage, not {}", description.stage),
                   "set ShaderModuleDescription::stage, e.g. ShaderStage::COMPUTE");
        }
        m_stage = *stage;
        m_hlsl = description.source_format == ShaderSourceFormat::HLSL;
        if (!m_hlsl && description.entry_point != "main")
        {
            reject(description, std::format("GLSL entry points are always 'main', not '{}'", description.entry_point),
                   "set entry_point to \"main\" (HLSL sources may name another function)");
        }

        // glslang is initialized once per process.
        static const bool initialized = glslang_initialize_process() != 0;
        if (!initialized)
        {
            reject(description, "glslang could not be initialized", "");
        }

        // Defines become a preamble; includes are resolved by the includer.
        for (const ShaderDefine& define : description.defines)
        {
            m_preamble += std::format("#define {} {}\n", define.name, define.value);
        }
        m_input = {
            .language = m_hlsl ? GLSLANG_SOURCE_HLSL : GLSLANG_SOURCE_GLSL,
            .stage = m_stage,
            .client = GLSLANG_CLIENT_VULKAN,
            .client_version = GLSLANG_TARGET_VULKAN_1_3,
            .target_language = GLSLANG_TARGET_SPV,
            .target_language_version = GLSLANG_TARGET_SPV_1_6,
            .code = m_source.c_str(),
            .default_version = m_hlsl ? 100 : 460,
            .default_profile = GLSLANG_NO_PROFILE,
            .force_default_version_and_profile = 0,
            .forward_compatible = 0,
            .messages = static_cast<glslang_messages_t>(GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT |
                                                        GLSLANG_MSG_DEBUG_INFO_BIT |
                                                        (m_hlsl ? GLSLANG_MSG_READ_HLSL_BIT : GLSLANG_MSG_DEFAULT_BIT)),
            .resource = glslang_default_resource(),
            .callbacks = m_includer.callbacks(),
            .callbacks_ctx = &m_includer,
        };
        m_shader = glslang_shader_create(&m_input);
        if (!m_preamble.empty())
        {
            glslang_shader_set_preamble(m_shader, m_preamble.c_str());
        }
        glslang_shader_set_entry_point(m_shader, m_entry_point.c_str());
    }

    ~Compilation()
    {
        if (m_program != nullptr)
        {
            glslang_program_delete(m_program);
        }
        glslang_shader_delete(m_shader);
    }

    Compilation(const Compilation&) = delete;
    auto operator=(const Compilation&) -> Compilation& = delete;

    /// @brief Runs the preprocessor (resolving every include).
    void preprocess()
    {
        if (glslang_shader_preprocess(m_shader, &m_input) == 0)
        {
            fail(glslang_shader_get_info_log(m_shader), glslang_shader_get_info_debug_log(m_shader));
        }
    }

    /// @brief Preprocesses, parses, links and generates SPIR-V with debug information (line information
    ///        and the source text, for source-level debugging in RenderDoc). NonSemantic debug information
    ///        is not emitted: glslang 16.2 writes it invalid for the built-in gl_PerVertex block of vertex
    ///        shaders. HLSL is legalized by the SPIR-V optimizer; GLSL is left unoptimized.
    [[nodiscard]] auto compile() -> std::vector<std::uint32_t>
    {
        preprocess();
        if (glslang_shader_parse(m_shader, &m_input) == 0)
        {
            fail(glslang_shader_get_info_log(m_shader), glslang_shader_get_info_debug_log(m_shader));
        }
        m_program = glslang_program_create();
        glslang_program_add_shader(m_program, m_shader);
        if (glslang_program_link(m_program, static_cast<int>(m_input.messages)) == 0)
        {
            fail(glslang_program_get_info_log(m_program), glslang_program_get_info_debug_log(m_program));
        }
        glslang_program_set_source_file(m_program, m_stage, m_source_path.c_str());
        glslang_program_add_source_text(m_program, m_stage, m_source.c_str(), m_source.size());
        glslang_spv_options_t options{
            .generate_debug_info = true,
            .strip_debug_info = false,
            .disable_optimizer = !m_hlsl,
            .optimize_size = false,
            .disassemble = false,
            .validate = false,
            .emit_nonsemantic_shader_debug_info = false,
            .emit_nonsemantic_shader_debug_source = false,
            .compile_only = false,
            .optimize_allow_expanded_id_bound = false,
        };
        glslang_program_SPIRV_generate_with_options(m_program, m_stage, &options);
        std::vector<std::uint32_t> spirv(glslang_program_SPIRV_get_size(m_program));
        glslang_program_SPIRV_get(m_program, spirv.data());
        if (spirv.empty())
        {
            fail(glslang_program_SPIRV_get_messages(m_program), nullptr);
        }
        return spirv;
    }

    /// @brief The files included so far.
    [[nodiscard]] auto included_files() -> std::vector<std::string>
    {
        return std::move(m_includer).included_files();
    }

private:
    [[noreturn]] void fail(const char* log, const char* debug_log) const
    {
        throw ShaderCompilationError({
            .operation = std::string{m_operation},
            .shader_name = m_shader_name,
            .stage = m_description.stage,
            .compiler_log = std::format("{}{}", log != nullptr ? log : "", debug_log != nullptr ? debug_log : ""),
        });
    }

private:
    const ShaderModuleDescription& m_description;
    std::string_view m_operation;
    Includer m_includer;
    std::string m_source;
    std::string m_entry_point;
    std::string m_source_path;
    std::string m_shader_name;
    std::string m_preamble;
    glslang_stage_t m_stage = GLSLANG_STAGE_VERTEX;
    bool m_hlsl = false;
    glslang_input_t m_input{};
    glslang_shader_t* m_shader = nullptr;
    glslang_program_t* m_program = nullptr;
};

#endif

/// @brief Rejects descriptions without source, and every description in Release builds.
void
require_source(const ShaderModuleDescription& description)
{
    if (description.source_format == ShaderSourceFormat::SPIRV)
    {
        reject(description, "the description holds SPIR-V, which needs no compilation",
               "set source_format to GLSL or HLSL and pass the source text");
    }
    if constexpr (!SHADER_HOT_RELOAD)
    {
        reject(description,
               std::format("{} source is compiled at run time only in Debug builds; this Release build only loads compiled SPIR-V",
                           description.source_format),
               "compile the shader at build time with gfx_add_shaders() and pass its SPIR-V");
    }
}

} // namespace

auto
compile_shader(const ShaderModuleDescription& description) -> CompiledShader
{
    require_source(description);
#if GFX_DEBUG_BUILD
    Compilation compilation{description, "compile_shader"};
    CompiledShader compiled;
    compiled.spirv = compilation.compile();
    compiled.included_files = compilation.included_files();
    return compiled;
#else
    return {};
#endif
}

auto
shader_dependencies(const ShaderModuleDescription& description) -> std::vector<std::string>
{
    require_source(description);
#if GFX_DEBUG_BUILD
    Compilation compilation{description, "shader_dependencies"};
    compilation.preprocess();
    return compilation.included_files();
#else
    return {};
#endif
}

} // namespace gfx
