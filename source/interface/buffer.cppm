/// @file buffer.cppm
/// @brief Buffer: linear GPU memory (vertices, indices, uniforms, storage, staging, indirect arguments).
export module gfx.interface:buffer;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :common_types;

export namespace gfx
{

/// @brief How to create a Buffer.
struct BufferDescription
{
    std::uint64_t size = 0;                                 ///< Bytes.
    BufferUsage usage = BufferUsage::NONE;                  ///< Every way the buffer will be used.
    MemoryLocation location = MemoryLocation::DEVICE_LOCAL; ///< Where its memory lives (when not placed).
    SharingMode sharing = SharingMode::EXCLUSIVE;           ///< Ownership across queue types.
    std::optional<MemoryPlacement> placement{};             ///< Place it in an existing DeviceMemory instead.
    std::string_view debug_name{};                          ///< Name shown by debugging tools.
};

/// @brief Linear GPU memory. Destroying the object releases the memory once the GPU no longer uses it.
class Buffer : public BackendObject
{
public:
    /// @brief Handle naming this buffer in commands, descriptors and barriers.
    [[nodiscard]] virtual auto handle() const noexcept -> BufferHandle = 0;

    /// @brief Bytes.
    [[nodiscard]] virtual auto size() const noexcept -> std::uint64_t = 0;

    /// @brief Ways the buffer may be used.
    [[nodiscard]] virtual auto usage() const noexcept -> BufferUsage = 0;

    /// @brief Where its memory lives.
    [[nodiscard]] virtual auto location() const noexcept -> MemoryLocation = 0;

    /// @brief The buffer's memory, mapped into the CPU address space (host-visible locations only).
    /// @throws InvalidUsageError for memory the CPU cannot access.
    [[nodiscard]] virtual auto mapped_memory() -> std::span<std::byte> = 0;

    /// @brief Makes CPU writes to a range of mapped memory visible to the GPU (non-coherent memory).
    virtual void flush_mapped_range(std::uint64_t offset = 0, std::uint64_t size = WHOLE_SIZE) = 0;

    /// @brief Makes GPU writes to a range of mapped memory visible to the CPU (non-coherent memory).
    virtual void invalidate_mapped_range(std::uint64_t offset = 0, std::uint64_t size = WHOLE_SIZE) = 0;

    /// @brief GPU virtual address of the buffer (requires BufferUsage::SHADER_DEVICE_ADDRESS).
    [[nodiscard]] virtual auto device_address() const -> std::uint64_t = 0;

    /// @brief The mapped memory viewed as an array of @p Element.
    template<class Element>
        requires std::is_trivially_copyable_v<Element>
    [[nodiscard]] auto mapped_elements() -> std::span<Element>
    {
        const std::span<std::byte> bytes = mapped_memory();
        return {reinterpret_cast<Element*>(bytes.data()), bytes.size() / sizeof(Element)};
    }
};

auto
Buffer::mapped_memory() -> std::span<std::byte>
{
    throw_not_implemented("Buffer::mapped_memory");
}

void
Buffer::flush_mapped_range(std::uint64_t /*offset*/,
                           std::uint64_t /*size*/)
{
    throw_not_implemented("Buffer::flush_mapped_range");
}

void
Buffer::invalidate_mapped_range(std::uint64_t /*offset*/,
                                std::uint64_t /*size*/)
{
    throw_not_implemented("Buffer::invalidate_mapped_range");
}

auto
Buffer::device_address() const -> std::uint64_t
{
    throw_not_implemented("Buffer::device_address");
}

} // namespace gfx
