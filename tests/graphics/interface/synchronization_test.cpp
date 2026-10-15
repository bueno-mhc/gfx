// Barriers derived from ResourceState pairs, as a render graph computes them.
#include <gtest/gtest.h>

import std;
import gfx;

namespace
{

using namespace gfx;

TEST(SynchronizationTest, ImageBarriersFollowTheStates)
{
    constexpr ImageBarrier barrier = image_barrier(ImageHandle{}, ResourceState::COLOR_ATTACHMENT_WRITE,
                                                   ResourceState::FRAGMENT_SHADER_SAMPLED_READ);
    static_assert(barrier.old_layout == ImageLayout::COLOR_ATTACHMENT);
    static_assert(barrier.new_layout == ImageLayout::SHADER_READ_ONLY);
    static_assert(barrier.source_stages == PipelineStage::COLOR_ATTACHMENT_OUTPUT);
    static_assert(barrier.source_access == MemoryAccess::COLOR_ATTACHMENT_WRITE);
    static_assert(barrier.destination_stages == PipelineStage::FRAGMENT_SHADER);
    static_assert(barrier.destination_access == MemoryAccess::SHADER_SAMPLED_READ);
    static_assert(barrier.range == ImageSubresourceRange{});
    SUCCEED();
}

TEST(SynchronizationTest, BufferBarriersFollowTheStates)
{
    constexpr BufferBarrier barrier = buffer_barrier(BufferHandle{}, ResourceState::COMPUTE_SHADER_STORAGE_WRITE,
                                                     ResourceState::VERTEX_BUFFER);
    static_assert(barrier.source_stages == PipelineStage::COMPUTE_SHADER);
    static_assert(barrier.destination_stages == PipelineStage::VERTEX_INPUT);
    static_assert(barrier.destination_access == MemoryAccess::VERTEX_ATTRIBUTE_READ);
    static_assert(barrier.size == WHOLE_SIZE && !barrier.source_queue.has_value());
    SUCCEED();
}

TEST(SynchronizationTest, BarriersAreNeededOnlyForHazardsAndLayoutChanges)
{
    using enum ResourceState;
    constexpr ResourceCategory image = ResourceCategory::IMAGE;
    constexpr ResourceCategory buffer = ResourceCategory::BUFFER;

    static_assert(!needs_barrier(FRAGMENT_SHADER_SAMPLED_READ, COMPUTE_SHADER_SAMPLED_READ, image),
                  "read after read in the same layout");
    static_assert(needs_barrier(COMPUTE_SHADER_STORAGE_WRITE, FRAGMENT_SHADER_SAMPLED_READ, buffer), "read after write");
    static_assert(needs_barrier(FRAGMENT_SHADER_SAMPLED_READ, COMPUTE_SHADER_STORAGE_WRITE, buffer), "write after read");
    static_assert(needs_barrier(COMPUTE_SHADER_STORAGE_READ, FRAGMENT_SHADER_SAMPLED_READ, image), "layout change");
    static_assert(!needs_barrier(COMPUTE_SHADER_STORAGE_READ, FRAGMENT_SHADER_SAMPLED_READ, buffer), "buffers have no layout");
    static_assert(needs_barrier(UNDEFINED, COLOR_ATTACHMENT_WRITE, image), "first use of an image");
    static_assert(!needs_barrier(UNDEFINED, VERTEX_BUFFER, buffer), "first read of a buffer");
    SUCCEED();
}

} // namespace
