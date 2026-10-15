/// @file vulkan_pipelines.cpp
/// @brief Shader modules (compiled in Debug builds, reflected, hot reloaded), descriptor pools and sets,
///        pipeline caches, and compute, graphics and ray tracing pipelines (compiled now or in the
///        background, rebuilt behind the same handles when their shaders reload). Nothing outside this
///        unit needs these classes, so they are declared here.
module;

#include <volk.h>

module gfx.backends.vulkan;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import gfx.interface;
import gfx.shader_tools;
import :common;
import :translation;
import :instance;
import :device;
import :pipelines;

namespace gfx::vulkan
{

namespace
{

/// @brief Guards the links between shader modules and the pipelines built from them (hot reload).
auto
dependency_mutex() -> std::mutex&
{
    static std::mutex mutex;
    return mutex;
}

[[noreturn]] void
reject(std::string_view operation,
       std::string problem,
       std::string suggestion = {})
{
    throw InvalidUsageError({
        .backend = BackendKind::VULKAN,
        .operation = std::string{operation},
        .problem = std::move(problem),
        .suggestion = std::move(suggestion),
    });
}

/// @brief Specialization data of a pipeline: every constant as a 32-bit value.
struct Specialization
{
    std::vector<VkSpecializationMapEntry> entries;
    std::vector<std::uint32_t> values;
    VkSpecializationInfo information{};

    explicit Specialization(std::span<const SpecializationConstant> constants)
    {
        for (const SpecializationConstant& constant : constants)
        {
            entries.push_back({.constantID = constant.id,
                               .offset = static_cast<std::uint32_t>(values.size() * sizeof(std::uint32_t)),
                               .size = sizeof(std::uint32_t)});
            values.push_back(constant.value);
        }
        information = {
            .mapEntryCount = static_cast<std::uint32_t>(entries.size()),
            .pMapEntries = entries.data(),
            .dataSize = values.size() * sizeof(std::uint32_t),
            .pData = values.data(),
        };
    }

    Specialization(const Specialization&) = delete;
    auto operator=(const Specialization&) -> Specialization& = delete;

    /// @brief The information for a shader stage (nullptr without constants).
    [[nodiscard]] auto get() const noexcept -> const VkSpecializationInfo*
    {
        return entries.empty() ? nullptr : &information;
    }
};

} // namespace

// ---------------------------------------------------------------------------------------------------
// Pipelines: compilation, asynchronous compilation, rebuilds
// ---------------------------------------------------------------------------------------------------

/// @brief What every kind of pipeline shares: its native pipeline and status (PipelineState, read by
///        command buffers), background compilation, and the rebuild hot reload asks for.
class PipelineBuilder
{
public:
    explicit PipelineBuilder(VulkanDevice& device)
        : m_device{device}
    {}

    virtual ~PipelineBuilder() = default;

    PipelineBuilder(const PipelineBuilder&) = delete;
    auto operator=(const PipelineBuilder&) -> PipelineBuilder& = delete;

    /// @brief Creates a native pipeline from the shaders' current code.
    [[nodiscard]] virtual auto build() const -> VkPipeline = 0;

    /// @brief Checks the shaders' current layouts against the pipeline (throws the mismatch).
    virtual void validate() const = 0;

    /// @brief Name for reports.
    [[nodiscard]] virtual auto pipeline_name() const -> std::string = 0;

    /// @brief A shader module it uses is being destroyed: it can no longer be rebuilt.
    virtual void forget(const VulkanShaderModule* module) noexcept = 0;

    /// @brief Replaces the native pipeline with one built from the shaders' current code (hot reload).
    void rebuild()
    {
        const VkPipeline replacement = build();
        const VkPipeline previous = m_state.pipeline.exchange(replacement, std::memory_order_acq_rel);
        m_state.status.store(PipelineStatus::READY, std::memory_order_release);
        destroy_later(previous);
    }

protected:
    /// @brief Compiles now, or in the background on the device's task scheduler.
    void start(PipelineCreationMode mode)
    {
        if (mode == PipelineCreationMode::ASYNCHRONOUS && m_device.task_scheduler())
        {
            m_state.status.store(PipelineStatus::COMPILING, std::memory_order_release);
            m_device.task_scheduler()([this]
            {
                try
                {
                    m_state.pipeline.store(build(), std::memory_order_release);
                    m_state.status.store(PipelineStatus::READY, std::memory_order_release);
                }
                catch (...)
                {
                    m_failure = std::current_exception();
                    m_state.status.store(PipelineStatus::COMPILATION_FAILED, std::memory_order_release);
                }
                m_state.status.notify_all();
            });
            return;
        }
        m_state.pipeline.store(build(), std::memory_order_release);
        m_state.status.store(PipelineStatus::READY, std::memory_order_release);
    }

    /// @brief Blocks until compiled; rethrows the compilation's error.
    void wait_for_compilation() const
    {
        m_state.status.wait(PipelineStatus::COMPILING, std::memory_order_acquire);
        if (m_state.status.load(std::memory_order_acquire) == PipelineStatus::COMPILATION_FAILED && m_failure)
        {
            std::rethrow_exception(m_failure);
        }
    }

    /// @brief Waits for a background compilation, then releases the native pipeline.
    void release() noexcept
    {
        m_state.status.wait(PipelineStatus::COMPILING, std::memory_order_acquire);
        destroy_later(m_state.pipeline.load(std::memory_order_acquire));
    }

    void destroy_later(VkPipeline pipeline) noexcept
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            m_device.vulkan_deleter().enqueue([device = &m_device, pipeline]
            {
                device->functions().vkDestroyPipeline(device->native(), pipeline, nullptr);
            });
        }
    }

    /// @brief The cache a description names, or the device's.
    [[nodiscard]] auto cache(PipelineCacheHandle handle, std::string_view operation) const -> VkPipelineCache
    {
        return handle.is_valid() ? m_device.handles().pipeline_caches.get(handle, operation).cache : m_device.default_pipeline_cache();
    }

protected:
    VulkanDevice& m_device;
    PipelineState m_state;
    std::exception_ptr m_failure;
};

// ---------------------------------------------------------------------------------------------------
// Shader modules
// ---------------------------------------------------------------------------------------------------

/// @brief A file a shader module watches, with its modification time when last read.
struct WatchedFile
{
    std::filesystem::path path;
    std::filesystem::file_time_type time;
};

/// @brief Compiled shader code with its reflected layout. In Debug builds a module with a source path
///        watches its source (and what it includes) and reloads when it changes.
class VulkanShaderModule final : public ShaderModule
{
public:
    VulkanShaderModule(VulkanDevice& device, const ShaderModuleDescription& description);
    ~VulkanShaderModule() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }

    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }

    void set_debug_name(std::string_view name) override
    {
        m_debug_name = name;
        m_device.name_object(VK_OBJECT_TYPE_SHADER_MODULE, m_module, m_debug_name);
    }

    [[nodiscard]] auto handle() const noexcept -> ShaderModuleHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto stage() const noexcept -> ShaderStage override
    {
        return m_stage;
    }

    [[nodiscard]] auto layout() const noexcept -> const ShaderLayout& override
    {
        return m_layout;
    }

    /// @brief The native module of the current code.
    [[nodiscard]] auto native() const noexcept -> VkShaderModule
    {
        return m_module;
    }

    /// @brief The entry point.
    [[nodiscard]] auto entry_point() const noexcept -> const char*
    {
        return m_entry_point.c_str();
    }

    /// @brief Registers a pipeline built from this module (hot reload rebuilds it).
    void add_dependent(PipelineBuilder* pipeline)
    {
        const std::scoped_lock lock{dependency_mutex()};
        m_dependents.push_back(pipeline);
    }

    /// @brief Unregisters a pipeline.
    void remove_dependent(PipelineBuilder* pipeline) noexcept
    {
        const std::scoped_lock lock{dependency_mutex()};
        std::erase(m_dependents, pipeline);
    }

    /// @brief Debug builds: reloads the module when a watched file changed (std::nullopt when none did).
    [[nodiscard]] auto reload_if_changed() -> std::optional<ShaderReload>;

private:
    void create_native(std::span<const std::uint32_t> spirv);
    void watch(const std::vector<std::string>& included_files);

private:
    VulkanDevice& m_device;
    VkShaderModule m_module = VK_NULL_HANDLE;
    ShaderStage m_stage;
    ShaderLayout m_layout;
    std::string m_entry_point;
    ShaderModuleHandle m_handle;
    std::string m_debug_name;
    std::vector<PipelineBuilder*> m_dependents;

    // Hot reload (Debug builds, modules with a source path).
    std::string m_source_path;
    ShaderSourceFormat m_source_format = ShaderSourceFormat::GLSL;
    std::vector<std::pair<std::string, std::string>> m_defines;
    std::vector<std::string> m_include_directories;
    std::vector<WatchedFile> m_watched;
    bool m_dependencies_known = false;
};

auto
VulkanDevice::create_shader_module(const ShaderModuleDescription& description) -> std::unique_ptr<ShaderModule>
{
    return std::make_unique<VulkanShaderModule>(*this, description);
}

VulkanShaderModule::VulkanShaderModule(VulkanDevice& device,
                                       const ShaderModuleDescription& description)
    : ShaderModule{}
    , m_device{device}
    , m_stage{description.stage}
    , m_entry_point{description.entry_point}
    , m_debug_name{description.name}
{
    set_validation_level(device.validation_level());
    static constexpr std::string_view OPERATION = "Device::create_shader_module";
    if (validates(ValidationLevel::CREATION))
    {
        validate_shader_module_description(description, BackendKind::VULKAN);
    }

    // Stages beyond vertex, fragment and compute need their features.
    if (has_any(description.stage, ShaderStage::TASK | ShaderStage::MESH))
    {
        device.require(DeviceFeature::MESH_SHADER, OPERATION);
    }
    if (has_any(description.stage, ShaderStage::RAY_GENERATION | ShaderStage::ANY_HIT | ShaderStage::CLOSEST_HIT | ShaderStage::MISS |
                                       ShaderStage::INTERSECTION | ShaderStage::CALLABLE))
    {
        device.require(DeviceFeature::RAY_TRACING, OPERATION);
    }
    if (description.stage == ShaderStage::GEOMETRY)
    {
        device.require(DeviceFeature::GEOMETRY_SHADER, OPERATION);
    }
    if (has_any(description.stage, ShaderStage::TESSELLATION_CONTROL | ShaderStage::TESSELLATION_EVALUATION))
    {
        device.require(DeviceFeature::TESSELLATION_SHADER, OPERATION);
    }

    // SPIR-V as given, or compiled from source (Debug builds); then its layout.
    std::vector<std::uint32_t> compiled;
    std::vector<std::string> included_files;
    std::span<const std::uint32_t> spirv = description.spirv;
    if (description.source_format != ShaderSourceFormat::SPIRV)
    {
        CompiledShader result = compile_shader(description);
        compiled = std::move(result.spirv);
        included_files = std::move(result.included_files);
        spirv = compiled;
    }
    m_layout = reflect_spirv(spirv, description.stage, description.entry_point, description.name);
    create_native(spirv);
    m_handle = device.handles().shader_modules.insert({.module = this});

    // Debug builds watch the source file (and its includes) of modules that name one.
    if constexpr (SHADER_HOT_RELOAD)
    {
        if (!description.source_path.empty())
        {
            m_source_path = description.source_path;
            m_source_format = description.source_format != ShaderSourceFormat::SPIRV ? description.source_format
                              : m_source_path.ends_with(".hlsl")                    ? ShaderSourceFormat::HLSL
                                                                                    : ShaderSourceFormat::GLSL;
            for (const ShaderDefine& define : description.defines)
            {
                m_defines.emplace_back(define.name, define.value);
            }
            m_include_directories.assign(description.include_directories.begin(), description.include_directories.end());
            m_dependencies_known = description.source_format != ShaderSourceFormat::SPIRV;
            watch(included_files);
        }
    }
}

void
VulkanShaderModule::create_native(std::span<const std::uint32_t> spirv)
{
    const VkShaderModuleCreateInfo information{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv.size_bytes(),
        .pCode = spirv.data(),
    };
    m_device.check_result(m_device.functions().vkCreateShaderModule(m_device.native(), &information, nullptr, &m_module),
                          "Device::create_shader_module", "vkCreateShaderModule");
    m_device.name_object(VK_OBJECT_TYPE_SHADER_MODULE, m_module, m_debug_name);
}

void
VulkanShaderModule::watch(const std::vector<std::string>& included_files)
{
    // The source and what it includes, with their current modification times.
    m_watched.clear();
    std::error_code error;
    m_watched.push_back({.path = m_source_path, .time = std::filesystem::last_write_time(m_source_path, error)});
    for (const std::string& file : included_files)
    {
        m_watched.push_back({.path = file, .time = std::filesystem::last_write_time(file, error)});
    }
}

VulkanShaderModule::~VulkanShaderModule()
{
    // Pipelines keep their native pipelines, but can no longer be rebuilt from this module.
    {
        const std::scoped_lock lock{dependency_mutex()};
        for (PipelineBuilder* dependent : m_dependents)
        {
            dependent->forget(this);
        }
    }
    (void)m_device.handles().shader_modules.remove(m_handle);
    m_device.vulkan_deleter().enqueue([device = &m_device, module = m_module]
    {
        device->functions().vkDestroyShaderModule(device->native(), module, nullptr);
    });
}

auto
VulkanShaderModule::reload_if_changed() -> std::optional<ShaderReload>
{
    if (m_source_path.empty())
    {
        return std::nullopt;
    }
    ShaderReload report{.shader_name = m_debug_name, .source_path = m_source_path, .result = ShaderReloadResult::RELOADED, .message = {}};

    // The description of a compilation from the source file.
    std::vector<ShaderDefine> defines;
    for (const auto& [name, value] : m_defines)
    {
        defines.push_back({.name = name, .value = value});
    }
    const std::vector<std::string_view> include_directories(m_include_directories.begin(), m_include_directories.end());
    const auto description_of = [&](std::string_view source) -> ShaderModuleDescription
    {
        return {.stage = m_stage,
                .source_format = m_source_format,
                .source = source,
                .source_path = m_source_path,
                .entry_point = m_entry_point,
                .defines = defines,
                .include_directories = include_directories,
                .name = m_debug_name};
    };
    const auto read = [](const std::string& path) -> std::string
    {
        std::ifstream stream{path, std::ios::binary};
        return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    };

    // Modules created from SPIR-V learn what their source includes the first time (preprocessor only).
    if (!m_dependencies_known)
    {
        m_dependencies_known = true;
        try
        {
            const std::string source = read(m_source_path);
            const std::vector<std::string> files = shader_dependencies(description_of(source));
            for (const std::string& file : files)
            {
                std::error_code error;
                m_watched.push_back({.path = file, .time = std::filesystem::last_write_time(file, error)});
            }
        }
        catch (const Error&)
        {
            // The source cannot be preprocessed now: its own changes are still watched.
        }
    }

    // Nothing changed: nothing to report.
    bool changed = false;
    for (WatchedFile& file : m_watched)
    {
        std::error_code error;
        const std::filesystem::file_time_type time = std::filesystem::last_write_time(file.path, error);
        if (!error && time != file.time)
        {
            file.time = time;
            changed = true;
        }
    }
    if (!changed)
    {
        return std::nullopt;
    }

    // Compile the new source; a failure keeps the old code.
    CompiledShader compiled;
    ShaderLayout layout;
    try
    {
        const std::string source = read(m_source_path);
        compiled = compile_shader(description_of(source));
        layout = reflect_spirv(compiled.spirv, m_stage, m_entry_point, m_debug_name);
    }
    catch (const Error& error)
    {
        report.result = ShaderReloadResult::COMPILATION_FAILED;
        report.message = error.what();
        return report;
    }

    // The new layout must still fit every pipeline built from the module.
    const std::scoped_lock lock{dependency_mutex()};
    ShaderLayout previous_layout = std::exchange(m_layout, std::move(layout));
    try
    {
        for (const PipelineBuilder* dependent : m_dependents)
        {
            dependent->validate();
        }
    }
    catch (const Error& error)
    {
        m_layout = std::move(previous_layout);
        report.result = ShaderReloadResult::LAYOUT_MISMATCH;
        report.message = error.what();
        return report;
    }

    // Swap the code, rebuild the pipelines behind their handles; a driver failure restores the old code.
    const VkShaderModule previous_module = m_module;
    try
    {
        create_native(compiled.spirv);
        for (PipelineBuilder* dependent : m_dependents)
        {
            dependent->rebuild();
        }
    }
    catch (const Error& error)
    {
        if (m_module != previous_module)
        {
            m_device.functions().vkDestroyShaderModule(m_device.native(), m_module, nullptr);
        }
        m_module = previous_module;
        m_layout = std::move(previous_layout);
        report.result = ShaderReloadResult::COMPILATION_FAILED;
        report.message = error.what();
        return report;
    }
    m_device.vulkan_deleter().enqueue([device = &m_device, previous_module]
    {
        device->functions().vkDestroyShaderModule(device->native(), previous_module, nullptr);
    });

    // Includes may have changed with the source.
    watch(compiled.included_files);
    return report;
}

auto
VulkanDevice::reload_changed_shaders() -> std::vector<ShaderReload>
{
    // Every module that watches a file; each reloads on its own.
    std::vector<VulkanShaderModule*> modules;
    m_handles.shader_modules.for_each([&modules](ShaderModuleHandle, const ShaderModuleRecord& record)
    {
        modules.push_back(record.module);
    });
    std::vector<ShaderReload> reports;
    for (VulkanShaderModule* module : modules)
    {
        if (std::optional<ShaderReload> report = module->reload_if_changed())
        {
            reports.push_back(std::move(*report));
        }
    }
    return reports;
}

/// @brief The module of @p handle, which must be a live shader module.
auto
module_of(VulkanDevice& device, ShaderModuleHandle handle, std::string_view operation) -> VulkanShaderModule*
{
    return handle.is_valid() ? device.handles().shader_modules.get(handle, operation).module : nullptr;
}

/// @brief The pipeline layout a description names, or the bindless layout.
auto
layout_of(VulkanDevice& device,
          PipelineLayoutHandle handle,
          std::string_view operation) -> const VulkanPipelineLayout&
{
    if (!handle.is_valid())
    {
        return static_cast<const VulkanPipelineLayout&>(device.bindless_pipeline_layout());
    }
    return *device.handles().pipeline_layouts.get(handle, operation).object;
}

/// @brief A stage of a pipeline, from a module's current code.
auto
stage_information(const VulkanShaderModule& module,
                  const VkSpecializationInfo* specialization) -> VkPipelineShaderStageCreateInfo
{
    return {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = to_vulkan_stage(module.stage()),
        .module = module.native(),
        .pName = module.entry_point(),
        .pSpecializationInfo = specialization,
    };
}

// ---------------------------------------------------------------------------------------------------
// Compute pipelines
// ---------------------------------------------------------------------------------------------------

class VulkanComputePipeline final : public ComputePipeline, public PipelineBuilder
{
public:
    VulkanComputePipeline(VulkanDevice& device, const ComputePipelineDescription& description);
    ~VulkanComputePipeline() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }

    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }

    void set_debug_name(std::string_view name) override
    {
        m_debug_name = name;
    }

    [[nodiscard]] auto handle() const noexcept -> ComputePipelineHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto layout() const noexcept -> PipelineLayoutHandle override
    {
        return m_layout->handle();
    }

    [[nodiscard]] auto workgroup_size() const noexcept -> std::array<std::uint32_t, 3> override
    {
        return m_workgroup_size;
    }

    [[nodiscard]] auto status() const noexcept -> PipelineStatus override
    {
        return m_state.status.load(std::memory_order_acquire);
    }

    void wait() const override
    {
        wait_for_compilation();
    }

    [[nodiscard]] auto build() const -> VkPipeline override;
    void validate() const override;

    [[nodiscard]] auto pipeline_name() const -> std::string override
    {
        return m_debug_name;
    }

    void forget(const VulkanShaderModule* module) noexcept override
    {
        if (m_shader == module)
        {
            m_shader = nullptr;
        }
    }

private:
    const VulkanPipelineLayout* m_layout;
    VulkanShaderModule* m_shader;
    std::vector<SpecializationConstant> m_constants;
    PipelineCacheHandle m_cache;
    std::array<std::uint32_t, 3> m_workgroup_size{};
    ComputePipelineHandle m_handle;
    std::string m_debug_name;
};

auto
VulkanDevice::create_compute_pipeline(const ComputePipelineDescription& description) -> std::unique_ptr<ComputePipeline>
{
    return std::make_unique<VulkanComputePipeline>(*this, description);
}

VulkanComputePipeline::VulkanComputePipeline(VulkanDevice& device,
                                             const ComputePipelineDescription& description)
    : ComputePipeline{}
    , PipelineBuilder{device}
    , m_layout{&layout_of(device, description.layout, "Device::create_compute_pipeline")}
    , m_shader{module_of(device, description.shader, "Device::create_compute_pipeline")}
    , m_constants{description.specialization_constants.begin(), description.specialization_constants.end()}
    , m_cache{description.cache}
    , m_debug_name{description.debug_name}
{
    ComputePipeline::set_validation_level(device.validation_level());
    if (m_shader == nullptr)
    {
        reject("Device::create_compute_pipeline", std::format("compute pipeline '{}' has no shader", m_debug_name));
    }
    if (ComputePipeline::validates(ValidationLevel::CREATION))
    {
        validate();
    }
    m_workgroup_size = m_shader->layout().workgroup_size;
    start(description.creation_mode);
    m_handle = device.handles().compute_pipelines.insert({.state = &m_state, .layout = m_layout->native()});
    m_shader->add_dependent(this);
}

VulkanComputePipeline::~VulkanComputePipeline()
{
    (void)m_device.handles().compute_pipelines.remove(m_handle);
    if (m_shader != nullptr)
    {
        m_shader->remove_dependent(this);
    }
    release();
}

void
VulkanComputePipeline::validate() const
{
    validate_compute_pipeline(*m_layout, *m_shader, m_constants);
}

auto
VulkanComputePipeline::build() const -> VkPipeline
{
    if (m_shader == nullptr)
    {
        reject("ComputePipeline::rebuild", std::format("the shader module of compute pipeline '{}' was destroyed", m_debug_name));
    }
    const Specialization specialization{m_constants};
    const VkComputePipelineCreateInfo information{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = stage_information(*m_shader, specialization.get()),
        .layout = m_layout->native(),
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    m_device.check_result(m_device.functions().vkCreateComputePipelines(m_device.native(), cache(m_cache, "Device::create_compute_pipeline"),
                                                                        1, &information, nullptr, &pipeline),
                          "Device::create_compute_pipeline", "vkCreateComputePipelines");
    m_device.name_object(VK_OBJECT_TYPE_PIPELINE, pipeline, m_debug_name);
    return pipeline;
}

// ---------------------------------------------------------------------------------------------------
// Graphics pipelines
// ---------------------------------------------------------------------------------------------------

/// @brief A graphics pipeline description with storage for everything its spans point to.
struct GraphicsPipelineSettings
{
    GraphicsPipelineDescription description;
    std::vector<VertexInputBinding> vertex_bindings;
    std::vector<VertexInputAttribute> vertex_attributes;
    std::vector<Format> color_formats;
    std::vector<ColorBlendAttachment> color_blend;
    std::vector<SpecializationConstant> constants;
    std::string debug_name;

    explicit GraphicsPipelineSettings(const GraphicsPipelineDescription& source)
        : description{source}
        , vertex_bindings{source.vertex_bindings.begin(), source.vertex_bindings.end()}
        , vertex_attributes{source.vertex_attributes.begin(), source.vertex_attributes.end()}
        , color_formats{source.color_formats.begin(), source.color_formats.end()}
        , color_blend{source.state.color_blend.begin(), source.state.color_blend.end()}
        , constants{source.specialization_constants.begin(), source.specialization_constants.end()}
        , debug_name{source.state.debug_name}
    {
        description.vertex_bindings = vertex_bindings;
        description.vertex_attributes = vertex_attributes;
        description.color_formats = color_formats;
        description.state.color_blend = color_blend;
        description.specialization_constants = constants;
        description.state.debug_name = debug_name;
    }

    GraphicsPipelineSettings(const GraphicsPipelineSettings&) = delete;
    auto operator=(const GraphicsPipelineSettings&) -> GraphicsPipelineSettings& = delete;
};

/// @brief Native dynamic states of a gfx DynamicState set (viewport, scissor, stencil reference and
///        blend constants are always dynamic).
auto
dynamic_states(DynamicState states) -> std::vector<VkDynamicState>
{
    std::vector<VkDynamicState> native{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE,
                                       VK_DYNAMIC_STATE_BLEND_CONSTANTS};
    constexpr std::array<std::pair<DynamicState, VkDynamicState>, 11> TABLE{{
        {DynamicState::DEPTH_BIAS, VK_DYNAMIC_STATE_DEPTH_BIAS},
        {DynamicState::DEPTH_BOUNDS, VK_DYNAMIC_STATE_DEPTH_BOUNDS},
        {DynamicState::LINE_WIDTH, VK_DYNAMIC_STATE_LINE_WIDTH},
        {DynamicState::CULL_MODE, VK_DYNAMIC_STATE_CULL_MODE},
        {DynamicState::FRONT_FACE, VK_DYNAMIC_STATE_FRONT_FACE},
        {DynamicState::PRIMITIVE_TOPOLOGY, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY},
        {DynamicState::DEPTH_TEST, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE},
        {DynamicState::DEPTH_WRITE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE},
        {DynamicState::DEPTH_COMPARE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP},
        {DynamicState::STENCIL_TEST, VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE},
        {DynamicState::STENCIL_OPERATION, VK_DYNAMIC_STATE_STENCIL_OP},
    }};
    for (const auto& [state, native_state] : TABLE)
    {
        if (has_any(states, state))
        {
            native.push_back(native_state);
        }
    }
    return native;
}

class VulkanGraphicsPipeline final : public GraphicsPipeline, public PipelineBuilder
{
public:
    VulkanGraphicsPipeline(VulkanDevice& device, const GraphicsPipelineDescription& description);
    ~VulkanGraphicsPipeline() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }

    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_settings.debug_name;
    }

    void set_debug_name(std::string_view name) override
    {
        m_settings.debug_name = name;
        m_settings.description.state.debug_name = m_settings.debug_name;
    }

    [[nodiscard]] auto handle() const noexcept -> GraphicsPipelineHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto layout() const noexcept -> PipelineLayoutHandle override
    {
        return m_layout->handle();
    }

    [[nodiscard]] auto status() const noexcept -> PipelineStatus override
    {
        return m_state.status.load(std::memory_order_acquire);
    }

    void wait() const override
    {
        wait_for_compilation();
    }

    [[nodiscard]] auto build() const -> VkPipeline override;
    void validate() const override;

    [[nodiscard]] auto pipeline_name() const -> std::string override
    {
        return m_settings.debug_name;
    }

    void forget(const VulkanShaderModule* module) noexcept override
    {
        for (VulkanShaderModule*& shader : m_shaders)
        {
            if (shader == module)
            {
                shader = nullptr;
                m_forgotten = true;
            }
        }
    }

private:
    [[nodiscard]] auto shaders() const noexcept -> GraphicsPipelineShaders;

private:
    GraphicsPipelineSettings m_settings;
    const VulkanPipelineLayout* m_layout;

    // Vertex, fragment, task, mesh, geometry, tessellation control, tessellation evaluation.
    std::array<VulkanShaderModule*, 7> m_shaders{};
    bool m_forgotten = false;
    GraphicsPipelineHandle m_handle;
};

auto
VulkanDevice::create_graphics_pipeline(const GraphicsPipelineDescription& description) -> std::unique_ptr<GraphicsPipeline>
{
    return std::make_unique<VulkanGraphicsPipeline>(*this, description);
}

VulkanGraphicsPipeline::VulkanGraphicsPipeline(VulkanDevice& device,
                                               const GraphicsPipelineDescription& description)
    : GraphicsPipeline{}
    , PipelineBuilder{device}
    , m_settings{description}
    , m_layout{&layout_of(device, description.layout, "Device::create_graphics_pipeline")}
{
    GraphicsPipeline::set_validation_level(device.validation_level());
    static constexpr std::string_view OPERATION = "Device::create_graphics_pipeline";
    m_shaders = {module_of(device, description.vertex_shader, OPERATION),
                 module_of(device, description.fragment_shader, OPERATION),
                 module_of(device, description.task_shader, OPERATION),
                 module_of(device, description.mesh_shader, OPERATION),
                 module_of(device, description.geometry_shader, OPERATION),
                 module_of(device, description.tessellation_control_shader, OPERATION),
                 module_of(device, description.tessellation_evaluation_shader, OPERATION)};

    // Features the state needs, then the shaders against the layout, the vertex input and the attachments.
    const GraphicsPipelineState& state = description.state;
    if (state.rasterization.conservative)
    {
        device.require(DeviceFeature::CONSERVATIVE_RASTERIZATION, OPERATION);
    }
    if (state.shading_rate != ShadingRate::ONE_BY_ONE)
    {
        device.require(DeviceFeature::VARIABLE_RATE_SHADING, OPERATION);
    }
    if (description.view_mask != 0)
    {
        device.require(DeviceFeature::MULTIVIEW, OPERATION);
    }
    if (GraphicsPipeline::validates(ValidationLevel::CREATION))
    {
        const std::uint32_t maximum_colors = device.physical_device().properties().limits.maximum_color_attachments;
        if (description.color_formats.size() > maximum_colors ||
            (!description.state.color_blend.empty() && description.state.color_blend.size() != description.color_formats.size()))
        {
            reject(OPERATION, std::format("graphics pipeline '{}' has {} color formats and {} blend states: at most {} attachments, "
                                          "and one blend state per attachment (or none)",
                                          m_settings.debug_name, description.color_formats.size(), description.state.color_blend.size(),
                                          maximum_colors));
        }
        validate();
    }
    start(description.creation_mode);
    m_handle = device.handles().graphics_pipelines.insert({.state = &m_state, .layout = m_layout->native()});
    for (VulkanShaderModule* shader : m_shaders)
    {
        if (shader != nullptr)
        {
            shader->add_dependent(this);
        }
    }
}

VulkanGraphicsPipeline::~VulkanGraphicsPipeline()
{
    (void)m_device.handles().graphics_pipelines.remove(m_handle);
    for (VulkanShaderModule* shader : m_shaders)
    {
        if (shader != nullptr)
        {
            shader->remove_dependent(this);
        }
    }
    release();
}

auto
VulkanGraphicsPipeline::shaders() const noexcept -> GraphicsPipelineShaders
{
    return {.vertex = m_shaders[0],
            .fragment = m_shaders[1],
            .task = m_shaders[2],
            .mesh = m_shaders[3],
            .geometry = m_shaders[4],
            .tessellation_control = m_shaders[5],
            .tessellation_evaluation = m_shaders[6]};
}

void
VulkanGraphicsPipeline::validate() const
{
    validate_graphics_pipeline(*m_layout, shaders(), m_settings.description);
}

auto
VulkanGraphicsPipeline::build() const -> VkPipeline
{
    static constexpr std::string_view OPERATION = "Device::create_graphics_pipeline";
    if (m_forgotten)
    {
        reject("GraphicsPipeline::rebuild", std::format("a shader module of graphics pipeline '{}' was destroyed", m_settings.debug_name));
    }
    const GraphicsPipelineDescription& description = m_settings.description;
    const GraphicsPipelineState& state = description.state;

    // Stages.
    const Specialization specialization{m_settings.constants};
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    for (const VulkanShaderModule* shader : m_shaders)
    {
        if (shader != nullptr)
        {
            stages.push_back(stage_information(*shader, specialization.get()));
        }
    }

    // Vertex input (none for mesh pipelines and bindless vertex pulling).
    std::vector<VkVertexInputBindingDescription> bindings;
    for (const VertexInputBinding& binding : m_settings.vertex_bindings)
    {
        bindings.push_back({.binding = binding.binding, .stride = binding.stride, .inputRate = to_vulkan(binding.rate)});
    }
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (const VertexInputAttribute& attribute : m_settings.vertex_attributes)
    {
        attributes.push_back({.location = attribute.location, .binding = attribute.binding, .format = to_vulkan(attribute.format),
                              .offset = attribute.offset});
    }
    const VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size()),
        .pVertexBindingDescriptions = bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };
    const VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = to_vulkan(state.topology),
        .primitiveRestartEnable = state.primitive_restart ? VK_TRUE : VK_FALSE,
    };
    const VkPipelineTessellationStateCreateInfo tessellation{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
        .patchControlPoints = state.patch_control_points,
    };
    const VkPipelineViewportStateCreateInfo viewport{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    // Rasterization, multisampling, depth and stencil, blending.
    const VkPipelineRasterizationConservativeStateCreateInfoEXT conservative{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT,
        .conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = state.rasterization.conservative ? &conservative : nullptr,
        .depthClampEnable = state.rasterization.depth_clamp ? VK_TRUE : VK_FALSE,
        .polygonMode = to_vulkan(state.rasterization.polygon_mode),
        .cullMode = to_vulkan(state.rasterization.cull_mode),
        .frontFace = to_vulkan(state.rasterization.front_face),
        .depthBiasEnable = state.rasterization.depth_bias ? VK_TRUE : VK_FALSE,
        .depthBiasConstantFactor = state.rasterization.depth_bias_constant,
        .depthBiasSlopeFactor = state.rasterization.depth_bias_slope,
        .lineWidth = state.rasterization.line_width,
    };
    const VkSampleMask sample_mask = state.sample_mask;
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = to_vulkan(description.samples),
        .pSampleMask = &sample_mask,
        .alphaToCoverageEnable = state.alpha_to_coverage ? VK_TRUE : VK_FALSE,
    };
    const auto stencil = [](const StencilOperationState& face) -> VkStencilOpState
    {
        return {.failOp = to_vulkan(face.fail),
                .passOp = to_vulkan(face.pass),
                .depthFailOp = to_vulkan(face.depth_fail),
                .compareOp = to_vulkan(face.compare),
                .compareMask = face.compare_mask,
                .writeMask = face.write_mask,
                .reference = face.reference};
    };
    const DepthStencilState& depth_stencil = state.depth_stencil;
    const VkPipelineDepthStencilStateCreateInfo depth{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = depth_stencil.depth_test ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = depth_stencil.depth_write ? VK_TRUE : VK_FALSE,
        .depthCompareOp = to_vulkan(depth_stencil.depth_compare),
        .depthBoundsTestEnable = depth_stencil.depth_bounds_test ? VK_TRUE : VK_FALSE,
        .stencilTestEnable = depth_stencil.stencil_test ? VK_TRUE : VK_FALSE,
        .front = stencil(depth_stencil.front),
        .back = stencil(depth_stencil.back),
        .minDepthBounds = depth_stencil.minimum_depth_bound,
        .maxDepthBounds = depth_stencil.maximum_depth_bound,
    };
    std::vector<VkPipelineColorBlendAttachmentState> blends;
    for (std::size_t index = 0; index < m_settings.color_formats.size(); ++index)
    {
        const ColorBlendAttachment blend = m_settings.color_blend.empty() ? ColorBlendAttachment{} : m_settings.color_blend[index];
        blends.push_back({
            .blendEnable = blend.blend ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor = to_vulkan(blend.source_color_factor),
            .dstColorBlendFactor = to_vulkan(blend.destination_color_factor),
            .colorBlendOp = to_vulkan(blend.color_operation),
            .srcAlphaBlendFactor = to_vulkan(blend.source_alpha_factor),
            .dstAlphaBlendFactor = to_vulkan(blend.destination_alpha_factor),
            .alphaBlendOp = to_vulkan(blend.alpha_operation),
            .colorWriteMask = to_vulkan(blend.write_mask),
        });
    }
    const VkPipelineColorBlendStateCreateInfo color_blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = static_cast<std::uint32_t>(blends.size()),
        .pAttachments = blends.data(),
        .blendConstants = {state.blend_constants[0], state.blend_constants[1], state.blend_constants[2], state.blend_constants[3]},
    };
    const std::vector<VkDynamicState> dynamic = dynamic_states(state.dynamic_states);
    const VkPipelineDynamicStateCreateInfo dynamic_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<std::uint32_t>(dynamic.size()),
        .pDynamicStates = dynamic.data(),
    };

    // Dynamic rendering: attachment formats instead of a render pass; then the shading rate.
    std::vector<VkFormat> color_formats;
    for (const Format format : m_settings.color_formats)
    {
        color_formats.push_back(to_vulkan(format));
    }
    const VkPipelineFragmentShadingRateStateCreateInfoKHR shading_rate{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR,
        .fragmentSize = to_vulkan(state.shading_rate),
        .combinerOps = {VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR, VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR},
    };
    const VkPipelineRenderingCreateInfo rendering{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = m_device.enabled(DeviceFeature::VARIABLE_RATE_SHADING) ? &shading_rate : nullptr,
        .viewMask = description.view_mask,
        .colorAttachmentCount = static_cast<std::uint32_t>(color_formats.size()),
        .pColorAttachmentFormats = color_formats.data(),
        .depthAttachmentFormat = to_vulkan(description.depth_format),
        .stencilAttachmentFormat = to_vulkan(description.stencil_format),
    };
    const bool mesh = m_shaders[3] != nullptr;
    const bool tessellated = m_shaders[5] != nullptr;
    const VkGraphicsPipelineCreateInfo information{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = static_cast<std::uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = mesh ? nullptr : &vertex_input,
        .pInputAssemblyState = mesh ? nullptr : &input_assembly,
        .pTessellationState = tessellated ? &tessellation : nullptr,
        .pViewportState = &viewport,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic_state,
        .layout = m_layout->native(),
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    m_device.check_result(m_device.functions().vkCreateGraphicsPipelines(m_device.native(), cache(description.cache, OPERATION), 1,
                                                                         &information, nullptr, &pipeline),
                          OPERATION, "vkCreateGraphicsPipelines");
    m_device.name_object(VK_OBJECT_TYPE_PIPELINE, pipeline, m_settings.debug_name);
    return pipeline;
}

// ---------------------------------------------------------------------------------------------------
// Ray tracing pipelines
// ---------------------------------------------------------------------------------------------------

class VulkanRayTracingPipeline final : public RayTracingPipeline, public PipelineBuilder
{
public:
    VulkanRayTracingPipeline(VulkanDevice& device, const RayTracingPipelineDescription& description);
    ~VulkanRayTracingPipeline() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }

    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }

    void set_debug_name(std::string_view name) override
    {
        m_debug_name = name;
    }

    [[nodiscard]] auto handle() const noexcept -> RayTracingPipelineHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto layout() const noexcept -> PipelineLayoutHandle override
    {
        return m_layout->handle();
    }

    [[nodiscard]] auto status() const noexcept -> PipelineStatus override
    {
        return m_state.status.load(std::memory_order_acquire);
    }

    void wait() const override
    {
        wait_for_compilation();
    }

    [[nodiscard]] auto shader_group_handles(std::uint32_t first, std::uint32_t count) const -> std::vector<std::byte> override;
    [[nodiscard]] auto build() const -> VkPipeline override;
    void validate() const override;

    [[nodiscard]] auto pipeline_name() const -> std::string override
    {
        return m_debug_name;
    }

    void forget(const VulkanShaderModule* module) noexcept override
    {
        for (VulkanShaderModule*& shader : m_modules)
        {
            if (shader == module)
            {
                shader = nullptr;
                m_forgotten = true;
            }
        }
    }

private:
    /// @brief Index of @p handle's module among the pipeline's stages (VK_SHADER_UNUSED_KHR for none).
    [[nodiscard]] auto stage_index(ShaderModuleHandle handle) const noexcept -> std::uint32_t;

private:
    const VulkanPipelineLayout* m_layout;
    std::vector<RayTracingShaderGroup> m_groups;
    std::vector<ShaderModuleHandle> m_module_handles; ///< One per distinct module, in stage order.
    std::vector<VulkanShaderModule*> m_modules;
    std::vector<SpecializationConstant> m_constants;
    std::uint32_t m_recursion_depth;
    PipelineCacheHandle m_cache;
    bool m_forgotten = false;
    RayTracingPipelineHandle m_handle;
    std::string m_debug_name;
};

auto
VulkanDevice::create_ray_tracing_pipeline(const RayTracingPipelineDescription& description) -> std::unique_ptr<RayTracingPipeline>
{
    require(DeviceFeature::RAY_TRACING, "Device::create_ray_tracing_pipeline");
    return std::make_unique<VulkanRayTracingPipeline>(*this, description);
}

VulkanRayTracingPipeline::VulkanRayTracingPipeline(VulkanDevice& device,
                                                   const RayTracingPipelineDescription& description)
    : RayTracingPipeline{}
    , PipelineBuilder{device}
    , m_layout{&layout_of(device, description.layout, "Device::create_ray_tracing_pipeline")}
    , m_groups{description.groups.begin(), description.groups.end()}
    , m_constants{description.specialization_constants.begin(), description.specialization_constants.end()}
    , m_recursion_depth{description.maximum_recursion_depth}
    , m_cache{description.cache}
    , m_debug_name{description.debug_name}
{
    RayTracingPipeline::set_validation_level(device.validation_level());
    static constexpr std::string_view OPERATION = "Device::create_ray_tracing_pipeline";

    // Every distinct module once, in the order the groups name them.
    for (const RayTracingShaderGroup& group : m_groups)
    {
        for (const ShaderModuleHandle handle : {group.general, group.closest_hit, group.any_hit, group.intersection})
        {
            if (handle.is_valid() && !std::ranges::contains(m_module_handles, handle))
            {
                m_module_handles.push_back(handle);
                m_modules.push_back(module_of(device, handle, OPERATION));
            }
        }
    }
    const std::uint32_t maximum_depth = device.physical_device().properties().limits.maximum_ray_recursion_depth;
    if (RayTracingPipeline::validates(ValidationLevel::CREATION))
    {
        if (m_recursion_depth == 0 || m_recursion_depth > maximum_depth)
        {
            reject(OPERATION, std::format("ray tracing pipeline '{}': recursion depth {} is outside [1, {}]", m_debug_name,
                                          m_recursion_depth, maximum_depth));
        }
        validate();
    }
    start(description.creation_mode);
    m_handle = device.handles().ray_tracing_pipelines.insert({.state = &m_state, .layout = m_layout->native()});
    for (VulkanShaderModule* module : m_modules)
    {
        module->add_dependent(this);
    }
}

VulkanRayTracingPipeline::~VulkanRayTracingPipeline()
{
    (void)m_device.handles().ray_tracing_pipelines.remove(m_handle);
    for (VulkanShaderModule* module : m_modules)
    {
        if (module != nullptr)
        {
            module->remove_dependent(this);
        }
    }
    release();
}

void
VulkanRayTracingPipeline::validate() const
{
    const RayTracingPipelineDescription description{
        .layout = m_layout->handle(),
        .groups = m_groups,
        .maximum_recursion_depth = m_recursion_depth,
        .specialization_constants = m_constants,
        .debug_name = m_debug_name,
    };
    validate_ray_tracing_pipeline(*m_layout, description, [this](ShaderModuleHandle handle) -> const ShaderModule*
    {
        const auto found = std::ranges::find(m_module_handles, handle);
        return found == m_module_handles.end() ? nullptr : m_modules[static_cast<std::size_t>(found - m_module_handles.begin())];
    });
}

auto
VulkanRayTracingPipeline::stage_index(ShaderModuleHandle handle) const noexcept -> std::uint32_t
{
    const auto found = std::ranges::find(m_module_handles, handle);
    return found == m_module_handles.end() ? VK_SHADER_UNUSED_KHR : static_cast<std::uint32_t>(found - m_module_handles.begin());
}

auto
VulkanRayTracingPipeline::build() const -> VkPipeline
{
    static constexpr std::string_view OPERATION = "Device::create_ray_tracing_pipeline";
    if (m_forgotten)
    {
        reject("RayTracingPipeline::rebuild", std::format("a shader module of ray tracing pipeline '{}' was destroyed", m_debug_name));
    }
    const Specialization specialization{m_constants};
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    for (const VulkanShaderModule* module : m_modules)
    {
        stages.push_back(stage_information(*module, specialization.get()));
    }
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
    for (const RayTracingShaderGroup& group : m_groups)
    {
        groups.push_back({
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .type = group.type == RayTracingShaderGroupType::GENERAL         ? VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR
                    : group.type == RayTracingShaderGroupType::TRIANGLES_HIT ? VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR
                                                                             : VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR,
            .generalShader = stage_index(group.general),
            .closestHitShader = stage_index(group.closest_hit),
            .anyHitShader = stage_index(group.any_hit),
            .intersectionShader = stage_index(group.intersection),
        });
    }
    const VkRayTracingPipelineCreateInfoKHR information{
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
        .stageCount = static_cast<std::uint32_t>(stages.size()),
        .pStages = stages.data(),
        .groupCount = static_cast<std::uint32_t>(groups.size()),
        .pGroups = groups.data(),
        .maxPipelineRayRecursionDepth = m_recursion_depth,
        .layout = m_layout->native(),
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    m_device.check_result(m_device.functions().vkCreateRayTracingPipelinesKHR(m_device.native(), VK_NULL_HANDLE, cache(m_cache, OPERATION), 1,
                                                                              &information, nullptr, &pipeline),
                          OPERATION, "vkCreateRayTracingPipelinesKHR");
    m_device.name_object(VK_OBJECT_TYPE_PIPELINE, pipeline, m_debug_name);
    return pipeline;
}

auto
VulkanRayTracingPipeline::shader_group_handles(std::uint32_t first,
                                               std::uint32_t count) const -> std::vector<std::byte>
{
    static constexpr std::string_view OPERATION = "RayTracingPipeline::shader_group_handles";
    wait_for_compilation();
    if (std::uint64_t{first} + count > m_groups.size())
    {
        reject(OPERATION, std::format("ray tracing pipeline '{}' has {} groups; groups [{}, {}) do not exist", m_debug_name, m_groups.size(),
                                      first, std::uint64_t{first} + count));
    }
    const std::uint32_t handle_size = m_device.physical_device().properties().limits.shader_group_handle_size;
    std::vector<std::byte> handles(std::size_t{handle_size} * count);
    m_device.check_result(m_device.functions().vkGetRayTracingShaderGroupHandlesKHR(
                              m_device.native(), m_state.pipeline.load(std::memory_order_acquire), first, count, handles.size(), handles.data()),
                          OPERATION, "vkGetRayTracingShaderGroupHandlesKHR");
    return handles;
}

// ---------------------------------------------------------------------------------------------------
// Pipeline caches
// ---------------------------------------------------------------------------------------------------

class VulkanPipelineCache final : public PipelineCache
{
public:
    VulkanPipelineCache(VulkanDevice& device, const PipelineCacheDescription& description);
    ~VulkanPipelineCache() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }

    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }

    void set_debug_name(std::string_view name) override
    {
        m_debug_name = name;
        m_device.name_object(VK_OBJECT_TYPE_PIPELINE_CACHE, m_cache, m_debug_name);
    }

    [[nodiscard]] auto handle() const noexcept -> PipelineCacheHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto loaded_initial_data() const noexcept -> bool override
    {
        return m_loaded;
    }

    [[nodiscard]] auto serialize() const -> std::vector<std::byte> override;

private:
    VulkanDevice& m_device;
    VkPipelineCache m_cache = VK_NULL_HANDLE;
    bool m_loaded = false;
    PipelineCacheHandle m_handle;
    std::string m_debug_name;
};

auto
VulkanDevice::create_pipeline_cache(const PipelineCacheDescription& description) -> std::unique_ptr<PipelineCache>
{
    return std::make_unique<VulkanPipelineCache>(*this, description);
}

VulkanPipelineCache::VulkanPipelineCache(VulkanDevice& device,
                                         const PipelineCacheDescription& description)
    : PipelineCache{}
    , m_device{device}
    , m_debug_name{description.debug_name}
{
    set_validation_level(device.validation_level());

    // Data of another driver, device or backend is ignored: its header must name this device.
    std::span<const std::byte> data = description.initial_data;
    VkPipelineCacheHeaderVersionOne header{};
    if (data.size() >= sizeof(header))
    {
        std::memcpy(&header, data.data(), sizeof(header));
    }
    const VkPhysicalDeviceProperties& properties = device.vulkan_physical_device().capabilities().properties;
    m_loaded = data.size() >= sizeof(header) && header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
               header.vendorID == properties.vendorID && header.deviceID == properties.deviceID &&
               std::ranges::equal(header.pipelineCacheUUID, properties.pipelineCacheUUID);
    if (!m_loaded)
    {
        data = {};
    }
    const VkPipelineCacheCreateInfo information{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .initialDataSize = data.size(),
        .pInitialData = data.data(),
    };
    device.check_result(device.functions().vkCreatePipelineCache(device.native(), &information, nullptr, &m_cache),
                        "Device::create_pipeline_cache", "vkCreatePipelineCache");
    m_handle = device.handles().pipeline_caches.insert({.cache = m_cache});
    device.name_object(VK_OBJECT_TYPE_PIPELINE_CACHE, m_cache, m_debug_name);
}

VulkanPipelineCache::~VulkanPipelineCache()
{
    (void)m_device.handles().pipeline_caches.remove(m_handle);
    m_device.vulkan_deleter().enqueue([device = &m_device, cache = m_cache]
    {
        device->functions().vkDestroyPipelineCache(device->native(), cache, nullptr);
    });
}

auto
VulkanPipelineCache::serialize() const -> std::vector<std::byte>
{
    std::size_t size = 0;
    m_device.check_result(m_device.functions().vkGetPipelineCacheData(m_device.native(), m_cache, &size, nullptr), "PipelineCache::serialize",
                          "vkGetPipelineCacheData");
    std::vector<std::byte> data(size);
    m_device.check_result(m_device.functions().vkGetPipelineCacheData(m_device.native(), m_cache, &size, data.data()),
                          "PipelineCache::serialize", "vkGetPipelineCacheData");
    data.resize(size);
    return data;
}

// ---------------------------------------------------------------------------------------------------
// Descriptor pools and sets
// ---------------------------------------------------------------------------------------------------

class VulkanDescriptorPool;

/// @brief A descriptor set allocated from a pool.
class VulkanDescriptorSet final : public DescriptorSet
{
public:
    VulkanDescriptorSet(VulkanDevice& device, VulkanDescriptorPool& pool, VkDescriptorSet set, const VulkanDescriptorSetLayout& layout);
    ~VulkanDescriptorSet() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }

    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }

    void set_debug_name(std::string_view name) override
    {
        m_debug_name = name;
        m_device.name_object(VK_OBJECT_TYPE_DESCRIPTOR_SET, m_set, m_debug_name);
    }

    [[nodiscard]] auto handle() const noexcept -> DescriptorSetHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto layout() const noexcept -> DescriptorSetLayoutHandle override
    {
        return m_layout_handle;
    }

    void update(std::span<const DescriptorWrite> writes) override;

private:
    VulkanDevice& m_device;
    VulkanDescriptorPool& m_pool;
    VkDescriptorSet m_set;
    DescriptorSetLayoutHandle m_layout_handle;
    std::vector<DescriptorSetLayoutBinding> m_bindings;
    DescriptorSetHandle m_handle;
    std::string m_debug_name;
};

/// @brief A descriptor pool; it counts the sets alive so that reset() can refuse while some are.
class VulkanDescriptorPool final : public DescriptorPool
{
public:
    VulkanDescriptorPool(VulkanDevice& device, const DescriptorPoolDescription& description);
    ~VulkanDescriptorPool() override;

    [[nodiscard]] auto backend() const noexcept -> BackendKind override
    {
        return BackendKind::VULKAN;
    }

    [[nodiscard]] auto debug_name() const noexcept -> std::string_view override
    {
        return m_debug_name;
    }

    void set_debug_name(std::string_view name) override
    {
        m_debug_name = name;
        m_device.name_object(VK_OBJECT_TYPE_DESCRIPTOR_POOL, m_pool, m_debug_name);
    }

    [[nodiscard]] auto handle() const noexcept -> DescriptorPoolHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto allocate(DescriptorSetLayoutHandle layout, std::uint32_t variable_descriptor_count)
        -> std::unique_ptr<DescriptorSet> override;
    void reset() override;

    /// @brief A set of this pool is destroyed: it returns its descriptors when the pool frees sets.
    void release(VkDescriptorSet set) noexcept
    {
        --m_live_sets;
        if (m_free_individual_sets)
        {
            m_device.vulkan_deleter().enqueue([device = &m_device, pool = m_pool, set]
            {
                device->functions().vkFreeDescriptorSets(device->native(), pool, 1, &set);
            });
        }
    }

private:
    VulkanDevice& m_device;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    bool m_free_individual_sets;
    bool m_update_after_bind;
    std::size_t m_live_sets = 0;
    DescriptorPoolHandle m_handle;
    std::string m_debug_name;
};

auto
VulkanDevice::create_descriptor_pool(const DescriptorPoolDescription& description) -> std::unique_ptr<DescriptorPool>
{
    return std::make_unique<VulkanDescriptorPool>(*this, description);
}

VulkanDescriptorPool::VulkanDescriptorPool(VulkanDevice& device,
                                           const DescriptorPoolDescription& description)
    : DescriptorPool{}
    , m_device{device}
    , m_free_individual_sets{description.free_individual_sets}
    , m_update_after_bind{description.update_after_bind}
    , m_debug_name{description.debug_name}
{
    set_validation_level(device.validation_level());
    if (description.maximum_sets == 0 || description.sizes.empty())
    {
        reject("Device::create_descriptor_pool",
               std::format("descriptor pool '{}' needs a set count and descriptor counts", m_debug_name));
    }
    std::vector<VkDescriptorPoolSize> sizes;
    for (const DescriptorPoolSize& size : description.sizes)
    {
        sizes.push_back({.type = to_vulkan(size.type), .descriptorCount = size.count});
    }
    VkDescriptorPoolCreateFlags flags = 0;
    if (description.free_individual_sets)
    {
        flags |= VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    }
    if (description.update_after_bind)
    {
        flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    }
    const VkDescriptorPoolCreateInfo information{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = flags,
        .maxSets = description.maximum_sets,
        .poolSizeCount = static_cast<std::uint32_t>(sizes.size()),
        .pPoolSizes = sizes.data(),
    };
    device.check_result(device.functions().vkCreateDescriptorPool(device.native(), &information, nullptr, &m_pool),
                        "Device::create_descriptor_pool", "vkCreateDescriptorPool");
    m_handle = device.handles().descriptor_pools.insert({.pool = m_pool});
    device.name_object(VK_OBJECT_TYPE_DESCRIPTOR_POOL, m_pool, m_debug_name);
}

VulkanDescriptorPool::~VulkanDescriptorPool()
{
    (void)m_device.handles().descriptor_pools.remove(m_handle);
    m_device.vulkan_deleter().enqueue([device = &m_device, pool = m_pool]
    {
        device->functions().vkDestroyDescriptorPool(device->native(), pool, nullptr);
    });
}

auto
VulkanDescriptorPool::allocate(DescriptorSetLayoutHandle layout,
                               std::uint32_t variable_descriptor_count) -> std::unique_ptr<DescriptorSet>
{
    static constexpr std::string_view OPERATION = "DescriptorPool::allocate";
    const DescriptorSetLayoutRecord& record = m_device.handles().descriptor_set_layouts.get(layout, OPERATION);
    if (validates(ValidationLevel::CREATION) && record.object->update_after_bind() && !m_update_after_bind)
    {
        reject(OPERATION, std::format("layout '{}' has update-after-bind bindings; pool '{}' was created without update_after_bind",
                                      record.object->debug_name(), m_debug_name));
    }

    // The size of a variable-sized last binding.
    const VkDescriptorSetVariableDescriptorCountAllocateInfo variable{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pDescriptorCounts = &variable_descriptor_count,
    };
    const VkDescriptorSetAllocateInfo information{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = variable_descriptor_count != 0 ? &variable : nullptr,
        .descriptorPool = m_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &record.layout,
    };
    VkDescriptorSet set = VK_NULL_HANDLE;
    const VkResult result = m_device.functions().vkAllocateDescriptorSets(m_device.native(), &information, &set);
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
    {
        throw OutOfMemoryError({
            .backend = BackendKind::VULKAN,
            .operation = std::string{OPERATION},
            .domain = MemoryDomain::DEVICE,
            .requested_bytes = 0,
            .details = std::format("descriptor pool '{}' is exhausted ({}); reset it, or create a larger one", m_debug_name,
                                   result_name(result)),
        });
    }
    m_device.check_result(result, OPERATION, "vkAllocateDescriptorSets");
    ++m_live_sets;
    return std::make_unique<VulkanDescriptorSet>(m_device, *this, set, *record.object);
}

void
VulkanDescriptorPool::reset()
{
    if (m_live_sets != 0)
    {
        reject("DescriptorPool::reset", std::format("descriptor pool '{}' still has {} live descriptor set(s)", m_debug_name, m_live_sets),
               "destroy the sets first (the GPU may still use them: the pool waits for nothing)");
    }
    m_device.check_result(m_device.functions().vkResetDescriptorPool(m_device.native(), m_pool, 0), "DescriptorPool::reset",
                          "vkResetDescriptorPool");
}

VulkanDescriptorSet::VulkanDescriptorSet(VulkanDevice& device,
                                         VulkanDescriptorPool& pool,
                                         VkDescriptorSet set,
                                         const VulkanDescriptorSetLayout& layout)
    : DescriptorSet{}
    , m_device{device}
    , m_pool{pool}
    , m_set{set}
    , m_layout_handle{layout.handle()}
    , m_bindings{layout.bindings().begin(), layout.bindings().end()}
{
    set_validation_level(device.validation_level());
    m_handle = device.handles().descriptor_sets.insert({.set = m_set});
}

VulkanDescriptorSet::~VulkanDescriptorSet()
{
    (void)m_device.handles().descriptor_sets.remove(m_handle);
    m_pool.release(m_set);
}

void
VulkanDescriptorSet::update(std::span<const DescriptorWrite> writes)
{
    static constexpr std::string_view OPERATION = "DescriptorSet::update";
    HandleTables& handles = m_device.handles();

    // Storage for the native descriptions, sized first so pointers into it stay valid.
    std::size_t count = 0;
    for (const DescriptorWrite& write : writes)
    {
        count += write.resources.size();
    }
    std::vector<VkDescriptorImageInfo> images;
    std::vector<VkDescriptorBufferInfo> buffers;
    std::vector<VkBufferView> texel_buffers;
    std::vector<VkAccelerationStructureKHR> structures;
    std::vector<VkWriteDescriptorSetAccelerationStructureKHR> structure_writes;
    images.reserve(count);
    buffers.reserve(count);
    texel_buffers.reserve(count);
    structures.reserve(count);
    structure_writes.reserve(writes.size());
    std::vector<VkWriteDescriptorSet> native;
    native.reserve(writes.size());

    for (const DescriptorWrite& write : writes)
    {
        // The binding exists, its array holds the elements, and every resource matches its type.
        const auto binding = std::ranges::find(m_bindings, write.binding, &DescriptorSetLayoutBinding::binding);
        if (binding == m_bindings.end())
        {
            reject(OPERATION, std::format("descriptor set '{}' has no binding {}", m_debug_name, write.binding));
        }
        if (validates(ValidationLevel::CREATION))
        {
            if (std::uint64_t{write.array_element} + write.resources.size() > binding->count)
            {
                reject(OPERATION, std::format("binding {} of descriptor set '{}' has {} element(s); elements [{}, {}) do not exist",
                                              write.binding, m_debug_name, binding->count, write.array_element,
                                              std::uint64_t{write.array_element} + write.resources.size()));
            }
            for (const DescriptorResource& resource : write.resources)
            {
                if (!descriptor_resource_matches(binding->type, resource))
                {
                    reject(OPERATION, std::format("binding {} of descriptor set '{}' is {}, which takes {}, not {}", write.binding,
                                                  m_debug_name, binding->type, descriptor_resource_name(binding->type),
                                                  descriptor_resource_name(resource)));
                }
            }
        }

        // The resources, translated.
        const std::size_t first_image = images.size();
        const std::size_t first_buffer = buffers.size();
        const std::size_t first_texel_buffer = texel_buffers.size();
        const std::size_t first_structure = structures.size();
        for (const DescriptorResource& resource : write.resources)
        {
            if (const auto* buffer = std::get_if<BufferDescriptor>(&resource))
            {
                buffers.push_back({.buffer = handles.buffers.get(buffer->buffer, OPERATION).buffer,
                                   .offset = buffer->offset,
                                   .range = buffer->range == WHOLE_SIZE ? VK_WHOLE_SIZE : buffer->range});
            }
            else if (const auto* image = std::get_if<ImageDescriptor>(&resource))
            {
                images.push_back({.imageView = handles.image_views.get(image->view, OPERATION).view, .imageLayout = to_vulkan(image->layout)});
            }
            else if (const auto* sampler = std::get_if<SamplerDescriptor>(&resource))
            {
                images.push_back({.sampler = handles.samplers.get(sampler->sampler, OPERATION).sampler});
            }
            else if (const auto* combined = std::get_if<CombinedImageSamplerDescriptor>(&resource))
            {
                images.push_back({.sampler = handles.samplers.get(combined->sampler, OPERATION).sampler,
                                  .imageView = handles.image_views.get(combined->view, OPERATION).view,
                                  .imageLayout = to_vulkan(combined->layout)});
            }
            else if (const auto* texel = std::get_if<BufferViewDescriptor>(&resource))
            {
                texel_buffers.push_back(handles.buffer_views.get(texel->view, OPERATION).view);
            }
            else if (const auto* structure = std::get_if<AccelerationStructureDescriptor>(&resource))
            {
                structures.push_back(handles.acceleration_structures.get(structure->structure, OPERATION).structure);
            }
        }
        const void* next = nullptr;
        if (structures.size() > first_structure)
        {
            structure_writes.push_back({.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
                                        .accelerationStructureCount = static_cast<std::uint32_t>(structures.size() - first_structure),
                                        .pAccelerationStructures = structures.data() + first_structure});
            next = &structure_writes.back();
        }
        native.push_back({
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = next,
            .dstSet = m_set,
            .dstBinding = write.binding,
            .dstArrayElement = write.array_element,
            .descriptorCount = static_cast<std::uint32_t>(write.resources.size()),
            .descriptorType = to_vulkan(binding->type),
            .pImageInfo = images.size() > first_image ? images.data() + first_image : nullptr,
            .pBufferInfo = buffers.size() > first_buffer ? buffers.data() + first_buffer : nullptr,
            .pTexelBufferView = texel_buffers.size() > first_texel_buffer ? texel_buffers.data() + first_texel_buffer : nullptr,
        });
    }
    m_device.functions().vkUpdateDescriptorSets(m_device.native(), static_cast<std::uint32_t>(native.size()), native.data(), 0, nullptr);
}

} // namespace gfx::vulkan
