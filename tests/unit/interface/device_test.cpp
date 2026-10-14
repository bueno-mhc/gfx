// Device, PhysicalDevice and Instance: descriptive defaults, typed creation, device selection.
#include <gtest/gtest.h>

import std;
import gfx;
import gfx.tests.fake_backend;

namespace
{

using namespace gfx;
using gfx::tests::FakeBuffer;
using gfx::tests::FakeDescriptorSet;
using gfx::tests::FakeDevice;
using gfx::tests::FakeInstance;
using gfx::tests::FakePhysicalDevice;

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
layout(location = 0) in vec3 a_position;
layout(location = 0) out vec3 v_color;
void main() {}
)glsl";

inline constexpr char MESH_FRAGMENT_SOURCE[] = R"glsl(
#version 460
layout(set = 2, binding = 0) uniform sampler2D u_albedo;
layout(location = 0) in vec3 v_color;
layout(location = 0) out vec4 o_color;
void main() {}
)glsl";

using BlurShader = GlslShader<BLUR_SOURCE, ShaderStage::COMPUTE, "blur">;
using MeshVertexShader = GlslShader<MESH_VERTEX_SOURCE, ShaderStage::VERTEX, "mesh vertex">;
using MeshFragmentShader = GlslShader<MESH_FRAGMENT_SOURCE, ShaderStage::FRAGMENT, "mesh fragment">;

inline constexpr PipelineLayoutDefinition BLUR_LAYOUT = pipeline_layout_from<BlurShader>();
inline constexpr PipelineLayoutDefinition MESH_LAYOUT = pipeline_layout_from<MeshVertexShader, MeshFragmentShader>();
inline constexpr VertexLayoutDefinition MESH_VERTICES = VertexLayoutDefinition{}
    .with_binding(0, 12)
    .with_attribute(0, 0, Format::R32G32B32_SFLOAT, 0);
inline constexpr AttachmentLayoutDefinition MESH_ATTACHMENTS = AttachmentLayoutDefinition{}
    .with_color(Format::B8G8R8A8_SRGB)
    .with_depth(Format::D32_SFLOAT);

TEST(DeviceTest, BackendsMustOverrideEveryOperation)
{
    static_assert(std::is_abstract_v<Device> && std::is_abstract_v<CommandBuffer> && std::is_abstract_v<Buffer>);
    static_assert(!std::is_copy_constructible_v<FakeDevice> && !std::is_move_constructible_v<FakeDevice>,
                  "backend objects have a stable address and are owned through std::unique_ptr");
    SUCCEED();
}

TEST(DeviceTest, UnimplementedOperationsThrowADescriptiveError)
{
    FakePhysicalDevice physical_device{PhysicalDeviceType::DISCRETE_GPU};
    FakeDevice device{physical_device};
    try
    {
        (void)device.create_event({});
        FAIL() << "BackendCapabilityError was not thrown";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_EQ(error.backend(), BackendKind::NULL_BACKEND);
        EXPECT_EQ(error.operation(), "Device::create_event");
        EXPECT_TRUE(std::string_view{error.what()}.starts_with(
            "[gfx::NULL_BACKEND] Device::create_event: capability 'Device::create_event' is not supported.\n"
            "  Reason: the NULL_BACKEND backend does not implement this operation yet\n"
            "  Suggestion: implement Device::create_event in the NULL_BACKEND backend, or use a backend that provides it"))
            << error.what();
    }
    // Through the interface, as user code does (default arguments belong to the interface).
    Device& interface = device;
    EXPECT_THROW((void)interface.create_buffer({.size = 16}), BackendCapabilityError);
    EXPECT_THROW((void)interface.queue(QueueType::COMPUTE), BackendCapabilityError);
    EXPECT_THROW(interface.deleter().enqueue([] {}), BackendCapabilityError);
}

TEST(DeviceTest, TypedShaderCreationPassesTheCompileTimeLayout)
{
    FakePhysicalDevice physical_device{PhysicalDeviceType::DISCRETE_GPU};
    FakeDevice device{physical_device};
    Device& interface = device;

    const TypedShaderModule<BlurShader> shader = interface.create_shader<BlurShader>();

    const tests::CapturedDescriptions& captured = device.captured();
    ASSERT_TRUE(captured.shader_module.has_value());
    EXPECT_EQ(captured.shader_module->stage, ShaderStage::COMPUTE);
    EXPECT_EQ(captured.shader_module->source_format, ShaderSourceFormat::GLSL);
    EXPECT_EQ(captured.shader_source, BlurShader::SOURCE);
    EXPECT_EQ(captured.shader_name, "blur");
    ASSERT_TRUE(captured.shader_module->expected_layout.has_value());
    EXPECT_EQ(*captured.shader_module->expected_layout, BlurShader::LAYOUT);
    EXPECT_TRUE(shader.handle().is_valid());
}

TEST(DeviceTest, TypedPipelineLayoutCreatesOneSetLayoutPerSet)
{
    FakePhysicalDevice physical_device{PhysicalDeviceType::DISCRETE_GPU};
    FakeDevice device{physical_device};
    Device& interface = device;

    const TypedPipelineLayout<MESH_LAYOUT> layout = interface.create_pipeline_layout<MESH_LAYOUT>("mesh");

    // Sets 0 and 2 are used, so set 1 gets an empty layout.
    const tests::CapturedDescriptions& captured = device.captured();
    ASSERT_EQ(captured.set_layout_bindings.size(), 3U);
    ASSERT_EQ(captured.set_layout_bindings[0].size(), 1U);
    EXPECT_EQ(captured.set_layout_bindings[0][0].type, DescriptorType::UNIFORM_BUFFER);
    EXPECT_EQ(captured.set_layout_bindings[0][0].stages, ShaderStage::VERTEX);
    EXPECT_TRUE(captured.set_layout_bindings[1].empty());
    EXPECT_EQ(captured.set_layout_bindings[2][0].type, DescriptorType::COMBINED_IMAGE_SAMPLER);
    EXPECT_EQ(captured.pipeline_layout_set_layouts.size(), 3U);
    EXPECT_TRUE(captured.push_constant_ranges.empty());
    EXPECT_EQ(layout.set_layout(2).handle(), captured.pipeline_layout_set_layouts[2]);
    EXPECT_THROW((void)layout.set_layout(3), InvalidUsageError);
}

TEST(DeviceTest, TypedComputePipelineCombinesTheTypedObjects)
{
    FakePhysicalDevice physical_device{PhysicalDeviceType::DISCRETE_GPU};
    FakeDevice device{physical_device};
    Device& interface = device;

    const TypedPipelineLayout<BLUR_LAYOUT> layout = interface.create_pipeline_layout<BLUR_LAYOUT>();
    const TypedShaderModule<BlurShader> shader = interface.create_shader<BlurShader>();
    const std::unique_ptr<ComputePipeline> pipeline = interface.create_compute_pipeline(layout, shader, "blur");

    const tests::CapturedDescriptions& captured = device.captured();
    ASSERT_TRUE(captured.compute_pipeline.has_value());
    EXPECT_EQ(captured.compute_pipeline->layout, layout.handle());
    EXPECT_EQ(captured.compute_pipeline->shader, shader.handle());
    ASSERT_EQ(captured.push_constant_ranges.size(), 1U);
    EXPECT_EQ(captured.push_constant_ranges[0].size, 12U);
    EXPECT_EQ(captured.push_constant_ranges[0].stages, ShaderStage::COMPUTE);
    EXPECT_EQ(pipeline->layout(), layout.handle());
}

TEST(DeviceTest, TypedGraphicsPipelinePassesVertexAndAttachmentLayouts)
{
    FakePhysicalDevice physical_device{PhysicalDeviceType::DISCRETE_GPU};
    FakeDevice device{physical_device};
    Device& interface = device;

    const TypedPipelineLayout<MESH_LAYOUT> layout = interface.create_pipeline_layout<MESH_LAYOUT>();
    const TypedShaderModule<MeshVertexShader> vertex = interface.create_shader<MeshVertexShader>();
    const TypedShaderModule<MeshFragmentShader> fragment = interface.create_shader<MeshFragmentShader>();
    const std::unique_ptr<GraphicsPipeline> pipeline =
        interface.create_graphics_pipeline<MESH_VERTICES, MESH_ATTACHMENTS>(layout, vertex, fragment, {.debug_name = "mesh"});

    const tests::CapturedDescriptions& captured = device.captured();
    ASSERT_TRUE(captured.graphics_pipeline.has_value());
    EXPECT_EQ(captured.graphics_pipeline->vertex_shader, vertex.handle());
    EXPECT_EQ(captured.graphics_pipeline->fragment_shader, fragment.handle());
    EXPECT_EQ(captured.vertex_attributes, (std::vector{VertexInputAttribute{0, 0, Format::R32G32B32_SFLOAT, 0}}));
    EXPECT_EQ(captured.color_formats, (std::vector{Format::B8G8R8A8_SRGB}));
    EXPECT_EQ(captured.graphics_pipeline->depth_format, Format::D32_SFLOAT);
    EXPECT_EQ(captured.graphics_pipeline->stencil_format, Format::UNDEFINED);
    EXPECT_EQ(captured.graphics_pipeline->state.debug_name, "mesh");
}

TEST(DeviceTest, DescriptorWritesResolveBindingsByName)
{
    FakeDescriptorSet set;
    set.write<"u_output", BlurShader>(ImageDescriptor{.layout = ImageLayout::GENERAL});
    set.write<"u_input", BlurShader>(CombinedImageSamplerDescriptor{});

    EXPECT_EQ(set.written_bindings(), (std::vector<std::uint32_t>{1, 0}));
    ASSERT_EQ(set.written_resources().size(), 2U);
    EXPECT_EQ(std::get<ImageDescriptor>(set.written_resources()[0]).layout, ImageLayout::GENERAL);
    EXPECT_TRUE(std::holds_alternative<CombinedImageSamplerDescriptor>(set.written_resources()[1]));

    static_assert(descriptor_resource_matches(DescriptorType::UNIFORM_BUFFER_DYNAMIC, DescriptorResource{BufferDescriptor{}}));
    static_assert(!descriptor_resource_matches(DescriptorType::STORAGE_IMAGE, DescriptorResource{SamplerDescriptor{}}));
}

TEST(DeviceTest, MappedBuffersCanBeViewedAsElements)
{
    FakeBuffer buffer;
    const std::span<std::uint32_t> words = buffer.mapped_elements<std::uint32_t>();
    EXPECT_EQ(words.size(), 16U);
    words[3] = 0xCAFEU;
    EXPECT_EQ(buffer.mapped_elements<std::uint32_t>()[3], 0xCAFEU);
    Buffer& interface = buffer;
    EXPECT_THROW(interface.flush_mapped_range(), BackendCapabilityError);
}

TEST(DeviceTest, PhysicalDevicesAreSelectedByPreference)
{
    FakeInstance instance{PhysicalDeviceType::CPU, PhysicalDeviceType::INTEGRATED_GPU, PhysicalDeviceType::DISCRETE_GPU};
    EXPECT_EQ(instance.select_physical_device().properties().type, PhysicalDeviceType::DISCRETE_GPU);
    EXPECT_EQ(instance.select_physical_device(PhysicalDevicePreference::LOW_POWER).properties().type,
              PhysicalDeviceType::INTEGRATED_GPU);
    EXPECT_THROW((void)instance.physical_device(3), BackendCapabilityError);

    FakeInstance empty{};
    EXPECT_THROW((void)empty.select_physical_device(), BackendCapabilityError);
}

TEST(DeviceTest, MissingFeaturesAreListed)
{
    const FakePhysicalDevice physical_device{PhysicalDeviceType::DISCRETE_GPU,
                                             {DeviceFeature::TIMELINE_SEMAPHORE, DeviceFeature::DYNAMIC_RENDERING}};
    const std::array required{DeviceFeature::TIMELINE_SEMAPHORE, DeviceFeature::MESH_SHADER, DeviceFeature::RAY_TRACING};
    EXPECT_EQ(physical_device.missing_features(required),
              (std::vector{DeviceFeature::MESH_SHADER, DeviceFeature::RAY_TRACING}));
}

} // namespace
