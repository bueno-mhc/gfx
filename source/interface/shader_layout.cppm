/// @file shader_layout.cppm
/// @brief Compile-time description of a shader's interface, and its consteval GLSL reflection.
///
/// A ShaderLayout lists what a shader binds (descriptor sets and bindings, push constants), consumes
/// (stage inputs) and produces (stage outputs). It is a literal value: it can be computed at compile
/// time by reflect_glsl() from GLSL source embedded in the program, or built with the with_* methods,
/// and it is then compared with pipeline layouts, vertex layouts and attachments at compile time.
///
/// reflect_glsl() understands the declarations that define a Vulkan GLSL interface:
/// - `layout(set = S, binding = B[, format]) uniform <opaque type> name[N];` (samplers, textures,
///   images, texel buffers, subpass inputs, acceleration structures; `[]` = runtime-sized);
/// - `layout(set = S, binding = B[, std140|std430|scalar]) uniform|buffer Block { ... } instance[N];`
///   with exact block sizes (scalars, vectors, matrices, structures, arrays, runtime arrays);
/// - `layout(push_constant) uniform Block { ... } instance;`
/// - `layout(location = L) in|out <type> name[N];` and `layout(local_size_x = X, ...) in;`
/// It does not run the preprocessor: declarations that depend on macros or #include are not seen.
/// The layout reflected from the compiled SPIR-V is compared with it when the shader module is
/// created, so such a difference is reported instead of being trusted.
export module gfx.interface:shader_layout;

import std;
import gfx.enums;
import gfx.errors;

// Compile-time diagnostics: reaching one of these functions during constant evaluation stops the
// compilation with an error that names the function, i.e. the problem.
namespace gfx::shader_layout_diagnostics
{

void binding_name_longer_than_MAXIMUM_BINDING_NAME_LENGTH() {}
void shader_layout_has_more_bindings_than_MAXIMUM_SHADER_BINDINGS() {}
void shader_layout_has_more_stage_variables_than_MAXIMUM_SHADER_STAGE_VARIABLES() {}
void glsl_has_more_structures_than_MAXIMUM_SHADER_STRUCTURES() {}
void glsl_structure_or_block_has_more_members_than_MAXIMUM_STRUCTURE_MEMBERS() {}
void glsl_unexpected_token() {}
void glsl_unknown_type_in_structure_or_block() {}
void glsl_unsupported_stage_variable_type() {}
void reflect_glsl_requires_exactly_one_shader_stage() {}

} // namespace gfx::shader_layout_diagnostics

export namespace gfx
{

/// @brief Name of a binding or stage variable, stored inline so that layouts are literal values.
struct BindingName
{
    std::array<char, MAXIMUM_BINDING_NAME_LENGTH> characters{}; ///< Characters (not null-terminated).
    std::uint32_t size = 0;                                      ///< Number of characters used.

    /// @brief Empty name.
    constexpr BindingName() = default;

    /// @brief Copies @p text; names longer than MAXIMUM_BINDING_NAME_LENGTH are rejected
    ///        (compile error during constant evaluation, InvalidUsageError at run time).
    constexpr BindingName(std::string_view text)
    {
        // Reject names that do not fit, at compile time when possible.
        if (text.size() > MAXIMUM_BINDING_NAME_LENGTH)
        {
            if consteval
            {
                shader_layout_diagnostics::binding_name_longer_than_MAXIMUM_BINDING_NAME_LENGTH();
            }
            else
            {
                throw InvalidUsageError({
                    .backend = std::nullopt,
                    .operation = "BindingName",
                    .problem = std::format("'{}' has {} characters; MAXIMUM_BINDING_NAME_LENGTH is {}", text,
                                           text.size(), MAXIMUM_BINDING_NAME_LENGTH),
                    .suggestion = "shorten the name, or raise MAXIMUM_BINDING_NAME_LENGTH in enums.cppm",
                });
            }
        }

        // Copy the characters.
        size = static_cast<std::uint32_t>(text.size());
        std::copy_n(text.begin(), size, characters.begin());
    }

    /// @brief Copies a string literal (allows `.name = "u_input"` in designated initializers).
    template<std::size_t Size>
    constexpr BindingName(const char (&text)[Size])
        : BindingName(std::string_view{text, Size - 1})
    {}

    /// @brief The name as a string view.
    [[nodiscard]] constexpr auto view() const noexcept -> std::string_view
    {
        return {characters.data(), size};
    }

    constexpr bool operator==(const BindingName&) const = default;
};

/// @brief A resource a shader binds through a descriptor set.
struct ShaderBinding
{
    std::uint32_t set = 0;                                ///< Descriptor set index.
    std::uint32_t binding = 0;                            ///< Binding index inside the set.
    DescriptorType type = DescriptorType::UNIFORM_BUFFER; ///< Kind of resource.
    std::uint32_t count = 1;                              ///< Array size; 0 = runtime-sized (bindless).
    std::uint32_t block_size = 0;                         ///< Declared size in bytes of a uniform or storage block.
    Format image_format = Format::UNDEFINED;              ///< Format qualifier of a storage image.
    BindingName name{};                                   ///< Block name for blocks, variable name otherwise.

    constexpr bool operator==(const ShaderBinding&) const = default;
};

/// @brief An input or output variable of a shader stage.
struct StageVariable
{
    std::uint32_t location = 0;       ///< First location.
    std::uint32_t location_count = 1; ///< Locations used (matrices and arrays use several).
    Format format = Format::UNDEFINED;///< Format of one location (e.g. R32G32B32_SFLOAT for vec3).
    BindingName name{};               ///< Variable name.

    constexpr bool operator==(const StageVariable&) const = default;
};

/// @brief Interface of one shader: bindings, push constants, stage inputs and outputs, workgroup size.
struct ShaderLayout
{
    ShaderStage stage = ShaderStage::NONE;                                ///< Stage the shader runs in.
    std::array<ShaderBinding, MAXIMUM_SHADER_BINDINGS> binding_storage{}; ///< Bindings (first binding_count used).
    std::uint32_t binding_count = 0;                                      ///< Number of bindings.
    std::array<StageVariable, MAXIMUM_SHADER_STAGE_VARIABLES> input_storage{};  ///< Inputs (first input_count used).
    std::uint32_t input_count = 0;                                        ///< Number of inputs.
    std::array<StageVariable, MAXIMUM_SHADER_STAGE_VARIABLES> output_storage{}; ///< Outputs (first output_count used).
    std::uint32_t output_count = 0;                                       ///< Number of outputs.
    std::uint32_t push_constant_size = 0;                                 ///< Bytes of push constants (0 = none).
    std::array<std::uint32_t, 3> workgroup_size{1, 1, 1};                 ///< Compute workgroup size.

    /// @brief Bindings of the shader.
    [[nodiscard]] constexpr auto bindings() const noexcept -> std::span<const ShaderBinding>
    {
        return {binding_storage.data(), binding_count};
    }

    /// @brief Stage inputs of the shader.
    [[nodiscard]] constexpr auto inputs() const noexcept -> std::span<const StageVariable>
    {
        return {input_storage.data(), input_count};
    }

    /// @brief Stage outputs of the shader.
    [[nodiscard]] constexpr auto outputs() const noexcept -> std::span<const StageVariable>
    {
        return {output_storage.data(), output_count};
    }

    /// @brief The binding called @p name, if any.
    [[nodiscard]] constexpr auto find(std::string_view name) const noexcept -> std::optional<ShaderBinding>
    {
        for (const ShaderBinding& binding : bindings())
        {
            if (binding.name.view() == name)
            {
                return binding;
            }
        }
        return std::nullopt;
    }

    /// @brief The binding at (@p set, @p binding), if any.
    [[nodiscard]] constexpr auto find(std::uint32_t set, std::uint32_t binding) const noexcept -> std::optional<ShaderBinding>
    {
        for (const ShaderBinding& candidate : bindings())
        {
            if (candidate.set == set && candidate.binding == binding)
            {
                return candidate;
            }
        }
        return std::nullopt;
    }

    /// @brief Appends a binding (compile error or InvalidUsageError when MAXIMUM_SHADER_BINDINGS is exceeded).
    constexpr void add_binding(const ShaderBinding& binding)
    {
        if (binding_count == MAXIMUM_SHADER_BINDINGS)
        {
            report_overflow("bindings", MAXIMUM_SHADER_BINDINGS, "MAXIMUM_SHADER_BINDINGS");
        }
        binding_storage[binding_count++] = binding;
    }

    /// @brief Appends a stage input.
    constexpr void add_input(const StageVariable& variable)
    {
        if (input_count == MAXIMUM_SHADER_STAGE_VARIABLES)
        {
            report_overflow("stage inputs", MAXIMUM_SHADER_STAGE_VARIABLES, "MAXIMUM_SHADER_STAGE_VARIABLES");
        }
        input_storage[input_count++] = variable;
    }

    /// @brief Appends a stage output.
    constexpr void add_output(const StageVariable& variable)
    {
        if (output_count == MAXIMUM_SHADER_STAGE_VARIABLES)
        {
            report_overflow("stage outputs", MAXIMUM_SHADER_STAGE_VARIABLES, "MAXIMUM_SHADER_STAGE_VARIABLES");
        }
        output_storage[output_count++] = variable;
    }

    /// @brief Copy with one more binding (consteval builder for declared layouts).
    [[nodiscard]] constexpr auto with_binding(this ShaderLayout self, const ShaderBinding& binding) -> ShaderLayout
    {
        self.add_binding(binding);
        return self;
    }

    /// @brief Copy with one more stage input.
    [[nodiscard]] constexpr auto with_input(this ShaderLayout self, const StageVariable& variable) -> ShaderLayout
    {
        self.add_input(variable);
        return self;
    }

    /// @brief Copy with one more stage output.
    [[nodiscard]] constexpr auto with_output(this ShaderLayout self, const StageVariable& variable) -> ShaderLayout
    {
        self.add_output(variable);
        return self;
    }

    /// @brief Copy with @p size bytes of push constants.
    [[nodiscard]] constexpr auto with_push_constants(this ShaderLayout self, std::uint32_t size) -> ShaderLayout
    {
        self.push_constant_size = size;
        return self;
    }

    /// @brief Copy with a compute workgroup size.
    [[nodiscard]] constexpr auto with_workgroup_size(this ShaderLayout self,
                                                     std::uint32_t x,
                                                     std::uint32_t y = 1,
                                                     std::uint32_t z = 1) -> ShaderLayout
    {
        self.workgroup_size = {x, y, z};
        return self;
    }

    constexpr bool operator==(const ShaderLayout&) const = default;

private:
    static constexpr void report_overflow(std::string_view what, std::uint32_t maximum, std::string_view constant)
    {
        if consteval
        {
            if (constant == "MAXIMUM_SHADER_BINDINGS")
            {
                shader_layout_diagnostics::shader_layout_has_more_bindings_than_MAXIMUM_SHADER_BINDINGS();
            }
            else
            {
                shader_layout_diagnostics::shader_layout_has_more_stage_variables_than_MAXIMUM_SHADER_STAGE_VARIABLES();
            }
        }
        else
        {
            throw InvalidUsageError({
                .backend = std::nullopt,
                .operation = "ShaderLayout",
                .problem = std::format("a shader layout holds at most {} {} ({})", maximum, what, constant),
                .suggestion = std::format("raise {} in enums.cppm", constant),
            });
        }
    }
};

} // namespace gfx

// =================================================================================================
// consteval GLSL reflection (implementation)
// =================================================================================================
namespace gfx::glsl_reflection
{

/// Splits GLSL source into identifiers, numbers and single-character punctuation, skipping
/// whitespace and comments.
class Lexer
{
public:
    constexpr explicit Lexer(std::string_view source) noexcept
        : m_source{source}
    {}

    /// Next token, or an empty view at the end of the source.
    [[nodiscard]] constexpr auto next() noexcept -> std::string_view
    {
        skip_trivia();
        if (m_position >= m_source.size())
        {
            return {};
        }

        // Identifiers and numbers are runs of word characters; anything else is one character.
        const std::size_t start = m_position;
        if (is_word_character(m_source[m_position]))
        {
            while (m_position < m_source.size() && is_word_character(m_source[m_position]))
            {
                ++m_position;
            }
        }
        else
        {
            ++m_position;
        }
        return m_source.substr(start, m_position - start);
    }

    /// Next token without consuming it.
    [[nodiscard]] constexpr auto peek() const noexcept -> std::string_view
    {
        Lexer copy = *this;
        return copy.next();
    }

    /// Consumes the rest of the current line (preprocessor directives).
    constexpr void skip_line() noexcept
    {
        while (m_position < m_source.size() && m_source[m_position] != '\n')
        {
            ++m_position;
        }
    }

    /// Whether only whitespace and comments remain.
    [[nodiscard]] constexpr auto at_end() noexcept -> bool
    {
        skip_trivia();
        return m_position >= m_source.size();
    }

    /// Consumes the next token, which must be @p expected.
    constexpr void expect(std::string_view expected) noexcept
    {
        if (next() != expected)
        {
            shader_layout_diagnostics::glsl_unexpected_token();
        }
    }

private:
    [[nodiscard]] static constexpr auto is_word_character(char character) noexcept -> bool
    {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '_';
    }

    constexpr void skip_trivia() noexcept
    {
        while (m_position < m_source.size())
        {
            const char character = m_source[m_position];
            if (character == ' ' || character == '\t' || character == '\n' || character == '\r')
            {
                ++m_position;
            }
            else if (m_source.substr(m_position, 2) == "//")
            {
                skip_line();
            }
            else if (m_source.substr(m_position, 2) == "/*")
            {
                const std::size_t end = m_source.find("*/", m_position + 2);
                m_position = end == std::string_view::npos ? m_source.size() : end + 2;
            }
            else
            {
                return;
            }
        }
    }

private:
    std::string_view m_source;
    std::size_t m_position = 0;
};

/// Size and alignment of a type under a block packing.
struct TypeLayout
{
    std::uint32_t size = 0;
    std::uint32_t alignment = 0;
    bool known = false;
};

/// A structure declared in the source, kept as member types and array sizes until a packing is known.
struct Structure
{
    std::string_view name;
    std::array<std::string_view, MAXIMUM_STRUCTURE_MEMBERS> member_types{};
    std::array<std::uint32_t, MAXIMUM_STRUCTURE_MEMBERS> member_counts{};
    std::uint32_t member_count = 0;
};

/// Every structure declared so far.
struct Structures
{
    std::array<Structure, MAXIMUM_SHADER_STRUCTURES> definitions{};
    std::uint32_t count = 0;
};

/// Qualifiers of a `layout(...)` list.
struct Qualifiers
{
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    std::uint32_t location = 0;
    bool has_location = false;
    bool push_constant = false;
    BlockPacking packing = BlockPacking::STD140;
    bool packing_explicit = false;
    Format image_format = Format::UNDEFINED;
    std::array<std::uint32_t, 3> local_size{1, 1, 1};
    bool has_local_size = false;
};

[[nodiscard]] constexpr auto
round_up(std::uint32_t value,
         std::uint32_t alignment) noexcept -> std::uint32_t
{
    return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
}

[[nodiscard]] constexpr auto
parse_unsigned(std::string_view text) noexcept -> std::uint32_t
{
    std::uint32_t value = 0;
    for (const char character : text)
    {
        if (character < '0' || character > '9')
        {
            break;
        }
        value = value * 10 + static_cast<std::uint32_t>(character - '0');
    }
    return value;
}

[[nodiscard]] constexpr auto
is_digit_between(char character,
                 char first,
                 char last) noexcept -> bool
{
    return character >= first && character <= last;
}

/// vecN-like type with @p components components of @p component_size bytes.
[[nodiscard]] constexpr auto
vector_layout(std::uint32_t components,
              std::uint32_t component_size,
              BlockPacking packing) noexcept -> TypeLayout
{
    // Scalar packing aligns vectors to their component; std140/std430 align vec3 like vec4.
    const std::uint32_t alignment = packing == BlockPacking::SCALAR ? component_size
                                                                    : (components == 3 ? 4 : components) * component_size;
    return {components * component_size, alignment, true};
}

/// Layout of an array of @p count elements (count 0 = runtime-sized: contributes no bytes).
[[nodiscard]] constexpr auto
array_layout(TypeLayout element,
             std::uint32_t count,
             BlockPacking packing) noexcept -> TypeLayout
{
    // std140 rounds the element alignment (and so the stride) up to 16 bytes.
    const std::uint32_t alignment = packing == BlockPacking::STD140 ? round_up(element.alignment, 16) : element.alignment;
    const std::uint32_t stride = round_up(element.size, alignment);
    return {stride * count, alignment, true};
}

/// Layout of a built-in GLSL type, or `known == false`.
[[nodiscard]] constexpr auto
builtin_layout(std::string_view type,
               BlockPacking packing) noexcept -> TypeLayout
{
    // Scalars.
    if (type == "float" || type == "int" || type == "uint" || type == "bool")
    {
        return {4, 4, true};
    }
    if (type == "double")
    {
        return {8, 8, true};
    }

    // Vectors: vecN, ivecN, uvecN, bvecN (4-byte components) and dvecN (8-byte components).
    for (const std::string_view prefix : {std::string_view{"vec"}, std::string_view{"ivec"}, std::string_view{"uvec"},
                                          std::string_view{"bvec"}, std::string_view{"dvec"}})
    {
        if (type.size() == prefix.size() + 1 && type.starts_with(prefix) && is_digit_between(type.back(), '2', '4'))
        {
            const std::uint32_t component_size = prefix == "dvec" ? 8 : 4;
            return vector_layout(static_cast<std::uint32_t>(type.back() - '0'), component_size, packing);
        }
    }

    // Matrices: matN (N columns of vecN) and matCxR (C columns of vecR), column-major.
    if (type.starts_with("mat") && type.size() >= 4 && is_digit_between(type[3], '2', '4'))
    {
        const auto columns = static_cast<std::uint32_t>(type[3] - '0');
        std::uint32_t rows = columns;
        if (type.size() == 6 && type[4] == 'x' && is_digit_between(type[5], '2', '4'))
        {
            rows = static_cast<std::uint32_t>(type[5] - '0');
        }
        else if (type.size() != 4)
        {
            return {};
        }
        const TypeLayout column = vector_layout(rows, 4, packing);
        return array_layout(column, columns, packing);
    }
    return {};
}

[[nodiscard]] constexpr auto type_layout(std::string_view type, BlockPacking packing, const Structures& structures) noexcept -> TypeLayout;

/// Layout of a declared structure under @p packing.
[[nodiscard]] constexpr auto
structure_layout(const Structure& structure,
                 BlockPacking packing,
                 const Structures& structures) noexcept -> TypeLayout
{
    std::uint32_t offset = 0;
    std::uint32_t alignment = packing == BlockPacking::SCALAR ? 1 : 4;

    // Members follow each other, each at the next offset aligned for it.
    for (std::uint32_t index = 0; index < structure.member_count; ++index)
    {
        TypeLayout member = type_layout(structure.member_types[index], packing, structures);
        if (!member.known)
        {
            shader_layout_diagnostics::glsl_unknown_type_in_structure_or_block();
        }
        if (structure.member_counts[index] != 1)
        {
            member = array_layout(member, structure.member_counts[index], packing);
        }
        offset = round_up(offset, member.alignment) + member.size;
        alignment = std::max(alignment, member.alignment);
    }

    // std140 aligns structures to 16 bytes; their size is padded to their alignment.
    if (packing == BlockPacking::STD140)
    {
        alignment = round_up(alignment, 16);
    }
    return {round_up(offset, alignment), alignment, true};
}

[[nodiscard]] constexpr auto
type_layout(std::string_view type,
            BlockPacking packing,
            const Structures& structures) noexcept -> TypeLayout
{
    // Built-in types first, then the structures declared before.
    if (const TypeLayout builtin = builtin_layout(type, packing); builtin.known)
    {
        return builtin;
    }
    for (std::uint32_t index = 0; index < structures.count; ++index)
    {
        if (structures.definitions[index].name == type)
        {
            return structure_layout(structures.definitions[index], packing, structures);
        }
    }
    return {};
}

/// Descriptor type of an opaque GLSL type, or std::nullopt for other types.
[[nodiscard]] constexpr auto
opaque_descriptor_type(std::string_view type) noexcept -> std::optional<DescriptorType>
{
    // Integer variants (isampler2D, uimage2D, ...) bind like their floating-point counterparts.
    std::string_view base = type;
    for (const std::string_view family : {std::string_view{"sampler"}, std::string_view{"texture"},
                                          std::string_view{"image"}, std::string_view{"subpassInput"}})
    {
        if ((type.starts_with('i') || type.starts_with('u')) && type.substr(1).starts_with(family))
        {
            base = type.substr(1);
        }
    }

    if (base == "sampler" || base == "samplerShadow")
    {
        return DescriptorType::SAMPLER;
    }
    if (base == "samplerBuffer")
    {
        return DescriptorType::UNIFORM_TEXEL_BUFFER;
    }
    if (base == "imageBuffer")
    {
        return DescriptorType::STORAGE_TEXEL_BUFFER;
    }
    if (base.starts_with("sampler"))
    {
        return DescriptorType::COMBINED_IMAGE_SAMPLER;
    }
    if (base.starts_with("texture"))
    {
        return DescriptorType::SAMPLED_IMAGE;
    }
    if (base.starts_with("image"))
    {
        return DescriptorType::STORAGE_IMAGE;
    }
    if (base.starts_with("subpassInput"))
    {
        return DescriptorType::INPUT_ATTACHMENT;
    }
    if (base == "accelerationStructureEXT")
    {
        return DescriptorType::ACCELERATION_STRUCTURE;
    }
    return std::nullopt;
}

/// Format of a storage-image format qualifier (rgba8, r32f, ...), or UNDEFINED for other qualifiers.
[[nodiscard]] constexpr auto
image_format_qualifier(std::string_view qualifier) noexcept -> Format
{
    constexpr std::array<std::pair<std::string_view, Format>, 38> table{{
        {"rgba32f", Format::R32G32B32A32_SFLOAT}, {"rgba16f", Format::R16G16B16A16_SFLOAT},
        {"rg32f", Format::R32G32_SFLOAT},         {"rg16f", Format::R16G16_SFLOAT},
        {"r11f_g11f_b10f", Format::B10G11R11_UFLOAT_PACK32},
        {"r32f", Format::R32_SFLOAT},             {"r16f", Format::R16_SFLOAT},
        {"rgba16", Format::R16G16B16A16_UNORM},   {"rgb10_a2", Format::A2B10G10R10_UNORM_PACK32},
        {"rgba8", Format::R8G8B8A8_UNORM},        {"rg16", Format::R16G16_UNORM},
        {"rg8", Format::R8G8_UNORM},              {"r16", Format::R16_UNORM},
        {"r8", Format::R8_UNORM},                 {"rgba16_snorm", Format::R16G16B16A16_SNORM},
        {"rgba8_snorm", Format::R8G8B8A8_SNORM},  {"rg16_snorm", Format::R16G16_SNORM},
        {"rg8_snorm", Format::R8G8_SNORM},        {"r16_snorm", Format::R16_SNORM},
        {"r8_snorm", Format::R8_SNORM},           {"rgba32i", Format::R32G32B32A32_SINT},
        {"rgba16i", Format::R16G16B16A16_SINT},   {"rgba8i", Format::R8G8B8A8_SINT},
        {"rg32i", Format::R32G32_SINT},           {"rg16i", Format::R16G16_SINT},
        {"rg8i", Format::R8G8_SINT},              {"r32i", Format::R32_SINT},
        {"r16i", Format::R16_SINT},               {"r8i", Format::R8_SINT},
        {"rgba32ui", Format::R32G32B32A32_UINT},  {"rgba16ui", Format::R16G16B16A16_UINT},
        {"rgba8ui", Format::R8G8B8A8_UINT},       {"rgb10_a2ui", Format::A2B10G10R10_UINT_PACK32},
        {"rg32ui", Format::R32G32_UINT},          {"rg16ui", Format::R16G16_UINT},
        {"rg8ui", Format::R8G8_UINT},             {"r32ui", Format::R32_UINT},
        {"r16ui", Format::R16_UINT},
    }};
    for (const auto& [name, format] : table)
    {
        if (name == qualifier)
        {
            return format;
        }
    }
    return Format::UNDEFINED;
}

/// Format of one location of a stage variable type, and how many locations a value of it uses.
[[nodiscard]] constexpr auto
stage_variable_format(std::string_view type) noexcept -> std::pair<Format, std::uint32_t>
{
    constexpr std::array<Format, 4> float_formats{Format::R32_SFLOAT, Format::R32G32_SFLOAT, Format::R32G32B32_SFLOAT,
                                                  Format::R32G32B32A32_SFLOAT};
    constexpr std::array<Format, 4> signed_formats{Format::R32_SINT, Format::R32G32_SINT, Format::R32G32B32_SINT,
                                                   Format::R32G32B32A32_SINT};
    constexpr std::array<Format, 4> unsigned_formats{Format::R32_UINT, Format::R32G32_UINT, Format::R32G32B32_UINT,
                                                     Format::R32G32B32A32_UINT};

    // Scalars and vectors use one location.
    if (type == "float")
    {
        return {float_formats[0], 1};
    }
    if (type == "int")
    {
        return {signed_formats[0], 1};
    }
    if (type == "uint")
    {
        return {unsigned_formats[0], 1};
    }
    const auto components = static_cast<std::size_t>(type.back() - '1');
    if (type.size() == 4 && type.starts_with("vec") && is_digit_between(type.back(), '2', '4'))
    {
        return {float_formats[components], 1};
    }
    if (type.size() == 5 && type.starts_with("ivec") && is_digit_between(type.back(), '2', '4'))
    {
        return {signed_formats[components], 1};
    }
    if (type.size() == 5 && type.starts_with("uvec") && is_digit_between(type.back(), '2', '4'))
    {
        return {unsigned_formats[components], 1};
    }

    // Matrices use one location per column.
    if (const TypeLayout matrix = builtin_layout(type, BlockPacking::STD430); matrix.known && type.starts_with("mat"))
    {
        const auto columns = static_cast<std::uint32_t>(type[3] - '0');
        const auto rows = type.size() == 6 ? static_cast<std::size_t>(type[5] - '1') : static_cast<std::size_t>(columns - 1);
        return {float_formats[rows], columns};
    }

    shader_layout_diagnostics::glsl_unsupported_stage_variable_type();
    return {Format::UNDEFINED, 1};
}

[[nodiscard]] constexpr auto
is_qualifier_keyword(std::string_view token) noexcept -> bool
{
    for (const std::string_view keyword : {"readonly", "writeonly", "coherent", "volatile", "restrict", "flat", "smooth",
                                           "noperspective", "highp", "mediump", "lowp", "centroid", "sample", "patch",
                                           "invariant", "precise", "const"})
    {
        if (token == keyword)
        {
            return true;
        }
    }
    return false;
}

/// Skips qualifier keywords and member `layout(...)` qualifiers; returns the next other token.
[[nodiscard]] constexpr auto
next_after_qualifiers(Lexer& lexer) noexcept -> std::string_view
{
    std::string_view token = lexer.next();
    while (is_qualifier_keyword(token) || token == "layout")
    {
        if (token == "layout")
        {
            lexer.expect("(");
            while (lexer.next() != ")")
            {
            }
        }
        token = lexer.next();
    }
    return token;
}

/// Parses `[N]` suffixes (several for multi-dimensional arrays): product of the sizes, 1 without
/// suffix, 0 for a runtime-sized array `[]`.
[[nodiscard]] constexpr auto
parse_array_suffix(Lexer& lexer) noexcept -> std::uint32_t
{
    std::uint32_t count = 1;
    while (lexer.peek() == "[")
    {
        (void)lexer.next();
        const std::string_view size = lexer.next();
        if (size == "]")
        {
            return 0;
        }
        lexer.expect("]");
        count *= parse_unsigned(size);
    }
    return count;
}

/// Parses the contents of a `layout(...)` list (the `layout` keyword is already consumed).
[[nodiscard]] constexpr auto
parse_qualifiers(Lexer& lexer) noexcept -> Qualifiers
{
    Qualifiers qualifiers;
    lexer.expect("(");
    for (std::string_view token = lexer.next(); token != ")"; token = lexer.next())
    {
        if (token == ",")
        {
            continue;
        }

        // Qualifiers are `name` or `name = value`.
        std::uint32_t value = 0;
        if (lexer.peek() == "=")
        {
            (void)lexer.next();
            value = parse_unsigned(lexer.next());
        }

        if (token == "set")
        {
            qualifiers.set = value;
        }
        else if (token == "binding")
        {
            qualifiers.binding = value;
        }
        else if (token == "location")
        {
            qualifiers.location = value;
            qualifiers.has_location = true;
        }
        else if (token == "push_constant")
        {
            qualifiers.push_constant = true;
        }
        else if (token == "std140" || token == "std430" || token == "scalar")
        {
            qualifiers.packing = token == "std140" ? BlockPacking::STD140
                                                   : (token == "std430" ? BlockPacking::STD430 : BlockPacking::SCALAR);
            qualifiers.packing_explicit = true;
        }
        else if (token == "local_size_x" || token == "local_size_y" || token == "local_size_z")
        {
            qualifiers.local_size[static_cast<std::size_t>(token.back() - 'x')] = value;
            qualifiers.has_local_size = true;
        }
        else if (const Format format = image_format_qualifier(token); format != Format::UNDEFINED)
        {
            qualifiers.image_format = format;
        }
    }
    return qualifiers;
}

/// Parses `{ type name[N], name2; ... }` and returns the declared size: the end of the last member,
/// as SPIR-V reflection reports it.
[[nodiscard]] constexpr auto
parse_block_members(Lexer& lexer,
                    BlockPacking packing,
                    const Structures& structures) noexcept -> std::uint32_t
{
    lexer.expect("{");
    std::uint32_t offset = 0;
    for (std::string_view type = next_after_qualifiers(lexer); type != "}"; type = next_after_qualifiers(lexer))
    {
        const TypeLayout element = type_layout(type, packing, structures);
        if (!element.known)
        {
            shader_layout_diagnostics::glsl_unknown_type_in_structure_or_block();
        }

        // One type may declare several members: `vec3 a, b[2];`.
        for (std::string_view separator = ","; separator == ","; separator = lexer.next())
        {
            (void)lexer.next(); // member name
            const std::uint32_t count = parse_array_suffix(lexer);
            const TypeLayout member = count == 1 ? element : array_layout(element, count, packing);
            offset = round_up(offset, member.alignment) + member.size;
        }
    }
    return offset;
}

/// Parses `struct Name { type member[N]; ... };` (the `struct` keyword is already consumed).
constexpr void
parse_structure(Lexer& lexer,
                Structures& structures) noexcept
{
    if (structures.count == MAXIMUM_SHADER_STRUCTURES)
    {
        shader_layout_diagnostics::glsl_has_more_structures_than_MAXIMUM_SHADER_STRUCTURES();
    }
    Structure& structure = structures.definitions[structures.count++];
    structure.name = lexer.next();
    lexer.expect("{");
    for (std::string_view type = next_after_qualifiers(lexer); type != "}"; type = next_after_qualifiers(lexer))
    {
        for (std::string_view separator = ","; separator == ","; separator = lexer.next())
        {
            if (structure.member_count == MAXIMUM_STRUCTURE_MEMBERS)
            {
                shader_layout_diagnostics::glsl_structure_or_block_has_more_members_than_MAXIMUM_STRUCTURE_MEMBERS();
            }
            (void)lexer.next(); // member name
            structure.member_types[structure.member_count] = type;
            structure.member_counts[structure.member_count] = parse_array_suffix(lexer);
            ++structure.member_count;
        }
    }
    lexer.expect(";");
}

/// Parses the declaration that follows a `layout(...)` list at global scope into @p layout.
constexpr void
parse_layout_declaration(Lexer& lexer,
                         const Qualifiers& qualifiers,
                         const Structures& structures,
                         ShaderLayout& layout) noexcept
{
    const std::string_view storage = next_after_qualifiers(lexer);

    // `layout(local_size_x = X, ...) in;` sets the compute workgroup size.
    if (storage == "in" && qualifiers.has_local_size && lexer.peek() == ";")
    {
        layout.workgroup_size = qualifiers.local_size;
        return;
    }

    // `layout(location = L) in|out type name[N];` declares a stage input or output.
    if ((storage == "in" || storage == "out") && qualifiers.has_location)
    {
        const std::string_view type = next_after_qualifiers(lexer);
        const std::string_view name = lexer.next();
        const std::uint32_t count = parse_array_suffix(lexer);
        const auto [format, locations] = stage_variable_format(type);
        const StageVariable variable{
            .location = qualifiers.location,
            .location_count = locations * (count == 0 ? 1 : count),
            .format = format,
            .name = BindingName{name},
        };
        if (storage == "in")
        {
            layout.add_input(variable);
        }
        else
        {
            layout.add_output(variable);
        }
        return;
    }
    if (storage != "uniform" && storage != "buffer")
    {
        return;
    }

    // Opaque uniforms: `uniform sampler2D name[N];`.
    const std::string_view type_or_block = next_after_qualifiers(lexer);
    if (const auto descriptor_type = opaque_descriptor_type(type_or_block); storage == "uniform" && descriptor_type)
    {
        const std::string_view name = lexer.next();
        layout.add_binding({
            .set = qualifiers.set,
            .binding = qualifiers.binding,
            .type = *descriptor_type,
            .count = parse_array_suffix(lexer),
            .block_size = 0,
            .image_format = qualifiers.image_format,
            .name = BindingName{name},
        });
        return;
    }

    // Blocks: uniform blocks default to std140, storage and push-constant blocks to std430.
    const bool storage_block = storage == "buffer";
    const BlockPacking packing = qualifiers.packing_explicit ? qualifiers.packing
                               : (storage_block || qualifiers.push_constant ? BlockPacking::STD430 : BlockPacking::STD140);
    const std::uint32_t size = parse_block_members(lexer, packing, structures);
    if (lexer.peek() != ";" && lexer.peek() != "[")
    {
        (void)lexer.next(); // instance name
    }
    const std::uint32_t count = parse_array_suffix(lexer);
    if (qualifiers.push_constant)
    {
        layout.push_constant_size = size;
        return;
    }
    layout.add_binding({
        .set = qualifiers.set,
        .binding = qualifiers.binding,
        .type = storage_block ? DescriptorType::STORAGE_BUFFER : DescriptorType::UNIFORM_BUFFER,
        .count = count,
        .block_size = size,
        .image_format = Format::UNDEFINED,
        .name = BindingName{type_or_block},
    });
}

} // namespace gfx::glsl_reflection

export namespace gfx
{

/// @brief Reflects the interface of a GLSL shader at compile time.
/// @param source GLSL source text (Vulkan GLSL, `#version 450` or later).
/// @param stage  Stage the shader is compiled for (exactly one stage).
/// @return The shader's bindings, push-constant size, stage inputs/outputs and workgroup size.
[[nodiscard]] consteval auto
reflect_glsl(std::string_view source,
             ShaderStage stage) -> ShaderLayout
{
    if (!std::has_single_bit(std::to_underlying(stage)))
    {
        shader_layout_diagnostics::reflect_glsl_requires_exactly_one_shader_stage();
    }

    ShaderLayout layout;
    layout.stage = stage;
    glsl_reflection::Structures structures;
    glsl_reflection::Lexer lexer{source};
    int depth = 0;

    // Only global declarations matter: function bodies (depth > 0) are skipped.
    while (!lexer.at_end())
    {
        const std::string_view token = lexer.next();
        if (token == "#")
        {
            lexer.skip_line();
        }
        else if (token == "{")
        {
            ++depth;
        }
        else if (token == "}")
        {
            --depth;
        }
        else if (depth == 0 && token == "struct")
        {
            glsl_reflection::parse_structure(lexer, structures);
        }
        else if (depth == 0 && token == "layout")
        {
            const glsl_reflection::Qualifiers qualifiers = glsl_reflection::parse_qualifiers(lexer);
            glsl_reflection::parse_layout_declaration(lexer, qualifiers, structures, layout);
        }
    }
    return layout;
}

} // namespace gfx
