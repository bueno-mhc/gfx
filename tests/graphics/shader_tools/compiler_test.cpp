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

auto
read_file(const std::filesystem::path& path) -> std::string
{
    std::ifstream stream{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

const std::filesystem::path SHADER_DIRECTORY{GFX_TEST_SHADER_DIRECTORY};

TEST(ShaderCompilerTest, ReleaseBuildsDoNotCompileSource)
{
    if constexpr (SHADER_HOT_RELOAD)
    {
        GTEST_SKIP() << "Debug builds compile shaders at run time";
    }
    try
    {
        (void)compile_shader({.stage = ShaderStage::COMPUTE,
                              .source_format = ShaderSourceFormat::GLSL,
                              .source = "#version 460\nvoid main() {}",
                              .name = "release.comp"});
        FAIL() << "compile_shader must throw";
    }
    catch (const InvalidUsageError& error)
    {
        EXPECT_TRUE(contains(error.what(), "only in Debug builds")) << error.what();
        EXPECT_TRUE(contains(error.what(), "gfx_add_shaders()")) << error.what();
    }
}

class ShaderCompilerDebugTest : public testing::Test
{
protected:
    void SetUp() override
    {
        if constexpr (!SHADER_HOT_RELOAD)
        {
            GTEST_SKIP() << "Release builds do not compile shaders at run time";
        }
    }
};

TEST_F(ShaderCompilerDebugTest, GlslCompiledAtRunTimeReflectsLikeTheBuildTimeSpirv)
{
    // The same file, defines and include directory as gfx_add_shaders().
    const std::string path = (SHADER_DIRECTORY / "reflection.comp").generic_string();
    const std::string source = read_file(path);
    const std::string include_directory = (SHADER_DIRECTORY / "include").generic_string();
    const std::array<ShaderDefine, 1> defines{{{.name = "TEXTURE_COUNT", .value = "4"}}};
    const std::array<std::string_view, 1> include_directories{include_directory};

    const CompiledShader compiled = compile_shader({
        .stage = ShaderStage::COMPUTE,
        .source_format = ShaderSourceFormat::GLSL,
        .source = source,
        .source_path = path,
        .defines = defines,
        .include_directories = include_directories,
        .name = "reflection.comp",
    });

    ASSERT_FALSE(compiled.spirv.empty());
    EXPECT_EQ(compiled.spirv.front(), SPIRV_MAGIC_NUMBER);
    EXPECT_EQ(reflect_spirv(compiled.spirv), reflect_spirv(shaders::reflection_comp_spirv));
    ASSERT_EQ(compiled.included_files.size(), 1U);
    EXPECT_TRUE(compiled.included_files[0].ends_with("include/common.glsl")) << compiled.included_files[0];
}

TEST_F(ShaderCompilerDebugTest, HlslCompiledAtRunTimeReflectsLikeTheBuildTimeSpirv)
{
    const std::string source = read_file(SHADER_DIRECTORY / "scale.comp.hlsl");
    const CompiledShader compiled = compile_shader({
        .stage = ShaderStage::COMPUTE,
        .source_format = ShaderSourceFormat::HLSL,
        .source = source,
        .entry_point = "main",
        .name = "scale.comp.hlsl",
    });
    EXPECT_EQ(reflect_spirv(compiled.spirv), reflect_spirv(shaders::scale_comp_spirv));
}

TEST_F(ShaderCompilerDebugTest, HlslEntryPointsMayHaveAnyName)
{
    const CompiledShader compiled = compile_shader({
        .stage = ShaderStage::COMPUTE,
        .source_format = ShaderSourceFormat::HLSL,
        .source = "[numthreads(32, 1, 1)] void clear_values() {}",
        .entry_point = "clear_values",
        .name = "clear.hlsl",
    });
    EXPECT_EQ(reflect_spirv(compiled.spirv, ShaderStage::COMPUTE, "clear_values").workgroup_size,
              (std::array<std::uint32_t, 3>{32, 1, 1}));
}

TEST_F(ShaderCompilerDebugTest, DefinesSelectShaderVariants)
{
    static constexpr std::string_view SOURCE = "#version 460\n"
                                               "layout(local_size_x = GROUP_SIZE) in;\n"
                                               "void main() {}\n";
    const std::array<ShaderDefine, 1> small{{{.name = "GROUP_SIZE", .value = "32"}}};
    const std::array<ShaderDefine, 1> large{{{.name = "GROUP_SIZE", .value = "256"}}};
    const auto compile = [](std::span<const ShaderDefine> defines)
    {
        return compile_shader({.stage = ShaderStage::COMPUTE,
                               .source_format = ShaderSourceFormat::GLSL,
                               .source = SOURCE,
                               .defines = defines,
                               .name = "variant.comp"});
    };
    EXPECT_EQ(reflect_spirv(compile(small).spirv).workgroup_size[0], 32U);
    EXPECT_EQ(reflect_spirv(compile(large).spirv).workgroup_size[0], 256U);
}

TEST_F(ShaderCompilerDebugTest, CompilationErrorsCarryTheCompilerLog)
{
    try
    {
        (void)compile_shader({.stage = ShaderStage::FRAGMENT,
                              .source_format = ShaderSourceFormat::GLSL,
                              .source = "#version 460\nvoid main()\n{\n    undefined_function();\n}\n",
                              .name = "broken.frag"});
        FAIL() << "compile_shader must throw";
    }
    catch (const ShaderCompilationError& error)
    {
        EXPECT_TRUE(contains(error.what(), "broken.frag")) << error.what();
        EXPECT_TRUE(contains(error.what(), "undefined_function")) << error.what();
        EXPECT_TRUE(contains(error.what(), ":4:")) << "the log names the line: " << error.what();
    }
}

TEST_F(ShaderCompilerDebugTest, MissingIncludesAreCompilationErrors)
{
    try
    {
        (void)compile_shader({.stage = ShaderStage::COMPUTE,
                              .source_format = ShaderSourceFormat::GLSL,
                              .source = "#version 460\n#extension GL_GOOGLE_include_directive : require\n"
                                        "#include \"missing.glsl\"\nlayout(local_size_x = 1) in;\nvoid main() {}\n",
                              .name = "include.comp"});
        FAIL() << "compile_shader must throw";
    }
    catch (const ShaderCompilationError& error)
    {
        EXPECT_TRUE(contains(error.what(), "missing.glsl")) << error.what();
    }
}

TEST_F(ShaderCompilerDebugTest, GlslEntryPointMustBeMainAndSpirvNeedsNoCompilation)
{
    EXPECT_THROW((void)compile_shader({.stage = ShaderStage::COMPUTE,
                                       .source_format = ShaderSourceFormat::GLSL,
                                       .source = "#version 460\nvoid main() {}",
                                       .entry_point = "start"}),
                 InvalidUsageError);
    EXPECT_THROW((void)compile_shader(shaders::reflection_comp), InvalidUsageError);
}

TEST_F(ShaderCompilerDebugTest, ShadersCompileConcurrently)
{
    const std::string source = read_file(SHADER_DIRECTORY / "stage_io.vert");
    std::atomic<int> failures{0};
    {
        std::vector<std::jthread> threads;
        for (int thread = 0; thread < 4; ++thread)
        {
            threads.emplace_back([&source, &failures]
            {
                for (int iteration = 0; iteration < 5; ++iteration)
                {
                    const CompiledShader compiled = compile_shader(
                        {.stage = ShaderStage::VERTEX, .source_format = ShaderSourceFormat::GLSL, .source = source, .name = "stage_io.vert"});
                    if (reflect_spirv(compiled.spirv) != reflect_spirv(shaders::stage_io_vert_spirv))
                    {
                        failures.fetch_add(1);
                    }
                }
            });
        }
    }
    EXPECT_EQ(failures.load(), 0);
}

} // namespace
