module gfx.errors;

import std;
import gfx.enums;

namespace gfx
{

// ------------------------------------------------------------------------------------------------- DeviceLostError
DeviceLostError::DeviceLostError(DeviceLostDescription description,
                                 std::source_location location)
    : Error(description.backend, description.operation,
            std::format("the device was lost; every object created from it is unusable{}{}{}",
                        description.details.empty() ? std::string{} : std::format(".\n  Details: {}", description.details),
                        description.active_markers.empty()
                            ? std::string{}
                            : std::format("\n  GPU was inside: {}",
                                          description.active_markers | std::views::join_with(std::string_view{" > "}) |
                                              std::ranges::to<std::string>()),
                        description.crash_report_path.empty() ? std::string{}
                                                              : std::format("\n  Crash report: {}", description.crash_report_path)),
            location)
    , m_active_markers{std::move(description.active_markers)}
    , m_crash_report_path{std::move(description.crash_report_path)}
{
}

auto
DeviceLostError::active_markers() const noexcept -> std::span<const std::string>
{
    return m_active_markers;
}

auto
DeviceLostError::crash_report_path() const noexcept -> std::string_view
{
    return m_crash_report_path;
}

// ------------------------------------------------------------------------------------------------- ShaderCompilationError
ShaderCompilationError::ShaderCompilationError(ShaderCompilationFailureDescription description,
                                               std::source_location location)
    : Error(std::nullopt, description.operation,
            std::format("failed to compile {} shader '{}':\n{}", description.stage, description.shader_name,
                        indented(description.compiler_log, "    ")),
            location)
    , m_shader_name{std::move(description.shader_name)}
    , m_stage{description.stage}
    , m_compiler_log{std::move(description.compiler_log)}
{
}

auto
ShaderCompilationError::shader_name() const noexcept -> std::string_view
{
    return m_shader_name;
}

auto
ShaderCompilationError::stage() const noexcept -> ShaderStage
{
    return m_stage;
}

auto
ShaderCompilationError::compiler_log() const noexcept -> std::string_view
{
    return m_compiler_log;
}

// ------------------------------------------------------------------------------------------------- ShaderLayoutMismatchError
ShaderLayoutMismatchError::ShaderLayoutMismatchError(ShaderLayoutMismatchDescription description,
                                                     std::source_location location)
    : Error(description.backend, description.operation,
            std::format("shader '{}' does not match its layout ({}): {}{}", description.shader_name, description.mismatch,
                        description.problem, suggestion_line(description.suggestion)),
            location)
    , m_shader_name{std::move(description.shader_name)}
    , m_mismatch{description.mismatch}
    , m_problem{std::move(description.problem)}
{
}

auto
ShaderLayoutMismatchError::shader_name() const noexcept -> std::string_view
{
    return m_shader_name;
}

auto
ShaderLayoutMismatchError::mismatch() const noexcept -> LayoutMismatch
{
    return m_mismatch;
}

auto
ShaderLayoutMismatchError::problem() const noexcept -> std::string_view
{
    return m_problem;
}

} // namespace gfx
