module gfx.errors;

import std;
import gfx.enums;

namespace gfx 
{

namespace 
{

auto compose_message(std::optional<BackendKind> backend, 
                     std::string_view operation, 
                     std::string_view details,
                     const std::source_location& location) -> std::string 
{
    // Format the origin as "[gfx::Backend]" or "[gfx]".
    const std::string origin = backend ? std::format("[gfx::{}]", *backend) 
                                       : std::string{"[gfx]"};
    
    // Format the message as "[gfx::Backend] operation: details\n  Location: file:line (function)" or
    return std::format("{} {}: {}\n  Location: {}:{} ({})", origin, operation, details, location.file_name(),
                       location.line(), location.function_name());
}

auto 
indent(std::string_view text, 
       std::string_view prefix) -> std::string 
{
    std::string result;

    // Indent every line of the text with the prefix. If the text is empty, return an empty string.
    for (const auto line : std::views::split(text, '\n')) 
    {
        result += prefix;
        result.append(line.begin(), line.end());
        result += '\n';
    }

    // Remove the trailing newline, so the caller can append more text on the same line if desired.
    if (!result.empty()) 
    {
        result.pop_back();
    }

    // Return the indented text.
    return result;
}

auto 
suggestion_suffix(std::string_view suggestion) -> std::string 
{
    return suggestion.empty() ? std::string{} 
                              : std::format("\n  Suggestion: {}", suggestion);
}

} // namespace

// ------------------------------------------------------------------------------------------------- Error
Error::Error(std::optional<BackendKind> backend, 
             std::string operation, 
             std::string_view details, 
             std::source_location location)
    : std::runtime_error(compose_message(backend, operation, details, location))
    , m_backend{backend}
    , m_operation{std::move(operation)}
    , m_location{location} 
{}

auto
Error::backend() const noexcept -> std::optional<BackendKind> 
{
    return m_backend;
}

auto 
Error::operation() const noexcept -> std::string_view 
{
    return m_operation;
}

auto 
Error::location() const noexcept -> const std::source_location& 
{
    return m_location;
}

// ------------------------------------------------------------------------------------------------- BackendCapabilityError
BackendCapabilityError::BackendCapabilityError(UnsupportedCapabilityDescription description, 
                                               std::source_location location)
    : Error(description.backend, description.operation,
            std::format("capability '{}' is not supported.\n  Reason: {}{}", description.capability, description.reason,
                        suggestion_suffix(description.suggestion)),
            location)
    , m_capability{std::move(description.capability)}
    , m_reason{std::move(description.reason)}
    , m_suggestion{std::move(description.suggestion)} 
{}

auto 
BackendCapabilityError::capability() const noexcept -> std::string_view 
{
    return m_capability;
}

auto 
BackendCapabilityError::reason() const noexcept -> std::string_view 
{
    return m_reason;
}

auto 
BackendCapabilityError::suggestion() const noexcept -> std::string_view 
{
    return m_suggestion;
}

// ------------------------------------------------------------------------------------------------- InvalidUsageError
InvalidUsageError::InvalidUsageError(InvalidUsageDescription description, 
                                     std::source_location location)
    : Error(description.backend, description.operation,
            std::format("{}{}", description.problem, suggestion_suffix(description.suggestion)), location)
    , m_problem{std::move(description.problem)}
    , m_suggestion{std::move(description.suggestion)} 
{}

auto
InvalidUsageError::problem() const noexcept -> std::string_view 
{
    return m_problem;
}

auto 
InvalidUsageError::suggestion() const noexcept -> std::string_view 
{
    return m_suggestion;
}

// ------------------------------------------------------------------------------------------------- InvalidHandleError
InvalidHandleError::InvalidHandleError(InvalidHandleDescription description, 
                                       std::source_location location)
    : Error(description.backend, description.operation,
            std::format("invalid {} handle: {}", description.handle_type, description.problem), location)
    , m_handle_type{std::move(description.handle_type)}
    , m_problem{std::move(description.problem)} 
{}

auto 
InvalidHandleError::handle_type() const noexcept -> std::string_view 
{
    return m_handle_type;
}

auto 
InvalidHandleError::problem() const noexcept -> std::string_view 
{
    return m_problem;
}

// ------------------------------------------------------------------------------------------------- OutOfMemoryError
OutOfMemoryError::OutOfMemoryError(OutOfMemoryDescription description, 
                                   std::source_location location)
    : Error(description.backend, description.operation,
            std::format("out of {} memory{}{}", description.domain == MemoryDomain::HOST ? "host" : "device",
                        description.requested_bytes == 0 ? std::string{}
                                                         : std::format(" while allocating {} bytes", description.requested_bytes),
                        description.details.empty() ? std::string{} : std::format(".\n  Details: {}", description.details)),
            location)
    , m_domain{description.domain}
    , m_requested_bytes{description.requested_bytes} 
{}

auto 
OutOfMemoryError::domain() const noexcept -> MemoryDomain 
{
    return m_domain;
}

auto 
OutOfMemoryError::requested_bytes() const noexcept -> std::uint64_t 
{
    return m_requested_bytes;
}

// ------------------------------------------------------------------------------------------------- DeviceLostError
DeviceLostError::DeviceLostError(DeviceLostDescription description, 
                                 std::source_location location)
    : Error(description.backend, description.operation,
            std::format("the device was lost; every object created from it is unusable{}",
                        description.details.empty() ? std::string{} : std::format(".\n  Details: {}", description.details)),
            location) 
{}

// ------------------------------------------------------------------------------------------------- BackendApiError
BackendApiError::BackendApiError(BackendApiFailureDescription description, 
                                 std::source_location location)
    : Error(description.backend, description.operation,
            std::format("{} failed with {} ({})", description.api_function, description.result_name, description.result_code),
            location)
    , m_api_function{std::move(description.api_function)}
    , m_result_code{description.result_code}
    , m_result_name{std::move(description.result_name)} 
{}

auto 
BackendApiError::api_function() const noexcept -> std::string_view 
{
    return m_api_function;
}

auto 
BackendApiError::result_code() const noexcept -> std::int64_t 
{
    return m_result_code;
}

auto 
BackendApiError::result_name() const noexcept -> std::string_view 
{
    return m_result_name;
}

// ------------------------------------------------------------------------------------------------- ShaderCompilationError
ShaderCompilationError::ShaderCompilationError(ShaderCompilationFailureDescription description, 
                                               std::source_location location)
    : Error(std::nullopt, description.operation,
            std::format("failed to compile {} shader '{}':\n{}", description.stage, description.shader_name,
                        indent(description.compiler_log, "    ")),
            location)
    , m_shader_name{std::move(description.shader_name)}
    , m_stage{description.stage}
    , m_compiler_log{std::move(description.compiler_log)} 
{}

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
                        description.problem, suggestion_suffix(description.suggestion)),
            location)
    , m_shader_name{std::move(description.shader_name)}
    , m_mismatch{description.mismatch}
    , m_problem{std::move(description.problem)}
{}

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
