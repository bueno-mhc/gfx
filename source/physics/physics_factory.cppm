/// @file physics_factory.cppm
/// @brief `import gfx.physics.factory;`: the only public entry to the physics backends. Applications choose a backend by
///        its PhysicsBackendKind; the backend modules are private to the library. PhysicsBackendKind::CUSTOM is the
///        backend of the CMake option GFX_PHYSICS_CUSTOM_BACKEND_TARGET (a target with a module
///        gfx.physics.backends.custom exporting gfx::physics::custom_backend::unavailable_reason() and
///        create_physics_world(description)); a custom backend may also be constructed directly, without the factory.
export module gfx.physics.factory;

import std;
import gfx.physics.interface;

export namespace gfx::physics
{

/// @brief Whether a backend can be used: built into the library, implemented, and able to run here.
struct PhysicsBackendAvailability
{
    PhysicsBackendKind backend = PhysicsBackendKind::NULL_BACKEND; ///< The backend.
    bool built = false;                                            ///< Compiled into the library.
    bool available = false;                                        ///< create_physics_world() can succeed.
    std::string reason{};                                          ///< Why it is unavailable (empty when available).
};

/// @brief Whether @p backend can be used, and why not.
[[nodiscard]] auto physics_backend_availability(PhysicsBackendKind backend) -> PhysicsBackendAvailability;

/// @brief The backends create_physics_world() can create, simulating backends first (the null backend last).
[[nodiscard]] auto available_physics_backends() -> std::vector<PhysicsBackendKind>;

/// @brief A world of @p backend.
/// @throws BackendCapabilityError if the backend was not built or is not available (the message says why and what to do).
/// @throws PhysicsError if @p description is invalid.
[[nodiscard]] auto create_physics_world(const PhysicsWorldDescription& description,
                                        PhysicsBackendKind backend) -> std::unique_ptr<PhysicsWorld>;

} // namespace gfx::physics
