/// @file shader_validation.cppm
/// @brief Run-time checks of reflected shader layouts against what the shaders are combined with:
///        the pipeline layout, the vertex attributes and the color attachments. Backends call
///        validate_compute_pipeline() and validate_graphics_pipeline() when creating pipelines, so a
///        mismatch is a descriptive ShaderLayoutMismatchError instead of undefined GPU behavior.
export module gfx.interface:shader_validation;

import std;
import gfx.enums;
import gfx.errors;
import :shader_layout;
import :shader_module;
import :pipeline_layout;
import :graphics_pipeline;

export namespace gfx
{

/// @brief Result of comparing a shader layout with what it is combined with; every field names part
///        of the problem.
struct LayoutCheck
{
    LayoutMismatch mismatch = LayoutMismatch::NONE;              ///< Kind of mismatch.
    std::uint32_t set = 0;                                       ///< Descriptor set of a binding mismatch.
    std::uint32_t index = 0;                                     ///< Binding or location.
    DescriptorType shader_type = DescriptorType::UNIFORM_BUFFER; ///< Descriptor type in the shader.
    DescriptorType layout_type = DescriptorType::UNIFORM_BUFFER; ///< Descriptor type in the layout.
    Format shader_format = Format::UNDEFINED;                    ///< Format of a shader input or output.
    Format layout_format = Format::UNDEFINED;                    ///< Format of the attribute or attachment.
    std::uint32_t shader_value = 0;                              ///< Count, offset or size on the shader side.
    std::uint32_t layout_value = 0;                              ///< Count, offset or size on the layout side.
    std::uint32_t shader_extra = 0;                              ///< Second value on the shader side (range end).
    std::uint32_t layout_extra = 0;                              ///< Second value on the layout side (range end).
    ShaderStage shader_stage = ShaderStage::NONE;                ///< Stage of the shader.
    ShaderStage layout_stages = ShaderStage::NONE;               ///< Stages the layout allows.

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
[[nodiscard]] auto
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
[[nodiscard]] auto
check_pipeline_layout(const ShaderLayout& shader,
                      const PipelineLayoutDefinition& layout) noexcept -> LayoutCheck
{
    // Every binding of the shader needs a compatible, visible binding in the layout.
    for (const ShaderBinding& binding : shader.bindings)
    {
        const LayoutCheck where{.set = binding.set, .index = binding.binding, .shader_type = binding.type};
        const LayoutBinding* provided = layout.find(binding.set, binding.binding);
        if (provided == nullptr)
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

    // The push-constant bytes the shader reads must lie inside the range visible to its stage.
    if (shader.push_constant_size > 0)
    {
        const PushConstantRange* range = layout.push_constant_range(shader.stage);
        if (range == nullptr)
        {
            ShaderStage visible = ShaderStage::NONE;
            for (const PushConstantRange& other : layout.push_constant_ranges)
            {
                visible |= other.stages;
            }
            return {.mismatch = LayoutMismatch::PUSH_CONSTANT_STAGE_NOT_VISIBLE, .shader_stage = shader.stage, .layout_stages = visible};
        }
        const std::uint32_t shader_end = shader.push_constant_offset + shader.push_constant_size;
        if (shader.push_constant_offset < range->offset || shader_end > range->offset + range->size)
        {
            return {.mismatch = LayoutMismatch::PUSH_CONSTANTS_OUTSIDE_RANGE,
                    .shader_value = shader.push_constant_offset,
                    .layout_value = range->offset,
                    .shader_extra = shader_end,
                    .layout_extra = range->offset + range->size,
                    .shader_stage = shader.stage,
                    .layout_stages = range->stages};
        }
    }
    return {};
}

/// @brief Checks that every input location of a vertex shader is fed by an attribute of a matching
///        numeric type (float, signed or unsigned integer; component counts may differ).
[[nodiscard]] auto
check_vertex_input(const ShaderLayout& vertex_shader,
                   std::span<const VertexInputAttribute> attributes) noexcept -> LayoutCheck
{
    for (const StageVariable& input : vertex_shader.inputs)
    {
        for (std::uint32_t location = input.location; location < input.location + input.location_count; ++location)
        {
            const auto attribute = std::ranges::find(attributes, location, &VertexInputAttribute::location);
            if (attribute == attributes.end())
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
[[nodiscard]] auto
check_fragment_output(const ShaderLayout& fragment_shader,
                      std::span<const Format> color_formats) noexcept -> LayoutCheck
{
    for (const StageVariable& output : fragment_shader.outputs)
    {
        for (std::uint32_t location = output.location; location < output.location + output.location_count; ++location)
        {
            if (location >= color_formats.size())
            {
                return {.mismatch = LayoutMismatch::FRAGMENT_OUTPUT_WITHOUT_ATTACHMENT,
                        .index = location,
                        .shader_format = output.format,
                        .layout_value = static_cast<std::uint32_t>(color_formats.size())};
            }
            const Format attachment = color_formats[location];
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
        case LayoutMismatch::PUSH_CONSTANTS_OUTSIDE_RANGE:
            return std::format("the shader reads push-constant bytes [{}, {}) but the pipeline layout's range for {} is [{}, {})",
                               check.shader_value, check.shader_extra, check.layout_stages, check.layout_value,
                               check.layout_extra);
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
            return std::format("set {} binding {} is declared as {} x{} by this shader and as {} x{} by an earlier one",
                               check.set, check.index, check.shader_type, check.shader_value, check.layout_type,
                               check.layout_value);
    }
    return std::format("unknown mismatch {}", check.mismatch);
}

/// @brief Does nothing for a passed check; throws ShaderLayoutMismatchError for a failed one.
/// @param check     Result of a check_* function.
/// @param shader    Module the check was about (its name and backend appear in the message).
/// @param operation Operation that combines the shader.
void
validate_layout_check(const LayoutCheck& check,
                      const ShaderModule& shader,
                      std::string_view operation)
{
    if (check.ok())
    {
        return;
    }
    throw ShaderLayoutMismatchError({
        .backend = shader.backend(),
        .operation = std::string{operation},
        .shader_name = std::string{shader.debug_name()},
        .mismatch = check.mismatch,
        .problem = describe(check),
        .suggestion = "make the shader and the layout it is combined with agree (the layout is reflected from the "
                      "shader's SPIR-V)",
    });
}

/// @brief Checks a compute pipeline before a backend creates it.
/// @throws ShaderLayoutMismatchError when the shader is not a compute shader or does not match @p layout.
void
validate_compute_pipeline(const PipelineLayout& layout,
                          const ShaderModule& shader)
{
    constexpr std::string_view OPERATION = "Device::create_compute_pipeline";
    validate_layout_check(check_shader_stage(shader.layout(), ShaderStage::COMPUTE), shader, OPERATION);
    validate_layout_check(check_pipeline_layout(shader.layout(), layout.definition()), shader, OPERATION);
}

/// @brief Checks a graphics pipeline before a backend creates it: stages, pipeline layout, vertex
///        attributes and color attachments.
/// @param layout          Pipeline layout named by @p description.
/// @param vertex_shader   Vertex shader named by @p description.
/// @param fragment_shader Fragment shader named by @p description, or nullptr for a depth-only pipeline.
/// @param description     The rest of the pipeline description.
/// @throws ShaderLayoutMismatchError naming the first mismatch.
void
validate_graphics_pipeline(const PipelineLayout& layout,
                           const ShaderModule& vertex_shader,
                           const ShaderModule* fragment_shader,
                           const GraphicsPipelineDescription& description)
{
    constexpr std::string_view OPERATION = "Device::create_graphics_pipeline";

    // The vertex shader: stage, bindings and push constants, attributes.
    validate_layout_check(check_shader_stage(vertex_shader.layout(), ShaderStage::VERTEX), vertex_shader, OPERATION);
    validate_layout_check(check_pipeline_layout(vertex_shader.layout(), layout.definition()), vertex_shader, OPERATION);
    validate_layout_check(check_vertex_input(vertex_shader.layout(), description.vertex_attributes), vertex_shader, OPERATION);

    // The fragment shader, if any: stage, bindings and push constants, attachments.
    if (fragment_shader != nullptr)
    {
        validate_layout_check(check_shader_stage(fragment_shader->layout(), ShaderStage::FRAGMENT), *fragment_shader, OPERATION);
        validate_layout_check(check_pipeline_layout(fragment_shader->layout(), layout.definition()), *fragment_shader, OPERATION);
        validate_layout_check(check_fragment_output(fragment_shader->layout(), description.color_formats), *fragment_shader,
                              OPERATION);
    }
}

/// @brief The pipeline layout that exactly provides what @p shaders use: their bindings (visible to the
///        stages that use them, named as the first shader names them) and one push-constant range
///        covering every shader's push constants, visible to each of them. Runtime-sized (bindless)
///        arrays get one descriptor: create bindless layouts explicitly with their real size and options.
/// @throws ShaderLayoutMismatchError (CONFLICTING_BINDINGS) when two shaders declare one (set, binding)
///         with different types or counts.
[[nodiscard]] auto
pipeline_layout_definition_from(std::span<const ShaderModule* const> shaders) -> PipelineLayoutDefinition
{
    PipelineLayoutDefinition layout;
    PushConstantRange push_constants{.stages = ShaderStage::NONE, .offset = std::numeric_limits<std::uint32_t>::max(), .size = 0};
    std::uint32_t push_constant_end = 0;
    for (const ShaderModule* shader : shaders)
    {
        const ShaderLayout& reflected = shader->layout();

        // Bindings: add new ones, widen the visibility of shared ones.
        for (const ShaderBinding& binding : reflected.bindings)
        {
            const std::uint32_t count = std::max(binding.count, 1U);
            const auto existing = std::ranges::find_if(layout.bindings, [&binding](const LayoutBinding& candidate)
            {
                return candidate.set == binding.set && candidate.binding == binding.binding;
            });
            if (existing == layout.bindings.end())
            {
                layout.bindings.push_back({.set = binding.set,
                                           .binding = binding.binding,
                                           .type = binding.type,
                                           .count = count,
                                           .stages = reflected.stage,
                                           .options = DescriptorBindingOption::NONE,
                                           .name = binding.name});
                continue;
            }
            if (existing->type != binding.type || existing->count != count)
            {
                validate_layout_check({.mismatch = LayoutMismatch::CONFLICTING_BINDINGS,
                                       .set = binding.set,
                                       .index = binding.binding,
                                       .shader_type = binding.type,
                                       .layout_type = existing->type,
                                       .shader_value = count,
                                       .layout_value = existing->count},
                                      *shader, "Device::create_pipeline_layout_from");
            }
            existing->stages |= reflected.stage;
        }

        // Push constants: one range covering every shader's bytes.
        if (reflected.push_constant_size > 0)
        {
            push_constants.stages |= reflected.stage;
            push_constants.offset = std::min(push_constants.offset, reflected.push_constant_offset);
            push_constant_end = std::max(push_constant_end, reflected.push_constant_offset + reflected.push_constant_size);
        }
    }
    if (push_constants.stages != ShaderStage::NONE)
    {
        push_constants.size = push_constant_end - push_constants.offset;
        layout.push_constant_ranges.push_back(push_constants);
    }
    return layout;
}

} // namespace gfx
