// The Box3D backend's joints: what Box3D can hold (no six-DOF joints, hinge and twist limits within ±0.99π), and each
// kind made of Box3D's — distance (rod, spring, rope; a stub is a spherical joint), hinge (revolute, about the frames' z),
// ball and socket (spherical), slider (prismatic, along the frames' x), fixed (weld), cone-twist (spherical with cone and
// twist limits about the frames' z). The frames are computed once, from the bodies' poses when the joint is applied, and
// kept to make the joint again when it is updated or enabled after breaking.
module;

#include <box3d/box3d.h>

module gfx.physics.backends.box3d;

import std;
import gfx.core.errors;
import gfx.core.math;
import gfx.physics.world;
import :backend;

namespace gfx::physics::box3d_backend
{

namespace
{

/// The widest hinge and twist limits Box3D takes.
constexpr float LIMIT_RANGE = 0.99F * std::numbers::pi_v<float>;

/// A breaking threshold as Box3D takes it (infinite: never).
[[nodiscard]] auto
threshold(float value) noexcept -> float
{
    return std::isfinite(value) ? value : std::numeric_limits<float>::max();
}

/// A length as Box3D takes it (infinite: very long).
[[nodiscard]] auto
finite_length(float value) noexcept -> float
{
    return std::isfinite(value) ? value : B3_HUGE;
}

/// The joint's frame in the world: at @p anchor, its @p local axis turned onto @p axis.
[[nodiscard]] auto
world_frame(Vector3 anchor,
            Vector3 local,
            Vector3 axis) noexcept -> Transform
{
    return {.position = anchor, .orientation = Quaternion::from_rotation_between(local, axis)};
}

} // namespace

void
Box3dBackend::check_joint(const JointDescription& description,
                          std::string_view operation) const
{
    const auto check_limits = [&](const std::optional<JointLimits>& limits, const char* what) {
        if (limits && (limits->lower < -LIMIT_RANGE || limits->upper > LIMIT_RANGE))
        {
            throw_unsupported(identity(), operation, PhysicsCapability::WIDE_JOINT_LIMITS,
                              std::format("Box3D's {} stay within ±{} radians ({} to {} given)", what, LIMIT_RANGE, limits->lower, limits->upper),
                              "narrow the limits, or leave them out", {});
        }
    };
    std::visit(
        [&](const auto& joint) {
            using Joint = std::remove_cvref_t<decltype(joint)>;
            if constexpr (std::same_as<Joint, SixDegreesOfFreedomJointDescription>)
            {
                throw_unsupported(identity(), operation, PhysicsCapability::SIX_DEGREES_OF_FREEDOM_JOINTS, "Box3D has no generic six-degree-of-freedom joint",
                                  "use a CONE_TWIST, HINGE, SLIDER or FIXED joint (or a backend with six-DOF joints)", {.rigid_body = joint.first});
            }
            else if constexpr (std::same_as<Joint, HingeJointDescription>)
            {
                check_limits(joint.limits, "hinge limits");
            }
            else if constexpr (std::same_as<Joint, ConeTwistJointDescription>)
            {
                check_limits(joint.twist_limits, "twist limits");
            }
        },
        description);
}

void
Box3dBackend::create_joint(JointHandle joint,
                           const JointDescription& description,
                           const WorldSnapshot& pending)
{
    // The frames in each body's frame, from the bodies' poses now (the second body's frame is the world's when there is none).
    if (m_joints.size() <= slot(joint))
    {
        m_joints.resize(slot(joint) + 1);
    }
    JointEntry& entry = m_joints[slot(joint)];
    entry = {.handle = joint};
    const auto [first, second] = joint_bodies(description);
    const RigidBodyRecord* first_record = find(pending, first);
    const RigidBodyRecord* second_record = second ? find(pending, second) : nullptr;
    const Transform first_pose = first_record != nullptr ? first_record->state.pose : Transform{};
    const Transform second_pose = second_record != nullptr ? second_record->state.pose : Transform{};
    const auto frames = [&](const Transform& world) {
        entry.frame_first = to_box3d(inverse(first_pose) * world);
        entry.frame_second = to_box3d(inverse(second_pose) * world);
    };
    std::visit(
        [&](const auto& joint_description) {
            using Joint = std::remove_cvref_t<decltype(joint_description)>;
            if constexpr (std::same_as<Joint, DistanceJointDescription>)
            {
                entry.frame_first = to_box3d(Transform{.position = joint_description.anchor_first});
                entry.frame_second = to_box3d(Transform{.position = joint_description.anchor_second});
            }
            else if constexpr (std::same_as<Joint, HingeJointDescription> || std::same_as<Joint, ConeTwistJointDescription>)
            {
                frames(world_frame(joint_description.anchor, {.z = 1.0F}, joint_description.axis));
            }
            else if constexpr (std::same_as<Joint, SliderJointDescription>)
            {
                frames(world_frame(joint_description.anchor, {.x = 1.0F}, joint_description.axis));
            }
            else if constexpr (std::same_as<Joint, BallAndSocketJointDescription> || std::same_as<Joint, FixedJointDescription>)
            {
                frames(Transform{.position = joint_description.anchor});
            }
            else
            {
                frames(joint_description.frame);
            }
            entry.breakable = std::isfinite(joint_description.breaking.force) || std::isfinite(joint_description.breaking.torque);
        },
        description);
    make_joint(entry, description);
}

void
Box3dBackend::make_joint(JointEntry& entry,
                         const JointDescription& description)
{
    // The base every kind shares: the bodies (the ground for the world), the frames, the thresholds, collisions.
    const auto [first, second] = joint_bodies(description);
    const b3BodyId body_a = m_bodies[slot(first)].id;
    const b3BodyId body_b = second ? m_bodies[slot(second)].id : m_ground;
    const auto fill = [&](b3JointDef& base, const auto& joint) {
        base.bodyIdA = body_a;
        base.bodyIdB = body_b;
        base.localFrameA = entry.frame_first;
        base.localFrameB = entry.frame_second;
        base.forceThreshold = threshold(joint.breaking.force);
        base.torqueThreshold = threshold(joint.breaking.torque);
        base.collideConnected = joint.collide_connected;
    };

    // Each kind.
    const b3JointId id = std::visit(
        [&](const auto& joint) -> b3JointId {
            using Joint = std::remove_cvref_t<decltype(joint)>;
            if constexpr (std::same_as<Joint, DistanceJointDescription>)
            {
                if (joint.mode == DistanceMode::STUB)
                {
                    b3SphericalJointDef definition = b3DefaultSphericalJointDef();
                    fill(definition.base, joint);
                    return b3CreateSphericalJoint(m_world, &definition);
                }
                b3DistanceJointDef definition = b3DefaultDistanceJointDef();
                fill(definition.base, joint);
                definition.length = joint.length;
                if (joint.mode == DistanceMode::SPRING)
                {
                    definition.enableSpring = true;
                    definition.hertz = joint.spring.frequency;
                    definition.dampingRatio = joint.spring.damping_ratio;
                    definition.enableLimit = joint.minimum_length > 0.0F || std::isfinite(joint.maximum_length);
                    definition.minLength = joint.minimum_length;
                    definition.maxLength = finite_length(joint.maximum_length);
                }
                else if (joint.mode == DistanceMode::ROPE)
                {
                    // A spring of no stiffness with a limit: slack when shorter than its length.
                    definition.enableSpring = true;
                    definition.hertz = 0.0F;
                    definition.dampingRatio = 0.0F;
                    definition.enableLimit = true;
                    definition.minLength = joint.minimum_length;
                    definition.maxLength = joint.length;
                }
                return b3CreateDistanceJoint(m_world, &definition);
            }
            else if constexpr (std::same_as<Joint, HingeJointDescription>)
            {
                b3RevoluteJointDef definition = b3DefaultRevoluteJointDef();
                fill(definition.base, joint);
                definition.enableLimit = joint.limits.has_value();
                definition.lowerAngle = joint.limits ? joint.limits->lower : 0.0F;
                definition.upperAngle = joint.limits ? joint.limits->upper : 0.0F;
                definition.enableMotor = joint.motor.has_value() && joint.motor->maximum_force > 0.0F;
                definition.maxMotorTorque = joint.motor ? joint.motor->maximum_force : 0.0F;
                definition.motorSpeed = joint.motor ? joint.motor->target_speed : 0.0F;
                definition.enableSpring = joint.spring.frequency > 0.0F;
                definition.hertz = joint.spring.frequency;
                definition.dampingRatio = joint.spring.damping_ratio;
                return b3CreateRevoluteJoint(m_world, &definition);
            }
            else if constexpr (std::same_as<Joint, BallAndSocketJointDescription>)
            {
                b3SphericalJointDef definition = b3DefaultSphericalJointDef();
                fill(definition.base, joint);
                return b3CreateSphericalJoint(m_world, &definition);
            }
            else if constexpr (std::same_as<Joint, SliderJointDescription>)
            {
                b3PrismaticJointDef definition = b3DefaultPrismaticJointDef();
                fill(definition.base, joint);
                definition.enableLimit = joint.limits.has_value();
                definition.lowerTranslation = joint.limits ? joint.limits->lower : 0.0F;
                definition.upperTranslation = joint.limits ? joint.limits->upper : 0.0F;
                definition.enableMotor = joint.motor.has_value() && joint.motor->maximum_force > 0.0F;
                definition.maxMotorForce = joint.motor ? joint.motor->maximum_force : 0.0F;
                definition.motorSpeed = joint.motor ? joint.motor->target_speed : 0.0F;
                definition.enableSpring = joint.spring.frequency > 0.0F;
                definition.hertz = joint.spring.frequency;
                definition.dampingRatio = joint.spring.damping_ratio;
                return b3CreatePrismaticJoint(m_world, &definition);
            }
            else if constexpr (std::same_as<Joint, FixedJointDescription>)
            {
                b3WeldJointDef definition = b3DefaultWeldJointDef();
                fill(definition.base, joint);
                definition.linearHertz = joint.linear_spring.frequency;
                definition.linearDampingRatio = joint.linear_spring.damping_ratio;
                definition.angularHertz = joint.angular_spring.frequency;
                definition.angularDampingRatio = joint.angular_spring.damping_ratio;
                return b3CreateWeldJoint(m_world, &definition);
            }
            else if constexpr (std::same_as<Joint, ConeTwistJointDescription>)
            {
                b3SphericalJointDef definition = b3DefaultSphericalJointDef();
                fill(definition.base, joint);
                definition.enableConeLimit = true;
                definition.coneAngle = joint.swing_limit;
                definition.enableTwistLimit = joint.twist_limits.has_value();
                definition.lowerTwistAngle = joint.twist_limits ? joint.twist_limits->lower : 0.0F;
                definition.upperTwistAngle = joint.twist_limits ? joint.twist_limits->upper : 0.0F;
                return b3CreateSphericalJoint(m_world, &definition);
            }
            else
            {
                // Refused when recorded.
                return b3JointId{};
            }
        },
        description);
    entry.id = id;
    entry.exists = b3Joint_IsValid(id);
    if (entry.exists)
    {
        if (m_joint_owners.size() <= static_cast<std::size_t>(id.index1))
        {
            m_joint_owners.resize(static_cast<std::size_t>(id.index1) + 1);
        }
        m_joint_owners[static_cast<std::size_t>(id.index1)] = entry.handle;
    }
}

void
Box3dBackend::destroy_joint(JointEntry& entry)
{
    if (!entry.exists)
    {
        return;
    }
    if (b3Joint_IsValid(entry.id))
    {
        m_joint_owners[static_cast<std::size_t>(entry.id.index1)] = {};
        b3DestroyJoint(entry.id, true);
    }
    entry.exists = false;
}

} // namespace gfx::physics::box3d_backend
