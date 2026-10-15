/// @file spirv_reflection.cpp
/// @brief reflect_spirv(): the interface of a SPIR-V entry point, read with the SPIRV-Cross C API.
module;

#include <spirv_cross/spirv_cross_c.h>

module gfx.shader_tools;

import std;
import gfx.enums;
import gfx.errors;
import gfx.interface;

namespace gfx
{

namespace
{

/// @brief Stage of a SPIR-V execution model (NONE for models gfx does not run).
auto
stage_of(SpvExecutionModel model) noexcept -> ShaderStage
{
    switch (model)
    {
        case SpvExecutionModelVertex:                   return ShaderStage::VERTEX;
        case SpvExecutionModelTessellationControl:      return ShaderStage::TESSELLATION_CONTROL;
        case SpvExecutionModelTessellationEvaluation:   return ShaderStage::TESSELLATION_EVALUATION;
        case SpvExecutionModelGeometry:                 return ShaderStage::GEOMETRY;
        case SpvExecutionModelFragment:                 return ShaderStage::FRAGMENT;
        case SpvExecutionModelGLCompute:                return ShaderStage::COMPUTE;
        case SpvExecutionModelTaskEXT:                  return ShaderStage::TASK;
        case SpvExecutionModelMeshEXT:                  return ShaderStage::MESH;
        case SpvExecutionModelRayGenerationKHR:         return ShaderStage::RAY_GENERATION;
        case SpvExecutionModelIntersectionKHR:          return ShaderStage::INTERSECTION;
        case SpvExecutionModelAnyHitKHR:                return ShaderStage::ANY_HIT;
        case SpvExecutionModelClosestHitKHR:            return ShaderStage::CLOSEST_HIT;
        case SpvExecutionModelMissKHR:                  return ShaderStage::MISS;
        case SpvExecutionModelCallableKHR:              return ShaderStage::CALLABLE;
        default:                                        return ShaderStage::NONE;
    }
}

/// @brief Format of a storage image's format qualifier (UNDEFINED for Unknown and 64-bit formats).
auto
format_of(SpvImageFormat format) noexcept -> Format
{
    switch (format)
    {
        case SpvImageFormatRgba32f:         return Format::R32G32B32A32_SFLOAT;
        case SpvImageFormatRgba16f:         return Format::R16G16B16A16_SFLOAT;
        case SpvImageFormatR32f:            return Format::R32_SFLOAT;
        case SpvImageFormatRgba8:           return Format::R8G8B8A8_UNORM;
        case SpvImageFormatRgba8Snorm:      return Format::R8G8B8A8_SNORM;
        case SpvImageFormatRg32f:           return Format::R32G32_SFLOAT;
        case SpvImageFormatRg16f:           return Format::R16G16_SFLOAT;
        case SpvImageFormatR11fG11fB10f:    return Format::B10G11R11_UFLOAT_PACK32;
        case SpvImageFormatR16f:            return Format::R16_SFLOAT;
        case SpvImageFormatRgba16:          return Format::R16G16B16A16_UNORM;
        case SpvImageFormatRgb10A2:         return Format::A2B10G10R10_UNORM_PACK32;
        case SpvImageFormatRg16:            return Format::R16G16_UNORM;
        case SpvImageFormatRg8:             return Format::R8G8_UNORM;
        case SpvImageFormatR16:             return Format::R16_UNORM;
        case SpvImageFormatR8:              return Format::R8_UNORM;
        case SpvImageFormatRgba16Snorm:     return Format::R16G16B16A16_SNORM;
        case SpvImageFormatRg16Snorm:       return Format::R16G16_SNORM;
        case SpvImageFormatRg8Snorm:        return Format::R8G8_SNORM;
        case SpvImageFormatR16Snorm:        return Format::R16_SNORM;
        case SpvImageFormatR8Snorm:         return Format::R8_SNORM;
        case SpvImageFormatRgba32i:         return Format::R32G32B32A32_SINT;
        case SpvImageFormatRgba16i:         return Format::R16G16B16A16_SINT;
        case SpvImageFormatRgba8i:          return Format::R8G8B8A8_SINT;
        case SpvImageFormatR32i:            return Format::R32_SINT;
        case SpvImageFormatRg32i:           return Format::R32G32_SINT;
        case SpvImageFormatRg16i:           return Format::R16G16_SINT;
        case SpvImageFormatRg8i:            return Format::R8G8_SINT;
        case SpvImageFormatR16i:            return Format::R16_SINT;
        case SpvImageFormatR8i:             return Format::R8_SINT;
        case SpvImageFormatRgba32ui:        return Format::R32G32B32A32_UINT;
        case SpvImageFormatRgba16ui:        return Format::R16G16B16A16_UINT;
        case SpvImageFormatRgba8ui:         return Format::R8G8B8A8_UINT;
        case SpvImageFormatR32ui:           return Format::R32_UINT;
        case SpvImageFormatRgb10a2ui:       return Format::A2B10G10R10_UINT_PACK32;
        case SpvImageFormatRg32ui:          return Format::R32G32_UINT;
        case SpvImageFormatRg16ui:          return Format::R16G16_UINT;
        case SpvImageFormatRg8ui:           return Format::R8G8_UINT;
        case SpvImageFormatR16ui:           return Format::R16_UINT;
        case SpvImageFormatR8ui:            return Format::R8_UINT;
        default:                            return Format::UNDEFINED;
    }
}

/// @brief Format of one location of a stage variable with @p components components of a scalar type.
///        Types without a format of their own map to the closest one with the same numeric type
///        (3-component 8- and 16-bit vectors to 4 components, 64-bit scalars to 32-bit), which is what
///        pipeline validation compares.
auto
format_of(spvc_basetype base_type,
          std::uint32_t components) noexcept -> Format
{
    // Formats by component count (1 to 4) for each scalar type.
    using Row = std::array<Format, 4>;
    static constexpr Row FLOAT32{Format::R32_SFLOAT, Format::R32G32_SFLOAT, Format::R32G32B32_SFLOAT, Format::R32G32B32A32_SFLOAT};
    static constexpr Row INT32{Format::R32_SINT, Format::R32G32_SINT, Format::R32G32B32_SINT, Format::R32G32B32A32_SINT};
    static constexpr Row UINT32{Format::R32_UINT, Format::R32G32_UINT, Format::R32G32B32_UINT, Format::R32G32B32A32_UINT};
    static constexpr Row FLOAT16{Format::R16_SFLOAT, Format::R16G16_SFLOAT, Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT};
    static constexpr Row INT16{Format::R16_SINT, Format::R16G16_SINT, Format::R16G16B16A16_SINT, Format::R16G16B16A16_SINT};
    static constexpr Row UINT16{Format::R16_UINT, Format::R16G16_UINT, Format::R16G16B16A16_UINT, Format::R16G16B16A16_UINT};
    static constexpr Row INT8{Format::R8_SINT, Format::R8G8_SINT, Format::R8G8B8A8_SINT, Format::R8G8B8A8_SINT};
    static constexpr Row UINT8{Format::R8_UINT, Format::R8G8_UINT, Format::R8G8B8A8_UINT, Format::R8G8B8A8_UINT};

    const std::size_t column = std::clamp<std::size_t>(components, 1, 4) - 1;
    switch (base_type)
    {
        case SPVC_BASETYPE_FP32:
        case SPVC_BASETYPE_FP64: 
            return FLOAT32[column];
        case SPVC_BASETYPE_INT32:
        case SPVC_BASETYPE_INT64: 
            return INT32[column];
        case SPVC_BASETYPE_UINT32:
        case SPVC_BASETYPE_UINT64:
        case SPVC_BASETYPE_BOOLEAN: 
            return UINT32[column];
        case SPVC_BASETYPE_FP16: 
            return FLOAT16[column];
        case SPVC_BASETYPE_INT16: 
            return INT16[column];
        case SPVC_BASETYPE_UINT16: 
            return UINT16[column];
        case SPVC_BASETYPE_INT8: 
            return INT8[column];
        case SPVC_BASETYPE_UINT8: 
            return UINT8[column];
        default: 
            return Format::UNDEFINED;
    }
}

/// @brief Owns a SPIRV-Cross context and collects its error messages.
class Reflector
{
public:
    Reflector(std::span<const std::uint32_t> spirv, std::string_view shader_name)
        : m_shader_name{shader_name}
    {
        // A context whose errors are collected for the exception.
        if (spvc_context_create(&m_context) != SPVC_SUCCESS)
        {
            reject("SPIRV-Cross could not create a context (out of memory)", "");
        }
        spvc_context_set_error_callback(m_context, &Reflector::collect_error, this);

        // Parse the module, then reflect without a target language.
        spvc_parsed_ir ir = nullptr;
        if (spvc_context_parse_spirv(m_context, spirv.data(), spirv.size(), &ir) != SPVC_SUCCESS ||
            spvc_context_create_compiler(m_context, SPVC_BACKEND_NONE, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &m_compiler) !=
                SPVC_SUCCESS)
        {
            reject(std::format("the SPIR-V could not be parsed: {}", m_errors),
                   "pass the complete words of a .spv file produced by glslang, DXC or gfx_add_shaders()");
        }
    }

    ~Reflector()
    {
        spvc_context_destroy(m_context);
    }

    Reflector(const Reflector&) = delete;
    Reflector(Reflector&&) = delete;
    auto operator=(const Reflector&) -> Reflector& = delete;
    auto operator=(Reflector&&) -> Reflector& = delete;

    /// @brief The interface of entry point @p entry_point of stage @p stage (NONE: any stage).
    [[nodiscard]] auto reflect(ShaderStage stage, std::string_view entry_point) -> ShaderLayout
    {
        ShaderLayout layout;
        layout.stage = select_entry_point(stage, entry_point);
        reflect_bindings(layout);
        reflect_stage_variables(layout);
        reflect_workgroup_size(layout);
        reflect_specialization_constants(layout);
        return layout;
    }

private:
    static void collect_error(void* user_data, const char* message)
    {
        std::string& errors = static_cast<Reflector*>(user_data)->m_errors;
        errors += errors.empty() ? message : std::format("; {}", message);
    }

    [[noreturn]] void reject(std::string problem, std::string suggestion) const
    {
        throw InvalidUsageError({
            .backend = std::nullopt,
            .operation = "reflect_spirv",
            .problem = std::format("shader '{}': {}", m_shader_name, problem),
            .suggestion = std::move(suggestion),
        });
    }

    // Makes the entry point named @p entry_point current and returns its stage.
    auto select_entry_point(ShaderStage stage, std::string_view entry_point) -> ShaderStage
    {
        const spvc_entry_point* entry_points = nullptr;
        std::size_t count = 0;
        spvc_compiler_get_entry_points(m_compiler, &entry_points, &count);

        // The entry point with that name (and stage, when one is expected).
        std::string available;
        for (std::size_t index = 0; index < count; ++index)
        {
            const ShaderStage candidate = stage_of(entry_points[index].execution_model);
            available += std::format("{}'{}' ({})", available.empty() ? "" : ", ", entry_points[index].name,
                                     candidate == ShaderStage::NONE ? std::string{"unsupported stage"}
                                                                    : std::format("{}", candidate));
            if (entry_point == entry_points[index].name && candidate != ShaderStage::NONE &&
                (stage == ShaderStage::NONE || stage == candidate))
            {
                spvc_compiler_set_entry_point(m_compiler, entry_points[index].name, entry_points[index].execution_model);
                return candidate;
            }
        }
        reject(stage == ShaderStage::NONE ? std::format("it has no entry point named '{}'", entry_point)
                                          : std::format("it has no {} entry point named '{}'", stage, entry_point),
               available.empty() ? std::string{"the module declares no entry point"}
                                 : std::format("its entry points are {}", available));
    }

    // Product of the array dimensions of @p type (0 for a runtime-sized array), skipping the
    // outermost @p skipped_outer dimensions.
    [[nodiscard]] auto element_count(spvc_type type, std::uint32_t skipped_outer = 0) const -> std::uint32_t
    {
        const std::uint32_t dimensions = spvc_type_get_num_array_dimensions(type);
        std::uint32_t count = 1;
        for (std::uint32_t dimension = 0; dimension + skipped_outer < dimensions; ++dimension)
        {
            std::uint32_t size = spvc_type_get_array_dimension(type, dimension);
            if (!spvc_type_array_dimension_is_literal(type, dimension))
            {
                // Sized by a specialization constant: its default value.
                size = spvc_constant_get_scalar_u32(spvc_compiler_get_constant_handle(m_compiler, size), 0, 0);
            }
            count *= size;
        }
        return count;
    }

    // Descriptor bindings and push constants of the entry point.
    void reflect_bindings(ShaderLayout& layout)
    {
        spvc_set active = nullptr;
        spvc_resources resources = nullptr;
        if (spvc_compiler_get_active_interface_variables(m_compiler, &active) != SPVC_SUCCESS ||
            spvc_compiler_create_shader_resources_for_active_variables(m_compiler, &resources, active) != SPVC_SUCCESS)
        {
            reject(std::format("its resources could not be reflected: {}", m_errors), "");
        }

        // Every kind of descriptor.
        static constexpr std::array<spvc_resource_type, 8> DESCRIPTOR_RESOURCES{
            SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, SPVC_RESOURCE_TYPE_STORAGE_BUFFER,    SPVC_RESOURCE_TYPE_STORAGE_IMAGE,
            SPVC_RESOURCE_TYPE_SAMPLED_IMAGE,  SPVC_RESOURCE_TYPE_SEPARATE_IMAGE,    SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS,
            SPVC_RESOURCE_TYPE_SUBPASS_INPUT,  SPVC_RESOURCE_TYPE_ACCELERATION_STRUCTURE};
        for (const spvc_resource_type resource_type : DESCRIPTOR_RESOURCES)
        {
            for (const spvc_reflected_resource& resource : resource_list(resources, resource_type))
            {
                add_binding(layout, resource_type, resource);
            }
        }

        // Push constants: the byte range the entry point reads.
        for (const spvc_reflected_resource& resource : resource_list(resources, SPVC_RESOURCE_TYPE_PUSH_CONSTANT))
        {
            const spvc_buffer_range* ranges = nullptr;
            std::size_t count = 0;
            spvc_compiler_get_active_buffer_ranges(m_compiler, resource.id, &ranges, &count);
            std::size_t begin = std::numeric_limits<std::size_t>::max();
            std::size_t end = 0;
            for (std::size_t index = 0; index < count; ++index)
            {
                begin = std::min(begin, ranges[index].offset);
                end = std::max(end, ranges[index].offset + ranges[index].range);
            }
            if (count > 0)
            {
                layout.push_constant_offset = static_cast<std::uint32_t>(begin);
                layout.push_constant_size = static_cast<std::uint32_t>(end - begin);
            }
        }

        // One entry per (set, binding, type), in binding order.
        std::ranges::sort(layout.bindings, {}, [](const ShaderBinding& binding)
        {
            return std::tuple{binding.set, binding.binding, binding.type};
        });
    }

    [[nodiscard]] auto resource_list(spvc_resources resources, spvc_resource_type type) const
        -> std::span<const spvc_reflected_resource>
    {
        const spvc_reflected_resource* list = nullptr;
        std::size_t count = 0;
        spvc_resources_get_resource_list_for_type(resources, type, &list, &count);
        return {list, count};
    }

    void add_binding(ShaderLayout& layout, spvc_resource_type resource_type, const spvc_reflected_resource& resource)
    {
        const spvc_type type = spvc_compiler_get_type_handle(m_compiler, resource.type_id);
        const spvc_type base_type = spvc_compiler_get_type_handle(m_compiler, resource.base_type_id);
        const bool texel_buffer = (resource_type == SPVC_RESOURCE_TYPE_STORAGE_IMAGE ||
                                   resource_type == SPVC_RESOURCE_TYPE_SAMPLED_IMAGE ||
                                   resource_type == SPVC_RESOURCE_TYPE_SEPARATE_IMAGE) &&
                                  spvc_type_get_image_dimension(base_type) == SpvDimBuffer;

        // The descriptor type.
        ShaderBinding binding;
        switch (resource_type)
        {
            case SPVC_RESOURCE_TYPE_UNIFORM_BUFFER: 
                binding.type = DescriptorType::UNIFORM_BUFFER; 
                break;
            case SPVC_RESOURCE_TYPE_STORAGE_BUFFER: 
                binding.type = DescriptorType::STORAGE_BUFFER; 
                break;
            case SPVC_RESOURCE_TYPE_STORAGE_IMAGE:
                binding.type = texel_buffer ? DescriptorType::STORAGE_TEXEL_BUFFER : DescriptorType::STORAGE_IMAGE;
                binding.image_format = format_of(spvc_type_get_image_storage_format(base_type));
                break;
            case SPVC_RESOURCE_TYPE_SAMPLED_IMAGE:
                binding.type = texel_buffer ? DescriptorType::UNIFORM_TEXEL_BUFFER : DescriptorType::COMBINED_IMAGE_SAMPLER;
                break;
            case SPVC_RESOURCE_TYPE_SEPARATE_IMAGE:
                binding.type = texel_buffer ? DescriptorType::UNIFORM_TEXEL_BUFFER : DescriptorType::SAMPLED_IMAGE;
                break;
            case SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS: 
                binding.type = DescriptorType::SAMPLER; 
                break;
            case SPVC_RESOURCE_TYPE_SUBPASS_INPUT: 
                binding.type = DescriptorType::INPUT_ATTACHMENT; 
                break;
            default: 
                binding.type = DescriptorType::ACCELERATION_STRUCTURE; 
                break;
        }

        // Where it is bound, how many, and how large a block is.
        binding.set = spvc_compiler_get_decoration(m_compiler, resource.id, SpvDecorationDescriptorSet);
        binding.binding = spvc_compiler_get_decoration(m_compiler, resource.id, SpvDecorationBinding);
        binding.count = element_count(type);
        if (resource_type == SPVC_RESOURCE_TYPE_UNIFORM_BUFFER || resource_type == SPVC_RESOURCE_TYPE_STORAGE_BUFFER)
        {
            std::size_t size = 0;
            spvc_compiler_get_declared_struct_size(m_compiler, base_type, &size);
            binding.block_size = static_cast<std::uint32_t>(size);
        }

        // The instance name, or the block name when the instance is anonymous.
        const char* instance_name = spvc_compiler_get_name(m_compiler, resource.id);
        binding.name = instance_name != nullptr && *instance_name != '\0' ? instance_name : resource.name;

        // Several declarations of one binding with the same type (aliases) are one binding.
        const auto same = std::ranges::find_if(layout.bindings, [&binding](const ShaderBinding& existing)
        {
            return existing.set == binding.set && existing.binding == binding.binding && existing.type == binding.type;
        });
        if (same != layout.bindings.end())
        {
            same->count = same->count == 0 || binding.count == 0 ? 0 : std::max(same->count, binding.count);
            same->block_size = std::max(same->block_size, binding.block_size);
            return;
        }
        layout.bindings.push_back(std::move(binding));
    }

    // Stage inputs and outputs (built-in variables excluded).
    void reflect_stage_variables(ShaderLayout& layout)
    {
        spvc_set active = nullptr;
        spvc_resources resources = nullptr;
        spvc_compiler_get_active_interface_variables(m_compiler, &active);
        spvc_compiler_create_shader_resources_for_active_variables(m_compiler, &resources, active);

        // Per-vertex arrays: tessellation and geometry inputs, tessellation control and mesh outputs.
        const bool arrayed_inputs = layout.stage == ShaderStage::TESSELLATION_CONTROL ||
                                    layout.stage == ShaderStage::TESSELLATION_EVALUATION ||
                                    layout.stage == ShaderStage::GEOMETRY;
        const bool arrayed_outputs = layout.stage == ShaderStage::TESSELLATION_CONTROL || layout.stage == ShaderStage::MESH;

        for (const spvc_reflected_resource& resource : resource_list(resources, SPVC_RESOURCE_TYPE_STAGE_INPUT))
        {
            add_stage_variable(layout.inputs, resource, arrayed_inputs);
        }
        for (const spvc_reflected_resource& resource : resource_list(resources, SPVC_RESOURCE_TYPE_STAGE_OUTPUT))
        {
            add_stage_variable(layout.outputs, resource, arrayed_outputs);
        }
        std::ranges::sort(layout.inputs, {}, &StageVariable::location);
        std::ranges::sort(layout.outputs, {}, &StageVariable::location);
    }

    void add_stage_variable(std::vector<StageVariable>& variables, const spvc_reflected_resource& resource, bool arrayed)
    {
        // Patch variables are not per-vertex arrays.
        const bool per_vertex = arrayed && !spvc_compiler_has_decoration(m_compiler, resource.id, SpvDecorationPatch);
        const spvc_type type = spvc_compiler_get_type_handle(m_compiler, resource.type_id);
        const std::uint32_t elements = element_count(type, per_vertex ? 1 : 0);
        const std::uint32_t location = spvc_compiler_get_decoration(m_compiler, resource.id, SpvDecorationLocation);
        const char* name = spvc_compiler_get_name(m_compiler, resource.id);

        // A block (interface block): one variable per member, at consecutive locations unless decorated.
        const spvc_type base_type = spvc_compiler_get_type_handle(m_compiler, resource.base_type_id);
        if (spvc_type_get_basetype(base_type) == SPVC_BASETYPE_STRUCT)
        {
            std::uint32_t next = location;
            for (std::uint32_t member = 0; member < spvc_type_get_num_member_types(base_type); ++member)
            {
                const spvc_type member_type =
                    spvc_compiler_get_type_handle(m_compiler, spvc_type_get_member_type(base_type, member));
                if (spvc_compiler_has_member_decoration(m_compiler, resource.base_type_id, member, SpvDecorationLocation))
                {
                    next = spvc_compiler_get_member_decoration(m_compiler, resource.base_type_id, member, SpvDecorationLocation);
                }
                StageVariable variable = scalar_variable(member_type, next, element_count(member_type));
                const char* member_name = spvc_compiler_get_member_name(m_compiler, resource.base_type_id, member);
                variable.name = std::format("{}.{}", name != nullptr && *name != '\0' ? name : resource.name,
                                            member_name != nullptr ? member_name : "");
                next += variable.location_count;
                variables.push_back(std::move(variable));
            }
            return;
        }

        StageVariable variable = scalar_variable(base_type, location, elements);
        variable.name = name != nullptr && *name != '\0' ? name : resource.name;
        variables.push_back(std::move(variable));
    }

    // A variable of scalar, vector or matrix @p type, @p elements times.
    [[nodiscard]] static auto scalar_variable(spvc_type type, std::uint32_t location, std::uint32_t elements) -> StageVariable
    {
        const spvc_basetype base = spvc_type_get_basetype(type);
        const std::uint32_t components = spvc_type_get_vector_size(type);
        const std::uint32_t columns = std::max(1U, spvc_type_get_columns(type));
        const bool wide = spvc_type_get_bit_width(type) == 64 && components > 2;
        return {
            .location = location,
            .location_count = std::max(1U, elements) * columns * (wide ? 2U : 1U),
            .format = format_of(base, components),
            .name = {},
        };
    }

    // Workgroup size of compute, task and mesh shaders (default values of specialization constants).
    void reflect_workgroup_size(ShaderLayout& layout)
    {
        if (layout.stage != ShaderStage::COMPUTE && layout.stage != ShaderStage::TASK && layout.stage != ShaderStage::MESH)
        {
            return;
        }

        // Literal sizes, or constant identifiers (LocalSizeId).
        const SpvExecutionMode* modes = nullptr;
        std::size_t count = 0;
        spvc_compiler_get_execution_modes(m_compiler, &modes, &count);
        for (const SpvExecutionMode mode : std::span{modes, count})
        {
            for (std::uint32_t axis = 0; axis < 3; ++axis)
            {
                const std::uint32_t argument = spvc_compiler_get_execution_mode_argument_by_index(m_compiler, mode, axis);
                if (mode == SpvExecutionModeLocalSize)
                {
                    layout.workgroup_size[axis] = argument;
                }
                else if (mode == SpvExecutionModeLocalSizeId)
                {
                    layout.workgroup_size[axis] =
                        spvc_constant_get_scalar_u32(spvc_compiler_get_constant_handle(m_compiler, argument), 0, 0);
                }
            }
        }

        // Sizes set by specialization constants (local_size_x_id): their default values.
        std::array<spvc_specialization_constant, 3> constants{};
        spvc_compiler_get_work_group_size_specialization_constants(m_compiler, &constants[0], &constants[1], &constants[2]);
        for (std::uint32_t axis = 0; axis < 3; ++axis)
        {
            if (constants[axis].id != 0)
            {
                layout.workgroup_size[axis] =
                    spvc_constant_get_scalar_u32(spvc_compiler_get_constant_handle(m_compiler, constants[axis].id), 0, 0);
            }
        }
    }

    // Specialization constants and the size of their values.
    void reflect_specialization_constants(ShaderLayout& layout)
    {
        const spvc_specialization_constant* constants = nullptr;
        std::size_t count = 0;
        spvc_compiler_get_specialization_constants(m_compiler, &constants, &count);
        for (const spvc_specialization_constant& constant : std::span{constants, count})
        {
            const spvc_type type =
                spvc_compiler_get_type_handle(m_compiler, spvc_constant_get_type(spvc_compiler_get_constant_handle(m_compiler, constant.id)));
            const std::uint32_t bits = spvc_type_get_bit_width(type);
            const char* name = spvc_compiler_get_name(m_compiler, constant.id);
            SpecializationConstantInfo information{
                .id = constant.constant_id,
                // Booleans are 32-bit values (VkBool32) in specialization data.
                .size = spvc_type_get_basetype(type) == SPVC_BASETYPE_BOOLEAN ? 4U : std::max(1U, bits / 8U),
                .name = name != nullptr ? name : "",
            };

            // Compilers declare one identifier several times (e.g. for the workgroup size): one entry
            // per identifier, named after the declaration that has a name.
            const auto same = std::ranges::find(layout.specialization_constants, information.id, &SpecializationConstantInfo::id);
            if (same == layout.specialization_constants.end())
            {
                layout.specialization_constants.push_back(std::move(information));
            }
            else if (same->name.empty())
            {
                same->name = std::move(information.name);
            }
        }
        std::ranges::sort(layout.specialization_constants, {}, &SpecializationConstantInfo::id);
    }

private:
    std::string m_shader_name;
    std::string m_errors;
    spvc_context m_context = nullptr;
    spvc_compiler m_compiler = nullptr;
};

} // namespace

auto
reflect_spirv(std::span<const std::uint32_t> spirv,
              ShaderStage stage,
              std::string_view entry_point,
              std::string_view shader_name) -> ShaderLayout
{
    // The words must at least start like SPIR-V before SPIRV-Cross parses them.
    if (spirv.size() < 5 || spirv.front() != SPIRV_MAGIC_NUMBER)
    {
        throw InvalidUsageError({
            .backend = std::nullopt,
            .operation = "reflect_spirv",
            .problem = std::format("shader '{}': the code is not SPIR-V ({} words{})", shader_name, spirv.size(),
                                   spirv.empty() ? std::string{}
                                                 : std::format(", first word {:#010x}", spirv.front())),
            .suggestion = "pass the contents of a .spv file as 32-bit words in their original byte order",
        });
    }
    Reflector reflector{spirv, shader_name};
    return reflector.reflect(stage, entry_point);
}

} // namespace gfx
