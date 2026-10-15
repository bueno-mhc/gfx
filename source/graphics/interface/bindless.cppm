/// @file bindless.cppm
/// @brief The bindless resource model: every texture, storage image, sampler, storage buffer and
///        acceleration structure is registered in one device-wide table when it is created, and shaders
///        reach it through a 32-bit index passed in push constants or buffers.
///
/// This is how modern engines bind resources (Unreal 5, id Tech, Frostbite) and the core of AZDO
/// (Approaching Zero Driver Overhead): one pipeline layout for every pipeline, the table bound once per
/// command buffer by the backend, and no descriptor updates or binds per draw.
///
/// Shaders declare the table in descriptor set BINDLESS_SET, one runtime-sized array per
/// BindlessResourceKind at the binding equal to the kind's value, e.g. in GLSL:
/// @code
/// layout(set = 0, binding = 0) uniform texture2D gfx_sampled_images[];
/// layout(set = 0, binding = 2) uniform sampler gfx_samplers[];
/// layout(push_constant) uniform Draw { uint albedo; uint linear_sampler; } draw;
/// vec4 color = texture(sampler2D(gfx_sampled_images[draw.albedo], gfx_samplers[draw.linear_sampler]), uv);
/// @endcode
export module gfx.interface:bindless;

import std;
import gfx.enums;
import :pipeline_layout;

export namespace gfx
{

/// @brief Index of a resource in the bindless table: GPU data that shaders use to index the table's
///        arrays. Unlike handles, its value is public, since it is written into push constants and
///        buffers. A default-constructed index is invalid (INVALID_BINDLESS_INDEX).
template<BindlessResourceKind Kind>
class BindlessIndex
{
public:
    /// @brief Kind of resource it indexes.
    static constexpr BindlessResourceKind KIND = Kind;

    /// @brief Invalid index.
    constexpr BindlessIndex() noexcept = default;

    /// @brief Index @p value (backends create indices; shaders receive the value).
    constexpr explicit BindlessIndex(std::uint32_t value) noexcept
        : m_value{value}
    {}

    /// @brief The index shaders use.
    [[nodiscard]] constexpr auto value() const noexcept -> std::uint32_t
    {
        return m_value;
    }

    /// @brief Whether it names a resource.
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        return m_value != INVALID_BINDLESS_INDEX;
    }

    /// @brief Same as is_valid().
    constexpr explicit operator bool() const noexcept
    {
        return is_valid();
    }

    constexpr auto operator<=>(const BindlessIndex&) const noexcept = default;

private:
    std::uint32_t m_value = INVALID_BINDLESS_INDEX;
};

using SampledImageIndex = BindlessIndex<BindlessResourceKind::SAMPLED_IMAGE>;                    ///< A texture.
using StorageImageIndex = BindlessIndex<BindlessResourceKind::STORAGE_IMAGE>;                    ///< A storage image.
using SamplerIndex = BindlessIndex<BindlessResourceKind::SAMPLER>;                                ///< A sampler.
using StorageBufferIndex = BindlessIndex<BindlessResourceKind::STORAGE_BUFFER>;                  ///< A storage buffer.
using AccelerationStructureIndex = BindlessIndex<BindlessResourceKind::ACCELERATION_STRUCTURE>;  ///< A top-level structure.

/// @brief Capacity of each array of the bindless table (DeviceDescription::bindless).
struct BindlessTableDescription
{
    std::uint32_t sampled_image_capacity = DEFAULT_BINDLESS_SAMPLED_IMAGE_CAPACITY;                   ///< Textures.
    std::uint32_t storage_image_capacity = DEFAULT_BINDLESS_STORAGE_IMAGE_CAPACITY;                   ///< Storage images.
    std::uint32_t sampler_capacity = DEFAULT_BINDLESS_SAMPLER_CAPACITY;                               ///< Samplers.
    std::uint32_t storage_buffer_capacity = DEFAULT_BINDLESS_STORAGE_BUFFER_CAPACITY;                 ///< Storage buffers.
    std::uint32_t acceleration_structure_capacity = DEFAULT_BINDLESS_ACCELERATION_STRUCTURE_CAPACITY; ///< Top-level structures.

    /// @brief Capacity of the array of @p kind.
    [[nodiscard]] constexpr auto capacity(BindlessResourceKind kind) const noexcept -> std::uint32_t
    {
        switch (kind)
        {
            case BindlessResourceKind::SAMPLED_IMAGE:
                return sampled_image_capacity;
            case BindlessResourceKind::STORAGE_IMAGE:
                return storage_image_capacity;
            case BindlessResourceKind::SAMPLER:
                return sampler_capacity;
            case BindlessResourceKind::STORAGE_BUFFER:
                return storage_buffer_capacity;
            case BindlessResourceKind::ACCELERATION_STRUCTURE:
                return acceleration_structure_capacity;
        }
        return 0;
    }

    constexpr bool operator==(const BindlessTableDescription&) const = default;
};

/// @brief How full the bindless table is (Device::bindless_usage()).
struct BindlessTableUsage
{
    std::array<std::uint32_t, ENUMERATOR_COUNT<BindlessResourceKind>> used{};     ///< Registered resources, by kind.
    std::array<std::uint32_t, ENUMERATOR_COUNT<BindlessResourceKind>> capacity{}; ///< Capacity, by kind.

    /// @brief Registered resources of @p kind.
    [[nodiscard]] constexpr auto used_by(BindlessResourceKind kind) const noexcept -> std::uint32_t
    {
        return used[static_cast<std::size_t>(std::to_underlying(kind))];
    }

    /// @brief Capacity of @p kind.
    [[nodiscard]] constexpr auto capacity_of(BindlessResourceKind kind) const noexcept -> std::uint32_t
    {
        return capacity[static_cast<std::size_t>(std::to_underlying(kind))];
    }
};

/// @brief Descriptor type of the array holding resources of @p kind.
[[nodiscard]] constexpr auto
bindless_descriptor_type(BindlessResourceKind kind) noexcept -> DescriptorType
{
    switch (kind)
    {
        case BindlessResourceKind::SAMPLED_IMAGE:
            return DescriptorType::SAMPLED_IMAGE;
        case BindlessResourceKind::STORAGE_IMAGE:
            return DescriptorType::STORAGE_IMAGE;
        case BindlessResourceKind::SAMPLER:
            return DescriptorType::SAMPLER;
        case BindlessResourceKind::STORAGE_BUFFER:
            return DescriptorType::STORAGE_BUFFER;
        case BindlessResourceKind::ACCELERATION_STRUCTURE:
            return DescriptorType::ACCELERATION_STRUCTURE;
    }
    return DescriptorType::SAMPLED_IMAGE;
}

/// @brief Name of the shader array holding resources of @p kind (the names gfx's shader headers use).
[[nodiscard]] constexpr auto
bindless_array_name(BindlessResourceKind kind) noexcept -> std::string_view
{
    switch (kind)
    {
        case BindlessResourceKind::SAMPLED_IMAGE:
            return "gfx_sampled_images";
        case BindlessResourceKind::STORAGE_IMAGE:
            return "gfx_storage_images";
        case BindlessResourceKind::SAMPLER:
            return "gfx_samplers";
        case BindlessResourceKind::STORAGE_BUFFER:
            return "gfx_storage_buffers";
        case BindlessResourceKind::ACCELERATION_STRUCTURE:
            return "gfx_acceleration_structures";
    }
    return {};
}

/// @brief The pipeline layout every pipeline uses by default: the bindless table in set BINDLESS_SET
///        (one partially bound, update-after-bind array per kind, visible to every stage) and
///        BINDLESS_PUSH_CONSTANT_SIZE bytes of push constants visible to every stage. Since every
///        pipeline shares it, binding the table once per command buffer serves every pipeline.
///        Backends return it from Device::bindless_pipeline_layout().definition().
/// @param table   Capacities of the arrays.
/// @param include Kinds the device supports (acceleration structures need DeviceFeature::RAY_QUERY
///                or RAY_TRACING).
[[nodiscard]] auto
bindless_pipeline_layout_definition(const BindlessTableDescription& table,
                                    std::span<const BindlessResourceKind> include) -> PipelineLayoutDefinition
{
    PipelineLayoutDefinition definition;
    for (const BindlessResourceKind kind : include)
    {
        definition.bindings.push_back({
            .set = BINDLESS_SET,
            .binding = static_cast<std::uint32_t>(std::to_underlying(kind)),
            .type = bindless_descriptor_type(kind),
            .count = table.capacity(kind),
            .stages = ShaderStage::ALL,
            .options = DescriptorBindingOption::PARTIALLY_BOUND | DescriptorBindingOption::UPDATE_AFTER_BIND,
            .name = std::string{bindless_array_name(kind)},
        });
    }
    definition.push_constant_ranges.push_back({.stages = ShaderStage::ALL, .offset = 0, .size = BINDLESS_PUSH_CONSTANT_SIZE});
    return definition;
}

} // namespace gfx

// Bindless indices are GPU data: 4 bytes, trivially copyable, standard layout.
static_assert(sizeof(gfx::SampledImageIndex) == sizeof(std::uint32_t) && std::is_trivially_copyable_v<gfx::SampledImageIndex> &&
              std::is_standard_layout_v<gfx::SampledImageIndex>);

/// @brief Formats a bindless index as "SAMPLED_IMAGE 5" or "SAMPLED_IMAGE invalid".
template<gfx::BindlessResourceKind Kind>
struct std::formatter<gfx::BindlessIndex<Kind>, char> : std::formatter<std::string_view, char>
{
    template<class FormatContext>
    auto format(const gfx::BindlessIndex<Kind>& index, FormatContext& context) const -> FormatContext::iterator
    {
        const std::string text = index.is_valid() ? std::format("{} {}", Kind, index.value()) : std::format("{} invalid", Kind);
        return std::formatter<std::string_view, char>::format(text, context);
    }
};
