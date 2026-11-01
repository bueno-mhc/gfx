// GpuXpbdBackend: the edits become patches of the GPU body arrays, the colliders' data and the joints' data; a step is one
// command buffer (patches, the step's start backed up, the contacts' detection and the coloring of joints and contacts,
// substeps of prediction, joint and contact projection and velocity update with the contacts' velocity pass, the joints'
// forces, sleep), submitted and waited for on the stepping thread, replayed from its backed-up start when a buffer or the
// recorded passes overflowed, and read back into the pending snapshot (bodies, joint states, broken joints).
module gfx.physics.gpu_xpbd;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import gfx.interface;
import gfx.core.errors;
import gfx.core.handles;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.handles;
import gfx.physics.interface;
import gfx.physics.shapes;
import gfx.physics.world;
import :settings;
import :resources;
import :narrow_phase;
import :solver;
import :backend;

namespace gfx::physics::gpu_xpbd
{

namespace
{

/// The mass properties a body moves with: a dynamic body's (a unit mass and inertia when its colliders give none), none
/// for the others (as the reference backend).
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

/// 1 along the world axes @p locks leaves free, 0 along the others (linear: the first three bits; angular: the last three).
[[nodiscard]] auto
freedom(MotionLock locks,
        bool angular) noexcept -> Vector3
{
    const auto free = [locks](MotionLock lock) { return has_any(locks, lock) ? 0.0F : 1.0F; };
    return angular ? Vector3{.x = free(MotionLock::ANGULAR_X), .y = free(MotionLock::ANGULAR_Y), .z = free(MotionLock::ANGULAR_Z)}
                   : Vector3{.x = free(MotionLock::LINEAR_X), .y = free(MotionLock::LINEAR_Y), .z = free(MotionLock::LINEAR_Z)};
}

/// The inverse of @p inertia, or zero when it is singular (the body does not turn).
[[nodiscard]] auto
inverse_inertia(const Matrix3& inertia) noexcept -> Matrix3
{
    if (!(std::abs(determinant(inertia)) > std::numeric_limits<float>::min()))
    {
        return Matrix3{.columns = {Vector3{}, Vector3{}, Vector3{}}};
    }
    return inverse(inertia);
}

/// The world inverse inertia of a body of local inverse inertia @p local at @p orientation, 0 about the axes
/// @p angular_freedom locks.
[[nodiscard]] auto
world_inverse_inertia(const Matrix3& local,
                      const Quaternion& orientation,
                      Vector3 angular_freedom) noexcept -> Matrix3
{
    const Matrix3 rotation = Matrix3::from_rotation(orientation);
    const Matrix3 world = rotation * local * transpose(rotation);
    return Matrix3{.columns = {component_multiply(world.columns[0], angular_freedom) * angular_freedom.x,
                               component_multiply(world.columns[1], angular_freedom) * angular_freedom.y,
                               component_multiply(world.columns[2], angular_freedom) * angular_freedom.z}};
}

[[nodiscard]] constexpr auto
vector(Vector3 value,
       float w = 0.0F) noexcept -> GpuVector
{
    return {value.x, value.y, value.z, w};
}

[[nodiscard]] constexpr auto
vector(const Quaternion& value) noexcept -> GpuVector
{
    return {value.x, value.y, value.z, value.w};
}

/// The diagonal (xx, yy, zz) of a symmetric matrix, with @p w.
[[nodiscard]] constexpr auto
diagonal(const Matrix3& matrix,
         float w = 0.0F) noexcept -> GpuVector
{
    return {matrix.columns[0].x, matrix.columns[1].y, matrix.columns[2].z, w};
}

/// The off-diagonal terms (xy, xz, yz) of a symmetric matrix.
[[nodiscard]] constexpr auto
off_diagonal(const Matrix3& matrix) noexcept -> GpuVector
{
    return {matrix.columns[1].x, matrix.columns[2].x, matrix.columns[2].y, 0.0F};
}

/// A vector of the readback at @p index of the array starting at @p offset.
[[nodiscard]] auto
read_vector(std::span<const std::byte> bytes,
            std::size_t offset,
            std::size_t index) noexcept -> GpuVector
{
    GpuVector value{};
    std::memcpy(value.data(), bytes.data() + offset + index * sizeof(GpuVector), sizeof(GpuVector));
    return value;
}

[[nodiscard]] constexpr auto
vector3(const GpuVector& value) noexcept -> Vector3
{
    return {.x = value[0], .y = value[1], .z = value[2]};
}

[[nodiscard]] constexpr auto
quaternion(const GpuVector& value) noexcept -> Quaternion
{
    return {.x = value[0], .y = value[1], .z = value[2], .w = value[3]};
}

/// The wall time between two instants, in seconds.
[[nodiscard]] auto
seconds_between(std::chrono::steady_clock::time_point start,
                std::chrono::steady_clock::time_point end) noexcept -> double
{
    return std::chrono::duration<double>(end - start).count();
}

/// The index of @p phase in StepStatistics::phase_seconds.
[[nodiscard]] constexpr auto
phase(StepPhase value) noexcept -> std::size_t
{
    return static_cast<std::size_t>(std::to_underlying(value));
}

/// How far a shape reaches from its frame's origin.
[[nodiscard]] auto
reach(const Shape& shape) noexcept -> float
{
    const Aabb bounds = shape.bounds();
    return length(component_maximum(component_absolute(bounds.minimum), component_absolute(bounds.maximum)));
}

/// What decides the static tree: a static collider's data and its body's pose (no padding: compared as bytes).
struct StaticSignature
{
    GpuCollider collider{};
    GpuVector center{};
    GpuVector orientation{};
};
static_assert(sizeof(StaticSignature) == sizeof(GpuCollider) + 2 * sizeof(GpuVector));

/// The bytes of a span (comparisons of GPU data, which has no padding).
template<class Element>
[[nodiscard]] auto
same_bytes(std::span<const Element> first,
           std::span<const Element> second) noexcept -> bool
{
    return first.size() == second.size() && (first.empty() || std::memcmp(first.data(), second.data(), first.size_bytes()) == 0);
}

} // namespace

// ===================================================================================================== the edits

/// Marks what each edit changed of the bodies, colliders and joints (forces and impulses come after: they need the final
/// centers).
struct ChangeMarker
{
    GpuXpbdBackend& backend;
    const WorldSnapshot& pending;

    void operator()(const edits::CreateRigidBody& change) const
    {
        BodyEntry& entry = backend.body_entry(change.body);
        entry = {};
        entry.handle = change.body;
        entry.created = true;
    }

    void operator()(const edits::DestroyRigidBody& change) const
    {
        BodyEntry& entry = backend.body_entry(change.body);
        entry = {};
        entry.destroyed = true;
        backend.colliders_changed(true, true);
    }

    void operator()(const edits::SetBodyPose& change) const
    {
        BodyEntry& entry = backend.body_entry(change.body);
        entry.pose_set = true;
        entry.woken = true;
    }

    void operator()(const edits::SetBodyLinearVelocity& change) const
    {
        set_velocity(change.body);
    }

    void operator()(const edits::SetBodyAngularVelocity& change) const
    {
        set_velocity(change.body);
    }

    void operator()(const edits::MoveKinematicBody& change) const
    {
        set_velocity(change.body);
    }

    void operator()(const edits::SetBodyKind& change) const
    {
        reconfigure(change.body, true);
        backend.colliders_changed(true, true);
    }

    void operator()(const edits::SetBodyMassProperties& change) const
    {
        reconfigure(change.body, true);
        backend.colliders_changed(false, false);
    }

    void operator()(const edits::SetBodyDamping& change) const
    {
        reconfigure(change.body, false);
    }

    void operator()(const edits::SetBodyGravityScale& change) const
    {
        reconfigure(change.body, false);
    }

    void operator()(const edits::SetBodyMotionLocks& change) const
    {
        reconfigure(change.body, false);
    }

    void operator()(const edits::SetBodySleepAllowed& change) const
    {
        reconfigure(change.body, false);
    }

    void operator()(const edits::SetBodyAwake& change) const
    {
        BodyEntry& entry = backend.body_entry(change.body);
        entry.awake_set = true;
        entry.woken = change.awake;
    }

    void operator()(const edits::ApplyForce& change) const
    {
        push(change.body);
    }

    void operator()(const edits::ApplyTorque& change) const
    {
        push(change.body);
    }

    void operator()(const edits::ApplyLinearImpulse& change) const
    {
        push(change.body);
    }

    void operator()(const edits::ApplyAngularImpulse& change) const
    {
        push(change.body);
    }

    // Colliders: their data and lists; a body's mass (center) follows its colliders.
    void operator()(const edits::CreateCollider& change) const
    {
        ColliderEntry& entry = backend.collider_entry(change.collider);
        entry = {.handle = change.collider, .body = change.body, .shape = backend.shape_index(change.description.shape)};
        reconfigure(change.body, true);
        backend.colliders_changed(true, false);
    }

    void operator()(const edits::DestroyCollider& change) const
    {
        ColliderEntry& entry = backend.collider_entry(change.collider);
        if (entry.body)
        {
            reconfigure(entry.body, true);
        }
        entry = {};
        backend.colliders_changed(true, false);
    }

    void operator()(const edits::SetColliderPose& change) const
    {
        collider_changed(change.collider, true);
    }

    void operator()(const edits::SetColliderDensity& change) const
    {
        collider_changed(change.collider, true);
    }

    void operator()(const edits::SetColliderMaterial& change) const
    {
        collider_changed(change.collider, false);
    }

    void operator()(const edits::SetColliderFilter& change) const
    {
        collider_changed(change.collider, false);
    }

    // Joints: their frames, fixed at their creation from their bodies' poses (their parts and states come from the records
    // each step); a joint created, changed or enabled wakes its bodies.
    void operator()(const edits::CreateJoint& change) const
    {
        const auto [first, second] = joint_bodies(change.description);
        const auto pose_of = [this](RigidBodyHandle body) {
            const RigidBodyRecord* record = body ? find(pending, body) : nullptr;
            return record != nullptr ? record->state.pose : Transform{};
        };
        backend.joint_entry(change.joint) = {.handle = change.joint, .frames = joint_frames(change.description, pose_of(first), pose_of(second))};
        wake(change.description);
    }

    void operator()(const edits::DestroyJoint& change) const
    {
        backend.joint_entry(change.joint) = {};
    }

    void operator()(const edits::UpdateJoint& change) const
    {
        wake(change.description);
    }

    void operator()(const edits::SetJointEnabled& change) const
    {
        if (const JointRecord* record = find(pending, change.joint))
        {
            wake(record->configuration);
        }
    }

    // The rest: the records are all the backend needs (the world's settings, motion quality, contact events...).
    template<class Change>
    void operator()(const Change& /*change*/) const
    {
    }

private:
    void set_velocity(RigidBodyHandle body) const
    {
        BodyEntry& entry = backend.body_entry(body);
        entry.velocity_set = true;
        entry.woken = true;
    }

    void reconfigure(RigidBodyHandle body,
                     bool wakes) const
    {
        BodyEntry& entry = backend.body_entry(body);
        entry.reconfigured = true;
        entry.woken = entry.woken || wakes;
    }

    void wake(const JointDescription& joint) const
    {
        const auto [first, second] = joint_bodies(joint);
        for (const RigidBodyHandle body : {first, second})
        {
            if (body)
            {
                backend.body_entry(body).woken = true;
            }
        }
    }

    void push(RigidBodyHandle body) const
    {
        BodyEntry& entry = backend.body_entry(body);
        entry.pushed = true;
        entry.woken = true;
    }

    void collider_changed(ColliderHandle collider,
                          bool mass) const
    {
        const ColliderEntry& entry = backend.collider_entry(collider);
        if (mass && entry.body)
        {
            reconfigure(entry.body, true);
        }
        backend.colliders_changed(false, false);
    }
};

/// The center of mass a body's patch will have: from its record when its pose, mass or kind changed, else as the GPU has it.
[[nodiscard]] auto
patched_center(const BodyEntry& entry,
               const RigidBodyRecord& record) noexcept -> Vector3
{
    if (entry.created || entry.pose_set || entry.reconfigured)
    {
        const Transform& pose = record.state.pose;
        return pose.position + rotate(pose.orientation, effective_mass(record).center_of_mass);
    }
    return entry.center;
}

/// Accumulates the forces, torques and impulses of the edits (about the centers the patches will have).
struct PushCollector
{
    GpuXpbdBackend& backend;
    const WorldSnapshot& pending;

    void operator()(const edits::ApplyForce& change) const
    {
        BodyEntry& entry = backend.body_entry(change.body);
        entry.force += change.force;
        if (change.point)
        {
            entry.torque += cross(*change.point - center(change.body, entry), change.force);
        }
    }

    void operator()(const edits::ApplyTorque& change) const
    {
        backend.body_entry(change.body).torque += change.torque;
    }

    void operator()(const edits::ApplyLinearImpulse& change) const
    {
        BodyEntry& entry = backend.body_entry(change.body);
        entry.linear_impulse += change.impulse;
        if (change.point)
        {
            entry.angular_impulse += cross(*change.point - center(change.body, entry), change.impulse);
        }
    }

    void operator()(const edits::ApplyAngularImpulse& change) const
    {
        backend.body_entry(change.body).angular_impulse += change.impulse;
    }

    template<class Change>
    void operator()(const Change& /*change*/) const
    {
    }

private:
    [[nodiscard]] auto center(RigidBodyHandle body,
                              const BodyEntry& entry) const noexcept -> Vector3
    {
        const RigidBodyRecord* record = find(pending, body);
        return record != nullptr ? patched_center(entry, *record) : entry.center;
    }
};

// ===================================================================================================== GpuXpbdBackend

GpuXpbdBackend::GpuXpbdBackend(OwnedDevice owned,
                               Device& device,
                               const GpuXpbdDescription& settings)
    : m_owned{std::move(owned)}
    , m_device{device}
    , m_capabilities{gpu_xpbd::capabilities(device, settings.variants)}
    , m_queue{&device.queue(m_capabilities.queue_type)}
    , m_command_pool{device.create_command_pool({.queue_type = m_capabilities.queue_type, .debug_name = "GPU physics steps"})}
    , m_command_buffer{m_command_pool->allocate(CommandBufferLevel::PRIMARY, "GPU physics step")}
    , m_timeline{device.create_timeline_semaphore({.initial_value = 0, .debug_name = "GPU physics steps"})}
    , m_pipelines{device}
    , m_solver{device, m_capabilities, settings}
{
}

GpuXpbdBackend::~GpuXpbdBackend()
{
    // The last step's buffers stay alive until the GPU is done with them; destructors never throw.
    try
    {
        if (m_timeline != nullptr && m_submitted > 0)
        {
            (void)m_timeline->wait(m_submitted);
        }
    }
    catch (...)
    {
        // A lost device: nothing waits any more.
    }
}

auto
GpuXpbdBackend::pipeline() const noexcept -> PipelineKinds
{
    return {.broad_phase = BroadPhaseKind::BACKEND, .narrow_phase = NarrowPhaseKind::BACKEND, .solver = SolverKind::XPBD, .integrator = IntegratorKind::XPBD};
}

void
GpuXpbdBackend::check(const WorldEdit& edit,
                      const RecordedWorld& /*recorded*/) const
{
    // What later phases bring is refused when recorded, never ignored; user constraints are refused for good. Every joint
    // kind is simulated.
    if (const auto* collider = std::get_if<edits::CreateCollider>(&edit))
    {
        GpuNarrowPhase::check_shape(*collider->description.shape);
    }
    if (std::holds_alternative<edits::CreateTrigger>(edit))
    {
        throw_unsupported(identity(), "PhysicsWorld::create_trigger", PhysicsCapability::WORLD_OBJECTS,
                          "the GPU backend does not simulate triggers yet (they arrive with phase G8 of docs/gpu-xpbd-design.md)",
                          "use another backend");
    }
    if (std::holds_alternative<edits::CreateSoftBody>(edit))
    {
        throw_unsupported(identity(), "PhysicsWorld::create_soft_body", PhysicsCapability::SOFT_BODIES,
                          "the GPU backend does not simulate soft bodies yet (they arrive with phase G7 of docs/gpu-xpbd-design.md)",
                          "use another backend");
    }
    if (std::holds_alternative<edits::CreateConstraint>(edit))
    {
        throw_unsupported(identity(), "PhysicsWorld::create_constraint", PhysicsCapability::USER_CONSTRAINTS,
                          "a user constraint's rows are C++ code (UserConstraint::rows()), which the GPU cannot run",
                          "use a joint, or the reference backend (PhysicsBackendKind::REFERENCE)");
    }
}

auto
GpuXpbdBackend::body_entry(RigidBodyHandle body) -> BodyEntry&
{
    const std::uint32_t index = slot(body);
    if (m_bodies.size() <= index)
    {
        m_bodies.resize(index + 1);
    }
    return m_bodies[index];
}

auto
GpuXpbdBackend::collider_entry(ColliderHandle collider) -> ColliderEntry&
{
    const std::uint32_t index = slot(collider);
    if (m_colliders.size() <= index)
    {
        m_colliders.resize(index + 1);
    }
    return m_colliders[index];
}

auto
GpuXpbdBackend::joint_entry(JointHandle joint) -> JointEntry&
{
    const std::uint32_t index = slot(joint);
    if (m_joints.size() <= index)
    {
        m_joints.resize(index + 1);
    }
    return m_joints[index];
}

void
GpuXpbdBackend::colliders_changed(bool lists,
                                  bool statics) noexcept
{
    m_colliders_dirty = true;
    m_lists_dirty = m_lists_dirty || lists;
    m_statics_dirty = m_statics_dirty || statics;
}

auto
GpuXpbdBackend::shape_index(const std::shared_ptr<const Shape>& shape) -> std::uint32_t
{
    // Each distinct shape once (kept alive: its address is the key); the narrow phase uploads the table before the step.
    if (const auto known = m_shape_indices.find(shape.get()); known != m_shape_indices.end())
    {
        return known->second;
    }
    const std::uint32_t index = m_solver.narrow_phase().add_shape(*shape);
    m_shape_indices.emplace(shape.get(), index);
    m_shapes.push_back(shape);
    return index;
}

void
GpuXpbdBackend::apply(std::span<const WorldEdit> edits,
                      WorldSnapshot& pending)
{
    // What changed first, then the forces and impulses about the centers the patches will have.
    const ChangeMarker marker{.backend = *this, .pending = pending};
    for (const WorldEdit& edit : edits)
    {
        std::visit(marker, edit);
    }
    const PushCollector collector{.backend = *this, .pending = pending};
    for (const WorldEdit& edit : edits)
    {
        std::visit(collector, edit);
    }

    // The patches of the bodies the edits changed, in slot order (the same edits make the same command buffer).
    const auto body_count = static_cast<std::uint32_t>(pending.rigid_bodies.size());
    if (m_bodies.size() < body_count)
    {
        m_bodies.resize(body_count);
    }
    m_body_patches.clear();
    for (std::uint32_t slot = 0; slot < body_count; ++slot)
    {
        const BodyEntry& entry = m_bodies[slot];
        if (entry.created || entry.destroyed || entry.pose_set || entry.velocity_set || entry.reconfigured || entry.woken || entry.awake_set || entry.pushed)
        {
            RigidBodyRecord& record = pending.rigid_bodies[slot];
            m_body_patches.push_back(make_patch(slot, record.handle ? &record : nullptr, pending));
        }
    }
}

auto
GpuXpbdBackend::make_patch(std::uint32_t slot,
                           RigidBodyRecord* record,
                           WorldSnapshot& pending) -> GpuBodyPatch
{
    BodyEntry& entry = m_bodies[slot];
    if (record == nullptr)
    {
        // A freed slot.
        entry = {};
        return {.slot = slot};
    }

    // Woken dynamic bodies are awake (WOKE_UP); asleep and static ones have no velocity (in their records too).
    const RigidBodyDescription& configuration = record->configuration;
    RigidBodyState& state = record->state;
    if (entry.woken && configuration.kind == BodyKind::DYNAMIC && !state.awake)
    {
        state.awake = true;
        pending.sleep_events.push_back({.kind = SleepEventKind::WOKE_UP, .body = record->handle});
    }
    const bool dynamic = configuration.kind == BodyKind::DYNAMIC;
    const bool kinematic = configuration.kind == BodyKind::KINEMATIC;
    const bool moves = kinematic || (dynamic && state.awake);

    // The pose: the record's when set, else the GPU's.
    const MassProperties mass = effective_mass(*record);
    const Quaternion orientation = entry.created || entry.pose_set ? state.pose.orientation : entry.orientation;
    const Vector3 center = patched_center(entry, *record);

    // The velocities of the center: the record's when set, else the GPU's; along the free axes; then the impulses.
    const Vector3 linear_freedom = freedom(configuration.locks, false);
    const Vector3 angular_freedom = freedom(configuration.locks, true);
    const Matrix3 local_inverse_inertia = inverse_inertia(mass.inertia);
    const float inverse_mass = dynamic ? 1.0F / mass.mass : 0.0F;
    Vector3 angular_velocity{};
    Vector3 linear_velocity{};
    if (moves)
    {
        const bool from_record = entry.created || entry.velocity_set || entry.reconfigured;
        angular_velocity = from_record ? state.angular_velocity : entry.angular_velocity;
        linear_velocity = from_record ? state.linear_velocity + cross(angular_velocity, center - state.pose.position) : entry.linear_velocity;
        if (dynamic)
        {
            // Along the free axes (kinematic bodies move as told), then the impulses.
            linear_velocity = component_multiply(linear_velocity, linear_freedom) + component_multiply(entry.linear_impulse, linear_freedom) * inverse_mass;
            angular_velocity = component_multiply(angular_velocity, angular_freedom) +
                               world_inverse_inertia(local_inverse_inertia, orientation, angular_freedom) * entry.angular_impulse;
        }
    }
    else
    {
        state.linear_velocity = {};
        state.angular_velocity = {};
    }

    // The flags: kind, awake, may sleep, locks (the patch also restarts the body's sleep timer).
    GpuBodyFlag flags = GpuBodyFlag::SIMULATED;
    flags |= dynamic ? GpuBodyFlag::DYNAMIC : GpuBodyFlag::NONE;
    flags |= kinematic ? GpuBodyFlag::KINEMATIC : GpuBodyFlag::NONE;
    flags |= moves ? GpuBodyFlag::AWAKE : GpuBodyFlag::NONE;
    flags |= configuration.allow_sleep ? GpuBodyFlag::SLEEPS : GpuBodyFlag::NONE;
    const std::uint32_t word = std::to_underlying(flags) | (std::uint32_t{std::to_underlying(configuration.locks)} << GPU_XPBD_BODY_LOCK_SHIFT);
    const bool pushed = dynamic && state.awake;
    const GpuBodyPatch patch{
        .slot = slot,
        .flags = word,
        .center = vector(center, inverse_mass),
        .orientation = vector(orientation),
        .linear_velocity = vector(linear_velocity, configuration.linear_damping),
        .angular_velocity = vector(angular_velocity, configuration.angular_damping),
        .inverse_inertia_diagonal = diagonal(local_inverse_inertia, configuration.gravity_scale),
        .inverse_inertia_off = off_diagonal(local_inverse_inertia),
        .inertia_diagonal = diagonal(dynamic ? mass.inertia : Matrix3{.columns = {Vector3{}, Vector3{}, Vector3{}}}),
        .inertia_off = off_diagonal(dynamic ? mass.inertia : Matrix3{.columns = {Vector3{}, Vector3{}, Vector3{}}}),
        .force = vector(pushed ? entry.force : Vector3{}),
        .torque = vector(pushed ? entry.torque : Vector3{}),
    };

    // The mirror now holds what the GPU will; the changes are consumed.
    const RigidBodyHandle handle = entry.handle ? entry.handle : record->handle;
    entry = {};
    entry.handle = handle;
    entry.center = center;
    entry.orientation = orientation;
    entry.linear_velocity = linear_velocity;
    entry.angular_velocity = angular_velocity;
    return patch;
}

void
GpuXpbdBackend::update_colliders(const WorldSnapshot& pending)
{
    // The colliders' data (poses about their bodies' centers of mass), the moving and static lists, the bodies' radii.
    const auto collider_count = static_cast<std::uint32_t>(pending.colliders.size());
    std::vector<GpuCollider> data(collider_count);
    std::vector<std::uint32_t> moving;
    std::vector<std::uint32_t> statics;
    std::vector<StaticSignature> signature;
    m_body_radii.assign(pending.rigid_bodies.size(), 0.0F);
    if (m_colliders.size() < collider_count)
    {
        m_colliders.resize(collider_count);
    }
    for (std::uint32_t collider = 0; collider < collider_count; ++collider)
    {
        const ColliderRecord& record = pending.colliders[collider];
        const RigidBodyRecord* body = record.handle ? find(pending, record.body) : nullptr;
        if (body == nullptr)
        {
            continue;
        }
        const ColliderDescription& configuration = record.configuration;
        const Vector3 position = configuration.pose.position - effective_mass(*body).center_of_mass;
        const auto category = std::to_underlying(configuration.filter.category);
        const auto mask = std::to_underlying(configuration.filter.mask);
        const Friction& friction = configuration.material.friction;
        const std::uint32_t body_slot = slot(record.body);
        data[collider] = {.position = vector(position),
                          .orientation = vector(configuration.pose.orientation),
                          .body = body_slot,
                          .shape = m_colliders[collider].shape,
                          .flags = 1,
                          .group = configuration.filter.group,
                          .filter = {static_cast<std::uint32_t>(category), static_cast<std::uint32_t>(category >> 32U), static_cast<std::uint32_t>(mask),
                                     static_cast<std::uint32_t>(mask >> 32U)},
                          .material = {friction.static_coefficient, friction.dynamic_coefficient, friction.rolling, configuration.material.restitution}};
        m_body_radii[body_slot] = std::max(m_body_radii[body_slot], length(position) + reach(*configuration.shape));
        if (body->configuration.kind == BodyKind::STATIC)
        {
            statics.push_back(collider);
            const BodyEntry& entry = m_bodies[body_slot];
            signature.push_back({.collider = data[collider], .center = vector(entry.center), .orientation = vector(entry.orientation)});
        }
        else
        {
            moving.push_back(collider);
        }
    }

    // Uploaded when anything changed; the static tree rebuilt when a static collider or a static body's pose changed.
    const std::span<const std::byte> signature_bytes = std::as_bytes(std::span{signature});
    const bool lists_changed = !same_bytes(std::span<const std::uint32_t>{moving}, std::span<const std::uint32_t>{m_moving_list}) ||
                               !same_bytes(std::span<const std::uint32_t>{statics}, std::span<const std::uint32_t>{m_static_list});
    const bool data_changed = !same_bytes(std::span<const GpuCollider>{data}, std::span<const GpuCollider>{m_collider_data});
    const bool statics_changed = lists_changed || m_statics_dirty || !same_bytes(signature_bytes, std::span<const std::byte>{m_static_signature});
    if (lists_changed || data_changed || statics_changed)
    {
        m_solver.set_colliders(data, moving, statics, statics_changed);
        m_collider_data = std::move(data);
        m_moving_list = std::move(moving);
        m_static_list = std::move(statics);
        m_static_signature.assign(signature_bytes.begin(), signature_bytes.end());
    }
    m_lists_changed = m_lists_changed || lists_changed;
    m_colliders_dirty = false;
    m_lists_dirty = false;
    m_statics_dirty = false;
}

void
GpuXpbdBackend::update_joints(const WorldSnapshot& pending)
{
    // Each joint's bodies, flags (active while enabled), frames about its bodies' centers of mass, breaking thresholds and
    // parts; the bodies joints keep apart (enabled joints between two bodies), sorted.
    const auto joint_count = static_cast<std::uint32_t>(pending.joints.size());
    std::vector<GpuJoint> joints(joint_count);
    std::vector<GpuJointPart> parts;
    std::vector<std::array<std::uint32_t, 2>> joined;
    for (std::uint32_t joint_slot = 0; joint_slot < joint_count; ++joint_slot)
    {
        const JointRecord& record = pending.joints[joint_slot];
        if (!record.handle || joint_slot >= m_joints.size())
        {
            continue;
        }
        const auto [first, second] = joint_bodies(record.configuration);
        const RigidBodyRecord* first_record = find(pending, first);
        const RigidBodyRecord* second_record = second ? find(pending, second) : nullptr;
        if (first_record == nullptr || (second && second_record == nullptr))
        {
            continue;
        }
        const JointFrames& frames = m_joints[joint_slot].frames;
        const Vector3 first_center = effective_mass(*first_record).center_of_mass;
        const Vector3 second_center = second_record != nullptr ? effective_mass(*second_record).center_of_mass : Vector3{};
        const JointBreaking breaking = std::visit([](const auto& description) { return description.breaking; }, record.configuration);
        const auto first_part = static_cast<std::uint32_t>(parts.size());
        append_joint_parts(record.configuration, parts);
        joints[joint_slot] = {.first = slot(first),
                              .second = second ? slot(second) : NO_SOLVER_BODY,
                              .flags = record.state.enabled ? std::to_underlying(GpuJointFlag::ACTIVE) : 0U,
                              .part_count = static_cast<std::uint32_t>(parts.size()) - first_part,
                              .first_part = first_part,
                              .reserved = {},
                              .first_position = vector(frames.first.position - first_center, breaking.force),
                              .first_orientation = vector(frames.first.orientation),
                              .second_position = vector(frames.second.position - second_center, breaking.torque),
                              .second_orientation = vector(frames.second.orientation)};
        if (record.state.enabled && second && keeps_apart(record.configuration))
        {
            const std::uint32_t first_slot = slot(first);
            const std::uint32_t second_slot = slot(second);
            joined.push_back({std::min(first_slot, second_slot), std::max(first_slot, second_slot)});
        }
    }
    std::ranges::sort(joined);
    const auto [duplicates, end] = std::ranges::unique(joined);
    joined.erase(duplicates, end);

    // Uploaded when anything changed.
    const bool changed = !same_bytes(std::span<const GpuJoint>{joints}, std::span<const GpuJoint>{m_joint_data}) ||
                         !same_bytes(std::span<const GpuJointPart>{parts}, std::span<const GpuJointPart>{m_part_data}) ||
                         !same_bytes(std::span<const std::array<std::uint32_t, 2>>{joined}, std::span<const std::array<std::uint32_t, 2>>{m_joined_data});
    if (changed)
    {
        m_solver.set_joints(joints, parts, joined);
        m_joint_data = std::move(joints);
        m_part_data = std::move(parts);
        m_joined_data = std::move(joined);
    }
}

auto
GpuXpbdBackend::step_motion(const WorldSnapshot& pending,
                            float seconds) const noexcept -> float
{
    // The farthest a moving body's colliders get in the step (its speed and its turn at its radius, gravity's growth):
    // the margin the boxes and the speculative contacts grow by, so that what a step brings into contact is found.
    float farthest = 0.0F;
    const float gravity = length(pending.configuration.gravity) * seconds * seconds;
    for (std::uint32_t slot = 0; slot < m_body_radii.size() && slot < pending.rigid_bodies.size(); ++slot)
    {
        const RigidBodyRecord& record = pending.rigid_bodies[slot];
        const bool moves = record.handle && (record.configuration.kind == BodyKind::KINEMATIC || (record.configuration.kind == BodyKind::DYNAMIC && record.state.awake));
        if (!moves || slot >= m_bodies.size())
        {
            continue;
        }
        const BodyEntry& entry = m_bodies[slot];
        const float speed = length(entry.linear_velocity) + length(entry.angular_velocity) * m_body_radii[slot];
        const float scale = record.configuration.kind == BodyKind::DYNAMIC ? std::abs(record.configuration.gravity_scale) : 0.0F;
        farthest = std::max(farthest, speed * seconds + gravity * scale);
    }
    return farthest;
}

void
GpuXpbdBackend::simulate(float seconds,
                         WorldSnapshot& pending)
{
    // The patches were made by apply() (the framework times it as the EDITS phase).
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    const auto body_count = static_cast<std::uint32_t>(pending.rigid_bodies.size());
    if (body_count == 0)
    {
        m_body_count = 0;
        return;
    }

    // Graphics failures (a lost device, memory exhausted when a buffer grows) become a PhysicsError naming the stage,
    // the graphics error nested.
    const std::uint32_t substeps = std::max(pending.configuration.solver.sub_steps, 1U);
    std::string_view stage = "recording the step";
    try
    {
        update_colliders(pending);
        update_joints(pending);
        m_substep_seconds = seconds / static_cast<float>(substeps);

        // The step, replayed from its start while a buffer or the recorded passes overflowed (each time with room for
        // what was counted).
        GpuSolverCounts counts{};
        for (std::uint32_t attempt = 0;; ++attempt)
        {
            stage = "recording the step";
            record_step(body_count, pending, seconds, attempt > 0);
            stage = "running the step on the GPU";
            const std::array command_buffers{m_command_buffer->handle()};
            const std::array signals{TimelineSemaphoreSignal{.semaphore = m_timeline->handle(), .value = m_submitted + 1}};
            const std::array batches{SubmitDescription{.command_buffers = command_buffers, .timeline_signals = signals}};
            m_queue->submit(batches);
            ++m_submitted;
            if (m_timeline->wait(m_submitted) != WaitStatus::SIGNALED)
            {
                throw PhysicsError({.backend = identity(), .operation = "PhysicsWorld::step", .problem = "the GPU step did not complete", .suggestion = {}});
            }
            counts = read_counts();
            if (!m_solver.overflowed(counts))
            {
                break;
            }
            if (attempt == GPU_XPBD_MAXIMUM_REPLAYS)
            {
                throw PhysicsError({.backend = identity(),
                                    .operation = "PhysicsWorld::step",
                                    .problem = std::format("the step still overflowed after {} replays ({} pairs, {} manifold slots, {} colors)",
                                                           GPU_XPBD_MAXIMUM_REPLAYS, counts.within_pairs + counts.between_pairs, counts.manifold_slots,
                                                           counts.colors),
                                    .suggestion = "the world's state is that of the last completed step"});
            }
        }
        const Clock::time_point solved = Clock::now();
        pending.statistics.phase_seconds[phase(StepPhase::SOLVER)] = seconds_between(start, solved);
        stage = "reading the step back";
        read_back(pending);
        m_solver.complete_step(counts);
        m_lists_changed = false;
        pending.statistics.pairs = counts.candidates;
        pending.statistics.contacts = counts.contact_points;
        m_retired.clear();
        (void)m_device.deleter().collect();
        pending.statistics.phase_seconds[phase(StepPhase::EVENTS)] = seconds_between(solved, Clock::now());
    }
    catch (const gfx::Error& error)
    {
        std::throw_with_nested(PhysicsError({.backend = identity(),
                                             .operation = "PhysicsWorld::step",
                                             .problem = std::format("the GPU failed while {}: {}", stage, error.what()),
                                             .suggestion = "the world's state is that of the last completed step; a lost device needs a new world"}));
    }
}

void
GpuXpbdBackend::record_step(std::uint32_t body_count,
                            const WorldSnapshot& pending,
                            float seconds,
                            bool replay)
{
    // Host buffers large enough (before recording: their bindless indices go into the parameters).
    const auto patch_count = static_cast<std::uint32_t>(m_body_patches.size());
    m_parameters.reserve(m_device, sizeof(GpuStepParameters), m_retired);
    m_patches.reserve(m_device, std::max<std::size_t>(patch_count, 1) * sizeof(GpuBodyPatch), m_retired);
    const std::uint32_t joint_count = m_solver.joint_count();
    m_readback.reserve(m_device,
                       std::uint64_t{body_count} * (sizeof(GpuBodyReadback) + sizeof(std::uint32_t)) + sizeof(GpuSolverCounts) +
                           std::uint64_t{joint_count} * 2 * sizeof(GpuVector),
                       m_retired);

    m_command_pool->reset();
    ComputeRecorder recorder{*m_command_buffer};

    // The arrays hold every slot (grown ones keep the slots the GPU had); the contacts' arrays and uploads.
    const std::uint32_t kept = m_body_count;
    for (GpuArray* array : {&m_centers, &m_orientations, &m_linear_velocities, &m_angular_velocities, &m_inverse_inertias, &m_inertias, &m_forces,
                            &m_torques, &m_displacements, &m_rotations, &m_flags, &m_previous_linear, &m_previous_angular, &m_sleep_timers})
    {
        (void)array->reserve(m_device, recorder, body_count, kept, m_retired);
    }
    for (GpuArray& backup : m_backups)
    {
        (void)backup.reserve(m_device, recorder, body_count, kept, m_retired);
    }
    m_body_count = body_count;
    m_solver.record_uploads(recorder, body_count, m_retired);

    // The parameters and the patches; this step's forces start from zero (the patches give some).
    const GpuStepParameters parameters{
        .gravity = vector(pending.configuration.gravity, m_substep_seconds),
        .body_count = body_count,
        .patch_count = patch_count,
        .substeps = std::max(pending.configuration.solver.sub_steps, 1U),
        .body_centers = m_centers.index(),
        .body_orientations = m_orientations.index(),
        .body_linear_velocities = m_linear_velocities.index(),
        .body_angular_velocities = m_angular_velocities.index(),
        .body_inverse_inertias = m_inverse_inertias.index(),
        .body_inertias = m_inertias.index(),
        .body_forces = m_forces.index(),
        .body_torques = m_torques.index(),
        .body_displacements = m_displacements.index(),
        .body_rotations = m_rotations.index(),
        .body_flags = m_flags.index(),
        .body_patches = m_patches.index(),
        .body_previous_linear = m_previous_linear.index(),
        .body_previous_angular = m_previous_angular.index(),
        .sleep_timers = m_sleep_timers.index(),
    };
    std::memcpy(m_parameters.bytes().data(), &parameters, sizeof(parameters));
    m_parameters.buffer().flush_mapped_range();
    const std::uint64_t vectors = std::uint64_t{body_count} * sizeof(GpuVector);
    const std::uint64_t words = std::uint64_t{body_count} * sizeof(std::uint32_t);
    if (!replay)
    {
        if (patch_count > 0)
        {
            std::memcpy(m_patches.bytes().data(), m_body_patches.data(), m_body_patches.size() * sizeof(GpuBodyPatch));
            m_patches.buffer().flush_mapped_range();
        }
        recorder.fill_buffer(m_forces.buffer().handle(), 0, vectors, 0);
        recorder.fill_buffer(m_torques.buffer().handle(), 0, vectors, 0);
    }
    const std::uint32_t parameters_index = m_parameters.index();
    const auto pass = [&recorder, parameters_index](ComputePipelineHandle pipeline, std::uint32_t count) {
        recorder.bind_pipeline(pipeline);
        recorder.push_constants(parameters_index);
        recorder.dispatch(workgroups(count));
        recorder.barrier({.memory_barriers = std::span{&COMPUTE_TO_COMPUTE, 1}});
    };

    // The step's start: patched and backed up, or (replaying) restored.
    const std::array<const GpuArray*, 6> state_arrays{&m_centers, &m_orientations, &m_linear_velocities, &m_angular_velocities, &m_flags, &m_sleep_timers};
    const auto copy_state = [&](bool restore) {
        for (std::size_t array = 0; array < state_arrays.size(); ++array)
        {
            const Buffer& live = state_arrays[array]->buffer();
            const Buffer& backup = m_backups[array].buffer();
            const std::array regions{BufferCopyRegion{.size = std::uint64_t{body_count} * state_arrays[array]->element_size()}};
            recorder.copy_buffer(restore ? backup.handle() : live.handle(), restore ? live.handle() : backup.handle(), regions);
        }
    };
    recorder.barrier({.memory_barriers = std::span{&BEFORE_PASSES, 1}});
    if (!replay)
    {
        if (patch_count > 0)
        {
            pass(m_pipelines.patch_bodies(), patch_count);
        }
        recorder.barrier({.memory_barriers = std::span{&COMPUTE_TO_TRANSFER, 1}});
        copy_state(false);
    }
    else
    {
        copy_state(true);
    }
    recorder.barrier({.memory_barriers = std::span{&BEFORE_PASSES, 1}});

    // The contacts, then the substeps: prediction, the position iterations, the velocity update, the velocity
    // iterations; then sleep.
    const SolverDescription& solver = pending.configuration.solver;
    const ContactStep step{.step_parameters = parameters_index,
                           .body_count = body_count,
                           .step_seconds = seconds,
                           .speculative_distance = pending.configuration.collision.speculative_distance + step_motion(pending, seconds),
                           .solver = solver,
                           .sleep = pending.configuration.sleep,
                           .collision = pending.configuration.collision,
                           .lists_changed = m_lists_changed};
    m_solver.record_detection(recorder, step);
    for (std::uint32_t substep = 0; substep < parameters.substeps; ++substep)
    {
        pass(m_pipelines.integrate(), body_count);
        for (std::uint32_t iteration = 0; iteration < std::max(solver.position_iterations, 1U); ++iteration)
        {
            m_solver.record_positions(recorder, iteration == 0);
        }
        pass(m_pipelines.update_velocities(), body_count);
        for (std::uint32_t iteration = 0; iteration < std::max(solver.velocity_iterations, 1U); ++iteration)
        {
            m_solver.record_velocities(recorder, iteration == 0);
        }
    }
    m_solver.record_joint_forces(recorder);
    m_solver.record_sleep(recorder);

    // The results into the readback buffer: the bodies' arrays, their flags, the solver's counts, the joints' results.
    recorder.barrier({.memory_barriers = std::span{&COMPUTE_TO_TRANSFER, 1}});
    std::uint64_t offset = 0;
    for (const GpuArray* array : {&m_centers, &m_orientations, &m_linear_velocities, &m_angular_velocities})
    {
        const std::array regions{BufferCopyRegion{.source_offset = 0, .destination_offset = offset, .size = vectors}};
        recorder.copy_buffer(array->buffer().handle(), m_readback.buffer().handle(), regions);
        offset += vectors;
    }
    const std::array flag_regions{BufferCopyRegion{.source_offset = 0, .destination_offset = offset, .size = words}};
    recorder.copy_buffer(m_flags.buffer().handle(), m_readback.buffer().handle(), flag_regions);
    offset += words;
    const std::array count_regions{BufferCopyRegion{.source_offset = 0, .destination_offset = offset, .size = sizeof(GpuSolverCounts)}};
    recorder.copy_buffer(m_solver.counts_buffer().handle(), m_readback.buffer().handle(), count_regions);
    offset += sizeof(GpuSolverCounts);
    if (joint_count > 0)
    {
        const std::array joint_regions{
            BufferCopyRegion{.source_offset = 0, .destination_offset = offset, .size = std::uint64_t{joint_count} * 2 * sizeof(GpuVector)}};
        recorder.copy_buffer(m_solver.joint_results_buffer().handle(), m_readback.buffer().handle(), joint_regions);
    }
    recorder.barrier({.memory_barriers = std::span{&TRANSFER_TO_HOST, 1}});
}

auto
GpuXpbdBackend::read_counts() -> GpuSolverCounts
{
    m_readback.buffer().invalidate_mapped_range();
    const std::span<const std::byte> bytes = m_readback.bytes();
    GpuSolverCounts counts{};
    const std::size_t offset = std::size_t{m_body_count} * (4 * sizeof(GpuVector) + sizeof(std::uint32_t));
    std::memcpy(&counts, bytes.data() + offset, sizeof(counts));
    return counts;
}

void
GpuXpbdBackend::read_back(WorldSnapshot& pending)
{
    // Arrays of centers, orientations, linear and angular velocities, then flags; sleep changes reported in slot order;
    // after the counts, the joints' results.
    m_readback.buffer().invalidate_mapped_range();
    const std::span<const std::byte> bytes = m_readback.bytes();
    const std::size_t vectors = std::size_t{m_body_count} * sizeof(GpuVector);
    for (std::uint32_t slot = 0; slot < m_body_count; ++slot)
    {
        RigidBodyRecord& record = pending.rigid_bodies[slot];
        if (!record.handle || record.configuration.kind == BodyKind::STATIC)
        {
            continue;
        }
        std::uint32_t flags = 0;
        std::memcpy(&flags, bytes.data() + 4 * vectors + slot * sizeof(std::uint32_t), sizeof(flags));
        const bool awake = (flags & std::to_underlying(GpuBodyFlag::AWAKE)) != 0;

        // The GPU's values, exactly, in the mirror; the frame's pose and velocities in the record.
        BodyEntry& entry = m_bodies[slot];
        entry.center = vector3(read_vector(bytes, 0, slot));
        entry.orientation = quaternion(read_vector(bytes, vectors, slot));
        entry.linear_velocity = vector3(read_vector(bytes, 2 * vectors, slot));
        entry.angular_velocity = vector3(read_vector(bytes, 3 * vectors, slot));
        RigidBodyState& state = record.state;
        state.pose.orientation = entry.orientation;
        state.pose.position = entry.center - rotate(entry.orientation, effective_mass(record).center_of_mass);
        state.linear_velocity = entry.linear_velocity + cross(entry.angular_velocity, state.pose.position - entry.center);
        state.angular_velocity = entry.angular_velocity;
        if (record.configuration.kind == BodyKind::DYNAMIC && awake != state.awake)
        {
            state.awake = awake;
            pending.sleep_events.push_back({.kind = awake ? SleepEventKind::WOKE_UP : SleepEventKind::FELL_ASLEEP, .body = record.handle});
        }
    }

    // The joints solved in the step: their force and torque (the step's averages); those that broke are disabled and
    // reported (in slot order).
    const std::size_t results = 4 * vectors + std::size_t{m_body_count} * sizeof(std::uint32_t) + sizeof(GpuSolverCounts);
    const std::uint32_t joint_count = std::min(m_solver.joint_count(), static_cast<std::uint32_t>(pending.joints.size()));
    for (std::uint32_t joint_slot = 0; joint_slot < joint_count; ++joint_slot)
    {
        JointRecord& record = pending.joints[joint_slot];
        const GpuVector force = read_vector(bytes, results, 2 * std::size_t{joint_slot});
        const auto flags = std::bit_cast<std::uint32_t>(force[3]);
        if (!record.handle || (flags & std::to_underlying(GpuJointFlag::SOLVED)) == 0)
        {
            continue;
        }
        JointState& state = record.state;
        state.force = vector3(force);
        state.torque = vector3(read_vector(bytes, results, 2 * std::size_t{joint_slot} + 1));
        if ((flags & std::to_underlying(GpuJointFlag::BROKEN)) != 0 && state.enabled)
        {
            state.enabled = false;
            state.broken = true;
            pending.joint_events.push_back(
                {.kind = JointEventKind::BROKEN, .joint = record.handle, .force = length(state.force), .torque = length(state.torque), .constraint = {}});
        }
    }
}

} // namespace gfx::physics::gpu_xpbd
