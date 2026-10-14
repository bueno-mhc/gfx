// Must not compile: the reflection cannot size a block member of an undeclared type, and says so.
import gfx;

inline constexpr char SOURCE[] = R"glsl(
#version 460
layout(set = 0, binding = 0) uniform Material { UndeclaredType value; } material;
void main() {}
)glsl";

using Shader = gfx::GlslShader<SOURCE, gfx::ShaderStage::FRAGMENT>;

auto main() -> int
{
    return static_cast<int>(Shader::LAYOUT.binding_count);
}
