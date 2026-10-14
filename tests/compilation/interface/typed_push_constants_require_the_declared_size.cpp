// Must not compile: the shader declares 12 bytes of push constants, the struct has 16.
import gfx;

inline constexpr char SOURCE[] = R"glsl(
#version 460
layout(local_size_x = 8) in;
layout(push_constant) uniform Push { vec2 texel; float radius; } push;
void main() {}
)glsl";

using Shader = gfx::GlslShader<SOURCE, gfx::ShaderStage::COMPUTE>;

struct WrongPush
{
    float texel[2];
    float radius;
    float extra;
};

void record(gfx::CommandBuffer& command_buffer)
{
    gfx::ComputeRecorder recorder{command_buffer};
    recorder.push_constants<Shader>(gfx::PipelineLayoutHandle{}, WrongPush{});
}

auto main() -> int
{
    return 0;
}
