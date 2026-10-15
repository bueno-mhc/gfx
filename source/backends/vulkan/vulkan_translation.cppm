/// @file vulkan_translation.cppm
/// @brief Internal partition: translation of gfx enumerations and structures to Vulkan ones (and back
///        where the backend reads Vulkan values).
module;

#include <volk.h>

module gfx.backends.vulkan:translation;

import std;
import gfx.enums;
import gfx.interface;

namespace gfx::vulkan
{

/// @brief One gfx bit and the Vulkan bits it stands for.
template<class Bits, class NativeBits>
struct BitTranslation
{
    Bits bits;
    NativeBits native;
};

/// @brief The Vulkan bits of every gfx bit set in @p value, according to @p table.
template<class Bits, class NativeBits, std::size_t COUNT>
[[nodiscard]] constexpr auto
translate_bits(Bits value,
               const std::array<BitTranslation<Bits, NativeBits>, COUNT>& table) noexcept -> NativeBits
{
    NativeBits native = 0;
    for (const BitTranslation<Bits, NativeBits>& entry : table)
    {
        if ((std::to_underlying(value) & std::to_underlying(entry.bits)) != 0)
        {
            native |= entry.native;
        }
    }
    return native;
}

// ------------------------------------------------------------------------------------------- formats

/// @brief Vulkan format of a gfx format (gfx format names are Vulkan's without VK_FORMAT_).
[[nodiscard]] constexpr auto
to_vulkan(Format format) noexcept -> VkFormat
{
    switch (format)
    {
    case Format::UNDEFINED: return VK_FORMAT_UNDEFINED;
    case Format::R8_UNORM: return VK_FORMAT_R8_UNORM;
    case Format::R8_SNORM: return VK_FORMAT_R8_SNORM;
    case Format::R8_UINT: return VK_FORMAT_R8_UINT;
    case Format::R8_SINT: return VK_FORMAT_R8_SINT;
    case Format::R8G8_UNORM: return VK_FORMAT_R8G8_UNORM;
    case Format::R8G8_SNORM: return VK_FORMAT_R8G8_SNORM;
    case Format::R8G8_UINT: return VK_FORMAT_R8G8_UINT;
    case Format::R8G8_SINT: return VK_FORMAT_R8G8_SINT;
    case Format::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::R8G8B8A8_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;
    case Format::R8G8B8A8_UINT: return VK_FORMAT_R8G8B8A8_UINT;
    case Format::R8G8B8A8_SINT: return VK_FORMAT_R8G8B8A8_SINT;
    case Format::R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::A2B10G10R10_UNORM_PACK32: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case Format::A2B10G10R10_UINT_PACK32: return VK_FORMAT_A2B10G10R10_UINT_PACK32;
    case Format::R16_UNORM: return VK_FORMAT_R16_UNORM;
    case Format::R16_SNORM: return VK_FORMAT_R16_SNORM;
    case Format::R16_UINT: return VK_FORMAT_R16_UINT;
    case Format::R16_SINT: return VK_FORMAT_R16_SINT;
    case Format::R16_SFLOAT: return VK_FORMAT_R16_SFLOAT;
    case Format::R16G16_UNORM: return VK_FORMAT_R16G16_UNORM;
    case Format::R16G16_SNORM: return VK_FORMAT_R16G16_SNORM;
    case Format::R16G16_UINT: return VK_FORMAT_R16G16_UINT;
    case Format::R16G16_SINT: return VK_FORMAT_R16G16_SINT;
    case Format::R16G16_SFLOAT: return VK_FORMAT_R16G16_SFLOAT;
    case Format::R16G16B16A16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
    case Format::R16G16B16A16_SNORM: return VK_FORMAT_R16G16B16A16_SNORM;
    case Format::R16G16B16A16_UINT: return VK_FORMAT_R16G16B16A16_UINT;
    case Format::R16G16B16A16_SINT: return VK_FORMAT_R16G16B16A16_SINT;
    case Format::R16G16B16A16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::R32_UINT: return VK_FORMAT_R32_UINT;
    case Format::R32_SINT: return VK_FORMAT_R32_SINT;
    case Format::R32_SFLOAT: return VK_FORMAT_R32_SFLOAT;
    case Format::R32G32_UINT: return VK_FORMAT_R32G32_UINT;
    case Format::R32G32_SINT: return VK_FORMAT_R32G32_SINT;
    case Format::R32G32_SFLOAT: return VK_FORMAT_R32G32_SFLOAT;
    case Format::R32G32B32_UINT: return VK_FORMAT_R32G32B32_UINT;
    case Format::R32G32B32_SINT: return VK_FORMAT_R32G32B32_SINT;
    case Format::R32G32B32_SFLOAT: return VK_FORMAT_R32G32B32_SFLOAT;
    case Format::R32G32B32A32_UINT: return VK_FORMAT_R32G32B32A32_UINT;
    case Format::R32G32B32A32_SINT: return VK_FORMAT_R32G32B32A32_SINT;
    case Format::R32G32B32A32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::B10G11R11_UFLOAT_PACK32: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case Format::E5B9G9R9_UFLOAT_PACK32: return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
    case Format::D16_UNORM: return VK_FORMAT_D16_UNORM;
    case Format::X8_D24_UNORM_PACK32: return VK_FORMAT_X8_D24_UNORM_PACK32;
    case Format::D32_SFLOAT: return VK_FORMAT_D32_SFLOAT;
    case Format::S8_UINT: return VK_FORMAT_S8_UINT;
    case Format::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
    case Format::D32_SFLOAT_S8_UINT: return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case Format::BC1_RGBA_UNORM_BLOCK: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case Format::BC1_RGBA_SRGB_BLOCK: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case Format::BC2_UNORM_BLOCK: return VK_FORMAT_BC2_UNORM_BLOCK;
    case Format::BC2_SRGB_BLOCK: return VK_FORMAT_BC2_SRGB_BLOCK;
    case Format::BC3_UNORM_BLOCK: return VK_FORMAT_BC3_UNORM_BLOCK;
    case Format::BC3_SRGB_BLOCK: return VK_FORMAT_BC3_SRGB_BLOCK;
    case Format::BC4_UNORM_BLOCK: return VK_FORMAT_BC4_UNORM_BLOCK;
    case Format::BC4_SNORM_BLOCK: return VK_FORMAT_BC4_SNORM_BLOCK;
    case Format::BC5_UNORM_BLOCK: return VK_FORMAT_BC5_UNORM_BLOCK;
    case Format::BC5_SNORM_BLOCK: return VK_FORMAT_BC5_SNORM_BLOCK;
    case Format::BC6H_UFLOAT_BLOCK: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
    case Format::BC6H_SFLOAT_BLOCK: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
    case Format::BC7_UNORM_BLOCK: return VK_FORMAT_BC7_UNORM_BLOCK;
    case Format::BC7_SRGB_BLOCK: return VK_FORMAT_BC7_SRGB_BLOCK;
    case Format::ETC2_R8G8B8A8_UNORM_BLOCK: return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
    case Format::ETC2_R8G8B8A8_SRGB_BLOCK: return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;
    case Format::ASTC_4x4_UNORM_BLOCK: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
    case Format::ASTC_4x4_SRGB_BLOCK: return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
    }
    return VK_FORMAT_UNDEFINED;
}

/// @brief gfx format of a Vulkan format (UNDEFINED for formats gfx does not name).
[[nodiscard]] constexpr auto
from_vulkan(VkFormat format) noexcept -> Format
{
    for (std::size_t index = 0; index < ENUMERATOR_COUNT<Format>; ++index)
    {
        const auto candidate = static_cast<Format>(index);
        if (to_vulkan(candidate) == format)
        {
            return candidate;
        }
    }
    return Format::UNDEFINED;
}

/// @brief Aspects a format has (COLOR, or DEPTH and/or STENCIL).
[[nodiscard]] constexpr auto
format_aspects(Format format) noexcept -> ImageAspect
{
    return format_properties(format).aspects;
}

/// @brief Vulkan aspect bits.
[[nodiscard]] constexpr auto
to_vulkan(ImageAspect aspects) noexcept -> VkImageAspectFlags
{
    constexpr std::array<BitTranslation<ImageAspect, VkImageAspectFlags>, 3> TABLE{{
        {ImageAspect::COLOR, VK_IMAGE_ASPECT_COLOR_BIT},
        {ImageAspect::DEPTH, VK_IMAGE_ASPECT_DEPTH_BIT},
        {ImageAspect::STENCIL, VK_IMAGE_ASPECT_STENCIL_BIT},
    }};
    return translate_bits(aspects, TABLE);
}

// ------------------------------------------------------------------------------------ synchronization

/// @brief Vulkan pipeline stages (synchronization2).
[[nodiscard]] constexpr auto
to_vulkan(PipelineStage stages) noexcept -> VkPipelineStageFlags2
{
    constexpr std::array<BitTranslation<PipelineStage, VkPipelineStageFlags2>, 20> TABLE{{
        {PipelineStage::DRAW_INDIRECT, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT},
        {PipelineStage::VERTEX_INPUT, VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT},
        {PipelineStage::VERTEX_SHADER, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT},
        {PipelineStage::TESSELLATION_CONTROL_SHADER, VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT},
        {PipelineStage::TESSELLATION_EVALUATION_SHADER, VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT},
        {PipelineStage::GEOMETRY_SHADER, VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT},
        {PipelineStage::FRAGMENT_SHADER, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT},
        {PipelineStage::EARLY_FRAGMENT_TESTS, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT},
        {PipelineStage::LATE_FRAGMENT_TESTS, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT},
        {PipelineStage::COLOR_ATTACHMENT_OUTPUT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
        {PipelineStage::COMPUTE_SHADER, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT},
        {PipelineStage::TRANSFER, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT},
        {PipelineStage::HOST, VK_PIPELINE_STAGE_2_HOST_BIT},
        {PipelineStage::TASK_SHADER, VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT},
        {PipelineStage::MESH_SHADER, VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT},
        {PipelineStage::RAY_TRACING_SHADER, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR},
        {PipelineStage::ACCELERATION_STRUCTURE_BUILD, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR},
        {PipelineStage::ALL_GRAPHICS, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT},
        {PipelineStage::ALL_COMMANDS, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT},
        {PipelineStage::SHADING_RATE_ATTACHMENT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR},
    }};
    return translate_bits(stages, TABLE);
}

/// @brief Vulkan memory accesses (synchronization2).
[[nodiscard]] constexpr auto
to_vulkan(MemoryAccess access) noexcept -> VkAccessFlags2
{
    constexpr std::array<BitTranslation<MemoryAccess, VkAccessFlags2>, 21> TABLE{{
        {MemoryAccess::INDIRECT_COMMAND_READ, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT},
        {MemoryAccess::INDEX_READ, VK_ACCESS_2_INDEX_READ_BIT},
        {MemoryAccess::VERTEX_ATTRIBUTE_READ, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT},
        {MemoryAccess::UNIFORM_READ, VK_ACCESS_2_UNIFORM_READ_BIT},
        {MemoryAccess::INPUT_ATTACHMENT_READ, VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT},
        {MemoryAccess::SHADER_SAMPLED_READ, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT},
        {MemoryAccess::SHADER_STORAGE_READ, VK_ACCESS_2_SHADER_STORAGE_READ_BIT},
        {MemoryAccess::SHADER_STORAGE_WRITE, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT},
        {MemoryAccess::COLOR_ATTACHMENT_READ, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT},
        {MemoryAccess::COLOR_ATTACHMENT_WRITE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT},
        {MemoryAccess::DEPTH_STENCIL_ATTACHMENT_READ, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT},
        {MemoryAccess::DEPTH_STENCIL_ATTACHMENT_WRITE, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
        {MemoryAccess::TRANSFER_READ, VK_ACCESS_2_TRANSFER_READ_BIT},
        {MemoryAccess::TRANSFER_WRITE, VK_ACCESS_2_TRANSFER_WRITE_BIT},
        {MemoryAccess::HOST_READ, VK_ACCESS_2_HOST_READ_BIT},
        {MemoryAccess::HOST_WRITE, VK_ACCESS_2_HOST_WRITE_BIT},
        {MemoryAccess::MEMORY_READ, VK_ACCESS_2_MEMORY_READ_BIT},
        {MemoryAccess::MEMORY_WRITE, VK_ACCESS_2_MEMORY_WRITE_BIT},
        {MemoryAccess::ACCELERATION_STRUCTURE_READ, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR},
        {MemoryAccess::ACCELERATION_STRUCTURE_WRITE, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR},
        {MemoryAccess::SHADING_RATE_ATTACHMENT_READ, VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR},
    }};
    return translate_bits(access, TABLE);
}

/// @brief Vulkan image layout.
[[nodiscard]] constexpr auto
to_vulkan(ImageLayout layout) noexcept -> VkImageLayout
{
    switch (layout)
    {
    case ImageLayout::UNDEFINED: return VK_IMAGE_LAYOUT_UNDEFINED;
    case ImageLayout::GENERAL: return VK_IMAGE_LAYOUT_GENERAL;
    case ImageLayout::COLOR_ATTACHMENT: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case ImageLayout::DEPTH_STENCIL_ATTACHMENT: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case ImageLayout::DEPTH_STENCIL_READ_ONLY: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case ImageLayout::SHADER_READ_ONLY: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case ImageLayout::TRANSFER_SOURCE: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case ImageLayout::TRANSFER_DESTINATION: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case ImageLayout::PRESENT_SOURCE: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    case ImageLayout::SHADING_RATE_ATTACHMENT: return VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

// ------------------------------------------------------------------------------------------ resources

/// @brief Vulkan buffer usage (device addresses are added by the backend when the device has them).
[[nodiscard]] constexpr auto
to_vulkan(BufferUsage usage) noexcept -> VkBufferUsageFlags
{
    constexpr std::array<BitTranslation<BufferUsage, VkBufferUsageFlags>, 13> TABLE{{
        {BufferUsage::TRANSFER_SOURCE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT},
        {BufferUsage::TRANSFER_DESTINATION, VK_BUFFER_USAGE_TRANSFER_DST_BIT},
        {BufferUsage::UNIFORM_TEXEL, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT},
        {BufferUsage::STORAGE_TEXEL, VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT},
        {BufferUsage::UNIFORM, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT},
        {BufferUsage::STORAGE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT},
        {BufferUsage::INDEX, VK_BUFFER_USAGE_INDEX_BUFFER_BIT},
        {BufferUsage::VERTEX, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT},
        {BufferUsage::INDIRECT, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT},
        {BufferUsage::SHADER_DEVICE_ADDRESS, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT},
        {BufferUsage::ACCELERATION_STRUCTURE_STORAGE, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR},
        {BufferUsage::ACCELERATION_STRUCTURE_BUILD_INPUT, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR},
        {BufferUsage::SHADER_BINDING_TABLE, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR},
    }};
    return translate_bits(usage, TABLE);
}

/// @brief Vulkan image usage.
[[nodiscard]] constexpr auto
to_vulkan(ImageUsage usage) noexcept -> VkImageUsageFlags
{
    constexpr std::array<BitTranslation<ImageUsage, VkImageUsageFlags>, 9> TABLE{{
        {ImageUsage::TRANSFER_SOURCE, VK_IMAGE_USAGE_TRANSFER_SRC_BIT},
        {ImageUsage::TRANSFER_DESTINATION, VK_IMAGE_USAGE_TRANSFER_DST_BIT},
        {ImageUsage::SAMPLED, VK_IMAGE_USAGE_SAMPLED_BIT},
        {ImageUsage::STORAGE, VK_IMAGE_USAGE_STORAGE_BIT},
        {ImageUsage::COLOR_ATTACHMENT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT},
        {ImageUsage::DEPTH_STENCIL_ATTACHMENT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT},
        {ImageUsage::TRANSIENT_ATTACHMENT, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT},
        {ImageUsage::INPUT_ATTACHMENT, VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT},
        {ImageUsage::SHADING_RATE_ATTACHMENT, VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR},
    }};
    return translate_bits(usage, TABLE);
}

/// @brief gfx image usage of Vulkan format features (what format_support() reports).
[[nodiscard]] constexpr auto
image_usage_of(VkFormatFeatureFlags2 features) noexcept -> ImageUsage
{
    ImageUsage usage = ImageUsage::NONE;
    const auto add = [&](VkFormatFeatureFlags2 required, ImageUsage granted)
    {
        if ((features & required) == required)
        {
            usage |= granted;
        }
    };
    add(VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT, ImageUsage::TRANSFER_SOURCE);
    add(VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT, ImageUsage::TRANSFER_DESTINATION);
    add(VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT, ImageUsage::SAMPLED);
    add(VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT, ImageUsage::STORAGE);
    add(VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT, ImageUsage::COLOR_ATTACHMENT | ImageUsage::INPUT_ATTACHMENT |
                                                       ImageUsage::TRANSIENT_ATTACHMENT);
    add(VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT, ImageUsage::DEPTH_STENCIL_ATTACHMENT |
                                                               ImageUsage::INPUT_ATTACHMENT | ImageUsage::TRANSIENT_ATTACHMENT);
    add(VK_FORMAT_FEATURE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR, ImageUsage::SHADING_RATE_ATTACHMENT);
    return usage;
}

/// @brief gfx buffer usage of Vulkan buffer format features.
[[nodiscard]] constexpr auto
buffer_usage_of(VkFormatFeatureFlags2 features) noexcept -> BufferUsage
{
    BufferUsage usage = BufferUsage::NONE;
    if ((features & VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT) != 0)
    {
        usage |= BufferUsage::UNIFORM_TEXEL;
    }
    if ((features & VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT) != 0)
    {
        usage |= BufferUsage::STORAGE_TEXEL;
    }
    if ((features & VK_FORMAT_FEATURE_2_VERTEX_BUFFER_BIT) != 0)
    {
        usage |= BufferUsage::VERTEX;
    }
    if ((features & VK_FORMAT_FEATURE_2_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR) != 0)
    {
        usage |= BufferUsage::ACCELERATION_STRUCTURE_BUILD_INPUT;
    }
    return usage;
}

/// @brief Vulkan image creation flags.
[[nodiscard]] constexpr auto
to_vulkan(ImageCreationOption options) noexcept -> VkImageCreateFlags
{
    constexpr std::array<BitTranslation<ImageCreationOption, VkImageCreateFlags>, 4> TABLE{{
        {ImageCreationOption::CUBE_COMPATIBLE, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT},
        {ImageCreationOption::MUTABLE_FORMAT, VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT},
        {ImageCreationOption::ALIASED, VK_IMAGE_CREATE_ALIAS_BIT},
        {ImageCreationOption::SPARSE_RESIDENCY, VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT},
    }};
    return translate_bits(options, TABLE);
}

/// @brief Vulkan image type.
[[nodiscard]] constexpr auto
to_vulkan(ImageDimension dimension) noexcept -> VkImageType
{
    switch (dimension)
    {
    case ImageDimension::ONE_DIMENSIONAL: return VK_IMAGE_TYPE_1D;
    case ImageDimension::TWO_DIMENSIONAL: return VK_IMAGE_TYPE_2D;
    case ImageDimension::THREE_DIMENSIONAL: return VK_IMAGE_TYPE_3D;
    }
    return VK_IMAGE_TYPE_2D;
}

/// @brief Vulkan image view type.
[[nodiscard]] constexpr auto
to_vulkan(ImageViewType type) noexcept -> VkImageViewType
{
    switch (type)
    {
    case ImageViewType::ONE_DIMENSIONAL: return VK_IMAGE_VIEW_TYPE_1D;
    case ImageViewType::TWO_DIMENSIONAL: return VK_IMAGE_VIEW_TYPE_2D;
    case ImageViewType::THREE_DIMENSIONAL: return VK_IMAGE_VIEW_TYPE_3D;
    case ImageViewType::CUBE: return VK_IMAGE_VIEW_TYPE_CUBE;
    case ImageViewType::ONE_DIMENSIONAL_ARRAY: return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    case ImageViewType::TWO_DIMENSIONAL_ARRAY: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case ImageViewType::CUBE_ARRAY: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    }
    return VK_IMAGE_VIEW_TYPE_2D;
}

/// @brief Vulkan image tiling.
[[nodiscard]] constexpr auto
to_vulkan(ImageTiling tiling) noexcept -> VkImageTiling
{
    return tiling == ImageTiling::LINEAR ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
}

/// @brief Vulkan sample count (the bits have the same values).
[[nodiscard]] constexpr auto
to_vulkan(SampleCount samples) noexcept -> VkSampleCountFlagBits
{
    return samples == SampleCount::NONE ? VK_SAMPLE_COUNT_1_BIT : static_cast<VkSampleCountFlagBits>(std::to_underlying(samples));
}

/// @brief gfx sample counts of Vulkan sample count bits.
[[nodiscard]] constexpr auto
from_vulkan(VkSampleCountFlags samples) noexcept -> SampleCount
{
    return static_cast<SampleCount>(samples & 0x7FU);
}

/// @brief Vulkan component swizzle.
[[nodiscard]] constexpr auto
to_vulkan(ComponentSwizzle swizzle) noexcept -> VkComponentSwizzle
{
    switch (swizzle)
    {
    case ComponentSwizzle::IDENTITY: return VK_COMPONENT_SWIZZLE_IDENTITY;
    case ComponentSwizzle::ZERO: return VK_COMPONENT_SWIZZLE_ZERO;
    case ComponentSwizzle::ONE: return VK_COMPONENT_SWIZZLE_ONE;
    case ComponentSwizzle::RED: return VK_COMPONENT_SWIZZLE_R;
    case ComponentSwizzle::GREEN: return VK_COMPONENT_SWIZZLE_G;
    case ComponentSwizzle::BLUE: return VK_COMPONENT_SWIZZLE_B;
    case ComponentSwizzle::ALPHA: return VK_COMPONENT_SWIZZLE_A;
    }
    return VK_COMPONENT_SWIZZLE_IDENTITY;
}

/// @brief Vulkan component mapping.
[[nodiscard]] constexpr auto
to_vulkan(const ComponentMapping& mapping) noexcept -> VkComponentMapping
{
    return {.r = to_vulkan(mapping.red), .g = to_vulkan(mapping.green), .b = to_vulkan(mapping.blue), .a = to_vulkan(mapping.alpha)};
}

/// @brief Vulkan subresource range (REMAINING_* have Vulkan's values).
[[nodiscard]] constexpr auto
to_vulkan(const ImageSubresourceRange& range) noexcept -> VkImageSubresourceRange
{
    return {
        .aspectMask = to_vulkan(range.aspects),
        .baseMipLevel = range.base_mip_level,
        .levelCount = range.mip_level_count,
        .baseArrayLayer = range.base_array_layer,
        .layerCount = range.array_layer_count,
    };
}

/// @brief Vulkan subresource layers.
[[nodiscard]] constexpr auto
to_vulkan(const ImageSubresourceLayers& layers) noexcept -> VkImageSubresourceLayers
{
    return {
        .aspectMask = to_vulkan(layers.aspects),
        .mipLevel = layers.mip_level,
        .baseArrayLayer = layers.base_array_layer,
        .layerCount = layers.array_layer_count,
    };
}

/// @brief Vulkan offset.
[[nodiscard]] constexpr auto
to_vulkan(const Offset& offset) noexcept -> VkOffset3D
{
    return {.x = offset.x, .y = offset.y, .z = offset.z};
}

/// @brief Vulkan extent.
[[nodiscard]] constexpr auto
to_vulkan(const Extent& extent) noexcept -> VkExtent3D
{
    return {.width = extent.width, .height = extent.height, .depth = extent.depth};
}

/// @brief Vulkan rectangle.
[[nodiscard]] constexpr auto
to_vulkan(const Rectangle& rectangle) noexcept -> VkRect2D
{
    return {.offset = {.x = rectangle.x, .y = rectangle.y}, .extent = {.width = rectangle.width, .height = rectangle.height}};
}

// -------------------------------------------------------------------------------------------- samplers

/// @brief Vulkan filter.
[[nodiscard]] constexpr auto
to_vulkan(Filter filter) noexcept -> VkFilter
{
    return filter == Filter::NEAREST ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

/// @brief Vulkan mipmap mode.
[[nodiscard]] constexpr auto
to_vulkan(SamplerMipmapMode mode) noexcept -> VkSamplerMipmapMode
{
    return mode == SamplerMipmapMode::NEAREST ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

/// @brief Vulkan address mode.
[[nodiscard]] constexpr auto
to_vulkan(SamplerAddressMode mode) noexcept -> VkSamplerAddressMode
{
    switch (mode)
    {
    case SamplerAddressMode::REPEAT: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case SamplerAddressMode::MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case SamplerAddressMode::CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case SamplerAddressMode::CLAMP_TO_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case SamplerAddressMode::MIRROR_CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

/// @brief Vulkan border color.
[[nodiscard]] constexpr auto
to_vulkan(BorderColor color) noexcept -> VkBorderColor
{
    switch (color)
    {
    case BorderColor::TRANSPARENT_BLACK_FLOAT: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    case BorderColor::TRANSPARENT_BLACK_INTEGER: return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    case BorderColor::OPAQUE_BLACK_FLOAT: return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    case BorderColor::OPAQUE_BLACK_INTEGER: return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    case BorderColor::OPAQUE_WHITE_FLOAT: return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    case BorderColor::OPAQUE_WHITE_INTEGER: return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    }
    return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
}

/// @brief Vulkan compare operation.
[[nodiscard]] constexpr auto
to_vulkan(CompareOperation operation) noexcept -> VkCompareOp
{
    switch (operation)
    {
    case CompareOperation::NEVER: return VK_COMPARE_OP_NEVER;
    case CompareOperation::LESS: return VK_COMPARE_OP_LESS;
    case CompareOperation::EQUAL: return VK_COMPARE_OP_EQUAL;
    case CompareOperation::LESS_OR_EQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOperation::GREATER: return VK_COMPARE_OP_GREATER;
    case CompareOperation::NOT_EQUAL: return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOperation::GREATER_OR_EQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOperation::ALWAYS: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

/// @brief Vulkan sampler reduction mode.
[[nodiscard]] constexpr auto
to_vulkan(SamplerReductionMode mode) noexcept -> VkSamplerReductionMode
{
    switch (mode)
    {
    case SamplerReductionMode::WEIGHTED_AVERAGE: return VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
    case SamplerReductionMode::MINIMUM: return VK_SAMPLER_REDUCTION_MODE_MIN;
    case SamplerReductionMode::MAXIMUM: return VK_SAMPLER_REDUCTION_MODE_MAX;
    }
    return VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
}

// ------------------------------------------------------------------------------------ shaders, binding

/// @brief Vulkan shader stages (ShaderStage::ALL is VK_SHADER_STAGE_ALL).
[[nodiscard]] constexpr auto
to_vulkan(ShaderStage stages) noexcept -> VkShaderStageFlags
{
    if (stages == ShaderStage::ALL)
    {
        return VK_SHADER_STAGE_ALL;
    }
    constexpr std::array<BitTranslation<ShaderStage, VkShaderStageFlags>, 14> TABLE{{
        {ShaderStage::VERTEX, VK_SHADER_STAGE_VERTEX_BIT},
        {ShaderStage::TESSELLATION_CONTROL, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT},
        {ShaderStage::TESSELLATION_EVALUATION, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT},
        {ShaderStage::GEOMETRY, VK_SHADER_STAGE_GEOMETRY_BIT},
        {ShaderStage::FRAGMENT, VK_SHADER_STAGE_FRAGMENT_BIT},
        {ShaderStage::COMPUTE, VK_SHADER_STAGE_COMPUTE_BIT},
        {ShaderStage::TASK, VK_SHADER_STAGE_TASK_BIT_EXT},
        {ShaderStage::MESH, VK_SHADER_STAGE_MESH_BIT_EXT},
        {ShaderStage::RAY_GENERATION, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {ShaderStage::ANY_HIT, VK_SHADER_STAGE_ANY_HIT_BIT_KHR},
        {ShaderStage::CLOSEST_HIT, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
        {ShaderStage::MISS, VK_SHADER_STAGE_MISS_BIT_KHR},
        {ShaderStage::INTERSECTION, VK_SHADER_STAGE_INTERSECTION_BIT_KHR},
        {ShaderStage::CALLABLE, VK_SHADER_STAGE_CALLABLE_BIT_KHR},
    }};
    return translate_bits(stages, TABLE);
}

/// @brief The Vulkan stage bit of one shader stage.
[[nodiscard]] constexpr auto
to_vulkan_stage(ShaderStage stage) noexcept -> VkShaderStageFlagBits
{
    return static_cast<VkShaderStageFlagBits>(to_vulkan(stage));
}

/// @brief Vulkan descriptor type.
[[nodiscard]] constexpr auto
to_vulkan(DescriptorType type) noexcept -> VkDescriptorType
{
    switch (type)
    {
    case DescriptorType::SAMPLER: return VK_DESCRIPTOR_TYPE_SAMPLER;
    case DescriptorType::COMBINED_IMAGE_SAMPLER: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case DescriptorType::SAMPLED_IMAGE: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case DescriptorType::STORAGE_IMAGE: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case DescriptorType::UNIFORM_TEXEL_BUFFER: return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    case DescriptorType::STORAGE_TEXEL_BUFFER: return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    case DescriptorType::UNIFORM_BUFFER: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case DescriptorType::STORAGE_BUFFER: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case DescriptorType::UNIFORM_BUFFER_DYNAMIC: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    case DescriptorType::STORAGE_BUFFER_DYNAMIC: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    case DescriptorType::INPUT_ATTACHMENT: return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    case DescriptorType::ACCELERATION_STRUCTURE: return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

/// @brief Vulkan descriptor binding flags.
[[nodiscard]] constexpr auto
to_vulkan(DescriptorBindingOption options) noexcept -> VkDescriptorBindingFlags
{
    constexpr std::array<BitTranslation<DescriptorBindingOption, VkDescriptorBindingFlags>, 4> TABLE{{
        {DescriptorBindingOption::UPDATE_AFTER_BIND, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT},
        {DescriptorBindingOption::UPDATE_UNUSED_WHILE_PENDING, VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT},
        {DescriptorBindingOption::PARTIALLY_BOUND, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT},
        {DescriptorBindingOption::VARIABLE_DESCRIPTOR_COUNT, VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT},
    }};
    return translate_bits(options, TABLE);
}

/// @brief Vulkan pipeline bind point.
[[nodiscard]] constexpr auto
to_vulkan(PipelineBindPoint bind_point) noexcept -> VkPipelineBindPoint
{
    switch (bind_point)
    {
    case PipelineBindPoint::GRAPHICS: return VK_PIPELINE_BIND_POINT_GRAPHICS;
    case PipelineBindPoint::COMPUTE: return VK_PIPELINE_BIND_POINT_COMPUTE;
    case PipelineBindPoint::RAY_TRACING: return VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
    }
    return VK_PIPELINE_BIND_POINT_GRAPHICS;
}

// ------------------------------------------------------------------------------------ graphics state

/// @brief Vulkan primitive topology.
[[nodiscard]] constexpr auto
to_vulkan(PrimitiveTopology topology) noexcept -> VkPrimitiveTopology
{
    switch (topology)
    {
    case PrimitiveTopology::POINT_LIST: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case PrimitiveTopology::LINE_LIST: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::LINE_STRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PrimitiveTopology::TRIANGLE_LIST: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveTopology::TRIANGLE_FAN: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    case PrimitiveTopology::PATCH_LIST: return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

/// @brief Vulkan polygon mode.
[[nodiscard]] constexpr auto
to_vulkan(PolygonMode mode) noexcept -> VkPolygonMode
{
    switch (mode)
    {
    case PolygonMode::FILL: return VK_POLYGON_MODE_FILL;
    case PolygonMode::LINE: return VK_POLYGON_MODE_LINE;
    case PolygonMode::POINT: return VK_POLYGON_MODE_POINT;
    }
    return VK_POLYGON_MODE_FILL;
}

/// @brief Vulkan cull mode.
[[nodiscard]] constexpr auto
to_vulkan(CullMode mode) noexcept -> VkCullModeFlags
{
    switch (mode)
    {
    case CullMode::NONE: return VK_CULL_MODE_NONE;
    case CullMode::FRONT: return VK_CULL_MODE_FRONT_BIT;
    case CullMode::BACK: return VK_CULL_MODE_BACK_BIT;
    case CullMode::FRONT_AND_BACK: return VK_CULL_MODE_FRONT_AND_BACK;
    }
    return VK_CULL_MODE_NONE;
}

/// @brief Vulkan front face.
[[nodiscard]] constexpr auto
to_vulkan(FrontFace front_face) noexcept -> VkFrontFace
{
    return front_face == FrontFace::CLOCKWISE ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

/// @brief Vulkan stencil operation.
[[nodiscard]] constexpr auto
to_vulkan(StencilOperation operation) noexcept -> VkStencilOp
{
    switch (operation)
    {
    case StencilOperation::KEEP: return VK_STENCIL_OP_KEEP;
    case StencilOperation::ZERO: return VK_STENCIL_OP_ZERO;
    case StencilOperation::REPLACE: return VK_STENCIL_OP_REPLACE;
    case StencilOperation::INCREMENT_AND_CLAMP: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case StencilOperation::DECREMENT_AND_CLAMP: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case StencilOperation::INVERT: return VK_STENCIL_OP_INVERT;
    case StencilOperation::INCREMENT_AND_WRAP: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case StencilOperation::DECREMENT_AND_WRAP: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    }
    return VK_STENCIL_OP_KEEP;
}

/// @brief Vulkan stencil faces.
[[nodiscard]] constexpr auto
to_vulkan(StencilFace faces) noexcept -> VkStencilFaceFlags
{
    constexpr std::array<BitTranslation<StencilFace, VkStencilFaceFlags>, 2> TABLE{{
        {StencilFace::FRONT, VK_STENCIL_FACE_FRONT_BIT},
        {StencilFace::BACK, VK_STENCIL_FACE_BACK_BIT},
    }};
    return translate_bits(faces, TABLE);
}

/// @brief Vulkan blend factor.
[[nodiscard]] constexpr auto
to_vulkan(BlendFactor factor) noexcept -> VkBlendFactor
{
    switch (factor)
    {
    case BlendFactor::ZERO: return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::ONE: return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SOURCE_COLOR: return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::ONE_MINUS_SOURCE_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::DESTINATION_COLOR: return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::ONE_MINUS_DESTINATION_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::SOURCE_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::ONE_MINUS_SOURCE_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DESTINATION_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::ONE_MINUS_DESTINATION_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case BlendFactor::CONSTANT_COLOR: return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case BlendFactor::ONE_MINUS_CONSTANT_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case BlendFactor::CONSTANT_ALPHA: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case BlendFactor::ONE_MINUS_CONSTANT_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    case BlendFactor::SOURCE_ALPHA_SATURATE: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case BlendFactor::SECOND_SOURCE_COLOR: return VK_BLEND_FACTOR_SRC1_COLOR;
    case BlendFactor::ONE_MINUS_SECOND_SOURCE_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
    case BlendFactor::SECOND_SOURCE_ALPHA: return VK_BLEND_FACTOR_SRC1_ALPHA;
    case BlendFactor::ONE_MINUS_SECOND_SOURCE_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}

/// @brief Vulkan blend operation.
[[nodiscard]] constexpr auto
to_vulkan(BlendOperation operation) noexcept -> VkBlendOp
{
    switch (operation)
    {
    case BlendOperation::ADD: return VK_BLEND_OP_ADD;
    case BlendOperation::SUBTRACT: return VK_BLEND_OP_SUBTRACT;
    case BlendOperation::REVERSE_SUBTRACT: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOperation::MINIMUM: return VK_BLEND_OP_MIN;
    case BlendOperation::MAXIMUM: return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

/// @brief Vulkan color write mask (the bits have the same values).
[[nodiscard]] constexpr auto
to_vulkan(ColorComponent components) noexcept -> VkColorComponentFlags
{
    return static_cast<VkColorComponentFlags>(std::to_underlying(components));
}

/// @brief Vulkan vertex input rate.
[[nodiscard]] constexpr auto
to_vulkan(VertexInputRate rate) noexcept -> VkVertexInputRate
{
    return rate == VertexInputRate::INSTANCE ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
}

/// @brief Vulkan index type.
[[nodiscard]] constexpr auto
to_vulkan(IndexType type) noexcept -> VkIndexType
{
    return type == IndexType::UNSIGNED_INTEGER_16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
}

/// @brief Vulkan attachment load operation.
[[nodiscard]] constexpr auto
to_vulkan(LoadOperation operation) noexcept -> VkAttachmentLoadOp
{
    switch (operation)
    {
    case LoadOperation::LOAD: return VK_ATTACHMENT_LOAD_OP_LOAD;
    case LoadOperation::CLEAR: return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOperation::DONT_CARE: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

/// @brief Vulkan attachment store operation.
[[nodiscard]] constexpr auto
to_vulkan(StoreOperation operation) noexcept -> VkAttachmentStoreOp
{
    switch (operation)
    {
    case StoreOperation::STORE: return VK_ATTACHMENT_STORE_OP_STORE;
    case StoreOperation::DONT_CARE: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    case StoreOperation::NONE: return VK_ATTACHMENT_STORE_OP_NONE;
    }
    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

/// @brief Vulkan resolve mode.
[[nodiscard]] constexpr auto
to_vulkan(ResolveMode mode) noexcept -> VkResolveModeFlagBits
{
    switch (mode)
    {
    case ResolveMode::NONE: return VK_RESOLVE_MODE_NONE;
    case ResolveMode::AVERAGE: return VK_RESOLVE_MODE_AVERAGE_BIT;
    case ResolveMode::MINIMUM: return VK_RESOLVE_MODE_MIN_BIT;
    case ResolveMode::MAXIMUM: return VK_RESOLVE_MODE_MAX_BIT;
    case ResolveMode::SAMPLE_ZERO: return VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
    }
    return VK_RESOLVE_MODE_NONE;
}

/// @brief Vulkan fragment size of a shading rate.
[[nodiscard]] constexpr auto
to_vulkan(ShadingRate rate) noexcept -> VkExtent2D
{
    switch (rate)
    {
    case ShadingRate::ONE_BY_ONE: return {.width = 1, .height = 1};
    case ShadingRate::ONE_BY_TWO: return {.width = 1, .height = 2};
    case ShadingRate::TWO_BY_ONE: return {.width = 2, .height = 1};
    case ShadingRate::TWO_BY_TWO: return {.width = 2, .height = 2};
    case ShadingRate::TWO_BY_FOUR: return {.width = 2, .height = 4};
    case ShadingRate::FOUR_BY_TWO: return {.width = 4, .height = 2};
    case ShadingRate::FOUR_BY_FOUR: return {.width = 4, .height = 4};
    }
    return {.width = 1, .height = 1};
}

/// @brief Vulkan shading rate combiner.
[[nodiscard]] constexpr auto
to_vulkan(ShadingRateCombiner combiner) noexcept -> VkFragmentShadingRateCombinerOpKHR
{
    switch (combiner)
    {
    case ShadingRateCombiner::KEEP: return VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
    case ShadingRateCombiner::REPLACE: return VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR;
    case ShadingRateCombiner::MINIMUM: return VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MIN_KHR;
    case ShadingRateCombiner::MAXIMUM: return VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MAX_KHR;
    }
    return VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
}

// -------------------------------------------------------------------------------------------- queries

/// @brief Vulkan query type.
[[nodiscard]] constexpr auto
to_vulkan(QueryType type) noexcept -> VkQueryType
{
    switch (type)
    {
    case QueryType::OCCLUSION: return VK_QUERY_TYPE_OCCLUSION;
    case QueryType::PIPELINE_STATISTICS: return VK_QUERY_TYPE_PIPELINE_STATISTICS;
    case QueryType::TIMESTAMP: return VK_QUERY_TYPE_TIMESTAMP;
    case QueryType::ACCELERATION_STRUCTURE_COMPACTED_SIZE: return VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
    }
    return VK_QUERY_TYPE_TIMESTAMP;
}

/// @brief Vulkan pipeline statistics.
[[nodiscard]] constexpr auto
to_vulkan(PipelineStatistic statistics) noexcept -> VkQueryPipelineStatisticFlags
{
    constexpr std::array<BitTranslation<PipelineStatistic, VkQueryPipelineStatisticFlags>, 7> TABLE{{
        {PipelineStatistic::INPUT_ASSEMBLY_VERTICES, VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT},
        {PipelineStatistic::INPUT_ASSEMBLY_PRIMITIVES, VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT},
        {PipelineStatistic::VERTEX_SHADER_INVOCATIONS, VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT},
        {PipelineStatistic::CLIPPING_INVOCATIONS, VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT},
        {PipelineStatistic::CLIPPING_PRIMITIVES, VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT},
        {PipelineStatistic::FRAGMENT_SHADER_INVOCATIONS, VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT},
        {PipelineStatistic::COMPUTE_SHADER_INVOCATIONS, VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT},
    }};
    return translate_bits(statistics, TABLE);
}

// ---------------------------------------------------------------------------- acceleration structures

/// @brief Vulkan acceleration structure type.
[[nodiscard]] constexpr auto
to_vulkan(AccelerationStructureType type) noexcept -> VkAccelerationStructureTypeKHR
{
    return type == AccelerationStructureType::TOP_LEVEL ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
                                                        : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
}

/// @brief Vulkan acceleration structure build flags.
[[nodiscard]] constexpr auto
to_vulkan(AccelerationStructureBuildOption options) noexcept -> VkBuildAccelerationStructureFlagsKHR
{
    constexpr std::array<BitTranslation<AccelerationStructureBuildOption, VkBuildAccelerationStructureFlagsKHR>, 5> TABLE{{
        {AccelerationStructureBuildOption::ALLOW_UPDATE, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR},
        {AccelerationStructureBuildOption::ALLOW_COMPACTION, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR},
        {AccelerationStructureBuildOption::PREFER_FAST_TRACE, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR},
        {AccelerationStructureBuildOption::PREFER_FAST_BUILD, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR},
        {AccelerationStructureBuildOption::MINIMIZE_MEMORY, VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR},
    }};
    return translate_bits(options, TABLE);
}

/// @brief Vulkan geometry flags.
[[nodiscard]] constexpr auto
to_vulkan(GeometryOption options) noexcept -> VkGeometryFlagsKHR
{
    constexpr std::array<BitTranslation<GeometryOption, VkGeometryFlagsKHR>, 2> TABLE{{
        {GeometryOption::OPAQUE_GEOMETRY, VK_GEOMETRY_OPAQUE_BIT_KHR},
        {GeometryOption::NO_DUPLICATE_ANY_HIT, VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR},
    }};
    return translate_bits(options, TABLE);
}

/// @brief Vulkan acceleration structure copy mode.
[[nodiscard]] constexpr auto
to_vulkan(AccelerationStructureCopyMode mode) noexcept -> VkCopyAccelerationStructureModeKHR
{
    return mode == AccelerationStructureCopyMode::COMPACT ? VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR
                                                          : VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;
}

// ---------------------------------------------------------------------------------------- properties

/// @brief gfx device type.
[[nodiscard]] constexpr auto
from_vulkan(VkPhysicalDeviceType type) noexcept -> PhysicalDeviceType
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return PhysicalDeviceType::INTEGRATED_GPU;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return PhysicalDeviceType::DISCRETE_GPU;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return PhysicalDeviceType::VIRTUAL_GPU;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return PhysicalDeviceType::CPU;
    default: return PhysicalDeviceType::OTHER;
    }
}

/// @brief gfx queue capabilities of Vulkan queue flags.
[[nodiscard]] constexpr auto
from_vulkan_queue_flags(VkQueueFlags flags) noexcept -> QueueCapability
{
    QueueCapability capabilities = QueueCapability::NONE;
    if ((flags & VK_QUEUE_GRAPHICS_BIT) != 0)
    {
        capabilities |= QueueCapability::GRAPHICS;
    }
    if ((flags & VK_QUEUE_COMPUTE_BIT) != 0)
    {
        capabilities |= QueueCapability::COMPUTE;
    }

    // Graphics and compute queues always support transfers, even when they do not say so.
    if ((flags & (VK_QUEUE_TRANSFER_BIT | VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0)
    {
        capabilities |= QueueCapability::TRANSFER;
    }
    if ((flags & VK_QUEUE_SPARSE_BINDING_BIT) != 0)
    {
        capabilities |= QueueCapability::SPARSE_BINDING;
    }
    return capabilities;
}

/// @brief gfx memory properties of Vulkan memory property flags.
[[nodiscard]] constexpr auto
from_vulkan_memory_properties(VkMemoryPropertyFlags flags) noexcept -> MemoryProperty
{
    MemoryProperty properties = MemoryProperty::NONE;
    if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
    {
        properties |= MemoryProperty::DEVICE_LOCAL;
    }
    if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
    {
        properties |= MemoryProperty::HOST_VISIBLE;
    }
    if ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0)
    {
        properties |= MemoryProperty::HOST_COHERENT;
    }
    if ((flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0)
    {
        properties |= MemoryProperty::HOST_CACHED;
    }
    if ((flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) != 0)
    {
        properties |= MemoryProperty::LAZILY_ALLOCATED;
    }
    return properties;
}

} // namespace gfx::vulkan
