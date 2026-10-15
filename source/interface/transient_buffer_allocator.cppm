/// @file transient_buffer_allocator.cppm
/// @brief TransientBufferAllocator: per-frame GPU data (constants, instance data, indirect records)
///        bump-allocated from one persistently mapped buffer.
///
/// AZDO: no buffer creation, mapping or descriptor update per draw. Each frame owns a region of one
/// buffer; allocations bump an atomic offset (lock-free, from any thread) and are reached by shaders
/// through the buffer's bindless index plus the allocation's offset (or its device address).
export module gfx.interface:transient_buffer_allocator;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import :bindless;
import :buffer;
import :physical_device;
import :device;

export namespace gfx
{

/// @brief Memory for this frame's data.
struct TransientAllocation
{
    BufferHandle buffer{};                ///< Buffer holding it.
    std::uint64_t offset = 0;             ///< First byte in the buffer.
    std::span<std::byte> memory{};        ///< CPU view: write the data here.
    StorageBufferIndex buffer_index{};    ///< Bindless index of the buffer (shaders add offset).
    std::uint64_t device_address = 0;     ///< GPU address of the first byte (when the buffer has SHADER_DEVICE_ADDRESS).
};

/// @brief Bump allocator over one persistently mapped buffer split into one region per frame in flight.
///        allocate() is thread-safe; begin_frame() must not run concurrently with it.
class TransientBufferAllocator
{
public:
    /// @brief Creates the buffer: @p bytes_per_frame for each of @p frames_in_flight frames.
    /// @param usage    Every way allocations are used (bindless STORAGE by default).
    /// @param location HOST_UPLOAD by default; DEVICE_LOCAL_HOST_VISIBLE (resizable BAR) when available.
    TransientBufferAllocator(Device& device,
                             std::uint64_t bytes_per_frame,
                             std::uint32_t frames_in_flight,
                             BufferUsage usage = BufferUsage::STORAGE | BufferUsage::UNIFORM | BufferUsage::INDIRECT |
                                                 BufferUsage::VERTEX | BufferUsage::INDEX | BufferUsage::TRANSFER_SOURCE,
                             MemoryLocation location = MemoryLocation::HOST_UPLOAD,
                             std::string_view debug_name = "transient buffer")
        : m_frames_in_flight{std::max(frames_in_flight, 1U)}
    {
        // Every allocation is aligned for any binding of its buffer.
        const DeviceLimits& limits = device.physical_device().properties().limits;
        m_alignment = std::max({std::uint64_t{16}, limits.minimum_uniform_buffer_offset_alignment,
                                limits.minimum_storage_buffer_offset_alignment});
        m_frame_size = (bytes_per_frame + m_alignment - 1) / m_alignment * m_alignment;

        // One buffer, one region per frame in flight, mapped once for its whole life.
        m_buffer = device.create_buffer({.size = m_frame_size * std::max(frames_in_flight, 1U),
                                         .usage = usage,
                                         .location = location,
                                         .debug_name = debug_name});
        m_memory = m_buffer->mapped_memory();
        m_device_address = has_any(usage, BufferUsage::SHADER_DEVICE_ADDRESS) ? m_buffer->device_address() : 0;
    }

    /// @brief Starts frame @p frame: its region is reused from the start. The GPU must have finished the
    ///        frame that used the region before (FrameTimeline::begin_frame() ensures it).
    void begin_frame(std::uint64_t frame)
    {
        m_frame_base = (frame % m_frames_in_flight) * m_frame_size;
        m_used.store(0, std::memory_order_relaxed);
    }

    /// @brief @p size bytes for this frame, aligned to @p alignment (0 = the device's buffer offset
    ///        alignment). Thread-safe and lock-free.
    /// @throws OutOfMemoryError when the frame's region is full.
    [[nodiscard]] auto allocate(std::uint64_t size, std::uint64_t alignment = 0) -> TransientAllocation
    {
        // Bump the offset atomically, aligned.
        const std::uint64_t align = alignment == 0 ? m_alignment : alignment;
        std::uint64_t used = m_used.load(std::memory_order_relaxed);
        std::uint64_t start = 0;
        do
        {
            start = (used + align - 1) / align * align;
            if (start + size > m_frame_size)
            {
                throw OutOfMemoryError({
                    .backend = m_buffer->backend(),
                    .operation = "TransientBufferAllocator::allocate",
                    .domain = MemoryDomain::HOST,
                    .requested_bytes = size,
                    .details = std::format("'{}' has {} of its {} bytes per frame left; raise bytes_per_frame",
                                           m_buffer->debug_name(), m_frame_size - std::min(start, m_frame_size), m_frame_size),
                });
            }
        } while (!m_used.compare_exchange_weak(used, start + size, std::memory_order_relaxed));

        // The allocation in the buffer and in its mapping.
        const std::uint64_t offset = m_frame_base + start;
        return {.buffer = m_buffer->handle(),
                .offset = offset,
                .memory = m_memory.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size)),
                .buffer_index = m_buffer->bindless_index(),
                .device_address = m_device_address == 0 ? 0 : m_device_address + offset};
    }

    /// @brief Allocates and fills memory with the elements of @p values (an array, vector, span, ...).
    template<std::ranges::contiguous_range Values>
        requires std::is_trivially_copyable_v<std::ranges::range_value_t<Values>>
    [[nodiscard]] auto write(const Values& values, std::uint64_t alignment = 0) -> TransientAllocation
    {
        const std::span<const std::byte> bytes = std::as_bytes(std::span{std::ranges::data(values), std::ranges::size(values)});
        TransientAllocation allocation = allocate(bytes.size(), alignment);
        std::ranges::copy(bytes, allocation.memory.begin());
        return allocation;
    }

    /// @brief Allocates and fills memory with @p value (not a range or a pointer, whose bytes would not
    ///        be the data).
    template<class Value>
        requires std::is_trivially_copyable_v<Value> && (!std::ranges::range<Value>) && (!std::is_pointer_v<Value>)
    [[nodiscard]] auto write(const Value& value, std::uint64_t alignment = 0) -> TransientAllocation
    {
        return write(std::span<const Value, 1>{&value, 1}, alignment);
    }

    /// @brief Bytes allocated this frame (including alignment padding).
    [[nodiscard]] auto used_bytes() const noexcept -> std::uint64_t
    {
        return m_used.load(std::memory_order_relaxed);
    }

    /// @brief Bytes available per frame.
    [[nodiscard]] auto bytes_per_frame() const noexcept -> std::uint64_t
    {
        return m_frame_size;
    }

    /// @brief The buffer.
    [[nodiscard]] auto buffer() const noexcept -> Buffer&
    {
        return *m_buffer;
    }

private:
    std::uint32_t m_frames_in_flight;
    std::uint64_t m_alignment = 16;
    std::uint64_t m_frame_size = 0;
    std::unique_ptr<Buffer> m_buffer;
    std::span<std::byte> m_memory;
    std::uint64_t m_device_address = 0;
    std::uint64_t m_frame_base = 0;
    std::atomic<std::uint64_t> m_used{0};
};

} // namespace gfx
