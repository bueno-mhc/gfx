#include "vulkan_test.hpp"

import gfx.tests.vulkan_shaders;

namespace
{

using namespace gfx;
using gfx::tests::VulkanDeviceTest;
using namespace std::chrono_literals;

const std::filesystem::path WORK_DIRECTORY{GFX_TEST_WORK_DIRECTORY};
const std::string INCLUDE_DIRECTORY = (std::filesystem::path{GFX_TEST_VULKAN_SHADER_DIRECTORY} / "include").generic_string();

/// @brief Writes @p text to @p path and moves its modification time forward (file systems with a
///        coarse clock would otherwise report the same time for two quick writes).
void
write_file(const std::filesystem::path& path, std::string_view text)
{
    static std::filesystem::file_time_type last{};
    {
        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        stream << text;
    }
    last = std::max(std::filesystem::file_time_type::clock::now(), last + 2s);
    std::filesystem::last_write_time(path, last);
}

/// @brief A compute shader writing @p expression into the bindless buffer named by push constants.
auto
fill_source(std::string_view expression) -> std::string
{
    return std::format("#version 460\n"
                       "#extension GL_GOOGLE_include_directive : require\n"
                       "#include \"bindless.glsl\"\n"
                       "layout(local_size_x = 64) in;\n"
                       "layout(push_constant) uniform Push {{ uint target; uint count; uint value; }} push;\n"
                       "void main() {{ const uint index = gl_GlobalInvocationID.x;\n"
                       "  if (index < push.count) {{ gfx_storage_buffers[nonuniformEXT(push.target)].words[index] = {}; }} }}\n",
                       expression);
}

class VulkanHotReloadTest : public VulkanDeviceTest
{
protected:
    void SetUp() override
    {
        if constexpr (!SHADER_HOT_RELOAD)
        {
            GTEST_SKIP() << "shaders reload in Debug builds only";
        }
        VulkanDeviceTest::SetUp();
        std::filesystem::create_directories(WORK_DIRECTORY);
    }

    /// @brief A module compiled from the file at @p path.
    [[nodiscard]] auto load(const std::filesystem::path& path, std::string_view source) -> std::unique_ptr<ShaderModule>
    {
        m_path = path.generic_string();
        m_source = std::string{source};
        write_file(path, source);
        return m_device->create_shader_module({.stage = ShaderStage::COMPUTE,
                                               .source_format = ShaderSourceFormat::GLSL,
                                               .source = m_source,
                                               .source_path = m_path,
                                               .include_directories = m_include_directories,
                                               .name = "reloaded.comp"});
    }

    /// @brief Runs @p pipeline over 64 words with push value 10 and returns what it wrote.
    [[nodiscard]] auto dispatch(const ComputePipeline& pipeline) -> std::vector<std::uint32_t>
    {
        std::unique_ptr<Buffer> words = m_device->create_buffer(
            {.size = 64 * sizeof(std::uint32_t), .usage = BufferUsage::STORAGE | BufferUsage::TRANSFER_SOURCE, .debug_name = "words"});
        run<QueueType::COMPUTE>([&](ComputeRecorder& recorder)
        {
            recorder.bind_pipeline(pipeline.handle());
            recorder.push_constants(std::array<std::uint32_t, 3>{words->bindless_index().value(), 64, 10});
            recorder.dispatch(1);
        });
        return read_buffer<std::uint32_t>(*words, 64, ResourceState::COMPUTE_SHADER_STORAGE_WRITE);
    }

    std::string m_path;
    std::string m_source;
    std::array<std::string_view, 1> m_include_directories{INCLUDE_DIRECTORY};
};

TEST_F(VulkanHotReloadTest, ChangedSourcesAreRecompiledAndTheirPipelinesRebuilt)
{
    const std::filesystem::path path = WORK_DIRECTORY / "reload_changed.comp";
    std::unique_ptr<ShaderModule> module = load(path, fill_source("push.value + index"));
    std::unique_ptr<ComputePipeline> pipeline = m_device->create_compute_pipeline({.shader = module->handle()});
    EXPECT_TRUE(m_device->poll_shader_changes().empty()) << "nothing changed yet";
    EXPECT_EQ(dispatch(*pipeline)[5], 15U);

    write_file(path, fill_source("push.value * 2u + index"));
    const std::vector<ShaderReload> reloads = m_device->poll_shader_changes();
    ASSERT_EQ(reloads.size(), 1U);
    EXPECT_EQ(reloads[0].result, ShaderReloadResult::RELOADED) << reloads[0].message;
    EXPECT_EQ(reloads[0].shader_name, "reloaded.comp");
    EXPECT_EQ(reloads[0].source_path, m_path);
    EXPECT_EQ(pipeline->status(), PipelineStatus::READY);
    EXPECT_TRUE(m_device->poll_shader_changes().empty()) << "each change is reported once";

    // The same pipeline handle now runs the new code.
    EXPECT_EQ(dispatch(*pipeline)[5], 25U);
}

TEST_F(VulkanHotReloadTest, SourcesThatDoNotCompileKeepTheOldCode)
{
    const std::filesystem::path path = WORK_DIRECTORY / "reload_broken.comp";
    std::unique_ptr<ShaderModule> module = load(path, fill_source("push.value + index"));
    std::unique_ptr<ComputePipeline> pipeline = m_device->create_compute_pipeline({.shader = module->handle()});

    write_file(path, fill_source("undefined_variable"));
    const std::vector<ShaderReload> reloads = m_device->poll_shader_changes();
    ASSERT_EQ(reloads.size(), 1U);
    EXPECT_EQ(reloads[0].result, ShaderReloadResult::COMPILATION_FAILED);
    EXPECT_NE(reloads[0].message.find("undefined_variable"), std::string::npos) << reloads[0].message;
    EXPECT_EQ(pipeline->status(), PipelineStatus::READY) << "the pipeline keeps its previous code";
    EXPECT_EQ(dispatch(*pipeline)[5], 15U);
}

TEST_F(VulkanHotReloadTest, SourcesThatNoLongerFitTheirPipelineLayoutsKeepTheOldCode)
{
    const std::filesystem::path path = WORK_DIRECTORY / "reload_mismatch.comp";
    std::unique_ptr<ShaderModule> module = load(path, fill_source("push.value + index"));
    std::unique_ptr<ComputePipeline> pipeline = m_device->create_compute_pipeline({.shader = module->handle()});

    // A uniform block in set 1, which the bindless layout does not have.
    write_file(path, "#version 460\nlayout(local_size_x = 64) in;\n"
                     "layout(set = 1, binding = 0) uniform Extra { uint value; } extra;\n"
                     "layout(set = 1, binding = 1) buffer Out { uint words[]; } result;\n"
                     "void main() { result.words[gl_GlobalInvocationID.x] = extra.value; }\n");
    const std::vector<ShaderReload> reloads = m_device->poll_shader_changes();
    ASSERT_EQ(reloads.size(), 1U);
    EXPECT_EQ(reloads[0].result, ShaderReloadResult::LAYOUT_MISMATCH);
    EXPECT_NE(reloads[0].message.find("set 1 binding"), std::string::npos) << reloads[0].message;
    EXPECT_EQ(module->layout().find("extra"), nullptr) << "the module keeps its previous layout";
}

TEST_F(VulkanHotReloadTest, ChangedIncludesReloadTheShadersThatIncludeThem)
{
    // The shader includes a header of its own directory.
    const std::filesystem::path header = WORK_DIRECTORY / "reload_value.glsl";
    write_file(header, "#define FILL_EXPRESSION (push.value + index)\n");
    std::string source = fill_source("FILL_EXPRESSION");
    source.insert(source.find("layout(local_size_x"), "#include \"reload_value.glsl\"\n");
    const std::filesystem::path path = WORK_DIRECTORY / "reload_include.comp";
    std::unique_ptr<ShaderModule> module = load(path, source);
    std::unique_ptr<ComputePipeline> pipeline = m_device->create_compute_pipeline({.shader = module->handle()});

    EXPECT_EQ(dispatch(*pipeline)[5], 15U);

    write_file(header, "#define FILL_EXPRESSION (push.value + 2u * index)\n");
    const std::vector<ShaderReload> reloads = m_device->poll_shader_changes();
    ASSERT_EQ(reloads.size(), 1U);
    EXPECT_EQ(reloads[0].result, ShaderReloadResult::RELOADED) << reloads[0].message;
    EXPECT_EQ(dispatch(*pipeline)[5], 20U);
}

TEST_F(VulkanHotReloadTest, SpirvModulesWithASourcePathWatchItAndItsIncludes)
{
    // The description gfx_add_shaders() generates names the source and its include directories.
    EXPECT_FALSE(gfx::tests::vulkan_shaders::fill_comp.source_path.empty());
    std::unique_ptr<ShaderModule> module = m_device->create_shader_module(gfx::tests::vulkan_shaders::fill_comp);
    std::unique_ptr<ComputePipeline> pipeline = m_device->create_compute_pipeline({.shader = module->handle()});
    EXPECT_TRUE(m_device->poll_shader_changes().empty()) << "the first poll learns the includes and reports nothing";
}

} // namespace
