/// @file errors.cppm
/// @brief Exceptions specific to the graphics library (device loss, shader compilation, shader layout
///        mismatches). The rest of the hierarchy (gfx::Error, BackendCapabilityError, InvalidUsageError,
///        InvalidHandleError, OutOfMemoryError, BackendApiError) is shared with the platform library and
///        re-exported from gfx.core.errors.
export module gfx.errors;

import std;
export import gfx.core.errors;
import gfx.enums;

export namespace gfx
{

/// @brief Describes the loss of a device (driver reset, GPU hang, removal).
struct DeviceLostDescription
{
    BackendIdentity backend;                      ///< Backend whose device was lost.
    std::string operation;                        ///< Operation that detected the loss.
    std::string details;                          ///< Additional information; may be empty.
    std::vector<std::string> active_markers{};    ///< GPU breadcrumbs: the debug labels the GPU was inside, outermost
                                                  ///< first (CrashDiagnostic::BREADCRUMBS; empty when not collected).
    std::string crash_report_path{};              ///< Vendor crash dump written (CrashDiagnostic::VENDOR_REPORT).
};

/// @brief Thrown when the device is lost; every object of that device becomes unusable.
class DeviceLostError final : public Error
{
public:
    /// @param description Where the loss was detected.
    /// @param location    Where the error was raised (defaults to the caller).
    explicit DeviceLostError(DeviceLostDescription description,
                             std::source_location location = std::source_location::current());

    /// @brief Debug labels the GPU was inside when it failed, outermost first.
    [[nodiscard]] auto active_markers() const noexcept -> std::span<const std::string>;

    /// @brief Vendor crash dump written for the failure (empty when none).
    [[nodiscard]] auto crash_report_path() const noexcept -> std::string_view;

private:
    std::vector<std::string> m_active_markers;
    std::string m_crash_report_path;
};




/// @brief Describes a shader that failed to compile.
struct ShaderCompilationFailureDescription
{
    std::string operation;    ///< Operation that compiled the shader.
    std::string shader_name;  ///< Name identifying the shader (file name or debug name).
    ShaderStage stage;        ///< Stage the shader was compiled for.
    std::string compiler_log; ///< Diagnostics of the compiler, with line numbers.
};

/// @brief Thrown when GLSL or HLSL source fails to compile to SPIR-V.
class ShaderCompilationError final : public Error
{
public:
    /// @param description Which shader failed and the compiler's diagnostics.
    /// @param location    Where the error was raised (defaults to the caller).
    explicit ShaderCompilationError(ShaderCompilationFailureDescription description,
                                    std::source_location location = std::source_location::current());

    /// @brief Name identifying the shader.
    [[nodiscard]] auto shader_name() const noexcept -> std::string_view;

    /// @brief Stage the shader was compiled for.
    [[nodiscard]] auto stage() const noexcept -> ShaderStage;

    /// @brief Diagnostics of the compiler.
    [[nodiscard]] auto compiler_log() const noexcept -> std::string_view;

private:
    std::string m_shader_name;
    ShaderStage m_stage;
    std::string m_compiler_log;
};




/// @brief Describes a shader whose interface does not match what it is combined with.
struct ShaderLayoutMismatchDescription
{
    std::optional<BackendIdentity> backend; ///< Backend involved, if any.
    std::string operation;                  ///< Operation that compared the layouts.
    std::string shader_name;                ///< Name identifying the shader.
    LayoutMismatch mismatch;                ///< Kind of mismatch.
    std::string problem;                    ///< Precise description (set, binding, types, ...).
    std::string suggestion;                 ///< How to fix it; may be empty.
};

/// @brief Thrown when a shader's bindings, inputs or outputs do not match a pipeline layout, a vertex
///        layout, color attachments, or the layout the shader was declared with at compile time.
class ShaderLayoutMismatchError final : public Error
{
public:
    /// @param description Which shader, which mismatch, and how to fix it.
    /// @param location    Where the error was raised (defaults to the caller).
    explicit ShaderLayoutMismatchError(ShaderLayoutMismatchDescription description,
                                       std::source_location location = std::source_location::current());

    /// @brief Name identifying the shader.
    [[nodiscard]] auto shader_name() const noexcept -> std::string_view;

    /// @brief Kind of mismatch.
    [[nodiscard]] auto mismatch() const noexcept -> LayoutMismatch;

    /// @brief Precise description of the mismatch.
    [[nodiscard]] auto problem() const noexcept -> std::string_view;

private:
    std::string m_shader_name;
    LayoutMismatch m_mismatch;
    std::string m_problem;
};

} // namespace gfx
