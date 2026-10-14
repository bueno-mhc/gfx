// Typed command recording: what each queue type may record, RAII begin/end, forwarding to the backend.
#include <gtest/gtest.h>

import std;
import gfx;
import gfx.tests.fake_backend;

namespace
{

using namespace gfx;
using gfx::tests::FakeCommandBuffer;

inline constexpr char BLUR_SOURCE[] = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D u_input;
layout(push_constant) uniform Push { vec2 texel; float radius; } push;
void main() {}
)glsl";

using BlurShader = GlslShader<BLUR_SOURCE, ShaderStage::COMPUTE, "blur">;

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
concept CanBeginRendering = requires(Type& recorder) { recorder.begin_rendering(RenderingDescription{}); };

template<class Type>
concept CanDraw = requires(Type& recorder) { recorder.draw(DrawArguments{}); };

template<class Type, class Shader, class Data>
concept CanPushTypedConstants = requires(Type& recorder, const Data& data) {
    recorder.template push_constants<Shader>(PipelineLayoutHandle{}, data);
};

TEST(RecorderTest, QueueTypesDetermineWhatCanBeRecorded)
{
    static_assert(CanDispatch<GraphicsRecorder> && CanDispatch<ComputeRecorder> && !CanDispatch<TransferRecorder>);
    static_assert(CanCopyBuffers<GraphicsRecorder> && CanCopyBuffers<ComputeRecorder> && CanCopyBuffers<TransferRecorder>);
    static_assert(CanBeginRendering<GraphicsRecorder> && !CanBeginRendering<ComputeRecorder> && !CanBeginRendering<TransferRecorder>);
    static_assert(!CanDraw<GraphicsRecorder>, "draws are recorded on the RenderingScope only");
    static_assert(CanDraw<RenderingScope<QueueType::GRAPHICS>>);
    static_assert(CanPushTypedConstants<ComputeRecorder, BlurShader, BlurPush>);
    static_assert(!CanPushTypedConstants<ComputeRecorder, BlurShader, double>, "push constants must have the declared size");
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

TEST(RecorderTest, TypedPushConstantsUseTheShaderStageAndSize)
{
    FakeCommandBuffer command_buffer{QueueType::COMPUTE};
    {
        ComputeRecorder recorder{command_buffer};
        recorder.push_constants<BlurShader>(PipelineLayoutHandle{}, BlurPush{{0.5F, 0.25F}, 3.0F});
    }
    EXPECT_EQ(command_buffer.log()[1], "push_constants COMPUTE 0 12");

    // The bytes are the struct's bytes.
    BlurPush read_back{};
    ASSERT_EQ(command_buffer.push_constant_bytes().size(), sizeof(BlurPush));
    std::memcpy(&read_back, command_buffer.push_constant_bytes().data(), sizeof(BlurPush));
    EXPECT_EQ(read_back.radius, 3.0F);
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

} // namespace
