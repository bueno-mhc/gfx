// Device, PhysicalDevice and Instance: descriptive defaults, shader modules and their reflected
// layouts, pipeline layouts derived from shaders, pipeline validation, descriptor writes, hot reload,
// device selection.
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

// SPIR-V the fake device accepts: its "reflection" ignores the code and reads the registered layout.
inline constexpr std::array<std::uint32_t, 1> FAKE_SPIRV{SPIRV_MAGIC_NUMBER};

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
        .inputs = {{.location = 0, .format = Format::R32G32B32_SFLOAT, .name = "a_position"}},
        .outputs = {{.location = 0, .format = Format::R32G32B32_SFLOAT, .name = "v_color"}},
    };
}

auto mesh_fragment_layout() -> ShaderLayout
{
    return {
        .stage = ShaderStage::FRAGMENT,
        .bindings = {{.set = 2, .binding = 0, .type = DescriptorType::COMBINED_IMAGE_SAMPLER, .name = "u_albedo"}},
        .push_constant_offset = 64,
        .push_constant_size = 16,
        .inputs = {{.location = 0, .format = Format::R32G32B32_SFLOAT, .name = "v_color"}},
        .outputs = {{.location = 0, .format = Format::R32G32B32A32_SFLOAT, .name = "o_color"}},
    };
}

/// A fake device whose "reflection" knows the three shaders above.
class DeviceTest : public ::testing::Test
{
protected:
    DeviceTest()
    {
        m_device.add_reflected_layout("blur", blur_layout());
        m_device.add_reflected_layout("mesh vertex", mesh_vertex_layout());
        m_device.add_reflected_layout("mesh fragment", mesh_fragment_layout());
    }

    /// Creates the module called @p name, as user code does: through the Device interface.
    auto create_shader(std::string_view name, ShaderStage stage) -> std::unique_ptr<ShaderModule>
    {
        return device().create_shader_module({.stage = stage, .spirv = FAKE_SPIRV, .name = name});
    }

    auto device() -> Device&
    {
        return m_device;
    }

protected:
    FakePhysicalDevice m_physical_device{PhysicalDeviceType::DISCRETE_GPU};
    FakeDevice m_device{m_physical_device};
};

TEST_F(DeviceTest, BackendsMustOverrideEveryOperation)
{
    static_assert(std::is_abstract_v<Device> && std::is_abstract_v<CommandBuffer> && std::is_abstract_v<Buffer>);
    static_assert(!std::is_copy_constructible_v<FakeDevice> && !std::is_move_constructible_v<FakeDevice>,
                  "backend objects have a stable address and are owned through std::unique_ptr");
    SUCCEED();
}

TEST_F(DeviceTest, UnimplementedOperationsThrowADescriptiveError)
{
    try
    {
        (void)m_device.create_event({});
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
    EXPECT_THROW((void)device().create_image({.format = Format::R8G8B8A8_UNORM}), BackendCapabilityError);
    EXPECT_THROW((void)device().queue(QueueType::COMPUTE), BackendCapabilityError);
    EXPECT_THROW(device().deleter().enqueue([] {}), BackendCapabilityError);
}

TEST_F(DeviceTest, ShaderModulesExposeTheLayoutReflectedFromTheirCode)
{
    const std::unique_ptr<ShaderModule> shader = create_shader("blur", ShaderStage::COMPUTE);
    EXPECT_EQ(shader->layout(), blur_layout());
    EXPECT_EQ(shader->debug_name(), "blur");
    EXPECT_TRUE(shader->handle().is_valid());
    EXPECT_EQ(m_device.captured().shader_module_names, (std::vector<std::string>{"blur"}));

    // The backend validates the description before reflecting anything.
    const std::array<std::uint32_t, 1> not_spirv{0xDEADBEEFU};
    EXPECT_THROW((void)device().create_shader_module({.stage = ShaderStage::COMPUTE, .spirv = not_spirv, .name = "blur"}),
                 InvalidUsageError);
}

TEST_F(DeviceTest, GlslModulesAreCreatedOnlyInDebugBuilds)
{
    const ShaderModuleDescription glsl{.stage = ShaderStage::COMPUTE,
                                       .source_format = ShaderSourceFormat::GLSL,
                                       .source = "#version 460\nvoid main() {}\n",
                                       .source_path = "shaders/blur.comp.glsl",
                                       .name = "blur"};
    if constexpr (SHADER_HOT_RELOAD)
    {
        EXPECT_NO_THROW((void)device().create_shader_module(glsl));
    }
    else
    {
        EXPECT_THROW((void)device().create_shader_module(glsl), InvalidUsageError);
    }
}

TEST_F(DeviceTest, PipelineLayoutsFromShadersCreateOneSetLayoutPerSet)
{
    const std::unique_ptr<ShaderModule> vertex = create_shader("mesh vertex", ShaderStage::VERTEX);
    const std::unique_ptr<ShaderModule> fragment = create_shader("mesh fragment", ShaderStage::FRAGMENT);
    const ReflectedPipelineLayout layout = device().create_pipeline_layout_from({vertex.get(), fragment.get()}, "mesh");

    // Sets 0 and 2 are used, so set 1 gets an empty layout; bindings keep their names and stages.
    const tests::CapturedDescriptions& captured = m_device.captured();
    ASSERT_EQ(captured.set_layout_bindings.size(), 3U);
    ASSERT_EQ(captured.set_layout_bindings[0].size(), 1U);
    EXPECT_EQ(captured.set_layout_bindings[0][0].type, DescriptorType::UNIFORM_BUFFER);
    EXPECT_EQ(captured.set_layout_bindings[0][0].stages, ShaderStage::VERTEX);
    EXPECT_EQ(captured.set_layout_bindings[0][0].name, "camera");
    EXPECT_TRUE(captured.set_layout_bindings[1].empty());
    EXPECT_EQ(captured.set_layout_bindings[2][0].type, DescriptorType::COMBINED_IMAGE_SAMPLER);
    EXPECT_EQ(captured.pipeline_layout_set_layouts.size(), 3U);
    EXPECT_EQ(layout.set_count(), 3U);
    EXPECT_EQ(layout.set_layout(2).handle(), captured.pipeline_layout_set_layouts[2]);
    EXPECT_THROW((void)layout.set_layout(3), InvalidUsageError);

    // One push-constant range covers the vertex bytes [0, 64) and the fragment bytes [64, 80).
    ASSERT_EQ(captured.push_constant_ranges.size(), 1U);
    EXPECT_EQ(captured.push_constant_ranges[0], (PushConstantRange{.stages = ShaderStage::VERTEX | ShaderStage::FRAGMENT,
                                                                   .offset = 0,
                                                                   .size = 80}));
    EXPECT_EQ(layout.definition().push_constant_ranges, captured.push_constant_ranges);
}

TEST_F(DeviceTest, ConflictingShaderBindingsAreReported)
{
    ShaderLayout conflicting = mesh_fragment_layout();
    conflicting.bindings[0].set = 0; // set 0 binding 0 is the vertex shader's uniform block
    m_device.add_reflected_layout("conflicting fragment", conflicting);
    const std::unique_ptr<ShaderModule> vertex = create_shader("mesh vertex", ShaderStage::VERTEX);
    const std::unique_ptr<ShaderModule> fragment = create_shader("conflicting fragment", ShaderStage::FRAGMENT);
    try
    {
        (void)device().create_pipeline_layout_from({vertex.get(), fragment.get()});
        FAIL() << "ShaderLayoutMismatchError was not thrown";
    }
    catch (const ShaderLayoutMismatchError& error)
    {
        EXPECT_EQ(error.mismatch(), LayoutMismatch::CONFLICTING_BINDINGS);
        EXPECT_EQ(error.shader_name(), "conflicting fragment");
        EXPECT_EQ(error.problem(), "set 0 binding 0 is declared as COMBINED_IMAGE_SAMPLER x1 by this shader and as "
                                   "UNIFORM_BUFFER x1 by an earlier one");
    }
}

TEST_F(DeviceTest, ComputePipelinesAreValidatedAgainstTheirLayout)
{
    const std::unique_ptr<ShaderModule> shader = create_shader("blur", ShaderStage::COMPUTE);
    const ReflectedPipelineLayout layout = device().create_pipeline_layout_from({shader.get()});
    const std::unique_ptr<ComputePipeline> pipeline =
        device().create_compute_pipeline({.layout = layout.handle(), .shader = shader->handle(), .debug_name = "blur"});
    EXPECT_EQ(pipeline->layout(), layout.handle());

    // An explicit layout without u_output does not fit the shader.
    const std::array bindings{DescriptorSetLayoutBinding{.binding = 0,
                                                         .type = DescriptorType::COMBINED_IMAGE_SAMPLER,
                                                         .stages = ShaderStage::COMPUTE,
                                                         .name = "u_input"}};
    const std::unique_ptr<DescriptorSetLayout> set_layout = device().create_descriptor_set_layout({.bindings = bindings});
    const std::array set_layouts{set_layout->handle()};
    const std::array push_constants{PushConstantRange{.stages = ShaderStage::COMPUTE, .offset = 0, .size = 12}};
    const std::unique_ptr<PipelineLayout> incomplete = device().create_pipeline_layout(
        {.set_layouts = set_layouts, .push_constant_ranges = push_constants, .debug_name = "incomplete"});
    try
    {
        (void)device().create_compute_pipeline({.layout = incomplete->handle(), .shader = shader->handle()});
        FAIL() << "ShaderLayoutMismatchError was not thrown";
    }
    catch (const ShaderLayoutMismatchError& error)
    {
        EXPECT_EQ(error.mismatch(), LayoutMismatch::BINDING_MISSING_FROM_LAYOUT);
        EXPECT_TRUE(std::string_view{error.what()}.starts_with(
            "[gfx::NULL_BACKEND] Device::create_compute_pipeline: shader 'blur' does not match its layout "
            "(BINDING_MISSING_FROM_LAYOUT): the shader uses set 0 binding 1 (STORAGE_IMAGE), which the pipeline "
            "layout does not declare"))
            << error.what();
    }

    // A vertex shader is not a compute shader.
    const std::unique_ptr<ShaderModule> vertex = create_shader("mesh vertex", ShaderStage::VERTEX);
    EXPECT_THROW((void)device().create_compute_pipeline({.layout = layout.handle(), .shader = vertex->handle()}),
                 ShaderLayoutMismatchError);
}

TEST_F(DeviceTest, GraphicsPipelinesAreValidatedAgainstAttributesAndAttachments)
{
    const std::unique_ptr<ShaderModule> vertex = create_shader("mesh vertex", ShaderStage::VERTEX);
    const std::unique_ptr<ShaderModule> fragment = create_shader("mesh fragment", ShaderStage::FRAGMENT);
    const ReflectedPipelineLayout layout = device().create_pipeline_layout_from({vertex.get(), fragment.get()});
    const std::array vertex_bindings{VertexInputBinding{.binding = 0, .stride = 12}};
    const std::array attributes{VertexInputAttribute{.location = 0, .binding = 0, .format = Format::R32G32B32_SFLOAT}};
    const std::array color_formats{Format::B8G8R8A8_SRGB};
    const GraphicsPipelineDescription description{
        .layout = layout.handle(),
        .vertex_shader = vertex->handle(),
        .fragment_shader = fragment->handle(),
        .vertex_bindings = vertex_bindings,
        .vertex_attributes = attributes,
        .color_formats = color_formats,
        .depth_format = Format::D32_SFLOAT,
        .state = {.debug_name = "mesh"},
    };
    EXPECT_NO_THROW((void)device().create_graphics_pipeline(description));
    EXPECT_EQ(m_device.captured().color_formats, (std::vector{Format::B8G8R8A8_SRGB}));

    // Without the position attribute.
    GraphicsPipelineDescription no_attributes = description;
    no_attributes.vertex_attributes = {};
    try
    {
        (void)device().create_graphics_pipeline(no_attributes);
        FAIL() << "ShaderLayoutMismatchError was not thrown";
    }
    catch (const ShaderLayoutMismatchError& error)
    {
        EXPECT_EQ(error.mismatch(), LayoutMismatch::VERTEX_INPUT_WITHOUT_ATTRIBUTE);
        EXPECT_EQ(error.shader_name(), "mesh vertex");
    }

    // An integer attachment for a float output.
    const std::array integer_color{Format::R32G32B32A32_UINT};
    GraphicsPipelineDescription integer_attachment = description;
    integer_attachment.color_formats = integer_color;
    EXPECT_THROW((void)device().create_graphics_pipeline(integer_attachment), ShaderLayoutMismatchError);

    // Depth only: no fragment shader to check against attachments.
    GraphicsPipelineDescription depth_only = description;
    depth_only.fragment_shader = {};
    depth_only.color_formats = {};
    EXPECT_NO_THROW((void)device().create_graphics_pipeline(depth_only));
}

TEST_F(DeviceTest, BindingsAreLocatedByNameOnce)
{
    const std::unique_ptr<ShaderModule> shader = create_shader("blur", ShaderStage::COMPUTE);
    const ReflectedPipelineLayout layout = device().create_pipeline_layout_from({shader.get()});
    EXPECT_EQ(layout.find_binding("u_output"),
              (BindingLocation{.set = 0, .binding = 1, .type = DescriptorType::STORAGE_IMAGE, .count = 1}));
    try
    {
        (void)layout.find_binding("u_ouput");
        FAIL() << "InvalidUsageError was not thrown";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_EQ(error.problem(), "pipeline layout 'fake' has no binding named 'u_ouput'");
        EXPECT_EQ(error.suggestion(), "its named bindings are 'u_input' (set 0, binding 0), 'u_output' (set 0, binding 1)");
    }
}

TEST_F(DeviceTest, DescriptorWritesAreCheckedAgainstTheBinding)
{
    const BindingLocation output{.set = 0, .binding = 1, .type = DescriptorType::STORAGE_IMAGE, .count = 1};
    const BindingLocation input{.set = 0, .binding = 0, .type = DescriptorType::COMBINED_IMAGE_SAMPLER, .count = 1};
    FakeDescriptorSet set;
    set.write(output, ImageDescriptor{.layout = ImageLayout::GENERAL});
    set.write(input, CombinedImageSamplerDescriptor{});
    set.write(3, BufferDescriptor{}); // by index: checked by the backend
    EXPECT_EQ(set.written_bindings(), (std::vector<std::uint32_t>{1, 0, 3}));
    EXPECT_EQ(std::get<ImageDescriptor>(set.written_resources()[0]).layout, ImageLayout::GENERAL);

    try
    {
        set.write(output, BufferDescriptor{});
        FAIL() << "InvalidUsageError was not thrown";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_EQ(error.problem(), "set 0 binding 1 is STORAGE_IMAGE, which takes ImageDescriptor, not BufferDescriptor");
    }
    EXPECT_THROW(set.write(output, ImageDescriptor{}, 1), InvalidUsageError) << "element 1 of a 1-element binding";
    EXPECT_EQ(set.written_bindings().size(), 3U) << "rejected writes reach no backend";

    static_assert(descriptor_resource_matches(DescriptorType::UNIFORM_BUFFER_DYNAMIC, DescriptorResource{BufferDescriptor{}}));
    static_assert(!descriptor_resource_matches(DescriptorType::STORAGE_IMAGE, DescriptorResource{SamplerDescriptor{}}));
}

TEST_F(DeviceTest, ShaderHotReloadExistsOnlyInDebugBuilds)
{
    if constexpr (SHADER_HOT_RELOAD)
    {
        // Debug: the request reaches the backend (which does not implement it yet).
        EXPECT_THROW((void)device().poll_shader_changes(), BackendCapabilityError);
    }
    else
    {
        // Release: nothing is reloaded, and nothing reaches the backend.
        EXPECT_TRUE(device().poll_shader_changes().empty());
    }
}

TEST(DeviceObjectsTest, MappedBuffersCanBeViewedAsElements)
{
    FakeBuffer buffer;
    const std::span<std::uint32_t> words = buffer.mapped_elements<std::uint32_t>();
    EXPECT_EQ(words.size(), 16U);
    words[3] = 0xCAFEU;
    EXPECT_EQ(buffer.mapped_elements<std::uint32_t>()[3], 0xCAFEU);
    Buffer& interface = buffer;
    EXPECT_THROW(interface.flush_mapped_range(), BackendCapabilityError);
}

TEST(DeviceObjectsTest, PhysicalDevicesAreSelectedByPreference)
{
    FakeInstance instance{PhysicalDeviceType::CPU, PhysicalDeviceType::INTEGRATED_GPU, PhysicalDeviceType::DISCRETE_GPU};
    EXPECT_EQ(instance.select_physical_device().properties().type, PhysicalDeviceType::DISCRETE_GPU);
    EXPECT_EQ(instance.select_physical_device(PhysicalDevicePreference::LOW_POWER).properties().type,
              PhysicalDeviceType::INTEGRATED_GPU);
    EXPECT_THROW((void)instance.physical_device(3), BackendCapabilityError);

    FakeInstance empty{};
    EXPECT_THROW((void)empty.select_physical_device(), BackendCapabilityError);
}

TEST(DeviceObjectsTest, DevicesWithoutTheCoreFeaturesAreNotSelected)
{
    // A discrete GPU without bindless descriptors loses to an integrated GPU that has them.
    FakeInstance instance{PhysicalDeviceType::INTEGRATED_GPU};
    instance.add_physical_device(PhysicalDeviceType::DISCRETE_GPU, {DeviceFeature::TIMELINE_SEMAPHORE, DeviceFeature::DYNAMIC_RENDERING});
    EXPECT_EQ(instance.select_physical_device().properties().type, PhysicalDeviceType::INTEGRATED_GPU);
    EXPECT_EQ(instance.physical_device(1).missing_core_features(), (std::vector{DeviceFeature::DESCRIPTOR_INDEXING}));

    // With no capable device, the error says what each one lacks.
    FakeInstance outdated{};
    outdated.add_physical_device(PhysicalDeviceType::DISCRETE_GPU, {});
    try
    {
        (void)outdated.select_physical_device();
        FAIL() << "BackendCapabilityError was not thrown";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_EQ(error.reason(), "no physical device has every core feature: 'fake DISCRETE_GPU' lacks TIMELINE_SEMAPHORE, "
                                  "DYNAMIC_RENDERING, DESCRIPTOR_INDEXING");
    }
}

TEST(DeviceObjectsTest, HlslIsAcceptedOnlyInDebugBuilds)
{
    const ShaderModuleDescription hlsl{.stage = ShaderStage::COMPUTE,
                                       .source_format = ShaderSourceFormat::HLSL,
                                       .source = "[numthreads(8, 8, 1)] void main() {}",
                                       .entry_point = "main",
                                       .name = "blur"};
    if constexpr (SHADER_HOT_RELOAD)
    {
        EXPECT_NO_THROW(validate_shader_module_description(hlsl));
    }
    else
    {
        EXPECT_THROW(validate_shader_module_description(hlsl), InvalidUsageError);
    }
}

TEST(DeviceObjectsTest, MissingFeaturesAreListed)
{
    const FakePhysicalDevice physical_device{PhysicalDeviceType::DISCRETE_GPU,
                                             {DeviceFeature::TIMELINE_SEMAPHORE, DeviceFeature::DYNAMIC_RENDERING}};
    const std::array required{DeviceFeature::TIMELINE_SEMAPHORE, DeviceFeature::MESH_SHADER, DeviceFeature::RAY_TRACING};
    EXPECT_EQ(physical_device.missing_features(required),
              (std::vector{DeviceFeature::MESH_SHADER, DeviceFeature::RAY_TRACING}));
}

} // namespace
