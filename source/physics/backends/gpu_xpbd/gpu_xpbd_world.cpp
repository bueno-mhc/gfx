// GpuXpbdBackend: the edits become patches of the GPU body arrays, a step is one command buffer (patches, then substeps
// of prediction and velocity update), submitted and waited for on the stepping thread, and read back into the pending
// snapshot.
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
import gfx.physics.world;
import :settings;
import :resources;
import :backend;

namespace gfx::physics::gpu_xpbd
{

namespace
{

/// The design document's phases that bring what G0 refuses.
constexpr std::string_view COLLISIONS_PHASE = "collisions arrive with phases G2 to G5 of docs/gpu-xpbd-design.md";

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

} // namespace

// ===================================================================================================== the edits

/// Marks what each edit changed of the bodies (forces and impulses come after: they need the final centers).
struct ChangeMarker
{
    GpuXpbdBackend& backend;

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
    }

    void operator()(const edits::SetBodyMassProperties& change) const
    {
        reconfigure(change.body, true);
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

    // The rest: the records are all the backend needs (the world's settings, motion quality, sleep permission...).
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

    void push(RigidBodyHandle body) const
    {
        BodyEntry& entry = backend.body_entry(body);
        entry.pushed = true;
        entry.woken = true;
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
    // What phase G0 cannot simulate yet is refused when recorded, never ignored; user constraints are refused for good.
    if (const auto* collider = std::get_if<edits::CreateCollider>(&edit))
    {
        throw_unsupported(identity(), "RigidBody::add_collider", PhysicsCapability::WORLD_OBJECTS,
                          std::format("the GPU backend does not simulate colliders yet ({})", COLLISIONS_PHASE),
                          "give the body its mass properties (RigidBodyDescription::mass_properties), or use another backend",
                          {.rigid_body = collider->body});
    }
    if (std::holds_alternative<edits::CreateJoint>(edit))
    {
        throw_unsupported(identity(), "PhysicsWorld::create_joint", PhysicsCapability::WORLD_OBJECTS,
                          "the GPU backend does not simulate joints yet (they arrive with phase G6 of docs/gpu-xpbd-design.md)",
                          "use another backend");
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

void
GpuXpbdBackend::apply(std::span<const WorldEdit> edits,
                      WorldSnapshot& pending)
{
    // What changed first, then the forces and impulses about the centers the patches will have.
    const ChangeMarker marker{.backend = *this};
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

    // The flags: kind, awake, locks.
    GpuBodyFlag flags = GpuBodyFlag::SIMULATED;
    flags |= dynamic ? GpuBodyFlag::DYNAMIC : GpuBodyFlag::NONE;
    flags |= kinematic ? GpuBodyFlag::KINEMATIC : GpuBodyFlag::NONE;
    flags |= moves ? GpuBodyFlag::AWAKE : GpuBodyFlag::NONE;
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
        m_substep_seconds = seconds / static_cast<float>(substeps);
        record_step(body_count, substeps, pending.configuration.gravity);
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
        const Clock::time_point solved = Clock::now();
        pending.statistics.phase_seconds[phase(StepPhase::SOLVER)] = seconds_between(start, solved);
        stage = "reading the step back";
        read_back(pending);
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
                            std::uint32_t substeps,
                            Vector3 gravity)
{
    // Host buffers large enough (before recording: their bindless indices go into the parameters).
    const auto patch_count = static_cast<std::uint32_t>(m_body_patches.size());
    m_parameters.reserve(m_device, sizeof(GpuStepParameters), m_retired);
    m_patches.reserve(m_device, std::max<std::size_t>(patch_count, 1) * sizeof(GpuBodyPatch), m_retired);
    m_readback.reserve(m_device, std::uint64_t{body_count} * (sizeof(GpuBodyReadback) + sizeof(std::uint32_t)), m_retired);

    m_command_pool->reset();
    ComputeRecorder recorder{*m_command_buffer};

    // The arrays hold every slot (grown ones keep the slots the GPU had).
    const std::uint32_t kept = m_body_count;
    for (GpuArray* array : {&m_centers, &m_orientations, &m_linear_velocities, &m_angular_velocities, &m_inverse_inertias, &m_inertias, &m_forces,
                            &m_torques, &m_displacements, &m_rotations, &m_flags})
    {
        (void)array->reserve(m_device, recorder, body_count, kept, m_retired);
    }
    m_body_count = body_count;

    // The parameters and the patches.
    const GpuStepParameters parameters{
        .gravity = vector(gravity, m_substep_seconds),
        .body_count = body_count,
        .patch_count = patch_count,
        .substeps = substeps,
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
    };
    std::memcpy(m_parameters.bytes().data(), &parameters, sizeof(parameters));
    m_parameters.buffer().flush_mapped_range();
    if (patch_count > 0)
    {
        std::memcpy(m_patches.bytes().data(), m_body_patches.data(), m_body_patches.size() * sizeof(GpuBodyPatch));
        m_patches.buffer().flush_mapped_range();
    }
    const std::uint32_t parameters_index = m_parameters.index();
    const auto pass = [&recorder, parameters_index](ComputePipelineHandle pipeline, std::uint32_t count) {
        recorder.bind_pipeline(pipeline);
        recorder.push_constants(parameters_index);
        recorder.dispatch(workgroups(count));
        recorder.barrier({.memory_barriers = std::span{&COMPUTE_TO_COMPUTE, 1}});
    };

    // The passes: patches, then the substeps.
    recorder.barrier({.memory_barriers = std::span{&BEFORE_PASSES, 1}});
    if (patch_count > 0)
    {
        pass(m_pipelines.patch_bodies(), patch_count);
    }
    for (std::uint32_t substep = 0; substep < substeps; ++substep)
    {
        pass(m_pipelines.integrate(), body_count);
        pass(m_pipelines.update_velocities(), body_count);
    }

    // The results into the readback buffer; this step's forces cleared.
    recorder.barrier({.memory_barriers = std::span{&COMPUTE_TO_TRANSFER, 1}});
    const std::uint64_t vectors = std::uint64_t{body_count} * sizeof(GpuVector);
    std::uint64_t offset = 0;
    for (const GpuArray* array : {&m_centers, &m_orientations, &m_linear_velocities, &m_angular_velocities})
    {
        const std::array regions{BufferCopyRegion{.source_offset = 0, .destination_offset = offset, .size = vectors}};
        recorder.copy_buffer(array->buffer().handle(), m_readback.buffer().handle(), regions);
        offset += vectors;
    }
    const std::array flag_regions{BufferCopyRegion{.source_offset = 0, .destination_offset = offset, .size = std::uint64_t{body_count} * sizeof(std::uint32_t)}};
    recorder.copy_buffer(m_flags.buffer().handle(), m_readback.buffer().handle(), flag_regions);
    recorder.fill_buffer(m_forces.buffer().handle(), 0, vectors, 0);
    recorder.fill_buffer(m_torques.buffer().handle(), 0, vectors, 0);
    recorder.barrier({.memory_barriers = std::span{&TRANSFER_TO_HOST, 1}});
}

void
GpuXpbdBackend::read_back(WorldSnapshot& pending)
{
    // Arrays of centers, orientations, linear and angular velocities, then flags.
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
        if ((flags & std::to_underlying(GpuBodyFlag::AWAKE)) == 0)
        {
            continue;
        }

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
    }
}

} // namespace gfx::physics::gpu_xpbd
