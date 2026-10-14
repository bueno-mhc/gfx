// Must not compile: a buffer cannot be written to a storage image binding.
import gfx;

inline constexpr char SOURCE[] = R"glsl(
#version 460
layout(local_size_x = 8) in;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D u_output;
void main() {}
)glsl";

using Shader = gfx::GlslShader<SOURCE, gfx::ShaderStage::COMPUTE>;

void write(gfx::DescriptorSet& set)
{
    set.write<"u_output", Shader>(gfx::BufferDescriptor{});
}

auto main() -> int
{
    return 0;
}
