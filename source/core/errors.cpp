module gfx.core.errors;

import std;
import gfx.core.enums;

namespace gfx
{

namespace
{

auto
compose_message(const std::optional<BackendIdentity>& backend,
                std::string_view operation,
                std::string_view details,
                const std::source_location& location) -> std::string
{
    // Format the origin as "[gfx::VULKAN]", "[gfx::platform::SDL3]" or "[gfx]".
    const std::string origin = backend ? std::format("[{}]", *backend)
                                       : std::string{"[gfx]"};

    // Format the message as "[origin] operation: details\n  Location: file:line (function)".
    return std::format("{} {}: {}\n  Location: {}:{} ({})", origin, operation, details, location.file_name(),
                       location.line(), location.function_name());
}

} // namespace

// ------------------------------------------------------------------------------------------------- Error
Error::Error(std::optional<BackendIdentity> backend,
             std::string operation,
             std::string_view details,
             std::source_location location)
    : std::runtime_error(compose_message(backend, operation, details, location))
    , m_backend{backend}
    , m_operation{std::move(operation)}
    , m_location{location}
{
}

auto
Error::backend() const noexcept -> std::optional<BackendIdentity>
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

auto
Error::suggestion_line(std::string_view suggestion) -> std::string
{
    return suggestion.empty() ? std::string{}
                              : std::format("\n  Suggestion: {}", suggestion);
}

auto
Error::indented(std::string_view text,
                std::string_view prefix) -> std::string
{
    std::string result;

    // Indent every line of the text with the prefix.
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
    return result;
}

// ------------------------------------------------------------------------------------------------- BackendCapabilityError
BackendCapabilityError::BackendCapabilityError(UnsupportedCapabilityDescription description,
                                               std::source_location location)
    : Error(description.backend, description.operation,
            std::format("capability '{}' is not supported.\n  Reason: {}{}", description.capability, description.reason,
                        suggestion_line(description.suggestion)),
            location)
    , m_capability{std::move(description.capability)}
    , m_reason{std::move(description.reason)}
    , m_suggestion{std::move(description.suggestion)}
{
}

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
            std::format("{}{}", description.problem, suggestion_line(description.suggestion)), location)
    , m_problem{std::move(description.problem)}
    , m_suggestion{std::move(description.suggestion)}
{
}

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
{
}

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
{
}

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

// ------------------------------------------------------------------------------------------------- BackendApiError
BackendApiError::BackendApiError(BackendApiFailureDescription description,
                                 std::source_location location)
    : Error(description.backend, description.operation,
            std::format("{} failed with {} ({})", description.api_function, description.result_name, description.result_code),
            location)
    , m_api_function{std::move(description.api_function)}
    , m_result_code{description.result_code}
    , m_result_name{std::move(description.result_name)}
{
}

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

} // namespace gfx
