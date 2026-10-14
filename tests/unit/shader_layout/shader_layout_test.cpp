// Compile-time GLSL reflection: every check is a static_assert, evaluated by the compiler.
#include <gtest/gtest.h>

import std;
import gfx;

namespace
{

using namespace gfx;

inline constexpr char COMPUTE_SOURCE[] = R"glsl(
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(local_size_x = 8, local_size_y = 4) in;

struct Particle { vec3 position; float mass; vec4 color; };   // 32 bytes in std430

// Comments mentioning layout(set = 9, binding = 9) uniform sampler2D ignored; are skipped.
layout(set = 0, binding = 0) uniform sampler2D u_input;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D u_output;
layout(set = 0, binding = 2) uniform Camera { mat4 view_projection; vec3 eye; float time; } camera;
layout(set = 1, binding = 0, std430) readonly buffer Particles { uint count; Particle items[]; } particles;
layout(set = 2, binding = 0) uniform texture2D u_textures[];
layout(set = 2, binding = 1) uniform sampler u_samplers[4];
layout(push_constant) uniform Push { vec2 texel; float radius; } push;

void main()
{
    // Declarations inside functions are not part of the interface.
    vec4 color = texture(u_input, vec2(0));
    imageStore(u_output, ivec2(gl_GlobalInvocationID.xy), color);
}
)glsl";

using ComputeShader = GlslShader<COMPUTE_SOURCE, ShaderStage::COMPUTE, "compute test">;
constexpr const ShaderLayout& COMPUTE = ComputeShader::LAYOUT;

TEST(ShaderLayoutTest, ReflectsComputeInterface)
{
    static_assert(COMPUTE.stage == ShaderStage::COMPUTE);
    static_assert(COMPUTE.workgroup_size == std::array<std::uint32_t, 3>{8, 4, 1});
    static_assert(COMPUTE.binding_count == 6);
    static_assert(COMPUTE.push_constant_size == 12);
    static_assert(ComputeShader::NAME == "compute test");
    SUCCEED();
}

TEST(ShaderLayoutTest, ReflectsOpaqueTypes)
{
    static_assert(COMPUTE.find("u_input")->type == DescriptorType::COMBINED_IMAGE_SAMPLER);
    static_assert(COMPUTE.find("u_output")->type == DescriptorType::STORAGE_IMAGE);
    static_assert(COMPUTE.find("u_output")->image_format == Format::R16G16B16A16_SFLOAT);
    static_assert(COMPUTE.find("u_textures")->type == DescriptorType::SAMPLED_IMAGE);
    static_assert(COMPUTE.find("u_textures")->count == 0, "runtime-sized (bindless) array");
    static_assert(COMPUTE.find("u_samplers")->type == DescriptorType::SAMPLER);
    static_assert(COMPUTE.find("u_samplers")->count == 4);
    static_assert(COMPUTE.find(2, 1)->name.view() == "u_samplers");
    static_assert(!COMPUTE.find(9, 9).has_value(), "commented-out declarations are ignored");
    SUCCEED();
}

TEST(ShaderLayoutTest, ComputesExactBlockSizes)
{
    // std140: mat4 (64) + vec3 at 64 (12) + float at 76 (4) = 80.
    static_assert(COMPUTE.find("Camera")->type == DescriptorType::UNIFORM_BUFFER);
    static_assert(COMPUTE.find("Camera")->block_size == 80);
    // std430: uint (4), then Particle[] aligned to 16 at offset 16; a runtime array adds nothing.
    static_assert(COMPUTE.find("Particles")->type == DescriptorType::STORAGE_BUFFER);
    static_assert(COMPUTE.find("Particles")->block_size == 16);
    SUCCEED();
}

inline constexpr char PACKING_SOURCE[] = R"glsl(
#version 460
#extension GL_EXT_scalar_block_layout : require
layout(set = 0, binding = 0) uniform Std140Arrays { float values[3]; vec2 pair; } a;      // 3 * 16 + 8 = 56
layout(set = 0, binding = 1, std430) buffer Std430Arrays { float values[3]; vec2 pair; } b; // 12 + 4 pad + 8 = 24
layout(set = 0, binding = 2, scalar) buffer ScalarBlock { vec3 position; float weight; vec3 normal; } c; // 12+4+12 = 28
layout(set = 0, binding = 3) uniform Matrices { mat3 rotation; mat2x4 skew; } d;       // 48 + 32 = 80
layout(set = 0, binding = 4, std430) buffer Multiple { vec3 a, b; float c, d[2]; } e;   // 12+4pad,12 = 28, 4 -> 32, 8 -> 40
layout(set = 0, binding = 5) uniform Offsets { layout(offset = 0) vec4 first; highp vec4 second; } f; // 32
)glsl";

constexpr ShaderLayout PACKING = reflect_glsl(PACKING_SOURCE, ShaderStage::FRAGMENT);

TEST(ShaderLayoutTest, AppliesEachBlockPacking)
{
    static_assert(PACKING.find("Std140Arrays")->block_size == 56, "std140 rounds array strides to 16");
    static_assert(PACKING.find("Std430Arrays")->block_size == 24, "std430 keeps the natural array stride");
    static_assert(PACKING.find("ScalarBlock")->block_size == 28, "scalar layout aligns vec3 to 4 bytes");
    static_assert(PACKING.find("Matrices")->block_size == 80, "mat3 is 3 x vec4 columns, mat2x4 is 2 x vec4");
    static_assert(PACKING.find("Multiple")->block_size == 40, "several members declared by one type");
    static_assert(PACKING.find("Offsets")->block_size == 32, "member qualifiers are skipped");
    SUCCEED();
}

inline constexpr char VERTEX_SOURCE[] = R"glsl(
#version 460
layout(location = 0) in vec3 a_position;
layout(location = 1) in flat ivec2 a_indices;
layout(location = 2) in mat4 a_instance_transform;   // locations 2..5
layout(location = 0) out vec2 v_texture_coordinate;
void main() {}
)glsl";

constexpr ShaderLayout VERTEX = reflect_glsl(VERTEX_SOURCE, ShaderStage::VERTEX);

TEST(ShaderLayoutTest, ReflectsStageInputsAndOutputs)
{
    static_assert(VERTEX.input_count == 3 && VERTEX.output_count == 1);
    static_assert(VERTEX.inputs()[0].format == Format::R32G32B32_SFLOAT);
    static_assert(VERTEX.inputs()[1].format == Format::R32G32_SINT);
    static_assert(VERTEX.inputs()[2].location == 2 && VERTEX.inputs()[2].location_count == 4);
    static_assert(VERTEX.inputs()[2].format == Format::R32G32B32A32_SFLOAT);
    static_assert(VERTEX.outputs()[0].name.view() == "v_texture_coordinate");
    SUCCEED();
}

TEST(ShaderLayoutTest, DeclaredLayoutsUseTheBuilder)
{
    constexpr ShaderLayout declared = ShaderLayout{.stage = ShaderStage::FRAGMENT}
        .with_binding({.set = 0, .binding = 0, .type = DescriptorType::COMBINED_IMAGE_SAMPLER, .name = "u_albedo"})
        .with_output({.location = 0, .format = Format::R32G32B32A32_SFLOAT, .name = "o_color"})
        .with_push_constants(16);
    static_assert(declared.binding_count == 1 && declared.output_count == 1);
    static_assert(declared.find("u_albedo")->type == DescriptorType::COMBINED_IMAGE_SAMPLER);
    static_assert(declared.push_constant_size == 16);
    SUCCEED();
}

TEST(ShaderLayoutTest, RuntimeLayoutsReportOverflowsAsErrors)
{
    ShaderLayout layout;
    for (std::uint32_t binding = 0; binding < MAXIMUM_SHADER_BINDINGS; ++binding)
    {
        layout.add_binding({.binding = binding});
    }
    EXPECT_THROW(layout.add_binding({.binding = MAXIMUM_SHADER_BINDINGS}), InvalidUsageError);

    const std::string too_long(MAXIMUM_BINDING_NAME_LENGTH + 1, 'x');
    EXPECT_THROW(BindingName{too_long}, InvalidUsageError);
}

} // namespace
