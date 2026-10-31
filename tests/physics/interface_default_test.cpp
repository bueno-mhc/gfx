// The interfaces' default definitions: a class overriding every operation by forwarding to its interface gets, for each
// one, a BackendCapabilityError naming the operation and the capability (and, for queries, a QueryFailure of kind
// UNSUPPORTED) — the error contract every backend inherits for what it does not implement. Generated from the list of
// the interfaces' operations (scratchpad physics_interface_defaults.py, the same list as interface.cpp's definitions).
#include <gtest/gtest.h>

import std;
import gfx.physics.interface;

namespace
{

using namespace gfx;
using namespace gfx::physics;

class BareCollider final : public Collider
{
public:
    BareCollider()
        : Collider{PhysicsBackendKind::CUSTOM}
    {
    }

    [[nodiscard]] auto handle() const noexcept -> ColliderHandle override
    {
        return {};
    }

    [[nodiscard]] auto body() const noexcept -> RigidBodyHandle override
    {
        return {};
    }

    [[nodiscard]] auto shape() const noexcept -> const std::shared_ptr<const Shape>& override
    {
        return m_shape;
    }

    [[nodiscard]] auto description() const -> ColliderDescription override
    {
        return Collider::description();
    }

    [[nodiscard]] auto world_bounds() const -> Aabb override
    {
        return Collider::world_bounds();
    }

    void set_pose(const Transform& pose) override
    {
        Collider::set_pose(pose);
    }

    void set_material(const SurfaceMaterial& material) override
    {
        Collider::set_material(material);
    }

    void set_density(float density) override
    {
        Collider::set_density(density);
    }

    void set_filter(const CollisionFilter& filter) override
    {
        Collider::set_filter(filter);
    }

    void set_contact_events(bool enabled) override
    {
        Collider::set_contact_events(enabled);
    }

private:
    std::shared_ptr<const Shape> m_shape{};
};

class BareRigidBody final : public RigidBody
{
public:
    BareRigidBody()
        : RigidBody{PhysicsBackendKind::CUSTOM}
    {
    }

    [[nodiscard]] auto handle() const noexcept -> RigidBodyHandle override
    {
        return {};
    }

    [[nodiscard]] auto description() const -> RigidBodyDescription override
    {
        return RigidBody::description();
    }

    [[nodiscard]] auto state() const -> RigidBodyState override
    {
        return RigidBody::state();
    }

    [[nodiscard]] auto mass_properties() const -> MassProperties override
    {
        return RigidBody::mass_properties();
    }

    [[nodiscard]] auto add_collider(const ColliderDescription& description) -> std::unique_ptr<Collider> override
    {
        return RigidBody::add_collider(description);
    }

    void set_pose(const Transform& pose) override
    {
        RigidBody::set_pose(pose);
    }

    void set_linear_velocity(Vector3 velocity) override
    {
        RigidBody::set_linear_velocity(velocity);
    }

    void set_angular_velocity(Vector3 velocity) override
    {
        RigidBody::set_angular_velocity(velocity);
    }

    void move_kinematic(const Transform& target,
                        float seconds) override
    {
        RigidBody::move_kinematic(target, seconds);
    }

    void apply_force(Vector3 force) override
    {
        RigidBody::apply_force(force);
    }

    void apply_force_at(Vector3 force,
                        Vector3 point) override
    {
        RigidBody::apply_force_at(force, point);
    }

    void apply_torque(Vector3 torque) override
    {
        RigidBody::apply_torque(torque);
    }

    void apply_linear_impulse(Vector3 impulse) override
    {
        RigidBody::apply_linear_impulse(impulse);
    }

    void apply_linear_impulse_at(Vector3 impulse,
                                 Vector3 point) override
    {
        RigidBody::apply_linear_impulse_at(impulse, point);
    }

    void apply_angular_impulse(Vector3 impulse) override
    {
        RigidBody::apply_angular_impulse(impulse);
    }

    void set_kind(BodyKind kind) override
    {
        RigidBody::set_kind(kind);
    }

    void set_mass_properties(const std::optional<MassProperties>& properties) override
    {
        RigidBody::set_mass_properties(properties);
    }

    void set_damping(float linear,
                     float angular) override
    {
        RigidBody::set_damping(linear, angular);
    }

    void set_gravity_scale(float scale) override
    {
        RigidBody::set_gravity_scale(scale);
    }

    void set_motion_quality(MotionQuality quality) override
    {
        RigidBody::set_motion_quality(quality);
    }

    void set_motion_locks(MotionLock locks) override
    {
        RigidBody::set_motion_locks(locks);
    }

    void set_sleep_allowed(bool allowed) override
    {
        RigidBody::set_sleep_allowed(allowed);
    }

    void wake_up() override
    {
        RigidBody::wake_up();
    }

    void put_to_sleep() override
    {
        RigidBody::put_to_sleep();
    }
};

class BareJoint final : public Joint
{
public:
    BareJoint()
        : Joint{PhysicsBackendKind::CUSTOM}
    {
    }

    [[nodiscard]] auto handle() const noexcept -> JointHandle override
    {
        return {};
    }

    [[nodiscard]] auto kind() const noexcept -> JointKind override
    {
        return JointKind::HINGE;
    }

    [[nodiscard]] auto bodies() const noexcept -> std::pair<RigidBodyHandle, RigidBodyHandle> override
    {
        return {};
    }

    [[nodiscard]] auto description() const -> JointDescription override
    {
        return Joint::description();
    }

    [[nodiscard]] auto state() const -> JointState override
    {
        return Joint::state();
    }

    void update(const JointDescription& description) override
    {
        Joint::update(description);
    }

    void set_enabled(bool enabled) override
    {
        Joint::set_enabled(enabled);
    }
};

class BareConstraint final : public Constraint
{
public:
    BareConstraint()
        : Constraint{PhysicsBackendKind::CUSTOM}
    {
    }

    [[nodiscard]] auto handle() const noexcept -> ConstraintHandle override
    {
        return {};
    }

    [[nodiscard]] auto bodies() const noexcept -> std::pair<RigidBodyHandle, RigidBodyHandle> override
    {
        return {};
    }

    [[nodiscard]] auto description() const -> ConstraintDescription override
    {
        return Constraint::description();
    }

    [[nodiscard]] auto state() const -> JointState override
    {
        return Constraint::state();
    }

    void set_enabled(bool enabled) override
    {
        Constraint::set_enabled(enabled);
    }
};

class BareTrigger final : public Trigger
{
public:
    BareTrigger()
        : Trigger{PhysicsBackendKind::CUSTOM}
    {
    }

    [[nodiscard]] auto handle() const noexcept -> TriggerHandle override
    {
        return {};
    }

    [[nodiscard]] auto shape() const noexcept -> const std::shared_ptr<const Shape>& override
    {
        return m_shape;
    }

    [[nodiscard]] auto description() const -> TriggerDescription override
    {
        return Trigger::description();
    }

    [[nodiscard]] auto world_bounds() const -> Aabb override
    {
        return Trigger::world_bounds();
    }

    void set_pose(const Transform& pose) override
    {
        Trigger::set_pose(pose);
    }

    void set_filter(const CollisionFilter& filter) override
    {
        Trigger::set_filter(filter);
    }

private:
    std::shared_ptr<const Shape> m_shape{};
};

class BareSoftBody final : public SoftBody
{
public:
    BareSoftBody()
        : SoftBody{PhysicsBackendKind::CUSTOM}
    {
    }

    [[nodiscard]] auto handle() const noexcept -> SoftBodyHandle override
    {
        return {};
    }

    [[nodiscard]] auto particle_count() const noexcept -> std::uint32_t override
    {
        return 0;
    }

    [[nodiscard]] auto description() const -> SoftBodyDescription override
    {
        return SoftBody::description();
    }

    [[nodiscard]] auto positions(std::span<Vector3> output) const -> std::span<Vector3> override
    {
        return SoftBody::positions(output);
    }

    [[nodiscard]] auto velocities(std::span<Vector3> output) const -> std::span<Vector3> override
    {
        return SoftBody::velocities(output);
    }

    [[nodiscard]] auto bounds() const -> Aabb override
    {
        return SoftBody::bounds();
    }

    void pin(const ParticlePin& pin) override
    {
        SoftBody::pin(pin);
    }

    void unpin(std::uint32_t particle) override
    {
        SoftBody::unpin(particle);
    }

    void move_particle(std::uint32_t particle,
                       Vector3 position) override
    {
        SoftBody::move_particle(particle, position);
    }

    void set_damping(float damping) override
    {
        SoftBody::set_damping(damping);
    }

    void set_gravity_scale(float scale) override
    {
        SoftBody::set_gravity_scale(scale);
    }

    void set_self_collision(bool enabled) override
    {
        SoftBody::set_self_collision(enabled);
    }

    void set_filter(const CollisionFilter& filter) override
    {
        SoftBody::set_filter(filter);
    }
};

class BareBroadPhase final : public BroadPhase
{
public:
    BareBroadPhase()
        : BroadPhase{PhysicsBackendKind::CUSTOM}
    {
    }

    [[nodiscard]] auto kind() const noexcept -> BroadPhaseKind override
    {
        return BroadPhaseKind::BVH;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return true;
    }

    [[nodiscard]] auto insert(ColliderHandle collider,
                const Aabb& box,
                const CollisionFilter& filter) -> SpatialProxyHandle override
    {
        return BroadPhase::insert(collider, box, filter);
    }

    void remove(SpatialProxyHandle proxy) override
    {
        BroadPhase::remove(proxy);
    }

    [[nodiscard]] auto move(SpatialProxyHandle proxy,
              const Aabb& box,
              Vector3 displacement) -> bool override
    {
        return BroadPhase::move(proxy, box, displacement);
    }

    void set_filter(SpatialProxyHandle proxy,
                    const CollisionFilter& filter) override
    {
        BroadPhase::set_filter(proxy, filter);
    }

    void update(TaskScheduler* scheduler) override
    {
        BroadPhase::update(scheduler);
    }

    [[nodiscard]] auto pairs() const -> std::span<const BroadPhasePair> override
    {
        return BroadPhase::pairs();
    }

    [[nodiscard]] auto new_pairs() const -> std::span<const BroadPhasePair> override
    {
        return BroadPhase::new_pairs();
    }

    [[nodiscard]] auto ended_pairs() const -> std::span<const BroadPhasePair> override
    {
        return BroadPhase::ended_pairs();
    }

    void structure_nodes(std::vector<SpatialNode>& nodes) const override
    {
        BroadPhase::structure_nodes(nodes);
    }
};

class BareNarrowPhase final : public NarrowPhase
{
public:
    BareNarrowPhase()
        : NarrowPhase{PhysicsBackendKind::CUSTOM}
    {
    }

    [[nodiscard]] auto kind() const noexcept -> NarrowPhaseKind override
    {
        return NarrowPhaseKind::GJK_EPA;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return true;
    }

    void collide(const ShapeInstance& first,
                 const ShapeInstance& second,
                 ContactCollector& contacts,
                 const CollisionSettings& settings) const override
    {
        NarrowPhase::collide(first, second, contacts, settings);
    }
};

class BareSolver final : public Solver
{
public:
    BareSolver()
        : Solver{PhysicsBackendKind::CUSTOM}
    {
    }

    [[nodiscard]] auto kind() const noexcept -> SolverKind override
    {
        return SolverKind::SEQUENTIAL_IMPULSE;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return true;
    }

    void solve(const SolverStep& step) override
    {
        Solver::solve(step);
    }
};

class BareIntegrator final : public Integrator
{
public:
    BareIntegrator()
        : Integrator{PhysicsBackendKind::CUSTOM}
    {
    }

    [[nodiscard]] auto kind() const noexcept -> IntegratorKind override
    {
        return IntegratorKind::VERLET;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return true;
    }

    [[nodiscard]] auto requirements() const noexcept -> IntegratorRequirements override
    {
        return {};
    }

    void predict(const IntegrationState& state,
                 float seconds) override
    {
        Integrator::predict(state, seconds);
    }

    void finalize(const IntegrationState& state,
                  float seconds) override
    {
        Integrator::finalize(state, seconds);
    }
};

class BarePhysicsWorld final : public PhysicsWorld
{
public:
    BarePhysicsWorld()
        : PhysicsWorld{PhysicsBackendKind::CUSTOM}
    {
    }

    [[nodiscard]] auto handle() const noexcept -> WorldHandle override
    {
        return {};
    }

    [[nodiscard]] auto is_stepping() const noexcept -> bool override
    {
        return false;
    }

    [[nodiscard]] auto step_count() const noexcept -> std::uint64_t override
    {
        return 0;
    }

    [[nodiscard]] auto description() const -> PhysicsWorldDescription override
    {
        return PhysicsWorld::description();
    }

    [[nodiscard]] auto create_rigid_body(const RigidBodyDescription& description) -> std::unique_ptr<RigidBody> override
    {
        return PhysicsWorld::create_rigid_body(description);
    }

    [[nodiscard]] auto create_soft_body(const SoftBodyDescription& description) -> std::unique_ptr<SoftBody> override
    {
        return PhysicsWorld::create_soft_body(description);
    }

    [[nodiscard]] auto create_joint(const JointDescription& description) -> std::unique_ptr<Joint> override
    {
        return PhysicsWorld::create_joint(description);
    }

    [[nodiscard]] auto create_constraint(const ConstraintDescription& description) -> std::unique_ptr<Constraint> override
    {
        return PhysicsWorld::create_constraint(description);
    }

    [[nodiscard]] auto create_trigger(const TriggerDescription& description) -> std::unique_ptr<Trigger> override
    {
        return PhysicsWorld::create_trigger(description);
    }

    void set_gravity(Vector3 gravity) override
    {
        PhysicsWorld::set_gravity(gravity);
    }

    void set_solver_description(const SolverDescription& description) override
    {
        PhysicsWorld::set_solver_description(description);
    }

    void set_sleep_description(const SleepDescription& description) override
    {
        PhysicsWorld::set_sleep_description(description);
    }

    void step(float seconds) override
    {
        PhysicsWorld::step(seconds);
    }

    void begin_step(float seconds) override
    {
        PhysicsWorld::begin_step(seconds);
    }

    void wait_step() override
    {
        PhysicsWorld::wait_step();
    }

    [[nodiscard]] auto events() const -> PhysicsEvents override
    {
        return PhysicsWorld::events();
    }

    [[nodiscard]] auto statistics() const -> StepStatistics override
    {
        return PhysicsWorld::statistics();
    }

    [[nodiscard]] auto broad_phase() -> BroadPhase& override
    {
        return PhysicsWorld::broad_phase();
    }

    [[nodiscard]] auto narrow_phase() -> NarrowPhase& override
    {
        return PhysicsWorld::narrow_phase();
    }

    [[nodiscard]] auto solver() -> Solver& override
    {
        return PhysicsWorld::solver();
    }

    [[nodiscard]] auto integrator() -> Integrator& override
    {
        return PhysicsWorld::integrator();
    }

    void replace_broad_phase(std::unique_ptr<BroadPhase> broad_phase) override
    {
        PhysicsWorld::replace_broad_phase(std::move(broad_phase));
    }

    void replace_narrow_phase(std::unique_ptr<NarrowPhase> narrow_phase) override
    {
        PhysicsWorld::replace_narrow_phase(std::move(narrow_phase));
    }

    void replace_solver(std::unique_ptr<Solver> solver) override
    {
        PhysicsWorld::replace_solver(std::move(solver));
    }

    void replace_integrator(std::unique_ptr<Integrator> integrator) override
    {
        PhysicsWorld::replace_integrator(std::move(integrator));
    }

    [[nodiscard]] auto soft_body_integrator() -> Integrator& override
    {
        return PhysicsWorld::soft_body_integrator();
    }

    void replace_soft_body_integrator(std::unique_ptr<Integrator> integrator) override
    {
        PhysicsWorld::replace_soft_body_integrator(std::move(integrator));
    }

    [[nodiscard]] auto ray_cast(const Ray& ray,
                                float maximum,
                                const QueryOptions& options) const noexcept -> QueryResult<RayHit> override
    {
        return PhysicsWorld::ray_cast(ray, maximum, options);
    }

    [[nodiscard]] auto ray_cast_all(const Ray& ray,
                                    float maximum,
                                    std::span<RayHit> output,
                                    const QueryOptions& options) const noexcept -> QueryResult<std::span<RayHit>> override
    {
        return PhysicsWorld::ray_cast_all(ray, maximum, output, options);
    }

    [[nodiscard]] auto shape_cast(const Shape& shape,
                                  const Transform& pose,
                                  Vector3 translation,
                                  const QueryOptions& options) const noexcept -> QueryResult<ShapeCastHit> override
    {
        return PhysicsWorld::shape_cast(shape, pose, translation, options);
    }

    [[nodiscard]] auto overlap(const Shape& shape,
                               const Transform& pose,
                               std::span<ColliderHandle> output,
                               const QueryOptions& options) const noexcept -> QueryResult<std::span<ColliderHandle>> override
    {
        return PhysicsWorld::overlap(shape, pose, output, options);
    }

    [[nodiscard]] auto overlap(const Aabb& box,
                               std::span<ColliderHandle> output,
                               const QueryOptions& options) const noexcept -> QueryResult<std::span<ColliderHandle>> override
    {
        return PhysicsWorld::overlap(box, output, options);
    }

    [[nodiscard]] auto closest_point(Vector3 point,
                                     float maximum,
                                     const QueryOptions& options) const noexcept -> QueryResult<ClosestPointHit> override
    {
        return PhysicsWorld::closest_point(point, maximum, options);
    }

    [[nodiscard]] auto contacts_of(RigidBodyHandle body,
                                   std::span<ContactInfo> output) const noexcept -> QueryResult<std::span<ContactInfo>> override
    {
        return PhysicsWorld::contacts_of(body, output);
    }
};


/// @p operation throws the BackendCapabilityError of @p name lacking @p capability.
template<class Operation>
void
expect_not_implemented(Operation&& operation,
                       std::string_view name,
                       std::string_view capability)
{
    try
    {
        operation();
        ADD_FAILURE() << name << " did not throw";
    }
    catch (const BackendCapabilityError& error)
    {
        EXPECT_EQ(error.capability(), capability) << name;
        EXPECT_EQ(error.operation(), name);
        EXPECT_NE(std::string_view{error.what()}.find("[gfx::physics::CUSTOM]"), std::string_view::npos) << error.what();
        EXPECT_NE(std::string_view{error.what()}.find("does not implement"), std::string_view::npos) << error.what();
    }
}

/// @p result is the UNSUPPORTED failure of @p name, which raise() throws as a BackendCapabilityError.
template<class Result>
void
expect_unsupported_query(const Result& result,
                         std::string_view name)
{
    ASSERT_FALSE(result.has_value()) << name;
    EXPECT_EQ(result.error().kind, QueryFailureKind::UNSUPPORTED) << name;
    EXPECT_EQ(result.error().operation, name);
    EXPECT_EQ(result.error().capability, PhysicsCapability::WORLD_OBJECTS) << name;
    EXPECT_THROW(result.error().raise(), BackendCapabilityError) << name;
}

TEST(PhysicsInterfaceTest, EveryOperationABackendDoesNotImplementThrowsADescriptiveError)
{
    ContactCollector contacts;
    BareCollider bare_collider;
    expect_not_implemented([&] { (void)bare_collider.description(); }, "Collider::description", "WORLD_OBJECTS");
    expect_not_implemented([&] { (void)bare_collider.world_bounds(); }, "Collider::world_bounds", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_collider.set_pose(Transform{}); }, "Collider::set_pose", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_collider.set_material(SurfaceMaterial{}); }, "Collider::set_material", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_collider.set_density(float{}); }, "Collider::set_density", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_collider.set_filter(CollisionFilter{}); }, "Collider::set_filter", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_collider.set_contact_events(bool{}); }, "Collider::set_contact_events", "WORLD_OBJECTS");
    BareRigidBody bare_rigid_body;
    expect_not_implemented([&] { (void)bare_rigid_body.description(); }, "RigidBody::description", "WORLD_OBJECTS");
    expect_not_implemented([&] { (void)bare_rigid_body.state(); }, "RigidBody::state", "WORLD_OBJECTS");
    expect_not_implemented([&] { (void)bare_rigid_body.mass_properties(); }, "RigidBody::mass_properties", "WORLD_OBJECTS");
    expect_not_implemented([&] { (void)bare_rigid_body.add_collider(ColliderDescription{}); }, "RigidBody::add_collider", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.set_pose(Transform{}); }, "RigidBody::set_pose", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.set_linear_velocity(Vector3{}); }, "RigidBody::set_linear_velocity", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.set_angular_velocity(Vector3{}); }, "RigidBody::set_angular_velocity", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.move_kinematic(Transform{}, float{}); }, "RigidBody::move_kinematic", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.apply_force(Vector3{}); }, "RigidBody::apply_force", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.apply_force_at(Vector3{}, Vector3{}); }, "RigidBody::apply_force_at", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.apply_torque(Vector3{}); }, "RigidBody::apply_torque", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.apply_linear_impulse(Vector3{}); }, "RigidBody::apply_linear_impulse", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.apply_linear_impulse_at(Vector3{}, Vector3{}); }, "RigidBody::apply_linear_impulse_at", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.apply_angular_impulse(Vector3{}); }, "RigidBody::apply_angular_impulse", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.set_kind(BodyKind{}); }, "RigidBody::set_kind", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.set_mass_properties(std::optional<MassProperties>{}); }, "RigidBody::set_mass_properties", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.set_damping(float{}, float{}); }, "RigidBody::set_damping", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.set_gravity_scale(float{}); }, "RigidBody::set_gravity_scale", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.set_motion_quality(MotionQuality{}); }, "RigidBody::set_motion_quality", "CONTINUOUS_COLLISION");
    expect_not_implemented([&] { bare_rigid_body.set_motion_locks(MotionLock{}); }, "RigidBody::set_motion_locks", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.set_sleep_allowed(bool{}); }, "RigidBody::set_sleep_allowed", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.wake_up(); }, "RigidBody::wake_up", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_rigid_body.put_to_sleep(); }, "RigidBody::put_to_sleep", "WORLD_OBJECTS");
    BareJoint bare_joint;
    expect_not_implemented([&] { (void)bare_joint.description(); }, "Joint::description", "WORLD_OBJECTS");
    expect_not_implemented([&] { (void)bare_joint.state(); }, "Joint::state", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_joint.update(JointDescription{}); }, "Joint::update", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_joint.set_enabled(bool{}); }, "Joint::set_enabled", "WORLD_OBJECTS");
    BareConstraint bare_constraint;
    expect_not_implemented([&] { (void)bare_constraint.description(); }, "Constraint::description", "USER_CONSTRAINTS");
    expect_not_implemented([&] { (void)bare_constraint.state(); }, "Constraint::state", "USER_CONSTRAINTS");
    expect_not_implemented([&] { bare_constraint.set_enabled(bool{}); }, "Constraint::set_enabled", "USER_CONSTRAINTS");
    BareTrigger bare_trigger;
    expect_not_implemented([&] { (void)bare_trigger.description(); }, "Trigger::description", "WORLD_OBJECTS");
    expect_not_implemented([&] { (void)bare_trigger.world_bounds(); }, "Trigger::world_bounds", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_trigger.set_pose(Transform{}); }, "Trigger::set_pose", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_trigger.set_filter(CollisionFilter{}); }, "Trigger::set_filter", "WORLD_OBJECTS");
    BareSoftBody bare_soft_body;
    expect_not_implemented([&] { (void)bare_soft_body.description(); }, "SoftBody::description", "SOFT_BODIES");
    expect_not_implemented([&] { (void)bare_soft_body.positions(std::span<Vector3>{}); }, "SoftBody::positions", "SOFT_BODIES");
    expect_not_implemented([&] { (void)bare_soft_body.velocities(std::span<Vector3>{}); }, "SoftBody::velocities", "SOFT_BODIES");
    expect_not_implemented([&] { (void)bare_soft_body.bounds(); }, "SoftBody::bounds", "SOFT_BODIES");
    expect_not_implemented([&] { bare_soft_body.pin(ParticlePin{}); }, "SoftBody::pin", "SOFT_BODIES");
    expect_not_implemented([&] { bare_soft_body.unpin(std::uint32_t{}); }, "SoftBody::unpin", "SOFT_BODIES");
    expect_not_implemented([&] { bare_soft_body.move_particle(std::uint32_t{}, Vector3{}); }, "SoftBody::move_particle", "SOFT_BODIES");
    expect_not_implemented([&] { bare_soft_body.set_damping(float{}); }, "SoftBody::set_damping", "SOFT_BODIES");
    expect_not_implemented([&] { bare_soft_body.set_gravity_scale(float{}); }, "SoftBody::set_gravity_scale", "SOFT_BODIES");
    expect_not_implemented([&] { bare_soft_body.set_self_collision(bool{}); }, "SoftBody::set_self_collision", "SOFT_BODIES");
    expect_not_implemented([&] { bare_soft_body.set_filter(CollisionFilter{}); }, "SoftBody::set_filter", "SOFT_BODIES");
    BareBroadPhase bare_broad_phase;
    expect_not_implemented([&] { (void)bare_broad_phase.insert(ColliderHandle{}, Aabb{}, CollisionFilter{}); }, "BroadPhase::insert", "CUSTOM_BROAD_PHASE");
    expect_not_implemented([&] { bare_broad_phase.remove(SpatialProxyHandle{}); }, "BroadPhase::remove", "CUSTOM_BROAD_PHASE");
    expect_not_implemented([&] { (void)bare_broad_phase.move(SpatialProxyHandle{}, Aabb{}, Vector3{}); }, "BroadPhase::move", "CUSTOM_BROAD_PHASE");
    expect_not_implemented([&] { bare_broad_phase.set_filter(SpatialProxyHandle{}, CollisionFilter{}); }, "BroadPhase::set_filter", "CUSTOM_BROAD_PHASE");
    expect_not_implemented([&] { bare_broad_phase.update(nullptr); }, "BroadPhase::update", "CUSTOM_BROAD_PHASE");
    expect_not_implemented([&] { (void)bare_broad_phase.pairs(); }, "BroadPhase::pairs", "CUSTOM_BROAD_PHASE");
    expect_not_implemented([&] { (void)bare_broad_phase.new_pairs(); }, "BroadPhase::new_pairs", "CUSTOM_BROAD_PHASE");
    expect_not_implemented([&] { (void)bare_broad_phase.ended_pairs(); }, "BroadPhase::ended_pairs", "CUSTOM_BROAD_PHASE");
    std::vector<SpatialNode> nodes;
    expect_not_implemented([&] { bare_broad_phase.structure_nodes(nodes); }, "BroadPhase::structure_nodes", "CUSTOM_BROAD_PHASE");
    BareNarrowPhase bare_narrow_phase;
    expect_not_implemented([&] { bare_narrow_phase.collide(ShapeInstance{}, ShapeInstance{}, contacts, CollisionSettings{}); }, "NarrowPhase::collide", "CUSTOM_NARROW_PHASE");
    BareSolver bare_solver;
    expect_not_implemented([&] { bare_solver.solve(SolverStep{}); }, "Solver::solve", "CUSTOM_SOLVER");
    BareIntegrator bare_integrator;
    expect_not_implemented([&] { bare_integrator.predict(IntegrationState{}, float{}); }, "Integrator::predict", "CUSTOM_INTEGRATOR");
    expect_not_implemented([&] { bare_integrator.finalize(IntegrationState{}, float{}); }, "Integrator::finalize", "CUSTOM_INTEGRATOR");
    BarePhysicsWorld bare_physics_world;
    expect_not_implemented([&] { (void)bare_physics_world.description(); }, "PhysicsWorld::description", "WORLD_OBJECTS");
    expect_not_implemented([&] { (void)bare_physics_world.create_rigid_body(RigidBodyDescription{}); }, "PhysicsWorld::create_rigid_body", "WORLD_OBJECTS");
    expect_not_implemented([&] { (void)bare_physics_world.create_soft_body(SoftBodyDescription{}); }, "PhysicsWorld::create_soft_body", "SOFT_BODIES");
    expect_not_implemented([&] { (void)bare_physics_world.create_joint(JointDescription{}); }, "PhysicsWorld::create_joint", "WORLD_OBJECTS");
    expect_not_implemented([&] { (void)bare_physics_world.create_constraint(ConstraintDescription{}); }, "PhysicsWorld::create_constraint", "USER_CONSTRAINTS");
    expect_not_implemented([&] { (void)bare_physics_world.create_trigger(TriggerDescription{}); }, "PhysicsWorld::create_trigger", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_physics_world.set_gravity(Vector3{}); }, "PhysicsWorld::set_gravity", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_physics_world.set_solver_description(SolverDescription{}); }, "PhysicsWorld::set_solver_description", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_physics_world.set_sleep_description(SleepDescription{}); }, "PhysicsWorld::set_sleep_description", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_physics_world.step(float{}); }, "PhysicsWorld::step", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_physics_world.begin_step(float{}); }, "PhysicsWorld::begin_step", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_physics_world.wait_step(); }, "PhysicsWorld::wait_step", "WORLD_OBJECTS");
    expect_not_implemented([&] { (void)bare_physics_world.events(); }, "PhysicsWorld::events", "WORLD_OBJECTS");
    expect_not_implemented([&] { (void)bare_physics_world.statistics(); }, "PhysicsWorld::statistics", "WORLD_OBJECTS");
    expect_not_implemented([&] { (void)bare_physics_world.broad_phase(); }, "PhysicsWorld::broad_phase", "WORLD_OBJECTS");
    expect_not_implemented([&] { (void)bare_physics_world.narrow_phase(); }, "PhysicsWorld::narrow_phase", "WORLD_OBJECTS");
    expect_not_implemented([&] { (void)bare_physics_world.solver(); }, "PhysicsWorld::solver", "WORLD_OBJECTS");
    expect_not_implemented([&] { (void)bare_physics_world.integrator(); }, "PhysicsWorld::integrator", "WORLD_OBJECTS");
    expect_not_implemented([&] { bare_physics_world.replace_broad_phase(std::unique_ptr<BroadPhase>{}); }, "PhysicsWorld::replace_broad_phase", "CUSTOM_BROAD_PHASE");
    expect_not_implemented([&] { bare_physics_world.replace_narrow_phase(std::unique_ptr<NarrowPhase>{}); }, "PhysicsWorld::replace_narrow_phase", "CUSTOM_NARROW_PHASE");
    expect_not_implemented([&] { bare_physics_world.replace_solver(std::unique_ptr<Solver>{}); }, "PhysicsWorld::replace_solver", "CUSTOM_SOLVER");
    expect_not_implemented([&] { bare_physics_world.replace_integrator(std::unique_ptr<Integrator>{}); }, "PhysicsWorld::replace_integrator", "CUSTOM_INTEGRATOR");
    expect_not_implemented([&] { (void)bare_physics_world.soft_body_integrator(); }, "PhysicsWorld::soft_body_integrator", "SOFT_BODIES");
    expect_not_implemented([&] { bare_physics_world.replace_soft_body_integrator(std::unique_ptr<Integrator>{}); }, "PhysicsWorld::replace_soft_body_integrator", "SOFT_BODIES");
    expect_unsupported_query(bare_physics_world.ray_cast(Ray{}, float{}, QueryOptions{}), "PhysicsWorld::ray_cast");
    expect_unsupported_query(bare_physics_world.ray_cast_all(Ray{}, float{}, std::span<RayHit>{}, QueryOptions{}), "PhysicsWorld::ray_cast_all");
    expect_unsupported_query(bare_physics_world.shape_cast(Shape{SphereShape{}}, Transform{}, Vector3{}, QueryOptions{}), "PhysicsWorld::shape_cast");
    expect_unsupported_query(bare_physics_world.overlap(Shape{SphereShape{}}, Transform{}, std::span<ColliderHandle>{}, QueryOptions{}), "PhysicsWorld::overlap");
    expect_unsupported_query(bare_physics_world.overlap(Aabb{}, std::span<ColliderHandle>{}, QueryOptions{}), "PhysicsWorld::overlap");
    expect_unsupported_query(bare_physics_world.closest_point(Vector3{}, float{}, QueryOptions{}), "PhysicsWorld::closest_point");
    expect_unsupported_query(bare_physics_world.contacts_of(RigidBodyHandle{}, std::span<ContactInfo>{}), "PhysicsWorld::contacts_of");
}

} // namespace
