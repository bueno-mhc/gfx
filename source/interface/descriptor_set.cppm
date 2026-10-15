/// @file descriptor_set.cppm
/// @brief DescriptorSet: resources bound to the bindings of a descriptor set layout.
export module gfx.interface:descriptor_set;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import :backend_object;
import :pipeline_layout;

export namespace gfx
{

/// @brief A buffer range bound to a uniform or storage binding.
struct BufferDescriptor
{
    BufferHandle buffer{};           ///< Buffer.
    std::uint64_t offset = 0;        ///< First byte.
    std::uint64_t range = WHOLE_SIZE;///< Bytes visible to shaders.
};

/// @brief An image view bound to a sampled, storage or input-attachment binding.
struct ImageDescriptor
{
    ImageViewHandle view{};                          ///< View.
    ImageLayout layout = ImageLayout::SHADER_READ_ONLY; ///< Layout the image is in when accessed.
};

/// @brief A sampler bound to a sampler binding.
struct SamplerDescriptor
{
    SamplerHandle sampler{}; ///< Sampler.
};

/// @brief An image view and a sampler bound to a combined image sampler binding.
struct CombinedImageSamplerDescriptor
{
    ImageViewHandle view{};                          ///< View.
    SamplerHandle sampler{};                         ///< Sampler.
    ImageLayout layout = ImageLayout::SHADER_READ_ONLY; ///< Layout the image is in when accessed.
};

/// @brief A buffer view bound to a uniform or storage texel buffer binding.
struct BufferViewDescriptor
{
    BufferViewHandle view{}; ///< View.
};

/// @brief A top-level acceleration structure bound to an acceleration structure binding.
struct AccelerationStructureDescriptor
{
    AccelerationStructureHandle structure{}; ///< Top-level structure.
};

/// @brief Any resource that can be written to a descriptor.
using DescriptorResource = std::variant<BufferDescriptor, ImageDescriptor, SamplerDescriptor, CombinedImageSamplerDescriptor,
                                        BufferViewDescriptor, AccelerationStructureDescriptor>;

/// @brief Resources written to consecutive array elements of one binding.
struct DescriptorWrite
{
    std::uint32_t binding = 0;                        ///< Binding index.
    std::uint32_t array_element = 0;                  ///< First array element written.
    std::span<const DescriptorResource> resources{};  ///< Resources, one per element.
};

/// @brief Whether a resource matches a binding type (for backends validating DescriptorWrites).
[[nodiscard]] constexpr auto
descriptor_resource_matches(DescriptorType type,
                            const DescriptorResource& resource) noexcept -> bool
{
    switch (type)
    {
        case DescriptorType::SAMPLER:
            return std::holds_alternative<SamplerDescriptor>(resource);
        case DescriptorType::COMBINED_IMAGE_SAMPLER:
            return std::holds_alternative<CombinedImageSamplerDescriptor>(resource);
        case DescriptorType::SAMPLED_IMAGE:
        case DescriptorType::STORAGE_IMAGE:
        case DescriptorType::INPUT_ATTACHMENT:
            return std::holds_alternative<ImageDescriptor>(resource);
        case DescriptorType::UNIFORM_BUFFER:
        case DescriptorType::STORAGE_BUFFER:
        case DescriptorType::UNIFORM_BUFFER_DYNAMIC:
        case DescriptorType::STORAGE_BUFFER_DYNAMIC:
            return std::holds_alternative<BufferDescriptor>(resource);
        case DescriptorType::UNIFORM_TEXEL_BUFFER:
        case DescriptorType::STORAGE_TEXEL_BUFFER:
            return std::holds_alternative<BufferViewDescriptor>(resource);
        case DescriptorType::ACCELERATION_STRUCTURE:
            return std::holds_alternative<AccelerationStructureDescriptor>(resource);
    }
    return false;
}

/// @brief Name of the DescriptorResource alternative a binding of type @p type accepts, for messages.
[[nodiscard]] constexpr auto
descriptor_resource_name(DescriptorType type) noexcept -> std::string_view
{
    switch (type)
    {
        case DescriptorType::SAMPLER:
            return "SamplerDescriptor";
        case DescriptorType::COMBINED_IMAGE_SAMPLER:
            return "CombinedImageSamplerDescriptor";
        case DescriptorType::SAMPLED_IMAGE:
        case DescriptorType::STORAGE_IMAGE:
        case DescriptorType::INPUT_ATTACHMENT:
            return "ImageDescriptor";
        case DescriptorType::UNIFORM_BUFFER:
        case DescriptorType::STORAGE_BUFFER:
        case DescriptorType::UNIFORM_BUFFER_DYNAMIC:
        case DescriptorType::STORAGE_BUFFER_DYNAMIC:
            return "BufferDescriptor";
        case DescriptorType::UNIFORM_TEXEL_BUFFER:
        case DescriptorType::STORAGE_TEXEL_BUFFER:
            return "BufferViewDescriptor";
        case DescriptorType::ACCELERATION_STRUCTURE:
            return "AccelerationStructureDescriptor";
    }
    return {};
}

/// @brief Name of the alternative @p resource holds, for messages.
[[nodiscard]] constexpr auto
descriptor_resource_name(const DescriptorResource& resource) noexcept -> std::string_view
{
    constexpr std::array<std::string_view, 6> NAMES{"BufferDescriptor",       "ImageDescriptor",
                                                    "SamplerDescriptor",      "CombinedImageSamplerDescriptor",
                                                    "BufferViewDescriptor",   "AccelerationStructureDescriptor"};
    static_assert(NAMES.size() == std::variant_size_v<DescriptorResource>);
    return NAMES[resource.index()];
}

/// @brief Resources bound to the bindings of a descriptor set layout. Allocated from a DescriptorPool.
class DescriptorSet : public BackendObject
{
public:
    /// @brief Handle naming this set when binding it.
    [[nodiscard]] virtual auto handle() const noexcept -> DescriptorSetHandle = 0;

    /// @brief Layout the set was allocated with.
    [[nodiscard]] virtual auto layout() const noexcept -> DescriptorSetLayoutHandle = 0;

    /// @brief Writes resources to bindings.
    /// @throws InvalidUsageError when a resource does not match its binding's type.
    virtual void update(std::span<const DescriptorWrite> writes) = 0;

    /// @brief Writes @p resource to the binding at @p location (from PipelineLayout::find_binding()).
    ///        The set must have been allocated with the layout of set @p location.set.
    /// @throws InvalidUsageError when the resource does not match the binding's type or
    ///         @p array_element is beyond the binding's array.
    void write(const BindingLocation& location,
               const DescriptorResource& resource,
               std::uint32_t array_element = 0)
    {
        // The resource must be the kind the binding accepts.
        if (!descriptor_resource_matches(location.type, resource))
        {
            throw InvalidUsageError({
                .backend = backend(),
                .operation = "DescriptorSet::write",
                .problem = std::format("set {} binding {} is {}, which takes {}, not {}", location.set, location.binding,
                                       location.type, descriptor_resource_name(location.type),
                                       descriptor_resource_name(resource)),
                .suggestion = "",
            });
        }

        // A fixed-size array must contain the element (runtime-sized arrays have count 0).
        if (location.count != 0 && array_element >= location.count)
        {
            throw InvalidUsageError({
                .backend = backend(),
                .operation = "DescriptorSet::write",
                .problem = std::format("set {} binding {} has {} element(s); element {} does not exist", location.set,
                                       location.binding, location.count, array_element),
                .suggestion = "",
            });
        }
        write(location.binding, resource, array_element);
    }

    /// @brief Writes @p resource to binding index @p binding (checked by the backend).
    void write(std::uint32_t binding,
               const DescriptorResource& resource,
               std::uint32_t array_element = 0)
    {
        const std::array<DescriptorResource, 1> resources{resource};
        const std::array<DescriptorWrite, 1> writes{
            DescriptorWrite{.binding = binding, .array_element = array_element, .resources = resources}};
        update(writes);
    }
};

void
DescriptorSet::update(std::span<const DescriptorWrite> /*writes*/)
{
    throw_not_implemented("DescriptorSet::update");
}

} // namespace gfx
