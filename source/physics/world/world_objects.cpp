// The objects of a SteppedWorld the application holds: rigid bodies, colliders, joints, triggers and soft bodies. Each
// keeps its handle, what it was created with and a weak link to its world's shared state: setters check their values and
// record edits, getters read the published snapshot (or what the object was created with, before its first step), and
// destructors record the object's destruction.
module gfx.physics.world;

import std;
import gfx.core.errors;
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

/// What an object knows of its world.
class WorldLink
{
public:
    WorldLink(const std::shared_ptr<WorldShared>& shared) noexcept
        : m_shared{shared}
        , m_world{shared->world}
        , m_backend{shared->identity}
    {
    }

    /// The world's shared state.
    /// @throws PhysicsError if the world was destroyed.
    [[nodiscard]] auto lock(std::string_view operation) const -> std::shared_ptr<WorldShared>
    {
        std::shared_ptr<WorldShared> shared = m_shared.lock();
        if (!shared)
        {
            reject_destroyed_world(m_backend, m_world, operation);
        }
        return shared;
    }

    /// Records @p edit.
    void record(WorldEdit edit,
                std::string_view operation) const
    {
        lock(operation)->record(std::move(edit), operation);
    }

    /// Records the destruction @p edit, if the world is still there.
    void destroy(const WorldEdit& edit) const noexcept
    {
        if (const std::shared_ptr<WorldShared> shared = m_shared.lock())
        {
            shared->record_destruction(edit);
        }
    }

    [[nodiscard]] auto world() const noexcept -> WorldHandle
    {
        return m_world;
    }

private:
    std::weak_ptr<WorldShared> m_shared;
    WorldHandle m_world{};
    BackendIdentity m_backend;
};

/// Throws unless @p handle is alive: an object whose body was destroyed (with it) fails here.
template<class Handle>
void
require_alive(const WorldShared& shared,
              Handle handle,
              RigidBodyHandle body,
              std::string_view operation,
              const PhysicsErrorContext& context)
{
    const std::scoped_lock lock{shared.mutex};
    if (shared.backend == nullptr)
    {
        reject_destroyed_world(shared.identity, shared.world, operation);
    }
    if (!shared.is_live(handle))
    {
        reject(shared.identity, operation,
               body && !shared.is_live(body) ? std::format("{} was destroyed with its body {}", handle, body) : std::format("{} was destroyed", handle),
               "keep a body alive as long as what is attached to it is used", context);
    }
}

// ===================================================================================================== colliders

class SteppedCollider final : public Collider
{
public:
    SteppedCollider(const std::shared_ptr<WorldShared>& shared,
                    ColliderHandle handle,
                    RigidBodyHandle body,
                    ColliderDescription initial)
        : Collider{shared->identity}
        , m_link{shared}
        , m_handle{handle}
        , m_body{body}
        , m_initial{std::move(initial)}
    {
    }

    ~SteppedCollider() override
    {
        m_link.destroy(edits::DestroyCollider{.collider = m_handle});
    }

    [[nodiscard]] auto handle() const noexcept -> ColliderHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto body() const noexcept -> RigidBodyHandle override
    {
        return m_body;
    }

    [[nodiscard]] auto shape() const noexcept -> const std::shared_ptr<const Shape>& override
    {
        return m_initial.shape;
    }

    [[nodiscard]] auto description() const -> ColliderDescription override
    {
        const auto shared = alive("Collider::description");
        const std::shared_lock lock{shared->published_mutex};
        const ColliderRecord* record = find(shared->published, m_handle);
        return record != nullptr ? record->configuration : m_initial;
    }

    [[nodiscard]] auto world_bounds() const -> Aabb override
    {
        const auto shared = alive("Collider::world_bounds");
        const std::shared_lock lock{shared->published_mutex};
        if (const ColliderRecord* record = find(shared->published, m_handle))
        {
            return record->world_bounds;
        }

        // Not stepped yet: at its body's pose of the last step (or the body's creation), else in the body's frame.
        const RigidBodyRecord* body = find(shared->published, m_body);
        return m_initial.shape->bounds(body != nullptr ? body->state.pose * m_initial.pose : m_initial.pose);
    }

    void set_pose(const Transform& pose) override
    {
        constexpr std::string_view OPERATION = "Collider::set_pose";
        m_link.record(edits::SetColliderPose{.collider = m_handle, .pose = checked_pose(pose, "the collider's pose", backend(), OPERATION, context())}, OPERATION);
    }

    void set_material(const SurfaceMaterial& material) override
    {
        constexpr std::string_view OPERATION = "Collider::set_material";
        check_material(material, backend(), OPERATION, context());
        m_link.record(edits::SetColliderMaterial{.collider = m_handle, .material = material}, OPERATION);
    }

    void set_density(float density) override
    {
        constexpr std::string_view OPERATION = "Collider::set_density";
        check_non_negative(density, "the density", backend(), OPERATION, context());
        m_link.record(edits::SetColliderDensity{.collider = m_handle, .density = density}, OPERATION);
    }

    void set_filter(const CollisionFilter& filter) override
    {
        m_link.record(edits::SetColliderFilter{.collider = m_handle, .filter = filter}, "Collider::set_filter");
    }

    void set_contact_events(bool enabled) override
    {
        m_link.record(edits::SetColliderContactEvents{.collider = m_handle, .enabled = enabled}, "Collider::set_contact_events");
    }

private:
    [[nodiscard]] auto context() const noexcept -> PhysicsErrorContext
    {
        return {.world = m_link.world(), .rigid_body = m_body, .collider = m_handle};
    }

    [[nodiscard]] auto alive(std::string_view operation) const -> std::shared_ptr<WorldShared>
    {
        std::shared_ptr<WorldShared> shared = m_link.lock(operation);
        require_alive(*shared, m_handle, m_body, operation, context());
        return shared;
    }

private:
    WorldLink m_link;
    ColliderHandle m_handle{};
    RigidBodyHandle m_body{};
    ColliderDescription m_initial{};
};

// ===================================================================================================== rigid bodies

class SteppedRigidBody final : public RigidBody
{
public:
    SteppedRigidBody(const std::shared_ptr<WorldShared>& shared,
                     RigidBodyHandle handle,
                     RigidBodyDescription initial)
        : RigidBody{shared->identity}
        , m_link{shared}
        , m_handle{handle}
        , m_initial{std::move(initial)}
    {
    }

    ~SteppedRigidBody() override
    {
        m_link.destroy(edits::DestroyRigidBody{.body = m_handle});
    }

    [[nodiscard]] auto handle() const noexcept -> RigidBodyHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto description() const -> RigidBodyDescription override
    {
        const auto shared = m_link.lock("RigidBody::description");
        const std::shared_lock lock{shared->published_mutex};
        const RigidBodyRecord* record = find(shared->published, m_handle);
        if (record == nullptr)
        {
            return m_initial;
        }
        RigidBodyDescription description = record->configuration;
        description.pose = record->state.pose;
        description.linear_velocity = record->state.linear_velocity;
        description.angular_velocity = record->state.angular_velocity;
        description.awake = record->state.awake;
        return description;
    }

    [[nodiscard]] auto state() const -> RigidBodyState override
    {
        const auto shared = m_link.lock("RigidBody::state");
        const std::shared_lock lock{shared->published_mutex};
        if (const RigidBodyRecord* record = find(shared->published, m_handle))
        {
            return record->state;
        }
        return {.pose = m_initial.pose, .linear_velocity = m_initial.linear_velocity, .angular_velocity = m_initial.angular_velocity, .awake = m_initial.awake};
    }

    [[nodiscard]] auto mass_properties() const -> MassProperties override
    {
        const auto shared = m_link.lock("RigidBody::mass_properties");
        const std::shared_lock lock{shared->published_mutex};
        if (const RigidBodyRecord* record = find(shared->published, m_handle))
        {
            return record->mass;
        }
        return m_initial.kind == BodyKind::DYNAMIC && m_initial.mass_properties ? *m_initial.mass_properties : MassProperties{};
    }

    [[nodiscard]] auto add_collider(const ColliderDescription& description) -> std::unique_ptr<Collider> override
    {
        constexpr std::string_view OPERATION = "RigidBody::add_collider";
        const auto shared = m_link.lock(OPERATION);
        ColliderDescription checked = checked_description(description, backend(), OPERATION, context());
        const MassProperties unit_mass = checked.shape->mass_properties(1.0F);
        const ColliderHandle collider = shared->create_collider(m_handle, checked, unit_mass);
        return std::make_unique<SteppedCollider>(shared, collider, m_handle, std::move(checked));
    }

    void set_pose(const Transform& pose) override
    {
        constexpr std::string_view OPERATION = "RigidBody::set_pose";
        m_link.record(edits::SetBodyPose{.body = m_handle, .pose = checked_pose(pose, "the body's pose", backend(), OPERATION, context())}, OPERATION);
    }

    void set_linear_velocity(Vector3 velocity) override
    {
        constexpr std::string_view OPERATION = "RigidBody::set_linear_velocity";
        check_vector(velocity, "the linear velocity", backend(), OPERATION, context());
        m_link.record(edits::SetBodyLinearVelocity{.body = m_handle, .velocity = velocity}, OPERATION);
    }

    void set_angular_velocity(Vector3 velocity) override
    {
        constexpr std::string_view OPERATION = "RigidBody::set_angular_velocity";
        check_vector(velocity, "the angular velocity", backend(), OPERATION, context());
        m_link.record(edits::SetBodyAngularVelocity{.body = m_handle, .velocity = velocity}, OPERATION);
    }

    void move_kinematic(const Transform& target,
                        float seconds) override
    {
        constexpr std::string_view OPERATION = "RigidBody::move_kinematic";
        const Transform checked = checked_pose(target, "the target", backend(), OPERATION, context());
        if (!(seconds > 0.0F) || !std::isfinite(seconds))
        {
            reject(backend(), OPERATION, std::format("the time {} is not finite and positive", seconds), "give the time to reach the target (usually the step's)",
                   context());
        }

        // Only kinematic bodies (as recorded) move this way.
        const auto shared = m_link.lock(OPERATION);
        BodyKind kind = BodyKind::DYNAMIC;
        {
            const std::scoped_lock lock{shared->mutex};
            kind = RecordedWorld{*shared}.body_kind(m_handle).value_or(BodyKind::DYNAMIC);
        }
        if (kind != BodyKind::KINEMATIC)
        {
            reject(backend(), OPERATION, std::format("the body is {}, not KINEMATIC", kind), "make it kinematic first (set_kind), or set its pose or velocities", context());
        }
        shared->record(edits::MoveKinematicBody{.body = m_handle, .target = checked, .seconds = seconds}, OPERATION);
    }

    void apply_force(Vector3 force) override
    {
        constexpr std::string_view OPERATION = "RigidBody::apply_force";
        check_vector(force, "the force", backend(), OPERATION, context());
        m_link.record(edits::ApplyForce{.body = m_handle, .force = force, .point = std::nullopt}, OPERATION);
    }

    void apply_force_at(Vector3 force,
                        Vector3 point) override
    {
        constexpr std::string_view OPERATION = "RigidBody::apply_force_at";
        check_vector(force, "the force", backend(), OPERATION, context());
        check_vector(point, "the point", backend(), OPERATION, context());
        m_link.record(edits::ApplyForce{.body = m_handle, .force = force, .point = point}, OPERATION);
    }

    void apply_torque(Vector3 torque) override
    {
        constexpr std::string_view OPERATION = "RigidBody::apply_torque";
        check_vector(torque, "the torque", backend(), OPERATION, context());
        m_link.record(edits::ApplyTorque{.body = m_handle, .torque = torque}, OPERATION);
    }

    void apply_linear_impulse(Vector3 impulse) override
    {
        constexpr std::string_view OPERATION = "RigidBody::apply_linear_impulse";
        check_vector(impulse, "the impulse", backend(), OPERATION, context());
        m_link.record(edits::ApplyLinearImpulse{.body = m_handle, .impulse = impulse, .point = std::nullopt}, OPERATION);
    }

    void apply_linear_impulse_at(Vector3 impulse,
                                 Vector3 point) override
    {
        constexpr std::string_view OPERATION = "RigidBody::apply_linear_impulse_at";
        check_vector(impulse, "the impulse", backend(), OPERATION, context());
        check_vector(point, "the point", backend(), OPERATION, context());
        m_link.record(edits::ApplyLinearImpulse{.body = m_handle, .impulse = impulse, .point = point}, OPERATION);
    }

    void apply_angular_impulse(Vector3 impulse) override
    {
        constexpr std::string_view OPERATION = "RigidBody::apply_angular_impulse";
        check_vector(impulse, "the angular impulse", backend(), OPERATION, context());
        m_link.record(edits::ApplyAngularImpulse{.body = m_handle, .impulse = impulse}, OPERATION);
    }

    void set_kind(BodyKind kind) override
    {
        m_link.record(edits::SetBodyKind{.body = m_handle, .kind = kind}, "RigidBody::set_kind");
    }

    void set_mass_properties(const std::optional<MassProperties>& properties) override
    {
        constexpr std::string_view OPERATION = "RigidBody::set_mass_properties";
        if (properties)
        {
            check_mass_properties(*properties, backend(), OPERATION, context());
        }
        m_link.record(edits::SetBodyMassProperties{.body = m_handle, .properties = properties}, OPERATION);
    }

    void set_damping(float linear,
                     float angular) override
    {
        constexpr std::string_view OPERATION = "RigidBody::set_damping";
        check_non_negative(linear, "the linear damping", backend(), OPERATION, context());
        check_non_negative(angular, "the angular damping", backend(), OPERATION, context());
        m_link.record(edits::SetBodyDamping{.body = m_handle, .linear = linear, .angular = angular}, OPERATION);
    }

    void set_gravity_scale(float scale) override
    {
        constexpr std::string_view OPERATION = "RigidBody::set_gravity_scale";
        check_finite(scale, "the gravity scale", backend(), OPERATION, context());
        m_link.record(edits::SetBodyGravityScale{.body = m_handle, .scale = scale}, OPERATION);
    }

    void set_motion_quality(MotionQuality quality) override
    {
        m_link.record(edits::SetBodyMotionQuality{.body = m_handle, .quality = quality}, "RigidBody::set_motion_quality");
    }

    void set_motion_locks(MotionLock locks) override
    {
        m_link.record(edits::SetBodyMotionLocks{.body = m_handle, .locks = locks}, "RigidBody::set_motion_locks");
    }

    void set_sleep_allowed(bool allowed) override
    {
        m_link.record(edits::SetBodySleepAllowed{.body = m_handle, .allowed = allowed}, "RigidBody::set_sleep_allowed");
    }

    void wake_up() override
    {
        m_link.record(edits::SetBodyAwake{.body = m_handle, .awake = true}, "RigidBody::wake_up");
    }

    void put_to_sleep() override
    {
        m_link.record(edits::SetBodyAwake{.body = m_handle, .awake = false}, "RigidBody::put_to_sleep");
    }

private:
    [[nodiscard]] auto context() const noexcept -> PhysicsErrorContext
    {
        return {.world = m_link.world(), .rigid_body = m_handle};
    }

private:
    WorldLink m_link;
    RigidBodyHandle m_handle{};
    RigidBodyDescription m_initial{};
};

// ===================================================================================================== joints

class SteppedJoint final : public Joint
{
public:
    SteppedJoint(const std::shared_ptr<WorldShared>& shared,
                 JointHandle handle,
                 JointDescription initial)
        : Joint{shared->identity}
        , m_link{shared}
        , m_handle{handle}
        , m_bodies{joint_bodies(initial)}
        , m_initial{std::move(initial)}
    {
    }

    ~SteppedJoint() override
    {
        m_link.destroy(edits::DestroyJoint{.joint = m_handle});
    }

    [[nodiscard]] auto handle() const noexcept -> JointHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto kind() const noexcept -> JointKind override
    {
        return joint_kind(m_initial);
    }

    [[nodiscard]] auto bodies() const noexcept -> std::pair<RigidBodyHandle, RigidBodyHandle> override
    {
        return m_bodies;
    }

    [[nodiscard]] auto description() const -> JointDescription override
    {
        const auto shared = alive("Joint::description");
        const std::shared_lock lock{shared->published_mutex};
        const JointRecord* record = find(shared->published, m_handle);
        return record != nullptr ? record->configuration : m_initial;
    }

    [[nodiscard]] auto state() const -> JointState override
    {
        const auto shared = alive("Joint::state");
        const std::shared_lock lock{shared->published_mutex};
        const JointRecord* record = find(shared->published, m_handle);
        return record != nullptr ? record->state : JointState{};
    }

    void update(const JointDescription& description) override
    {
        constexpr std::string_view OPERATION = "Joint::update";
        if (joint_kind(description) != kind())
        {
            reject(backend(), OPERATION, std::format("the description is of a {} joint, this joint is {}", joint_kind(description), kind()),
                   "describe a joint of the same kind (destroy this one and create another to change the kind)", context());
        }
        if (joint_bodies(description) != m_bodies)
        {
            reject(backend(), OPERATION, "the description joins other bodies", "keep the bodies (create another joint to join others)", context());
        }
        m_link.record(edits::UpdateJoint{.joint = m_handle, .description = checked_description(description, backend(), OPERATION, context())}, OPERATION);
    }

    void set_enabled(bool enabled) override
    {
        m_link.record(edits::SetJointEnabled{.joint = m_handle, .enabled = enabled}, "Joint::set_enabled");
    }

private:
    [[nodiscard]] auto context() const noexcept -> PhysicsErrorContext
    {
        return {.world = m_link.world(), .rigid_body = m_bodies.first, .joint = m_handle};
    }

    [[nodiscard]] auto alive(std::string_view operation) const -> std::shared_ptr<WorldShared>
    {
        std::shared_ptr<WorldShared> shared = m_link.lock(operation);
        const RigidBodyHandle gone = [&] {
            const std::scoped_lock lock{shared->mutex};
            return !shared->is_live(m_bodies.first) ? m_bodies.first : m_bodies.second && !shared->is_live(m_bodies.second) ? m_bodies.second : RigidBodyHandle{};
        }();
        require_alive(*shared, m_handle, gone, operation, context());
        return shared;
    }

private:
    WorldLink m_link;
    JointHandle m_handle{};
    std::pair<RigidBodyHandle, RigidBodyHandle> m_bodies{};
    JointDescription m_initial{};
};

// ===================================================================================================== triggers

class SteppedTrigger final : public Trigger
{
public:
    SteppedTrigger(const std::shared_ptr<WorldShared>& shared,
                   TriggerHandle handle,
                   TriggerDescription initial)
        : Trigger{shared->identity}
        , m_link{shared}
        , m_handle{handle}
        , m_initial{std::move(initial)}
    {
    }

    ~SteppedTrigger() override
    {
        m_link.destroy(edits::DestroyTrigger{.trigger = m_handle});
    }

    [[nodiscard]] auto handle() const noexcept -> TriggerHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto shape() const noexcept -> const std::shared_ptr<const Shape>& override
    {
        return m_initial.shape;
    }

    [[nodiscard]] auto description() const -> TriggerDescription override
    {
        const auto shared = alive("Trigger::description");
        const std::shared_lock lock{shared->published_mutex};
        const TriggerRecord* record = find(shared->published, m_handle);
        return record != nullptr ? record->configuration : m_initial;
    }

    [[nodiscard]] auto world_bounds() const -> Aabb override
    {
        const auto shared = alive("Trigger::world_bounds");
        const std::shared_lock lock{shared->published_mutex};
        if (const TriggerRecord* record = find(shared->published, m_handle))
        {
            return record->world_bounds;
        }
        const RigidBodyRecord* body = m_initial.body ? find(shared->published, m_initial.body) : nullptr;
        return m_initial.shape->bounds(body != nullptr ? body->state.pose * m_initial.pose : m_initial.pose);
    }

    void set_pose(const Transform& pose) override
    {
        constexpr std::string_view OPERATION = "Trigger::set_pose";
        m_link.record(edits::SetTriggerPose{.trigger = m_handle, .pose = checked_pose(pose, "the trigger's pose", backend(), OPERATION, context())}, OPERATION);
    }

    void set_filter(const CollisionFilter& filter) override
    {
        m_link.record(edits::SetTriggerFilter{.trigger = m_handle, .filter = filter}, "Trigger::set_filter");
    }

private:
    [[nodiscard]] auto context() const noexcept -> PhysicsErrorContext
    {
        return {.world = m_link.world(), .rigid_body = m_initial.body ? std::optional{m_initial.body} : std::nullopt, .trigger = m_handle};
    }

    [[nodiscard]] auto alive(std::string_view operation) const -> std::shared_ptr<WorldShared>
    {
        std::shared_ptr<WorldShared> shared = m_link.lock(operation);
        require_alive(*shared, m_handle, m_initial.body, operation, context());
        return shared;
    }

private:
    WorldLink m_link;
    TriggerHandle m_handle{};
    TriggerDescription m_initial{};
};

// ===================================================================================================== soft bodies

class SteppedSoftBody final : public SoftBody
{
public:
    SteppedSoftBody(const std::shared_ptr<WorldShared>& shared,
                    SoftBodyHandle handle,
                    SoftBodyDescription initial)
        : SoftBody{shared->identity}
        , m_link{shared}
        , m_handle{handle}
        , m_initial{std::move(initial)}
    {
    }

    ~SteppedSoftBody() override
    {
        m_link.destroy(edits::DestroySoftBody{.soft_body = m_handle});
    }

    [[nodiscard]] auto handle() const noexcept -> SoftBodyHandle override
    {
        return m_handle;
    }

    [[nodiscard]] auto particle_count() const noexcept -> std::uint32_t override
    {
        return static_cast<std::uint32_t>(m_initial.particles.size());
    }

    [[nodiscard]] auto description() const -> SoftBodyDescription override
    {
        const auto shared = m_link.lock("SoftBody::description");
        const std::shared_lock lock{shared->published_mutex};
        const SoftBodyRecord* record = find(shared->published, m_handle);
        return record != nullptr ? record->configuration : m_initial;
    }

    auto positions(std::span<Vector3> output) const -> std::span<Vector3> override
    {
        constexpr std::string_view OPERATION = "SoftBody::positions";
        const auto shared = checked_output(output, OPERATION);
        const std::shared_lock lock{shared->published_mutex};
        if (const SoftBodyRecord* record = find(shared->published, m_handle))
        {
            std::ranges::copy(record->positions, output.begin());
        }
        else
        {
            std::ranges::transform(m_initial.particles, output.begin(), &SoftBodyParticle::position);
        }
        return output.first(particle_count());
    }

    auto velocities(std::span<Vector3> output) const -> std::span<Vector3> override
    {
        constexpr std::string_view OPERATION = "SoftBody::velocities";
        const auto shared = checked_output(output, OPERATION);
        const std::shared_lock lock{shared->published_mutex};
        if (const SoftBodyRecord* record = find(shared->published, m_handle))
        {
            std::ranges::copy(record->velocities, output.begin());
        }
        else
        {
            std::ranges::fill(output.first(particle_count()), Vector3{});
        }
        return output.first(particle_count());
    }

    [[nodiscard]] auto bounds() const -> Aabb override
    {
        const auto shared = m_link.lock("SoftBody::bounds");
        const std::shared_lock lock{shared->published_mutex};
        if (const SoftBodyRecord* record = find(shared->published, m_handle))
        {
            return record->bounds;
        }
        Aabb box = Aabb::empty();
        for (const SoftBodyParticle& particle : m_initial.particles)
        {
            box = box.merged(particle.position);
        }
        return box.expanded(m_initial.particle_radius);
    }

    void pin(const ParticlePin& pin) override
    {
        constexpr std::string_view OPERATION = "SoftBody::pin";
        check_pin(pin, particle_count(), backend(), OPERATION, context());
        m_link.record(edits::PinParticle{.soft_body = m_handle, .pin = pin}, OPERATION);
    }

    void unpin(std::uint32_t particle) override
    {
        constexpr std::string_view OPERATION = "SoftBody::unpin";
        check_particle(particle, OPERATION);
        m_link.record(edits::UnpinParticle{.soft_body = m_handle, .particle = particle}, OPERATION);
    }

    void move_particle(std::uint32_t particle,
                       Vector3 position) override
    {
        constexpr std::string_view OPERATION = "SoftBody::move_particle";
        check_particle(particle, OPERATION);
        check_vector(position, "the particle's position", backend(), OPERATION, context());
        m_link.record(edits::MoveParticle{.soft_body = m_handle, .particle = particle, .position = position}, OPERATION);
    }

    void set_damping(float damping) override
    {
        constexpr std::string_view OPERATION = "SoftBody::set_damping";
        if (!(damping >= 0.0F && damping <= 1.0F))
        {
            reject(backend(), OPERATION, std::format("the damping {} is outside [0, 1]", damping), "give a damping in [0, 1]", context());
        }
        m_link.record(edits::SetSoftBodyDamping{.soft_body = m_handle, .damping = damping}, OPERATION);
    }

    void set_gravity_scale(float scale) override
    {
        constexpr std::string_view OPERATION = "SoftBody::set_gravity_scale";
        check_finite(scale, "the gravity scale", backend(), OPERATION, context());
        m_link.record(edits::SetSoftBodyGravityScale{.soft_body = m_handle, .scale = scale}, OPERATION);
    }

    void set_self_collision(bool enabled) override
    {
        m_link.record(edits::SetSoftBodySelfCollision{.soft_body = m_handle, .enabled = enabled}, "SoftBody::set_self_collision");
    }

    void set_filter(const CollisionFilter& filter) override
    {
        m_link.record(edits::SetSoftBodyFilter{.soft_body = m_handle, .filter = filter}, "SoftBody::set_filter");
    }

private:
    [[nodiscard]] auto context() const noexcept -> PhysicsErrorContext
    {
        return {.world = m_link.world(), .soft_body = m_handle};
    }

    void check_particle(std::uint32_t particle,
                        std::string_view operation) const
    {
        if (particle >= particle_count())
        {
            reject(backend(), operation, std::format("particle {} of {}", particle, particle_count()), "name a particle of the soft body", context());
        }
    }

    [[nodiscard]] auto checked_output(std::span<Vector3> output,
                                      std::string_view operation) const -> std::shared_ptr<WorldShared>
    {
        if (output.size() < particle_count())
        {
            reject(backend(), operation, std::format("the output holds {} values for {} particles", output.size(), particle_count()),
                   "give at least particle_count() elements", context());
        }
        return m_link.lock(operation);
    }

private:
    WorldLink m_link;
    SoftBodyHandle m_handle{};
    SoftBodyDescription m_initial{};
};

} // namespace

// ===================================================================================================== factories

auto
make_rigid_body(const std::shared_ptr<WorldShared>& shared,
                RigidBodyHandle handle,
                const RigidBodyDescription& initial) -> std::unique_ptr<RigidBody>
{
    return std::make_unique<SteppedRigidBody>(shared, handle, initial);
}

auto
make_joint(const std::shared_ptr<WorldShared>& shared,
           JointHandle handle,
           const JointDescription& initial) -> std::unique_ptr<Joint>
{
    return std::make_unique<SteppedJoint>(shared, handle, initial);
}

auto
make_trigger(const std::shared_ptr<WorldShared>& shared,
             TriggerHandle handle,
             const TriggerDescription& initial) -> std::unique_ptr<Trigger>
{
    return std::make_unique<SteppedTrigger>(shared, handle, initial);
}

auto
make_soft_body(const std::shared_ptr<WorldShared>& shared,
               SoftBodyHandle handle,
               const SoftBodyDescription& initial) -> std::unique_ptr<SoftBody>
{
    return std::make_unique<SteppedSoftBody>(shared, handle, initial);
}

} // namespace gfx::physics
