// Must not compile: binding 1 is a storage image in the shader but a sampled image in the layout.
// The compiler prints the complete LayoutCheck (mismatch, set, binding, both descriptor types).
import gfx;

inline constexpr char BLUR_SOURCE[] = R"glsl(
#version 460
layout(local_size_x = 8) in;
layout(set = 0, binding = 0) uniform sampler2D u_input;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D u_output;
void main() {}
)glsl";

using BlurShader = gfx::GlslShader<BLUR_SOURCE, gfx::ShaderStage::COMPUTE, "blur">;

inline constexpr gfx::PipelineLayoutDefinition WRONG_LAYOUT = gfx::PipelineLayoutDefinition{}
    .with_binding(0, 0, gfx::DescriptorType::COMBINED_IMAGE_SAMPLER, gfx::ShaderStage::COMPUTE)
    .with_binding(0, 1, gfx::DescriptorType::SAMPLED_IMAGE, gfx::ShaderStage::COMPUTE);

void create(gfx::Device& device)
{
    const auto layout = device.create_pipeline_layout<WRONG_LAYOUT>();
    const auto shader = device.create_shader<BlurShader>();
    const auto pipeline = device.create_compute_pipeline(layout, shader);
}

auto main() -> int
{
    return 0;
}
