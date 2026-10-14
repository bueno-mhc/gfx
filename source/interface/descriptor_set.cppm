/// @file descriptor_set.cppm
/// @brief DescriptorSet: resources bound to the bindings of a descriptor set layout.
export module gfx.interface:descriptor_set;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :shader_layout;
import :layout_compatibility;

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

/// @brief Any resource that can be written to a descriptor.
using DescriptorResource = std::variant<BufferDescriptor, ImageDescriptor, SamplerDescriptor, CombinedImageSamplerDescriptor>;

/// @brief Resources written to consecutive array elements of one binding.
struct DescriptorWrite
{
    std::uint32_t binding = 0;                        ///< Binding index.
    std::uint32_t array_element = 0;                  ///< First array element written.
    std::span<const DescriptorResource> resources{};  ///< Resources, one per element.
};

/// @brief The DescriptorResource alternative that @p Type bindings accept (std::monostate when the
///        library cannot write that type yet: texel buffers and acceleration structures).
template<DescriptorType Type>
using DescriptorResourceOf =
    std::conditional_t<Type == DescriptorType::SAMPLER, SamplerDescriptor,
    std::conditional_t<Type == DescriptorType::COMBINED_IMAGE_SAMPLER, CombinedImageSamplerDescriptor,
    std::conditional_t<Type == DescriptorType::SAMPLED_IMAGE || Type == DescriptorType::STORAGE_IMAGE ||
                       Type == DescriptorType::INPUT_ATTACHMENT, ImageDescriptor,
    std::conditional_t<Type == DescriptorType::UNIFORM_BUFFER || Type == DescriptorType::STORAGE_BUFFER ||
                       Type == DescriptorType::UNIFORM_BUFFER_DYNAMIC || Type == DescriptorType::STORAGE_BUFFER_DYNAMIC,
                       BufferDescriptor, std::monostate>>>>;

/// @brief Whether @p Resource can be written to a binding of type @p Type.
template<class Resource, DescriptorType Type>
concept DescriptorResourceFor = std::same_as<Resource, DescriptorResourceOf<Type>>;

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
        default:
            return false;
    }
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

    /// @brief Writes @p resource to the binding @p Name of @p Shader. The binding index is found and
    ///        the resource type checked at compile time (a buffer written to an image binding does not
    ///        compile). The set must have been allocated with the layout of that binding's set.
    template<FixedString Name, ShaderDefinition Shader, class Resource>
    void write(const Resource& resource, std::uint32_t array_element = 0)
    {
        // Resolve the binding by name and check the resource type, both at compile time.
        constexpr ShaderBinding binding = binding_of<Name, Shader>();
        static_assert(!std::same_as<DescriptorResourceOf<binding.type>, std::monostate>,
                      "gfx: this descriptor type cannot be written with DescriptorSet::write yet");
        static_assert(DescriptorResourceFor<Resource, binding.type>,
                      "gfx: the resource type does not match the binding's descriptor type (see DescriptorResourceOf)");

        // Write the single resource.
        const std::array<DescriptorResource, 1> resources{resource};
        const std::array<DescriptorWrite, 1> writes{
            DescriptorWrite{.binding = binding.binding, .array_element = array_element, .resources = resources}};
        update(writes);
    }
};

void
DescriptorSet::update(std::span<const DescriptorWrite> /*writes*/)
{
    throw_not_implemented("DescriptorSet::update");
}

} // namespace gfx
