// The joints as the GPU solves them: each joint's frames fixed at its creation (as the reference backend fixes them) and
// each kind as a list of parts (GpuJointPart: an XPBD constraint each, joint_solve.comp projects them in order) — motors
// first, then springs, angular parts, and the anchors last (the constraint projected last is the best kept).
module gfx.physics.gpu_xpbd;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.interface;
import :resources;
import :solver;

namespace gfx::physics::gpu_xpbd
{

namespace
{

/// No bound.
constexpr float UNBOUNDED = std::numeric_limits<float>::infinity();

[[nodiscard]] constexpr auto
vector(Vector3 value,
       float w) noexcept -> GpuVector
{
    return {value.x, value.y, value.z, w};
}

/// A part of @p kind about @p axis, bounded by @p lower and @p upper, soft by @p spring.
[[nodiscard]] auto
part(GpuJointPartKind kind,
     std::uint32_t axis,
     Vector3 lower,
     Vector3 upper,
     const JointSpring& spring = {}) noexcept -> GpuJointPart
{
    return {.kind = std::to_underlying(kind), .axis = axis, .reserved = {}, .lower = vector(lower, spring.frequency), .upper = vector(upper, spring.damping_ratio)};
}

/// A part of a single bound pair (DISTANCE, ANGLE, SWING, ALIGNED_AXIS, ORIENTATION).
[[nodiscard]] auto
scalar_part(GpuJointPartKind kind,
            std::uint32_t axis,
            float lower,
            float upper,
            const JointSpring& spring = {}) noexcept -> GpuJointPart
{
    return part(kind, axis, {.x = lower}, {.x = upper}, spring);
}

/// A motor about or along @p axis of the first frame.
[[nodiscard]] auto
motor_part(GpuJointPartKind kind,
           std::uint32_t axis,
           const JointMotor& motor) noexcept -> GpuJointPart
{
    return scalar_part(kind, axis, motor.target_speed, motor.maximum_force);
}

/// The anchors together (all three axes locked).
[[nodiscard]] auto
anchors(const JointSpring& spring = {}) noexcept -> GpuJointPart
{
    return part(GpuJointPartKind::ANCHORS, 0, {}, {}, spring);
}

/// Springs of frequency 0 are rigid: nothing to add for a spring that is not there.
[[nodiscard]] auto
has_spring(const JointSpring& spring) noexcept -> bool
{
    return spring.frequency > 0.0F;
}

} // namespace

auto
joint_frames(const JointDescription& description,
             const Transform& first_pose,
             const Transform& second_pose) noexcept -> JointFrames
{
    // The joint's world frame now, in each body's frame (distance joints' anchors are in the bodies' frames already).
    const bool has_second = joint_bodies(description).second.is_valid();
    const auto frames_of = [&](const Transform& world) {
        return JointFrames{.first = inverse(first_pose) * world, .second = has_second ? inverse(second_pose) * world : world};
    };
    return std::visit(
        [&](const auto& joint) -> JointFrames {
            using Joint = std::remove_cvref_t<decltype(joint)>;
            if constexpr (std::same_as<Joint, DistanceJointDescription>)
            {
                return {.first = {.position = joint.anchor_first}, .second = {.position = joint.anchor_second}};
            }
            else if constexpr (std::same_as<Joint, HingeJointDescription> || std::same_as<Joint, ConeTwistJointDescription>)
            {
                return frames_of({.position = joint.anchor, .orientation = Quaternion::from_rotation_between({.z = 1.0F}, joint.axis)});
            }
            else if constexpr (std::same_as<Joint, SliderJointDescription>)
            {
                return frames_of({.position = joint.anchor, .orientation = Quaternion::from_rotation_between({.x = 1.0F}, joint.axis)});
            }
            else if constexpr (std::same_as<Joint, SixDegreesOfFreedomJointDescription>)
            {
                return frames_of(joint.frame);
            }
            else
            {
                return frames_of({.position = joint.anchor});
            }
        },
        description);
}

void
append_joint_parts(const JointDescription& description,
                   std::vector<GpuJointPart>& parts)
{
    std::visit(
        [&parts](const auto& joint) {
            using Joint = std::remove_cvref_t<decltype(joint)>;
            if constexpr (std::same_as<Joint, DistanceJointDescription>)
            {
                // A stub holds the anchors together; the others keep the distance, within bounds, or softly.
                switch (joint.mode)
                {
                    case DistanceMode::STUB:
                        parts.push_back(anchors());
                        break;
                    case DistanceMode::ROD:
                        parts.push_back(scalar_part(GpuJointPartKind::DISTANCE, 0, joint.length, joint.length));
                        break;
                    case DistanceMode::SPRING:
                        // A spring of frequency 0 pulls nothing: the length is free between the limits.
                        if (has_spring(joint.spring))
                        {
                            parts.push_back(scalar_part(GpuJointPartKind::DISTANCE, 0, joint.length, joint.length, joint.spring));
                        }
                        if (joint.minimum_length > 0.0F || std::isfinite(joint.maximum_length))
                        {
                            parts.push_back(scalar_part(GpuJointPartKind::DISTANCE, 0, joint.minimum_length, joint.maximum_length));
                        }
                        break;
                    case DistanceMode::ROPE:
                        parts.push_back(scalar_part(GpuJointPartKind::DISTANCE, 0, joint.minimum_length, joint.length));
                        break;
                }
            }
            else if constexpr (std::same_as<Joint, HingeJointDescription>)
            {
                // The motor and the spring about z, the axes aligned, the limits, the anchors.
                if (joint.motor)
                {
                    parts.push_back(motor_part(GpuJointPartKind::ANGULAR_MOTOR, 2, *joint.motor));
                }
                if (has_spring(joint.spring))
                {
                    parts.push_back(scalar_part(GpuJointPartKind::ANGLE, 2, 0.0F, 0.0F, joint.spring));
                }
                parts.push_back(scalar_part(GpuJointPartKind::ALIGNED_AXIS, 2, 0.0F, 0.0F));
                if (joint.limits)
                {
                    parts.push_back(scalar_part(GpuJointPartKind::ANGLE, 2, joint.limits->lower, joint.limits->upper));
                }
                parts.push_back(anchors());
            }
            else if constexpr (std::same_as<Joint, BallAndSocketJointDescription>)
            {
                parts.push_back(anchors());
            }
            else if constexpr (std::same_as<Joint, SliderJointDescription>)
            {
                // The motor and the spring along x, the orientation kept, the anchors on the axis within its limits.
                if (joint.motor)
                {
                    parts.push_back(motor_part(GpuJointPartKind::LINEAR_MOTOR, 0, *joint.motor));
                }
                if (has_spring(joint.spring))
                {
                    parts.push_back(part(GpuJointPartKind::ANCHORS, 0, {.y = -UNBOUNDED, .z = -UNBOUNDED}, {.y = UNBOUNDED, .z = UNBOUNDED}, joint.spring));
                }
                parts.push_back(scalar_part(GpuJointPartKind::ORIENTATION, 0, 0.0F, 0.0F));
                const float lower = joint.limits ? joint.limits->lower : -UNBOUNDED;
                const float upper = joint.limits ? joint.limits->upper : UNBOUNDED;
                parts.push_back(part(GpuJointPartKind::ANCHORS, 0, {.x = lower}, {.x = upper}));
            }
            else if constexpr (std::same_as<Joint, FixedJointDescription>)
            {
                parts.push_back(scalar_part(GpuJointPartKind::ORIENTATION, 0, 0.0F, 0.0F, joint.angular_spring));
                parts.push_back(anchors(joint.linear_spring));
            }
            else if constexpr (std::same_as<Joint, ConeTwistJointDescription>)
            {
                // The second frame's z within the cone around the first's, the twist about z within its limits, the anchors.
                parts.push_back(scalar_part(GpuJointPartKind::SWING, 2, 0.0F, joint.swing_limit));
                if (joint.twist_limits)
                {
                    parts.push_back(scalar_part(GpuJointPartKind::ANGLE, 2, joint.twist_limits->lower, joint.twist_limits->upper));
                }
                parts.push_back(anchors());
            }
            else if constexpr (std::same_as<Joint, SixDegreesOfFreedomJointDescription>)
            {
                // Motors on any axis; the rotations (all locked: the orientation kept; else each angle locked at 0 or
                // within its limits); the translations in one part (locked: 0; limited; free: unbounded).
                for (std::uint32_t axis = 0; axis < 6; ++axis)
                {
                    if (joint.motors[axis])
                    {
                        parts.push_back(motor_part(axis < 3 ? GpuJointPartKind::LINEAR_MOTOR : GpuJointPartKind::ANGULAR_MOTOR, axis % 3, *joint.motors[axis]));
                    }
                }
                const auto locked = [&joint](std::uint32_t axis) { return joint.motion[axis] == AxisMotion::LOCKED; };
                if (locked(3) && locked(4) && locked(5))
                {
                    parts.push_back(scalar_part(GpuJointPartKind::ORIENTATION, 0, 0.0F, 0.0F));
                }
                else
                {
                    for (std::uint32_t axis = 3; axis < 6; ++axis)
                    {
                        if (joint.motion[axis] == AxisMotion::LOCKED)
                        {
                            parts.push_back(scalar_part(GpuJointPartKind::ANGLE, axis - 3, 0.0F, 0.0F));
                        }
                        else if (joint.motion[axis] == AxisMotion::LIMITED)
                        {
                            parts.push_back(scalar_part(GpuJointPartKind::ANGLE, axis - 3, joint.limits[axis].lower, joint.limits[axis].upper));
                        }
                    }
                }
                std::array<float, 3> lower{};
                std::array<float, 3> upper{};
                bool translation_bounded = false;
                for (std::uint32_t axis = 0; axis < 3; ++axis)
                {
                    const AxisMotion motion = joint.motion[axis];
                    lower[axis] = motion == AxisMotion::FREE ? -UNBOUNDED : motion == AxisMotion::LIMITED ? joint.limits[axis].lower : 0.0F;
                    upper[axis] = motion == AxisMotion::FREE ? UNBOUNDED : motion == AxisMotion::LIMITED ? joint.limits[axis].upper : 0.0F;
                    translation_bounded = translation_bounded || motion != AxisMotion::FREE;
                }
                if (translation_bounded)
                {
                    parts.push_back(part(GpuJointPartKind::ANCHORS, 0, {.x = lower[0], .y = lower[1], .z = lower[2]}, {.x = upper[0], .y = upper[1], .z = upper[2]}));
                }
            }
        },
        description);
}

auto
keeps_apart(const JointDescription& description) noexcept -> bool
{
    return std::visit([](const auto& joint) { return !joint.collide_connected; }, description);
}

} // namespace gfx::physics::gpu_xpbd
