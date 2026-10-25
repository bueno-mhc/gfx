// SteppedWorld: handles and slots, recording edits (liveness, the backend's checks, cascading destructions), stepping
// on the caller or on the world's thread, applying the edits to the pending records, mass properties and boxes,
// publishing, the reader handshake, and the pipeline as the backend describes it.
module gfx.physics.world;

import std;
import gfx.core.errors;
import gfx.core.handles;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.handles;
import gfx.physics.interface;
import :state;

namespace gfx::physics
{

namespace
{

// ===================================================================================================== slots

template<class Tag>
[[nodiscard]] auto
slot_of(Handle<Tag> handle) noexcept -> std::uint32_t
{
    return handle.index(WorldShared::key());
}

template<class Tag>
[[nodiscard]] auto
live_in(const SlotList& slots,
        Handle<Tag> handle) noexcept -> bool
{
    const std::uint32_t index = handle.index(WorldShared::key());
    const std::uint32_t generation = handle.generation(WorldShared::key());
    return generation != 0 && index < slots.live.size() && slots.live[index] != 0 && slots.generations[index] == generation;
}

/// A slot for a new object (a released one first).
[[nodiscard]] auto
issue_slot(SlotList& slots,
           const BackendIdentity& backend,
           std::string_view operation) -> SlotNumber
{
    if (!slots.free.empty())
    {
        const std::uint32_t index = slots.free.back();
        slots.free.pop_back();
        slots.live[index] = 1;
        return {.index = index, .generation = slots.generations[index]};
    }
    const auto index = static_cast<std::uint32_t>(slots.generations.size());
    if (index > MAXIMUM_HANDLE_INDEX)
    {
        reject(backend, operation, std::format("the world holds {} objects of this kind, the most a handle can name", index), "destroy objects first", {});
    }
    slots.generations.push_back(1);
    slots.live.push_back(1);
    return {.index = index, .generation = 1};
}

/// Frees @p handle's slot: its next object gets the next generation.
template<class Tag>
void
release_slot(SlotList& slots,
             Handle<Tag> handle) noexcept
{
    const std::uint32_t index = slot_of(handle);
    if (index >= slots.generations.size() || slots.generations[index] != handle.generation(WorldShared::key()))
    {
        return;
    }
    slots.generations[index] = slots.generations[index] == MAXIMUM_HANDLE_GENERATION ? 1 : static_cast<std::uint8_t>(slots.generations[index] + 1);
    slots.live[index] = 0;
    slots.free.push_back(index);
}

/// Grows @p values to hold index @p index.
template<class Value>
auto
grown(std::vector<Value>& values,
      std::uint32_t index) -> Value&
{
    if (index >= values.size())
    {
        values.resize(static_cast<std::size_t>(index) + 1);
    }
    return values[index];
}

/// Removes @p value from @p values (if there).
template<class Value>
void
erase_value(std::vector<Value>& values,
            const Value& value) noexcept
{
    std::erase(values, value);
}

// ===================================================================================================== liveness of what an edit names

/// Throws the PhysicsError of an edit naming an object that is not alive.
template<class Handle>
[[noreturn]] void
reject_dead(const WorldShared& shared,
            std::string_view operation,
            Handle handle,
            std::string_view reason,
            const PhysicsErrorContext& context)
{
    reject(shared.identity, operation, std::format("{} {}", handle, reason),
           "keep the object (its std::unique_ptr) alive while it is used, and its body as long as it is attached", context);
}

void
require_body(const WorldShared& shared,
             std::string_view operation,
             RigidBodyHandle body)
{
    if (!shared.is_live(body))
    {
        reject_dead(shared, operation, body, "is not a live rigid body of this world (destroyed, or another world's)", {.world = shared.world, .rigid_body = body});
    }
}

/// Requires every object @p change names to be alive.
template<class Change>
void
require_live(const WorldShared& shared,
             const Change& change,
             std::string_view operation)
{
    if constexpr (requires { change.collider; })
    {
        if (!shared.is_live(change.collider))
        {
            const std::uint32_t slot = slot_of(change.collider);
            const RigidBodyHandle body = slot < shared.collider_bodies.size() ? shared.collider_bodies[slot] : RigidBodyHandle{};
            reject_dead(shared, operation, change.collider, body && !shared.is_live(body) ? std::format("was destroyed with its body {}", body) : "was destroyed",
                        {.world = shared.world, .collider = change.collider});
        }
    }
    else if constexpr (requires { change.joint; })
    {
        if (!shared.is_live(change.joint))
        {
            const std::uint32_t slot = slot_of(change.joint);
            const auto bodies = slot < shared.joint_bodies.size() ? shared.joint_bodies[slot] : std::pair<RigidBodyHandle, RigidBodyHandle>{};
            const RigidBodyHandle gone = bodies.first && !shared.is_live(bodies.first) ? bodies.first : bodies.second && !shared.is_live(bodies.second) ? bodies.second : RigidBodyHandle{};
            reject_dead(shared, operation, change.joint, gone ? std::format("was destroyed with its body {}", gone) : "was destroyed",
                        {.world = shared.world, .joint = change.joint});
        }
    }
    else if constexpr (requires { change.trigger; })
    {
        if (!shared.is_live(change.trigger))
        {
            reject_dead(shared, operation, change.trigger, "was destroyed (with its body, if it had one)", {.world = shared.world, .trigger = change.trigger});
        }
    }
    else if constexpr (requires { change.soft_body; })
    {
        if (!shared.is_live(change.soft_body))
        {
            reject_dead(shared, operation, change.soft_body, "was destroyed", {.world = shared.world, .soft_body = change.soft_body});
        }
        if constexpr (requires { change.pin; })
        {
            if (change.pin.body)
            {
                require_body(shared, operation, change.pin.body);
            }
        }
    }
    else if constexpr (requires { change.body; })
    {
        require_body(shared, operation, change.body);
    }
}

// ===================================================================================================== the pipeline as the backend describes it

/// The BackendCapabilityError of an operation on a component the backend runs itself.
[[noreturn]] void
refuse_backend_component(const BackendIdentity& backend,
                         std::string_view operation,
                         PhysicsCapability capability)
{
    throw_unsupported(backend, operation, capability, "the world runs the backend's own component, which cannot be driven from outside",
                      "use the abstraction's component directly (gfx.physics.spatial, gfx.physics.narrow_phase), or a backend that takes components "
                      "(PhysicsBackendKind::REFERENCE)");
}

class BackendBroadPhase final : public BroadPhase
{
public:
    BackendBroadPhase(BackendIdentity backend,
                      BroadPhaseKind kind) noexcept
        : BroadPhase{backend}
        , m_kind{kind}
    {
    }

    [[nodiscard]] auto kind() const noexcept -> BroadPhaseKind override
    {
        return m_kind;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return false;
    }

    auto insert(ColliderHandle /*collider*/,
                const Aabb& /*box*/,
                const CollisionFilter& /*filter*/) -> SpatialProxyHandle override
    {
        refuse_backend_component(backend(), "BroadPhase::insert", PhysicsCapability::CUSTOM_BROAD_PHASE);
    }

    void remove(SpatialProxyHandle /*proxy*/) override
    {
        refuse_backend_component(backend(), "BroadPhase::remove", PhysicsCapability::CUSTOM_BROAD_PHASE);
    }

    auto move(SpatialProxyHandle /*proxy*/,
              const Aabb& /*box*/,
              Vector3 /*displacement*/) -> bool override
    {
        refuse_backend_component(backend(), "BroadPhase::move", PhysicsCapability::CUSTOM_BROAD_PHASE);
    }

    void set_filter(SpatialProxyHandle /*proxy*/,
                    const CollisionFilter& /*filter*/) override
    {
        refuse_backend_component(backend(), "BroadPhase::set_filter", PhysicsCapability::CUSTOM_BROAD_PHASE);
    }

    void update(TaskScheduler* /*scheduler*/) override
    {
        refuse_backend_component(backend(), "BroadPhase::update", PhysicsCapability::CUSTOM_BROAD_PHASE);
    }

    [[nodiscard]] auto pairs() const -> std::span<const BroadPhasePair> override
    {
        refuse_backend_component(backend(), "BroadPhase::pairs", PhysicsCapability::CUSTOM_BROAD_PHASE);
    }

    [[nodiscard]] auto new_pairs() const -> std::span<const BroadPhasePair> override
    {
        refuse_backend_component(backend(), "BroadPhase::new_pairs", PhysicsCapability::CUSTOM_BROAD_PHASE);
    }

    [[nodiscard]] auto ended_pairs() const -> std::span<const BroadPhasePair> override
    {
        refuse_backend_component(backend(), "BroadPhase::ended_pairs", PhysicsCapability::CUSTOM_BROAD_PHASE);
    }

private:
    BroadPhaseKind m_kind;
};

class BackendNarrowPhase final : public NarrowPhase
{
public:
    BackendNarrowPhase(BackendIdentity backend,
                       NarrowPhaseKind kind) noexcept
        : NarrowPhase{backend}
        , m_kind{kind}
    {
    }

    [[nodiscard]] auto kind() const noexcept -> NarrowPhaseKind override
    {
        return m_kind;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return false;
    }

    void collide(const ShapeInstance& /*first*/,
                 const ShapeInstance& /*second*/,
                 ContactCollector& /*contacts*/,
                 const CollisionSettings& /*settings*/) const override
    {
        refuse_backend_component(backend(), "NarrowPhase::collide", PhysicsCapability::CUSTOM_NARROW_PHASE);
    }

private:
    NarrowPhaseKind m_kind;
};

class BackendSolver final : public Solver
{
public:
    BackendSolver(BackendIdentity backend,
                  SolverKind kind) noexcept
        : Solver{backend}
        , m_kind{kind}
    {
    }

    [[nodiscard]] auto kind() const noexcept -> SolverKind override
    {
        return m_kind;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return false;
    }

private:
    SolverKind m_kind;
};

class BackendIntegrator final : public Integrator
{
public:
    BackendIntegrator(BackendIdentity backend,
                      IntegratorKind kind) noexcept
        : Integrator{backend}
        , m_kind{kind}
    {
    }

    [[nodiscard]] auto kind() const noexcept -> IntegratorKind override
    {
        return m_kind;
    }

    [[nodiscard]] auto is_external() const noexcept -> bool override
    {
        return false;
    }

    [[nodiscard]] auto requirements() const noexcept -> IntegratorRequirements override
    {
        return {};
    }

    void predict(const IntegrationState& /*state*/,
                 float /*seconds*/) override
    {
        refuse_backend_component(backend(), "Integrator::predict", PhysicsCapability::CUSTOM_INTEGRATOR);
    }

    void finalize(const IntegrationState& /*state*/,
                  float /*seconds*/) override
    {
        refuse_backend_component(backend(), "Integrator::finalize", PhysicsCapability::CUSTOM_INTEGRATOR);
    }

private:
    IntegratorKind m_kind;
};

// ===================================================================================================== applying edits to the records

/// @p properties at @p density.
[[nodiscard]] auto
scaled(const MassProperties& properties,
       float density) noexcept -> MassProperties
{
    return {.mass = properties.mass * density, .center_of_mass = properties.center_of_mass, .inertia = properties.inertia * density};
}

/// The angular velocity that turns @p from into @p to in @p seconds (the shortest way).
[[nodiscard]] auto
angular_velocity_between(const Quaternion& from,
                         const Quaternion& to,
                         float seconds) noexcept -> Vector3
{
    Quaternion turn = to * conjugate(from);
    if (turn.w < 0.0F)
    {
        turn = {.x = -turn.x, .y = -turn.y, .z = -turn.z, .w = -turn.w};
    }
    const Vector3 axis{.x = turn.x, .y = turn.y, .z = turn.z};
    const float sine = length(axis);
    if (sine <= 1e-7F)
    {
        return axis * (2.0F / seconds);
    }
    const float angle = 2.0F * std::atan2(sine, turn.w);
    return axis * (angle / (sine * seconds));
}

/// What each edit changes in the pending records.
struct RecordApplier
{
    SteppedWorld::Implementation& implementation;
    WorldSnapshot& pending;

    void body_mass_changed(RigidBodyHandle body) const
    {
        grown(implementation.mass_changed, slot_of(body)) = 1;
    }

    template<class Change>
    [[nodiscard]] auto body(const Change& change) const noexcept -> RigidBodyRecord*
    {
        return find(pending, change.body);
    }

    // The world.
    void operator()(const edits::SetGravity& change) const
    {
        pending.configuration.gravity = change.gravity;
        pending.configuration_changed = true;
    }

    void operator()(const edits::SetSolverDescription& change) const
    {
        pending.configuration.solver = change.description;
        pending.configuration_changed = true;
    }

    void operator()(const edits::SetSleepDescription& change) const
    {
        pending.configuration.sleep = change.description;
        pending.configuration_changed = true;
    }

    // Rigid bodies.
    void operator()(const edits::CreateRigidBody& change) const
    {
        grown(pending.rigid_bodies, slot_of(change.body)) = {.handle = change.body,
                                                              .configuration = change.description,
                                                              .mass = {},
                                                              .state = {.pose = change.description.pose,
                                                                        .linear_velocity = change.description.linear_velocity,
                                                                        .angular_velocity = change.description.angular_velocity,
                                                                        .awake = change.description.awake},
                                                              .configuration_changed = true};
        body_mass_changed(change.body);
    }

    void operator()(const edits::DestroyRigidBody& change) const
    {
        if (RigidBodyRecord* record = body(change))
        {
            *record = {};
        }
        implementation.released_bodies.push_back(change.body);

        // Pins to the body are released.
        for (SoftBodyRecord& soft_body : pending.soft_bodies)
        {
            if (std::erase_if(soft_body.configuration.pins, [&change](const ParticlePin& pin) { return pin.body == change.body; }) > 0)
            {
                soft_body.configuration_changed = true;
            }
        }
    }

    void operator()(const edits::SetBodyPose& change) const
    {
        if (RigidBodyRecord* record = body(change))
        {
            record->state.pose = change.pose;
        }
    }

    void operator()(const edits::SetBodyLinearVelocity& change) const
    {
        if (RigidBodyRecord* record = body(change))
        {
            record->state.linear_velocity = change.velocity;
        }
    }

    void operator()(const edits::SetBodyAngularVelocity& change) const
    {
        if (RigidBodyRecord* record = body(change))
        {
            record->state.angular_velocity = change.velocity;
        }
    }

    void operator()(const edits::MoveKinematicBody& change) const
    {
        // The velocities that reach the target in the time given.
        if (RigidBodyRecord* record = body(change))
        {
            record->state.linear_velocity = (change.target.position - record->state.pose.position) / change.seconds;
            record->state.angular_velocity = angular_velocity_between(record->state.pose.orientation, change.target.orientation, change.seconds);
            record->state.awake = true;
        }
    }

    // Forces, torques and impulses are the backend's alone.
    void operator()(const edits::ApplyForce& /*change*/) const
    {
    }

    void operator()(const edits::ApplyTorque& /*change*/) const
    {
    }

    void operator()(const edits::ApplyLinearImpulse& /*change*/) const
    {
    }

    void operator()(const edits::ApplyAngularImpulse& /*change*/) const
    {
    }

    void operator()(const edits::SetBodyKind& change) const
    {
        if (RigidBodyRecord* record = body(change))
        {
            record->configuration.kind = change.kind;
            record->configuration_changed = true;
            body_mass_changed(change.body);
        }
    }

    void operator()(const edits::SetBodyMassProperties& change) const
    {
        if (RigidBodyRecord* record = body(change))
        {
            record->configuration.mass_properties = change.properties;
            record->configuration_changed = true;
            body_mass_changed(change.body);
        }
    }

    void operator()(const edits::SetBodyDamping& change) const
    {
        if (RigidBodyRecord* record = body(change))
        {
            record->configuration.linear_damping = change.linear;
            record->configuration.angular_damping = change.angular;
            record->configuration_changed = true;
        }
    }

    void operator()(const edits::SetBodyGravityScale& change) const
    {
        if (RigidBodyRecord* record = body(change))
        {
            record->configuration.gravity_scale = change.scale;
            record->configuration_changed = true;
        }
    }

    void operator()(const edits::SetBodyMotionQuality& change) const
    {
        if (RigidBodyRecord* record = body(change))
        {
            record->configuration.motion_quality = change.quality;
            record->configuration_changed = true;
        }
    }

    void operator()(const edits::SetBodyMotionLocks& change) const
    {
        if (RigidBodyRecord* record = body(change))
        {
            record->configuration.locks = change.locks;
            record->configuration_changed = true;
        }
    }

    void operator()(const edits::SetBodySleepAllowed& change) const
    {
        if (RigidBodyRecord* record = body(change))
        {
            record->configuration.allow_sleep = change.allowed;
            record->configuration_changed = true;
        }
    }

    void operator()(const edits::SetBodyAwake& change) const
    {
        if (RigidBodyRecord* record = body(change))
        {
            record->state.awake = change.awake;
        }
    }

    // Colliders.
    void operator()(const edits::CreateCollider& change) const
    {
        grown(pending.colliders, slot_of(change.collider)) = {.handle = change.collider,
                                                               .body = change.body,
                                                               .configuration = change.description,
                                                               .unit_mass = change.unit_mass,
                                                               .world_bounds = {},
                                                               .configuration_changed = true};
        body_mass_changed(change.body);
    }

    void operator()(const edits::DestroyCollider& change) const
    {
        if (ColliderRecord* record = find(pending, change.collider))
        {
            body_mass_changed(record->body);
            *record = {};
        }
        implementation.released_colliders.push_back(change.collider);
    }

    void operator()(const edits::SetColliderPose& change) const
    {
        if (ColliderRecord* record = find(pending, change.collider))
        {
            record->configuration.pose = change.pose;
            record->configuration_changed = true;
            body_mass_changed(record->body);
        }
    }

    void operator()(const edits::SetColliderMaterial& change) const
    {
        if (ColliderRecord* record = find(pending, change.collider))
        {
            record->configuration.material = change.material;
            record->configuration_changed = true;
        }
    }

    void operator()(const edits::SetColliderDensity& change) const
    {
        if (ColliderRecord* record = find(pending, change.collider))
        {
            record->configuration.density = change.density;
            record->configuration_changed = true;
            body_mass_changed(record->body);
        }
    }

    void operator()(const edits::SetColliderFilter& change) const
    {
        if (ColliderRecord* record = find(pending, change.collider))
        {
            record->configuration.filter = change.filter;
            record->configuration_changed = true;
        }
    }

    void operator()(const edits::SetColliderContactEvents& change) const
    {
        if (ColliderRecord* record = find(pending, change.collider))
        {
            record->configuration.contact_events = change.enabled;
            record->configuration_changed = true;
        }
    }

    // Joints.
    void operator()(const edits::CreateJoint& change) const
    {
        grown(pending.joints, slot_of(change.joint)) = {.handle = change.joint, .configuration = change.description, .state = {}, .configuration_changed = true};
    }

    void operator()(const edits::DestroyJoint& change) const
    {
        if (JointRecord* record = find(pending, change.joint))
        {
            *record = {};
        }
        implementation.released_joints.push_back(change.joint);
    }

    void operator()(const edits::UpdateJoint& change) const
    {
        if (JointRecord* record = find(pending, change.joint))
        {
            record->configuration = change.description;
            record->configuration_changed = true;
        }
    }

    void operator()(const edits::SetJointEnabled& change) const
    {
        if (JointRecord* record = find(pending, change.joint))
        {
            record->state.enabled = change.enabled;
            record->state.broken = record->state.broken && !change.enabled;
        }
    }

    // Triggers.
    void operator()(const edits::CreateTrigger& change) const
    {
        grown(pending.triggers, slot_of(change.trigger)) = {.handle = change.trigger, .configuration = change.description, .world_bounds = {}, .configuration_changed = true};
    }

    void operator()(const edits::DestroyTrigger& change) const
    {
        if (TriggerRecord* record = find(pending, change.trigger))
        {
            *record = {};
        }
        implementation.released_triggers.push_back(change.trigger);
    }

    void operator()(const edits::SetTriggerPose& change) const
    {
        if (TriggerRecord* record = find(pending, change.trigger))
        {
            record->configuration.pose = change.pose;
            record->configuration_changed = true;
        }
    }

    void operator()(const edits::SetTriggerFilter& change) const
    {
        if (TriggerRecord* record = find(pending, change.trigger))
        {
            record->configuration.filter = change.filter;
            record->configuration_changed = true;
        }
    }

    // Soft bodies.
    void operator()(const edits::CreateSoftBody& change) const
    {
        SoftBodyRecord& record = grown(pending.soft_bodies, slot_of(change.soft_body));
        record.handle = change.soft_body;
        record.configuration = change.description;
        record.positions.clear();
        record.velocities.clear();
        for (const SoftBodyParticle& particle : change.description.particles)
        {
            record.positions.push_back(particle.position);
            record.velocities.push_back(particle.velocity);
        }
        record.configuration_changed = true;
        implementation.forget_soft_body_model(change.soft_body);
    }

    void operator()(const edits::DestroySoftBody& change) const
    {
        if (SoftBodyRecord* record = find(pending, change.soft_body))
        {
            *record = {};
        }
        implementation.forget_soft_body_model(change.soft_body);
        implementation.released_soft_bodies.push_back(change.soft_body);
    }

    void operator()(const edits::PinParticle& change) const
    {
        if (SoftBodyRecord* record = find(pending, change.soft_body))
        {
            std::erase_if(record->configuration.pins, [&change](const ParticlePin& pin) { return pin.particle == change.pin.particle; });
            record->configuration.pins.push_back(change.pin);
            record->configuration_changed = true;
        }
    }

    void operator()(const edits::UnpinParticle& change) const
    {
        if (SoftBodyRecord* record = find(pending, change.soft_body))
        {
            std::erase_if(record->configuration.pins, [&change](const ParticlePin& pin) { return pin.particle == change.particle; });
            record->configuration_changed = true;
        }
    }

    void operator()(const edits::MoveParticle& change) const
    {
        if (SoftBodyRecord* record = find(pending, change.soft_body))
        {
            record->positions[change.particle] = change.position;
            record->velocities[change.particle] = {};
        }
    }

    void operator()(const edits::SetSoftBodyDamping& change) const
    {
        if (SoftBodyRecord* record = find(pending, change.soft_body))
        {
            record->configuration.damping = change.damping;
            record->configuration_changed = true;
        }
    }

    void operator()(const edits::SetSoftBodyGravityScale& change) const
    {
        if (SoftBodyRecord* record = find(pending, change.soft_body))
        {
            record->configuration.gravity_scale = change.scale;
            record->configuration_changed = true;
        }
    }

    void operator()(const edits::SetSoftBodySelfCollision& change) const
    {
        if (SoftBodyRecord* record = find(pending, change.soft_body))
        {
            record->configuration.self_collision = change.enabled;
            record->configuration_changed = true;
        }
    }

    void operator()(const edits::SetSoftBodyFilter& change) const
    {
        if (SoftBodyRecord* record = find(pending, change.soft_body))
        {
            record->configuration.filter = change.filter;
            record->configuration_changed = true;
        }
    }
};

/// Copies @p from into @p to: whole records whose configuration changed or whose slot holds another object, the state
/// (what @p copy_state copies) of the others.
template<class Record, class CopyState>
void
copy_records(std::vector<Record>& to,
             std::vector<Record>& from,
             CopyState&& copy_state)
{
    if (to.size() < from.size())
    {
        to.resize(from.size());
    }
    for (std::size_t index = 0; index < from.size(); ++index)
    {
        if (from[index].configuration_changed || from[index].handle != to[index].handle)
        {
            from[index].configuration_changed = false;
            to[index] = from[index];
        }
        else
        {
            copy_state(to[index], from[index]);
        }
    }
}

/// The identity of @p backend (which must not be null).
[[nodiscard]] auto
identity_of(const std::unique_ptr<SteppedWorldBackend>& backend) -> BackendIdentity
{
    if (!backend)
    {
        reject(PhysicsSubsystem::WORLD, "SteppedWorld::SteppedWorld", "the backend is null", "give the world a backend (a SteppedWorldBackend)", {});
    }
    return backend->identity();
}

} // namespace

// ===================================================================================================== shared functions

void
reject_destroyed_world(const BackendIdentity& backend,
                       WorldHandle world,
                       std::string_view operation)
{
    reject(backend, operation, "the world was destroyed", "destroy a world's objects before the world (or keep the world while they are used)",
           {.world = world});
}

// ===================================================================================================== WorldShared

auto
WorldShared::is_live(RigidBodyHandle handle) const noexcept -> bool
{
    return live_in(rigid_bodies, handle);
}

auto
WorldShared::is_live(ColliderHandle handle) const noexcept -> bool
{
    return live_in(colliders, handle);
}

auto
WorldShared::is_live(JointHandle handle) const noexcept -> bool
{
    return live_in(joints, handle);
}

auto
WorldShared::is_live(TriggerHandle handle) const noexcept -> bool
{
    return live_in(triggers, handle);
}

auto
WorldShared::is_live(SoftBodyHandle handle) const noexcept -> bool
{
    return live_in(soft_bodies, handle);
}

void
WorldShared::record(WorldEdit edit,
                    std::string_view operation)
{
    const std::scoped_lock lock{mutex};
    if (backend == nullptr)
    {
        reject_destroyed_world(identity, world, operation);
    }

    // Its objects alive, the backend able to apply it.
    std::visit([&](const auto& change) { require_live(*this, change, operation); }, edit);
    backend->check(edit, RecordedWorld{*this});

    // What it changes of the recorded bodies.
    if (const auto* kind = std::get_if<edits::SetBodyKind>(&edit))
    {
        body_recordings[slot_of(kind->body)].kind = kind->kind;
    }
    recorded.push_back(std::move(edit));
}

void
WorldShared::record_destruction(const WorldEdit& edit) noexcept
{
    try
    {
        const std::scoped_lock lock{mutex};
        if (backend == nullptr)
        {
            return;
        }
        if (const auto* body = std::get_if<edits::DestroyRigidBody>(&edit))
        {
            // A body goes with its colliders, triggers and joints (destroyed first).
            if (!is_live(body->body))
            {
                return;
            }
            BodyRecording& recording = body_recordings[slot_of(body->body)];
            for (const ColliderHandle collider : recording.colliders)
            {
                if (is_live(collider))
                {
                    colliders.live[slot_of(collider)] = 0;
                    recorded.push_back(edits::DestroyCollider{.collider = collider});
                }
            }
            for (const TriggerHandle trigger : recording.triggers)
            {
                if (is_live(trigger))
                {
                    triggers.live[slot_of(trigger)] = 0;
                    recorded.push_back(edits::DestroyTrigger{.trigger = trigger});
                }
            }
            for (const JointHandle joint : recording.joints)
            {
                if (is_live(joint))
                {
                    joints.live[slot_of(joint)] = 0;
                    recorded.push_back(edits::DestroyJoint{.joint = joint});
                }
            }
            recording = {};
            rigid_bodies.live[slot_of(body->body)] = 0;
            recorded.push_back(edit);
        }
        else if (const auto* collider = std::get_if<edits::DestroyCollider>(&edit))
        {
            if (!is_live(collider->collider))
            {
                return;
            }
            colliders.live[slot_of(collider->collider)] = 0;
            erase_value(body_recordings[slot_of(collider_bodies[slot_of(collider->collider)])].colliders, collider->collider);
            recorded.push_back(edit);
        }
        else if (const auto* trigger = std::get_if<edits::DestroyTrigger>(&edit))
        {
            if (!is_live(trigger->trigger))
            {
                return;
            }
            triggers.live[slot_of(trigger->trigger)] = 0;
            if (const RigidBodyHandle owner = trigger_bodies[slot_of(trigger->trigger)])
            {
                erase_value(body_recordings[slot_of(owner)].triggers, trigger->trigger);
            }
            recorded.push_back(edit);
        }
        else if (const auto* joint = std::get_if<edits::DestroyJoint>(&edit))
        {
            if (!is_live(joint->joint))
            {
                return;
            }
            joints.live[slot_of(joint->joint)] = 0;
            for (const RigidBodyHandle owner : {joint_bodies[slot_of(joint->joint)].first, joint_bodies[slot_of(joint->joint)].second})
            {
                if (owner && is_live(owner))
                {
                    erase_value(body_recordings[slot_of(owner)].joints, joint->joint);
                }
            }
            recorded.push_back(edit);
        }
        else if (const auto* soft_body = std::get_if<edits::DestroySoftBody>(&edit))
        {
            if (!is_live(soft_body->soft_body))
            {
                return;
            }
            soft_bodies.live[slot_of(soft_body->soft_body)] = 0;
            recorded.push_back(edit);
        }
    }
    catch (...)
    {
        // Out of memory while a destructor records: the object stays in the simulation.
    }
}

auto
WorldShared::create_rigid_body(const RigidBodyDescription& description) -> RigidBodyHandle
{
    constexpr std::string_view OPERATION = "PhysicsWorld::create_rigid_body";
    const std::scoped_lock lock{mutex};
    if (backend == nullptr)
    {
        reject_destroyed_world(identity, world, OPERATION);
    }
    const SlotNumber slot = issue_slot(rigid_bodies, identity, OPERATION);
    const RigidBodyHandle body = RigidBodyHandle::create(key(), slot.index, slot.generation);
    try
    {
        WorldEdit edit = edits::CreateRigidBody{.body = body, .description = description};
        backend->check(edit, RecordedWorld{*this});
        grown(body_recordings, slot.index) = {.kind = description.kind};
        recorded.push_back(std::move(edit));
    }
    catch (...)
    {
        release_slot(rigid_bodies, body);
        throw;
    }
    return body;
}

auto
WorldShared::create_collider(RigidBodyHandle body,
                             const ColliderDescription& description,
                             const MassProperties& unit_mass) -> ColliderHandle
{
    constexpr std::string_view OPERATION = "RigidBody::add_collider";
    const std::scoped_lock lock{mutex};
    if (backend == nullptr)
    {
        reject_destroyed_world(identity, world, OPERATION);
    }
    require_body(*this, OPERATION, body);
    const SlotNumber slot = issue_slot(colliders, identity, OPERATION);
    const ColliderHandle collider = ColliderHandle::create(key(), slot.index, slot.generation);
    try
    {
        WorldEdit edit = edits::CreateCollider{.collider = collider, .body = body, .description = description, .unit_mass = unit_mass};
        backend->check(edit, RecordedWorld{*this});
        grown(collider_bodies, slot.index) = body;
        grown(collider_shapes, slot.index) = description.shape->kind();
        grown(collider_shape_pointers, slot.index) = description.shape;
        body_recordings[slot_of(body)].colliders.push_back(collider);
        recorded.push_back(std::move(edit));
    }
    catch (...)
    {
        release_slot(colliders, collider);
        throw;
    }
    return collider;
}

auto
WorldShared::create_joint(const JointDescription& description) -> JointHandle
{
    constexpr std::string_view OPERATION = "PhysicsWorld::create_joint";
    const std::scoped_lock lock{mutex};
    if (backend == nullptr)
    {
        reject_destroyed_world(identity, world, OPERATION);
    }
    const auto [first, second] = physics::joint_bodies(description);
    require_body(*this, OPERATION, first);
    if (second)
    {
        require_body(*this, OPERATION, second);
    }
    const SlotNumber slot = issue_slot(joints, identity, OPERATION);
    const JointHandle joint = JointHandle::create(key(), slot.index, slot.generation);
    try
    {
        WorldEdit edit = edits::CreateJoint{.joint = joint, .description = description};
        backend->check(edit, RecordedWorld{*this});
        grown(joint_bodies, slot.index) = {first, second};
        body_recordings[slot_of(first)].joints.push_back(joint);
        if (second)
        {
            body_recordings[slot_of(second)].joints.push_back(joint);
        }
        recorded.push_back(std::move(edit));
    }
    catch (...)
    {
        release_slot(joints, joint);
        throw;
    }
    return joint;
}

auto
WorldShared::create_trigger(const TriggerDescription& description) -> TriggerHandle
{
    constexpr std::string_view OPERATION = "PhysicsWorld::create_trigger";
    const std::scoped_lock lock{mutex};
    if (backend == nullptr)
    {
        reject_destroyed_world(identity, world, OPERATION);
    }
    if (description.body)
    {
        require_body(*this, OPERATION, description.body);
    }
    const SlotNumber slot = issue_slot(triggers, identity, OPERATION);
    const TriggerHandle trigger = TriggerHandle::create(key(), slot.index, slot.generation);
    try
    {
        WorldEdit edit = edits::CreateTrigger{.trigger = trigger, .description = description};
        backend->check(edit, RecordedWorld{*this});
        grown(trigger_bodies, slot.index) = description.body;
        if (description.body)
        {
            body_recordings[slot_of(description.body)].triggers.push_back(trigger);
        }
        recorded.push_back(std::move(edit));
    }
    catch (...)
    {
        release_slot(triggers, trigger);
        throw;
    }
    return trigger;
}

auto
WorldShared::create_soft_body(const SoftBodyDescription& description) -> SoftBodyHandle
{
    constexpr std::string_view OPERATION = "PhysicsWorld::create_soft_body";
    const std::scoped_lock lock{mutex};
    if (backend == nullptr)
    {
        reject_destroyed_world(identity, world, OPERATION);
    }
    for (const ParticlePin& pin : description.pins)
    {
        if (pin.body)
        {
            require_body(*this, OPERATION, pin.body);
        }
    }
    const SlotNumber slot = issue_slot(soft_bodies, identity, OPERATION);
    const SoftBodyHandle soft_body = SoftBodyHandle::create(key(), slot.index, slot.generation);
    try
    {
        WorldEdit edit = edits::CreateSoftBody{.soft_body = soft_body, .description = description};
        backend->check(edit, RecordedWorld{*this});
        grown(soft_body_particles, slot.index) = static_cast<std::uint32_t>(description.particles.size());
        recorded.push_back(std::move(edit));
    }
    catch (...)
    {
        release_slot(soft_bodies, soft_body);
        throw;
    }
    return soft_body;
}

void
WorldShared::release(std::span<const RigidBodyHandle> rigid_bodies_released,
                     std::span<const ColliderHandle> colliders_released,
                     std::span<const JointHandle> joints_released,
                     std::span<const TriggerHandle> triggers_released,
                     std::span<const SoftBodyHandle> soft_bodies_released) noexcept
{
    const std::scoped_lock lock{mutex};
    for (const RigidBodyHandle handle : rigid_bodies_released)
    {
        release_slot(rigid_bodies, handle);
    }
    for (const ColliderHandle handle : colliders_released)
    {
        release_slot(colliders, handle);
        if (slot_of(handle) < collider_shape_pointers.size())
        {
            collider_shape_pointers[slot_of(handle)] = nullptr;
        }
    }
    for (const JointHandle handle : joints_released)
    {
        release_slot(joints, handle);
    }
    for (const TriggerHandle handle : triggers_released)
    {
        release_slot(triggers, handle);
    }
    for (const SoftBodyHandle handle : soft_bodies_released)
    {
        release_slot(soft_bodies, handle);
    }
}

// ===================================================================================================== RecordedWorld

auto
RecordedWorld::body_kind(RigidBodyHandle body) const noexcept -> std::optional<BodyKind>
{
    return m_shared->is_live(body) ? std::optional{m_shared->body_recordings[slot_of(body)].kind} : std::nullopt;
}

auto
RecordedWorld::collider_shape_kinds(RigidBodyHandle body) const -> std::vector<ShapeKind>
{
    std::vector<ShapeKind> kinds;
    if (m_shared->is_live(body))
    {
        for (const ColliderHandle collider : m_shared->body_recordings[slot_of(body)].colliders)
        {
            if (m_shared->is_live(collider))
            {
                kinds.push_back(m_shared->collider_shapes[slot_of(collider)]);
            }
        }
    }
    return kinds;
}

auto
RecordedWorld::collider_body(ColliderHandle collider) const noexcept -> std::optional<RigidBodyHandle>
{
    return m_shared->is_live(collider) ? std::optional{m_shared->collider_bodies[slot_of(collider)]} : std::nullopt;
}

auto
RecordedWorld::body_colliders(RigidBodyHandle body) const -> std::vector<ColliderHandle>
{
    std::vector<ColliderHandle> colliders;
    if (m_shared->is_live(body))
    {
        for (const ColliderHandle collider : m_shared->body_recordings[slot_of(body)].colliders)
        {
            if (m_shared->is_live(collider))
            {
                colliders.push_back(collider);
            }
        }
    }
    return colliders;
}

auto
RecordedWorld::collider_shape(ColliderHandle collider) const -> std::shared_ptr<const Shape>
{
    return m_shared->is_live(collider) ? m_shared->collider_shape_pointers[slot_of(collider)] : nullptr;
}

// ===================================================================================================== SteppedWorldBackend defaults

void
SteppedWorldBackend::check(const WorldEdit& /*edit*/,
                           const RecordedWorld& /*recorded*/) const
{
}

auto
SteppedWorldBackend::ray_cast(const Ray& /*ray*/,
                              float /*maximum*/,
                              const QueryOptions& /*options*/) const noexcept -> std::optional<QueryResult<RayHit>>
{
    return std::nullopt;
}

auto
SteppedWorldBackend::ray_cast_all(const Ray& /*ray*/,
                                  float /*maximum*/,
                                  std::span<RayHit> /*output*/,
                                  const QueryOptions& /*options*/) const noexcept -> std::optional<QueryResult<std::span<RayHit>>>
{
    return std::nullopt;
}

auto
SteppedWorldBackend::shape_cast(const Shape& /*shape*/,
                                const Transform& /*pose*/,
                                Vector3 /*translation*/,
                                const QueryOptions& /*options*/) const noexcept -> std::optional<QueryResult<ShapeCastHit>>
{
    return std::nullopt;
}

auto
SteppedWorldBackend::overlap(const Shape& /*shape*/,
                             const Transform& /*pose*/,
                             std::span<ColliderHandle> /*output*/,
                             const QueryOptions& /*options*/) const noexcept -> std::optional<QueryResult<std::span<ColliderHandle>>>
{
    return std::nullopt;
}

auto
SteppedWorldBackend::overlap(const Aabb& /*box*/,
                             std::span<ColliderHandle> /*output*/,
                             const QueryOptions& /*options*/) const noexcept -> std::optional<QueryResult<std::span<ColliderHandle>>>
{
    return std::nullopt;
}

auto
SteppedWorldBackend::closest_point(Vector3 /*point*/,
                                   float /*maximum*/,
                                   const QueryOptions& /*options*/) const noexcept -> std::optional<QueryResult<ClosestPointHit>>
{
    return std::nullopt;
}

void
SteppedWorldBackend::replace_broad_phase(std::unique_ptr<BroadPhase> /*broad_phase*/)
{
    throw_unsupported(identity(), "PhysicsWorld::replace_broad_phase", PhysicsCapability::CUSTOM_BROAD_PHASE, "the backend runs its own broad phase",
                      "use a backend that takes components (PhysicsBackendKind::REFERENCE)");
}

void
SteppedWorldBackend::replace_narrow_phase(std::unique_ptr<NarrowPhase> /*narrow_phase*/)
{
    throw_unsupported(identity(), "PhysicsWorld::replace_narrow_phase", PhysicsCapability::CUSTOM_NARROW_PHASE, "the backend runs its own narrow phase",
                      "use a backend that takes components (PhysicsBackendKind::REFERENCE)");
}

void
SteppedWorldBackend::replace_solver(std::unique_ptr<Solver> /*solver*/)
{
    throw_unsupported(identity(), "PhysicsWorld::replace_solver", PhysicsCapability::CUSTOM_SOLVER, "the backend runs its own solver",
                      "use a backend that takes components (PhysicsBackendKind::REFERENCE)");
}

void
SteppedWorldBackend::replace_integrator(std::unique_ptr<Integrator> /*integrator*/)
{
    throw_unsupported(identity(), "PhysicsWorld::replace_integrator", PhysicsCapability::CUSTOM_INTEGRATOR, "the backend integrates its rigid bodies itself",
                      "use a backend that takes components (PhysicsBackendKind::REFERENCE)");
}

auto
SteppedWorldBackend::simulates_soft_bodies() const noexcept -> bool
{
    return false;
}

void
SteppedWorldBackend::apply_soft_body_corrections(std::span<const BodyCorrection> corrections,
                                                 WorldSnapshot& pending)
{
    // Each dynamic body moved about its center of mass, then its velocities changed by the impulses (its frame's origin
    // moves with the turn about the center); it wakes.
    for (const BodyCorrection& correction : corrections)
    {
        RigidBodyRecord* record = find(pending, correction.body);
        if (record == nullptr || record->configuration.kind != BodyKind::DYNAMIC || !(record->mass.mass > 0.0F))
        {
            continue;
        }
        Transform& pose = record->state.pose;
        const Vector3 center = transform_point(pose, record->mass.center_of_mass) + correction.translation;
        pose.orientation = normalized(correction.rotation * pose.orientation);
        pose.position = center - rotate(pose.orientation, record->mass.center_of_mass);
        const Matrix3 rotation = Matrix3::from_rotation(pose.orientation);
        const Matrix3 inverse_inertia = std::abs(determinant(record->mass.inertia)) > 0.0F
                                            ? rotation * inverse(record->mass.inertia) * transpose(rotation)
                                            : Matrix3{.columns = {Vector3{}, Vector3{}, Vector3{}}};
        const Vector3 turn = inverse_inertia * correction.angular_impulse;
        record->state.linear_velocity += correction.linear_impulse / record->mass.mass + cross(turn, pose.position - center);
        record->state.angular_velocity += turn;
        record->state.awake = true;
    }
}

// ===================================================================================================== SteppedWorld

SteppedWorld::SteppedWorld(const PhysicsWorldDescription& description,
                           std::unique_ptr<SteppedWorldBackend> backend)
    : PhysicsWorld{identity_of(backend)}
    , m_shared{std::make_shared<WorldShared>(issue_world_handle(), identity_of(backend))}
    , m_implementation{std::make_unique<Implementation>()}
{
    check_description(description, this->backend(), "SteppedWorld::SteppedWorld", {.world = m_shared->world});

    // The settings in both snapshots; the backend and its pipeline.
    Implementation& implementation = *m_implementation;
    m_shared->published.configuration = description;
    implementation.pending.configuration = description;
    implementation.scheduler = description.task_scheduler;
    implementation.asynchronous = description.asynchronous_stepping;
    const PipelineKinds kinds = backend->pipeline();
    implementation.broad_phase = std::make_unique<BackendBroadPhase>(this->backend(), kinds.broad_phase);
    implementation.narrow_phase = std::make_unique<BackendNarrowPhase>(this->backend(), kinds.narrow_phase);
    implementation.solver = std::make_unique<BackendSolver>(this->backend(), kinds.solver);
    implementation.integrator = std::make_unique<BackendIntegrator>(this->backend(), kinds.integrator);
    implementation.soft_body_integrator = std::make_unique<XpbdIntegrator>();
    implementation.backend = std::move(backend);
    m_shared->backend = implementation.backend.get();

    // The world's own thread, when it steps asynchronously.
    if (implementation.asynchronous)
    {
        implementation.worker = std::jthread{[&implementation, shared = m_shared.get()](std::stop_token stop) { implementation.work(stop, *shared); }};
    }
}

SteppedWorld::~SteppedWorld()
{
    // A running step ends (its results dropped), the thread stops, then objects throw and the backend goes.
    Implementation& implementation = *m_implementation;
    if (implementation.asynchronous && implementation.stepping.load(std::memory_order_seq_cst))
    {
        std::unique_lock lock{implementation.step_mutex};
        implementation.step_condition.wait(lock, [&implementation] { return implementation.step_finished; });
    }
    implementation.worker.request_stop();
    if (implementation.worker.joinable())
    {
        implementation.worker.join();
    }
    const std::scoped_lock lock{m_shared->mutex};
    m_shared->backend = nullptr;
}

auto
SteppedWorld::world_backend() noexcept -> SteppedWorldBackend&
{
    return *m_implementation->backend;
}

auto
SteppedWorld::handle() const noexcept -> WorldHandle
{
    return m_shared->world;
}

auto
SteppedWorld::description() const -> PhysicsWorldDescription
{
    const std::shared_lock lock{m_shared->published_mutex};
    return m_shared->published.configuration;
}

auto
SteppedWorld::create_rigid_body(const RigidBodyDescription& description) -> std::unique_ptr<RigidBody>
{
    const RigidBodyDescription checked = checked_description(description, backend(), "PhysicsWorld::create_rigid_body", {.world = handle()});
    return make_rigid_body(m_shared, m_shared->create_rigid_body(checked), checked);
}

auto
SteppedWorld::create_soft_body(const SoftBodyDescription& description) -> std::unique_ptr<SoftBody>
{
    check_description(description, backend(), "PhysicsWorld::create_soft_body", {.world = handle()});
    return make_soft_body(m_shared, m_shared->create_soft_body(description), description);
}

auto
SteppedWorld::create_joint(const JointDescription& description) -> std::unique_ptr<Joint>
{
    const JointDescription checked = checked_description(description, backend(), "PhysicsWorld::create_joint", {.world = handle()});
    return make_joint(m_shared, m_shared->create_joint(checked), checked);
}

auto
SteppedWorld::create_trigger(const TriggerDescription& description) -> std::unique_ptr<Trigger>
{
    const TriggerDescription checked = checked_description(description, backend(), "PhysicsWorld::create_trigger", {.world = handle()});
    return make_trigger(m_shared, m_shared->create_trigger(checked), checked);
}

void
SteppedWorld::set_gravity(Vector3 gravity)
{
    check_vector(gravity, "the gravity", backend(), "PhysicsWorld::set_gravity", {.world = handle()});
    m_shared->record(edits::SetGravity{.gravity = gravity}, "PhysicsWorld::set_gravity");
}

void
SteppedWorld::set_solver_description(const SolverDescription& description)
{
    check_description(description, backend(), "PhysicsWorld::set_solver_description", {.world = handle()});
    m_shared->record(edits::SetSolverDescription{.description = description}, "PhysicsWorld::set_solver_description");
}

void
SteppedWorld::set_sleep_description(const SleepDescription& description)
{
    check_description(description, backend(), "PhysicsWorld::set_sleep_description", {.world = handle()});
    m_shared->record(edits::SetSleepDescription{.description = description}, "PhysicsWorld::set_sleep_description");
}

void
SteppedWorld::step(float seconds)
{
    begin_step(seconds);
    wait_step();
}

void
SteppedWorld::begin_step(float seconds)
{
    constexpr std::string_view OPERATION = "PhysicsWorld::begin_step";
    if (!(seconds > 0.0F) || !std::isfinite(seconds))
    {
        reject(backend(), OPERATION, std::format("the step of {} seconds is not finite and positive", seconds), "step by a positive time", {.world = handle()});
    }
    Implementation& implementation = *m_implementation;
    bool idle = false;
    if (!implementation.stepping.compare_exchange_strong(idle, true, std::memory_order_seq_cst))
    {
        reject(backend(), OPERATION, "a step is already running", "call wait_step() before the next begin_step()", {.world = handle()});
    }

    // The queries already running finish first (they incremented the readers before they saw the flag).
    for (std::uint32_t readers = implementation.readers.load(std::memory_order_seq_cst); readers != 0; readers = implementation.readers.load(std::memory_order_seq_cst))
    {
        implementation.readers.wait(readers, std::memory_order_seq_cst);
    }

    // The step: handed to the world's thread, or run now.
    if (implementation.asynchronous)
    {
        {
            const std::scoped_lock lock{implementation.step_mutex};
            implementation.step_seconds = seconds;
            implementation.step_requested = true;
            implementation.step_finished = false;
            implementation.step_error = {};
        }
        implementation.step_condition.notify_all();
        return;
    }
    try
    {
        implementation.run_step(*m_shared, seconds);
    }
    catch (...)
    {
        implementation.step_error = std::current_exception();
    }
    implementation.step_finished = true;
}

void
SteppedWorld::wait_step()
{
    Implementation& implementation = *m_implementation;
    if (!implementation.stepping.load(std::memory_order_seq_cst))
    {
        return;
    }

    // The step's end, and what it threw.
    std::exception_ptr error;
    {
        std::unique_lock lock{implementation.step_mutex};
        implementation.step_condition.wait(lock, [&implementation] { return implementation.step_finished; });
        implementation.step_finished = false;
        error = std::exchange(implementation.step_error, {});
    }
    if (error)
    {
        implementation.stepping.store(false, std::memory_order_seq_cst);
        std::rethrow_exception(error);
    }

    // Published while queries are still refused; then queries and getters see everything the step did.
    implementation.publish(*m_shared);
    implementation.completed.fetch_add(1, std::memory_order_release);
    implementation.stepping.store(false, std::memory_order_seq_cst);
}

auto
SteppedWorld::is_stepping() const noexcept -> bool
{
    return m_implementation->stepping.load(std::memory_order_acquire);
}

auto
SteppedWorld::step_count() const noexcept -> std::uint64_t
{
    return m_implementation->completed.load(std::memory_order_acquire);
}

auto
SteppedWorld::events() const -> PhysicsEvents
{
    const std::shared_lock lock{m_shared->published_mutex};
    const WorldSnapshot& published = m_shared->published;
    return {.contacts = published.contact_events, .triggers = published.trigger_events, .joints = published.joint_events, .sleeps = published.sleep_events};
}

auto
SteppedWorld::statistics() const -> StepStatistics
{
    const std::shared_lock lock{m_shared->published_mutex};
    return m_shared->published.statistics;
}

auto
SteppedWorld::broad_phase() -> BroadPhase&
{
    return *m_implementation->broad_phase;
}

auto
SteppedWorld::narrow_phase() -> NarrowPhase&
{
    return *m_implementation->narrow_phase;
}

auto
SteppedWorld::solver() -> Solver&
{
    return *m_implementation->solver;
}

auto
SteppedWorld::integrator() -> Integrator&
{
    return *m_implementation->integrator;
}

void
SteppedWorld::replace_broad_phase(std::unique_ptr<BroadPhase> broad_phase)
{
    m_implementation->backend->replace_broad_phase(std::move(broad_phase));
}

void
SteppedWorld::replace_narrow_phase(std::unique_ptr<NarrowPhase> narrow_phase)
{
    m_implementation->backend->replace_narrow_phase(std::move(narrow_phase));
}

void
SteppedWorld::replace_solver(std::unique_ptr<Solver> solver)
{
    m_implementation->backend->replace_solver(std::move(solver));
}

void
SteppedWorld::replace_integrator(std::unique_ptr<Integrator> integrator)
{
    m_implementation->backend->replace_integrator(std::move(integrator));
}

auto
SteppedWorld::soft_body_integrator() -> Integrator&
{
    if (m_implementation->backend->simulates_soft_bodies())
    {
        physics::throw_unsupported(backend(), "PhysicsWorld::soft_body_integrator", PhysicsCapability::SOFT_BODIES, "the backend simulates its soft bodies itself",
                                   "leave the backend's soft bodies to it", {.world = handle()});
    }
    return *m_implementation->soft_body_integrator;
}

void
SteppedWorld::replace_soft_body_integrator(std::unique_ptr<Integrator> integrator)
{
    constexpr std::string_view OPERATION = "PhysicsWorld::replace_soft_body_integrator";
    if (m_implementation->backend->simulates_soft_bodies())
    {
        physics::throw_unsupported(backend(), OPERATION, PhysicsCapability::SOFT_BODIES, "the backend simulates its soft bodies itself", "leave the backend's soft bodies to it",
                                   {.world = handle()});
    }
    if (integrator == nullptr)
    {
        reject(backend(), OPERATION, "the integrator is null", "give an integrator (make_integrator())", {.world = handle()});
    }
    if (is_stepping())
    {
        reject(backend(), OPERATION, "a step is in progress", "replace it between steps (after wait_step())", {.world = handle()});
    }
    m_implementation->soft_body_integrator = std::move(integrator);
}

// ===================================================================================================== SteppedWorld::Implementation

void
SteppedWorld::Implementation::work(std::stop_token stop,
                                   WorldShared& shared)
{
    std::unique_lock lock{step_mutex};
    while (step_condition.wait(lock, stop, [this] { return step_requested; }))
    {
        step_requested = false;
        const float seconds = step_seconds;
        lock.unlock();
        std::exception_ptr error;
        try
        {
            run_step(shared, seconds);
        }
        catch (...)
        {
            error = std::current_exception();
        }
        lock.lock();
        step_error = error;
        step_finished = true;
        step_condition.notify_all();
    }
}

void
SteppedWorld::Implementation::run_step(WorldShared& shared,
                                       float seconds)
{
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();

    // A fresh start for what the step reports.
    pending.contact_events.clear();
    pending.trigger_events.clear();
    pending.joint_events.clear();
    pending.sleep_events.clear();
    pending.contacts.clear();
    pending.statistics = {};

    // The edits recorded so far, applied to the records then by the backend.
    {
        const std::scoped_lock lock{shared.mutex};
        std::swap(stepping_edits, shared.recorded);
    }
    apply_to_records(stepping_edits);
    update_masses();
    backend->apply(stepping_edits, pending);
    const bool soft_bodies = steps_soft_bodies();
    if (soft_bodies)
    {
        body_start_poses.resize(pending.rigid_bodies.size());
        std::ranges::transform(pending.rigid_bodies, body_start_poses.begin(), [](const RigidBodyRecord& record) { return record.state.pose; });
    }
    const Clock::time_point applied = Clock::now();

    // The simulation, the soft bodies' (when the framework steps them), then the boxes the new poses make.
    backend->simulate(seconds, pending);
    const Clock::time_point simulated = Clock::now();
    if (soft_bodies)
    {
        simulate_soft_bodies(seconds);
        pending.statistics.phase_seconds[std::to_underlying(StepPhase::SOFT_BODIES)] = std::chrono::duration<double>(Clock::now() - simulated).count();
    }
    const Clock::time_point soft_bodies_simulated = Clock::now();
    update_bounds();

    // The statistics the framework knows.
    StepStatistics& statistics = pending.statistics;
    statistics.step = completed.load(std::memory_order_relaxed) + 1;
    statistics.seconds = seconds;
    statistics.edits = static_cast<std::uint32_t>(stepping_edits.size());
    statistics.phase_seconds[std::to_underlying(StepPhase::EDITS)] = std::chrono::duration<double>(applied - start).count();
    statistics.phase_seconds[std::to_underlying(StepPhase::EVENTS)] += std::chrono::duration<double>(Clock::now() - soft_bodies_simulated).count();
    statistics.rigid_bodies = 0;
    statistics.awake_bodies = 0;
    for (const RigidBodyRecord& record : pending.rigid_bodies)
    {
        if (record.handle)
        {
            ++statistics.rigid_bodies;
            statistics.awake_bodies += record.state.awake && record.configuration.kind != BodyKind::STATIC ? 1U : 0U;
        }
    }
    statistics.joints = static_cast<std::uint32_t>(std::ranges::count_if(pending.joints, [](const JointRecord& record) { return record.handle.is_valid(); }));
    statistics.soft_bodies = static_cast<std::uint32_t>(std::ranges::count_if(pending.soft_bodies, [](const SoftBodyRecord& record) { return record.handle.is_valid(); }));
    stepping_edits.clear();
}

void
SteppedWorld::Implementation::apply_to_records(std::span<const WorldEdit> edits)
{
    const RecordApplier applier{.implementation = *this, .pending = pending};
    for (const WorldEdit& edit : edits)
    {
        std::visit(applier, edit);
    }
}

void
SteppedWorld::Implementation::update_masses()
{
    // Bodies whose mass properties come from their colliders start from nothing; one pass over the colliders adds them.
    bool any = false;
    for (std::size_t slot = 0; slot < mass_changed.size(); ++slot)
    {
        if (mass_changed[slot] == 0 || slot >= pending.rigid_bodies.size() || !pending.rigid_bodies[slot].handle)
        {
            mass_changed[slot] = 0;
            continue;
        }
        RigidBodyRecord& record = pending.rigid_bodies[slot];
        record.configuration_changed = true;
        if (record.configuration.kind != BodyKind::DYNAMIC)
        {
            record.mass = {};
            mass_changed[slot] = 0;
        }
        else if (record.configuration.mass_properties)
        {
            record.mass = *record.configuration.mass_properties;
            mass_changed[slot] = 0;
        }
        else
        {
            record.mass = {};
            any = true;
        }
    }
    if (any)
    {
        for (const ColliderRecord& collider : pending.colliders)
        {
            if (!collider.handle)
            {
                continue;
            }
            const std::uint32_t slot = slot_of(collider.body);
            if (slot < mass_changed.size() && mass_changed[slot] != 0)
            {
                RigidBodyRecord& body = pending.rigid_bodies[slot];
                body.mass = combined(body.mass, transformed(scaled(collider.unit_mass, collider.configuration.density), collider.configuration.pose));
            }
        }
    }
    std::ranges::fill(mass_changed, std::uint8_t{0});
}

auto
SteppedWorld::Implementation::steps_soft_bodies() const noexcept -> bool
{
    return !backend->simulates_soft_bodies() && std::ranges::any_of(pending.soft_bodies, [](const SoftBodyRecord& record) { return record.handle.is_valid(); });
}

void
SteppedWorld::Implementation::simulate_soft_bodies(float seconds)
{
    // The bodies by slot, as the backend's step moved them.
    coupled_bodies.resize(pending.rigid_bodies.size());
    for (std::size_t slot = 0; slot < pending.rigid_bodies.size(); ++slot)
    {
        const RigidBodyRecord& record = pending.rigid_bodies[slot];
        coupled_bodies[slot] = record.handle ? CoupledBody{.handle = record.handle,
                                                           .kind = record.configuration.kind,
                                                           .start_pose = slot < body_start_poses.size() ? body_start_poses[slot] : record.state.pose,
                                                           .end_pose = record.state.pose,
                                                           .mass = record.mass,
                                                           .locks = record.configuration.locks}
                                             : CoupledBody{};
    }

    // The colliders on them.
    coupled_colliders.clear();
    for (const ColliderRecord& record : pending.colliders)
    {
        if (record.handle)
        {
            coupled_colliders.push_back({.handle = record.handle,
                                         .body = slot_of(record.body),
                                         .shape = record.configuration.shape.get(),
                                         .pose = record.configuration.pose,
                                         .friction = record.configuration.material.friction,
                                         .filter = record.configuration.filter});
        }
    }

    // The soft bodies, their models made when first stepped; the step; what it did to the bodies, to the backend.
    if (soft_body_models.size() < pending.soft_bodies.size())
    {
        soft_body_models.resize(pending.soft_bodies.size());
    }
    simulated_soft_bodies.clear();
    for (std::size_t slot = 0; slot < pending.soft_bodies.size(); ++slot)
    {
        SoftBodyRecord& record = pending.soft_bodies[slot];
        if (!record.handle)
        {
            continue;
        }
        if (soft_body_models[slot] == nullptr)
        {
            soft_body_models[slot] = std::make_unique<SoftBodyModel>(record.configuration);
        }
        simulated_soft_bodies.push_back({.model = soft_body_models[slot].get(), .settings = &record.configuration, .positions = record.positions, .velocities = record.velocities});
    }
    soft_body_solver.step(seconds, pending.configuration.gravity, simulated_soft_bodies, coupled_bodies, coupled_colliders, *soft_body_integrator, scheduler);
    if (!soft_body_solver.corrections().empty())
    {
        backend->apply_soft_body_corrections(soft_body_solver.corrections(), pending);
    }
}

void
SteppedWorld::Implementation::forget_soft_body_model(SoftBodyHandle soft_body) noexcept
{
    const std::uint32_t slot = slot_of(soft_body);
    if (slot < soft_body_models.size())
    {
        soft_body_models[slot].reset();
    }
}

void
SteppedWorld::Implementation::update_bounds()
{
    for (ColliderRecord& collider : pending.colliders)
    {
        if (collider.handle)
        {
            collider.world_bounds = collider.configuration.shape->bounds(world_pose(pending, collider));
        }
    }
    for (TriggerRecord& trigger : pending.triggers)
    {
        if (trigger.handle)
        {
            const RigidBodyRecord* body = trigger.configuration.body ? find(pending, trigger.configuration.body) : nullptr;
            trigger.world_bounds = trigger.configuration.shape->bounds(body != nullptr ? body->state.pose * trigger.configuration.pose : trigger.configuration.pose);
        }
    }
    for (SoftBodyRecord& soft_body : pending.soft_bodies)
    {
        if (soft_body.handle)
        {
            soft_body.bounds = Aabb::empty();
            for (const Vector3 position : soft_body.positions)
            {
                soft_body.bounds = soft_body.bounds.merged(position);
            }
            soft_body.bounds = soft_body.bounds.expanded(soft_body.configuration.particle_radius);
        }
    }
}

void
SteppedWorld::Implementation::publish(WorldShared& shared)
{
    // The pending snapshot into the published one, under the getters' lock.
    {
        const std::unique_lock lock{shared.published_mutex};
        WorldSnapshot& published = shared.published;
        if (pending.configuration_changed)
        {
            pending.configuration_changed = false;
            published.configuration = pending.configuration;
        }
        copy_records(published.rigid_bodies, pending.rigid_bodies, [](RigidBodyRecord& to, const RigidBodyRecord& from) { to.state = from.state; });
        copy_records(published.colliders, pending.colliders, [](ColliderRecord& to, const ColliderRecord& from) { to.world_bounds = from.world_bounds; });
        copy_records(published.joints, pending.joints, [](JointRecord& to, const JointRecord& from) { to.state = from.state; });
        copy_records(published.triggers, pending.triggers, [](TriggerRecord& to, const TriggerRecord& from) { to.world_bounds = from.world_bounds; });
        copy_records(published.soft_bodies, pending.soft_bodies, [](SoftBodyRecord& to, const SoftBodyRecord& from) {
            to.positions = from.positions;
            to.velocities = from.velocities;
            to.bounds = from.bounds;
        });
        published.contact_events = pending.contact_events;
        published.trigger_events = pending.trigger_events;
        published.joint_events = pending.joint_events;
        published.sleep_events = pending.sleep_events;
        published.contacts = pending.contacts;
        published.statistics = pending.statistics;
    }

    // The slots of the objects destroyed in this step become free; the query tree follows the published colliders.
    shared.release(released_bodies, released_colliders, released_joints, released_triggers, released_soft_bodies);
    released_bodies.clear();
    released_colliders.clear();
    released_joints.clear();
    released_triggers.clear();
    released_soft_bodies.clear();
    update_query_tree(shared.published);
}

void
SteppedWorld::Implementation::update_query_tree(const WorldSnapshot& published)
{
    if (collider_proxies.size() < published.colliders.size())
    {
        collider_proxies.resize(published.colliders.size());
        proxy_colliders.resize(published.colliders.size());
    }
    for (std::size_t slot = 0; slot < published.colliders.size(); ++slot)
    {
        const ColliderRecord& collider = published.colliders[slot];
        SpatialProxyHandle& proxy = collider_proxies[slot];

        // A proxy of a collider gone (or of the slot's previous collider) goes.
        if (proxy_colliders[slot] != collider.handle && proxy)
        {
            query_tree.remove(proxy);
            proxy = {};
            proxy_colliders[slot] = {};
        }
        if (!collider.handle)
        {
            continue;
        }
        if (!proxy)
        {
            proxy = query_tree.insert(collider.handle, collider.world_bounds);
            proxy_colliders[slot] = collider.handle;
        }
        else if (query_tree.box(proxy) != collider.world_bounds)
        {
            query_tree.update(proxy, collider.world_bounds);
        }
    }
}

// ===================================================================================================== QueryScope

QueryScope::QueryScope(const SteppedWorld::Implementation& implementation) noexcept
    : m_implementation{&implementation}
    , m_active{false}
{
    // Readers first, then the flag (a step sets the flag, then reads the readers: one of the two sees the other).
    implementation.readers.fetch_add(1, std::memory_order_seq_cst);
    m_active = !implementation.stepping.load(std::memory_order_seq_cst);
}

QueryScope::~QueryScope()
{
    const std::uint32_t previous = m_implementation->readers.fetch_sub(1, std::memory_order_seq_cst);
    if (previous == 1 && m_implementation->stepping.load(std::memory_order_seq_cst))
    {
        m_implementation->readers.notify_all();
    }
}

} // namespace gfx::physics
