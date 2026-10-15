// Run-time validation of reflected shader layouts against pipeline layouts, vertex attributes and
// color attachments, and of shader module descriptions.
#include <gtest/gtest.h>

import std;
import gfx;

namespace
{

using namespace gfx;

// The layouts SPIR-V reflection produces for three shaders:
//   blur (compute):    sampler2D u_input (0,0); rgba16f image2D u_output (0,1); push { vec2 texel; float radius; }
//   mesh vertex:       uniform Camera camera (0,0); push { mat4 model; }; in vec3 a_position (0); in vec2 a_uv (1)
//   mesh fragment:     sampler2D u_albedo (1,0); out vec4 o_color (0); out uint o_object (1)
auto blur_layout() -> ShaderLayout
{
    return {
        .stage = ShaderStage::COMPUTE,
        .bindings = {{.set = 0, .binding = 0, .type = DescriptorType::COMBINED_IMAGE_SAMPLER, .name = "u_input"},
                     {.set = 0, .binding = 1, .type = DescriptorType::STORAGE_IMAGE, .image_format = Format::R16G16B16A16_SFLOAT,
                      .name = "u_output"}},
        .push_constant_offset = 0,
        .push_constant_size = 12,
        .workgroup_size = {8, 8, 1},
    };
}

auto mesh_vertex_layout() -> ShaderLayout
{
    return {
        .stage = ShaderStage::VERTEX,
        .bindings = {{.set = 0, .binding = 0, .type = DescriptorType::UNIFORM_BUFFER, .block_size = 64, .name = "camera"}},
        .push_constant_offset = 0,
        .push_constant_size = 64,
        .inputs = {{.location = 0, .format = Format::R32G32B32_SFLOAT, .name = "a_position"},
                   {.location = 1, .format = Format::R32G32_SFLOAT, .name = "a_uv"}},
    };
}

auto mesh_fragment_layout() -> ShaderLayout
{
    return {
        .stage = ShaderStage::FRAGMENT,
        .bindings = {{.set = 1, .binding = 0, .type = DescriptorType::COMBINED_IMAGE_SAMPLER, .name = "u_albedo"}},
        .outputs = {{.location = 0, .format = Format::R32G32B32A32_SFLOAT, .name = "o_color"},
                    {.location = 1, .format = Format::R32_UINT, .name = "o_object"}},
    };
}

auto blur_pipeline_layout() -> PipelineLayoutDefinition
{
    return {
        .bindings = {{.set = 0, .binding = 0, .type = DescriptorType::COMBINED_IMAGE_SAMPLER, .stages = ShaderStage::COMPUTE,
                      .name = "u_input"},
                     {.set = 0, .binding = 1, .type = DescriptorType::STORAGE_IMAGE, .stages = ShaderStage::COMPUTE,
                      .name = "u_output"}},
        .push_constant_ranges = {{.stages = ShaderStage::COMPUTE, .offset = 0, .size = 12}},
    };
}

TEST(ShaderLayoutTest, FindsBindingsByNameAndSlot)
{
    const ShaderLayout layout = blur_layout();
    ASSERT_NE(layout.find("u_output"), nullptr);
    EXPECT_EQ(layout.find("u_output")->binding, 1U);
    EXPECT_EQ(layout.find(0, 0)->name, "u_input");
    EXPECT_EQ(layout.find("u_missing"), nullptr);
    EXPECT_EQ(layout.find(2, 0), nullptr);
}

TEST(ShaderValidationTest, MatchingLayoutsPass)
{
    EXPECT_TRUE(check_pipeline_layout(blur_layout(), blur_pipeline_layout()).ok());
    EXPECT_TRUE(check_shader_stage(blur_layout(), ShaderStage::COMPUTE).ok());

    // Extra layout bindings are allowed.
    PipelineLayoutDefinition larger = blur_pipeline_layout();
    larger.bindings.push_back({.set = 0, .binding = 2, .type = DescriptorType::UNIFORM_BUFFER, .stages = ShaderStage::VERTEX});
    EXPECT_TRUE(check_pipeline_layout(blur_layout(), larger).ok());
}

TEST(ShaderValidationTest, DescriptorMismatchesAreReported)
{
    PipelineLayoutDefinition sampled_instead_of_storage = blur_pipeline_layout();
    sampled_instead_of_storage.bindings[1].type = DescriptorType::SAMPLED_IMAGE;
    const LayoutCheck check = check_pipeline_layout(blur_layout(), sampled_instead_of_storage);
    EXPECT_EQ(check.mismatch, LayoutMismatch::DESCRIPTOR_TYPE_MISMATCH);
    EXPECT_EQ(check.set, 0U);
    EXPECT_EQ(check.index, 1U);
    EXPECT_EQ(describe(check), "set 0 binding 1 is STORAGE_IMAGE in the shader but SAMPLED_IMAGE in the pipeline layout");
}

TEST(ShaderValidationTest, EveryKindOfPipelineLayoutMismatchIsDetected)
{
    PipelineLayoutDefinition missing = blur_pipeline_layout();
    missing.bindings.pop_back();
    EXPECT_EQ(check_pipeline_layout(blur_layout(), missing).mismatch, LayoutMismatch::BINDING_MISSING_FROM_LAYOUT);

    PipelineLayoutDefinition invisible = blur_pipeline_layout();
    invisible.bindings[0].stages = ShaderStage::FRAGMENT;
    EXPECT_EQ(check_pipeline_layout(blur_layout(), invisible).mismatch, LayoutMismatch::STAGE_NOT_VISIBLE);

    PipelineLayoutDefinition too_few = blur_pipeline_layout();
    ShaderLayout array_shader = blur_layout();
    array_shader.bindings[0].count = 4;
    EXPECT_EQ(check_pipeline_layout(array_shader, too_few).mismatch, LayoutMismatch::DESCRIPTOR_COUNT_MISMATCH);

    PipelineLayoutDefinition small_push = blur_pipeline_layout();
    small_push.push_constant_ranges[0].size = 8;
    const LayoutCheck outside = check_pipeline_layout(blur_layout(), small_push);
    EXPECT_EQ(outside.mismatch, LayoutMismatch::PUSH_CONSTANTS_OUTSIDE_RANGE);
    EXPECT_EQ(describe(outside), "the shader reads push-constant bytes [0, 12) but the pipeline layout's range for COMPUTE is [0, 8)");

    PipelineLayoutDefinition hidden_push = blur_pipeline_layout();
    hidden_push.push_constant_ranges[0].stages = ShaderStage::FRAGMENT;
    EXPECT_EQ(check_pipeline_layout(blur_layout(), hidden_push).mismatch, LayoutMismatch::PUSH_CONSTANT_STAGE_NOT_VISIBLE);
}

TEST(ShaderValidationTest, PushConstantsAreCheckedAsAByteRange)
{
    // A shader reading bytes [16, 32) fits a range [0, 32), not a range [0, 16) or [20, 40).
    ShaderLayout shader = blur_layout();
    shader.push_constant_offset = 16;
    shader.push_constant_size = 16;
    PipelineLayoutDefinition layout = blur_pipeline_layout();
    layout.push_constant_ranges[0] = {.stages = ShaderStage::COMPUTE, .offset = 0, .size = 32};
    EXPECT_TRUE(check_pipeline_layout(shader, layout).ok());
    layout.push_constant_ranges[0] = {.stages = ShaderStage::COMPUTE, .offset = 0, .size = 16};
    EXPECT_EQ(check_pipeline_layout(shader, layout).mismatch, LayoutMismatch::PUSH_CONSTANTS_OUTSIDE_RANGE);
    layout.push_constant_ranges[0] = {.stages = ShaderStage::COMPUTE, .offset = 20, .size = 20};
    EXPECT_EQ(check_pipeline_layout(shader, layout).mismatch, LayoutMismatch::PUSH_CONSTANTS_OUTSIDE_RANGE);
}

TEST(ShaderValidationTest, DynamicBlocksAndArrayCountsFollowVulkanRules)
{
    const ShaderLayout shader{
        .stage = ShaderStage::FRAGMENT,
        .bindings = {{.set = 0, .binding = 0, .type = DescriptorType::UNIFORM_BUFFER, .name = "Frame"},
                     {.set = 0, .binding = 1, .type = DescriptorType::SAMPLED_IMAGE, .count = 0, .name = "u_textures"}},
    };
    PipelineLayoutDefinition layout{
        .bindings = {{.set = 0, .binding = 0, .type = DescriptorType::UNIFORM_BUFFER_DYNAMIC, .stages = ShaderStage::FRAGMENT},
                     {.set = 0, .binding = 1, .type = DescriptorType::SAMPLED_IMAGE, .count = 1024, .stages = ShaderStage::FRAGMENT,
                      .options = DescriptorBindingOption::PARTIALLY_BOUND | DescriptorBindingOption::VARIABLE_DESCRIPTOR_COUNT}},
    };
    EXPECT_TRUE(check_pipeline_layout(shader, layout).ok()) << "a plain block binds through a dynamic one";

    layout.bindings[1].count = 0;
    EXPECT_EQ(check_pipeline_layout(shader, layout).mismatch, LayoutMismatch::DESCRIPTOR_COUNT_MISMATCH)
        << "a runtime-sized array needs a real count in the layout";
}

TEST(ShaderValidationTest, VertexAndFragmentMismatchesAreDetected)
{
    const std::array attributes{VertexInputAttribute{.location = 0, .binding = 0, .format = Format::R32G32B32_SFLOAT, .offset = 0},
                                VertexInputAttribute{.location = 1, .binding = 0, .format = Format::R16G16_UNORM, .offset = 12}};
    EXPECT_TRUE(check_vertex_input(mesh_vertex_layout(), attributes).ok())
        << "normalized integer attributes feed float inputs; component counts may differ";

    const LayoutCheck missing = check_vertex_input(mesh_vertex_layout(), std::span{attributes}.first(1));
    EXPECT_EQ(missing.mismatch, LayoutMismatch::VERTEX_INPUT_WITHOUT_ATTRIBUTE);
    EXPECT_EQ(missing.index, 1U);

    const std::array integer_uv{attributes[0], VertexInputAttribute{.location = 1, .binding = 0, .format = Format::R32G32_UINT, .offset = 12}};
    EXPECT_EQ(check_vertex_input(mesh_vertex_layout(), integer_uv).mismatch, LayoutMismatch::VERTEX_INPUT_TYPE_MISMATCH);

    const std::array color_formats{Format::B8G8R8A8_SRGB, Format::R32_UINT};
    EXPECT_TRUE(check_fragment_output(mesh_fragment_layout(), color_formats).ok());
    const LayoutCheck no_attachment = check_fragment_output(mesh_fragment_layout(), std::span{color_formats}.first(1));
    EXPECT_EQ(no_attachment.mismatch, LayoutMismatch::FRAGMENT_OUTPUT_WITHOUT_ATTACHMENT);
    EXPECT_EQ(no_attachment.index, 1U);
    const std::array float_object{Format::B8G8R8A8_SRGB, Format::R16_SFLOAT};
    EXPECT_EQ(check_fragment_output(mesh_fragment_layout(), float_object).mismatch, LayoutMismatch::FRAGMENT_OUTPUT_TYPE_MISMATCH);

    EXPECT_EQ(check_shader_stage(mesh_fragment_layout(), ShaderStage::VERTEX).mismatch, LayoutMismatch::WRONG_SHADER_STAGE);
}

TEST(PipelineLayoutDefinitionTest, FindsBindingsRangesAndSets)
{
    const PipelineLayoutDefinition layout{
        .bindings = {{.set = 0, .binding = 0, .type = DescriptorType::UNIFORM_BUFFER, .stages = ShaderStage::VERTEX, .name = "camera"},
                     {.set = 2, .binding = 3, .type = DescriptorType::SAMPLER, .stages = ShaderStage::FRAGMENT, .name = ""}},
        .push_constant_ranges = {{.stages = ShaderStage::VERTEX, .offset = 0, .size = 64},
                                 {.stages = ShaderStage::FRAGMENT, .offset = 64, .size = 16}},
    };
    EXPECT_EQ(layout.find("camera")->binding, 0U);
    EXPECT_EQ(layout.find(""), nullptr) << "an empty name never matches";
    EXPECT_EQ(layout.find(2, 3)->type, DescriptorType::SAMPLER);
    EXPECT_EQ(layout.set_count(), 3U);
    EXPECT_TRUE(layout.set_bindings(1).empty());
    ASSERT_EQ(layout.set_bindings(2).size(), 1U);
    EXPECT_EQ(layout.set_bindings(2)[0].binding, 3U);
    EXPECT_EQ(layout.push_constant_range(ShaderStage::FRAGMENT)->offset, 64U);
    EXPECT_EQ(layout.push_constant_range(ShaderStage::COMPUTE), nullptr);
}

TEST(ShaderModuleDescriptionTest, SpirvMustStartWithItsMagicNumber)
{
    const std::array<std::uint32_t, 2> spirv{SPIRV_MAGIC_NUMBER, 0x00010600U};
    EXPECT_NO_THROW(validate_shader_module_description({.stage = ShaderStage::COMPUTE, .spirv = spirv, .name = "blur"}));

    const std::array<std::uint32_t, 1> swapped{0x03022307U};
    try
    {
        validate_shader_module_description({.stage = ShaderStage::COMPUTE, .spirv = swapped, .name = "blur"});
        FAIL() << "InvalidUsageError was not thrown";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_EQ(error.problem(), "shader 'blur': the code is not SPIR-V: its first word is 0x03022307, SPIR-V starts with 0x07230203");
    }
    EXPECT_THROW(validate_shader_module_description({.stage = ShaderStage::COMPUTE, .name = "empty"}), InvalidUsageError);
    EXPECT_THROW(validate_shader_module_description({.stage = ShaderStage::VERTEX | ShaderStage::FRAGMENT, .spirv = spirv}),
                 InvalidUsageError);
    EXPECT_THROW(validate_shader_module_description({.stage = ShaderStage::COMPUTE, .spirv = spirv, .entry_point = ""}),
                 InvalidUsageError);
}

TEST(ShaderModuleDescriptionTest, GlslIsAcceptedOnlyInDebugBuilds)
{
    const ShaderModuleDescription glsl{.stage = ShaderStage::COMPUTE,
                                       .source_format = ShaderSourceFormat::GLSL,
                                       .source = "#version 460\nvoid main() {}\n",
                                       .name = "blur"};
    if constexpr (SHADER_HOT_RELOAD)
    {
        EXPECT_NO_THROW(validate_shader_module_description(glsl));
    }
    else
    {
        try
        {
            validate_shader_module_description(glsl);
            FAIL() << "InvalidUsageError was not thrown";
        }
        catch (const InvalidUsageError& error)
        {
            EXPECT_EQ(error.problem(), "shader 'blur': GLSL source is compiled at run time only in Debug builds; this "
                                       "Release build only loads compiled SPIR-V");
            EXPECT_EQ(error.suggestion(), "compile the shader at build time with gfx_add_shaders() and pass its SPIR-V");
        }
    }
}

} // namespace
