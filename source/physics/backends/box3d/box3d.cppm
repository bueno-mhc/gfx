/// @file box3d.cppm
/// @brief The Box3D backend (Erin Catto's 3D engine, MIT): a SteppedWorld whose backend applies the edits to a Box3D world
///        and steps it — rigid bodies, colliders (compounds as several shapes on the body; cylinders and cones as hulls of
///        ColliderDescription::round_shape_segments sides; meshes and height fields on static bodies), triggers (sensors,
///        STAY made by the backend), joints (distance, revolute, spherical, prismatic, weld; breakable through Box3D's
///        joint events), events, Box3D's own queries, and Box3D's task hooks forwarded to the world's TaskScheduler.
///        What Box3D cannot do throws BackendCapabilityError when it is recorded (six-DOF joints, different static and
///        dynamic friction, rounded or very large hulls, meshes on moving bodies, posed height fields, soft bodies until
///        the abstraction's XPBD arrives). Box3D's own queries (QueryEngine::BACKEND) skip triggers, ignore the back sides
///        of one-sided meshes and do not report a ray that starts inside a shape (the abstraction's report it at distance
///        0); casts of cylinders, cones and meshes, and closest points, are the abstraction's. Solver and sleep settings
///        Box3D has no counterpart for (iterations, static friction speed, angular sleep threshold, sleep time,
///        CollisionSettings) are hints it ignores. Private to the library:
///        create_physics_world(description, PhysicsBackendKind::BOX3D).
export module gfx.physics.backends.box3d;

import std;
import gfx.physics.interface;

export namespace gfx::physics::box3d_backend
{

/// @brief Why the backend cannot be used (empty: it can).
[[nodiscard]] auto unavailable_reason() -> std::string;

/// @brief A world of Box3D.
/// @throws PhysicsError if @p description is invalid.
/// @throws BackendCapabilityError if Box3D already holds as many worlds as it can.
[[nodiscard]] auto create_physics_world(const PhysicsWorldDescription& description) -> std::unique_ptr<PhysicsWorld>;

} // namespace gfx::physics::box3d_backend
