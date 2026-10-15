/// @file handles.cppm
/// @brief The handles of the graphics library: one tag per kind of object a Device owns, the handle
///        aliases (BufferHandle, ImageHandle, ...) and the concepts that check where a handle may be
///        used. Handle<Tag> itself, its encoding and HandleIssuer come from gfx.core.handles, which
///        this module re-exports.
export module gfx.handles;

import std;
export import gfx.core.handles;
import gfx.enums;

export namespace gfx
{

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

/// @brief Tag of BufferViewHandle.
struct BufferViewTag
{
    static constexpr std::string_view NAME = "BufferView";
    static constexpr QueueCapability USABLE_ON = QueueCapability::GRAPHICS | QueueCapability::COMPUTE;
};

/// @brief Tag of AccelerationStructureHandle.
struct AccelerationStructureTag
{
    static constexpr std::string_view NAME = "AccelerationStructure";
    static constexpr QueueCapability USABLE_ON = QueueCapability::GRAPHICS | QueueCapability::COMPUTE;
};

/// @brief Tag of RayTracingPipelineHandle.
struct RayTracingPipelineTag
{
    static constexpr std::string_view NAME = "RayTracingPipeline";
    static constexpr QueueCapability USABLE_ON = QueueCapability::COMPUTE;
};

/// @brief Tag of PipelineCacheHandle.
struct PipelineCacheTag
{
    static constexpr std::string_view NAME = "PipelineCache";
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
using BufferViewHandle          = Handle<BufferViewTag>;          ///< Names a BufferView.
using AccelerationStructureHandle = Handle<AccelerationStructureTag>; ///< Names an AccelerationStructure.
using RayTracingPipelineHandle  = Handle<RayTracingPipelineTag>;  ///< Names a RayTracingPipeline.
using PipelineCacheHandle       = Handle<PipelineCacheTag>;       ///< Names a PipelineCache.

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
