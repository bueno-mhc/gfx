/// @file uploader.cppm
/// @brief Uploader: copies CPU data into device-local buffers and images through a per-frame staging
///        ring, batching every copy of a frame into the command buffer the application records.
export module gfx.interface:uploader;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import :common_types;
import :commands;
import :synchronization;
import :buffer;
import :image;
import :recorder;
import :device;
import :transient_buffer_allocator;

export namespace gfx
{

/// @brief Staged uploads. Data is copied into persistently mapped staging memory at once; the copy into
///        the destination is recorded into the caller's recorder (usually a transfer queue's), with the
///        barriers that move the destination from its state before to its state after. Not thread-safe
///        across threads sharing one recorder; staging allocation itself is thread-safe.
class Uploader
{
public:
    /// @brief Creates the staging ring: @p staging_bytes_per_frame for each of @p frames_in_flight frames.
    Uploader(Device& device, std::uint64_t staging_bytes_per_frame, std::uint32_t frames_in_flight)
        : m_staging{device, staging_bytes_per_frame, frames_in_flight, BufferUsage::TRANSFER_SOURCE, MemoryLocation::HOST_UPLOAD,
                    "upload staging"}
    {}

    /// @brief Starts frame @p frame (after FrameTimeline::begin_frame()).
    void begin_frame(std::uint64_t frame)
    {
        m_staging.begin_frame(frame);
    }

    /// @brief Copies @p data into @p destination at @p offset.
    /// @param before State of the destination range before the upload (UNDEFINED when its contents are not needed).
    /// @param after  State it must be in afterwards (e.g. SHADER_STORAGE_READ, VERTEX_BUFFER).
    template<QueueType Type>
    void upload_buffer(Recorder<Type>& recorder,
                       BufferHandle destination,
                       std::uint64_t offset,
                       std::span<const std::byte> data,
                       ResourceState before,
                       ResourceState after)
    {
        // Stage the data.
        const TransientAllocation staging = m_staging.allocate(data.size(), 16);
        std::ranges::copy(data, staging.memory.begin());

        // Copy it, between the barriers that order it with the destination's other uses.
        const std::array start{buffer_barrier(destination, before, ResourceState::TRANSFER_DESTINATION)};
        recorder.barrier({.buffer_barriers = start});
        const std::array region{BufferCopyRegion{.source_offset = staging.offset, .destination_offset = offset, .size = data.size()}};
        recorder.copy_buffer(staging.buffer, destination, region);
        const std::array finish{buffer_barrier(destination, ResourceState::TRANSFER_DESTINATION, after)};
        recorder.barrier({.buffer_barriers = finish});
    }

    /// @brief Copies the elements of @p values (an array, vector, span, ...) into @p destination at @p offset.
    template<QueueType Type, std::ranges::contiguous_range Values>
        requires std::is_trivially_copyable_v<std::ranges::range_value_t<Values>>
    void upload_buffer(Recorder<Type>& recorder,
                       BufferHandle destination,
                       std::uint64_t offset,
                       const Values& values,
                       ResourceState before,
                       ResourceState after)
    {
        upload_buffer(recorder, destination, offset, std::as_bytes(std::span{std::ranges::data(values), std::ranges::size(values)}),
                      before, after);
    }

    /// @brief Copies tightly packed @p texels into one mip level (and layers) of @p image.
    /// @param before      State of the subresources before the upload.
    /// @param after       State they must be in afterwards (e.g. SHADER_SAMPLED_READ).
    /// @param subresource Mip level and layers written.
    /// @param offset      First texel written.
    /// @param extent      Texels written (empty = the whole mip level).
    /// @throws InvalidUsageError at ValidationLevel::COMMANDS when @p texels does not hold exactly the
    ///         region's bytes.
    template<QueueType Type>
    void upload_image(Recorder<Type>& recorder,
                      const Image& image,
                      std::span<const std::byte> texels,
                      ResourceState before,
                      ResourceState after,
                      const ImageSubresourceLayers& subresource = {},
                      Offset offset = {},
                      std::optional<Extent> extent = {})
    {
        // The region: the whole mip level unless given.
        const FormatProperties& format = format_properties(image.format());
        const auto level_size = [&](std::uint32_t size)
        {
            return std::max(size >> subresource.mip_level, 1U);
        };
        const Extent written = extent.value_or(Extent{.width = level_size(image.extent().width),
                                                      .height = level_size(image.extent().height),
                                                      .depth = level_size(image.extent().depth)});
        const std::uint64_t block_width = format.block_width;
        const std::uint64_t block_height = format.block_height;
        const std::uint64_t block_bytes = format.bytes_per_block;
        const std::uint64_t blocks_x = (written.width + block_width - 1) / block_width;
        const std::uint64_t blocks_y = (written.height + block_height - 1) / block_height;
        const std::uint64_t expected = blocks_x * blocks_y * written.depth * subresource.array_layer_count * block_bytes;
        if (image.validates(ValidationLevel::COMMANDS) && texels.size() != expected)
        {
            throw InvalidUsageError({
                .backend = image.backend(),
                .operation = "Uploader::upload_image",
                .problem = std::format("'{}' mip level {}: {}x{}x{} texels of {} take {} bytes, but {} were given", image.debug_name(),
                                       subresource.mip_level, written.width, written.height, written.depth, image.format(),
                                       expected, texels.size()),
                .suggestion = "pass tightly packed texels of exactly the uploaded region",
            });
        }

        // Stage the texels, aligned for the format.
        const std::uint64_t alignment = std::lcm(std::uint64_t{16}, block_bytes);
        const TransientAllocation staging = m_staging.allocate(texels.size(), alignment);
        std::ranges::copy(texels, staging.memory.begin());

        // Transition, copy, transition.
        const ImageSubresourceRange range{.aspects = subresource.aspects,
                                          .base_mip_level = subresource.mip_level,
                                          .mip_level_count = 1,
                                          .base_array_layer = subresource.base_array_layer,
                                          .array_layer_count = subresource.array_layer_count};
        const std::array start{image_barrier(image.handle(), before, ResourceState::TRANSFER_DESTINATION, range)};
        recorder.barrier({.image_barriers = start});
        const std::array region{BufferImageCopyRegion{.buffer_offset = staging.offset,
                                                      .buffer_row_length = 0,
                                                      .buffer_image_height = 0,
                                                      .image_subresource = subresource,
                                                      .image_offset = offset,
                                                      .image_extent = written}};
        recorder.copy_buffer_to_image(staging.buffer, image.handle(), ImageLayout::TRANSFER_DESTINATION, region);
        const std::array finish{image_barrier(image.handle(), ResourceState::TRANSFER_DESTINATION, after, range)};
        recorder.barrier({.image_barriers = finish});
    }

    /// @brief The staging ring.
    [[nodiscard]] auto staging() noexcept -> TransientBufferAllocator&
    {
        return m_staging;
    }

private:
    TransientBufferAllocator m_staging;
};

} // namespace gfx
