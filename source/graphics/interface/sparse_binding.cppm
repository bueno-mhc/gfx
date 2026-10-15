/// @file sparse_binding.cppm
/// @brief Sparse (partially resident) resources: memory bound to buffer ranges and image tiles on demand
///        (texture streaming, virtual texturing), with Queue::bind_sparse().
export module gfx.interface:sparse_binding;

import std;
import gfx.enums;
import gfx.handles;
import :common_types;
import :synchronization;

export namespace gfx
{

/// @brief How a sparse image is divided into tiles (Device::sparse_image_requirements()).
struct SparseImageRequirements
{
    ImageAspect aspects = ImageAspect::COLOR;  ///< Aspects the tiles cover.
    Extent tile_extent{};                      ///< Texels of one tile.
    std::uint32_t mip_tail_first_level = 0;    ///< First mip level stored in the mip tail.
    std::uint64_t mip_tail_size = 0;           ///< Bytes of the mip tail (bound as one opaque range).
    std::uint64_t mip_tail_offset = 0;         ///< Resource offset of the mip tail.
    std::uint64_t mip_tail_stride = 0;         ///< Bytes between the mip tails of consecutive layers (0 = one tail).
    std::uint64_t tile_size = 0;               ///< Bytes of memory per tile.
};

/// @brief Memory bound to (or, with a null memory, unbound from) a byte range of a sparse buffer.
struct SparseBufferBind
{
    BufferHandle buffer{};              ///< Sparse buffer.
    std::uint64_t resource_offset = 0;  ///< First byte of the buffer (a multiple of the tile size).
    std::uint64_t size = 0;             ///< Bytes bound.
    DeviceMemoryHandle memory{};        ///< Memory (null = unbind).
    std::uint64_t memory_offset = 0;    ///< First byte of the memory.
};

/// @brief Memory bound to a byte range of a sparse image's opaque storage (mip tails, metadata).
struct SparseImageOpaqueBind
{
    ImageHandle image{};                ///< Sparse image.
    std::uint64_t resource_offset = 0;  ///< First byte (SparseImageRequirements::mip_tail_offset, ...).
    std::uint64_t size = 0;             ///< Bytes bound.
    DeviceMemoryHandle memory{};        ///< Memory (null = unbind).
    std::uint64_t memory_offset = 0;    ///< First byte of the memory.
};

/// @brief Memory bound to a box of tiles of a sparse image.
struct SparseImageBind
{
    ImageHandle image{};                        ///< Sparse image (ImageCreationOption::SPARSE_RESIDENCY).
    ImageSubresourceLayers subresource{};       ///< Aspect, mip level and first layer.
    Offset offset{};                            ///< First texel (a multiple of the tile extent).
    Extent extent{};                            ///< Texels bound (whole tiles, or up to the image's edge).
    DeviceMemoryHandle memory{};                ///< Memory (null = unbind).
    std::uint64_t memory_offset = 0;            ///< First byte of the memory.
};

/// @brief One sparse binding operation of Queue::bind_sparse(), ordered with timeline semaphores.
struct SparseBindDescription
{
    std::span<const SparseBufferBind> buffer_binds{};             ///< Buffer ranges.
    std::span<const SparseImageOpaqueBind> image_opaque_binds{};  ///< Opaque image ranges.
    std::span<const SparseImageBind> image_binds{};               ///< Image tiles.
    std::span<const TimelineSemaphoreWait> timeline_waits{};      ///< Waited before binding.
    std::span<const TimelineSemaphoreSignal> timeline_signals{};  ///< Signaled once bound.
};

} // namespace gfx
