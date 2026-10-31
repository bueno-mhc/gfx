// The physics interface's definitions: the errors of PhysicsObject, world handles, and the definitions of every
// operation a backend does not override — a BackendCapabilityError naming the operation and the capability, or, for
// queries, a QueryFailure of kind UNSUPPORTED.
module gfx.physics.interface;

import std;
import gfx.core.errors;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.handles;
import gfx.physics.primitives;
import gfx.physics.shapes;
import gfx.physics.narrow_phase;
import gfx.physics.spatial;
import gfx.physics.tasks;

namespace gfx::physics
{

// ===================================================================================================== PhysicsObject

void
PhysicsObject::throw_unsupported(std::string_view operation,
                                 PhysicsCapability capability,
                                 std::string_view reason,
                                 std::string_view suggestion,
                                 const PhysicsErrorContext& context,
                                 std::source_location location) const
{
    gfx::physics::throw_unsupported(m_backend, operation, capability, reason, suggestion, context, location);
}

void
PhysicsObject::throw_not_implemented(std::string_view operation,
                                     PhysicsCapability capability,
                                     const PhysicsErrorContext& context,
                                     std::source_location location) const
{
    gfx::physics::throw_unsupported(m_backend, operation, capability, std::format("this backend does not implement {}", operation),
                                    std::format("override {} in the backend, or use a backend that has it (PhysicsBackendKind::BOX3D or REFERENCE)", operation),
                                    context, location);
}

// ===================================================================================================== PhysicsWorld helpers

auto
PhysicsWorld::issue_world_handle() -> WorldHandle
{
    // Worlds are numbered in the order they are made; the index wraps around past the largest a handle holds.
    static std::atomic<std::uint32_t> next_world{0};
    const std::uint32_t number = next_world.fetch_add(1, std::memory_order_relaxed);
    return WorldHandle::create(handle_key(), number % (MAXIMUM_HANDLE_INDEX + 1), 1);
}

auto
PhysicsWorld::unsupported_query(std::string_view operation) const noexcept -> QueryFailure
{
    return {.kind = QueryFailureKind::UNSUPPORTED,
            .operation = operation,
            .detail = "this backend does not answer queries",
            .backend = backend(),
            .capability = PhysicsCapability::WORLD_OBJECTS,
            .context = {.world = handle()}};
}

// ===================================================================================================== Collider

auto
Collider::description() const -> ColliderDescription
{
    throw_not_implemented("Collider::description", PhysicsCapability::WORLD_OBJECTS);
}

auto
Collider::world_bounds() const -> Aabb
{
    throw_not_implemented("Collider::world_bounds", PhysicsCapability::WORLD_OBJECTS);
}

void
Collider::set_pose(const Transform& /*pose*/)
{
    throw_not_implemented("Collider::set_pose", PhysicsCapability::WORLD_OBJECTS);
}

void
Collider::set_material(const SurfaceMaterial& /*material*/)
{
    throw_not_implemented("Collider::set_material", PhysicsCapability::WORLD_OBJECTS);
}

void
Collider::set_density(float /*density*/)
{
    throw_not_implemented("Collider::set_density", PhysicsCapability::WORLD_OBJECTS);
}

void
Collider::set_filter(const CollisionFilter& /*filter*/)
{
    throw_not_implemented("Collider::set_filter", PhysicsCapability::WORLD_OBJECTS);
}

void
Collider::set_contact_events(bool /*enabled*/)
{
    throw_not_implemented("Collider::set_contact_events", PhysicsCapability::WORLD_OBJECTS);
}

// ===================================================================================================== RigidBody

auto
RigidBody::description() const -> RigidBodyDescription
{
    throw_not_implemented("RigidBody::description", PhysicsCapability::WORLD_OBJECTS);
}

auto
RigidBody::state() const -> RigidBodyState
{
    throw_not_implemented("RigidBody::state", PhysicsCapability::WORLD_OBJECTS);
}

auto
RigidBody::mass_properties() const -> MassProperties
{
    throw_not_implemented("RigidBody::mass_properties", PhysicsCapability::WORLD_OBJECTS);
}

auto
RigidBody::add_collider(const ColliderDescription& /*description*/) -> std::unique_ptr<Collider>
{
    throw_not_implemented("RigidBody::add_collider", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::set_pose(const Transform& /*pose*/)
{
    throw_not_implemented("RigidBody::set_pose", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::set_linear_velocity(Vector3 /*velocity*/)
{
    throw_not_implemented("RigidBody::set_linear_velocity", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::set_angular_velocity(Vector3 /*velocity*/)
{
    throw_not_implemented("RigidBody::set_angular_velocity", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::move_kinematic(const Transform& /*target*/,
                          float /*seconds*/)
{
    throw_not_implemented("RigidBody::move_kinematic", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::apply_force(Vector3 /*force*/)
{
    throw_not_implemented("RigidBody::apply_force", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::apply_force_at(Vector3 /*force*/,
                          Vector3 /*point*/)
{
    throw_not_implemented("RigidBody::apply_force_at", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::apply_torque(Vector3 /*torque*/)
{
    throw_not_implemented("RigidBody::apply_torque", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::apply_linear_impulse(Vector3 /*impulse*/)
{
    throw_not_implemented("RigidBody::apply_linear_impulse", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::apply_linear_impulse_at(Vector3 /*impulse*/,
                                   Vector3 /*point*/)
{
    throw_not_implemented("RigidBody::apply_linear_impulse_at", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::apply_angular_impulse(Vector3 /*impulse*/)
{
    throw_not_implemented("RigidBody::apply_angular_impulse", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::set_kind(BodyKind /*kind*/)
{
    throw_not_implemented("RigidBody::set_kind", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::set_mass_properties(const std::optional<MassProperties>& /*properties*/)
{
    throw_not_implemented("RigidBody::set_mass_properties", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::set_damping(float /*linear*/,
                       float /*angular*/)
{
    throw_not_implemented("RigidBody::set_damping", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::set_gravity_scale(float /*scale*/)
{
    throw_not_implemented("RigidBody::set_gravity_scale", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::set_motion_quality(MotionQuality /*quality*/)
{
    throw_not_implemented("RigidBody::set_motion_quality", PhysicsCapability::CONTINUOUS_COLLISION);
}

void
RigidBody::set_motion_locks(MotionLock /*locks*/)
{
    throw_not_implemented("RigidBody::set_motion_locks", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::set_sleep_allowed(bool /*allowed*/)
{
    throw_not_implemented("RigidBody::set_sleep_allowed", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::wake_up()
{
    throw_not_implemented("RigidBody::wake_up", PhysicsCapability::WORLD_OBJECTS);
}

void
RigidBody::put_to_sleep()
{
    throw_not_implemented("RigidBody::put_to_sleep", PhysicsCapability::WORLD_OBJECTS);
}

// ===================================================================================================== Joint

auto
Joint::description() const -> JointDescription
{
    throw_not_implemented("Joint::description", PhysicsCapability::WORLD_OBJECTS);
}

auto
Joint::state() const -> JointState
{
    throw_not_implemented("Joint::state", PhysicsCapability::WORLD_OBJECTS);
}

void
Joint::update(const JointDescription& /*description*/)
{
    throw_not_implemented("Joint::update", PhysicsCapability::WORLD_OBJECTS);
}

void
Joint::set_enabled(bool /*enabled*/)
{
    throw_not_implemented("Joint::set_enabled", PhysicsCapability::WORLD_OBJECTS);
}

// ===================================================================================================== Constraint

auto
Constraint::description() const -> ConstraintDescription
{
    throw_not_implemented("Constraint::description", PhysicsCapability::USER_CONSTRAINTS);
}

auto
Constraint::state() const -> JointState
{
    throw_not_implemented("Constraint::state", PhysicsCapability::USER_CONSTRAINTS);
}

void
Constraint::set_enabled(bool /*enabled*/)
{
    throw_not_implemented("Constraint::set_enabled", PhysicsCapability::USER_CONSTRAINTS);
}

// ===================================================================================================== Trigger

auto
Trigger::description() const -> TriggerDescription
{
    throw_not_implemented("Trigger::description", PhysicsCapability::WORLD_OBJECTS);
}

auto
Trigger::world_bounds() const -> Aabb
{
    throw_not_implemented("Trigger::world_bounds", PhysicsCapability::WORLD_OBJECTS);
}

void
Trigger::set_pose(const Transform& /*pose*/)
{
    throw_not_implemented("Trigger::set_pose", PhysicsCapability::WORLD_OBJECTS);
}

void
Trigger::set_filter(const CollisionFilter& /*filter*/)
{
    throw_not_implemented("Trigger::set_filter", PhysicsCapability::WORLD_OBJECTS);
}

// ===================================================================================================== SoftBody

auto
SoftBody::description() const -> SoftBodyDescription
{
    throw_not_implemented("SoftBody::description", PhysicsCapability::SOFT_BODIES);
}

auto
SoftBody::positions(std::span<Vector3> /*output*/) const -> std::span<Vector3>
{
    throw_not_implemented("SoftBody::positions", PhysicsCapability::SOFT_BODIES);
}

auto
SoftBody::velocities(std::span<Vector3> /*output*/) const -> std::span<Vector3>
{
    throw_not_implemented("SoftBody::velocities", PhysicsCapability::SOFT_BODIES);
}

auto
SoftBody::bounds() const -> Aabb
{
    throw_not_implemented("SoftBody::bounds", PhysicsCapability::SOFT_BODIES);
}

void
SoftBody::pin(const ParticlePin& /*pin*/)
{
    throw_not_implemented("SoftBody::pin", PhysicsCapability::SOFT_BODIES);
}

void
SoftBody::unpin(std::uint32_t /*particle*/)
{
    throw_not_implemented("SoftBody::unpin", PhysicsCapability::SOFT_BODIES);
}

void
SoftBody::move_particle(std::uint32_t /*particle*/,
                        Vector3 /*position*/)
{
    throw_not_implemented("SoftBody::move_particle", PhysicsCapability::SOFT_BODIES);
}

void
SoftBody::set_damping(float /*damping*/)
{
    throw_not_implemented("SoftBody::set_damping", PhysicsCapability::SOFT_BODIES);
}

void
SoftBody::set_gravity_scale(float /*scale*/)
{
    throw_not_implemented("SoftBody::set_gravity_scale", PhysicsCapability::SOFT_BODIES);
}

void
SoftBody::set_self_collision(bool /*enabled*/)
{
    throw_not_implemented("SoftBody::set_self_collision", PhysicsCapability::SOFT_BODIES);
}

void
SoftBody::set_filter(const CollisionFilter& /*filter*/)
{
    throw_not_implemented("SoftBody::set_filter", PhysicsCapability::SOFT_BODIES);
}

// ===================================================================================================== BroadPhase

auto
BroadPhase::insert(ColliderHandle /*collider*/,
                   const Aabb& /*box*/,
                   const CollisionFilter& /*filter*/) -> SpatialProxyHandle
{
    throw_not_implemented("BroadPhase::insert", PhysicsCapability::CUSTOM_BROAD_PHASE);
}

void
BroadPhase::remove(SpatialProxyHandle /*proxy*/)
{
    throw_not_implemented("BroadPhase::remove", PhysicsCapability::CUSTOM_BROAD_PHASE);
}

auto
BroadPhase::move(SpatialProxyHandle /*proxy*/,
                 const Aabb& /*box*/,
                 Vector3 /*displacement*/) -> bool
{
    throw_not_implemented("BroadPhase::move", PhysicsCapability::CUSTOM_BROAD_PHASE);
}

void
BroadPhase::set_filter(SpatialProxyHandle /*proxy*/,
                       const CollisionFilter& /*filter*/)
{
    throw_not_implemented("BroadPhase::set_filter", PhysicsCapability::CUSTOM_BROAD_PHASE);
}

void
BroadPhase::update(TaskScheduler* /*scheduler*/)
{
    throw_not_implemented("BroadPhase::update", PhysicsCapability::CUSTOM_BROAD_PHASE);
}

auto
BroadPhase::pairs() const -> std::span<const BroadPhasePair>
{
    throw_not_implemented("BroadPhase::pairs", PhysicsCapability::CUSTOM_BROAD_PHASE);
}

auto
BroadPhase::new_pairs() const -> std::span<const BroadPhasePair>
{
    throw_not_implemented("BroadPhase::new_pairs", PhysicsCapability::CUSTOM_BROAD_PHASE);
}

auto
BroadPhase::ended_pairs() const -> std::span<const BroadPhasePair>
{
    throw_not_implemented("BroadPhase::ended_pairs", PhysicsCapability::CUSTOM_BROAD_PHASE);
}

void
BroadPhase::structure_nodes(std::vector<SpatialNode>& /*nodes*/) const
{
    throw_not_implemented("BroadPhase::structure_nodes", PhysicsCapability::CUSTOM_BROAD_PHASE);
}

// ===================================================================================================== NarrowPhase

void
NarrowPhase::collide(const ShapeInstance& /*first*/,
                     const ShapeInstance& /*second*/,
                     ContactCollector& /*contacts*/,
                     const CollisionSettings& /*settings*/) const
{
    throw_not_implemented("NarrowPhase::collide", PhysicsCapability::CUSTOM_NARROW_PHASE);
}

// ===================================================================================================== Solver

void
Solver::solve(const SolverStep& /*step*/)
{
    throw_not_implemented("Solver::solve", PhysicsCapability::CUSTOM_SOLVER);
}

// ===================================================================================================== Integrator

void
Integrator::predict(const IntegrationState& /*state*/,
                    float /*seconds*/)
{
    throw_not_implemented("Integrator::predict", PhysicsCapability::CUSTOM_INTEGRATOR);
}

void
Integrator::finalize(const IntegrationState& /*state*/,
                     float /*seconds*/)
{
    throw_not_implemented("Integrator::finalize", PhysicsCapability::CUSTOM_INTEGRATOR);
}

// ===================================================================================================== PhysicsWorld

auto
PhysicsWorld::description() const -> PhysicsWorldDescription
{
    throw_not_implemented("PhysicsWorld::description", PhysicsCapability::WORLD_OBJECTS);
}

auto
PhysicsWorld::create_rigid_body(const RigidBodyDescription& /*description*/) -> std::unique_ptr<RigidBody>
{
    throw_not_implemented("PhysicsWorld::create_rigid_body", PhysicsCapability::WORLD_OBJECTS);
}

auto
PhysicsWorld::create_soft_body(const SoftBodyDescription& /*description*/) -> std::unique_ptr<SoftBody>
{
    throw_not_implemented("PhysicsWorld::create_soft_body", PhysicsCapability::SOFT_BODIES);
}

auto
PhysicsWorld::create_joint(const JointDescription& /*description*/) -> std::unique_ptr<Joint>
{
    throw_not_implemented("PhysicsWorld::create_joint", PhysicsCapability::WORLD_OBJECTS);
}

auto
PhysicsWorld::create_constraint(const ConstraintDescription& /*description*/) -> std::unique_ptr<Constraint>
{
    throw_not_implemented("PhysicsWorld::create_constraint", PhysicsCapability::USER_CONSTRAINTS);
}

auto
PhysicsWorld::create_trigger(const TriggerDescription& /*description*/) -> std::unique_ptr<Trigger>
{
    throw_not_implemented("PhysicsWorld::create_trigger", PhysicsCapability::WORLD_OBJECTS);
}

void
PhysicsWorld::set_gravity(Vector3 /*gravity*/)
{
    throw_not_implemented("PhysicsWorld::set_gravity", PhysicsCapability::WORLD_OBJECTS);
}

void
PhysicsWorld::set_solver_description(const SolverDescription& /*description*/)
{
    throw_not_implemented("PhysicsWorld::set_solver_description", PhysicsCapability::WORLD_OBJECTS);
}

void
PhysicsWorld::set_sleep_description(const SleepDescription& /*description*/)
{
    throw_not_implemented("PhysicsWorld::set_sleep_description", PhysicsCapability::WORLD_OBJECTS);
}

void
PhysicsWorld::step(float /*seconds*/)
{
    throw_not_implemented("PhysicsWorld::step", PhysicsCapability::WORLD_OBJECTS);
}

void
PhysicsWorld::begin_step(float /*seconds*/)
{
    throw_not_implemented("PhysicsWorld::begin_step", PhysicsCapability::WORLD_OBJECTS);
}

void
PhysicsWorld::wait_step()
{
    throw_not_implemented("PhysicsWorld::wait_step", PhysicsCapability::WORLD_OBJECTS);
}

auto
PhysicsWorld::events() const -> PhysicsEvents
{
    throw_not_implemented("PhysicsWorld::events", PhysicsCapability::WORLD_OBJECTS);
}

auto
PhysicsWorld::statistics() const -> StepStatistics
{
    throw_not_implemented("PhysicsWorld::statistics", PhysicsCapability::WORLD_OBJECTS);
}

auto
PhysicsWorld::ray_cast(const Ray& /*ray*/,
                       float /*maximum*/,
                       const QueryOptions& /*options*/) const noexcept -> QueryResult<RayHit>
{
    return std::unexpected(unsupported_query("PhysicsWorld::ray_cast"));
}

auto
PhysicsWorld::ray_cast_all(const Ray& /*ray*/,
                           float /*maximum*/,
                           std::span<RayHit> /*output*/,
                           const QueryOptions& /*options*/) const noexcept -> QueryResult<std::span<RayHit>>
{
    return std::unexpected(unsupported_query("PhysicsWorld::ray_cast_all"));
}

auto
PhysicsWorld::shape_cast(const Shape& /*shape*/,
                         const Transform& /*pose*/,
                         Vector3 /*translation*/,
                         const QueryOptions& /*options*/) const noexcept -> QueryResult<ShapeCastHit>
{
    return std::unexpected(unsupported_query("PhysicsWorld::shape_cast"));
}

auto
PhysicsWorld::overlap(const Shape& /*shape*/,
                      const Transform& /*pose*/,
                      std::span<ColliderHandle> /*output*/,
                      const QueryOptions& /*options*/) const noexcept -> QueryResult<std::span<ColliderHandle>>
{
    return std::unexpected(unsupported_query("PhysicsWorld::overlap"));
}

auto
PhysicsWorld::overlap(const Aabb& /*box*/,
                      std::span<ColliderHandle> /*output*/,
                      const QueryOptions& /*options*/) const noexcept -> QueryResult<std::span<ColliderHandle>>
{
    return std::unexpected(unsupported_query("PhysicsWorld::overlap"));
}

auto
PhysicsWorld::closest_point(Vector3 /*point*/,
                            float /*maximum*/,
                            const QueryOptions& /*options*/) const noexcept -> QueryResult<ClosestPointHit>
{
    return std::unexpected(unsupported_query("PhysicsWorld::closest_point"));
}

auto
PhysicsWorld::contacts_of(RigidBodyHandle /*body*/,
                          std::span<ContactInfo> /*output*/) const noexcept -> QueryResult<std::span<ContactInfo>>
{
    return std::unexpected(unsupported_query("PhysicsWorld::contacts_of"));
}

auto
PhysicsWorld::broad_phase() -> BroadPhase&
{
    throw_not_implemented("PhysicsWorld::broad_phase", PhysicsCapability::WORLD_OBJECTS);
}

auto
PhysicsWorld::narrow_phase() -> NarrowPhase&
{
    throw_not_implemented("PhysicsWorld::narrow_phase", PhysicsCapability::WORLD_OBJECTS);
}

auto
PhysicsWorld::solver() -> Solver&
{
    throw_not_implemented("PhysicsWorld::solver", PhysicsCapability::WORLD_OBJECTS);
}

auto
PhysicsWorld::integrator() -> Integrator&
{
    throw_not_implemented("PhysicsWorld::integrator", PhysicsCapability::WORLD_OBJECTS);
}

void
PhysicsWorld::replace_broad_phase(std::unique_ptr<BroadPhase> /*broad_phase*/)
{
    throw_not_implemented("PhysicsWorld::replace_broad_phase", PhysicsCapability::CUSTOM_BROAD_PHASE);
}

void
PhysicsWorld::replace_narrow_phase(std::unique_ptr<NarrowPhase> /*narrow_phase*/)
{
    throw_not_implemented("PhysicsWorld::replace_narrow_phase", PhysicsCapability::CUSTOM_NARROW_PHASE);
}

void
PhysicsWorld::replace_solver(std::unique_ptr<Solver> /*solver*/)
{
    throw_not_implemented("PhysicsWorld::replace_solver", PhysicsCapability::CUSTOM_SOLVER);
}

void
PhysicsWorld::replace_integrator(std::unique_ptr<Integrator> /*integrator*/)
{
    throw_not_implemented("PhysicsWorld::replace_integrator", PhysicsCapability::CUSTOM_INTEGRATOR);
}

auto
PhysicsWorld::soft_body_integrator() -> Integrator&
{
    throw_not_implemented("PhysicsWorld::soft_body_integrator", PhysicsCapability::SOFT_BODIES);
}

void
PhysicsWorld::replace_soft_body_integrator(std::unique_ptr<Integrator> /*integrator*/)
{
    throw_not_implemented("PhysicsWorld::replace_soft_body_integrator", PhysicsCapability::SOFT_BODIES);
}

} // namespace gfx::physics
