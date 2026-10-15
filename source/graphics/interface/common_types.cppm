/// @file common_types.cppm
/// @brief Small value types shared by the interfaces: viewports, image subresources and memory
///        requirements, plus the value types and the native window handle of gfx.core (versions,
///        extents, offsets, rectangles, NativeWindowHandle), re-exported here.
export module gfx.interface:common_types;

import std;
import gfx.enums;
import gfx.handles;
export import gfx.core.types;
export import gfx.core.native_window;

export namespace gfx
{

/// @brief Transformation from normalized device coordinates to framebuffer coordinates.
struct Viewport
{
    float x = 0.0F;             ///< Left edge.
    float y = 0.0F;             ///< Top edge.
    float width = 0.0F;         ///< Width in pixels.
    float height = 0.0F;        ///< Height in pixels.
    float minimum_depth = 0.0F; ///< Depth mapped from z = 0.
    float maximum_depth = 1.0F; ///< Depth mapped from z = 1.

    constexpr bool operator==(const Viewport&) const = default;
};

/// @brief Mip levels and array layers of an image, for views and barriers.
struct ImageSubresourceRange
{
    ImageAspect aspects = ImageAspect::COLOR;             ///< Aspects included.
    std::uint32_t base_mip_level = 0;                     ///< First mip level.
    std::uint32_t mip_level_count = REMAINING_MIP_LEVELS; ///< Number of mip levels.
    std::uint32_t base_array_layer = 0;                   ///< First array layer.
    std::uint32_t array_layer_count = REMAINING_ARRAY_LAYERS; ///< Number of array layers.

    constexpr bool operator==(const ImageSubresourceRange&) const = default;
};

/// @brief One mip level and a range of array layers of an image, for copies.
struct ImageSubresourceLayers
{
    ImageAspect aspects = ImageAspect::COLOR; ///< Aspects copied.
    std::uint32_t mip_level = 0;              ///< Mip level.
    std::uint32_t base_array_layer = 0;       ///< First array layer.
    std::uint32_t array_layer_count = 1;      ///< Number of array layers.

    constexpr bool operator==(const ImageSubresourceLayers&) const = default;
};

/// @brief Source of each component of an image view.
struct ComponentMapping
{
    ComponentSwizzle red = ComponentSwizzle::IDENTITY;   ///< Source of red.
    ComponentSwizzle green = ComponentSwizzle::IDENTITY; ///< Source of green.
    ComponentSwizzle blue = ComponentSwizzle::IDENTITY;  ///< Source of blue.
    ComponentSwizzle alpha = ComponentSwizzle::IDENTITY; ///< Source of alpha.

    constexpr bool operator==(const ComponentMapping&) const = default;
};

/// @brief Memory a resource needs, as reported by the device.
struct MemoryRequirements
{
    std::uint64_t size = 0;                ///< Bytes.
    std::uint64_t alignment = 1;           ///< Required alignment of the offset in a DeviceMemory.
    std::uint32_t memory_type_bits = 0;    ///< Memory types (one bit each) the resource may use.

    constexpr bool operator==(const MemoryRequirements&) const = default;
};

/// @brief Places a resource inside an existing DeviceMemory allocation (render graph aliasing).
struct MemoryPlacement
{
    DeviceMemoryHandle memory; ///< Allocation that backs the resource.
    std::uint64_t offset = 0;  ///< Byte offset inside the allocation (multiple of the required alignment).

    constexpr bool operator==(const MemoryPlacement&) const = default;
};

} // namespace gfx
