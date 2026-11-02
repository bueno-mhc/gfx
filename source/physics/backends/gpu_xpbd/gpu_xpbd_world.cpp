// GpuXpbdBackend: the edits become patches of the GPU body arrays, the colliders' data, the joints' data and the soft
// bodies' particles; a step is one command buffer (patches, the step's start backed up, the contacts' detection and the
// coloring of joints and contacts, substeps of prediction, soft-constraint, joint and contact projection and velocity
// update with the contacts' velocity pass, the joints' forces, sleep), submitted and waited for on the stepping thread,
// replayed from its backed-up start when a buffer or the recorded passes overflowed, and read back into the pending
// snapshot (bodies, joint states, broken joints, particles).
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
import gfx.physics.gpu_xpbd.shaders;
import :settings;
import :resources;
import :primitives;
import :narrow_phase;
import :solver;
import :backend;

namespace gfx::physics::gpu_xpbd
{

namespace
{

/// Bytes of a query's input: the query, then the shape's record at 256 (gpu_queries.cpp).
constexpr std::uint64_t SHAPE_RECORD_BYTES_END = 512;

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

/// The entry of a soft body made of @p description: its model, its constraints for the GPU color after color — the
/// model's colors, then its tethers' (a tether moves its first particle only: two conflict when they move the same
/// particle or one moves the other's anchor; colored greedily in order, 64 colors at a time, as the model colors the
/// others).
[[nodiscard]] auto
make_soft_body_entry(SoftBodyHandle handle,
                     const SoftBodyDescription& description) -> SoftBodyEntry
{
    SoftBodyEntry entry{.handle = handle, .model = std::make_shared<const SoftBodyModel>(description), .constraints = {}, .color_starts = {}, .first_particle = 0};
    const SoftBodyModel& model = *entry.model;
    const auto converted = [](const SoftBodyModel::Constraint& constraint) {
        return GpuSoftConstraint{.particles = constraint.particles, .kind = std::to_underlying(constraint.kind), .compliance = constraint.compliance, .rest = constraint.rest,
                                 .reserved = 0};
    };
    for (const SoftBodyModel::Constraint& constraint : model.constraints())
    {
        entry.constraints.push_back(converted(constraint));
    }
    entry.color_starts.assign(model.color_starts().begin(), model.color_starts().end());

    // The tethers' colors.
    const std::span<const SoftBodyModel::Constraint> tethers = model.tethers();
    std::vector<std::uint32_t> colors(tethers.size(), 0);
    std::vector<std::uint32_t> remaining(tethers.size());
    std::ranges::iota(remaining, 0U);
    std::vector<std::uint32_t> next;
    std::vector<std::uint64_t> held(model.particle_count(), 0);
    std::vector<std::uint64_t> anchored(model.particle_count(), 0);
    std::uint32_t color_count = 0;
    for (std::uint32_t base = 0; !remaining.empty(); base += 64)
    {
        std::ranges::fill(held, std::uint64_t{0});
        std::ranges::fill(anchored, std::uint64_t{0});
        next.clear();
        for (const std::uint32_t index : remaining)
        {
            const std::uint32_t moved = tethers[index].particles[0];
            const std::uint32_t anchor = tethers[index].particles[1];
            const std::uint64_t taken = held[moved] | anchored[moved] | held[anchor];
            if (taken == ~std::uint64_t{0})
            {
                next.push_back(index);
                continue;
            }
            const auto bit = static_cast<std::uint32_t>(std::countr_one(taken));
            colors[index] = base + bit;
            color_count = std::max(color_count, base + bit + 1);
            held[moved] |= std::uint64_t{1} << bit;
            anchored[anchor] |= std::uint64_t{1} << bit;
        }
        remaining.swap(next);
    }

    // The tethers color after color, in order within each, after the model's colors.
    std::vector<std::uint32_t> starts(color_count + 1, 0);
    for (const std::uint32_t color : colors)
    {
        ++starts[color + 1];
    }
    std::partial_sum(starts.begin(), starts.end(), starts.begin());
    const auto first = static_cast<std::uint32_t>(entry.constraints.size());
    entry.constraints.resize(first + tethers.size());
    std::vector<std::uint32_t> cursor(starts.begin(), starts.end() - 1);
    for (std::size_t index = 0; index < tethers.size(); ++index)
    {
        entry.constraints[first + cursor[colors[index]]++] = converted(tethers[index]);
    }
    for (std::uint32_t color = 0; color < color_count; ++color)
    {
        entry.color_starts.push_back(first + starts[color + 1]);
    }
    return entry;
}

/// Marks what each edit changed of the bodies, colliders and joints (forces and impulses come after: they need the final
/// centers).
struct ChangeMarker
{
    GpuXpbdBackend& backend;
    WorldSnapshot& pending;

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
        backend.forget_collider(change.collider, pending);
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

    // Triggers: their shapes in the table; their data comes from the records each step.
    void operator()(const edits::CreateTrigger& change) const
    {
        backend.trigger_entry(change.trigger) = {.handle = change.trigger, .shape = backend.shape_index(change.description.shape), .overlaps = {}};
        backend.colliders_changed(false, false);
    }

    void operator()(const edits::DestroyTrigger& change) const
    {
        backend.trigger_entry(change.trigger) = {};
        backend.colliders_changed(false, false);
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

    // Soft bodies: made or destroyed, their particles are laid out again (their spheres among the moving colliders); a
    // particle moved, their state is uploaded again; pins and settings come from the records each step.
    void operator()(const edits::CreateSoftBody& change) const
    {
        backend.soft_body_entry(change.soft_body) = make_soft_body_entry(change.soft_body, change.description);
        backend.soft_bodies_changed(true);
        backend.colliders_changed(true, false);
    }

    void operator()(const edits::DestroySoftBody& change) const
    {
        backend.soft_body_entry(change.soft_body) = {};
        backend.soft_bodies_changed(true);
        backend.colliders_changed(true, false);
    }

    void operator()(const edits::MoveParticle& /*change*/) const
    {
        backend.soft_bodies_changed(false);
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
    , m_soft_bodies{device}
    , m_timer{device, m_capabilities.timestamps, m_capabilities.queue_family}
{
    // The queries' command buffer, pipeline and buffers (made now: a query never allocates), the records their regions and
    // shapes take in the shape table.
    m_query_pool = device.create_command_pool({.queue_type = m_capabilities.queue_type, .debug_name = "GPU physics queries"});
    m_query_commands = m_query_pool->allocate(CommandBufferLevel::PRIMARY, "GPU physics query");
    m_query_pipeline = make_pipeline(device, shaders::query_comp, m_query_modules);
    m_query_pairs = device.create_buffer({.size = std::uint64_t{GPU_XPBD_QUERY_PAIRS} * sizeof(GpuPair), .usage = BufferUsage::STORAGE, .debug_name = "GPU physics: query pairs"});
    m_query_pieces = device.create_buffer(
        {.size = std::uint64_t{GPU_XPBD_QUERY_PIECES} * sizeof(GpuQueryResult), .usage = BufferUsage::STORAGE, .debug_name = "GPU physics: query pieces"});
    m_query_input.reserve(device, SHAPE_RECORD_BYTES_END, m_retired);
    const std::uint64_t answer_bytes = 2 * sizeof(GpuVector) + std::uint64_t{GPU_XPBD_QUERY_PAIRS} * sizeof(GpuQueryResult);
    m_query_answers = device.create_buffer(
        {.size = answer_bytes, .usage = BufferUsage::STORAGE | BufferUsage::TRANSFER_SOURCE | BufferUsage::TRANSFER_DESTINATION, .debug_name = "GPU physics: query answers"});
    m_query_results.reserve(device, answer_bytes, m_retired);
    const std::shared_ptr<const Shape> placeholder = make_shape(BoxShape{});
    m_query_records = {m_solver.narrow_phase().add_shape(*placeholder), m_solver.narrow_phase().add_shape(*placeholder)};
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
    // kind and soft bodies are simulated.
    if (const auto* collider = std::get_if<edits::CreateCollider>(&edit))
    {
        GpuNarrowPhase::check_shape(*collider->description.shape);
    }
    if (const auto* trigger = std::get_if<edits::CreateTrigger>(&edit))
    {
        GpuNarrowPhase::check_shape(*trigger->description.shape);
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

auto
GpuXpbdBackend::trigger_entry(TriggerHandle trigger) -> TriggerEntry&
{
    const std::uint32_t index = slot(trigger);
    if (m_triggers.size() <= index)
    {
        m_triggers.resize(index + 1);
    }
    return m_triggers[index];
}

auto
GpuXpbdBackend::soft_body_entry(SoftBodyHandle soft_body) -> SoftBodyEntry&
{
    const std::uint32_t index = slot(soft_body);
    if (m_soft_body_entries.size() <= index)
    {
        m_soft_body_entries.resize(index + 1);
    }
    return m_soft_body_entries[index];
}

void
GpuXpbdBackend::soft_bodies_changed(bool layout) noexcept
{
    m_soft_layout_changed = m_soft_layout_changed || layout;
    m_particle_state_dirty = true;
}

auto
GpuXpbdBackend::particle_shape(float radius) -> std::uint32_t
{
    // One sphere per radius (kept alive with the colliders' shapes).
    const auto key = std::bit_cast<std::uint32_t>(radius);
    if (const auto known = m_sphere_shapes.find(key); known != m_sphere_shapes.end())
    {
        return known->second;
    }
    const std::uint32_t index = shape_index(make_shape(SphereShape{.radius = radius}));
    m_sphere_shapes.emplace(key, index);
    return index;
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
                          .flags = GPU_COLLIDER_LIVE,
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

    // The particles' spheres: moving colliders after the colliders' slots, their nodes after the bodies' slots (soft bodies
    // laid out by update_soft_bodies()).
    const auto body_count = static_cast<std::uint32_t>(pending.rigid_bodies.size());
    for (std::uint32_t soft_slot = 0; soft_slot < m_soft_body_entries.size(); ++soft_slot)
    {
        const SoftBodyEntry& entry = m_soft_body_entries[soft_slot];
        if (!entry.handle)
        {
            continue;
        }
        const SoftBodyDescription& settings = pending.soft_bodies[soft_slot].configuration;
        const std::uint32_t shape = particle_shape(settings.particle_radius);
        const auto category = std::to_underlying(settings.filter.category);
        const auto mask = std::to_underlying(settings.filter.mask);
        for (std::uint32_t particle = 0; particle < entry.model->particle_count(); ++particle)
        {
            moving.push_back(static_cast<std::uint32_t>(data.size()));
            data.push_back({.position = {},
                            .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
                            .body = body_count + entry.first_particle + particle,
                            .shape = shape,
                            .flags = GPU_COLLIDER_LIVE | GPU_COLLIDER_PARTICLE,
                            .group = settings.filter.group,
                            .filter = {static_cast<std::uint32_t>(category), static_cast<std::uint32_t>(category >> 32U), static_cast<std::uint32_t>(mask),
                                       static_cast<std::uint32_t>(mask >> 32U)},
                            .material = {settings.friction, settings.friction, 0.0F, 0.0F}});
        }
    }

    // The triggers' shapes after them: about their bodies' centers of mass, or in the world (no body).
    std::vector<std::uint32_t> triggers;
    m_trigger_slots.clear();
    for (std::uint32_t trigger_slot = 0; trigger_slot < m_triggers.size() && trigger_slot < pending.triggers.size(); ++trigger_slot)
    {
        const TriggerRecord& record = pending.triggers[trigger_slot];
        if (!record.handle || !m_triggers[trigger_slot].handle)
        {
            continue;
        }
        const TriggerDescription& configuration = record.configuration;
        const RigidBodyRecord* body = configuration.body ? find(pending, configuration.body) : nullptr;
        if (configuration.body && body == nullptr)
        {
            continue;
        }
        const Vector3 position = body != nullptr ? configuration.pose.position - effective_mass(*body).center_of_mass : configuration.pose.position;
        const auto category = std::to_underlying(configuration.filter.category);
        const auto mask = std::to_underlying(configuration.filter.mask);
        triggers.push_back(static_cast<std::uint32_t>(data.size()));
        m_trigger_slots.push_back(trigger_slot);
        data.push_back({.position = vector(position),
                        .orientation = vector(configuration.pose.orientation),
                        .body = body != nullptr ? slot(configuration.body) : NO_SOLVER_BODY,
                        .shape = m_triggers[trigger_slot].shape,
                        .flags = GPU_COLLIDER_TRIGGER,
                        .group = configuration.filter.group,
                        .filter = {static_cast<std::uint32_t>(category), static_cast<std::uint32_t>(category >> 32U), static_cast<std::uint32_t>(mask),
                                   static_cast<std::uint32_t>(mask >> 32U)},
                        .material = {}});
    }

    // Uploaded when anything changed; the static tree rebuilt when a static collider or a static body's pose changed.
    const std::span<const std::byte> signature_bytes = std::as_bytes(std::span{signature});
    const bool lists_changed = !same_bytes(std::span<const std::uint32_t>{moving}, std::span<const std::uint32_t>{m_moving_list}) ||
                               !same_bytes(std::span<const std::uint32_t>{statics}, std::span<const std::uint32_t>{m_static_list});
    const bool data_changed = !same_bytes(std::span<const GpuCollider>{data}, std::span<const GpuCollider>{m_collider_data}) ||
                              !same_bytes(std::span<const std::uint32_t>{triggers}, std::span<const std::uint32_t>{m_trigger_list});
    const bool statics_changed = lists_changed || m_statics_dirty || !same_bytes(signature_bytes, std::span<const std::byte>{m_static_signature});
    if (lists_changed || data_changed || statics_changed)
    {
        m_solver.set_colliders(data, moving, statics, triggers, statics_changed);
        m_collider_data = std::move(data);
        m_moving_list = std::move(moving);
        m_static_list = std::move(statics);
        m_trigger_list = std::move(triggers);
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

    // The pins that share their corrections with a body (a dynamic one with mass) or are compliant: joints of a PIN part
    // from the particle's node to the point of the body (about its center of mass) or of the world, after the joints'
    // slots (rigid pins to the world or to other bodies make their particles kinematic instead).
    const auto body_count = static_cast<std::uint32_t>(pending.rigid_bodies.size());
    constexpr float NEVER = std::numeric_limits<float>::infinity();
    for (std::uint32_t soft_slot = 0; soft_slot < m_soft_body_entries.size(); ++soft_slot)
    {
        const SoftBodyEntry& entry = m_soft_body_entries[soft_slot];
        if (!entry.handle)
        {
            continue;
        }
        for (const ParticlePin& pin : pending.soft_bodies[soft_slot].configuration.pins)
        {
            const RigidBodyRecord* body = pin.body ? find(pending, pin.body) : nullptr;
            if (pin.particle >= entry.model->particle_count() || (pin.body && body == nullptr))
            {
                continue;
            }
            const bool shared = body != nullptr && effective_mass(*body).mass > 0.0F;
            if (!shared && pin.compliance == 0.0F)
            {
                continue;
            }
            const auto first_part = static_cast<std::uint32_t>(parts.size());
            parts.push_back({.kind = std::to_underlying(GpuJointPartKind::PIN), .axis = 0, .reserved = {}, .lower = {0.0F, 0.0F, 0.0F, pin.compliance}, .upper = {}});
            const Vector3 point = body != nullptr ? pin.point - effective_mass(*body).center_of_mass : pin.point;
            joints.push_back({.first = body_count + entry.first_particle + pin.particle,
                              .second = body != nullptr ? slot(pin.body) : NO_SOLVER_BODY,
                              .flags = std::to_underlying(GpuJointFlag::ACTIVE),
                              .part_count = 1,
                              .first_part = first_part,
                              .reserved = {},
                              .first_position = {0.0F, 0.0F, 0.0F, NEVER},
                              .first_orientation = {0.0F, 0.0F, 0.0F, 1.0F},
                              .second_position = vector(point, NEVER),
                              .second_orientation = {0.0F, 0.0F, 0.0F, 1.0F}});
        }
    }

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

void
GpuXpbdBackend::update_soft_bodies(const WorldSnapshot& pending)
{
    // Laid out again when soft bodies were made or destroyed: each live one's particles after the previous one's, in slot
    // order; their constraints (particle indices shifted) gathered color by color — a color of every soft body in one
    // pass.
    if (m_soft_layout_changed)
    {
        std::uint32_t first = 0;
        std::size_t colors = 0;
        for (SoftBodyEntry& entry : m_soft_body_entries)
        {
            if (entry.handle)
            {
                entry.first_particle = first;
                first += entry.model->particle_count();
                colors = std::max(colors, entry.color_starts.size() - 1);
            }
        }
        m_particle_count = first;
        std::vector<GpuSoftConstraint> constraints;
        std::vector<std::uint32_t> color_starts{0};
        for (std::size_t color = 0; color < colors; ++color)
        {
            for (const SoftBodyEntry& entry : m_soft_body_entries)
            {
                if (!entry.handle || color + 1 >= entry.color_starts.size())
                {
                    continue;
                }
                for (std::uint32_t index = entry.color_starts[color]; index < entry.color_starts[color + 1]; ++index)
                {
                    GpuSoftConstraint constraint = entry.constraints[index];
                    for (std::uint32_t& particle : constraint.particles)
                    {
                        particle += entry.first_particle;
                    }
                    constraints.push_back(constraint);
                }
            }
            color_starts.push_back(static_cast<std::uint32_t>(constraints.size()));
        }
        m_soft_bodies.set_constraints(constraints, color_starts);
        m_soft_layout_changed = false;
        m_particle_state_dirty = true;
    }

    // Their state from the records, when laid out again or when an edit moved a particle (the records hold what the GPU
    // computed, exactly).
    if (m_particle_state_dirty)
    {
        std::vector<GpuVector> positions(m_particle_count);
        std::vector<GpuVector> velocities(m_particle_count);
        for (std::uint32_t soft_slot = 0; soft_slot < m_soft_body_entries.size(); ++soft_slot)
        {
            const SoftBodyEntry& entry = m_soft_body_entries[soft_slot];
            if (!entry.handle)
            {
                continue;
            }
            const SoftBodyRecord& record = pending.soft_bodies[soft_slot];
            for (std::uint32_t particle = 0; particle < entry.model->particle_count(); ++particle)
            {
                positions[entry.first_particle + particle] = vector(record.positions[particle]);
                velocities[entry.first_particle + particle] = vector(record.velocities[particle]);
            }
        }
        m_soft_bodies.set_state(positions, velocities);
        m_particle_state_dirty = false;
    }

    // The particles (inverse masses, pins) and the soft bodies' settings, uploaded when they changed: a particle pinned
    // rigidly to the world or to a body that takes no correction follows its point (kinematic, as the abstraction's
    // soft-body solver holds it); its soft body's constraints iterate its sub-steps over the world's substeps per substep.
    const std::uint32_t substeps = std::max(pending.configuration.solver.sub_steps, 1U);
    std::vector<GpuParticle> particles(m_particle_count);
    std::vector<GpuSoftBody> soft_bodies;
    std::uint32_t iterations = 0;
    const GpuBodyFlag simulated = GpuBodyFlag::SIMULATED | GpuBodyFlag::AWAKE;
    for (std::uint32_t soft_slot = 0; soft_slot < m_soft_body_entries.size(); ++soft_slot)
    {
        const SoftBodyEntry& entry = m_soft_body_entries[soft_slot];
        if (!entry.handle)
        {
            continue;
        }
        const SoftBodyDescription& settings = pending.soft_bodies[soft_slot].configuration;
        const SoftBodyModel& model = *entry.model;
        const auto index = static_cast<std::uint32_t>(soft_bodies.size());
        const std::uint32_t soft_iterations = (std::max(settings.sub_steps, 1U) + substeps - 1) / substeps;
        iterations = std::max(iterations, soft_iterations);
        soft_bodies.push_back({.gravity_scale = settings.gravity_scale,
                               .damping = settings.damping,
                               .radius = settings.particle_radius,
                               .friction = settings.friction,
                               .iterations = soft_iterations,
                               .flags = settings.self_collision ? GPU_SOFT_BODY_SELF_COLLISION : 0U,
                               .first_particle = entry.first_particle,
                               .particle_count = model.particle_count()});
        for (std::uint32_t particle = 0; particle < model.particle_count(); ++particle)
        {
            const float inverse_mass = model.inverse_masses()[particle];
            particles[entry.first_particle + particle] = {.inverse_mass = inverse_mass,
                                                          .flags = std::to_underlying(simulated | (inverse_mass > 0.0F ? GpuBodyFlag::DYNAMIC : GpuBodyFlag::NONE)),
                                                          .soft_body = index,
                                                          .pin_body = NO_SOLVER_BODY,
                                                          .rest = vector(model.rest_positions()[particle]),
                                                          .pin_point = {}};
        }
        for (const ParticlePin& pin : settings.pins)
        {
            const RigidBodyRecord* body = pin.body ? find(pending, pin.body) : nullptr;
            if (pin.particle >= model.particle_count() || pin.compliance != 0.0F || (pin.body && (body == nullptr || effective_mass(*body).mass > 0.0F)))
            {
                continue;
            }
            GpuParticle& held = particles[entry.first_particle + pin.particle];
            held.inverse_mass = 0.0F;
            held.flags = std::to_underlying(simulated | GpuBodyFlag::KINEMATIC);
            held.pin_body = body != nullptr ? slot(pin.body) : NO_SOLVER_BODY;
            held.pin_point = vector(pin.point);
        }
    }
    m_soft_bodies.set_iterations(iterations);
    if (!same_bytes(std::span<const GpuParticle>{particles}, std::span<const GpuParticle>{m_particle_data}) ||
        !same_bytes(std::span<const GpuSoftBody>{soft_bodies}, std::span<const GpuSoftBody>{m_soft_body_data}))
    {
        m_soft_bodies.set_particles(particles, soft_bodies);
        m_particle_data = std::move(particles);
        m_soft_body_data = std::move(soft_bodies);
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

    // And the particles: their speed and gravity's growth.
    for (const SoftBodyRecord& record : pending.soft_bodies)
    {
        if (!record.handle)
        {
            continue;
        }
        float fastest = 0.0F;
        for (const Vector3 velocity : record.velocities)
        {
            fastest = std::max(fastest, length(velocity));
        }
        farthest = std::max(farthest, fastest * seconds + gravity * std::abs(record.configuration.gravity_scale));
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
    const bool soft_bodies = std::ranges::any_of(pending.soft_bodies, [](const SoftBodyRecord& record) { return record.handle.is_valid(); });
    if (body_count == 0 && !soft_bodies && m_particle_count == 0)
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
        update_soft_bodies(pending);
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
            run_commands();
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
        if (m_timer.enabled())
        {
            m_timer.read(pending.statistics.phase_seconds);
        }
        else
        {
            pending.statistics.phase_seconds[phase(StepPhase::SOLVER)] = seconds_between(start, solved);
        }
        const double gpu_events = m_timer.enabled() ? pending.statistics.phase_seconds[phase(StepPhase::EVENTS)] : 0.0;

        // More touching contacts than the reports had room for: reported again (the step's contacts are still on the GPU).
        if (counts.reported > m_solver.report_capacity())
        {
            stage = "reading the contacts back";
            m_command_pool->reset();
            {
                ComputeRecorder recorder{*m_command_buffer};
                m_solver.record_reports_again(recorder, counts.reported, m_retired);
                m_report_readback.reserve(m_device, std::uint64_t{m_solver.report_capacity()} * sizeof(GpuContactReport), m_retired);
                recorder.barrier({.memory_barriers = std::span{&COMPUTE_TO_TRANSFER, 1}});
                const std::array regions{BufferCopyRegion{.size = std::uint64_t{counts.reported} * sizeof(GpuContactReport)}};
                recorder.copy_buffer(m_solver.reports_buffer().handle(), m_report_readback.buffer().handle(), regions);
                recorder.barrier({.memory_barriers = std::span{&TRANSFER_TO_HOST, 1}});
            }
            run_commands();
        }
        stage = "reading the step back";
        read_back(pending);
        const std::uint32_t kept_points = read_contacts(pending, counts.reported);
        read_triggers(pending, counts.trigger_overlaps);
        m_query_tree_current = m_solver.trigger_capacity() > 0;
        m_solver.complete_step(counts);
        m_lists_changed = false;
        pending.statistics.pairs = counts.candidates;
        pending.statistics.contacts = counts.touching_points + kept_points;
        m_retired.clear();
        (void)m_device.deleter().collect();
        pending.statistics.phase_seconds[phase(StepPhase::EVENTS)] = gpu_events + seconds_between(solved, Clock::now());
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
    const std::uint32_t particle_count = m_soft_bodies.particle_count();
    m_readback.reserve(m_device,
                       std::uint64_t{body_count} * (sizeof(GpuBodyReadback) + sizeof(std::uint32_t)) + sizeof(GpuSolverCounts) +
                           (std::uint64_t{joint_count} + particle_count) * 2 * sizeof(GpuVector),
                       m_retired);
    m_report_readback.reserve(m_device, std::uint64_t{m_solver.report_capacity()} * sizeof(GpuContactReport), m_retired);

    m_command_pool->reset();
    ComputeRecorder recorder{*m_command_buffer};
    m_timer.begin(recorder);

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
    m_solver.record_uploads(recorder, body_count, body_count + particle_count, m_retired);
    m_soft_bodies.record_uploads(recorder, m_retired);

    // The parameters and the patches; this step's forces start from zero (the patches give some).
    const GpuStepParameters parameters{
        .gravity = vector(pending.configuration.gravity, m_substep_seconds),
        .body_count = body_count,
        .patch_count = patch_count,
        .substeps = std::max(pending.configuration.solver.sub_steps, 1U),
        .particle_count = particle_count,
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
        .particle_positions = m_soft_bodies.positions().index(),
        .particle_velocities = m_soft_bodies.velocities().index(),
        .particle_displacements = m_soft_bodies.displacements().index(),
        .particle_previous_velocities = m_soft_bodies.previous_velocities().index(),
        .particles = m_soft_bodies.particles().index(),
        .soft_bodies = m_soft_bodies.soft_bodies().index(),
        .reserved = {},
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
        if (body_count > 0)
        {
            recorder.fill_buffer(m_forces.buffer().handle(), 0, vectors, 0);
            recorder.fill_buffer(m_torques.buffer().handle(), 0, vectors, 0);
        }
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
        m_soft_bodies.record_backup(recorder, restore);
        for (std::size_t array = 0; array < state_arrays.size() && body_count > 0; ++array)
        {
            const Buffer& live = state_arrays[array]->buffer();
            const Buffer& backup = m_backups[array].buffer();
            const std::array regions{BufferCopyRegion{.size = std::uint64_t{body_count} * state_arrays[array]->element_size()}};
            recorder.copy_buffer(restore ? backup.handle() : live.handle(), restore ? live.handle() : backup.handle(), regions);
        }
    };
    recorder.barrier({.memory_barriers = std::span{&BEFORE_PASSES, 1}});
    m_timer.mark(recorder, StepPhase::SOLVER);
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
                           .node_count = body_count + particle_count,
                           .step_seconds = seconds,
                           .speculative_distance = pending.configuration.collision.speculative_distance + step_motion(pending, seconds),
                           .solver = solver,
                           .sleep = pending.configuration.sleep,
                           .collision = pending.configuration.collision,
                           .lists_changed = m_lists_changed,
                           .timer = &m_timer};
    m_solver.record_detection(recorder, step);
    const std::uint32_t position_iterations = std::max(solver.position_iterations, 1U);
    const std::uint32_t iterations = std::max(position_iterations, m_soft_bodies.iterations());
    for (std::uint32_t substep = 0; substep < parameters.substeps; ++substep)
    {
        // The bodies' prediction, then the particles' (they may follow a body); each iteration the soft constraints, then
        // the joints and contacts (soft bodies may ask for more iterations than the world's, rigid ones fewer). The soft
        // bodies' passes timed apart.
        const bool particles = particle_count > 0;
        const auto soft = [&](bool begins) {
            if (particles)
            {
                m_timer.mark(recorder, begins ? StepPhase::SOFT_BODIES : StepPhase::SOLVER);
            }
        };
        pass(m_pipelines.integrate(), body_count);
        soft(true);
        m_soft_bodies.record_integrate(recorder, parameters_index);
        for (std::uint32_t iteration = 0; iteration < iterations; ++iteration)
        {
            soft(true);
            m_soft_bodies.record_iteration(recorder, parameters_index, iteration);
            soft(false);
            if (iteration < position_iterations)
            {
                m_solver.record_positions(recorder, iteration == 0);
            }
        }
        pass(m_pipelines.update_velocities(), body_count);
        soft(true);
        m_soft_bodies.record_velocity_update(recorder, parameters_index);
        soft(false);
        for (std::uint32_t iteration = 0; iteration < std::max(solver.velocity_iterations, 1U); ++iteration)
        {
            m_solver.record_velocities(recorder, iteration == 0);
        }
    }
    m_solver.record_joint_forces(recorder);
    m_solver.record_sleep(recorder);
    m_timer.mark(recorder, StepPhase::EVENTS);
    m_solver.record_reports(recorder);
    m_solver.record_triggers(recorder);

    // The results into the readback buffer: the bodies' arrays, their flags, the solver's counts, the joints' results.
    recorder.barrier({.memory_barriers = std::span{&COMPUTE_TO_TRANSFER, 1}});
    std::uint64_t offset = 0;
    for (const GpuArray* array : {&m_centers, &m_orientations, &m_linear_velocities, &m_angular_velocities})
    {
        if (vectors > 0)
        {
            const std::array regions{BufferCopyRegion{.source_offset = 0, .destination_offset = offset, .size = vectors}};
            recorder.copy_buffer(array->buffer().handle(), m_readback.buffer().handle(), regions);
        }
        offset += vectors;
    }
    if (words > 0)
    {
        const std::array flag_regions{BufferCopyRegion{.source_offset = 0, .destination_offset = offset, .size = words}};
        recorder.copy_buffer(m_flags.buffer().handle(), m_readback.buffer().handle(), flag_regions);
    }
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
    offset += std::uint64_t{joint_count} * 2 * sizeof(GpuVector);
    for (const GpuArray* array : {&m_soft_bodies.positions(), &m_soft_bodies.velocities()})
    {
        if (particle_count > 0)
        {
            const std::array regions{
                BufferCopyRegion{.source_offset = 0, .destination_offset = offset, .size = std::uint64_t{particle_count} * sizeof(GpuVector)}};
            recorder.copy_buffer(array->buffer().handle(), m_readback.buffer().handle(), regions);
        }
        offset += std::uint64_t{particle_count} * sizeof(GpuVector);
    }
    const std::array report_regions{BufferCopyRegion{.size = std::uint64_t{m_solver.report_capacity()} * sizeof(GpuContactReport)}};
    recorder.copy_buffer(m_solver.reports_buffer().handle(), m_report_readback.buffer().handle(), report_regions);
    if (m_solver.trigger_capacity() > 0)
    {
        const std::uint64_t trigger_bytes = std::uint64_t{m_solver.trigger_capacity()} * sizeof(GpuTriggerOverlap);
        m_trigger_readback.reserve(m_device, trigger_bytes, m_retired);
        const std::array trigger_regions{BufferCopyRegion{.size = trigger_bytes}};
        recorder.copy_buffer(m_solver.trigger_results_buffer().handle(), m_trigger_readback.buffer().handle(), trigger_regions);
    }
    m_timer.end(recorder);
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

    // The particles' positions and velocities, after the joints' results.
    const std::size_t particles = results + std::size_t{m_solver.joint_count()} * 2 * sizeof(GpuVector);
    const std::size_t particle_vectors = std::size_t{m_soft_bodies.particle_count()} * sizeof(GpuVector);
    for (std::uint32_t soft_slot = 0; soft_slot < m_soft_body_entries.size() && soft_slot < pending.soft_bodies.size(); ++soft_slot)
    {
        const SoftBodyEntry& entry = m_soft_body_entries[soft_slot];
        SoftBodyRecord& record = pending.soft_bodies[soft_slot];
        if (!entry.handle || !record.handle || record.positions.size() != entry.model->particle_count())
        {
            continue;
        }
        for (std::uint32_t particle = 0; particle < entry.model->particle_count(); ++particle)
        {
            record.positions[particle] = vector3(read_vector(bytes, particles, entry.first_particle + particle));
            record.velocities[particle] = vector3(read_vector(bytes, particles + particle_vectors, entry.first_particle + particle));
        }
    }
}

} // namespace gfx::physics::gpu_xpbd
