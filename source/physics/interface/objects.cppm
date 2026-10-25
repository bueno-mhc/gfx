/// @file objects.cppm
/// @brief The objects of a world the application owns (std::unique_ptr): colliders, rigid bodies, joints, triggers and
///        soft bodies.
///
/// Every change (a setter, a force, an impulse, a creation, a destruction) is recorded and applied at the start of the
/// next step, in the order recorded, whatever thread records it and whenever (PhysicsWorld). Getters return the state
/// of the last completed step — an object created since shows what it was created with. Destroying an object (its
/// destructor) is a change like the others. Once the world is destroyed, every operation throws PhysicsError; once a
/// body is destroyed, so do the operations of its colliders, triggers and joints.
export module gfx.physics.interface:objects;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.primitives;
import gfx.physics.shapes;
import gfx.physics.spatial;
import :descriptions;
import :object;

export namespace gfx::physics
{

// ===================================================================================================== colliders

/// @brief A shape attached to a rigid body.
class Collider : public PhysicsObject
{
public:
    /// @brief Its handle.
    [[nodiscard]] virtual auto handle() const noexcept -> ColliderHandle = 0;

    /// @brief The body it is attached to.
    [[nodiscard]] virtual auto body() const noexcept -> RigidBodyHandle = 0;

    /// @brief Its shape (fixed when it is created).
    [[nodiscard]] virtual auto shape() const noexcept -> const std::shared_ptr<const Shape>& = 0;

    /// @brief What it is after the last completed step: its description with the changes applied since.
    /// @throws PhysicsError if the world or its body was destroyed.
    [[nodiscard]] virtual auto description() const -> ColliderDescription = 0;

    /// @brief Its box in world space after the last completed step.
    /// @throws PhysicsError if the world or its body was destroyed.
    [[nodiscard]] virtual auto world_bounds() const -> Aabb = 0;

    /// @brief Moves it on its body (@p pose in the body's frame).
    /// @throws PhysicsError if @p pose is not finite, or the world or its body was destroyed.
    virtual void set_pose(const Transform& pose) = 0;

    /// @brief Changes its friction and restitution.
    /// @throws PhysicsError if a coefficient is negative or not finite, or the restitution is outside [0, 1].
    /// @throws BackendCapabilityError if the backend cannot honor the material (Box3D: different static and dynamic
    ///         friction).
    virtual void set_material(const SurfaceMaterial& material) = 0;

    /// @brief Changes its density (its body's mass properties follow, unless given).
    /// @throws PhysicsError if @p density is negative or not finite.
    virtual void set_density(float density) = 0;

    /// @brief Changes what it is and what it collides with.
    virtual void set_filter(const CollisionFilter& filter) = 0;

    /// @brief Whether its contacts make ContactEvents.
    virtual void set_contact_events(bool enabled) = 0;

protected:
    using PhysicsObject::PhysicsObject;
};

// ===================================================================================================== rigid bodies

/// @brief A rigid body: a frame that moves, with colliders.
class RigidBody : public PhysicsObject
{
public:
    /// @brief Its handle.
    [[nodiscard]] virtual auto handle() const noexcept -> RigidBodyHandle = 0;

    /// @brief What it is after the last completed step: its description with the changes applied since, the pose and
    ///        velocities of its state.
    /// @throws PhysicsError if the world was destroyed.
    [[nodiscard]] virtual auto description() const -> RigidBodyDescription = 0;

    /// @brief Where it is and how it moves after the last completed step.
    /// @throws PhysicsError if the world was destroyed.
    [[nodiscard]] virtual auto state() const -> RigidBodyState = 0;

    /// @brief Its mass properties in its frame after the last completed step: those given, or its colliders' (zero
    ///        for static and kinematic bodies).
    /// @throws PhysicsError if the world was destroyed.
    [[nodiscard]] virtual auto mass_properties() const -> MassProperties = 0;

    /// @brief Attaches a collider.
    /// @throws PhysicsError if @p description is invalid (no shape, a negative density, ...).
    /// @throws BackendCapabilityError if the backend cannot simulate it (Box3D: a mesh, height field or compound on a
    ///         body that is not static).
    [[nodiscard]] virtual auto add_collider(const ColliderDescription& description) -> std::unique_ptr<Collider> = 0;

    // ------------------------------------------------------------------------------------------------ motion

    /// @brief Teleports it.
    /// @throws PhysicsError if @p pose is not finite (any non-zero orientation is normalized).
    virtual void set_pose(const Transform& pose) = 0;

    /// @brief Sets its velocity.
    virtual void set_linear_velocity(Vector3 velocity) = 0;

    /// @brief Sets its angular velocity (radians per second, world axes).
    virtual void set_angular_velocity(Vector3 velocity) = 0;

    /// @brief Sets a kinematic body's velocities so that it reaches @p target in @p seconds.
    /// @throws PhysicsError if the body is not kinematic or @p seconds is not positive.
    virtual void move_kinematic(const Transform& target,
                                float seconds) = 0;

    /// @brief Applies @p force at its center of mass during the next step.
    virtual void apply_force(Vector3 force) = 0;

    /// @brief Applies @p force at the world point @p point during the next step.
    virtual void apply_force_at(Vector3 force,
                                Vector3 point) = 0;

    /// @brief Applies @p torque during the next step.
    virtual void apply_torque(Vector3 torque) = 0;

    /// @brief Changes its velocity by @p impulse at its center of mass.
    virtual void apply_linear_impulse(Vector3 impulse) = 0;

    /// @brief Applies @p impulse at the world point @p point.
    virtual void apply_linear_impulse_at(Vector3 impulse,
                                         Vector3 point) = 0;

    /// @brief Changes its angular velocity by @p impulse.
    virtual void apply_angular_impulse(Vector3 impulse) = 0;

    // ------------------------------------------------------------------------------------------------ configuration

    /// @brief Makes it static, kinematic or dynamic.
    /// @throws BackendCapabilityError if one of its colliders cannot be on such a body.
    virtual void set_kind(BodyKind kind) = 0;

    /// @brief Gives its mass properties (std::nullopt: from its colliders).
    /// @throws PhysicsError if the mass is not positive or the inertia not finite.
    virtual void set_mass_properties(const std::optional<MassProperties>& properties) = 0;

    /// @brief Sets its damping (per second, each at least 0).
    virtual void set_damping(float linear,
                             float angular) = 0;

    /// @brief Sets how much of the world's gravity pulls it.
    virtual void set_gravity_scale(float scale) = 0;

    /// @brief Sets how its collisions are found (CONTINUOUS: along its path).
    virtual void set_motion_quality(MotionQuality quality) = 0;

    /// @brief Sets the world axes it may not move along or about.
    virtual void set_motion_locks(MotionLock locks) = 0;

    /// @brief Allows it to sleep, or keeps it awake.
    virtual void set_sleep_allowed(bool allowed) = 0;

    /// @brief Wakes it (and its island) up.
    virtual void wake_up() = 0;

    /// @brief Puts it (and its island) to sleep.
    virtual void put_to_sleep() = 0;

protected:
    using PhysicsObject::PhysicsObject;
};

// ===================================================================================================== joints

/// @brief A joint between two bodies, or a body and the world.
class Joint : public PhysicsObject
{
public:
    /// @brief Its handle.
    [[nodiscard]] virtual auto handle() const noexcept -> JointHandle = 0;

    /// @brief Its kind (fixed when it is created).
    [[nodiscard]] virtual auto kind() const noexcept -> JointKind = 0;

    /// @brief The bodies it joins (the second null for the world).
    [[nodiscard]] virtual auto bodies() const noexcept -> std::pair<RigidBodyHandle, RigidBodyHandle> = 0;

    /// @brief What it is after the last completed step.
    /// @throws PhysicsError if the world or one of its bodies was destroyed.
    [[nodiscard]] virtual auto description() const -> JointDescription = 0;

    /// @brief How it did in the last completed step (forces, broken).
    /// @throws PhysicsError if the world or one of its bodies was destroyed.
    [[nodiscard]] virtual auto state() const -> JointState = 0;

    /// @brief Changes its limits, motors, springs or breaking thresholds: @p description of the same kind and bodies.
    /// @throws PhysicsError if @p description is invalid, of another kind or joins other bodies.
    virtual void update(const JointDescription& description) = 0;

    /// @brief Enables it (a broken joint again too) or disables it.
    virtual void set_enabled(bool enabled) = 0;

protected:
    using PhysicsObject::PhysicsObject;
};

// ===================================================================================================== triggers

/// @brief A volume reporting the colliders that enter, stay in and leave it (TriggerEvents).
class Trigger : public PhysicsObject
{
public:
    /// @brief Its handle.
    [[nodiscard]] virtual auto handle() const noexcept -> TriggerHandle = 0;

    /// @brief Its volume (fixed when it is created).
    [[nodiscard]] virtual auto shape() const noexcept -> const std::shared_ptr<const Shape>& = 0;

    /// @brief What it is after the last completed step.
    /// @throws PhysicsError if the world or its body was destroyed.
    [[nodiscard]] virtual auto description() const -> TriggerDescription = 0;

    /// @brief Its box in world space after the last completed step.
    [[nodiscard]] virtual auto world_bounds() const -> Aabb = 0;

    /// @brief Moves it (in the world, or in its body's frame).
    virtual void set_pose(const Transform& pose) = 0;

    /// @brief Changes what it detects.
    virtual void set_filter(const CollisionFilter& filter) = 0;

protected:
    using PhysicsObject::PhysicsObject;
};

// ===================================================================================================== soft bodies

/// @brief Particles held together by constraints (cloth, ropes, volumes).
class SoftBody : public PhysicsObject
{
public:
    /// @brief Its handle.
    [[nodiscard]] virtual auto handle() const noexcept -> SoftBodyHandle = 0;

    /// @brief Its particles (fixed when it is created).
    [[nodiscard]] virtual auto particle_count() const noexcept -> std::uint32_t = 0;

    /// @brief What it is after the last completed step: its description with the changes applied since (the particles'
    ///        starting positions are those it was created with).
    /// @throws PhysicsError if the world was destroyed.
    [[nodiscard]] virtual auto description() const -> SoftBodyDescription = 0;

    /// @brief Copies its particles' positions after the last completed step into @p output.
    /// @return The first particle_count() elements of @p output.
    /// @throws PhysicsError if @p output holds fewer than particle_count() elements.
    virtual auto positions(std::span<Vector3> output) const -> std::span<Vector3> = 0;

    /// @brief Copies its particles' velocities after the last completed step into @p output.
    /// @throws PhysicsError if @p output holds fewer than particle_count() elements.
    virtual auto velocities(std::span<Vector3> output) const -> std::span<Vector3> = 0;

    /// @brief The box around its particles after the last completed step.
    [[nodiscard]] virtual auto bounds() const -> Aabb = 0;

    /// @brief Holds a particle to a point of the world or of a body (replaces its pin, if any).
    /// @throws PhysicsError if the particle or the body does not exist.
    virtual void pin(const ParticlePin& pin) = 0;

    /// @brief Releases a particle's pin.
    virtual void unpin(std::uint32_t particle) = 0;

    /// @brief Teleports a particle (its velocity becomes 0).
    virtual void move_particle(std::uint32_t particle,
                               Vector3 position) = 0;

    /// @brief Sets the velocity it loses per second, in [0, 1].
    virtual void set_damping(float damping) = 0;

    /// @brief Sets how much of the world's gravity pulls it.
    virtual void set_gravity_scale(float scale) = 0;

    /// @brief Whether its particles collide with each other.
    virtual void set_self_collision(bool enabled) = 0;

    /// @brief Changes what its particles collide with.
    virtual void set_filter(const CollisionFilter& filter) = 0;

protected:
    using PhysicsObject::PhysicsObject;
};

} // namespace gfx::physics
