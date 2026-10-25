/// @file reference.cppm
/// @brief The reference backend: a SteppedWorld whose backend is made only of the abstraction's components — a broad
///        phase (SpatialBroadPhaseComponent<Bvh> by default), a narrow phase (GjkNarrowPhase), a solver
///        (SequentialImpulseSolver) and an integrator of the bodies' velocities (SemiImplicitEulerIntegrator), each
///        replaceable between steps (PhysicsWorld::replace_broad_phase() ...). It simulates everything the interface
///        describes: every shape on every kind of body, separate static and dynamic friction, rolling resistance,
///        restitution, every joint kind (six-DOF ones too), breakable joints, user constraints, triggers (ENTER, STAY,
///        EXIT), contact events, islands that fall asleep, speculative contacts (continuous bodies stretch them by their
///        motion), and the abstraction's soft bodies through the framework. It is the template for custom backends:
///        its pipeline is that of §A of the design, each phase a component call. Private to the library:
///        create_physics_world(description, PhysicsBackendKind::REFERENCE).
export module gfx.physics.backends.reference;

import std;
import gfx.physics.interface;

export namespace gfx::physics::reference_backend
{

/// @brief Why the backend cannot be used (empty: it always can).
[[nodiscard]] auto unavailable_reason() -> std::string;

/// @brief A world of the reference backend.
/// @throws PhysicsError if @p description is invalid.
[[nodiscard]] auto create_physics_world(const PhysicsWorldDescription& description) -> std::unique_ptr<PhysicsWorld>;

} // namespace gfx::physics::reference_backend
