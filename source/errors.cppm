/// @file errors.cppm
/// @brief Exceptions thrown by gfx.
///
/// Every exception derives from gfx::Error (itself a std::runtime_error). Messages are multi-line and
/// always name the backend (when one is involved), the operation, the problem, a suggestion when one
/// exists, and the source location that raised it:
/// @code
/// [gfx::METAL] Device::create_event: capability 'Event (split barrier)' is not supported.
///   Reason: Metal has no GPU-side split-barrier object.
///   Suggestion: use Recorder::barrier() within a queue.
///   Location: source/backends/metal/device.cpp:212 (create_event)
/// @endcode
export module gfx.errors;

import std;
import gfx.enums;

export namespace gfx 
{

/// @brief Base class of every exception thrown by gfx.
class Error : public std::runtime_error 
{
public:
    /// @brief Backend that raised the error, or std::nullopt for backend-independent errors.
    [[nodiscard]] auto backend() const noexcept -> std::optional<BackendKind>;

    /// @brief Operation that failed, e.g. "Device::create_buffer".
    [[nodiscard]] auto operation() const noexcept -> std::string_view;

    /// @brief Source location that raised the error.
    [[nodiscard]] auto location() const noexcept -> const std::source_location&;

protected:
    /// @param backend   Backend involved, if any.
    /// @param operation Operation that failed.
    /// @param details   Description of the problem; may span several lines.
    /// @param location  Where the error was raised.
    Error(std::optional<BackendKind> backend, std::string operation, std::string_view details, std::source_location location);

private:
    std::optional<BackendKind> m_backend;
    std::string m_operation;
    std::source_location m_location;
};




/// @brief Describes an operation a backend cannot perform.
struct UnsupportedCapabilityDescription 
{
    BackendKind backend;    ///< Backend that lacks the capability.
    std::string operation;  ///< Operation that was requested, e.g. "Device::create_event".
    std::string capability; ///< Missing capability, e.g. "Event (split barrier)".
    std::string reason;     ///< Why the backend cannot provide it.
    std::string suggestion; ///< What the caller can do instead.
};

/// @brief Thrown when a backend cannot honor an operation of the abstraction.
///
/// Backends never degrade silently: an operation they cannot implement throws this error.
class BackendCapabilityError final : public Error 
{
public:
    /// @param description What is unsupported, why, and what to do instead.
    /// @param location    Where the error was raised (defaults to the caller).
    explicit BackendCapabilityError(UnsupportedCapabilityDescription description,
                                    std::source_location location = std::source_location::current());

    /// @brief Missing capability.
    [[nodiscard]] auto capability() const noexcept -> std::string_view;

    /// @brief Why the backend cannot provide the capability.
    [[nodiscard]] auto reason() const noexcept -> std::string_view;

    /// @brief What the caller can do instead.
    [[nodiscard]] auto suggestion() const noexcept -> std::string_view;

private:
    std::string m_capability;
    std::string m_reason;
    std::string m_suggestion;
};




/// @brief Describes an invalid use of the API (invalid description, wrong call order, ...).
struct InvalidUsageDescription 
{
    std::optional<BackendKind> backend; ///< Backend involved, if any.
    std::string operation;              ///< Operation that was called.
    std::string problem;                ///< What is wrong.
    std::string suggestion;             ///< How to fix it; may be empty.
};

/// @brief Thrown when the API is used incorrectly.
class InvalidUsageError final : public Error 
{
public:
    /// @param description What is wrong and how to fix it.
    /// @param location    Where the error was raised (defaults to the caller).
    explicit InvalidUsageError(InvalidUsageDescription description,
                               std::source_location location = std::source_location::current());

    /// @brief What is wrong.
    [[nodiscard]] auto problem() const noexcept -> std::string_view;

    /// @brief How to fix it; may be empty.
    [[nodiscard]] auto suggestion() const noexcept -> std::string_view;

private:
    std::string m_problem;
    std::string m_suggestion;
};




/// @brief Describes a handle that does not name a live object.
struct InvalidHandleDescription 
{
    std::optional<BackendKind> backend; ///< Backend involved, if any.
    std::string operation;              ///< Operation that received the handle.
    std::string handle_type;            ///< Kind of handle, e.g. "Buffer".
    std::string problem;                ///< Why it is invalid (null, destroyed, out of range, ...).
};

/// @brief Thrown when a handle is null, refers to a destroyed object, or cannot be encoded.
class InvalidHandleError final : public Error 
{
public:
    /// @param description Which handle is invalid and why.
    /// @param location    Where the error was raised (defaults to the caller).
    explicit InvalidHandleError(InvalidHandleDescription description,
                                std::source_location location = std::source_location::current());

    /// @brief Kind of handle, e.g. "Buffer".
    [[nodiscard]] auto handle_type() const noexcept -> std::string_view;

    /// @brief Why it is invalid.
    [[nodiscard]] auto problem() const noexcept -> std::string_view;

private:
    std::string m_handle_type;
    std::string m_problem;
};




/// @brief Describes a failed memory allocation.
struct OutOfMemoryDescription 
{
    std::optional<BackendKind> backend; ///< Backend involved, if any.
    std::string operation;              ///< Operation that allocated.
    MemoryDomain domain;                ///< Exhausted memory.
    std::uint64_t requested_bytes;      ///< Size of the failed allocation (0 when unknown).
    std::string details;                ///< Additional information; may be empty.
};

/// @brief Thrown when host or device memory is exhausted.
class OutOfMemoryError final : public Error 
{
public:
    /// @param description Which allocation failed.
    /// @param location    Where the error was raised (defaults to the caller).
    explicit OutOfMemoryError(OutOfMemoryDescription description,
                              std::source_location location = std::source_location::current());

    /// @brief Exhausted memory.
    [[nodiscard]] auto domain() const noexcept -> MemoryDomain;

    /// @brief Size of the failed allocation, 0 when unknown.
    [[nodiscard]] auto requested_bytes() const noexcept -> std::uint64_t;

private:
    MemoryDomain m_domain;
    std::uint64_t m_requested_bytes;
};




/// @brief Describes the loss of a device (driver reset, GPU hang, removal).
struct DeviceLostDescription 
{
    BackendKind backend;   ///< Backend whose device was lost.
    std::string operation; ///< Operation that detected the loss.
    std::string details;   ///< Additional information; may be empty.
};

/// @brief Thrown when the device is lost; every object of that device becomes unusable.
class DeviceLostError final : public Error 
{
public:
    /// @param description Where the loss was detected.
    /// @param location    Where the error was raised (defaults to the caller).
    explicit DeviceLostError(DeviceLostDescription description,
                             std::source_location location = std::source_location::current());
};




/// @brief Describes an unexpected failure reported by the native graphics API.
struct BackendApiFailureDescription 
{
    BackendKind backend;      ///< Backend that called the API.
    std::string operation;    ///< Operation of the abstraction being performed.
    std::string api_function; ///< Native function that failed, e.g. "vkCreateBuffer".
    std::int64_t result_code; ///< Native result code.
    std::string result_name;  ///< Name of the result code, e.g. "VK_ERROR_INITIALIZATION_FAILED".
};

/// @brief Thrown when the native API reports a failure that no more specific error describes.
class BackendApiError final : public Error 
{
public:
    /// @param description Which native call failed and with which result.
    /// @param location    Where the error was raised (defaults to the caller).
    explicit BackendApiError(BackendApiFailureDescription description,
                             std::source_location location = std::source_location::current());

    /// @brief Native function that failed.
    [[nodiscard]] auto api_function() const noexcept -> std::string_view;

    /// @brief Native result code.
    [[nodiscard]] auto result_code() const noexcept -> std::int64_t;

    /// @brief Name of the native result code.
    [[nodiscard]] auto result_name() const noexcept -> std::string_view;

private:
    std::string m_api_function;
    std::int64_t m_result_code;
    std::string m_result_name;
};




/// @brief Describes a shader that failed to compile.
struct ShaderCompilationFailureDescription 
{
    std::string operation;    ///< Operation that compiled the shader.
    std::string shader_name;  ///< Name identifying the shader (file name or debug name).
    ShaderStage stage;        ///< Stage the shader was compiled for.
    std::string compiler_log; ///< Diagnostics of the compiler, with line numbers.
};

/// @brief Thrown when GLSL source fails to compile to SPIR-V.
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
    std::optional<BackendKind> backend; ///< Backend involved, if any.
    std::string operation;              ///< Operation that compared the layouts.
    std::string shader_name;            ///< Name identifying the shader.
    LayoutMismatch mismatch;            ///< Kind of mismatch.
    std::string problem;                ///< Precise description (set, binding, types, ...).
    std::string suggestion;             ///< How to fix it; may be empty.
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
