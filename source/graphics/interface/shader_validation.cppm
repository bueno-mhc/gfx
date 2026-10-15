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
import :compute_pipeline;
import :ray_tracing_pipeline;

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

/// @brief Checks that every input location of a stage is written by the previous stage with the same
///        numeric type (float, signed or unsigned integer).
[[nodiscard]] auto
check_stage_interface(const ShaderLayout& previous_stage,
                      const ShaderLayout& next_stage) noexcept -> LayoutCheck
{
    for (const StageVariable& input : next_stage.inputs)
    {
        const auto output = std::ranges::find(previous_stage.outputs, input.location, &StageVariable::location);
        if (output == previous_stage.outputs.end() ||
            shader_numeric_type(output->format) != shader_numeric_type(input.format))
        {
            return {.mismatch = LayoutMismatch::STAGE_INTERFACE_MISMATCH,
                    .index = input.location,
                    .shader_format = input.format,
                    .layout_format = output == previous_stage.outputs.end() ? Format::UNDEFINED : output->format,
                    .shader_stage = next_stage.stage,
                    .layout_stages = previous_stage.stage};
        }
    }
    return {};
}

/// @brief Checks that every specialization constant a pipeline sets is declared by one of its shaders.
[[nodiscard]] auto
check_specialization_constants(std::span<const ShaderLayout* const> shaders,
                               std::span<const SpecializationConstant> constants) noexcept -> LayoutCheck
{
    for (const SpecializationConstant& constant : constants)
    {
        const bool declared = std::ranges::any_of(shaders, [&constant](const ShaderLayout* shader)
        {
            return shader != nullptr &&
                   std::ranges::contains(shader->specialization_constants, constant.id, &SpecializationConstantInfo::id);
        });
        if (!declared)
        {
            return {.mismatch = LayoutMismatch::UNKNOWN_SPECIALIZATION_CONSTANT, .index = constant.id};
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
        case LayoutMismatch::STAGE_INTERFACE_MISMATCH:
            return std::format("the {} stage reads location {} as {}, but the {} stage writes {} there", check.shader_stage,
                               check.index, check.shader_format, check.layout_stages,
                               check.layout_format == Format::UNDEFINED ? std::string{"nothing"}
                                                                        : std::format("{}", check.layout_format));
        case LayoutMismatch::UNKNOWN_SPECIALIZATION_CONSTANT:
            return std::format("the pipeline sets specialization constant {}, which none of its shaders declares", check.index);
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

/// @brief Checks a compute pipeline before a backend creates it (with the bindless pipeline layout when
///        the description names none). Backends call it at ValidationLevel::CREATION.
/// @throws ShaderLayoutMismatchError when the shader is not a compute shader, does not match @p layout,
///         or does not declare a specialization constant the description sets.
void
validate_compute_pipeline(const PipelineLayout& layout,
                          const ShaderModule& shader,
                          std::span<const SpecializationConstant> specialization_constants = {})
{
    static constexpr std::string_view OPERATION = "Device::create_compute_pipeline";
    validate_layout_check(check_shader_stage(shader.layout(), ShaderStage::COMPUTE), shader, OPERATION);
    validate_layout_check(check_pipeline_layout(shader.layout(), layout.definition()), shader, OPERATION);
    const std::array<const ShaderLayout*, 1> layouts{&shader.layout()};
    validate_layout_check(check_specialization_constants(layouts, specialization_constants), shader, OPERATION);
}

/// @brief The shader modules a GraphicsPipelineDescription names, resolved by the backend (nullptr for
///        the stages it leaves empty).
struct GraphicsPipelineShaders
{
    const ShaderModule* vertex = nullptr;                  ///< Vertex shader.
    const ShaderModule* fragment = nullptr;                ///< Fragment shader.
    const ShaderModule* task = nullptr;                    ///< Task shader.
    const ShaderModule* mesh = nullptr;                    ///< Mesh shader.
    const ShaderModule* geometry = nullptr;                ///< Geometry shader.
    const ShaderModule* tessellation_control = nullptr;    ///< Tessellation control shader.
    const ShaderModule* tessellation_evaluation = nullptr; ///< Tessellation evaluation shader.
};

/// @brief Checks a graphics pipeline before a backend creates it: the combination of stages, each
///        stage against the pipeline layout, vertex attributes, the interface between the last
///        pre-rasterization stage and the fragment shader, color attachments and specialization
///        constants. Backends call it at ValidationLevel::CREATION.
/// @param layout      Pipeline layout named by @p description (the bindless layout when it names none).
/// @param shaders     Shader modules named by @p description.
/// @param description The rest of the pipeline description.
/// @throws InvalidUsageError when the stages do not form a pipeline (no vertex or mesh shader, both,
///         a task shader without a mesh shader, half a tessellation pair).
/// @throws ShaderLayoutMismatchError naming the first mismatch.
void
validate_graphics_pipeline(const PipelineLayout& layout,
                           const GraphicsPipelineShaders& shaders,
                           const GraphicsPipelineDescription& description)
{
    static constexpr std::string_view OPERATION = "Device::create_graphics_pipeline";
    const auto reject = [&layout](std::string problem, std::string suggestion)
    {
        throw InvalidUsageError({.backend = layout.backend(),
                                 .operation = std::string{OPERATION},
                                 .problem = std::move(problem),
                                 .suggestion = std::move(suggestion)});
    };

    // The stages must form a pipeline: vertex or mesh processing, never both.
    if ((shaders.vertex == nullptr) == (shaders.mesh == nullptr))
    {
        reject(shaders.vertex == nullptr ? "the pipeline has neither a vertex nor a mesh shader"
                                         : "the pipeline has both a vertex and a mesh shader",
               "use a vertex shader (with optional tessellation and geometry shaders) or a mesh shader (with an optional "
               "task shader)");
    }
    if (shaders.task != nullptr && shaders.mesh == nullptr)
    {
        reject("the pipeline has a task shader without a mesh shader", "add the mesh shader the task shader launches");
    }
    if ((shaders.tessellation_control == nullptr) != (shaders.tessellation_evaluation == nullptr))
    {
        reject("the pipeline has only one of the two tessellation shaders", "use both tessellation shaders, or neither");
    }
    if (shaders.mesh != nullptr && (shaders.geometry != nullptr || shaders.tessellation_evaluation != nullptr))
    {
        reject("a mesh pipeline cannot have geometry or tessellation shaders", "remove them, or use a vertex shader");
    }

    // Every stage: its kind and its bindings and push constants.
    const std::array<std::pair<const ShaderModule*, ShaderStage>, 7> stages{{
        {shaders.vertex, ShaderStage::VERTEX},
        {shaders.tessellation_control, ShaderStage::TESSELLATION_CONTROL},
        {shaders.tessellation_evaluation, ShaderStage::TESSELLATION_EVALUATION},
        {shaders.geometry, ShaderStage::GEOMETRY},
        {shaders.task, ShaderStage::TASK},
        {shaders.mesh, ShaderStage::MESH},
        {shaders.fragment, ShaderStage::FRAGMENT},
    }};
    std::vector<const ShaderLayout*> layouts;
    for (const auto& [shader, stage] : stages)
    {
        if (shader != nullptr)
        {
            validate_layout_check(check_shader_stage(shader->layout(), stage), *shader, OPERATION);
            validate_layout_check(check_pipeline_layout(shader->layout(), layout.definition()), *shader, OPERATION);
            layouts.push_back(&shader->layout());
        }
    }

    // Vertex input, then what the last pre-rasterization stage hands to the fragment shader.
    if (shaders.vertex != nullptr)
    {
        validate_layout_check(check_vertex_input(shaders.vertex->layout(), description.vertex_attributes), *shaders.vertex,
                              OPERATION);
    }
    const ShaderModule* last = shaders.geometry != nullptr                  ? shaders.geometry
                               : shaders.tessellation_evaluation != nullptr ? shaders.tessellation_evaluation
                                                                            : shaders.vertex;
    if (shaders.fragment != nullptr)
    {
        if (last != nullptr)
        {
            validate_layout_check(check_stage_interface(last->layout(), shaders.fragment->layout()), *shaders.fragment, OPERATION);
        }
        validate_layout_check(check_fragment_output(shaders.fragment->layout(), description.color_formats), *shaders.fragment,
                              OPERATION);
    }

    // Specialization constants must exist in some stage.
    const ShaderModule& any_shader = shaders.vertex != nullptr ? *shaders.vertex : *shaders.mesh;
    validate_layout_check(check_specialization_constants(layouts, description.specialization_constants), any_shader, OPERATION);
}

/// @brief Checks a ray tracing pipeline before a backend creates it: group composition, shader stages,
///        the pipeline layout and specialization constants. Backends call it at ValidationLevel::CREATION.
/// @param layout      Pipeline layout named by @p description (the bindless layout when it names none).
/// @param description The pipeline description.
/// @param resolve     The backend's module for a handle (nullptr for a null handle).
/// @throws InvalidUsageError when a group is malformed or no group generates rays.
/// @throws ShaderLayoutMismatchError naming the first mismatch.
void
validate_ray_tracing_pipeline(const PipelineLayout& layout,
                              const RayTracingPipelineDescription& description,
                              const std::function<const ShaderModule*(ShaderModuleHandle)>& resolve)
{
    static constexpr std::string_view OPERATION = "Device::create_ray_tracing_pipeline";
    const auto reject = [&layout](std::size_t group, std::string problem)
    {
        throw InvalidUsageError({.backend = layout.backend(),
                                 .operation = std::string{OPERATION},
                                 .problem = std::format("group {}: {}", group, problem),
                                 .suggestion = "GENERAL groups hold one ray generation, miss or callable shader; hit groups "
                                               "hold closest-hit, any-hit and (procedural) intersection shaders"});
    };

    // Each present shader: its stage must be one of @p allowed, and it must match the layout.
    std::vector<const ShaderLayout*> layouts;
    bool generates_rays = false;
    const auto check = [&](std::size_t group, ShaderModuleHandle handle, ShaderStage allowed, std::string_view role)
    {
        const ShaderModule* shader = resolve(handle);
        if (shader == nullptr)
        {
            return;
        }
        if (!has_any(allowed, shader->layout().stage))
        {
            reject(group, std::format("its {} shader '{}' is a {} shader", role, shader->debug_name(), shader->layout().stage));
        }
        validate_layout_check(check_pipeline_layout(shader->layout(), layout.definition()), *shader, OPERATION);
        layouts.push_back(&shader->layout());
        generates_rays = generates_rays || shader->layout().stage == ShaderStage::RAY_GENERATION;
    };

    for (std::size_t index = 0; index < description.groups.size(); ++index)
    {
        const RayTracingShaderGroup& group = description.groups[index];
        switch (group.type)
        {
            case RayTracingShaderGroupType::GENERAL:
                if (!group.general.is_valid() || group.closest_hit.is_valid() || group.any_hit.is_valid() ||
                    group.intersection.is_valid())
                {
                    reject(index, "a GENERAL group holds exactly one shader, in `general`");
                }
                check(index, group.general, ShaderStage::RAY_GENERATION | ShaderStage::MISS | ShaderStage::CALLABLE, "general");
                break;
            case RayTracingShaderGroupType::TRIANGLES_HIT:
            case RayTracingShaderGroupType::PROCEDURAL_HIT:
            {
                const bool procedural = group.type == RayTracingShaderGroupType::PROCEDURAL_HIT;
                if (group.general.is_valid() || procedural != group.intersection.is_valid())
                {
                    reject(index, procedural ? "a PROCEDURAL_HIT group needs an intersection shader and no general shader"
                                             : "a TRIANGLES_HIT group has no general or intersection shader");
                }
                check(index, group.closest_hit, ShaderStage::CLOSEST_HIT, "closest-hit");
                check(index, group.any_hit, ShaderStage::ANY_HIT, "any-hit");
                check(index, group.intersection, ShaderStage::INTERSECTION, "intersection");
                break;
            }
        }
    }
    if (!generates_rays)
    {
        reject(description.groups.size(), "no group holds a ray generation shader");
    }

    // Specialization constants must exist in some shader.
    if (!description.specialization_constants.empty())
    {
        const LayoutCheck constants = check_specialization_constants(layouts, description.specialization_constants);
        if (!constants.ok())
        {
            throw ShaderLayoutMismatchError({.backend = layout.backend(),
                                             .operation = std::string{OPERATION},
                                             .shader_name = std::string{description.debug_name},
                                             .mismatch = constants.mismatch,
                                             .problem = describe(constants),
                                             .suggestion = "declare the constant in a shader, or stop setting it"});
        }
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
