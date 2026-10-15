// The bindless model: GPU-visible indices, the universal pipeline layout every pipeline uses by
// default, and shaders validated against it.
#include <gtest/gtest.h>

import std;
import gfx;
import gfx.tests.fake_backend;

namespace
{

using namespace gfx;
using gfx::tests::FakeBuffer;
using gfx::tests::FakeCommandBuffer;
using gfx::tests::FakeDevice;
using gfx::tests::FakePhysicalDevice;

inline constexpr std::array<std::uint32_t, 1> FAKE_SPIRV{SPIRV_MAGIC_NUMBER};

/// A compute shader reading textures and samplers through the bindless table, with 8 bytes of push
/// constants (two indices): what SPIR-V reflection reports for
///   layout(set = 0, binding = 0) uniform texture2D gfx_sampled_images[];
///   layout(set = 0, binding = 2) uniform sampler gfx_samplers[];
///   layout(push_constant) uniform Draw { uint texture; uint sampler_index; } draw;
auto bindless_compute_layout() -> ShaderLayout
{
    return {
        .stage = ShaderStage::COMPUTE,
        .bindings = {{.set = 0, .binding = 0, .type = DescriptorType::SAMPLED_IMAGE, .count = 0, .name = "gfx_sampled_images"},
                     {.set = 0, .binding = 2, .type = DescriptorType::SAMPLER, .count = 0, .name = "gfx_samplers"}},
        .push_constant_offset = 0,
        .push_constant_size = 8,
        .workgroup_size = {8, 8, 1},
    };
}

struct DrawIndices
{
    SampledImageIndex texture;
    SamplerIndex sampler;
};

TEST(BindlessTest, IndicesAreGpuData)
{
    static_assert(sizeof(DrawIndices) == 8 && std::is_trivially_copyable_v<DrawIndices>, "indices go into push constants as-is");
    constexpr SampledImageIndex invalid{};
    constexpr SampledImageIndex texture{42};
    static_assert(!invalid.is_valid() && invalid.value() == INVALID_BINDLESS_INDEX);
    static_assert(texture.is_valid() && texture.value() == 42 && SampledImageIndex::KIND == BindlessResourceKind::SAMPLED_IMAGE);
    static_assert(!std::is_convertible_v<SampledImageIndex, StorageImageIndex>, "indices of different kinds do not mix");
    EXPECT_EQ(std::format("{}", texture), "SAMPLED_IMAGE 42");
    EXPECT_EQ(std::format("{}", StorageBufferIndex{}), "STORAGE_BUFFER invalid");
}

TEST(BindlessTest, TheBindlessLayoutHasOneArrayPerKindAndPushConstantsForEveryStage)
{
    constexpr std::array kinds{BindlessResourceKind::SAMPLED_IMAGE, BindlessResourceKind::SAMPLER, BindlessResourceKind::STORAGE_BUFFER};
    const PipelineLayoutDefinition layout = bindless_pipeline_layout_definition({.sampled_image_capacity = 1000}, kinds);
    ASSERT_EQ(layout.bindings.size(), 3U);
    const LayoutBinding* textures = layout.find("gfx_sampled_images");
    ASSERT_NE(textures, nullptr);
    EXPECT_EQ(textures->set, BINDLESS_SET);
    EXPECT_EQ(textures->binding, 0U);
    EXPECT_EQ(textures->count, 1000U);
    EXPECT_EQ(textures->stages, ShaderStage::ALL);
    EXPECT_TRUE(has_all(textures->options, DescriptorBindingOption::PARTIALLY_BOUND | DescriptorBindingOption::UPDATE_AFTER_BIND));
    EXPECT_EQ(layout.find(0, 3)->type, DescriptorType::STORAGE_BUFFER) << "binding index = BindlessResourceKind value";
    EXPECT_EQ(layout.push_constant_ranges,
              (std::vector{PushConstantRange{.stages = ShaderStage::ALL, .offset = 0, .size = BINDLESS_PUSH_CONSTANT_SIZE}}));
    EXPECT_EQ(BindlessTableDescription{}.capacity(BindlessResourceKind::SAMPLER), DEFAULT_BINDLESS_SAMPLER_CAPACITY);
}

TEST(BindlessTest, ShadersAreCheckedAgainstTheBindlessLayout)
{
    const PipelineLayoutDefinition& bindless = tests::default_bindless_layout().definition();
    EXPECT_TRUE(check_pipeline_layout(bindless_compute_layout(), bindless).ok()) << "runtime-sized arrays fit the table's capacity";

    // A shader that declares the texture array as storage images does not.
    ShaderLayout wrong = bindless_compute_layout();
    wrong.bindings[0].type = DescriptorType::STORAGE_IMAGE;
    EXPECT_EQ(check_pipeline_layout(wrong, bindless).mismatch, LayoutMismatch::DESCRIPTOR_TYPE_MISMATCH);

    // Nor one that reads more push constants than the bindless layout has.
    ShaderLayout too_many = bindless_compute_layout();
    too_many.push_constant_size = BINDLESS_PUSH_CONSTANT_SIZE + 4;
    EXPECT_EQ(check_pipeline_layout(too_many, bindless).mismatch, LayoutMismatch::PUSH_CONSTANTS_OUTSIDE_RANGE);
}

TEST(BindlessTest, PipelinesWithoutALayoutUseTheBindlessOne)
{
    FakePhysicalDevice physical_device{PhysicalDeviceType::DISCRETE_GPU};
    FakeDevice device{physical_device};
    device.add_reflected_layout("bindless blur", bindless_compute_layout());
    Device& interface = device;
    const std::unique_ptr<ShaderModule> shader =
        interface.create_shader_module({.stage = ShaderStage::COMPUTE, .spirv = FAKE_SPIRV, .name = "bindless blur"});

    // No layout, no descriptor sets: the pipeline uses the device's bindless layout.
    const std::unique_ptr<ComputePipeline> pipeline = interface.create_compute_pipeline({.shader = shader->handle()});
    EXPECT_EQ(pipeline->layout(), interface.bindless_pipeline_layout().handle());
    EXPECT_EQ(pipeline->status(), PipelineStatus::READY);
}

TEST(BindlessTest, StorageBuffersHaveABindlessIndex)
{
    const FakeBuffer storage{256, BufferUsage::STORAGE, 7};
    const FakeBuffer vertices{256, BufferUsage::VERTEX, 8};
    EXPECT_EQ(storage.bindless_index(), StorageBufferIndex{7});
    EXPECT_FALSE(vertices.bindless_index().is_valid()) << "only STORAGE buffers are in the table";
}

TEST(BindlessTest, PushConstantsDefaultToTheBindlessLayout)
{
    FakeCommandBuffer command_buffer{QueueType::GRAPHICS};
    {
        ComputeRecorder recorder{command_buffer};
        recorder.push_constants(DrawIndices{SampledImageIndex{3}, SamplerIndex{1}});
    }
    EXPECT_EQ(command_buffer.log()[1], std::format("push_constants {} 0 8", ShaderStage::ALL));
    EXPECT_EQ(command_buffer.statistics().push_constant_writes, 1U);
    EXPECT_EQ(command_buffer.statistics().descriptor_set_binds, 0U) << "bindless: nothing to bind";
}

} // namespace
