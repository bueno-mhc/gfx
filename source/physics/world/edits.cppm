/// @file edits.cppm
/// @brief The changes a world records and applies at the start of its next step, in the order recorded (WorldEdit).
///        A backend built on SteppedWorld receives them in SteppedWorldBackend::apply(). Every edit names its object by
///        handle; a creation carries the full description; a destruction is an edit like the others. The values are
///        checked when recorded (descriptions valid, orientations normalized, objects alive), so a backend applies them
///        as they are.
export module gfx.physics.world:edits;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.handles;
import gfx.physics.interface;

export namespace gfx::physics::edits
{

// ===================================================================================================== the world

/// @brief PhysicsWorld::set_gravity().
struct SetGravity
{
    Vector3 gravity{};
};

/// @brief PhysicsWorld::set_solver_description().
struct SetSolverDescription
{
    SolverDescription description{};
};

/// @brief PhysicsWorld::set_sleep_description().
struct SetSleepDescription
{
    SleepDescription description{};
};

// ===================================================================================================== rigid bodies

/// @brief PhysicsWorld::create_rigid_body().
struct CreateRigidBody
{
    RigidBodyHandle body{};
    RigidBodyDescription description{};
};

/// @brief A rigid body's destruction (its colliders, triggers and joints are destroyed by edits recorded before it).
struct DestroyRigidBody
{
    RigidBodyHandle body{};
};

/// @brief RigidBody::set_pose() (normalized orientation).
struct SetBodyPose
{
    RigidBodyHandle body{};
    Transform pose{};
};

/// @brief RigidBody::set_linear_velocity().
struct SetBodyLinearVelocity
{
    RigidBodyHandle body{};
    Vector3 velocity{};
};

/// @brief RigidBody::set_angular_velocity().
struct SetBodyAngularVelocity
{
    RigidBodyHandle body{};
    Vector3 velocity{};
};

/// @brief RigidBody::move_kinematic().
struct MoveKinematicBody
{
    RigidBodyHandle body{};
    Transform target{};
    float seconds = 0.0F;
};

/// @brief RigidBody::apply_force() (no point: at the center of mass) and apply_force_at().
struct ApplyForce
{
    RigidBodyHandle body{};
    Vector3 force{};
    std::optional<Vector3> point{};
};

/// @brief RigidBody::apply_torque().
struct ApplyTorque
{
    RigidBodyHandle body{};
    Vector3 torque{};
};

/// @brief RigidBody::apply_linear_impulse() (no point: at the center of mass) and apply_linear_impulse_at().
struct ApplyLinearImpulse
{
    RigidBodyHandle body{};
    Vector3 impulse{};
    std::optional<Vector3> point{};
};

/// @brief RigidBody::apply_angular_impulse().
struct ApplyAngularImpulse
{
    RigidBodyHandle body{};
    Vector3 impulse{};
};

/// @brief RigidBody::set_kind().
struct SetBodyKind
{
    RigidBodyHandle body{};
    BodyKind kind = BodyKind::DYNAMIC;
};

/// @brief RigidBody::set_mass_properties().
struct SetBodyMassProperties
{
    RigidBodyHandle body{};
    std::optional<MassProperties> properties{};
};

/// @brief RigidBody::set_damping().
struct SetBodyDamping
{
    RigidBodyHandle body{};
    float linear = 0.0F;
    float angular = 0.0F;
};

/// @brief RigidBody::set_gravity_scale().
struct SetBodyGravityScale
{
    RigidBodyHandle body{};
    float scale = 1.0F;
};

/// @brief RigidBody::set_motion_quality().
struct SetBodyMotionQuality
{
    RigidBodyHandle body{};
    MotionQuality quality = MotionQuality::DISCRETE;
};

/// @brief RigidBody::set_motion_locks().
struct SetBodyMotionLocks
{
    RigidBodyHandle body{};
    MotionLock locks = MotionLock::NONE;
};

/// @brief RigidBody::set_sleep_allowed().
struct SetBodySleepAllowed
{
    RigidBodyHandle body{};
    bool allowed = true;
};

/// @brief RigidBody::wake_up() (true) and put_to_sleep() (false).
struct SetBodyAwake
{
    RigidBodyHandle body{};
    bool awake = true;
};

// ===================================================================================================== colliders

/// @brief RigidBody::add_collider().
struct CreateCollider
{
    ColliderHandle collider{};
    RigidBodyHandle body{};
    ColliderDescription description{};
    MassProperties unit_mass{}; ///< The shape's mass properties at density 1, in the shape's frame.
};

/// @brief A collider's destruction.
struct DestroyCollider
{
    ColliderHandle collider{};
};

/// @brief Collider::set_pose().
struct SetColliderPose
{
    ColliderHandle collider{};
    Transform pose{};
};

/// @brief Collider::set_material().
struct SetColliderMaterial
{
    ColliderHandle collider{};
    SurfaceMaterial material{};
};

/// @brief Collider::set_density().
struct SetColliderDensity
{
    ColliderHandle collider{};
    float density = 0.0F;
};

/// @brief Collider::set_filter().
struct SetColliderFilter
{
    ColliderHandle collider{};
    CollisionFilter filter{};
};

/// @brief Collider::set_contact_events().
struct SetColliderContactEvents
{
    ColliderHandle collider{};
    bool enabled = true;
};

// ===================================================================================================== joints

/// @brief PhysicsWorld::create_joint().
struct CreateJoint
{
    JointHandle joint{};
    JointDescription description{};
};

/// @brief A joint's destruction.
struct DestroyJoint
{
    JointHandle joint{};
};

/// @brief Joint::update().
struct UpdateJoint
{
    JointHandle joint{};
    JointDescription description{};
};

/// @brief Joint::set_enabled().
struct SetJointEnabled
{
    JointHandle joint{};
    bool enabled = true;
};

// ===================================================================================================== user constraints

/// @brief PhysicsWorld::create_constraint().
struct CreateConstraint
{
    ConstraintHandle constraint{};
    ConstraintDescription description{};
};

/// @brief A user constraint's destruction (with its bodies too: recorded before theirs).
struct DestroyConstraint
{
    ConstraintHandle constraint{};
};

/// @brief Constraint::set_enabled().
struct SetConstraintEnabled
{
    ConstraintHandle constraint{};
    bool enabled = true;
};

// ===================================================================================================== triggers

/// @brief PhysicsWorld::create_trigger().
struct CreateTrigger
{
    TriggerHandle trigger{};
    TriggerDescription description{};
};

/// @brief A trigger's destruction.
struct DestroyTrigger
{
    TriggerHandle trigger{};
};

/// @brief Trigger::set_pose().
struct SetTriggerPose
{
    TriggerHandle trigger{};
    Transform pose{};
};

/// @brief Trigger::set_filter().
struct SetTriggerFilter
{
    TriggerHandle trigger{};
    CollisionFilter filter{};
};

// ===================================================================================================== soft bodies

/// @brief PhysicsWorld::create_soft_body().
struct CreateSoftBody
{
    SoftBodyHandle soft_body{};
    SoftBodyDescription description{};
};

/// @brief A soft body's destruction.
struct DestroySoftBody
{
    SoftBodyHandle soft_body{};
};

/// @brief SoftBody::pin().
struct PinParticle
{
    SoftBodyHandle soft_body{};
    ParticlePin pin{};
};

/// @brief SoftBody::unpin().
struct UnpinParticle
{
    SoftBodyHandle soft_body{};
    std::uint32_t particle = 0;
};

/// @brief SoftBody::move_particle().
struct MoveParticle
{
    SoftBodyHandle soft_body{};
    std::uint32_t particle = 0;
    Vector3 position{};
};

/// @brief SoftBody::set_damping().
struct SetSoftBodyDamping
{
    SoftBodyHandle soft_body{};
    float damping = 0.0F;
};

/// @brief SoftBody::set_gravity_scale().
struct SetSoftBodyGravityScale
{
    SoftBodyHandle soft_body{};
    float scale = 1.0F;
};

/// @brief SoftBody::set_self_collision().
struct SetSoftBodySelfCollision
{
    SoftBodyHandle soft_body{};
    bool enabled = false;
};

/// @brief SoftBody::set_filter().
struct SetSoftBodyFilter
{
    SoftBodyHandle soft_body{};
    CollisionFilter filter{};
};

} // namespace gfx::physics::edits

export namespace gfx::physics
{

/// @brief Any change a world records.
using WorldEdit = std::variant<
    // The world.
    edits::SetGravity, edits::SetSolverDescription, edits::SetSleepDescription,
    // Rigid bodies.
    edits::CreateRigidBody, edits::DestroyRigidBody, edits::SetBodyPose, edits::SetBodyLinearVelocity, edits::SetBodyAngularVelocity,
    edits::MoveKinematicBody, edits::ApplyForce, edits::ApplyTorque, edits::ApplyLinearImpulse, edits::ApplyAngularImpulse, edits::SetBodyKind,
    edits::SetBodyMassProperties, edits::SetBodyDamping, edits::SetBodyGravityScale, edits::SetBodyMotionQuality, edits::SetBodyMotionLocks,
    edits::SetBodySleepAllowed, edits::SetBodyAwake,
    // Colliders.
    edits::CreateCollider, edits::DestroyCollider, edits::SetColliderPose, edits::SetColliderMaterial, edits::SetColliderDensity,
    edits::SetColliderFilter, edits::SetColliderContactEvents,
    // Joints.
    edits::CreateJoint, edits::DestroyJoint, edits::UpdateJoint, edits::SetJointEnabled,
    // Triggers.
    edits::CreateTrigger, edits::DestroyTrigger, edits::SetTriggerPose, edits::SetTriggerFilter,
    // Soft bodies.
    edits::CreateSoftBody, edits::DestroySoftBody, edits::PinParticle, edits::UnpinParticle, edits::MoveParticle, edits::SetSoftBodyDamping,
    edits::SetSoftBodyGravityScale, edits::SetSoftBodySelfCollision, edits::SetSoftBodyFilter,
    // User constraints.
    edits::CreateConstraint, edits::DestroyConstraint, edits::SetConstraintEnabled>;

} // namespace gfx::physics
