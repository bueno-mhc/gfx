// Must not compile: the vertex shader reads location 1, which the vertex layout does not provide.
import gfx;

inline constexpr char VERTEX_SOURCE[] = R"glsl(
#version 460
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_texture_coordinate;
void main() {}
)glsl";

inline constexpr char FRAGMENT_SOURCE[] = R"glsl(
#version 460
layout(location = 0) out vec4 o_color;
void main() {}
)glsl";

using VertexShader = gfx::GlslShader<VERTEX_SOURCE, gfx::ShaderStage::VERTEX>;
using FragmentShader = gfx::GlslShader<FRAGMENT_SOURCE, gfx::ShaderStage::FRAGMENT>;

inline constexpr gfx::PipelineLayoutDefinition LAYOUT = gfx::pipeline_layout_from<VertexShader, FragmentShader>();
inline constexpr gfx::VertexLayoutDefinition POSITIONS_ONLY = gfx::VertexLayoutDefinition{}
    .with_binding(0, 12)
    .with_attribute(0, 0, gfx::Format::R32G32B32_SFLOAT, 0);
inline constexpr gfx::AttachmentLayoutDefinition ATTACHMENTS = gfx::AttachmentLayoutDefinition{}.with_color(gfx::Format::R8G8B8A8_UNORM);

void create(gfx::Device& device)
{
    const auto layout = device.create_pipeline_layout<LAYOUT>();
    const auto vertex = device.create_shader<VertexShader>();
    const auto fragment = device.create_shader<FragmentShader>();
    const auto pipeline = device.create_graphics_pipeline<POSITIONS_ONLY, ATTACHMENTS>(layout, vertex, fragment);
}

auto main() -> int
{
    return 0;
}
