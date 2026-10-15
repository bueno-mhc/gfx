/// @file device_memory.cppm
/// @brief DeviceMemory: an explicit memory allocation in which resources can be placed (aliasing).
export module gfx.interface:device_memory;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;

export namespace gfx
{

/// @brief How to allocate a DeviceMemory.
struct DeviceMemoryDescription
{
    std::uint64_t size = 0;                                ///< Bytes.
    MemoryLocation location = MemoryLocation::DEVICE_LOCAL;///< Where the memory lives.
    std::uint32_t memory_type_bits = ~0U;                  ///< Allowed memory types (from MemoryRequirements).
    std::string_view debug_name{};                         ///< Name shown by debugging tools.
};

/// @brief Budget and usage of one memory heap (Device::memory_budget(), DeviceFeature::MEMORY_BUDGET):
///        how much this process may use before the driver starts evicting or failing allocations.
struct MemoryHeapBudget
{
    MemoryProperty properties = MemoryProperty::NONE; ///< DEVICE_LOCAL for video memory, NONE for system memory.
    std::uint64_t size = 0;                           ///< Bytes of the heap.
    std::uint64_t budget = 0;                         ///< Bytes this process may use (shared with other processes).
    std::uint64_t usage = 0;                          ///< Bytes this process uses.
};

/// @brief The library's own allocations (Device::memory_statistics()).
struct MemoryStatistics
{
    std::uint64_t block_count = 0;      ///< Memory blocks allocated from the driver.
    std::uint64_t allocation_count = 0; ///< Sub-allocations placed in them (buffers, images, ...).
    std::uint64_t block_bytes = 0;      ///< Bytes of the blocks.
    std::uint64_t allocation_bytes = 0; ///< Bytes of the sub-allocations (the rest is free or fragmentation).
};

/// @brief A block of device memory. Buffers and images placed in it (BufferDescription::placement,
///        ImageDescription::placement) may alias each other, as render graphs do with transient resources.
class DeviceMemory : public BackendObject
{
public:
    /// @brief Handle naming this allocation.
    [[nodiscard]] virtual auto handle() const noexcept -> DeviceMemoryHandle = 0;

    /// @brief Bytes allocated.
    [[nodiscard]] virtual auto size() const noexcept -> std::uint64_t = 0;

    /// @brief Where the memory lives.
    [[nodiscard]] virtual auto location() const noexcept -> MemoryLocation = 0;
};

} // namespace gfx
