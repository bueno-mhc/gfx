/// @file platform_object.cppm
/// @brief Common base class of every object a platform backend implements.
///
/// As in the graphics library, every backend operation is a pure virtual function with an out-of-class
/// definition: a backend must override every operation, and when it cannot honor one its override
/// throws its own BackendCapabilityError (with the precise reason) or forwards to the interface's
/// definition, which throws a generic "not implemented" error. Nothing unsupported is ignored silently.
export module gfx.platform.interface:platform_object;

import std;
import gfx.core;
import gfx.platform.enums;

export namespace gfx::platform
{

/// @brief Base class of the objects a platform backend implements: the platform, windows, cursors and
///        the input devices.
///
/// Objects are owned through std::unique_ptr and are neither copyable nor movable: their address is
/// stable for their whole lifetime. Their destructor releases the native objects (windows possibly at
/// the next event pump: see Platform). Every object knows the validation level of its platform.
class PlatformObject : public HandleIssuer
{
public:
    virtual ~PlatformObject() = default;
    PlatformObject(const PlatformObject&) = delete;
    PlatformObject(PlatformObject&&) = delete;
    auto operator=(const PlatformObject&) -> PlatformObject& = delete;
    auto operator=(PlatformObject&&) -> PlatformObject& = delete;

    /// @brief Backend that implements this object.
    [[nodiscard]] virtual auto backend() const noexcept -> PlatformBackendKind = 0;

    /// @brief Checks the library performs for this object (the level of its platform).
    [[nodiscard]] auto validation_level() const noexcept -> ValidationLevel
    {
        return m_validation_level;
    }

    /// @brief Whether checks of level @p level run for this object.
    [[nodiscard]] auto validates(ValidationLevel level) const noexcept -> bool
    {
        return m_validation_level >= level;
    }

protected:
    PlatformObject() = default;

    /// @brief Backends pass their platform's validation level to every object they create.
    explicit PlatformObject(ValidationLevel validation_level) noexcept
        : m_validation_level{validation_level}
    {
    }

    /// @brief Sets the validation level (for backends that learn it after construction).
    void set_validation_level(ValidationLevel validation_level) noexcept
    {
        m_validation_level = validation_level;
    }

    /// @brief Throws a BackendCapabilityError describing why this backend cannot perform @p operation.
    /// @param operation  Operation of the interface, e.g. "Window::set_icon".
    /// @param capability Missing capability, e.g. "window icons".
    /// @param reason     Why the backend cannot provide it.
    /// @param suggestion What the caller can do instead.
    /// @param location   Where the error is raised (defaults to the caller).
    [[noreturn]] void throw_unsupported(std::string_view operation,
                                        std::string_view capability,
                                        std::string reason,
                                        std::string suggestion,
                                        std::source_location location = std::source_location::current()) const;

    /// @brief Throws the generic error of an operation this backend does not implement.
    /// @param operation Operation of the interface, e.g. "Window::set_icon".
    /// @param location  Where the error is raised (defaults to the caller).
    [[noreturn]] void throw_not_implemented(std::string_view operation,
                                            std::source_location location = std::source_location::current()) const;

private:
    ValidationLevel m_validation_level = DEFAULT_VALIDATION_LEVEL;
};

void
PlatformObject::throw_unsupported(std::string_view operation,
                                  std::string_view capability,
                                  std::string reason,
                                  std::string suggestion,
                                  std::source_location location) const
{
    throw BackendCapabilityError(
        {
            .backend = backend(),
            .operation = std::string{operation},
            .capability = std::string{capability},
            .reason = std::move(reason),
            .suggestion = std::move(suggestion),
        },
        location);
}

void
PlatformObject::throw_not_implemented(std::string_view operation,
                                      std::source_location location) const
{
    // The capability is the operation itself: the backend provides no implementation of it.
    throw_unsupported(operation,
                      operation,
                      std::format("the {} backend does not implement this operation yet", backend()),
                      std::format("implement {} in the {} backend, or use a backend that provides it", operation, backend()),
                      location);
}

} // namespace gfx::platform
