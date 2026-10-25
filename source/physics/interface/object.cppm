/// @file object.cppm
/// @brief PhysicsObject: the base of every object of a physics world (the world itself, its bodies, colliders, joints,
///        triggers and pipeline components). It names its backend in errors and may issue handles (HandleIssuer).
///
/// Every operation of the interfaces that can fail is pure virtual with a definition that throws a descriptive
/// gfx::BackendCapabilityError: a backend overrides what it supports and may forward the rest to the base class, whose
/// error names the operation, the capability and how to get it. Accessors that cannot fail (noexcept) are plain pure
/// virtual functions.
export module gfx.physics.interface:object;

import std;
import gfx.core.errors;
import gfx.core.handles;
import gfx.physics.enums;
import gfx.physics.errors;

export namespace gfx::physics
{

/// @brief The base of the objects of a physics world.
class PhysicsObject : public HandleIssuer
{
public:
    virtual ~PhysicsObject() = default;

    PhysicsObject(const PhysicsObject&) = delete;
    auto operator=(const PhysicsObject&) -> PhysicsObject& = delete;

    /// @brief The backend (or subsystem) that implements the object: errors name it ("[gfx::physics::BOX3D] ...").
    [[nodiscard]] auto backend() const noexcept -> BackendIdentity
    {
        return m_backend;
    }

protected:
    /// @param backend The backend or subsystem implementing the object.
    explicit PhysicsObject(BackendIdentity backend) noexcept
        : m_backend{backend}
    {
    }

    /// @brief Throws gfx::BackendCapabilityError: the object's backend cannot do @p capability for @p operation.
    [[noreturn]] void throw_unsupported(std::string_view operation,
                                        PhysicsCapability capability,
                                        std::string_view reason,
                                        std::string_view suggestion,
                                        const PhysicsErrorContext& context = {},
                                        std::source_location location = std::source_location::current()) const;

    /// @brief Throws the gfx::BackendCapabilityError of an operation the backend does not implement (what the default
    ///        definitions of the interfaces throw).
    [[noreturn]] void throw_not_implemented(std::string_view operation,
                                            PhysicsCapability capability,
                                            const PhysicsErrorContext& context = {},
                                            std::source_location location = std::source_location::current()) const;

private:
    BackendIdentity m_backend;
};

} // namespace gfx::physics
