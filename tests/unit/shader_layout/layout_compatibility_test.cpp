// Compatibility of shader layouts with pipeline layouts, vertex layouts and attachments.
#include <gtest/gtest.h>

import std;
import gfx;

namespace
{

using namespace gfx;

inline constexpr char BLUR_SOURCE[] = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D u_input;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D u_output;
layout(push_constant) uniform Push { vec2 texel; float radius; } push;
void main() {}
)glsl";

inline constexpr char MESH_VERTEX_SOURCE[] = R"glsl(
#version 460
layout(set = 0, binding = 0) uniform Camera { mat4 view_projection; } camera;
layout(push_constant) uniform Push { mat4 model; } push;
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_texture_coordinate;
layout(location = 0) out vec2 v_texture_coordinate;
void main() {}
)glsl";

inline constexpr char MESH_FRAGMENT_SOURCE[] = R"glsl(
#version 460
layout(set = 1, binding = 0) uniform sampler2D u_albedo;
layout(location = 0) in vec2 v_texture_coordinate;
layout(location = 0) out vec4 o_color;
layout(location = 1) out uint o_object_identifier;
void main() {}
)glsl";

using BlurShader = GlslShader<BLUR_SOURCE, ShaderStage::COMPUTE, "blur">;
using MeshVertexShader = GlslShader<MESH_VERTEX_SOURCE, ShaderStage::VERTEX, "mesh vertex">;
using MeshFragmentShader = GlslShader<MESH_FRAGMENT_SOURCE, ShaderStage::FRAGMENT, "mesh fragment">;

inline constexpr PipelineLayoutDefinition BLUR_LAYOUT = PipelineLayoutDefinition{}
    .with_binding(0, 0, DescriptorType::COMBINED_IMAGE_SAMPLER, ShaderStage::COMPUTE)
    .with_binding(0, 1, DescriptorType::STORAGE_IMAGE, ShaderStage::COMPUTE)
    .with_push_constants(12, ShaderStage::COMPUTE);

inline constexpr VertexLayoutDefinition MESH_VERTICES = VertexLayoutDefinition{}
    .with_binding(0, 20)
    .with_attribute(0, 0, Format::R32G32B32_SFLOAT, 0)
    .with_attribute(1, 0, Format::R16G16_UNORM, 12);

inline constexpr AttachmentLayoutDefinition MESH_ATTACHMENTS = AttachmentLayoutDefinition{}
    .with_color(Format::B8G8R8A8_SRGB)
    .with_color(Format::R32_UINT)
    .with_depth(Format::D24_UNORM_S8_UINT);

TEST(LayoutCompatibilityTest, MatchingLayoutsPass)
{
    static_assert(ShaderDefinition<BlurShader>);
    static_assert(LayoutCompatible<BlurShader, BLUR_LAYOUT>);
    static_assert(check_vertex_input(MeshVertexShader::LAYOUT, MESH_VERTICES).ok(),
                  "normalized integer attributes feed float inputs; component counts may differ");
    static_assert(check_fragment_output(MeshFragmentShader::LAYOUT, MESH_ATTACHMENTS).ok());
    static_assert(check_shader_stage(MeshVertexShader::LAYOUT, ShaderStage::VERTEX).ok());
    static_assert(LayoutDiagnostic<check_pipeline_layout(BlurShader::LAYOUT, BLUR_LAYOUT)>::OK);
    SUCCEED();
}

TEST(LayoutCompatibilityTest, DescriptorMismatchesAreReported)
{
    // static: a reference template argument (LayoutCompatible<Shader, LAYOUT>) needs static storage.
    static constexpr PipelineLayoutDefinition sampled_instead_of_storage = PipelineLayoutDefinition{}
        .with_binding(0, 0, DescriptorType::COMBINED_IMAGE_SAMPLER, ShaderStage::COMPUTE)
        .with_binding(0, 1, DescriptorType::SAMPLED_IMAGE, ShaderStage::COMPUTE)
        .with_push_constants(12, ShaderStage::COMPUTE);
    constexpr LayoutCheck check = check_pipeline_layout(BlurShader::LAYOUT, sampled_instead_of_storage);
    static_assert(check.mismatch == LayoutMismatch::DESCRIPTOR_TYPE_MISMATCH);
    static_assert(check.set == 0 && check.index == 1);
    static_assert(check.shader_type == DescriptorType::STORAGE_IMAGE && check.layout_type == DescriptorType::SAMPLED_IMAGE);
    static_assert(!LayoutCompatible<BlurShader, sampled_instead_of_storage>);
    EXPECT_EQ(describe(check), "set 0 binding 1 is STORAGE_IMAGE in the shader but SAMPLED_IMAGE in the pipeline layout");
}

TEST(LayoutCompatibilityTest, EveryKindOfPipelineLayoutMismatchIsDetected)
{
    constexpr auto missing = PipelineLayoutDefinition{}
        .with_binding(0, 0, DescriptorType::COMBINED_IMAGE_SAMPLER, ShaderStage::COMPUTE);
    static_assert(check_pipeline_layout(BlurShader::LAYOUT, missing).mismatch == LayoutMismatch::BINDING_MISSING_FROM_LAYOUT);

    constexpr auto invisible = BLUR_LAYOUT.with_binding(0, 2, DescriptorType::UNIFORM_BUFFER, ShaderStage::VERTEX);
    constexpr auto invisible_input = PipelineLayoutDefinition{}
        .with_binding(0, 0, DescriptorType::COMBINED_IMAGE_SAMPLER, ShaderStage::FRAGMENT)
        .with_binding(0, 1, DescriptorType::STORAGE_IMAGE, ShaderStage::COMPUTE)
        .with_push_constants(12, ShaderStage::COMPUTE);
    static_assert(check_pipeline_layout(BlurShader::LAYOUT, invisible).ok(), "extra layout bindings are allowed");
    static_assert(check_pipeline_layout(BlurShader::LAYOUT, invisible_input).mismatch == LayoutMismatch::STAGE_NOT_VISIBLE);

    constexpr auto small_push = PipelineLayoutDefinition{}
        .with_binding(0, 0, DescriptorType::COMBINED_IMAGE_SAMPLER, ShaderStage::COMPUTE)
        .with_binding(0, 1, DescriptorType::STORAGE_IMAGE, ShaderStage::COMPUTE)
        .with_push_constants(8, ShaderStage::COMPUTE);
    static_assert(check_pipeline_layout(BlurShader::LAYOUT, small_push).mismatch == LayoutMismatch::PUSH_CONSTANTS_TOO_LARGE);

    constexpr auto hidden_push = small_push.with_push_constants(16, ShaderStage::FRAGMENT);
    static_assert(check_pipeline_layout(BlurShader::LAYOUT, hidden_push).mismatch ==
                  LayoutMismatch::PUSH_CONSTANT_STAGE_NOT_VISIBLE);
    SUCCEED();
}

TEST(LayoutCompatibilityTest, DynamicBlocksAndArrayCountsFollowVulkanRules)
{
    constexpr ShaderLayout shader = ShaderLayout{.stage = ShaderStage::FRAGMENT}
        .with_binding({.set = 0, .binding = 0, .type = DescriptorType::UNIFORM_BUFFER, .name = "Frame"})
        .with_binding({.set = 0, .binding = 1, .type = DescriptorType::SAMPLED_IMAGE, .count = 0, .name = "u_textures"});
    constexpr auto dynamic_layout = PipelineLayoutDefinition{}
        .with_binding(0, 0, DescriptorType::UNIFORM_BUFFER_DYNAMIC, ShaderStage::FRAGMENT)
        .with_binding(0, 1, DescriptorType::SAMPLED_IMAGE, ShaderStage::FRAGMENT, 1024,
                      DescriptorBindingOption::PARTIALLY_BOUND | DescriptorBindingOption::VARIABLE_DESCRIPTOR_COUNT);
    static_assert(check_pipeline_layout(shader, dynamic_layout).ok());

    constexpr auto unsized_layout = dynamic_layout.with_binding(1, 0, DescriptorType::SAMPLER, ShaderStage::FRAGMENT);
    static_assert(check_pipeline_layout(shader, unsized_layout).ok());
    SUCCEED();
}

TEST(LayoutCompatibilityTest, VertexAndFragmentMismatchesAreDetected)
{
    constexpr auto missing_attribute = VertexLayoutDefinition{}
        .with_binding(0, 12)
        .with_attribute(0, 0, Format::R32G32B32_SFLOAT, 0);
    constexpr LayoutCheck missing = check_vertex_input(MeshVertexShader::LAYOUT, missing_attribute);
    static_assert(missing.mismatch == LayoutMismatch::VERTEX_INPUT_WITHOUT_ATTRIBUTE && missing.index == 1);

    constexpr auto integer_only = VertexLayoutDefinition{}
        .with_binding(0, 20)
        .with_attribute(0, 0, Format::R32G32B32_SFLOAT, 0)
        .with_attribute(1, 0, Format::R32G32_UINT, 12);
    static_assert(check_vertex_input(MeshVertexShader::LAYOUT, integer_only).mismatch == LayoutMismatch::VERTEX_INPUT_TYPE_MISMATCH);

    constexpr auto one_attachment = AttachmentLayoutDefinition{}.with_color(Format::B8G8R8A8_SRGB);
    constexpr LayoutCheck no_attachment = check_fragment_output(MeshFragmentShader::LAYOUT, one_attachment);
    static_assert(no_attachment.mismatch == LayoutMismatch::FRAGMENT_OUTPUT_WITHOUT_ATTACHMENT && no_attachment.index == 1);

    constexpr auto float_identifier = AttachmentLayoutDefinition{}.with_color(Format::B8G8R8A8_SRGB).with_color(Format::R16_SFLOAT);
    static_assert(check_fragment_output(MeshFragmentShader::LAYOUT, float_identifier).mismatch ==
                  LayoutMismatch::FRAGMENT_OUTPUT_TYPE_MISMATCH);

    static_assert(check_shader_stage(MeshFragmentShader::LAYOUT, ShaderStage::VERTEX).mismatch ==
                  LayoutMismatch::WRONG_SHADER_STAGE);
    static_assert(MESH_ATTACHMENTS.stencil_format == Format::D24_UNORM_S8_UINT, "with_depth also sets the stencil format");
    SUCCEED();
}

TEST(LayoutCompatibilityTest, RuntimeValidationThrowsADescriptiveError)
{
    const LayoutCheck check = check_pipeline_layout(BlurShader::LAYOUT, PipelineLayoutDefinition{});
    try
    {
        validate_layout_check(check, BlurShader::NAME, "Device::create_compute_pipeline");
        FAIL() << "ShaderLayoutMismatchError was not thrown";
    }
    catch (const ShaderLayoutMismatchError& error)
    {
        EXPECT_EQ(error.mismatch(), LayoutMismatch::BINDING_MISSING_FROM_LAYOUT);
        EXPECT_EQ(error.shader_name(), "blur");
        const std::string_view message = error.what();
        EXPECT_TRUE(message.starts_with("[gfx] Device::create_compute_pipeline: shader 'blur' does not match its layout "
                                        "(BINDING_MISSING_FROM_LAYOUT): the shader uses set 0 binding 0 "
                                        "(COMBINED_IMAGE_SAMPLER), which the pipeline layout does not declare"))
            << message;
    }
    EXPECT_NO_THROW(validate_layout_check(LayoutCheck{}, "shader", "operation"));
}

TEST(LayoutCompatibilityTest, BindingsAndPushConstantsAreFoundByName)
{
    static_assert(binding_of<"u_output", BlurShader>().binding == 1);
    static_assert(binding_of<"u_output", BlurShader>().type == DescriptorType::STORAGE_IMAGE);

    struct BlurPush
    {
        float texel[2];
        float radius;
    };
    static_assert(PushConstantsFor<BlurPush, BlurShader>);
    static_assert(!PushConstantsFor<double, BlurShader>);
    static_assert(!PushConstantsFor<BlurPush, MeshFragmentShader>, "the fragment shader declares no push constants");
    SUCCEED();
}

TEST(LayoutCompatibilityTest, PipelineLayoutsCanBeDerivedFromShaders)
{
    static constexpr PipelineLayoutDefinition derived = pipeline_layout_from<MeshVertexShader, MeshFragmentShader>();
    static_assert(derived.binding_count == 2);
    static_assert(derived.find(0, 0)->type == DescriptorType::UNIFORM_BUFFER);
    static_assert(derived.find(0, 0)->stages == ShaderStage::VERTEX);
    static_assert(derived.find(1, 0)->stages == ShaderStage::FRAGMENT);
    static_assert(derived.push_constant_size == 64 && derived.push_constant_stages == ShaderStage::VERTEX);
    static_assert(derived.set_count() == 2);
    static_assert(LayoutCompatible<MeshVertexShader, derived> && LayoutCompatible<MeshFragmentShader, derived>);
    SUCCEED();
}

} // namespace
