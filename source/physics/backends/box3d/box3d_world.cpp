// The Box3D backend's world: creation, Box3D's task hooks over the world's TaskScheduler, the checks of what Box3D cannot
// do, the edits applied to the Box3D world in order, the step, and what it reports — body moves and sleep, contacts (begin,
// end, hit; one touch per pair of colliders however many shapes touch), trigger overlaps (enter, stay, exit), broken
// joints, joint forces, the contact list, the statistics.
module;

#include <box3d/box3d.h>

module gfx.physics.backends.box3d;

import std;
import gfx.core.errors;
import gfx.core.handles;
import gfx.core.math;
import gfx.physics.world;
import :backend;

namespace gfx::physics::box3d_backend
{

namespace
{

// The partition's conversions next to this unit's (whose names would hide them).
using box3d_backend::from_box3d;
using box3d_backend::to_box3d;

// ===================================================================================================== task hooks

/// Box3D's enqueueTask: the task is started on the world's scheduler; its ticket goes back to Box3D (past the bridge's
/// capacity, the task runs at once and Box3D is told so with a null result).
auto
enqueue_task(b3TaskCallback* task,
             void* task_context,
             void* user_context,
             const char* /*name*/) -> void*
{
    TaskBridge& bridge = *static_cast<TaskBridge*>(user_context);
    const std::uint32_t index = bridge.used.fetch_add(1, std::memory_order_relaxed);
    if (index >= bridge.tickets.size())
    {
        task(task_context);
        return nullptr;
    }
    bridge.tickets[index] = bridge.scheduler->start([task, task_context] { task(task_context); });
    return &bridge.tickets[index];
}

/// Box3D's finishTask: waits for the started task.
void
finish_task(void* user_task,
            void* user_context)
{
    static_cast<TaskBridge*>(user_context)->scheduler->finish(*static_cast<TaskTicket*>(user_task));
}

// ===================================================================================================== values

[[nodiscard]] auto
to_box3d(BodyKind kind) noexcept -> b3BodyType
{
    switch (kind)
    {
        case BodyKind::STATIC:
            return b3_staticBody;
        case BodyKind::KINEMATIC:
            return b3_kinematicBody;
        case BodyKind::DYNAMIC:
            return b3_dynamicBody;
    }
    return b3_dynamicBody;
}

[[nodiscard]] auto
to_box3d(MotionLock locks) noexcept -> b3MotionLocks
{
    return {.linearX = has_any(locks, MotionLock::LINEAR_X),
            .linearY = has_any(locks, MotionLock::LINEAR_Y),
            .linearZ = has_any(locks, MotionLock::LINEAR_Z),
            .angularX = has_any(locks, MotionLock::ANGULAR_X),
            .angularY = has_any(locks, MotionLock::ANGULAR_Y),
            .angularZ = has_any(locks, MotionLock::ANGULAR_Z)};
}

[[nodiscard]] auto
to_box3d(const MassProperties& properties) noexcept -> b3MassData
{
    return {.mass = properties.mass,
            .center = to_box3d(properties.center_of_mass),
            .inertia = {.cx = to_box3d(properties.inertia.columns[0]), .cy = to_box3d(properties.inertia.columns[1]), .cz = to_box3d(properties.inertia.columns[2])}};
}

[[nodiscard]] auto
from_box3d(const b3MassData& mass) noexcept -> MassProperties
{
    return {.mass = mass.mass,
            .center_of_mass = from_box3d(mass.center),
            .inertia = {.columns = {from_box3d(mass.inertia.cx), from_box3d(mass.inertia.cy), from_box3d(mass.inertia.cz)}}};
}

/// The key of two slots (the first in the high half).
[[nodiscard]] auto
pair_key(std::uint32_t first,
         std::uint32_t second) noexcept -> std::uint64_t
{
    return (static_cast<std::uint64_t>(first) << 32U) | second;
}

/// @p values grown to hold index @p index.
template<class Value>
auto
grown(std::vector<Value>& values,
      std::size_t index) -> Value&
{
    if (index >= values.size())
    {
        values.resize(index + 1);
    }
    return values[index];
}

/// Our manifold of a Box3D manifold between the shapes @p shape_a and @p shape_b (flipped when @p flip).
[[nodiscard]] auto
to_manifold(const b3Manifold& manifold,
            b3ShapeId shape_a,
            b3ShapeId shape_b,
            bool flip) -> ContactManifold
{
    // Box3D's anchors are relative to each body's center of mass, in world space; ours are the midway points.
    const Vector3 center_a = from_box3d(b3Body_GetWorldCenter(b3Shape_GetBody(shape_a)));
    const Vector3 center_b = from_box3d(b3Body_GetWorldCenter(b3Shape_GetBody(shape_b)));
    ContactManifold result{.normal = flip ? -from_box3d(manifold.normal) : from_box3d(manifold.normal)};
    result.point_count = static_cast<std::uint32_t>(std::min(manifold.pointCount, static_cast<int>(MAXIMUM_MANIFOLD_POINTS)));
    for (std::uint32_t index = 0; index < result.point_count; ++index)
    {
        const b3ManifoldPoint& point = manifold.points[index];
        const auto triangle = point.triangleIndex >= 0 ? static_cast<std::uint32_t>(point.triangleIndex) : NO_SUB_SHAPE;
        result.points[index] = {.position = ((center_a + from_box3d(point.anchorA)) + (center_b + from_box3d(point.anchorB))) * 0.5F,
                                .separation = point.separation,
                                .feature = {.sub_shape_first = flip ? NO_SUB_SHAPE : triangle, .sub_shape_second = flip ? triangle : NO_SUB_SHAPE, .local = point.featureId},
                                .normal_impulse = point.normalImpulse,
                                .tangent_impulse = {}};
    }
    return result;
}

} // namespace

// ===================================================================================================== the edits

/// Applies each edit to the Box3D world.
struct EditApplier
{
    Box3dBackend& backend;
    WorldSnapshot& pending;

    [[nodiscard]] auto body(RigidBodyHandle handle) const -> BodyEntry&
    {
        return backend.m_bodies[Box3dBackend::slot(handle)];
    }

    // The world.
    void operator()(const edits::SetGravity& change) const
    {
        b3World_SetGravity(backend.m_world, to_box3d(change.gravity));
    }

    void operator()(const edits::SetSolverDescription& change) const
    {
        const SolverDescription& solver = change.description;
        backend.m_sub_steps = static_cast<int>(solver.sub_steps);
        b3World_SetRestitutionThreshold(backend.m_world, solver.restitution_threshold);
        b3World_SetContactTuning(backend.m_world, solver.contact_hertz, solver.contact_damping_ratio, solver.maximum_push_speed);
        b3World_EnableWarmStarting(backend.m_world, solver.warm_starting);
    }

    void operator()(const edits::SetSleepDescription& change) const
    {
        b3World_EnableSleeping(backend.m_world, change.description.enabled);
        backend.m_sleep_threshold = change.description.linear_threshold;
        for (const BodyEntry& entry : backend.m_bodies)
        {
            if (b3Body_IsValid(entry.id))
            {
                b3Body_SetSleepThreshold(entry.id, backend.m_sleep_threshold);
            }
        }
    }

    // Rigid bodies.
    void operator()(const edits::CreateRigidBody& change) const
    {
        const RigidBodyDescription& description = change.description;
        b3BodyDef definition = b3DefaultBodyDef();
        definition.type = to_box3d(description.kind);
        definition.position = to_box3d(description.pose.position);
        definition.rotation = to_box3d(description.pose.orientation);
        definition.linearVelocity = to_box3d(description.linear_velocity);
        definition.angularVelocity = to_box3d(description.angular_velocity);
        definition.linearDamping = description.linear_damping;
        definition.angularDamping = description.angular_damping;
        definition.gravityScale = description.gravity_scale;
        definition.sleepThreshold = backend.m_sleep_threshold;
        definition.motionLocks = to_box3d(description.locks);
        definition.enableSleep = description.allow_sleep;
        definition.isAwake = description.awake;
        definition.isBullet = description.motion_quality == MotionQuality::CONTINUOUS;
        const b3BodyId id = b3CreateBody(backend.m_world, &definition);
        grown(backend.m_bodies, Box3dBackend::slot(change.body)) = {.handle = change.body, .id = id, .mass_changed = description.mass_properties.has_value()};
        grown(backend.m_body_owners, static_cast<std::size_t>(id.index1)) = change.body;
    }

    void operator()(const edits::DestroyRigidBody& change) const
    {
        // Its colliders, triggers and joints went first (their edits came before).
        BodyEntry& entry = body(change.body);
        backend.m_body_owners[static_cast<std::size_t>(entry.id.index1)] = {};
        b3DestroyBody(entry.id);
        entry = {};
    }

    void operator()(const edits::SetBodyPose& change) const
    {
        b3Body_SetTransform(body(change.body).id, to_box3d(change.pose.position), to_box3d(change.pose.orientation));
    }

    void operator()(const edits::SetBodyLinearVelocity& change) const
    {
        b3Body_SetLinearVelocity(body(change.body).id, to_box3d(change.velocity));
    }

    void operator()(const edits::SetBodyAngularVelocity& change) const
    {
        b3Body_SetAngularVelocity(body(change.body).id, to_box3d(change.velocity));
    }

    void operator()(const edits::MoveKinematicBody& change) const
    {
        b3Body_SetTargetTransform(body(change.body).id, to_box3d(change.target), change.seconds, true);
    }

    void operator()(const edits::ApplyForce& change) const
    {
        if (change.point)
        {
            b3Body_ApplyForce(body(change.body).id, to_box3d(change.force), to_box3d(*change.point), true);
        }
        else
        {
            b3Body_ApplyForceToCenter(body(change.body).id, to_box3d(change.force), true);
        }
    }

    void operator()(const edits::ApplyTorque& change) const
    {
        b3Body_ApplyTorque(body(change.body).id, to_box3d(change.torque), true);
    }

    void operator()(const edits::ApplyLinearImpulse& change) const
    {
        if (change.point)
        {
            b3Body_ApplyLinearImpulse(body(change.body).id, to_box3d(change.impulse), to_box3d(*change.point), true);
        }
        else
        {
            b3Body_ApplyLinearImpulseToCenter(body(change.body).id, to_box3d(change.impulse), true);
        }
    }

    void operator()(const edits::ApplyAngularImpulse& change) const
    {
        b3Body_ApplyAngularImpulse(body(change.body).id, to_box3d(change.impulse), true);
    }

    void operator()(const edits::SetBodyKind& change) const
    {
        b3Body_SetType(body(change.body).id, to_box3d(change.kind));
        body(change.body).mass_changed = true;
    }

    void operator()(const edits::SetBodyMassProperties& change) const
    {
        body(change.body).mass_changed = true;
    }

    void operator()(const edits::SetBodyDamping& change) const
    {
        b3Body_SetLinearDamping(body(change.body).id, change.linear);
        b3Body_SetAngularDamping(body(change.body).id, change.angular);
    }

    void operator()(const edits::SetBodyGravityScale& change) const
    {
        b3Body_SetGravityScale(body(change.body).id, change.scale);
    }

    void operator()(const edits::SetBodyMotionQuality& change) const
    {
        b3Body_SetBullet(body(change.body).id, change.quality == MotionQuality::CONTINUOUS);
    }

    void operator()(const edits::SetBodyMotionLocks& change) const
    {
        b3Body_SetMotionLocks(body(change.body).id, to_box3d(change.locks));
    }

    void operator()(const edits::SetBodySleepAllowed& change) const
    {
        b3Body_EnableSleep(body(change.body).id, change.allowed);
    }

    void operator()(const edits::SetBodyAwake& change) const
    {
        b3Body_SetAwake(body(change.body).id, change.awake);
    }

    // Colliders.
    void operator()(const edits::CreateCollider& change) const
    {
        backend.create_collider(change, pending);
        body(change.body).mass_changed = true;
    }

    void operator()(const edits::DestroyCollider& change) const
    {
        const RigidBodyHandle owner = backend.m_colliders[Box3dBackend::slot(change.collider)].body;
        backend.destroy_collider(change.collider, pending);
        if (b3Body_IsValid(body(owner).id))
        {
            body(owner).mass_changed = true;
        }
    }

    void operator()(const edits::SetColliderPose& change) const
    {
        // Box3D cannot move a shape on its body: the collider's shapes are made again at the new pose.
        const ColliderRecord* record = find(pending, change.collider);
        if (record == nullptr)
        {
            return;
        }
        const ColliderEntry& entry = backend.m_colliders[Box3dBackend::slot(change.collider)];
        const edits::CreateCollider again{.collider = change.collider, .body = entry.body, .description = record->configuration, .unit_mass = record->unit_mass};
        backend.destroy_collider(change.collider, pending);
        backend.create_collider(again, pending);
        body(again.body).mass_changed = true;
    }

    void operator()(const edits::SetColliderMaterial& change) const
    {
        const b3SurfaceMaterial material{.friction = change.material.friction.dynamic_coefficient,
                                         .restitution = change.material.restitution,
                                         .rollingResistance = change.material.friction.rolling};
        for (const b3ShapeId shape : backend.m_colliders[Box3dBackend::slot(change.collider)].shapes)
        {
            b3Shape_SetSurfaceMaterial(shape, material);
        }
    }

    void operator()(const edits::SetColliderDensity& change) const
    {
        ColliderEntry& entry = backend.m_colliders[Box3dBackend::slot(change.collider)];
        for (const b3ShapeId shape : entry.shapes)
        {
            b3Shape_SetDensity(shape, change.density, true);
        }
        body(entry.body).mass_changed = true;
    }

    void operator()(const edits::SetColliderFilter& change) const
    {
        ColliderEntry& entry = backend.m_colliders[Box3dBackend::slot(change.collider)];
        entry.filter = change.filter;
        for (const b3ShapeId shape : entry.shapes)
        {
            b3Shape_SetFilter(shape, to_box3d(change.filter), true);
        }
    }

    void operator()(const edits::SetColliderContactEvents& change) const
    {
        ColliderEntry& entry = backend.m_colliders[Box3dBackend::slot(change.collider)];
        entry.contact_events = change.enabled;
        for (const b3ShapeId shape : entry.shapes)
        {
            b3Shape_EnableHitEvents(shape, change.enabled);
        }
    }

    // Joints.
    void operator()(const edits::CreateJoint& change) const
    {
        backend.create_joint(change.joint, change.description, pending);
    }

    void operator()(const edits::DestroyJoint& change) const
    {
        JointEntry& entry = backend.m_joints[Box3dBackend::slot(change.joint)];
        backend.destroy_joint(entry);
        entry = {};
    }

    void operator()(const edits::UpdateJoint& change) const
    {
        // Made again from its frames, unless it is disabled or broken.
        JointEntry& entry = backend.m_joints[Box3dBackend::slot(change.joint)];
        const bool existed = entry.exists;
        backend.destroy_joint(entry);
        entry.breakable = std::visit([](const auto& joint) { return std::isfinite(joint.breaking.force) || std::isfinite(joint.breaking.torque); }, change.description);
        if (existed)
        {
            backend.make_joint(entry, change.description);
        }
    }

    void operator()(const edits::SetJointEnabled& change) const
    {
        JointEntry& entry = backend.m_joints[Box3dBackend::slot(change.joint)];
        const JointRecord* record = find(pending, change.joint);
        if (change.enabled && !entry.exists && record != nullptr)
        {
            backend.make_joint(entry, record->configuration);
        }
        else if (!change.enabled)
        {
            backend.destroy_joint(entry);
        }
    }

    // Triggers.
    void operator()(const edits::CreateTrigger& change) const
    {
        backend.create_trigger(change, pending);
    }

    void operator()(const edits::DestroyTrigger& change) const
    {
        backend.destroy_trigger(change.trigger, pending);
    }

    void operator()(const edits::SetTriggerPose& change) const
    {
        // A trigger on a body of its own moves its body; one on a body is made again at the new pose.
        TriggerEntry& entry = backend.m_triggers[Box3dBackend::slot(change.trigger)];
        if (entry.has_own_body)
        {
            b3Body_SetTransform(entry.own_body, to_box3d(change.pose.position), to_box3d(change.pose.orientation));
            return;
        }
        const TriggerRecord* record = find(pending, change.trigger);
        if (record != nullptr)
        {
            const edits::CreateTrigger again{.trigger = change.trigger, .description = record->configuration};
            backend.destroy_trigger(change.trigger, pending);
            backend.create_trigger(again, pending);
        }
    }

    void operator()(const edits::SetTriggerFilter& change) const
    {
        for (const b3ShapeId shape : backend.m_triggers[Box3dBackend::slot(change.trigger)].shapes)
        {
            b3Shape_SetFilter(shape, to_box3d(change.filter), true);
        }
    }

    // Soft bodies are the abstraction's (XPBD, stepped by the framework after Box3D's step): nothing for Box3D.
    template<class Change>
        requires requires(const Change& change) { change.soft_body; }
    void operator()(const Change& /*change*/) const
    {
    }
};

// ===================================================================================================== Box3dBackend

Box3dBackend::Box3dBackend(const PhysicsWorldDescription& description)
    : m_tasks{std::make_unique<TaskBridge>()}
{
    // Box3D holds a fixed number of worlds.
    if (b3GetWorldCount() >= B3_MAX_WORLDS)
    {
        throw_unsupported(PhysicsBackendKind::BOX3D, "create_physics_world", PhysicsCapability::WORLD_OBJECTS,
                          std::format("Box3D holds at most {} worlds at once", B3_MAX_WORLDS), "destroy a Box3D world first");
    }

    // The world as described; the task hooks when there is a scheduler with more than one thread.
    const SolverDescription& solver = description.solver;
    b3WorldDef definition = b3DefaultWorldDef();
    definition.gravity = to_box3d(description.gravity);
    definition.restitutionThreshold = solver.restitution_threshold;
    definition.hitEventThreshold = description.hit_event_speed;
    definition.contactHertz = solver.contact_hertz;
    definition.contactDampingRatio = solver.contact_damping_ratio;
    definition.contactSpeed = solver.maximum_push_speed;
    definition.enableSleep = description.sleep.enabled;
    definition.enableContinuous = true;
    definition.workerCount = 1;
    if (description.task_scheduler != nullptr && description.task_scheduler->concurrency() > 1)
    {
        m_tasks->scheduler = description.task_scheduler;
        definition.workerCount = std::min<std::uint32_t>(description.task_scheduler->concurrency(), B3_MAX_WORKERS);
        definition.enqueueTask = &enqueue_task;
        definition.finishTask = &finish_task;
        definition.userTaskContext = m_tasks.get();
    }
    m_world = b3CreateWorld(&definition);
    b3World_EnableWarmStarting(m_world, solver.warm_starting);
    m_sub_steps = static_cast<int>(solver.sub_steps);
    m_sleep_threshold = description.sleep.linear_threshold;

    // A static body joints to the world attach to.
    const b3BodyDef ground = b3DefaultBodyDef();
    m_ground = b3CreateBody(m_world, &ground);
}

Box3dBackend::~Box3dBackend()
{
    b3DestroyWorld(m_world);
}

auto
Box3dBackend::shape_owner(b3ShapeId shape) const noexcept -> const ShapeOwner*
{
    const auto index = static_cast<std::size_t>(shape.index1);
    if (shape.index1 < 0 || index >= m_shape_owners.size() || !b3Shape_IsValid(shape))
    {
        return nullptr;
    }
    const ShapeOwner& owner = m_shape_owners[index];
    return owner.collider || owner.trigger ? &owner : nullptr;
}

void
Box3dBackend::own_shape(b3ShapeId shape,
                        ShapeOwner owner)
{
    grown(m_shape_owners, static_cast<std::size_t>(shape.index1)) = owner;
}

void
Box3dBackend::disown_shape(b3ShapeId shape)
{
    if (shape.index1 >= 0 && static_cast<std::size_t>(shape.index1) < m_shape_owners.size())
    {
        m_shape_owners[static_cast<std::size_t>(shape.index1)] = {};
    }
}

void
Box3dBackend::check(const WorldEdit& edit,
                    const RecordedWorld& recorded) const
{
    // Shapes on bodies of their kinds, materials, joints, soft bodies: what Box3D cannot simulate is refused now.
    if (const auto* collider = std::get_if<edits::CreateCollider>(&edit))
    {
        check_shape(*collider->description.shape, collider->description.pose, recorded.body_kind(collider->body).value_or(BodyKind::DYNAMIC),
                    collider->description.round_shape_segments, "RigidBody::add_collider", {.rigid_body = collider->body});
        const Friction& friction = collider->description.material.friction;
        if (friction.static_coefficient != friction.dynamic_coefficient)
        {
            throw_unsupported(identity(), "RigidBody::add_collider", PhysicsCapability::SEPARATE_STATIC_FRICTION,
                              std::format("Box3D has one friction coefficient (static {} and dynamic {} given)", friction.static_coefficient, friction.dynamic_coefficient),
                              "give equal static and dynamic coefficients", {.rigid_body = collider->body});
        }
    }
    else if (const auto* material = std::get_if<edits::SetColliderMaterial>(&edit))
    {
        const Friction& friction = material->material.friction;
        if (friction.static_coefficient != friction.dynamic_coefficient)
        {
            throw_unsupported(identity(), "Collider::set_material", PhysicsCapability::SEPARATE_STATIC_FRICTION,
                              std::format("Box3D has one friction coefficient (static {} and dynamic {} given)", friction.static_coefficient, friction.dynamic_coefficient),
                              "give equal static and dynamic coefficients", {.collider = material->collider});
        }
    }
    else if (const auto* pose = std::get_if<edits::SetColliderPose>(&edit))
    {
        if (const std::shared_ptr<const Shape> shape = recorded.collider_shape(pose->collider))
        {
            check_shape(*shape, pose->pose, recorded.body_kind(recorded.collider_body(pose->collider).value_or(RigidBodyHandle{})).value_or(BodyKind::DYNAMIC), 0,
                        "Collider::set_pose", {.collider = pose->collider});
        }
    }
    else if (const auto* kind = std::get_if<edits::SetBodyKind>(&edit))
    {
        for (const ColliderHandle attached : recorded.body_colliders(kind->body))
        {
            if (const std::shared_ptr<const Shape> shape = recorded.collider_shape(attached))
            {
                check_shape(*shape, Transform{}, kind->kind, 0, "RigidBody::set_kind", {.rigid_body = kind->body, .collider = attached});
            }
        }
    }
    else if (const auto* joint = std::get_if<edits::CreateJoint>(&edit))
    {
        check_joint(joint->description, "PhysicsWorld::create_joint");
    }
    else if (const auto* update = std::get_if<edits::UpdateJoint>(&edit))
    {
        check_joint(update->description, "Joint::update");
    }
    else if (const auto* trigger = std::get_if<edits::CreateTrigger>(&edit))
    {
        check_shape(*trigger->description.shape, trigger->description.pose, BodyKind::STATIC, 16, "PhysicsWorld::create_trigger", {});
    }
}

void
Box3dBackend::apply(std::span<const WorldEdit> edits,
                    WorldSnapshot& pending)
{
    // In order; then the mass properties of the bodies whose colliders or settings changed, read back from Box3D.
    const EditApplier applier{.backend = *this, .pending = pending};
    for (const WorldEdit& edit : edits)
    {
        std::visit(applier, edit);
    }
    for (BodyEntry& entry : m_bodies)
    {
        if (entry.mass_changed && b3Body_IsValid(entry.id))
        {
            entry.mass_changed = false;
            apply_mass(entry.handle, pending);
        }
    }
}

void
Box3dBackend::apply_mass(RigidBodyHandle body,
                         WorldSnapshot& pending)
{
    RigidBodyRecord* record = find(pending, body);
    if (record == nullptr)
    {
        return;
    }
    const b3BodyId id = m_bodies[slot(body)].id;
    if (record->configuration.kind == BodyKind::DYNAMIC)
    {
        if (record->configuration.mass_properties)
        {
            b3Body_SetMassData(id, to_box3d(*record->configuration.mass_properties));
        }
        else
        {
            b3Body_ApplyMassFromShapes(id);
        }
        record->mass = from_box3d(b3Body_GetMassData(id));
    }
    else
    {
        record->mass = {};
    }
    record->configuration_changed = true;
}

void
Box3dBackend::simulate(float seconds,
                       WorldSnapshot& pending)
{
    // The step, then what it reports.
    ++m_step;
    m_tasks->used.store(0, std::memory_order_relaxed);
    b3World_Step(m_world, seconds, m_sub_steps);
    capture_bodies(pending);
    capture_contacts(pending);
    capture_triggers(pending);
    capture_joints(pending);

    // Box3D's profile is in milliseconds.
    const b3Profile profile = b3World_GetProfile(m_world);
    StepStatistics& statistics = pending.statistics;
    statistics.phase_seconds[std::to_underlying(StepPhase::BROAD_PHASE)] = static_cast<double>(profile.pairs) / 1000.0;
    statistics.phase_seconds[std::to_underlying(StepPhase::NARROW_PHASE)] = static_cast<double>(profile.collide) / 1000.0;
    statistics.phase_seconds[std::to_underlying(StepPhase::SOLVER)] = static_cast<double>(profile.solve) / 1000.0;
    statistics.pairs = static_cast<std::uint32_t>(b3World_GetCounters(m_world).contactCount);
    statistics.contacts = 0;
    for (const ContactInfo& contact : pending.contacts)
    {
        statistics.contacts += contact.manifold.point_count;
    }
}

void
Box3dBackend::apply_soft_body_corrections(std::span<const BodyCorrection> corrections,
                                          WorldSnapshot& pending)
{
    // A sleeping body wakes only for an impulse that would move it faster than its sleep speed (a cloth resting on a
    // sleeping box lets it sleep). An awake one moves where the soft bodies left it (about its center of mass), then
    // takes the impulses through its center; its pose and velocities read back.
    for (const BodyCorrection& correction : corrections)
    {
        RigidBodyRecord* record = find(pending, correction.body);
        const std::uint32_t body_slot = slot(correction.body);
        if (record == nullptr || body_slot >= m_bodies.size() || !(record->mass.mass > 0.0F))
        {
            continue;
        }
        const b3BodyId id = m_bodies[body_slot].id;
        if (!b3Body_IsAwake(id) && !(length(correction.linear_impulse) / record->mass.mass > m_sleep_threshold))
        {
            continue;
        }
        Transform pose = record->state.pose;
        const Vector3 center = transform_point(pose, record->mass.center_of_mass) + correction.translation;
        pose.orientation = normalized(correction.rotation * pose.orientation);
        pose.position = center - rotate(pose.orientation, record->mass.center_of_mass);
        b3Body_SetTransform(id, to_box3d(pose.position), to_box3d(pose.orientation));
        b3Body_ApplyLinearImpulseToCenter(id, to_box3d(correction.linear_impulse), true);
        b3Body_ApplyAngularImpulse(id, to_box3d(correction.angular_impulse), true);
        record->state.pose = pose;
        record->state.linear_velocity = from_box3d(b3Body_GetLinearVelocity(id));
        record->state.angular_velocity = from_box3d(b3Body_GetAngularVelocity(id));
        if (!record->state.awake)
        {
            record->state.awake = true;
            pending.sleep_events.push_back({.kind = SleepEventKind::WOKE_UP, .body = correction.body});
        }
    }
}

void
Box3dBackend::capture_bodies(WorldSnapshot& pending)
{
    // Moved bodies: their poses and velocities; sleep and waking from the move events.
    const b3BodyEvents events = b3World_GetBodyEvents(m_world);
    for (int index = 0; index < events.moveCount; ++index)
    {
        const b3BodyMoveEvent& move = events.moveEvents[index];
        if (move.bodyId.index1 < 0 || static_cast<std::size_t>(move.bodyId.index1) >= m_body_owners.size())
        {
            continue;
        }
        const RigidBodyHandle handle = m_body_owners[static_cast<std::size_t>(move.bodyId.index1)];
        RigidBodyRecord* record = find(pending, handle);
        if (record == nullptr)
        {
            continue;
        }
        const bool was_awake = record->state.awake;
        record->state.pose = from_box3d(move.transform);
        if (move.fellAsleep)
        {
            record->state.linear_velocity = {};
            record->state.angular_velocity = {};
            record->state.awake = false;
            pending.sleep_events.push_back({.kind = SleepEventKind::FELL_ASLEEP, .body = handle});
        }
        else
        {
            record->state.linear_velocity = from_box3d(b3Body_GetLinearVelocity(move.bodyId));
            record->state.angular_velocity = from_box3d(b3Body_GetAngularVelocity(move.bodyId));
            record->state.awake = true;
            if (!was_awake)
            {
                pending.sleep_events.push_back({.kind = SleepEventKind::WOKE_UP, .body = handle});
            }
        }
    }
}

void
Box3dBackend::capture_contacts(WorldSnapshot& pending)
{
    const b3ContactEvents events = b3World_GetContactEvents(m_world);
    const auto colliders_of = [this](b3ShapeId a, b3ShapeId b) -> std::optional<std::pair<ShapeOwner, ShapeOwner>> {
        const ShapeOwner* first = shape_owner(a);
        const ShapeOwner* second = shape_owner(b);
        if (first == nullptr || second == nullptr || !first->collider || !second->collider)
        {
            return std::nullopt;
        }
        return std::pair{*first, *second};
    };
    const auto reported = [this](ColliderHandle first, ColliderHandle second) {
        return m_colliders[slot(first)].contact_events || m_colliders[slot(second)].contact_events;
    };

    // Begins: a pair touches from its first pair of shapes on.
    for (int index = 0; index < events.beginCount; ++index)
    {
        const b3ContactBeginTouchEvent& begin = events.beginEvents[index];
        const auto owners = colliders_of(begin.shapeIdA, begin.shapeIdB);
        if (!owners || owners->first.collider == owners->second.collider)
        {
            continue;
        }
        const bool a_first = owners->first.collider < owners->second.collider;
        const ColliderHandle first = a_first ? owners->first.collider : owners->second.collider;
        const ColliderHandle second = a_first ? owners->second.collider : owners->first.collider;
        TouchingPair& pair = m_touching[pair_key(slot(first), slot(second))];
        pair.first = first;
        pair.second = second;
        pair.contacts.push_back(begin.contactId);
        if (pair.contacts.size() == 1 && reported(first, second))
        {
            ContactEvent event{.kind = ContactEventKind::BEGIN, .first = first, .second = second};
            if (b3Contact_IsValid(begin.contactId))
            {
                const b3ContactData data = b3Contact_GetData(begin.contactId);
                if (data.manifoldCount > 0 && data.manifolds[0].pointCount > 0)
                {
                    const ContactManifold manifold = to_manifold(data.manifolds[0], data.shapeIdA, data.shapeIdB, !a_first);
                    event.point = manifold.points[0].position;
                    event.normal = manifold.normal;
                }
            }
            pending.contact_events.push_back(event);
        }
    }

    // Ends: a pair stops touching with its last pair of shapes.
    for (int index = 0; index < events.endCount; ++index)
    {
        const b3ContactEndTouchEvent& end = events.endEvents[index];
        const auto owners = colliders_of(end.shapeIdA, end.shapeIdB);
        if (!owners || owners->first.collider == owners->second.collider)
        {
            continue;
        }
        const ColliderHandle first = std::min(owners->first.collider, owners->second.collider);
        const ColliderHandle second = std::max(owners->first.collider, owners->second.collider);
        const auto found = m_touching.find(pair_key(slot(first), slot(second)));
        if (found == m_touching.end())
        {
            continue;
        }
        std::erase_if(found->second.contacts, [&end](b3ContactId contact) { return contact.index1 == end.contactId.index1 && contact.generation == end.contactId.generation; });
        if (found->second.contacts.empty())
        {
            m_touching.erase(found);
            if (reported(first, second))
            {
                pending.contact_events.push_back({.kind = ContactEventKind::END, .first = first, .second = second});
            }
        }
    }

    // Hits.
    for (int index = 0; index < events.hitCount; ++index)
    {
        const b3ContactHitEvent& hit = events.hitEvents[index];
        const auto owners = colliders_of(hit.shapeIdA, hit.shapeIdB);
        if (!owners || owners->first.collider == owners->second.collider || !reported(owners->first.collider, owners->second.collider))
        {
            continue;
        }
        const bool a_first = owners->first.collider < owners->second.collider;
        pending.contact_events.push_back({.kind = ContactEventKind::HIT,
                                          .first = a_first ? owners->first.collider : owners->second.collider,
                                          .second = a_first ? owners->second.collider : owners->first.collider,
                                          .point = from_box3d(hit.point),
                                          .normal = a_first ? from_box3d(hit.normal) : -from_box3d(hit.normal),
                                          .approach_speed = hit.approachSpeed});
    }

    // The contact list: a manifold of each touching pair, in handle order.
    for (const auto& [key, pair] : m_touching)
    {
        for (const b3ContactId contact : pair.contacts)
        {
            if (!b3Contact_IsValid(contact))
            {
                continue;
            }
            const b3ContactData data = b3Contact_GetData(contact);
            if (data.manifoldCount == 0)
            {
                continue;
            }
            const ShapeOwner* owner_a = shape_owner(data.shapeIdA);
            const bool flip = owner_a == nullptr || owner_a->collider != pair.first;
            pending.contacts.push_back(
                {.collider = pair.first, .other = pair.second, .other_body = m_colliders[slot(pair.second)].body, .manifold = to_manifold(data.manifolds[0], data.shapeIdA, data.shapeIdB, flip)});
            break;
        }
    }
    std::ranges::sort(pending.contacts, [](const ContactInfo& first, const ContactInfo& second) {
        return std::pair{first.collider, first.other} < std::pair{second.collider, second.other};
    });
}

void
Box3dBackend::capture_triggers(WorldSnapshot& pending)
{
    // Enter with the first shape of a collider in a trigger, exit with its last; stay every later step.
    const b3SensorEvents events = b3World_GetSensorEvents(m_world);
    for (int index = 0; index < events.beginCount; ++index)
    {
        const b3SensorBeginTouchEvent& begin = events.beginEvents[index];
        const ShapeOwner* sensor = shape_owner(begin.sensorShapeId);
        const ShapeOwner* visitor = shape_owner(begin.visitorShapeId);
        if (sensor == nullptr || visitor == nullptr || !sensor->trigger || !visitor->collider)
        {
            continue;
        }
        TriggerOverlap& overlap = m_overlaps[pair_key(slot(sensor->trigger), slot(visitor->collider))];
        if (overlap.shapes++ == 0)
        {
            overlap = {.trigger = sensor->trigger, .collider = visitor->collider, .shapes = 1, .entered = m_step};
            pending.trigger_events.push_back({.kind = TriggerEventKind::ENTER, .trigger = sensor->trigger, .collider = visitor->collider});
        }
    }
    for (int index = 0; index < events.endCount; ++index)
    {
        const b3SensorEndTouchEvent& end = events.endEvents[index];
        const ShapeOwner* sensor = shape_owner(end.sensorShapeId);
        const ShapeOwner* visitor = shape_owner(end.visitorShapeId);
        if (sensor == nullptr || visitor == nullptr || !sensor->trigger || !visitor->collider)
        {
            continue;
        }
        const auto found = m_overlaps.find(pair_key(slot(sensor->trigger), slot(visitor->collider)));
        if (found != m_overlaps.end() && --found->second.shapes == 0)
        {
            pending.trigger_events.push_back({.kind = TriggerEventKind::EXIT, .trigger = sensor->trigger, .collider = visitor->collider});
            m_overlaps.erase(found);
        }
    }
    const std::size_t stays = pending.trigger_events.size();
    for (const auto& [key, overlap] : m_overlaps)
    {
        if (overlap.entered < m_step)
        {
            pending.trigger_events.push_back({.kind = TriggerEventKind::STAY, .trigger = overlap.trigger, .collider = overlap.collider});
        }
    }
    std::sort(pending.trigger_events.begin() + static_cast<std::ptrdiff_t>(stays), pending.trigger_events.end(),
              [](const TriggerEvent& first, const TriggerEvent& second) { return std::pair{first.trigger, first.collider} < std::pair{second.trigger, second.collider}; });
}

void
Box3dBackend::capture_joints(WorldSnapshot& pending)
{
    // Joints past their thresholds break (Box3D reports them); the others report their forces.
    const b3JointEvents events = b3World_GetJointEvents(m_world);
    for (int index = 0; index < events.count; ++index)
    {
        const b3JointId id = events.jointEvents[index].jointId;
        if (id.index1 < 0 || static_cast<std::size_t>(id.index1) >= m_joint_owners.size())
        {
            continue;
        }
        const JointHandle handle = m_joint_owners[static_cast<std::size_t>(id.index1)];
        JointRecord* record = find(pending, handle);
        if (record == nullptr || !m_joints[slot(handle)].exists || !same_id(m_joints[slot(handle)].id, id) || !m_joints[slot(handle)].breakable)
        {
            continue;
        }
        const Vector3 force = from_box3d(b3Joint_GetConstraintForce(id));
        const Vector3 torque = from_box3d(b3Joint_GetConstraintTorque(id));
        destroy_joint(m_joints[slot(handle)]);
        record->state = {.enabled = false, .broken = true, .force = force, .torque = torque};
        pending.joint_events.push_back({.kind = JointEventKind::BROKEN, .joint = handle, .force = length(force), .torque = length(torque)});
    }
    for (JointEntry& entry : m_joints)
    {
        if (entry.exists)
        {
            if (JointRecord* record = find(pending, entry.handle))
            {
                record->state.force = from_box3d(b3Joint_GetConstraintForce(entry.id));
                record->state.torque = from_box3d(b3Joint_GetConstraintTorque(entry.id));
            }
        }
    }
}

void
Box3dBackend::end_touches_of(ColliderHandle collider,
                             WorldSnapshot& pending)
{
    // The pairs of a collider whose shapes go: they end now (Box3D's own end events will not name it any more).
    for (auto pair = m_touching.begin(); pair != m_touching.end();)
    {
        if (pair->second.first == collider || pair->second.second == collider)
        {
            if (m_colliders[slot(pair->second.first)].contact_events || m_colliders[slot(pair->second.second)].contact_events)
            {
                pending.contact_events.push_back({.kind = ContactEventKind::END, .first = pair->second.first, .second = pair->second.second});
            }
            pair = m_touching.erase(pair);
        }
        else
        {
            ++pair;
        }
    }
}

void
Box3dBackend::end_overlaps_of(ColliderHandle collider,
                              TriggerHandle trigger,
                              WorldSnapshot& pending)
{
    // The overlaps of a collider or a trigger that goes: they exit now.
    for (auto overlap = m_overlaps.begin(); overlap != m_overlaps.end();)
    {
        if ((collider && overlap->second.collider == collider) || (trigger && overlap->second.trigger == trigger))
        {
            pending.trigger_events.push_back({.kind = TriggerEventKind::EXIT, .trigger = overlap->second.trigger, .collider = overlap->second.collider});
            overlap = m_overlaps.erase(overlap);
        }
        else
        {
            ++overlap;
        }
    }
}

// ===================================================================================================== the module's functions

auto
unavailable_reason() -> std::string
{
    return {};
}

auto
create_physics_world(const PhysicsWorldDescription& description) -> std::unique_ptr<PhysicsWorld>
{
    // The description is checked before Box3D sees it.
    check_world_description(description, PhysicsBackendKind::BOX3D);
    return std::make_unique<SteppedWorld>(description, std::make_unique<Box3dBackend>(description));
}

} // namespace gfx::physics::box3d_backend
