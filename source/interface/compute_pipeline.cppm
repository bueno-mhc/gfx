/// @file compute_pipeline.cppm
/// @brief ComputePipeline: a compute shader bound to a pipeline layout.
export module gfx.interface:compute_pipeline;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :shader_module;

export namespace gfx
{

/// @brief How to create a ComputePipeline.
struct ComputePipelineDescription
{
    PipelineLayoutHandle layout{};                                        ///< Pipeline layout (null = the bindless layout).
    ShaderModuleHandle shader{};                                          ///< Compute shader module.
    std::span<const SpecializationConstant> specialization_constants{};   ///< Constant values.
    PipelineCacheHandle cache{};                                          ///< Cache to use (null = the device's own).
    PipelineCreationMode creation_mode = PipelineCreationMode::IMMEDIATE; ///< Compile now or in the background.
    std::string_view debug_name{};                                        ///< Name shown by debugging tools.
};

/// @brief A compute shader bound to a pipeline layout; a peer of GraphicsPipeline.
class ComputePipeline : public BackendObject
{
public:
    /// @brief Handle naming this pipeline when binding it.
    [[nodiscard]] virtual auto handle() const noexcept -> ComputePipelineHandle = 0;

    /// @brief Pipeline layout.
    [[nodiscard]] virtual auto layout() const noexcept -> PipelineLayoutHandle = 0;

    /// @brief Workgroup size of the shader.
    [[nodiscard]] virtual auto workgroup_size() const noexcept -> std::array<std::uint32_t, 3> = 0;

    /// @brief Whether it can be bound (see PipelineCreationMode::ASYNCHRONOUS).
    [[nodiscard]] virtual auto status() const noexcept -> PipelineStatus = 0;

    /// @brief Blocks until compiled.
    /// @throws The compilation error when it failed.
    virtual void wait() const = 0;
};

void
ComputePipeline::wait() const
{
    throw_not_implemented("ComputePipeline::wait");
}

} // namespace gfx
