module gfx.physics.backends.reference;

import std;
import gfx.core.errors;
import gfx.core.handles;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.shapes;
import gfx.physics.narrow_phase;
import gfx.physics.spatial;
import gfx.physics.dynamics;
import gfx.physics.world;
import :backend;

namespace gfx::physics::reference_backend
{

namespace
{

/// Below it, a mass or a determinant counts as zero.
constexpr float TINY = 1.0e-9F;

/// A matrix of zeros.
constexpr Matrix3 ZERO_MATRIX{.columns = {Vector3{}, Vector3{}, Vector3{}}};

/// The mass properties a body moves with: a dynamic body's (a unit mass and inertia when its colliders give none),
/// none for the others.
[[nodiscard]] auto
effective_mass(const RigidBodyRecord& record) noexcept -> MassProperties
{
    if (record.configuration.kind != BodyKind::DYNAMIC)
    {
        return {};
    }
    if (record.mass.mass > 0.0F)
    {
        return record.mass;
    }
    return {.mass = 1.0F, .center_of_mass = {}, .inertia = Matrix3::identity()};
}

/// 1 along the axes @p locks leaves free, 0 along the others (linear: the first three bits; angular: the last three).
[[nodiscard]] auto
freedom(MotionLock locks,
        bool angular) noexcept -> Vector3
{
    const auto free = [locks](MotionLock lock) { return has_any(locks, lock) ? 0.0F : 1.0F; };
    return angular ? Vector3{.x = free(MotionLock::ANGULAR_X), .y = free(MotionLock::ANGULAR_Y), .z = free(MotionLock::ANGULAR_Z)}
                   : Vector3{.x = free(MotionLock::LINEAR_X), .y = free(MotionLock::LINEAR_Y), .z = free(MotionLock::LINEAR_Z)};
}

/// The world inverse inertia of a body of @p mass at @p orientation, 0 about the axes @p angular_freedom locks.
[[nodiscard]] auto
world_inverse_inertia(const MassProperties& mass,
                      const Quaternion& orientation,
                      Vector3 angular_freedom) noexcept -> Matrix3
{
    if (!(std::abs(determinant(mass.inertia)) > std::numeric_limits<float>::min()))
    {
        return ZERO_MATRIX;
    }
    const Matrix3 rotation = Matrix3::from_rotation(orientation);
    const Matrix3 world = rotation * inverse(mass.inertia) * transpose(rotation);
    return Matrix3{.columns = {component_multiply(world.columns[0], angular_freedom) * angular_freedom.x, component_multiply(world.columns[1], angular_freedom) * angular_freedom.y,
                               component_multiply(world.columns[2], angular_freedom) * angular_freedom.z}};
}

/// The rotation turning +z (or +x) onto @p axis.
[[nodiscard]] auto
frame_toward(Vector3 from,
             Vector3 axis) noexcept -> Quaternion
{
    return Quaternion::from_rotation_between(from, axis);
}

/// Union-find: the root of @p index.
[[nodiscard]] auto
island_root(std::vector<std::uint32_t>& parents,
            std::uint32_t index) noexcept -> std::uint32_t
{
    while (parents[index] != index)
    {
        parents[index] = parents[parents[index]];
        index = parents[index];
    }
    return index;
}

void
join_islands(std::vector<std::uint32_t>& parents,
             std::uint32_t first,
             std::uint32_t second) noexcept
{
    const std::uint32_t a = island_root(parents, first);
    const std::uint32_t b = island_root(parents, second);
    if (a != b)
    {
        // The smaller slot becomes the root: islands are the same whatever order pairs come in.
        parents[std::max(a, b)] = std::min(a, b);
    }
}

} // namespace

// ===================================================================================================== the factory's functions

auto
unavailable_reason() -> std::string
{
    return {};
}

auto
create_physics_world(const PhysicsWorldDescription& description) -> std::unique_ptr<PhysicsWorld>
{
    check_world_description(description, PhysicsBackendKind::REFERENCE);
    return std::make_unique<SteppedWorld>(description, std::make_unique<ReferenceBackend>(description));
}

// ===================================================================================================== the edits

/// Applies each edit to what the backend keeps of the objects.
struct EditApplier
{
    ReferenceBackend& backend;
    WorldSnapshot& pending;

    [[nodiscard]] auto body(RigidBodyHandle handle) const -> BodyEntry&
    {
        const std::uint32_t slot = ReferenceBackend::slot(handle);
        if (backend.m_bodies.size() <= slot)
        {
            backend.m_bodies.resize(slot + 1);
        }
        return backend.m_bodies[slot];
    }

    /// Where @p handle's center of mass is now.
    [[nodiscard]] auto center_of(RigidBodyHandle handle) const noexcept -> Vector3
    {
        const RigidBodyRecord* record = find(pending, handle);
        return record != nullptr ? transform_point(record->state.pose, effective_mass(*record).center_of_mass) : Vector3{};
    }

    // Bodies: forces and impulses kept for the step; whatever moves a body wakes it.
    void operator()(const edits::CreateRigidBody& change) const
    {
        body(change.body) = {.handle = change.body, .woken = change.description.awake};
    }

    void operator()(const edits::DestroyRigidBody& change) const
    {
        body(change.body) = {};
    }

    void operator()(const edits::SetBodyPose& change) const
    {
        body(change.body).woken = true;
    }

    void operator()(const edits::SetBodyLinearVelocity& change) const
    {
        body(change.body).woken = true;
    }

    void operator()(const edits::SetBodyAngularVelocity& change) const
    {
        body(change.body).woken = true;
    }

    void operator()(const edits::MoveKinematicBody& change) const
    {
        body(change.body).woken = true;
    }

    void operator()(const edits::SetBodyKind& change) const
    {
        body(change.body).woken = true;
    }

    void operator()(const edits::SetBodyMassProperties& change) const
    {
        body(change.body).woken = true;
    }

    void operator()(const edits::SetBodyAwake& change) const
    {
        BodyEntry& entry = body(change.body);
        entry.woken = change.awake;
        entry.sleep_time = 0.0F;
    }

    void operator()(const edits::ApplyForce& change) const
    {
        BodyEntry& entry = body(change.body);
        entry.force += change.force;
        if (change.point)
        {
            entry.torque += cross(*change.point - center_of(change.body), change.force);
        }
        entry.woken = true;
    }

    void operator()(const edits::ApplyTorque& change) const
    {
        BodyEntry& entry = body(change.body);
        entry.torque += change.torque;
        entry.woken = true;
    }

    void operator()(const edits::ApplyLinearImpulse& change) const
    {
        BodyEntry& entry = body(change.body);
        entry.linear_impulse += change.impulse;
        if (change.point)
        {
            entry.angular_impulse += cross(*change.point - center_of(change.body), change.impulse);
        }
        entry.woken = true;
    }

    void operator()(const edits::ApplyAngularImpulse& change) const
    {
        BodyEntry& entry = body(change.body);
        entry.angular_impulse += change.impulse;
        entry.woken = true;
    }

    // Colliders: a proxy in the broad phase; destroyed, their touches end now.
    void operator()(const edits::CreateCollider& change) const
    {
        const std::uint32_t slot = ReferenceBackend::slot(change.collider);
        if (backend.m_colliders.size() <= slot)
        {
            backend.m_colliders.resize(slot + 1);
        }
        const ColliderRecord* record = find(pending, change.collider);
        const Aabb box = record != nullptr ? ReferenceBackend::collider_box(pending, *record) : Aabb::empty();
        backend.m_colliders[slot] = {.handle = change.collider, .body = change.body, .proxy = backend.m_broad_phase->insert(change.collider, box, change.description.filter), .box = box};
        body(change.body).woken = true;
    }

    void operator()(const edits::DestroyCollider& change) const
    {
        const std::uint32_t slot = ReferenceBackend::slot(change.collider);
        if (slot < backend.m_colliders.size() && backend.m_colliders[slot].handle == change.collider)
        {
            backend.forget_collider(change.collider, pending);
            backend.m_broad_phase->remove(backend.m_colliders[slot].proxy);
            backend.m_colliders[slot] = {};
        }
    }

    void operator()(const edits::SetColliderFilter& change) const
    {
        const std::uint32_t slot = ReferenceBackend::slot(change.collider);
        if (slot < backend.m_colliders.size() && backend.m_colliders[slot].handle == change.collider)
        {
            backend.m_broad_phase->set_filter(backend.m_colliders[slot].proxy, change.filter);
        }
    }

    // Joints and user constraints: frames from the bodies' poses now; updated or enabled, they start over.
    void operator()(const edits::CreateJoint& change) const
    {
        const std::uint32_t slot = ReferenceBackend::slot(change.joint);
        if (backend.m_joints.size() <= slot)
        {
            backend.m_joints.resize(slot + 1);
        }
        backend.m_joints[slot] = ReferenceBackend::joint_entry(change.description, pending);
    }

    void operator()(const edits::UpdateJoint& change) const
    {
        backend.m_joints[ReferenceBackend::slot(change.joint)].impulses = {};
    }

    void operator()(const edits::SetJointEnabled& change) const
    {
        backend.m_joints[ReferenceBackend::slot(change.joint)].impulses = {};
    }

    void operator()(const edits::CreateConstraint& change) const
    {
        const std::uint32_t slot = ReferenceBackend::slot(change.constraint);
        if (backend.m_constraints.size() <= slot)
        {
            backend.m_constraints.resize(slot + 1);
        }
        backend.m_constraints[slot] = {};
    }

    void operator()(const edits::SetConstraintEnabled& change) const
    {
        backend.m_constraints[ReferenceBackend::slot(change.constraint)].impulses = {};
    }

    // Triggers: their overlaps.
    void operator()(const edits::CreateTrigger& change) const
    {
        const std::uint32_t slot = ReferenceBackend::slot(change.trigger);
        if (backend.m_triggers.size() <= slot)
        {
            backend.m_triggers.resize(slot + 1);
        }
        backend.m_triggers[slot] = {.handle = change.trigger, .overlaps = {}};
    }

    void operator()(const edits::DestroyTrigger& change) const
    {
        const std::uint32_t slot = ReferenceBackend::slot(change.trigger);
        if (slot < backend.m_triggers.size())
        {
            backend.m_triggers[slot].handle = {};
            backend.m_triggers[slot].overlaps.clear();
        }
    }

    // The rest: the records are all the backend needs (the world's settings, damping, materials, soft bodies...).
    template<class Change>
    void operator()(const Change& /*change*/) const
    {
    }
};

// ===================================================================================================== ReferenceBackend

ReferenceBackend::ReferenceBackend(const PhysicsWorldDescription& description)
    : m_scheduler{description.task_scheduler}
    , m_broad_phase{std::make_unique<SpatialBroadPhaseComponent<Bvh>>()}
    , m_narrow_phase{std::make_unique<GjkNarrowPhase>()}
    , m_solver{std::make_unique<SequentialImpulseSolver>()}
    , m_integrator{std::make_unique<SemiImplicitEulerIntegrator>()}
{
}

ReferenceBackend::~ReferenceBackend() = default;

auto
ReferenceBackend::pipeline() const noexcept -> PipelineKinds
{
    return {.broad_phase = m_broad_phase->kind(), .narrow_phase = m_narrow_phase->kind(), .solver = m_solver->kind(), .integrator = m_integrator->kind()};
}

auto
ReferenceBackend::broad_phase_component() noexcept -> BroadPhase*
{
    return m_broad_phase.get();
}

auto
ReferenceBackend::narrow_phase_component() noexcept -> NarrowPhase*
{
    return m_narrow_phase.get();
}

auto
ReferenceBackend::solver_component() noexcept -> Solver*
{
    return m_solver.get();
}

auto
ReferenceBackend::integrator_component() noexcept -> Integrator*
{
    return m_integrator.get();
}

namespace
{

/// Throws the PhysicsError of a null component.
template<class Component>
void
require_component(const std::unique_ptr<Component>& component,
                  std::string_view operation)
{
    if (component == nullptr)
    {
        throw PhysicsError({.backend = PhysicsBackendKind::REFERENCE, .operation = std::string{operation}, .problem = "the component is null",
                            .suggestion = "give the component to run (the abstraction's, or one of the application's)"});
    }
}

} // namespace

void
ReferenceBackend::replace_broad_phase(std::unique_ptr<BroadPhase> broad_phase)
{
    // The live colliders move to the new broad phase.
    require_component(broad_phase, "PhysicsWorld::replace_broad_phase");
    m_broad_phase = std::move(broad_phase);
    if (m_last_pending != nullptr)
    {
        insert_colliders(*m_last_pending);
    }
}

void
ReferenceBackend::replace_narrow_phase(std::unique_ptr<NarrowPhase> narrow_phase)
{
    require_component(narrow_phase, "PhysicsWorld::replace_narrow_phase");
    m_narrow_phase = std::move(narrow_phase);
}

void
ReferenceBackend::replace_solver(std::unique_ptr<Solver> solver)
{
    require_component(solver, "PhysicsWorld::replace_solver");
    m_solver = std::move(solver);
}

void
ReferenceBackend::replace_integrator(std::unique_ptr<Integrator> integrator)
{
    require_component(integrator, "PhysicsWorld::replace_integrator");
    m_integrator = std::move(integrator);
}

void
ReferenceBackend::insert_colliders(const WorldSnapshot& pending)
{
    for (ColliderEntry& entry : m_colliders)
    {
        if (!entry.handle)
        {
            continue;
        }
        const ColliderRecord* record = find(pending, entry.handle);
        entry.proxy = m_broad_phase->insert(entry.handle, entry.box, record != nullptr ? record->configuration.filter : CollisionFilter{});
    }
}

auto
ReferenceBackend::collider_box(const WorldSnapshot& pending,
                               const ColliderRecord& collider) noexcept -> Aabb
{
    return collider.configuration.shape->bounds(world_pose(pending, collider));
}

auto
ReferenceBackend::joint_entry(const JointDescription& description,
                              const WorldSnapshot& pending) -> JointEntry
{
    // The joint's world frame now, in each body's frame (distance joints' anchors are in the bodies' frames already).
    const auto [first, second] = joint_bodies(description);
    const auto pose_of = [&pending](RigidBodyHandle body) {
        const RigidBodyRecord* record = body ? find(pending, body) : nullptr;
        return record != nullptr ? record->state.pose : Transform{};
    };
    const Transform first_pose = pose_of(first);
    const Transform second_pose = pose_of(second);
    const auto entry_of = [&](const Transform& world) {
        return JointEntry{.frame_first = inverse(first_pose) * world, .frame_second = second ? inverse(second_pose) * world : world, .impulses = {}};
    };
    return std::visit(
        [&](const auto& joint) -> JointEntry {
            using Joint = std::remove_cvref_t<decltype(joint)>;
            if constexpr (std::same_as<Joint, DistanceJointDescription>)
            {
                return {.frame_first = {.position = joint.anchor_first}, .frame_second = {.position = joint.anchor_second}, .impulses = {}};
            }
            else if constexpr (std::same_as<Joint, HingeJointDescription> || std::same_as<Joint, ConeTwistJointDescription>)
            {
                return entry_of({.position = joint.anchor, .orientation = frame_toward({.z = 1.0F}, joint.axis)});
            }
            else if constexpr (std::same_as<Joint, SliderJointDescription>)
            {
                return entry_of({.position = joint.anchor, .orientation = frame_toward({.x = 1.0F}, joint.axis)});
            }
            else if constexpr (std::same_as<Joint, SixDegreesOfFreedomJointDescription>)
            {
                return entry_of(joint.frame);
            }
            else
            {
                return entry_of({.position = joint.anchor});
            }
        },
        description);
}

auto
ReferenceBackend::joined(RigidBodyHandle first,
                         RigidBodyHandle second) const noexcept -> bool
{
    const std::pair key = std::minmax(first, second);
    return std::ranges::binary_search(m_joined, std::pair{key.first, key.second});
}

void
ReferenceBackend::apply(std::span<const WorldEdit> edits,
                        WorldSnapshot& pending)
{
    m_last_pending = &pending;
    const EditApplier applier{.backend = *this, .pending = pending};
    for (const WorldEdit& edit : edits)
    {
        std::visit(applier, edit);
    }
}

// ===================================================================================================== the step

void
ReferenceBackend::simulate(float seconds,
                           WorldSnapshot& pending)
{
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    m_last_pending = &pending;
    const PhysicsWorldDescription& world = pending.configuration;
    const std::size_t body_count = pending.rigid_bodies.size();
    if (m_bodies.size() < body_count)
    {
        m_bodies.resize(body_count);
    }

    // ------------------------------------------------------------------------------------------------ the bodies
    // Bodies an edit woke are awake; each body as the solver sees it (asleep and static ones do not move); the awake
    // dynamic ones take their impulses now, their forces and gravity at each sub-step through the integrator.
    m_solver_bodies.assign(body_count, SolverBody{});
    m_body_forces.assign(body_count, Vector3{});
    m_body_torques.assign(body_count, Vector3{});
    for (std::uint32_t slot = 0; slot < body_count; ++slot)
    {
        RigidBodyRecord& record = pending.rigid_bodies[slot];
        BodyEntry& entry = m_bodies[slot];
        if (!record.handle)
        {
            continue;
        }
        if (entry.woken && record.configuration.kind == BodyKind::DYNAMIC && !record.state.awake)
        {
            record.state.awake = true;
            pending.sleep_events.push_back({.kind = SleepEventKind::WOKE_UP, .body = record.handle});
        }
        entry.woken = false;
        const MassProperties mass = effective_mass(record);
        SolverBody& body = m_solver_bodies[slot];
        body.center = transform_point(record.state.pose, mass.center_of_mass);
        body.orientation = record.state.pose.orientation;
        if (record.configuration.kind == BodyKind::KINEMATIC)
        {
            body.linear_velocity = record.state.linear_velocity;
            body.angular_velocity = record.state.angular_velocity;
            continue;
        }
        if (record.configuration.kind == BodyKind::DYNAMIC)
        {
            m_body_forces[slot] = entry.force + world.gravity * (record.configuration.gravity_scale * mass.mass);
            m_body_torques[slot] = entry.torque;
        }
        if (record.configuration.kind != BodyKind::DYNAMIC || !record.state.awake)
        {
            record.state.linear_velocity = {};
            record.state.angular_velocity = {};
            entry.force = entry.torque = entry.linear_impulse = entry.angular_impulse = {};
            continue;
        }
        const Vector3 linear_freedom = freedom(record.configuration.locks, false);
        const Vector3 angular_freedom = freedom(record.configuration.locks, true);
        body.inverse_mass = 1.0F / mass.mass;
        body.inverse_inertia = world_inverse_inertia(mass, body.orientation, angular_freedom);
        body.linear_freedom = linear_freedom;
        body.angular_velocity = component_multiply(record.state.angular_velocity, angular_freedom) + body.inverse_inertia * entry.angular_impulse;
        const Vector3 velocity = record.state.linear_velocity + cross(record.state.angular_velocity, body.center - record.state.pose.position);
        body.linear_velocity = component_multiply(velocity, linear_freedom) + component_multiply(entry.linear_impulse, linear_freedom) * body.inverse_mass;
        entry.force = entry.torque = entry.linear_impulse = entry.angular_impulse = {};
    }

    // ------------------------------------------------------------------------------------------------ contacts
    // The bodies that may not collide; the colliders' boxes (stretched by the motion of continuous bodies); the broad
    // phase's pairs; their manifolds.
    m_joined.clear();
    for (const JointRecord& joint : pending.joints)
    {
        const bool keeps_apart = std::visit([](const auto& description) { return !description.collide_connected; }, joint.configuration);
        if (joint.handle && joint.state.enabled && keeps_apart)
        {
            const auto [first, second] = joint_bodies(joint.configuration);
            const std::pair key = std::minmax(first, second);
            m_joined.emplace_back(key.first, key.second);
        }
    }
    for (const ConstraintRecord& constraint : pending.constraints)
    {
        if (constraint.handle && constraint.state.enabled && !constraint.configuration.collide_connected)
        {
            const std::pair key = std::minmax(constraint.configuration.first, constraint.configuration.second);
            m_joined.emplace_back(key.first, key.second);
        }
    }
    std::ranges::sort(m_joined);
    for (ColliderEntry& entry : m_colliders)
    {
        const ColliderRecord* record = entry.handle ? find(pending, entry.handle) : nullptr;
        if (record == nullptr)
        {
            continue;
        }
        const RigidBodyRecord* body = find(pending, record->body);
        Aabb box = collider_box(pending, *record);
        Vector3 displacement{};
        if (body != nullptr && body->configuration.kind != BodyKind::STATIC)
        {
            displacement = m_solver_bodies[slot(record->body)].linear_velocity * seconds;
            if (body->configuration.motion_quality == MotionQuality::CONTINUOUS)
            {
                box = box.merged(Aabb{.minimum = box.minimum + displacement, .maximum = box.maximum + displacement});
            }
        }
        entry.box = box;
        (void)m_broad_phase->move(entry.proxy, box, displacement);
    }
    m_broad_phase->update(m_scheduler);
    const Clock::time_point broad = Clock::now();
    find_contacts(seconds, pending);
    const Clock::time_point narrow = Clock::now();

    // ------------------------------------------------------------------------------------------------ islands
    // Bodies joined by contacts, joints or constraints (dynamic ones only: static and kinematic bodies do not join
    // islands); an island with an awake body (or touching a moving kinematic one) is awake as a whole.
    m_islands.resize(body_count);
    std::ranges::iota(m_islands, 0U);
    m_island_awake.assign(body_count, 0);
    const auto dynamic_slot = [&pending](RigidBodyHandle handle) -> std::optional<std::uint32_t> {
        const RigidBodyRecord* record = handle ? find(pending, handle) : nullptr;
        if (record == nullptr || record->configuration.kind != BodyKind::DYNAMIC)
        {
            return std::nullopt;
        }
        return ReferenceBackend::slot(handle);
    };
    const auto moving_kinematic = [&pending](RigidBodyHandle handle) {
        const RigidBodyRecord* record = handle ? find(pending, handle) : nullptr;
        return record != nullptr && record->configuration.kind == BodyKind::KINEMATIC && (record->state.linear_velocity != Vector3{} || record->state.angular_velocity != Vector3{});
    };
    const auto connect = [&](RigidBodyHandle first, RigidBodyHandle second) {
        const std::optional<std::uint32_t> a = dynamic_slot(first);
        const std::optional<std::uint32_t> b = dynamic_slot(second);
        if (a && b)
        {
            join_islands(m_islands, *a, *b);
        }
        else if (a && moving_kinematic(second))
        {
            m_island_awake[*a] = 1;
        }
        else if (b && moving_kinematic(first))
        {
            m_island_awake[*b] = 1;
        }
    };
    for (const PairManifold& manifold : m_manifolds)
    {
        connect(m_colliders[slot(manifold.pair.first)].body, m_colliders[slot(manifold.pair.second)].body);
    }
    for (const JointRecord& joint : pending.joints)
    {
        if (joint.handle && joint.state.enabled)
        {
            const auto [first, second] = joint_bodies(joint.configuration);
            connect(first, second);
        }
    }
    for (const ConstraintRecord& constraint : pending.constraints)
    {
        if (constraint.handle && constraint.state.enabled)
        {
            connect(constraint.configuration.first, constraint.configuration.second);
        }
    }
    for (std::uint32_t slot_index = 0; slot_index < body_count; ++slot_index)
    {
        const RigidBodyRecord& record = pending.rigid_bodies[slot_index];
        if (record.handle && record.configuration.kind == BodyKind::DYNAMIC && (record.state.awake || m_island_awake[slot_index] != 0))
        {
            m_island_awake[island_root(m_islands, slot_index)] = 1;
        }
    }
    for (std::uint32_t slot_index = 0; slot_index < body_count; ++slot_index)
    {
        RigidBodyRecord& record = pending.rigid_bodies[slot_index];
        if (!record.handle || record.configuration.kind != BodyKind::DYNAMIC || record.state.awake || m_island_awake[island_root(m_islands, slot_index)] == 0)
        {
            continue;
        }
        // Woken by its island: it starts at rest.
        record.state.awake = true;
        m_bodies[slot_index].sleep_time = 0.0F;
        pending.sleep_events.push_back({.kind = SleepEventKind::WOKE_UP, .body = record.handle});
        const MassProperties mass = effective_mass(record);
        SolverBody& body = m_solver_bodies[slot_index];
        body.inverse_mass = 1.0F / mass.mass;
        body.linear_freedom = freedom(record.configuration.locks, false);
        body.inverse_inertia = world_inverse_inertia(mass, body.orientation, freedom(record.configuration.locks, true));
    }

    // ------------------------------------------------------------------------------------------------ the solver
    // Contacts of a pair with a moving body, with the pair's materials; joints and constraints that hold a moving body.
    m_solver_contacts.clear();
    m_contact_manifolds.clear();
    for (std::uint32_t index = 0; index < m_manifolds.size(); ++index)
    {
        const PairManifold& manifold = m_manifolds[index];
        const ColliderRecord* first = find(pending, manifold.pair.first);
        const ColliderRecord* second = find(pending, manifold.pair.second);
        if (first == nullptr || second == nullptr || manifold.manifold.point_count == 0)
        {
            continue;
        }
        const std::uint32_t first_body = slot(first->body);
        const std::uint32_t second_body = slot(second->body);
        if (!(m_solver_bodies[first_body].inverse_mass > 0.0F) && !(m_solver_bodies[second_body].inverse_mass > 0.0F))
        {
            continue;
        }
        const Friction& a = first->configuration.material.friction;
        const Friction& b = second->configuration.material.friction;
        m_solver_contacts.push_back({.first = first_body,
                                     .second = second_body,
                                     .manifold = manifold.manifold,
                                     .friction = {.static_coefficient = std::sqrt(a.static_coefficient * b.static_coefficient),
                                                  .dynamic_coefficient = std::sqrt(a.dynamic_coefficient * b.dynamic_coefficient),
                                                  .rolling = std::max(a.rolling, b.rolling)},
                                     .restitution = std::max(first->configuration.material.restitution, second->configuration.material.restitution),
                                     .rolling_impulse = {}});
        m_contact_manifolds.push_back(index);
    }
    m_solver_joints.clear();
    m_joint_slots.clear();
    const auto solver_frame = [&pending](RigidBodyHandle body, const Transform& frame) {
        const RigidBodyRecord* record = body ? find(pending, body) : nullptr;
        return record != nullptr ? Transform{.position = frame.position - effective_mass(*record).center_of_mass, .orientation = frame.orientation} : frame;
    };
    const auto holds_moving = [&](RigidBodyHandle first, RigidBodyHandle second) {
        const auto moves = [&](RigidBodyHandle body) { return body && m_solver_bodies[slot(body)].inverse_mass > 0.0F; };
        return moves(first) || moves(second);
    };
    for (std::uint32_t joint_slot = 0; joint_slot < pending.joints.size(); ++joint_slot)
    {
        const JointRecord& record = pending.joints[joint_slot];
        const auto [first, second] = record.handle ? joint_bodies(record.configuration) : std::pair<RigidBodyHandle, RigidBodyHandle>{};
        if (!record.handle || !record.state.enabled || !holds_moving(first, second))
        {
            continue;
        }
        const JointEntry& entry = m_joints[joint_slot];
        m_solver_joints.push_back({.first = slot(first),
                                   .second = second ? slot(second) : NO_SOLVER_BODY,
                                   .description = record.configuration,
                                   .user = nullptr,
                                   .frame_first = solver_frame(first, entry.frame_first),
                                   .frame_second = solver_frame(second, entry.frame_second),
                                   .impulses = entry.impulses,
                                   .force = {},
                                   .torque = {}});
        m_joint_slots.push_back(joint_slot);
    }
    for (std::uint32_t constraint_slot = 0; constraint_slot < pending.constraints.size(); ++constraint_slot)
    {
        const ConstraintRecord& record = pending.constraints[constraint_slot];
        if (!record.handle || !record.state.enabled || !holds_moving(record.configuration.first, record.configuration.second))
        {
            continue;
        }
        m_solver_joints.push_back({.first = slot(record.configuration.first),
                                   .second = record.configuration.second ? slot(record.configuration.second) : NO_SOLVER_BODY,
                                   .description = {},
                                   .user = record.configuration.constraint.get(),
                                   .frame_first = {},
                                   .frame_second = {},
                                   .impulses = m_constraints[constraint_slot].impulses,
                                   .force = {},
                                   .torque = {}});
        m_joint_slots.push_back(0x80000000U | constraint_slot);
    }
    // Sub-steps (SolverDescription::sub_steps): the forces over each through the integrator, the contacts' points moved
    // with their bodies (collided once per step), the solve.
    const std::uint32_t sub_steps = std::max(1U, world.solver.sub_steps);
    const float sub_seconds = seconds / static_cast<float>(sub_steps);
    m_contact_geometry.resize(m_solver_contacts.size());
    for (std::size_t index = 0; index < m_solver_contacts.size(); ++index)
    {
        const SolverContact& contact = m_solver_contacts[index];
        const SolverBody& first = m_solver_bodies[contact.first];
        const SolverBody& second = m_solver_bodies[contact.second];
        ContactGeometry& geometry = m_contact_geometry[index];
        geometry.normal = rotate(conjugate(first.orientation), contact.manifold.normal);
        for (std::uint32_t point = 0; point < contact.manifold.point_count; ++point)
        {
            const ContactPoint& source = contact.manifold.points[point];
            geometry.first[point] = rotate(conjugate(first.orientation), source.position - contact.manifold.normal * (0.5F * source.separation) - first.center);
            geometry.second[point] = rotate(conjugate(second.orientation), source.position + contact.manifold.normal * (0.5F * source.separation) - second.center);
        }
    }
    for (std::uint32_t sub_step = 0; sub_step < sub_steps; ++sub_step)
    {
        // Forces and gravity, torques, damping.
        m_integrated.clear();
        m_centers.clear();
        m_velocities.clear();
        m_previous_centers.clear();
        m_forces.clear();
        m_inverse_masses.clear();
        for (std::uint32_t slot_index = 0; slot_index < body_count; ++slot_index)
        {
            const SolverBody& body = m_solver_bodies[slot_index];
            if (body.inverse_mass > 0.0F)
            {
                m_integrated.push_back(slot_index);
                m_centers.push_back(body.center);
                m_velocities.push_back(body.linear_velocity);
                m_previous_centers.push_back(body.center - body.linear_velocity * sub_seconds);
                m_forces.push_back(m_body_forces[slot_index]);
                m_inverse_masses.push_back(body.inverse_mass);
            }
        }
        m_integrator->predict({.positions = m_centers,
                               .velocities = m_velocities,
                               .inverse_masses = m_inverse_masses,
                               .forces = m_forces,
                               .previous_positions = m_previous_centers,
                               .gravity = {},
                               .force_field = nullptr},
                              sub_seconds);
        for (std::size_t index = 0; index < m_integrated.size(); ++index)
        {
            const std::uint32_t slot_index = m_integrated[index];
            SolverBody& body = m_solver_bodies[slot_index];
            const RigidBodyDescription& configuration = pending.rigid_bodies[slot_index].configuration;
            body.linear_velocity = component_multiply(m_velocities[index], body.linear_freedom) / (1.0F + sub_seconds * configuration.linear_damping);
            body.angular_velocity = (body.angular_velocity + body.inverse_inertia * (m_body_torques[slot_index] * sub_seconds)) / (1.0F + sub_seconds * configuration.angular_damping);
        }

        // The contacts' points where their bodies now are (the first sub-step's are the narrow phase's).
        if (sub_step > 0)
        {
            for (std::size_t index = 0; index < m_solver_contacts.size(); ++index)
            {
                SolverContact& contact = m_solver_contacts[index];
                const SolverBody& first = m_solver_bodies[contact.first];
                const SolverBody& second = m_solver_bodies[contact.second];
                const ContactGeometry& geometry = m_contact_geometry[index];
                const Vector3 normal = rotate(first.orientation, geometry.normal);
                contact.manifold.normal = normal;
                for (std::uint32_t point = 0; point < contact.manifold.point_count; ++point)
                {
                    const Vector3 on_first = first.center + rotate(first.orientation, geometry.first[point]);
                    const Vector3 on_second = second.center + rotate(second.orientation, geometry.second[point]);
                    contact.manifold.points[point].position = (on_first + on_second) * 0.5F;
                    contact.manifold.points[point].separation = dot(on_second - on_first, normal);
                }
            }
        }
        m_solver->solve(
            {.seconds = sub_seconds, .settings = world.solver, .bodies = m_solver_bodies, .contacts = m_solver_contacts, .joints = m_solver_joints, .scheduler = m_scheduler});
    }

    // ------------------------------------------------------------------------------------------------ results
    // The moving bodies' poses and velocities (of their frames' origins).
    for (std::uint32_t slot_index = 0; slot_index < body_count; ++slot_index)
    {
        RigidBodyRecord& record = pending.rigid_bodies[slot_index];
        const SolverBody& body = m_solver_bodies[slot_index];
        const bool moved = record.configuration.kind == BodyKind::KINEMATIC || (record.configuration.kind == BodyKind::DYNAMIC && record.state.awake);
        if (!record.handle || !moved)
        {
            continue;
        }
        const Vector3 center_of_mass = effective_mass(record).center_of_mass;
        record.state.pose.orientation = body.orientation;
        record.state.pose.position = body.center - rotate(body.orientation, center_of_mass);
        record.state.angular_velocity = body.angular_velocity;
        record.state.linear_velocity = body.linear_velocity + cross(body.angular_velocity, record.state.pose.position - body.center);
    }

    // The contacts' impulses for the next step; the joints' forces, and those that broke.
    for (std::size_t index = 0; index < m_solver_contacts.size(); ++index)
    {
        m_manifolds[m_contact_manifolds[index]].manifold = m_solver_contacts[index].manifold;
    }
    for (const PairManifold& manifold : m_manifolds)
    {
        m_manifold_cache.store({.first = manifold.pair.first, .second = manifold.pair.second, .sub_shape_first = manifold.manifold.sub_shape_first,
                                .sub_shape_second = manifold.manifold.sub_shape_second},
                               manifold.manifold);
    }
    for (std::size_t index = 0; index < m_solver_joints.size(); ++index)
    {
        const SolverJoint& solved = m_solver_joints[index];
        const bool constraint = (m_joint_slots[index] & 0x80000000U) != 0;
        const std::uint32_t record_slot = m_joint_slots[index] & 0x7FFFFFFFU;
        JointState& state = constraint ? pending.constraints[record_slot].state : pending.joints[record_slot].state;
        const JointBreaking breaking = constraint ? pending.constraints[record_slot].configuration.breaking
                                                  : std::visit([](const auto& description) { return description.breaking; }, pending.joints[record_slot].configuration);
        JointEntry& entry = constraint ? m_constraints[record_slot] : m_joints[record_slot];
        entry.impulses = solved.impulses;
        state.force = solved.force;
        state.torque = solved.torque;
        const float force = length(solved.force);
        const float torque = length(solved.torque);
        if (force > breaking.force || torque > breaking.torque)
        {
            state.enabled = false;
            state.broken = true;
            entry.impulses = {};
            pending.joint_events.push_back({.kind = JointEventKind::BROKEN,
                                            .joint = constraint ? JointHandle{} : pending.joints[record_slot].handle,
                                            .force = force,
                                            .torque = torque,
                                            .constraint = constraint ? pending.constraints[record_slot].handle : ConstraintHandle{}});
        }
    }
    const Clock::time_point solved = Clock::now();

    // ------------------------------------------------------------------------------------------------ sleep
    // An island falls asleep when all its bodies have stayed slow long enough (and may sleep).
    m_island_sleep.assign(body_count, std::numeric_limits<float>::infinity());
    for (std::uint32_t slot_index = 0; slot_index < body_count; ++slot_index)
    {
        const RigidBodyRecord& record = pending.rigid_bodies[slot_index];
        if (!record.handle || record.configuration.kind != BodyKind::DYNAMIC || !record.state.awake)
        {
            continue;
        }
        BodyEntry& entry = m_bodies[slot_index];
        const SolverBody& body = m_solver_bodies[slot_index];
        const bool slow = length(body.linear_velocity) < world.sleep.linear_threshold && length(body.angular_velocity) < world.sleep.angular_threshold;
        entry.sleep_time = world.sleep.enabled && record.configuration.allow_sleep && slow ? entry.sleep_time + seconds : 0.0F;
        float& island = m_island_sleep[island_root(m_islands, slot_index)];
        island = std::min(island, entry.sleep_time);
    }
    for (const PairManifold& manifold : m_manifolds)
    {
        // Touching a moving kinematic body keeps an island awake.
        for (const ColliderHandle collider : {manifold.pair.first, manifold.pair.second})
        {
            const RigidBodyHandle body = m_colliders[slot(collider)].body;
            const ColliderHandle other = collider == manifold.pair.first ? manifold.pair.second : manifold.pair.first;
            if (const std::optional<std::uint32_t> own = dynamic_slot(body); own && moving_kinematic(m_colliders[slot(other)].body))
            {
                m_island_sleep[island_root(m_islands, *own)] = 0.0F;
            }
        }
    }
    for (std::uint32_t slot_index = 0; slot_index < body_count; ++slot_index)
    {
        RigidBodyRecord& record = pending.rigid_bodies[slot_index];
        if (!record.handle || record.configuration.kind != BodyKind::DYNAMIC || !record.state.awake || m_island_sleep[island_root(m_islands, slot_index)] < world.sleep.time)
        {
            continue;
        }
        record.state.awake = false;
        record.state.linear_velocity = {};
        record.state.angular_velocity = {};
        pending.sleep_events.push_back({.kind = SleepEventKind::FELL_ASLEEP, .body = record.handle});
    }

    // ------------------------------------------------------------------------------------------------ events and statistics
    report_contacts(pending);
    update_triggers(pending);
    m_manifold_cache.end_step();
    StepStatistics& statistics = pending.statistics;
    statistics.phase_seconds[std::to_underlying(StepPhase::BROAD_PHASE)] = std::chrono::duration<double>(broad - start).count();
    statistics.phase_seconds[std::to_underlying(StepPhase::NARROW_PHASE)] = std::chrono::duration<double>(narrow - broad).count();
    statistics.phase_seconds[std::to_underlying(StepPhase::SOLVER)] = std::chrono::duration<double>(solved - narrow).count();
    statistics.pairs = static_cast<std::uint32_t>(m_broad_phase->pairs().size());
}

} // namespace gfx::physics::reference_backend
