/// @file handles.cppm
/// @brief Strongly typed numeric handles naming the objects a Device owns.
///
/// A handle is a 32-bit value: bits [23:0] hold a slot index and bits [31:24] a generation that
/// starts at 1, so the value 0 is always the null handle. Handles are trivially copyable, so they
/// can be stored in render graph nodes, sorted, hashed and passed between threads by value.
///
/// User code can only test, compare, hash and format a handle: its value is private and supports
/// no arithmetic. Encoding and decoding require a HandleKey, which only HandleIssuer descendants
/// (the backends' devices) can obtain.
export module gfx.handles;

import std;
import gfx.enums;
import gfx.errors;

export namespace gfx 
{

class HandleIssuer;

/// @brief Token proving that the caller may encode or decode handle values.
///
/// It can only be created by HandleIssuer, so it is out of reach of code that merely uses handles.
class HandleKey final 
{
private:
    friend class HandleIssuer;
    constexpr HandleKey() noexcept = default;
};

/// @brief Base class of the objects allowed to create handles and read their slot (backend devices).
class HandleIssuer 
{
protected:
    constexpr HandleIssuer() noexcept = default;
    constexpr HandleIssuer(const HandleIssuer&) noexcept = default;
    constexpr HandleIssuer(HandleIssuer&&) noexcept = default;
    constexpr auto operator=(const HandleIssuer&) noexcept -> HandleIssuer& = default;
    constexpr auto operator=(HandleIssuer&&) noexcept -> HandleIssuer& = default;
    ~HandleIssuer() = default;

    /// @brief Key required by Handle::create, Handle::index and Handle::generation.
    [[nodiscard]] static constexpr auto handle_key() noexcept -> HandleKey 
    { 
        return HandleKey{}; 
    }
};

/// @brief Strongly typed, trivially copyable numeric name of an object owned by a Device.
/// @tparam Tag Empty type identifying the kind of object; it provides `NAME` (used in messages) and
///             `USABLE_ON` (the queue capabilities of commands that accept the handle).
template<class Tag>
class Handle 
{
public:
    /// @brief Kind of object this handle names.
    using TagType = Tag;

    /// @brief Creates the null handle.
    constexpr Handle() noexcept = default;

    /// @brief The null handle.
    [[nodiscard]] static constexpr auto null() noexcept -> Handle 
    { 
        return Handle{}; 
    }

    /// @brief Encodes a slot index and generation. Reserved for backends.
    /// @param key        Proof that the caller is a HandleIssuer.
    /// @param index      Slot index, at most MAXIMUM_HANDLE_INDEX.
    /// @param generation Slot generation, in [1, MAXIMUM_HANDLE_GENERATION].
    /// @throws InvalidHandleError if @p index or @p generation is out of range.
    [[nodiscard]] static constexpr auto create(HandleKey key, std::uint32_t index, std::uint32_t generation) -> Handle;

    /// @brief Slot index. Reserved for backends.
    [[nodiscard]] constexpr auto index([[maybe_unused]] HandleKey key) const noexcept -> std::uint32_t 
    {
        return m_value & MAXIMUM_HANDLE_INDEX;
    }

    /// @brief Slot generation (0 for the null handle). Reserved for backends.
    [[nodiscard]] constexpr auto generation([[maybe_unused]] HandleKey key) const noexcept -> std::uint32_t 
    {
        return m_value >> HANDLE_INDEX_BITS;
    }

    /// @brief Whether the handle names an object (it may still be destroyed; the device checks that).
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool 
    { 
        return m_value != 0; 
    }

    /// @brief Same as is_valid().
    [[nodiscard]] constexpr explicit operator bool() const noexcept 
    { 
        return is_valid(); 
    }

    /// @brief Handles of the same kind compare by value; the null handle orders first.
    [[nodiscard]] constexpr auto operator<=>(const Handle&) const noexcept = default;

private:
    friend struct std::hash<Handle>;
    friend struct std::formatter<Handle, char>;

private:
    std::uint32_t m_value = 0;
};

template<class Tag>
constexpr auto Handle<Tag>::create([[maybe_unused]] HandleKey key, 
                                   std::uint32_t index, 
                                   std::uint32_t generation) -> Handle 
{
    // Check that the caller is a HandleIssuer, so only backends can create handles.
    if (index > MAXIMUM_HANDLE_INDEX) 
    {
        throw InvalidHandleError({
            .backend = std::nullopt,
            .operation = "Handle::create",
            .handle_type = std::string{Tag::NAME},
            .problem = std::format("slot index {} exceeds MAXIMUM_HANDLE_INDEX ({}); a device holds at most {} live "
                                   "objects of one kind",
                                   index, MAXIMUM_HANDLE_INDEX, MAXIMUM_HANDLE_INDEX + 1U),
        });
    }

    // Check that the generation is in [1, MAXIMUM_HANDLE_GENERATION]; generation 0 is reserved for the null handle.
    if (generation == 0 || generation > MAXIMUM_HANDLE_GENERATION) 
    {
        throw InvalidHandleError({
            .backend = std::nullopt,
            .operation = "Handle::create",
            .handle_type = std::string{Tag::NAME},
            .problem = std::format("generation {} is outside [1, {}]; generation 0 is reserved for the null handle",
                                   generation, MAXIMUM_HANDLE_GENERATION),
        });
    }

    // Create the handle by packing the generation and index into a 32-bit value.
    Handle handle;
    handle.m_value = (generation << HANDLE_INDEX_BITS) | index;
    return handle;
}

// ================================================================================================= Tags

/// @brief Tag of BufferHandle.
struct BufferTag 
{
    static constexpr std::string_view NAME = "Buffer";
    static constexpr QueueCapability USABLE_ON = QueueCapability::GRAPHICS | QueueCapability::COMPUTE | QueueCapability::TRANSFER;
};

/// @brief Tag of ImageHandle.
struct ImageTag 
{
    static constexpr std::string_view NAME = "Image";
    static constexpr QueueCapability USABLE_ON = QueueCapability::GRAPHICS | QueueCapability::COMPUTE | QueueCapability::TRANSFER;
};

/// @brief Tag of ImageViewHandle.
struct ImageViewTag 
{
    static constexpr std::string_view NAME = "ImageView";
    static constexpr QueueCapability USABLE_ON = QueueCapability::GRAPHICS | QueueCapability::COMPUTE;
};

/// @brief Tag of SamplerHandle.
struct SamplerTag 
{
    static constexpr std::string_view NAME = "Sampler";
    static constexpr QueueCapability USABLE_ON = QueueCapability::GRAPHICS | QueueCapability::COMPUTE;
};

/// @brief Tag of DeviceMemoryHandle.
struct DeviceMemoryTag 
{
    static constexpr std::string_view NAME = "DeviceMemory";
    static constexpr QueueCapability USABLE_ON = QueueCapability::NONE;
};

/// @brief Tag of ShaderModuleHandle.
struct ShaderModuleTag 
{
    static constexpr std::string_view NAME = "ShaderModule";
    static constexpr QueueCapability USABLE_ON = QueueCapability::NONE;
};

/// @brief Tag of DescriptorSetLayoutHandle.
struct DescriptorSetLayoutTag 
{
    static constexpr std::string_view NAME = "DescriptorSetLayout";
    static constexpr QueueCapability USABLE_ON = QueueCapability::NONE;
};

/// @brief Tag of PipelineLayoutHandle.
struct PipelineLayoutTag 
{
    static constexpr std::string_view NAME = "PipelineLayout";
    static constexpr QueueCapability USABLE_ON = QueueCapability::GRAPHICS | QueueCapability::COMPUTE;
};

/// @brief Tag of GraphicsPipelineHandle.
struct GraphicsPipelineTag 
{
    static constexpr std::string_view NAME = "GraphicsPipeline";
    static constexpr QueueCapability USABLE_ON = QueueCapability::GRAPHICS;
};

/// @brief Tag of ComputePipelineHandle.
struct ComputePipelineTag 
{
    static constexpr std::string_view NAME = "ComputePipeline";
    static constexpr QueueCapability USABLE_ON = QueueCapability::COMPUTE;
};

/// @brief Tag of DescriptorPoolHandle.
struct DescriptorPoolTag 
{
    static constexpr std::string_view NAME = "DescriptorPool";
    static constexpr QueueCapability USABLE_ON = QueueCapability::NONE;
};

/// @brief Tag of DescriptorSetHandle.
struct DescriptorSetTag 
{
    static constexpr std::string_view NAME = "DescriptorSet";
    static constexpr QueueCapability USABLE_ON = QueueCapability::GRAPHICS | QueueCapability::COMPUTE;
};
/// @brief Tag of CommandPoolHandle.
struct CommandPoolTag 
{
    static constexpr std::string_view NAME = "CommandPool";
    static constexpr QueueCapability USABLE_ON = QueueCapability::NONE;
};

/// @brief Tag of CommandBufferHandle.
struct CommandBufferTag 
{
    static constexpr std::string_view NAME = "CommandBuffer";
    static constexpr QueueCapability USABLE_ON = QueueCapability::GRAPHICS | QueueCapability::COMPUTE | QueueCapability::TRANSFER;
};

/// @brief Tag of QueueHandle.
struct QueueTag 
{
    static constexpr std::string_view NAME = "Queue";
    static constexpr QueueCapability USABLE_ON = QueueCapability::NONE;
};

/// @brief Tag of FenceHandle.
struct FenceTag 
{
    static constexpr std::string_view NAME = "Fence";
    static constexpr QueueCapability USABLE_ON = QueueCapability::NONE;
};

/// @brief Tag of SemaphoreHandle.
struct SemaphoreTag 
{
    static constexpr std::string_view NAME = "Semaphore";
    static constexpr QueueCapability USABLE_ON = QueueCapability::NONE;
};

/// @brief Tag of TimelineSemaphoreHandle.
struct TimelineSemaphoreTag
{
    static constexpr std::string_view NAME = "TimelineSemaphore";
    static constexpr QueueCapability USABLE_ON = QueueCapability::NONE;
};

/// @brief Tag of EventHandle.
struct EventTag 
{
    static constexpr std::string_view NAME = "Event";
    static constexpr QueueCapability USABLE_ON = QueueCapability::GRAPHICS | QueueCapability::COMPUTE;
};

/// @brief Tag of QueryPoolHandle.
struct QueryPoolTag 
{
    static constexpr std::string_view NAME = "QueryPool";
    static constexpr QueueCapability USABLE_ON = QueueCapability::GRAPHICS | QueueCapability::COMPUTE | QueueCapability::TRANSFER;
};

/// @brief Tag of SwapchainHandle.
struct SwapchainTag {
    static constexpr std::string_view NAME = "Swapchain";
    static constexpr QueueCapability USABLE_ON = QueueCapability::NONE;
};

// ================================================================================================= Handle types

using BufferHandle              = Handle<BufferTag>;              ///< Names a Buffer.
using ImageHandle               = Handle<ImageTag>;               ///< Names an Image.
using ImageViewHandle           = Handle<ImageViewTag>;           ///< Names an ImageView.
using SamplerHandle             = Handle<SamplerTag>;             ///< Names a Sampler.
using DeviceMemoryHandle        = Handle<DeviceMemoryTag>;        ///< Names a DeviceMemory allocation.
using ShaderModuleHandle        = Handle<ShaderModuleTag>;        ///< Names a ShaderModule.
using DescriptorSetLayoutHandle = Handle<DescriptorSetLayoutTag>; ///< Names a DescriptorSetLayout.
using PipelineLayoutHandle      = Handle<PipelineLayoutTag>;      ///< Names a PipelineLayout.
using GraphicsPipelineHandle    = Handle<GraphicsPipelineTag>;    ///< Names a GraphicsPipeline.
using ComputePipelineHandle     = Handle<ComputePipelineTag>;     ///< Names a ComputePipeline.
using DescriptorPoolHandle      = Handle<DescriptorPoolTag>;      ///< Names a DescriptorPool.
using DescriptorSetHandle       = Handle<DescriptorSetTag>;       ///< Names a DescriptorSet.
using CommandPoolHandle         = Handle<CommandPoolTag>;         ///< Names a CommandPool.
using CommandBufferHandle       = Handle<CommandBufferTag>;       ///< Names a CommandBuffer.
using QueueHandle               = Handle<QueueTag>;               ///< Names a Queue.
using FenceHandle               = Handle<FenceTag>;               ///< Names a Fence.
using SemaphoreHandle           = Handle<SemaphoreTag>;           ///< Names a binary Semaphore.
using TimelineSemaphoreHandle   = Handle<TimelineSemaphoreTag>;   ///< Names a TimelineSemaphore.
using EventHandle               = Handle<EventTag>;               ///< Names an Event.
using QueryPoolHandle           = Handle<QueryPoolTag>;           ///< Names a QueryPool.
using SwapchainHandle           = Handle<SwapchainTag>;           ///< Names a Swapchain.

// ================================================================================================= Concepts

/// @brief A gfx handle: a 4-byte, trivially copyable Handle whose tag provides NAME and USABLE_ON.
template<class Type>
concept HandleType = requires { 
                        typename Type::TagType; // Type has a nested TagType.
                    }
                    && std::same_as<Type, Handle<typename Type::TagType>> // Type is a Handle of its TagType.
                    && std::is_trivially_copyable_v<Type>  // Type is trivially copyable.
                    && sizeof(Type) == sizeof(std::uint32_t) // Type is 4 bytes.
                    && requires {
                           { Type::TagType::NAME } -> std::convertible_to<std::string_view>; // Type has a nested NAME.
                           { Type::TagType::USABLE_ON } -> std::convertible_to<QueueCapability>; // Type has a nested USABLE_ON.
                       };

/// @brief A handle accepted by commands recorded for graphics queues.
template<class Type>
concept GraphicsHandle = HandleType<Type> && has_any(Type::TagType::USABLE_ON, QueueCapability::GRAPHICS);

/// @brief A handle accepted by commands recorded for compute queues.
template<class Type>
concept ComputeHandle = HandleType<Type> && has_any(Type::TagType::USABLE_ON, QueueCapability::COMPUTE);

/// @brief A handle accepted by commands recorded for transfer queues.
template<class Type>
concept TransferHandle = HandleType<Type> && has_any(Type::TagType::USABLE_ON, QueueCapability::TRANSFER);

/// @brief A handle naming memory-backed resource contents: the subjects of barriers and aliasing.
template<class Type>
concept ResourceHandle = std::same_as<Type, BufferHandle> || std::same_as<Type, ImageHandle>;

} // namespace gfx


/// @brief Hashes a handle, so handles can key unordered containers.
template<class Tag>
struct std::hash<gfx::Handle<Tag>> 
{
    [[nodiscard]] auto operator()(const gfx::Handle<Tag>& handle) const noexcept -> std::size_t 
    {
        return std::hash<std::uint32_t>{}(handle.m_value);
    }
};

/// @brief Formats a handle as "Buffer(index 5, generation 3)" or "Buffer(null)".
template<class Tag>
struct std::formatter<gfx::Handle<Tag>, char> : std::formatter<std::string_view, char> 
{
    template<class FormatContext>
    auto format(const gfx::Handle<Tag>& handle, FormatContext& context) const -> FormatContext::iterator 
    {
        // Format the handle as "Buffer(index 5, generation 3)" or "Buffer(null)".
        const std::string text = !handle.m_value
            ? std::format("{}(null)", Tag::NAME)
            : std::format("{}(index {}, generation {})", Tag::NAME, handle.m_value & gfx::MAXIMUM_HANDLE_INDEX,
                          handle.m_value >> gfx::HANDLE_INDEX_BITS);

        // Delegate to the string_view formatter to write the text into the context.
        return std::formatter<std::string_view, char>::format(text, context);
    }
};
