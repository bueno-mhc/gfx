#include <gtest/gtest.h>

import std;
import gfx;
import gfx.tests.shaders;

namespace
{

using namespace gfx;
namespace shaders = gfx::tests::shaders;

auto
contains(std::string_view text, std::string_view part) -> bool
{
    return text.find(part) != std::string_view::npos;
}

TEST(SpirvReflectionTest, ReflectsEveryKindOfBindingWithCountsBlockSizesAndNames)
{
    const ShaderLayout layout = reflect_spirv(shaders::reflection_comp_spirv, ShaderStage::COMPUTE);

    EXPECT_EQ(layout.stage, ShaderStage::COMPUTE);
    const std::vector<ShaderBinding> expected{
        {.set = 0, .binding = 3, .type = DescriptorType::STORAGE_BUFFER, .count = 1, .block_size = 16, .name = "particles"},
        {.set = 1, .binding = 0, .type = DescriptorType::UNIFORM_BUFFER, .count = 1, .block_size = 128, .name = "camera"},
        {.set = 1,
         .binding = 2,
         .type = DescriptorType::STORAGE_IMAGE,
         .count = 1,
         .image_format = Format::R16G16B16A16_SFLOAT,
         .name = "output_image"},
        {.set = 1, .binding = 4, .type = DescriptorType::COMBINED_IMAGE_SAMPLER, .count = 4, .name = "textures"},
        {.set = 2, .binding = 0, .type = DescriptorType::SAMPLED_IMAGE, .count = 0, .name = "bindless_textures"},
        {.set = 2, .binding = 1, .type = DescriptorType::SAMPLER, .count = 2, .name = "samplers"},
        {.set = 3, .binding = 0, .type = DescriptorType::UNIFORM_TEXEL_BUFFER, .count = 1, .name = "lookup_table"},
    };
    EXPECT_EQ(layout.bindings, expected);
    EXPECT_EQ(layout.find("unused_texture"), nullptr) << "bindings the entry point does not use are not reported";
}

TEST(SpirvReflectionTest, ReflectsOnlyThePushConstantBytesTheShaderReads)
{
    const ShaderLayout layout = reflect_spirv(shaders::reflection_comp_spirv);
    EXPECT_EQ(layout.push_constant_offset, 4U);
    EXPECT_EQ(layout.push_constant_size, 8U);
}

TEST(SpirvReflectionTest, ReflectsWorkgroupSizeAndSpecializationConstants)
{
    const ShaderLayout layout = reflect_spirv(shaders::reflection_comp_spirv);
    EXPECT_EQ(layout.workgroup_size, (std::array<std::uint32_t, 3>{8, 4, 1}));
    const std::vector<SpecializationConstantInfo> expected{
        {.id = 3, .size = 4, .name = "SCALE"},
        {.id = 7, .size = 4, .name = "ENABLED"},
    };
    EXPECT_EQ(layout.specialization_constants, expected);
}

TEST(SpirvReflectionTest, WorkgroupSizeFromSpecializationConstantsIsTheirDefault)
{
    const ShaderLayout layout = reflect_spirv(shaders::specialized_comp_spirv);
    EXPECT_EQ(layout.workgroup_size, (std::array<std::uint32_t, 3>{16, 2, 1}));
    ASSERT_EQ(layout.bindings.size(), 1U);
    EXPECT_EQ(layout.bindings[0].name, "Values") << "an anonymous block is named after its block";
    EXPECT_EQ(layout.bindings[0].block_size, 0U) << "a block holding only a runtime array has no fixed part";
    const std::vector<SpecializationConstantInfo> constants{
        {.id = 0, .size = 4, .name = "GROUP_WIDTH"},
        {.id = 1, .size = 4, .name = "GROUP_HEIGHT"},
    };
    EXPECT_EQ(layout.specialization_constants, constants) << "one entry per constant identifier";
}

TEST(SpirvReflectionTest, ReflectsVertexInputsAndOutputsWithFormatsAndLocationCounts)
{
    const ShaderLayout layout = reflect_spirv(shaders::stage_io_vert_spirv, ShaderStage::VERTEX);
    const std::vector<StageVariable> inputs{
        {.location = 0, .location_count = 1, .format = Format::R32G32B32_SFLOAT, .name = "position"},
        {.location = 1, .location_count = 1, .format = Format::R32G32_SFLOAT, .name = "texture_coordinate"},
        {.location = 2, .location_count = 1, .format = Format::R32G32B32A32_UINT, .name = "joints"},
        {.location = 3, .location_count = 4, .format = Format::R32G32B32A32_SFLOAT, .name = "instance_transform"},
    };
    const std::vector<StageVariable> outputs{
        {.location = 0, .location_count = 1, .format = Format::R32G32_SFLOAT, .name = "out_texture_coordinate"},
        {.location = 1, .location_count = 1, .format = Format::R32_UINT, .name = "out_joint"},
    };
    EXPECT_EQ(layout.inputs, inputs);
    EXPECT_EQ(layout.outputs, outputs) << "built-in outputs (gl_Position) are not reported";
}

TEST(SpirvReflectionTest, ReflectsFragmentOutputs)
{
    const ShaderLayout layout = reflect_spirv(shaders::stage_io_frag_spirv, ShaderStage::FRAGMENT);
    ASSERT_EQ(layout.outputs.size(), 2U);
    EXPECT_EQ(layout.outputs[0].format, Format::R32G32B32A32_SFLOAT);
    EXPECT_EQ(layout.outputs[1].format, Format::R32_UINT);
    EXPECT_EQ(layout.outputs[1].name, "identifier");
}

TEST(SpirvReflectionTest, ReflectsHlslCompiledAtBuildTime)
{
    const ShaderLayout layout = reflect_spirv(shaders::scale_comp_spirv, ShaderStage::COMPUTE);
    ASSERT_EQ(layout.bindings.size(), 1U);
    EXPECT_EQ(layout.bindings[0].set, 0U);
    EXPECT_EQ(layout.bindings[0].binding, 0U);
    EXPECT_EQ(layout.bindings[0].type, DescriptorType::STORAGE_BUFFER);
    EXPECT_EQ(layout.bindings[0].name, "values");
    EXPECT_EQ(layout.push_constant_offset, 0U);
    EXPECT_EQ(layout.push_constant_size, 8U);
    EXPECT_EQ(layout.workgroup_size, (std::array<std::uint32_t, 3>{64, 1, 1}));
    ASSERT_EQ(layout.specialization_constants.size(), 1U);
    EXPECT_EQ(layout.specialization_constants[0].name, "GROUP_SIZE");
}

TEST(SpirvReflectionTest, ReflectedLayoutsPassPipelineValidation)
{
    // The vertex shader's outputs feed the fragment shader's inputs.
    const ShaderLayout vertex = reflect_spirv(shaders::stage_io_vert_spirv);
    const ShaderLayout fragment = reflect_spirv(shaders::stage_io_frag_spirv);
    ASSERT_EQ(vertex.outputs.size(), fragment.inputs.size());
    for (std::size_t index = 0; index < vertex.outputs.size(); ++index)
    {
        EXPECT_EQ(vertex.outputs[index].location, fragment.inputs[index].location);
        EXPECT_EQ(vertex.outputs[index].format, fragment.inputs[index].format);
    }
}

TEST(SpirvReflectionTest, RejectsCodeThatIsNotSpirv)
{
    const std::array<std::uint32_t, 6> words{0x12345678, 1, 2, 3, 4, 5};
    try
    {
        (void)reflect_spirv(words, ShaderStage::COMPUTE, "main", "broken.comp");
        FAIL() << "reflect_spirv must throw";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_TRUE(contains(error.what(), "shader 'broken.comp': the code is not SPIR-V (6 words, first word 0x12345678)"))
            << error.what();
    }
}

TEST(SpirvReflectionTest, RejectsMissingEntryPointsAndWrongStages)
{
    try
    {
        (void)reflect_spirv(shaders::reflection_comp_spirv, ShaderStage::COMPUTE, "other", "reflection.comp");
        FAIL() << "reflect_spirv must throw";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_TRUE(contains(error.what(), "it has no COMPUTE entry point named 'other'")) << error.what();
        EXPECT_TRUE(contains(error.what(), "its entry points are 'main' (COMPUTE)")) << error.what();
    }
    EXPECT_THROW((void)reflect_spirv(shaders::reflection_comp_spirv, ShaderStage::FRAGMENT), InvalidUsageError);
}

TEST(GfxAddShadersTest, GeneratedDescriptionsNameTheStageEntryPointAndFile)
{
    EXPECT_EQ(shaders::reflection_comp.stage, ShaderStage::COMPUTE);
    EXPECT_EQ(shaders::reflection_comp.source_format, ShaderSourceFormat::SPIRV);
    EXPECT_EQ(shaders::reflection_comp.spirv.data(), shaders::reflection_comp_spirv.data());
    EXPECT_EQ(shaders::reflection_comp.spirv.front(), SPIRV_MAGIC_NUMBER);
    EXPECT_EQ(shaders::reflection_comp.entry_point, "main");
    EXPECT_EQ(shaders::reflection_comp.name, "reflection.comp");
    EXPECT_EQ(shaders::stage_io_vert.stage, ShaderStage::VERTEX);
    EXPECT_EQ(shaders::stage_io_frag.stage, ShaderStage::FRAGMENT);
    EXPECT_EQ(shaders::scale_comp.name, "scale.comp.hlsl");
    EXPECT_NO_THROW(validate_shader_module_description(shaders::reflection_comp));

    // Debug builds can reload the shader from its source; Release builds carry no source paths.
    if constexpr (SHADER_HOT_RELOAD)
    {
        EXPECT_TRUE(shaders::reflection_comp.source_path.ends_with("shaders/reflection.comp"))
            << shaders::reflection_comp.source_path;
        ASSERT_EQ(shaders::reflection_comp.defines.size(), 1U);
        EXPECT_EQ(shaders::reflection_comp.defines[0].name, "TEXTURE_COUNT");
        EXPECT_EQ(shaders::reflection_comp.defines[0].value, "4");
        ASSERT_EQ(shaders::reflection_comp.include_directories.size(), 1U);
        EXPECT_TRUE(shaders::reflection_comp.include_directories[0].ends_with("shaders/include"));
    }
    else
    {
        EXPECT_TRUE(shaders::reflection_comp.source_path.empty());
        EXPECT_TRUE(shaders::reflection_comp.defines.empty());
    }
}

} // namespace
