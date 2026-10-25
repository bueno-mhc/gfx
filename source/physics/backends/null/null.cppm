/// @file null.cppm
/// @brief The null backend: a world that has nothing and does nothing — every operation that would make or change
///        something throws gfx::BackendCapabilityError (PhysicsCapability::WORLD_OBJECTS), queries fail with
///        QueryFailureKind::UNSUPPORTED, getters report an empty world. It shows the error contract a backend keeps for
///        what it lacks. Private to the library: create_physics_world(description, PhysicsBackendKind::NULL_BACKEND).
export module gfx.physics.backends.null;

import std;
import gfx.physics.interface;

export namespace gfx::physics::null_backend
{

/// @brief Why the backend cannot be used (empty: it can).
[[nodiscard]] auto unavailable_reason() -> std::string;

/// @brief A world of the null backend (it keeps @p description, which description() returns).
[[nodiscard]] auto create_physics_world(const PhysicsWorldDescription& description) -> std::unique_ptr<PhysicsWorld>;

} // namespace gfx::physics::null_backend
