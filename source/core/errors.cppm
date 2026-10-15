/// @file errors.cppm
/// @brief Exceptions thrown by the gfx libraries.
///
/// Every exception derives from gfx::Error (itself a std::runtime_error); the graphics and the platform
/// libraries share this hierarchy, so one `catch (const gfx::BackendCapabilityError&)` covers both.
/// Messages are multi-line and always name the backend (when one is involved), the operation, the
/// problem, a suggestion when one exists, and the source location that raised it:
/// @code
/// [gfx::METAL] Device::create_event: capability 'Event (split barrier)' is not supported.
///   Reason: Metal has no GPU-side split-barrier object.
///   Suggestion: use Recorder::barrier() within a queue.
///   Location: source/graphics/backends/metal/device.cpp:212 (create_event)
/// @endcode
export module gfx.core.errors;

import std;
import gfx.core.enums;

export namespace gfx
{

/// @brief The backend an error comes from, in any gfx library: the library's name and the backend's
///        enumerator. Converts implicitly from any backend enumeration (gfx::BackendKind,
///        gfx::platform::PlatformBackendKind) and compares equal to the enumerator it was made from.
class BackendIdentity
{
public:
    /// @brief The backend @p backend of the library its enumeration belongs to.
    template<BackendEnumeration Backend>
    constexpr BackendIdentity(Backend backend) noexcept // NOLINT(google-explicit-constructor): implicit by design
        : m_library{EnumerationTraits<Backend>::BACKEND_LIBRARY}
        , m_name{to_string(backend)}
    {
    }

    /// @brief Name of the library, e.g. "gfx" or "gfx::platform".
    [[nodiscard]] constexpr auto library() const noexcept -> std::string_view
    {
        return m_library;
    }

    /// @brief Name of the backend, e.g. "VULKAN" or "SDL3".
    [[nodiscard]] constexpr auto name() const noexcept -> std::string_view
    {
        return m_name;
    }

    /// @brief Same library and backend.
    [[nodiscard]] constexpr auto operator==(const BackendIdentity&) const noexcept -> bool = default;

    /// @brief Whether this is the backend @p backend.
    template<BackendEnumeration Backend>
    [[nodiscard]] constexpr auto operator==(Backend backend) const noexcept -> bool
    {
        return *this == BackendIdentity{backend};
    }

private:
    std::string_view m_library;
    std::string_view m_name;
};

/// @brief Base class of every exception thrown by the gfx libraries.
class Error : public std::runtime_error
{
public:
    /// @brief Backend that raised the error, or std::nullopt for backend-independent errors.
    [[nodiscard]] auto backend() const noexcept -> std::optional<BackendIdentity>;

    /// @brief Operation that failed, e.g. "Device::create_buffer".
    [[nodiscard]] auto operation() const noexcept -> std::string_view;

    /// @brief Source location that raised the error.
    [[nodiscard]] auto location() const noexcept -> const std::source_location&;

protected:
    /// @param backend   Backend involved, if any.
    /// @param operation Operation that failed.
    /// @param details   Description of the problem; may span several lines.
    /// @param location  Where the error was raised.
    Error(std::optional<BackendIdentity> backend,
          std::string operation,
          std::string_view details,
          std::source_location location);

    /// @brief "\n  Suggestion: <suggestion>", or nothing when @p suggestion is empty.
    [[nodiscard]] static auto suggestion_line(std::string_view suggestion) -> std::string;

    /// @brief @p text with @p prefix in front of every line.
    [[nodiscard]] static auto indented(std::string_view text,
                                       std::string_view prefix) -> std::string;

private:
    std::optional<BackendIdentity> m_backend;
    std::string m_operation;
    std::source_location m_location;
};




/// @brief Describes an operation a backend cannot perform.
struct UnsupportedCapabilityDescription
{
    BackendIdentity backend; ///< Backend that lacks the capability.
    std::string operation;   ///< Operation that was requested, e.g. "Device::create_event".
    std::string capability;  ///< Missing capability, e.g. "Event (split barrier)".
    std::string reason;      ///< Why the backend cannot provide it.
    std::string suggestion;  ///< What the caller can do instead.
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
    std::optional<BackendIdentity> backend; ///< Backend involved, if any.
    std::string operation;                  ///< Operation that was called.
    std::string problem;                    ///< What is wrong.
    std::string suggestion;                 ///< How to fix it; may be empty.
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
    std::optional<BackendIdentity> backend; ///< Backend involved, if any.
    std::string operation;                  ///< Operation that received the handle.
    std::string handle_type;                ///< Kind of handle, e.g. "Buffer".
    std::string problem;                    ///< Why it is invalid (null, destroyed, out of range, ...).
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
    std::optional<BackendIdentity> backend; ///< Backend involved, if any.
    std::string operation;                  ///< Operation that allocated.
    MemoryDomain domain;                    ///< Exhausted memory.
    std::uint64_t requested_bytes;          ///< Size of the failed allocation (0 when unknown).
    std::string details;                    ///< Additional information; may be empty.
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




/// @brief Describes an unexpected failure reported by a native API (graphics API, windowing library).
struct BackendApiFailureDescription
{
    BackendIdentity backend;  ///< Backend that called the API.
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

} // namespace gfx

/// @brief Formats a backend as "<library>::<backend>", e.g. "gfx::VULKAN".
template<>
struct std::formatter<gfx::BackendIdentity, char> : std::formatter<std::string_view, char>
{
    template<class FormatContext>
    auto format(const gfx::BackendIdentity& backend, FormatContext& context) const -> FormatContext::iterator
    {
        const std::string text = std::format("{}::{}", backend.library(), backend.name());
        return std::formatter<std::string_view, char>::format(text, context);
    }
};
