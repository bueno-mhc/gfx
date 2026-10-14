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
