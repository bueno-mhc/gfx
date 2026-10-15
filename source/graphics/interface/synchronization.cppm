/// @file synchronization.cppm
/// @brief Barriers (synchronization2 model), their derivation from ResourceState pairs for render
///        graphs, and the semaphore waits and signals of queue submissions.
export module gfx.interface:synchronization;

import std;
import gfx.enums;
import gfx.handles;
import :common_types;

export namespace gfx
{

/// @brief Memory dependency between earlier and later commands, for every resource.
struct GlobalMemoryBarrier
{
    PipelineStage source_stages = PipelineStage::NONE;      ///< Stages that must complete first.
    MemoryAccess source_access = MemoryAccess::NONE;        ///< Writes to make available.
    PipelineStage destination_stages = PipelineStage::NONE; ///< Stages that wait.
    MemoryAccess destination_access = MemoryAccess::NONE;   ///< Accesses that must see the writes.

    constexpr bool operator==(const GlobalMemoryBarrier&) const = default;
};

/// @brief Memory dependency on a range of a buffer, optionally transferring it between queue types.
struct BufferBarrier
{
    BufferHandle buffer{};                                   ///< Buffer.
    std::uint64_t offset = 0;                                ///< First byte.
    std::uint64_t size = WHOLE_SIZE;                         ///< Bytes covered.
    PipelineStage source_stages = PipelineStage::NONE;       ///< Stages that must complete first.
    MemoryAccess source_access = MemoryAccess::NONE;         ///< Writes to make available.
    PipelineStage destination_stages = PipelineStage::NONE;  ///< Stages that wait.
    MemoryAccess destination_access = MemoryAccess::NONE;    ///< Accesses that must see the writes.
    std::optional<QueueType> source_queue{};                 ///< Releasing queue type (ownership transfer).
    std::optional<QueueType> destination_queue{};            ///< Acquiring queue type (ownership transfer).

    constexpr bool operator==(const BufferBarrier&) const = default;
};

/// @brief Memory dependency and layout transition on subresources of an image.
struct ImageBarrier
{
    ImageHandle image{};                                     ///< Image.
    ImageSubresourceRange range{};                           ///< Mip levels, layers and aspects covered.
    ImageLayout old_layout = ImageLayout::UNDEFINED;         ///< Layout before (UNDEFINED discards the contents).
    ImageLayout new_layout = ImageLayout::UNDEFINED;         ///< Layout after.
    PipelineStage source_stages = PipelineStage::NONE;       ///< Stages that must complete first.
    MemoryAccess source_access = MemoryAccess::NONE;         ///< Writes to make available.
    PipelineStage destination_stages = PipelineStage::NONE;  ///< Stages that wait.
    MemoryAccess destination_access = MemoryAccess::NONE;    ///< Accesses that must see the writes.
    std::optional<QueueType> source_queue{};                 ///< Releasing queue type (ownership transfer).
    std::optional<QueueType> destination_queue{};            ///< Acquiring queue type (ownership transfer).

    constexpr bool operator==(const ImageBarrier&) const = default;
};

/// @brief Barriers recorded together by Recorder::barrier().
struct BarrierBatch
{
    std::span<const GlobalMemoryBarrier> memory_barriers{}; ///< Global memory barriers.
    std::span<const BufferBarrier> buffer_barriers{};       ///< Buffer barriers.
    std::span<const ImageBarrier> image_barriers{};         ///< Image barriers.
};

/// @brief Whether a resource used in state @p before needs a barrier before being used in @p after:
///        always after or before a write, and for images whenever the layout changes.
[[nodiscard]] constexpr auto
needs_barrier(ResourceState before,
              ResourceState after,
              ResourceCategory category) noexcept -> bool
{
    const ResourceStateSynchronization& earlier = resource_state_synchronization(before);
    const ResourceStateSynchronization& later = resource_state_synchronization(after);

    // Images change layout through barriers.
    if (has_any(category, ResourceCategory::IMAGE) && earlier.layout != later.layout && later.layout != ImageLayout::UNDEFINED)
    {
        return true;
    }

    // Reads after reads need no synchronization; every hazard involving a write does.
    return earlier.writes || later.writes;
}

/// @brief Barrier moving a buffer from state @p before to state @p after.
[[nodiscard]] constexpr auto
buffer_barrier(BufferHandle buffer,
               ResourceState before,
               ResourceState after) noexcept -> BufferBarrier
{
    const ResourceStateSynchronization& earlier = resource_state_synchronization(before);
    const ResourceStateSynchronization& later = resource_state_synchronization(after);
    return {
        .buffer = buffer,
        .source_stages = earlier.stages,
        .source_access = earlier.access,
        .destination_stages = later.stages,
        .destination_access = later.access,
    };
}

/// @brief Barrier moving image subresources from state @p before to state @p after (including the
///        layout transition the states imply).
[[nodiscard]] constexpr auto
image_barrier(ImageHandle image,
              ResourceState before,
              ResourceState after,
              ImageSubresourceRange range = {}) noexcept -> ImageBarrier
{
    const ResourceStateSynchronization& earlier = resource_state_synchronization(before);
    const ResourceStateSynchronization& later = resource_state_synchronization(after);
    return {
        .image = image,
        .range = range,
        .old_layout = earlier.layout,
        .new_layout = later.layout,
        .source_stages = earlier.stages,
        .source_access = earlier.access,
        .destination_stages = later.stages,
        .destination_access = later.access,
    };
}

/// @brief Wait on a binary semaphore (swapchain acquisition) before the given stages run.
struct SemaphoreWait
{
    SemaphoreHandle semaphore{};                       ///< Semaphore.
    PipelineStage stages = PipelineStage::ALL_COMMANDS;///< Stages that wait.
};

/// @brief Signal of a binary semaphore (swapchain presentation) after the given stages complete.
struct SemaphoreSignal
{
    SemaphoreHandle semaphore{};                       ///< Semaphore.
    PipelineStage stages = PipelineStage::ALL_COMMANDS;///< Stages that must complete.
};

/// @brief Wait until a timeline semaphore reaches a value before the given stages run.
struct TimelineSemaphoreWait
{
    TimelineSemaphoreHandle semaphore{};               ///< Semaphore.
    std::uint64_t value = 0;                           ///< Value to reach.
    PipelineStage stages = PipelineStage::ALL_COMMANDS;///< Stages that wait.
};

/// @brief Set a timeline semaphore to a value once the given stages complete.
struct TimelineSemaphoreSignal
{
    TimelineSemaphoreHandle semaphore{};               ///< Semaphore.
    std::uint64_t value = 0;                           ///< Value to set (greater than its current value).
    PipelineStage stages = PipelineStage::ALL_COMMANDS;///< Stages that must complete.
};

} // namespace gfx
