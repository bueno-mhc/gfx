/// @file errors.cppm
/// @brief Errors of the physics and camera libraries. They join the gfx hierarchy (gfx::Error, a std::runtime_error):
///        - PhysicsError: an operation that cannot be honored (an invalid description, a destroyed body, a world in
///          the wrong state), naming the world, the bodies, the collider, the joint, the operation, the backend or
///          subsystem, the problem and a suggestion;
///        - gfx::BackendCapabilityError (core): a backend lacks a capability (throw_unsupported(), naming the
///          PhysicsCapability and the same objects), so one catch covers every gfx library;
///        - QueryFailure: why a query (ray cast, shape cast, overlap) has no result, returned in std::expected
///          (QueryResult) without throwing nor allocating; raise() throws it as one of the above.
/// @code
/// [gfx::physics::BOX3D] PhysicsWorld::create_joint: capability 'SIX_DEGREES_OF_FREEDOM_JOINTS' is not supported.
///   Reason: Box3D has no generic six-degree-of-freedom joint.
///   World: World(index 0, generation 1)
///   Body: RigidBody(index 4, generation 1)
///   Suggestion: use a CONE_TWIST, HINGE or SLIDER joint, or a backend with six-DOF joints.
///   Location: source/physics/backends/box3d/box3d_joints.cpp:88 (create_joint)
/// @endcode
export module gfx.physics.errors;

import std;
export import gfx.core.errors;
import gfx.physics.enums;
import gfx.physics.handles;

export namespace gfx::physics
{

/// @brief The objects an error concerns (each optional): printed as lines of its message.
struct PhysicsErrorContext
{
    std::optional<WorldHandle> world{};          ///< The world.
    std::optional<RigidBodyHandle> rigid_body{}; ///< The rigid body.
    std::optional<SoftBodyHandle> soft_body{};   ///< The soft body.
    std::optional<ColliderHandle> collider{};    ///< The collider.
    std::optional<ShapeHandle> shape{};          ///< The shape.
    std::optional<JointHandle> joint{};          ///< The joint.
    std::optional<ConstraintHandle> constraint{}; ///< The user constraint.
    std::optional<TriggerHandle> trigger{};      ///< The trigger.
};

/// @brief Describes an operation of the physics or camera libraries that cannot be honored.
struct PhysicsErrorDescription
{
    std::optional<BackendIdentity> backend{}; ///< The backend or subsystem (PhysicsBackendKind, PhysicsSubsystem, CameraBackendKind).
    std::string operation{};                  ///< The operation, e.g. "RigidBody::set_mass_properties".
    std::string problem{};                    ///< What is wrong.
    std::string suggestion{};                 ///< What to do instead; may be empty.
    PhysicsErrorContext context{};            ///< The objects concerned.
};

/// @brief Thrown when an operation of the physics or camera libraries cannot be honored.
class PhysicsError : public Error
{
public:
    /// @param description What failed, why, and what to do instead.
    /// @param location    Where the error was raised (defaults to the caller).
    explicit PhysicsError(PhysicsErrorDescription description,
                          std::source_location location = std::source_location::current());

    /// @brief What is wrong.
    [[nodiscard]] auto problem() const noexcept -> std::string_view;

    /// @brief What to do instead; may be empty.
    [[nodiscard]] auto suggestion() const noexcept -> std::string_view;

    /// @brief The objects the error concerns.
    [[nodiscard]] auto context() const noexcept -> const PhysicsErrorContext&;

private:
    std::string m_problem;
    std::string m_suggestion;
    PhysicsErrorContext m_context;
};

/// @brief The context's lines, as messages print them ("\n  World: World(index 0, generation 1)..."), or nothing.
[[nodiscard]] auto context_lines(const PhysicsErrorContext& context) -> std::string;

/// @brief Throws gfx::BackendCapabilityError: @p backend cannot do @p capability for @p operation. The reason is
///        followed by the context's lines.
[[noreturn]] void throw_unsupported(BackendIdentity backend,
                                    std::string_view operation,
                                    PhysicsCapability capability,
                                    std::string_view reason,
                                    std::string_view suggestion,
                                    const PhysicsErrorContext& context = {},
                                    std::source_location location = std::source_location::current());

/// @brief Why a query has no result: a small value (no allocation; its texts are static) returned in
///        std::expected by queries, which never throw.
struct QueryFailure
{
    QueryFailureKind kind = QueryFailureKind::NO_HIT;        ///< Why.
    std::string_view operation{};                            ///< The query, e.g. "PhysicsWorld::ray_cast" (static text).
    std::string_view detail{};                               ///< More about the reason (static text); may be empty.
    std::optional<BackendIdentity> backend{};                ///< The backend or subsystem that answered.
    std::optional<PhysicsCapability> capability{};           ///< What the backend lacks (UNSUPPORTED).
    PhysicsErrorContext context{};                           ///< The objects concerned.

    /// @brief The failure as a message (allocates).
    [[nodiscard]] auto message() const -> std::string;

    /// @brief Throws the failure: BackendCapabilityError when UNSUPPORTED with a backend and a capability,
    ///        PhysicsError otherwise.
    [[noreturn]] void raise(std::source_location location = std::source_location::current()) const;
};

/// @brief The result of a query: @p Value, or why there is none.
template<class Value>
using QueryResult = std::expected<Value, QueryFailure>;

} // namespace gfx::physics
