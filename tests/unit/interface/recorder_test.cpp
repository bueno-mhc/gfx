// Typed command recording: what each queue type may record, RAII begin/end, push constants checked
// against the pipeline layout, and rendering passes recorded in parallel by several threads.
#include <gtest/gtest.h>

import std;
import gfx;
import gfx.tests.fake_backend;

namespace
{

using namespace gfx;
using gfx::tests::FakeCommandBuffer;
using gfx::tests::FakePipelineLayout;

struct BlurPush
{
    float texel[2];
    float radius;
};

// What a recorder or scope type can record, as concepts (requires-expressions need a template).
template<class Type>
concept CanDispatch = requires(Type& recorder) { recorder.dispatch(1U, 1U, 1U); };

template<class Type>
concept CanCopyBuffers = requires(Type& recorder) { recorder.copy_buffer(BufferHandle{}, BufferHandle{}, std::span<const BufferCopyRegion>{}); };

template<class Type>
concept CanBarrier = requires(Type& recorder) { recorder.barrier(BarrierBatch{}); };

template<class Type>
concept CanBeginRendering = requires(Type& recorder) { recorder.begin_rendering(RenderingDescription{}); };

template<class Type>
concept CanBeginParallelRendering = requires(Type& recorder) { recorder.begin_parallel_rendering(RenderingDescription{}); };

template<class Type>
concept CanDraw = requires(Type& recorder) { recorder.draw(DrawArguments{}); };

template<class Type, class Data>
concept CanPushConstants = requires(Type& recorder, const PipelineLayout& layout, const Data& data) {
    recorder.push_constants(layout, data);
};

/// A layout with push-constant bytes [0, 12) for compute.
auto compute_push_layout() -> FakePipelineLayout
{
    return FakePipelineLayout{1, {.push_constant_ranges = {{.stages = ShaderStage::COMPUTE, .offset = 0, .size = 12}}}, "blur"};
}

TEST(RecorderTest, QueueTypesDetermineWhatCanBeRecorded)
{
    static_assert(CanDispatch<GraphicsRecorder> && CanDispatch<ComputeRecorder> && !CanDispatch<TransferRecorder>);
    static_assert(CanCopyBuffers<GraphicsRecorder> && CanCopyBuffers<ComputeRecorder> && CanCopyBuffers<TransferRecorder>);
    static_assert(CanBeginRendering<GraphicsRecorder> && !CanBeginRendering<ComputeRecorder> && !CanBeginRendering<TransferRecorder>);
    static_assert(CanBeginParallelRendering<GraphicsRecorder> && !CanBeginParallelRendering<ComputeRecorder>);
    static_assert(!CanDraw<GraphicsRecorder>, "draws are recorded inside rendering passes only");
    static_assert(CanDraw<RenderingScope<QueueType::GRAPHICS>> && CanDraw<RenderingPartRecorder>);
    static_assert(!CanDraw<ParallelRenderingScope>, "the primary of a parallel pass only executes the parts");
    static_assert(!CanBarrier<RenderingPartRecorder> && !CanCopyBuffers<RenderingPartRecorder>,
                  "a part holds only commands allowed inside a rendering pass");
    static_assert(CanPushConstants<ComputeRecorder, BlurPush> && CanPushConstants<RenderingPartRecorder, BlurPush>);
    static_assert(!CanPushConstants<TransferRecorder, BlurPush>);
    static_assert(!CanPushConstants<ComputeRecorder, const BlurPush*>, "a pointer's bytes are an address");
    static_assert(!CanPushConstants<ComputeRecorder, std::span<const std::byte>>, "a span's bytes are an address");
    static_assert(ComputeRecorder::CAPABILITIES == (QueueCapability::COMPUTE | QueueCapability::TRANSFER));
    SUCCEED();
}

TEST(RecorderTest, RecordingBeginsAndEndsWithTheRecorder)
{
    FakeCommandBuffer command_buffer{QueueType::COMPUTE};
    {
        ComputeRecorder recorder{command_buffer};
        recorder.dispatch(4, 2);
    }
    EXPECT_EQ(command_buffer.log(), (std::vector<std::string>{"begin", "dispatch 4 2 1", "end"}));
}

TEST(RecorderTest, RenderingScopeHoldsTheDrawsOfOnePass)
{
    FakeCommandBuffer command_buffer{QueueType::GRAPHICS};
    const std::array<ColorAttachment, 1> attachments{ColorAttachment{}};
    {
        GraphicsRecorder recorder{command_buffer};
        recorder.begin_debug_label("frame");
        {
            RenderingScope<QueueType::GRAPHICS> rendering = recorder.begin_rendering({.color_attachments = attachments});
            rendering.set_viewport({.width = 640.0F, .height = 480.0F});
            rendering.draw({.vertex_count = 3});
        }
        recorder.end_debug_label();
    }
    EXPECT_EQ(command_buffer.log(), (std::vector<std::string>{"begin", "begin_debug_label frame", "begin_rendering 1",
                                                              "set_viewport 640 480", "draw 3 1", "end_rendering",
                                                              "end_debug_label", "end"}));
}

TEST(RecorderTest, ComputeBindingsUseTheComputeBindPoint)
{
    FakeCommandBuffer command_buffer{QueueType::GRAPHICS};
    {
        ComputeRecorder recorder{command_buffer}; // a graphics queue also runs compute work
        const std::array<DescriptorSetHandle, 2> sets{};
        recorder.bind_descriptor_sets(PipelineLayoutHandle{}, 1, sets);
    }
    EXPECT_EQ(command_buffer.log()[1], "bind_descriptor_sets COMPUTE 1 2");
}

TEST(RecorderTest, PushConstantsUseTheStagesOfTheLayoutsRanges)
{
    const FakePipelineLayout layout = compute_push_layout();
    FakeCommandBuffer command_buffer{QueueType::COMPUTE};
    {
        ComputeRecorder recorder{command_buffer};
        recorder.push_constants(layout, BlurPush{{0.5F, 0.25F}, 3.0F});
    }
    EXPECT_EQ(command_buffer.log()[1], "push_constants COMPUTE 0 12");

    // The bytes are the struct's bytes.
    BlurPush read_back{};
    ASSERT_EQ(command_buffer.push_constant_bytes().size(), sizeof(BlurPush));
    std::memcpy(&read_back, command_buffer.push_constant_bytes().data(), sizeof(BlurPush));
    EXPECT_EQ(read_back.radius, 3.0F);

    // A write overlapping two ranges names both ranges' stages.
    const FakePipelineLayout shared{2,
                                    {.push_constant_ranges = {{.stages = ShaderStage::VERTEX, .offset = 0, .size = 16},
                                                              {.stages = ShaderStage::FRAGMENT, .offset = 16, .size = 16}}},
                                    "shared"};
    EXPECT_EQ(push_constant_stages(shared, 8, 16), ShaderStage::VERTEX | ShaderStage::FRAGMENT);
    EXPECT_EQ(push_constant_stages(shared, 16, 4), ShaderStage::FRAGMENT);
}

TEST(RecorderTest, PushConstantsOutsideTheLayoutAreRejected)
{
    const FakePipelineLayout layout = compute_push_layout();
    FakeCommandBuffer command_buffer{QueueType::COMPUTE};
    ComputeRecorder recorder{command_buffer};
    try
    {
        recorder.push_constants(layout, BlurPush{}, 4); // bytes [4, 16) of a [0, 12) range
        FAIL() << "InvalidUsageError was not thrown";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_EQ(error.problem(), "push-constant bytes [4, 16) are not all inside the push-constant ranges of pipeline "
                                   "layout 'blur' (byte 12 is outside every range)");
        EXPECT_EQ(error.suggestion(), "its ranges are [0, 12) for COMPUTE");
    }
    EXPECT_EQ(command_buffer.log(), (std::vector<std::string>{"begin"})) << "nothing was recorded";
}

TEST(RecorderTest, CommandBuffersOfWeakerQueuesAreRejected)
{
    FakeCommandBuffer transfer_command_buffer{QueueType::TRANSFER};
    try
    {
        ComputeRecorder recorder{transfer_command_buffer};
        FAIL() << "InvalidUsageError was not thrown";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("a COMPUTE recorder needs a command buffer whose queue runs "
                                                      "COMPUTE | TRANSFER work, but 'fake' belongs to a TRANSFER queue"),
                  std::string_view::npos)
            << error.what();
    }
    EXPECT_TRUE(transfer_command_buffer.log().empty()) << "recording must not begin";

    FakeCommandBuffer compute_command_buffer{QueueType::COMPUTE};
    EXPECT_THROW(GraphicsRecorder{compute_command_buffer}, InvalidUsageError);
}

TEST(RecorderTest, SecondaryCommandBuffersOnlyRecordRenderingParts)
{
    FakeCommandBuffer secondary{QueueType::GRAPHICS, CommandBufferLevel::SECONDARY};
    EXPECT_THROW(GraphicsRecorder{secondary}, InvalidUsageError);
    EXPECT_TRUE(secondary.log().empty());
}

TEST(RecorderTest, RenderingPassesAreRecordedInParallel)
{
    FakeCommandBuffer primary{QueueType::GRAPHICS, CommandBufferLevel::PRIMARY, 1};
    std::vector<std::unique_ptr<CommandBuffer>> parts;
    parts.push_back(std::make_unique<FakeCommandBuffer>(QueueType::GRAPHICS, CommandBufferLevel::SECONDARY, 2));
    parts.push_back(std::make_unique<FakeCommandBuffer>(QueueType::GRAPHICS, CommandBufferLevel::SECONDARY, 3));
    const std::array<ColorAttachment, 2> attachments{ColorAttachment{}, ColorAttachment{}};
    {
        GraphicsRecorder recorder{primary};
        ParallelRenderingScope pass = recorder.begin_parallel_rendering({.color_attachments = attachments});
        EXPECT_EQ(pass.inheritance().primary, primary.handle());
        EXPECT_EQ(pass.inheritance().pass, 0U);
        EXPECT_EQ(pass.inheritance().color_attachment_formats().size(), 2U);

        // Each thread records its own part; the parts are executed in their order, not in finishing order.
        {
            std::vector<std::jthread> threads;
            for (std::size_t index = 0; index < parts.size(); ++index)
            {
                threads.emplace_back([&parts, &pass, index]
                {
                    RenderingPartRecorder part{*parts[index], pass.inheritance()};
                    part.set_viewport({.width = 640.0F, .height = 480.0F});
                    part.draw({.vertex_count = static_cast<std::uint32_t>(3 * (index + 1))});
                });
            }
        }
        pass.execute(parts);
    }
    EXPECT_EQ(primary.log(), (std::vector<std::string>{"begin", "begin_parallel_rendering 2",
                                                       "execute_commands CommandBuffer(index 2, generation 1) "
                                                       "CommandBuffer(index 3, generation 1)",
                                                       "end_rendering", "end"}));
    const auto& second = dynamic_cast<const FakeCommandBuffer&>(*parts[1]);
    EXPECT_EQ(second.log(), (std::vector<std::string>{"begin_rendering_part CommandBuffer(index 1, generation 1) pass 0",
                                                      "set_viewport 640 480", "draw 6 1", "end"}));
}

TEST(RecorderTest, RenderingPartsNeedSecondaryGraphicsCommandBuffersAndARealPass)
{
    FakeCommandBuffer primary{QueueType::GRAPHICS};
    const RenderingInheritance inheritance{.primary = primary.handle()};

    FakeCommandBuffer primary_part{QueueType::GRAPHICS, CommandBufferLevel::PRIMARY};
    EXPECT_THROW(RenderingPartRecorder(primary_part, inheritance), InvalidUsageError);
    FakeCommandBuffer compute_part{QueueType::COMPUTE, CommandBufferLevel::SECONDARY};
    EXPECT_THROW(RenderingPartRecorder(compute_part, inheritance), InvalidUsageError);
    FakeCommandBuffer orphan_part{QueueType::GRAPHICS, CommandBufferLevel::SECONDARY};
    try
    {
        RenderingPartRecorder part{orphan_part, RenderingInheritance{}};
        FAIL() << "InvalidUsageError was not thrown";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_EQ(error.problem(), "the inheritance does not describe a parallel rendering pass");
    }
    EXPECT_TRUE(primary_part.log().empty() && compute_part.log().empty() && orphan_part.log().empty())
        << "recording must not begin";

    // Executing a primary command buffer as a part is rejected too.
    std::vector<std::unique_ptr<CommandBuffer>> parts;
    parts.push_back(std::make_unique<FakeCommandBuffer>(QueueType::GRAPHICS, CommandBufferLevel::PRIMARY, 2));
    GraphicsRecorder recorder{primary};
    ParallelRenderingScope pass = recorder.begin_parallel_rendering({});
    EXPECT_THROW(pass.execute(parts), InvalidUsageError);
}

} // namespace
