/// @file backend_object.cppm
/// @brief Common base class of every object a backend implements.
///
/// Every backend operation of the interfaces is a pure virtual function **with** an out-of-class
/// definition. A backend must override every operation (its class stays abstract otherwise); when it
/// cannot honor one, its override either throws its own BackendCapabilityError (with a precise reason)
/// or forwards to the interface's definition, which throws a generic "not implemented" error.
export module gfx.interface:backend_object;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;

export namespace gfx
{

/// @brief Base class of every object a backend implements: devices, resources, pipelines, ...
///
/// Objects are owned through std::unique_ptr and are neither copyable nor movable: their address is
/// stable for their whole lifetime, and their destructor releases the native objects (possibly later,
/// through the device's Deleter, once the GPU no longer uses them).
///
/// Every object knows the validation level of its instance (stored, not virtual, so a check costs one
/// comparison): the library's own checks of creations, recordings and commands consult it.
class BackendObject : public HandleIssuer
{
public:
    virtual ~BackendObject() = default;

    BackendObject(const BackendObject&) = delete;
    BackendObject(BackendObject&&) = delete;
    auto operator=(const BackendObject&) -> BackendObject& = delete;
    auto operator=(BackendObject&&) -> BackendObject& = delete;

    /// @brief Backend that implements this object.
    [[nodiscard]] virtual auto backend() const noexcept -> BackendKind = 0;

    /// @brief Name given at creation (the `debug_name` of its description), shown by debugging tools.
    [[nodiscard]] virtual auto debug_name() const noexcept -> std::string_view = 0;

    /// @brief Renames the object, in messages and in debugging tools (RenderDoc, PIX, validation layers).
    virtual void set_debug_name(std::string_view name) = 0;

    /// @brief Checks the library performs for this object (the level of its instance).
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
    BackendObject() = default;

    /// @brief Backends pass their instance's validation level to every object they create.
    explicit BackendObject(ValidationLevel validation_level) noexcept
        : m_validation_level{validation_level}
    {}

    /// @brief Sets the validation level (for backends that learn it after construction).
    void set_validation_level(ValidationLevel validation_level) noexcept
    {
        m_validation_level = validation_level;
    }

    /// @brief Throws a BackendCapabilityError describing why this backend cannot perform @p operation.
    /// @param operation  Operation of the interface, e.g. "Device::create_event".
    /// @param capability Missing capability, e.g. "Event (split barrier)".
    /// @param reason     Why the backend cannot provide it.
    /// @param suggestion What the caller can do instead.
    /// @param location   Where the error is raised (defaults to the caller).
    [[noreturn]] void throw_unsupported(std::string_view operation,
                                        std::string_view capability,
                                        std::string reason,
                                        std::string suggestion,
                                        std::source_location location = std::source_location::current()) const;

    /// @brief Throws the generic error of an operation this backend does not implement.
    /// @param operation Operation of the interface, e.g. "Device::create_event".
    /// @param location  Where the error is raised (defaults to the caller).
    [[noreturn]] void throw_not_implemented(std::string_view operation,
                                            std::source_location location = std::source_location::current()) const;

private:
    ValidationLevel m_validation_level = DEFAULT_VALIDATION_LEVEL;
};

void
BackendObject::throw_unsupported(std::string_view operation,
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
BackendObject::throw_not_implemented(std::string_view operation,
                                     std::source_location location) const
{
    // The capability is the operation itself: the backend provides no implementation of it.
    throw_unsupported(operation,
                      operation,
                      std::format("the {} backend does not implement this operation yet", backend()),
                      std::format("implement {} in the {} backend, or use a backend that provides it", operation, backend()),
                      location);
}

} // namespace gfx
