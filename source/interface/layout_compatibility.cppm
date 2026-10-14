/// @file layout_compatibility.cppm
/// @brief Compile-time definitions of pipeline layouts, vertex layouts and attachments; the checks
///        that compare a ShaderLayout with them; and typed shader definitions.
///
/// The same constexpr check functions serve two purposes:
/// - at compile time, the typed API (Device::create_compute_pipeline<...> and friends) evaluates them
///   and a mismatch stops the compilation, printing the complete LayoutCheck value;
/// - at run time, backends call validate_layout_check(), which throws ShaderLayoutMismatchError.
export module gfx.interface:layout_compatibility;

import std;
import gfx.enums;
import gfx.errors;
import :shader_layout;

// Compile-time diagnostics (see shader_layout.cppm).
namespace gfx::layout_diagnostics
{

void pipeline_layout_has_more_bindings_than_MAXIMUM_PIPELINE_LAYOUT_BINDINGS() {}
void vertex_layout_has_more_bindings_than_MAXIMUM_VERTEX_INPUT_BINDINGS() {}
void vertex_layout_has_more_attributes_than_MAXIMUM_VERTEX_INPUT_ATTRIBUTES() {}
void attachment_layout_has_more_color_attachments_than_MAXIMUM_COLOR_ATTACHMENTS() {}
void shader_declares_no_binding_with_this_name() {}
void shaders_declare_conflicting_bindings() {}
void shader_layout_is_incompatible() {}

} // namespace gfx::layout_diagnostics

export namespace gfx
{

/// @brief Compile-time string usable as a template argument (e.g. binding_of<"u_input", Shader>()).
template<std::size_t Size>
struct FixedString
{
    std::array<char, Size> characters{}; ///< Characters including the terminating null.

    /// @brief Copies a string literal.
    consteval FixedString(const char (&text)[Size])
    {
        std::copy_n(text, Size, characters.begin());
    }

    /// @brief The string without its terminating null.
    [[nodiscard]] constexpr auto view() const noexcept -> std::string_view
    {
        return {characters.data(), Size - 1};
    }
};

// =================================================================================================
// Pipeline layout definition
// =================================================================================================

/// @brief One binding of a pipeline layout: a (set, binding) slot, its type, size and visibility.
struct LayoutBinding
{
    std::uint32_t set = 0;                                       ///< Descriptor set index.
    std::uint32_t binding = 0;                                   ///< Binding index inside the set.
    DescriptorType type = DescriptorType::UNIFORM_BUFFER;        ///< Kind of resource.
    std::uint32_t count = 1;                                     ///< Number of descriptors.
    ShaderStage stages = ShaderStage::NONE;                      ///< Stages that may access it.
    DescriptorBindingOption options = DescriptorBindingOption::NONE; ///< Descriptor indexing options.

    constexpr bool operator==(const LayoutBinding&) const = default;
};

/// @brief Complete pipeline layout known at compile time: every binding of every set and the push
///        constants. Device::create_pipeline_layout<DEFINITION>() creates the matching objects.
struct PipelineLayoutDefinition
{
    std::array<LayoutBinding, MAXIMUM_PIPELINE_LAYOUT_BINDINGS> binding_storage{}; ///< Bindings (first binding_count used).
    std::uint32_t binding_count = 0;                     ///< Number of bindings.
    std::uint32_t push_constant_size = 0;                ///< Bytes of push constants (0 = none).
    ShaderStage push_constant_stages = ShaderStage::NONE;///< Stages that may read the push constants.

    /// @brief Every binding, in declaration order.
    [[nodiscard]] constexpr auto bindings() const noexcept -> std::span<const LayoutBinding>
    {
        return {binding_storage.data(), binding_count};
    }

    /// @brief The binding at (@p set, @p binding), if any.
    [[nodiscard]] constexpr auto find(std::uint32_t set, std::uint32_t binding) const noexcept -> std::optional<LayoutBinding>
    {
        for (const LayoutBinding& candidate : bindings())
        {
            if (candidate.set == set && candidate.binding == binding)
            {
                return candidate;
            }
        }
        return std::nullopt;
    }

    /// @brief Number of descriptor sets: highest set index used + 1 (0 without bindings).
    [[nodiscard]] constexpr auto set_count() const noexcept -> std::uint32_t
    {
        std::uint32_t count = 0;
        for (const LayoutBinding& binding : bindings())
        {
            count = std::max(count, binding.set + 1);
        }
        return count;
    }

    /// @brief Appends a binding (compile error or InvalidUsageError beyond MAXIMUM_PIPELINE_LAYOUT_BINDINGS).
    constexpr void add_binding(const LayoutBinding& binding)
    {
        if (binding_count == MAXIMUM_PIPELINE_LAYOUT_BINDINGS)
        {
            if consteval
            {
                layout_diagnostics::pipeline_layout_has_more_bindings_than_MAXIMUM_PIPELINE_LAYOUT_BINDINGS();
            }
            else
            {
                throw InvalidUsageError({
                    .backend = std::nullopt,
                    .operation = "PipelineLayoutDefinition::add_binding",
                    .problem = std::format("a pipeline layout definition holds at most {} bindings",
                                           MAXIMUM_PIPELINE_LAYOUT_BINDINGS),
                    .suggestion = "raise MAXIMUM_PIPELINE_LAYOUT_BINDINGS in enums.cppm",
                });
            }
        }
        binding_storage[binding_count++] = binding;
    }

    /// @brief Copy with one more binding.
    [[nodiscard]] constexpr auto with_binding(this PipelineLayoutDefinition self,
                                              std::uint32_t set,
                                              std::uint32_t binding,
                                              DescriptorType type,
                                              ShaderStage stages,
                                              std::uint32_t count = 1,
                                              DescriptorBindingOption options = DescriptorBindingOption::NONE)
        -> PipelineLayoutDefinition
    {
        self.add_binding({.set = set, .binding = binding, .type = type, .count = count, .stages = stages, .options = options});
        return self;
    }

    /// @brief Copy with @p size bytes of push constants visible to @p stages.
    [[nodiscard]] constexpr auto with_push_constants(this PipelineLayoutDefinition self,
                                                     std::uint32_t size,
                                                     ShaderStage stages) -> PipelineLayoutDefinition
    {
        self.push_constant_size = size;
        self.push_constant_stages = stages;
        return self;
    }

    constexpr bool operator==(const PipelineLayoutDefinition&) const = default;
};

// =================================================================================================
// Vertex layout definition
// =================================================================================================

/// @brief A vertex buffer binding: stride and step rate.
struct VertexInputBinding
{
    std::uint32_t binding = 0;                         ///< Binding index.
    std::uint32_t stride = 0;                          ///< Bytes between consecutive elements.
    VertexInputRate rate = VertexInputRate::VERTEX;    ///< Per vertex or per instance.

    constexpr bool operator==(const VertexInputBinding&) const = default;
};

/// @brief A vertex attribute: where a shader input location reads from.
struct VertexInputAttribute
{
    std::uint32_t location = 0;        ///< Shader input location.
    std::uint32_t binding = 0;         ///< Vertex buffer binding it reads.
    Format format = Format::UNDEFINED; ///< Format of the data in the buffer.
    std::uint32_t offset = 0;          ///< Byte offset inside one element.

    constexpr bool operator==(const VertexInputAttribute&) const = default;
};

/// @brief Vertex buffer bindings and attributes known at compile time.
struct VertexLayoutDefinition
{
    std::array<VertexInputBinding, MAXIMUM_VERTEX_INPUT_BINDINGS> binding_storage{};       ///< Bindings.
    std::uint32_t binding_count = 0;                                                       ///< Number of bindings.
    std::array<VertexInputAttribute, MAXIMUM_VERTEX_INPUT_ATTRIBUTES> attribute_storage{}; ///< Attributes.
    std::uint32_t attribute_count = 0;                                                     ///< Number of attributes.

    /// @brief Every binding.
    [[nodiscard]] constexpr auto bindings() const noexcept -> std::span<const VertexInputBinding>
    {
        return {binding_storage.data(), binding_count};
    }

    /// @brief Every attribute.
    [[nodiscard]] constexpr auto attributes() const noexcept -> std::span<const VertexInputAttribute>
    {
        return {attribute_storage.data(), attribute_count};
    }

    /// @brief The attribute that feeds @p location, if any.
    [[nodiscard]] constexpr auto find_attribute(std::uint32_t location) const noexcept -> std::optional<VertexInputAttribute>
    {
        for (const VertexInputAttribute& attribute : attributes())
        {
            if (attribute.location == location)
            {
                return attribute;
            }
        }
        return std::nullopt;
    }

    /// @brief Copy with one more binding.
    [[nodiscard]] constexpr auto with_binding(this VertexLayoutDefinition self,
                                              std::uint32_t binding,
                                              std::uint32_t stride,
                                              VertexInputRate rate = VertexInputRate::VERTEX) -> VertexLayoutDefinition
    {
        if (self.binding_count == MAXIMUM_VERTEX_INPUT_BINDINGS)
        {
            layout_diagnostics::vertex_layout_has_more_bindings_than_MAXIMUM_VERTEX_INPUT_BINDINGS();
        }
        self.binding_storage[self.binding_count++] = {.binding = binding, .stride = stride, .rate = rate};
        return self;
    }

    /// @brief Copy with one more attribute.
    [[nodiscard]] constexpr auto with_attribute(this VertexLayoutDefinition self,
                                                std::uint32_t location,
                                                std::uint32_t binding,
                                                Format format,
                                                std::uint32_t offset) -> VertexLayoutDefinition
    {
        if (self.attribute_count == MAXIMUM_VERTEX_INPUT_ATTRIBUTES)
        {
            layout_diagnostics::vertex_layout_has_more_attributes_than_MAXIMUM_VERTEX_INPUT_ATTRIBUTES();
        }
        self.attribute_storage[self.attribute_count++] = {.location = location, .binding = binding, .format = format, .offset = offset};
        return self;
    }

    constexpr bool operator==(const VertexLayoutDefinition&) const = default;
};

// =================================================================================================
// Attachment layout definition
// =================================================================================================

/// @brief Formats of the attachments a graphics pipeline renders to, known at compile time.
struct AttachmentLayoutDefinition
{
    std::array<Format, MAXIMUM_COLOR_ATTACHMENTS> color_format_storage{}; ///< Color formats (first color_count used).
    std::uint32_t color_count = 0;                                        ///< Number of color attachments.
    Format depth_format = Format::UNDEFINED;                              ///< Depth format, UNDEFINED = none.
    Format stencil_format = Format::UNDEFINED;                            ///< Stencil format, UNDEFINED = none.
    SampleCount samples = SampleCount::ONE;                               ///< Samples per pixel.

    /// @brief Formats of the color attachments, by location.
    [[nodiscard]] constexpr auto color_formats() const noexcept -> std::span<const Format>
    {
        return {color_format_storage.data(), color_count};
    }

    /// @brief Copy with one more color attachment (at the next location).
    [[nodiscard]] constexpr auto with_color(this AttachmentLayoutDefinition self, Format format) -> AttachmentLayoutDefinition
    {
        if (self.color_count == MAXIMUM_COLOR_ATTACHMENTS)
        {
            layout_diagnostics::attachment_layout_has_more_color_attachments_than_MAXIMUM_COLOR_ATTACHMENTS();
        }
        self.color_format_storage[self.color_count++] = format;
        return self;
    }

    /// @brief Copy with a depth attachment (also the stencil attachment for depth/stencil formats).
    [[nodiscard]] constexpr auto with_depth(this AttachmentLayoutDefinition self, Format format) -> AttachmentLayoutDefinition
    {
        self.depth_format = format;
        if (has_any(format_properties(format).aspects, ImageAspect::STENCIL))
        {
            self.stencil_format = format;
        }
        return self;
    }

    /// @brief Copy with @p samples samples per pixel.
    [[nodiscard]] constexpr auto with_samples(this AttachmentLayoutDefinition self, SampleCount samples) -> AttachmentLayoutDefinition
    {
        self.samples = samples;
        return self;
    }

    constexpr bool operator==(const AttachmentLayoutDefinition&) const = default;
};

// =================================================================================================
// Checks
// =================================================================================================

/// @brief Result of comparing a shader layout with what it is combined with. On failure, GCC prints
///        this value in the compile error, so every field names part of the problem.
struct LayoutCheck
{
    LayoutMismatch mismatch = LayoutMismatch::NONE;          ///< Kind of mismatch.
    std::uint32_t set = 0;                                   ///< Descriptor set of a binding mismatch.
    std::uint32_t index = 0;                                 ///< Binding or location.
    DescriptorType shader_type = DescriptorType::UNIFORM_BUFFER; ///< Descriptor type in the shader.
    DescriptorType layout_type = DescriptorType::UNIFORM_BUFFER; ///< Descriptor type in the layout.
    Format shader_format = Format::UNDEFINED;                ///< Format of a shader input or output.
    Format layout_format = Format::UNDEFINED;                ///< Format of the attribute or attachment.
    std::uint32_t shader_value = 0;                          ///< Count or size on the shader side.
    std::uint32_t layout_value = 0;                          ///< Count or size on the layout side.
    ShaderStage shader_stage = ShaderStage::NONE;            ///< Stage of the shader.
    ShaderStage layout_stages = ShaderStage::NONE;           ///< Stages the layout allows.

    /// @brief Whether the layouts are compatible.
    [[nodiscard]] constexpr auto ok() const noexcept -> bool
    {
        return mismatch == LayoutMismatch::NONE;
    }

    constexpr bool operator==(const LayoutCheck&) const = default;
};

/// @brief How shaders read a format: SIGNED_FLOAT for every floating-point or normalized format,
///        SIGNED_INTEGER or UNSIGNED_INTEGER for integer formats, NONE for UNDEFINED.
[[nodiscard]] constexpr auto
shader_numeric_type(Format format) noexcept -> FormatNumericType
{
    switch (format_properties(format).numeric_type)
    {
        case FormatNumericType::SIGNED_INTEGER:
            return FormatNumericType::SIGNED_INTEGER;
        case FormatNumericType::UNSIGNED_INTEGER:
            return FormatNumericType::UNSIGNED_INTEGER;
        case FormatNumericType::NONE:
            return FormatNumericType::NONE;
        default:
            return FormatNumericType::SIGNED_FLOAT;
    }
}

/// @brief Whether a shader descriptor type can be bound through a layout descriptor type (a uniform
///        or storage block also accepts the dynamic-offset variant).
[[nodiscard]] constexpr auto
descriptor_types_compatible(DescriptorType shader_type,
                            DescriptorType layout_type) noexcept -> bool
{
    return shader_type == layout_type ||
           (shader_type == DescriptorType::UNIFORM_BUFFER && layout_type == DescriptorType::UNIFORM_BUFFER_DYNAMIC) ||
           (shader_type == DescriptorType::STORAGE_BUFFER && layout_type == DescriptorType::STORAGE_BUFFER_DYNAMIC);
}

/// @brief Checks that @p shader was written for the @p expected stage.
[[nodiscard]] constexpr auto
check_shader_stage(const ShaderLayout& shader,
                   ShaderStage expected) noexcept -> LayoutCheck
{
    if (shader.stage == expected)
    {
        return {};
    }
    return {.mismatch = LayoutMismatch::WRONG_SHADER_STAGE, .shader_stage = shader.stage, .layout_stages = expected};
}

/// @brief Checks that every binding and the push constants of @p shader are provided by @p layout.
[[nodiscard]] constexpr auto
check_pipeline_layout(const ShaderLayout& shader,
                      const PipelineLayoutDefinition& layout) noexcept -> LayoutCheck
{
    // Every binding of the shader needs a compatible, visible binding in the layout.
    for (const ShaderBinding& binding : shader.bindings())
    {
        const LayoutCheck where{.set = binding.set, .index = binding.binding, .shader_type = binding.type};
        const std::optional<LayoutBinding> provided = layout.find(binding.set, binding.binding);
        if (!provided)
        {
            LayoutCheck check = where;
            check.mismatch = LayoutMismatch::BINDING_MISSING_FROM_LAYOUT;
            return check;
        }
        if (!descriptor_types_compatible(binding.type, provided->type))
        {
            LayoutCheck check = where;
            check.mismatch = LayoutMismatch::DESCRIPTOR_TYPE_MISMATCH;
            check.layout_type = provided->type;
            return check;
        }
        // A runtime-sized shader array (count 0) accepts any count; otherwise the layout needs enough.
        if ((binding.count == 0 && provided->count == 0) || (binding.count != 0 && provided->count < binding.count))
        {
            LayoutCheck check = where;
            check.mismatch = LayoutMismatch::DESCRIPTOR_COUNT_MISMATCH;
            check.layout_type = provided->type;
            check.shader_value = binding.count;
            check.layout_value = provided->count;
            return check;
        }
        if (!has_all(provided->stages, shader.stage))
        {
            LayoutCheck check = where;
            check.mismatch = LayoutMismatch::STAGE_NOT_VISIBLE;
            check.layout_type = provided->type;
            check.shader_stage = shader.stage;
            check.layout_stages = provided->stages;
            return check;
        }
    }

    // The push constants of the shader must fit in the layout's range, visible to its stage.
    if (shader.push_constant_size > 0)
    {
        if (shader.push_constant_size > layout.push_constant_size)
        {
            return {.mismatch = LayoutMismatch::PUSH_CONSTANTS_TOO_LARGE,
                    .shader_value = shader.push_constant_size,
                    .layout_value = layout.push_constant_size};
        }
        if (!has_all(layout.push_constant_stages, shader.stage))
        {
            return {.mismatch = LayoutMismatch::PUSH_CONSTANT_STAGE_NOT_VISIBLE,
                    .shader_stage = shader.stage,
                    .layout_stages = layout.push_constant_stages};
        }
    }
    return {};
}

/// @brief Checks that every input location of a vertex shader is fed by an attribute of a matching
///        numeric type (float, signed or unsigned integer; component counts may differ).
[[nodiscard]] constexpr auto
check_vertex_input(const ShaderLayout& vertex_shader,
                   const VertexLayoutDefinition& vertices) noexcept -> LayoutCheck
{
    for (const StageVariable& input : vertex_shader.inputs())
    {
        for (std::uint32_t location = input.location; location < input.location + input.location_count; ++location)
        {
            const std::optional<VertexInputAttribute> attribute = vertices.find_attribute(location);
            if (!attribute)
            {
                return {.mismatch = LayoutMismatch::VERTEX_INPUT_WITHOUT_ATTRIBUTE, .index = location, .shader_format = input.format};
            }
            if (shader_numeric_type(attribute->format) != shader_numeric_type(input.format))
            {
                return {.mismatch = LayoutMismatch::VERTEX_INPUT_TYPE_MISMATCH,
                        .index = location,
                        .shader_format = input.format,
                        .layout_format = attribute->format};
            }
        }
    }
    return {};
}

/// @brief Checks that every output location of a fragment shader has a color attachment of a
///        matching numeric type.
[[nodiscard]] constexpr auto
check_fragment_output(const ShaderLayout& fragment_shader,
                      const AttachmentLayoutDefinition& attachments) noexcept -> LayoutCheck
{
    for (const StageVariable& output : fragment_shader.outputs())
    {
        for (std::uint32_t location = output.location; location < output.location + output.location_count; ++location)
        {
            if (location >= attachments.color_count)
            {
                return {.mismatch = LayoutMismatch::FRAGMENT_OUTPUT_WITHOUT_ATTACHMENT,
                        .index = location,
                        .shader_format = output.format,
                        .layout_value = attachments.color_count};
            }
            const Format attachment = attachments.color_format_storage[location];
            if (shader_numeric_type(attachment) != shader_numeric_type(output.format))
            {
                return {.mismatch = LayoutMismatch::FRAGMENT_OUTPUT_TYPE_MISMATCH,
                        .index = location,
                        .shader_format = output.format,
                        .layout_format = attachment};
            }
        }
    }
    return {};
}

/// @brief Human-readable explanation of a failed check (empty for a passed one).
[[nodiscard]] auto
describe(const LayoutCheck& check) -> std::string
{
    switch (check.mismatch)
    {
        case LayoutMismatch::NONE:
            return {};
        case LayoutMismatch::WRONG_SHADER_STAGE:
            return std::format("the shader was compiled for the {} stage, but it is used as a {} shader",
                               check.shader_stage, check.layout_stages);
        case LayoutMismatch::BINDING_MISSING_FROM_LAYOUT:
            return std::format("the shader uses set {} binding {} ({}), which the pipeline layout does not declare",
                               check.set, check.index, check.shader_type);
        case LayoutMismatch::DESCRIPTOR_TYPE_MISMATCH:
            return std::format("set {} binding {} is {} in the shader but {} in the pipeline layout",
                               check.set, check.index, check.shader_type, check.layout_type);
        case LayoutMismatch::DESCRIPTOR_COUNT_MISMATCH:
            return std::format("set {} binding {} needs {} descriptor(s) (0 = runtime-sized array) but the pipeline layout "
                               "provides {}",
                               check.set, check.index, check.shader_value, check.layout_value);
        case LayoutMismatch::STAGE_NOT_VISIBLE:
            return std::format("set {} binding {} is visible to {} in the pipeline layout, not to the {} stage",
                               check.set, check.index, check.layout_stages, check.shader_stage);
        case LayoutMismatch::PUSH_CONSTANTS_TOO_LARGE:
            return std::format("the shader uses {} bytes of push constants but the pipeline layout provides {}",
                               check.shader_value, check.layout_value);
        case LayoutMismatch::PUSH_CONSTANT_STAGE_NOT_VISIBLE:
            return std::format("the pipeline layout's push constants are visible to {}, not to the {} stage",
                               check.layout_stages, check.shader_stage);
        case LayoutMismatch::VERTEX_INPUT_WITHOUT_ATTRIBUTE:
            return std::format("vertex input location {} ({}) has no vertex attribute", check.index, check.shader_format);
        case LayoutMismatch::VERTEX_INPUT_TYPE_MISMATCH:
            return std::format("vertex input location {} is {} in the shader but its attribute format is {} "
                               "(float and integer types cannot be mixed)",
                               check.index, check.shader_format, check.layout_format);
        case LayoutMismatch::FRAGMENT_OUTPUT_WITHOUT_ATTACHMENT:
            return std::format("fragment output location {} ({}) has no color attachment; the pipeline has {}",
                               check.index, check.shader_format, check.layout_value);
        case LayoutMismatch::FRAGMENT_OUTPUT_TYPE_MISMATCH:
            return std::format("fragment output location {} is {} in the shader but the attachment format is {} "
                               "(float and integer types cannot be mixed)",
                               check.index, check.shader_format, check.layout_format);
        case LayoutMismatch::CONFLICTING_BINDINGS:
            return std::format("set {} binding {} is declared as {} by one shader and as {} by another",
                               check.set, check.index, check.shader_type, check.layout_type);
    }
    return std::format("unknown mismatch {}", check.mismatch);
}

/// @brief Does nothing for a passed check. For a failed one: a compile error during constant
///        evaluation, ShaderLayoutMismatchError at run time.
/// @param check       Result of a check_* function.
/// @param shader_name Name of the shader, for the message.
/// @param operation   Operation that combines the shader, for the message.
constexpr void
validate_layout_check(const LayoutCheck& check,
                      std::string_view shader_name,
                      std::string_view operation)
{
    if (check.ok())
    {
        return;
    }
    if consteval
    {
        layout_diagnostics::shader_layout_is_incompatible();
    }
    else
    {
        throw ShaderLayoutMismatchError({
            .backend = std::nullopt,
            .operation = std::string{operation},
            .shader_name = std::string{shader_name},
            .mismatch = check.mismatch,
            .problem = describe(check),
            .suggestion = "make the shader declaration and the layout it is combined with agree",
        });
    }
}

/// @brief Instantiated by the typed API to check a layout at compile time. On failure the static
///        assertion fails and the compiler prints the complete LayoutCheck value, e.g.
///        `LayoutCheck{LayoutMismatch::DESCRIPTOR_TYPE_MISMATCH, 0, 1, DescriptorType::STORAGE_IMAGE,
///        DescriptorType::SAMPLED_IMAGE, ...}`.
template<LayoutCheck Result>
struct LayoutDiagnostic
{
    static_assert(Result.ok(), "gfx: the shader does not match what it is combined with; the LayoutCheck value "
                               "above names the mismatch, set/binding or location, and both sides");

    /// @brief Whether the check passed (always true when the static assertion holds).
    static constexpr bool OK = Result.ok();
};

// =================================================================================================
// Typed shader definitions
// =================================================================================================

/// @brief A shader known at compile time: its source text, a name and its layout.
template<class Type>
concept ShaderDefinition = requires {
    { Type::SOURCE } -> std::convertible_to<std::string_view>;
    { Type::NAME } -> std::convertible_to<std::string_view>;
    { Type::LAYOUT } -> std::same_as<const ShaderLayout&>;
};

/// @brief GLSL shader whose layout is reflected from its source at compile time.
/// @tparam Source A `constexpr` character array holding the GLSL source.
/// @tparam Stage  Stage the shader is compiled for.
/// @tparam Name   Name used in messages and for hot reload.
template<const auto& Source, ShaderStage Stage, FixedString Name = "GLSL shader">
struct GlslShader
{
    static constexpr std::string_view SOURCE{Source};           ///< GLSL source.
    static constexpr std::string_view NAME = Name.view();       ///< Name for messages.
    static constexpr ShaderLayout LAYOUT = reflect_glsl(SOURCE, Stage); ///< Reflected layout.
};

/// @brief Shader whose layout is declared explicitly (for GLSL the reflection does not understand).
/// @tparam Source A `constexpr` character array holding the GLSL source.
/// @tparam Layout The declared layout (built with ShaderLayout's with_* methods).
/// @tparam Name   Name used in messages and for hot reload.
template<const auto& Source, const ShaderLayout& Layout, FixedString Name = "declared shader">
struct DeclaredShader
{
    static constexpr std::string_view SOURCE{Source};     ///< GLSL source.
    static constexpr std::string_view NAME = Name.view(); ///< Name for messages.
    static constexpr const ShaderLayout& LAYOUT = Layout; ///< Declared layout.
};

/// @brief Whether @p Shader's bindings and push constants are provided by @p Layout.
template<class Shader, const PipelineLayoutDefinition& Layout>
concept LayoutCompatible = ShaderDefinition<Shader> && check_pipeline_layout(Shader::LAYOUT, Layout).ok();

/// @brief Push-constant data of the exact size a shader declares.
template<class Data, class Shader>
concept PushConstantsFor = ShaderDefinition<Shader> && std::is_trivially_copyable_v<Data> &&
                           Shader::LAYOUT.push_constant_size > 0 && sizeof(Data) == Shader::LAYOUT.push_constant_size;

/// @brief The binding called @p Name in @p Shader (compile error when there is none).
template<FixedString Name, ShaderDefinition Shader>
[[nodiscard]] consteval auto
binding_of() -> ShaderBinding
{
    constexpr std::optional<ShaderBinding> binding = Shader::LAYOUT.find(Name.view());
    if (!binding)
    {
        layout_diagnostics::shader_declares_no_binding_with_this_name();
    }
    return *binding;
}

/// @brief The pipeline layout that exactly provides what @p Shaders use: their bindings (visible to
///        the stages that use them) and the largest push-constant block (compile error when two
///        shaders declare one (set, binding) with different types). Runtime-sized (bindless) arrays
///        get one descriptor: declare bindless layouts explicitly with their real size and options.
template<ShaderDefinition... Shaders>
[[nodiscard]] consteval auto
pipeline_layout_from() -> PipelineLayoutDefinition
{
    PipelineLayoutDefinition layout;
    const auto merge = [&layout](const ShaderLayout& shader)
    {
        // Bindings: add new ones, widen the visibility of shared ones.
        for (const ShaderBinding& binding : shader.bindings())
        {
            bool merged = false;
            for (std::uint32_t index = 0; index < layout.binding_count; ++index)
            {
                LayoutBinding& existing = layout.binding_storage[index];
                if (existing.set == binding.set && existing.binding == binding.binding)
                {
                    if (existing.type != binding.type || existing.count != std::max(binding.count, 1U))
                    {
                        layout_diagnostics::shaders_declare_conflicting_bindings();
                    }
                    existing.stages |= shader.stage;
                    merged = true;
                }
            }
            if (!merged)
            {
                layout.add_binding({.set = binding.set,
                                    .binding = binding.binding,
                                    .type = binding.type,
                                    .count = std::max(binding.count, 1U),
                                    .stages = shader.stage});
            }
        }

        // Push constants: one range large enough for every shader, visible to each of them.
        if (shader.push_constant_size > 0)
        {
            layout.push_constant_size = std::max(layout.push_constant_size, shader.push_constant_size);
            layout.push_constant_stages |= shader.stage;
        }
    };
    (merge(Shaders::LAYOUT), ...);
    return layout;
}

} // namespace gfx
